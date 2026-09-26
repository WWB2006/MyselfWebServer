
#include "myself/net/TcpConnection.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

#include "myself/log/Logger.h"
#include "myself/net/EventLoop.h"

namespace myself {

TcpConnection::TcpConnection(EventLoop* loop, int connfd, std::string peerIp,
                             uint16_t peerPort)
    : loop_(loop),
      socket_(connfd),
      channel_(loop, connfd),
      peerIp_(std::move(peerIp)),
      peerPort_(peerPort) {
    channel_.setReadCallback([this] { handleRead(); });
    channel_.setWriteCallback([this] { handleWrite(); });
    channel_.setCloseCallback([this] { handleClose(); });
    channel_.setErrorCallback([this] { handleError(); });
}

TcpConnection::~TcpConnection() {
    if (!closed_) {
        channel_.disableAll();
    }
}

void TcpConnection::start() {
    channel_.tie(shared_from_this());
    channel_.enableReading();
    loop_->updateChannel(&channel_);
}

void TcpConnection::send(const std::string& data) {
    if (closed_) {
        return;
    }
    sendInLoop(data);
}

void TcpConnection::sendInLoop(const std::string& data) {
    size_t remaining = data.size();
    const char* cursor = data.data();

    // 输出缓冲区为空时先尝试直接写，减少一次拷贝
    if (!channel_.isWriting() && output_.readableBytes() == 0) {
        const ssize_t n = ::write(socket_.fd(), cursor, remaining);
        if (n >= 0) {
            cursor += n;
            remaining -= static_cast<size_t>(n);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            LOG_ERROR << "conn write failed: " << std::strerror(errno);
            handleClose();
            return;
        }
    }

    if (remaining > 0) {
        output_.append(cursor, remaining);
        channel_.enableWriting();  // 剩余数据等可写事件继续发送
    }
}

void TcpConnection::shutdownWrite() {
    if (!closed_) {
        ::shutdown(socket_.fd(), SHUT_WR);
    }
}

void TcpConnection::forceClose() {
    handleClose();
}

void TcpConnection::setIdleTimeout(double seconds) {
    idleTimeoutSeconds_ = seconds;
    if (seconds > 0 && !closed_) {
        refreshIdleTimer();
    }
}

void TcpConnection::refreshIdleTimer() {
    if (idleTimeoutSeconds_ <= 0 || closed_) {
        return;
    }
    if (idleTimer_.valid()) {
        loop_->cancelTimer(idleTimer_);
        idleTimer_ = TimerId();
    }

    // 只持有弱引用：连接先关闭时回调什么也不做
    std::weak_ptr<TcpConnection> weak = shared_from_this();
    idleTimer_ = loop_->runAfter(idleTimeoutSeconds_, [weak] {
        if (auto conn = weak.lock()) {
            conn->handleIdleTimeout();
        }
    });
}

void TcpConnection::handleIdleTimeout() {
    if (closed_) {
        return;
    }
    LOG_WARN << "timeout fd=" << socket_.fd() << " peer=" << peerIp_ << ":" << peerPort_
             << " idle over " << idleTimeoutSeconds_ << "s, closing";
    handleClose();
}

void TcpConnection::handleRead() {
    int savedErrno = 0;
    bool peerClosed = false;
    bool activity = false;

    // ET 模式：一次事件必须循环读到 EAGAIN
    for (;;) {
        const ssize_t n = input_.readFd(socket_.fd(), &savedErrno);
        if (n > 0) {
            activity = true;
            if (messageCallback_) {
                messageCallback_(shared_from_this(), &input_);
            }
            continue;
        }
        if (n == 0) {
            peerClosed = true;
            break;
        }
        if (savedErrno == EAGAIN || savedErrno == EWOULDBLOCK) {
            break;
        }
        if (savedErrno == EINTR) {
            continue;
        }
        LOG_ERROR << "conn read failed: " << std::strerror(savedErrno);
        handleError();
        return;
    }

    if (activity) {
        refreshIdleTimer();  // 有数据往来就刷新空闲计时
    }

    if (peerClosed) {
        handleClose();
    }
}

void TcpConnection::handleWrite() {
    if (output_.readableBytes() == 0) {
        channel_.disableWriting();
        return;
    }

    const ssize_t n = ::write(socket_.fd(), output_.peek(), output_.readableBytes());
    if (n > 0) {
        output_.retrieve(static_cast<size_t>(n));
        if (output_.readableBytes() == 0) {
            channel_.disableWriting();
        }
        return;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        LOG_ERROR << "conn write failed: " << std::strerror(errno);
        handleClose();
    }
}

void TcpConnection::handleClose() {
    if (closed_) {
        return;
    }
    closed_ = true;

    if (idleTimer_.valid()) {
        loop_->cancelTimer(idleTimer_);  // 先取消空闲定时器，避免回调访问已关闭连接
        idleTimer_ = TimerId();
    }

    channel_.disableAll();  // 不再关心任何事件
    channel_.remove();      // 从 epoll 与事件循环注销
    socket_.close();

    if (closeCallback_) {
        closeCallback_(shared_from_this());
    }
}

void TcpConnection::handleError() {
    int error = 0;
    socklen_t len = sizeof(error);
    if (::getsockopt(socket_.fd(), SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        error = errno;
    }
    LOG_ERROR << "conn socket error on fd=" << socket_.fd() << ": "
              << std::strerror(error);
    handleClose();
}

}  // namespace myself

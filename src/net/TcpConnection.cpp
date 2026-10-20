
#include "myself/net/TcpConnection.h"

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#include "myself/log/Logger.h"
#include "myself/net/EventLoop.h"
#include "myself/util/Metrics.h"

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
            myself::metrics::bytesWritten(static_cast<size_t>(n));
            cursor += n;
            remaining -= static_cast<size_t>(n);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            LOG_ERROR << "conn write failed: " << std::strerror(errno);
            myself::metrics::error();
            handleClose();
            return;
        }
    }

    if (remaining > 0) {
        output_.append(cursor, remaining);
        channel_.enableWriting();  // 剩余数据等可写事件继续发送
    }
}

void TcpConnection::sendFile(const FileBodyPtr& file) {
    if (closed_ || !file || file->length() == 0) {
        return;
    }

    if (pendingFile_) {
        // 上一个大文件还在发：退化成内存缓冲，避免两段 sendfile 互相覆盖状态。
        // 罕见路径（需要 pipelining 两个大文件），正确性优先。
        LOG_WARN << "zero-copy busy on fd=" << socket_.fd() << ", fallback to buffered copy";
        std::string data(file->length(), '\0');
        size_t total = 0;
        while (total < file->length()) {
            const ssize_t n = ::pread(file->fd(), &data[total], file->length() - total,
                                      file->offset() + static_cast<off_t>(total));
            if (n > 0) {
                total += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;  // 读失败或提前 EOF：按实际读到的长度发送
        }
        data.resize(total);
        sendInLoop(data);
        return;
    }

    // 此刻输出缓冲里的字节（响应头，可能还有更早的响应）必须先落地
    preFileBytes_ = output_.readableBytes();
    pendingFile_ = file;
    fileOffset_ = file->offset();
    fileRemaining_ = file->length();

    channel_.enableWriting();
    handleWrite();  // 立刻推进一次，省掉一次 epoll 事件往返
}

void TcpConnection::shutdownWrite() {
    if (!closed_) {
        if (!channel_.isWriting() && output_.readableBytes() == 0 && !pendingFile_) {
            ::shutdown(socket_.fd(), SHUT_WR);
        } else {
            // 输出缓冲区/待发文件还有数据，延迟到 handleWrite 冲刷完毕后再执行半关闭，
            // 避免提前关闭写端触发 EPIPE (Broken pipe) 与 TCP RST
            shutdownAfterWrite_ = true;
        }
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
            myself::metrics::bytesRead(static_cast<size_t>(n));
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
        myself::metrics::error();
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
    if (closed_) {
        return;
    }

    // 阶段 1：文件体之前的字节（响应头、更早的响应）必须先写完
    if (preFileBytes_ > 0) {
        if (!flushOutput(preFileBytes_)) {
            return;
        }
        preFileBytes_ = 0;
    }

    // 阶段 2：文件体走内核态直接对拷（PageCache -> socket，不经过用户态）
    if (pendingFile_) {
        if (!sendPendingFile()) {
            return;
        }
    }

    // 阶段 3：文件之后可能还有 pipelining 攒下的响应
    if (output_.readableBytes() > 0) {
        if (!flushOutput(output_.readableBytes())) {
            return;
        }
    }

    channel_.disableWriting();
    if (shutdownAfterWrite_) {
        ::shutdown(socket_.fd(), SHUT_WR);
    }
}

bool TcpConnection::flushOutput(size_t limit) {
    while (limit > 0 && output_.readableBytes() > 0) {
        const size_t chunk = std::min(limit, output_.readableBytes());
        const ssize_t n = ::write(socket_.fd(), output_.peek(), chunk);
        if (n > 0) {
            const size_t written = static_cast<size_t>(n);
            myself::metrics::bytesWritten(written);
            output_.retrieve(written);
            if (preFileBytes_ > 0) {
                preFileBytes_ -= std::min(preFileBytes_, written);
            }
            limit -= written;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;  // 内核发送缓冲已满，等下一次可写事件
        }
        LOG_ERROR << "conn write failed: " << std::strerror(errno);
        myself::metrics::error();
        handleClose();
        return false;
    }
    return true;
}

bool TcpConnection::sendPendingFile() {
    // ET 模式：一次可写事件必须把文件尽量发完，否则不会再收到 EPOLLOUT
    while (fileRemaining_ > 0) {
        const ssize_t n =
            ::sendfile(socket_.fd(), pendingFile_->fd(), &fileOffset_, fileRemaining_);
        if (n > 0) {
            fileRemaining_ -= static_cast<size_t>(n);
            myself::metrics::bytesWritten(static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            // 文件在发送途中被截断：按已发完处理，避免死循环
            LOG_WARN << "sendfile hit EOF early on fd=" << socket_.fd()
                     << ", remaining=" << fileRemaining_;
            fileRemaining_ = 0;
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        LOG_ERROR << "sendfile failed: " << std::strerror(errno);
        myself::metrics::error();
        handleClose();
        return false;
    }

    pendingFile_.reset();  // 最后一份引用析构 -> 关闭文件 fd
    fileRemaining_ = 0;
    return true;
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

    // 释放待发文件：fd 由 FileBody 的析构关闭，避免连接中途断开时泄漏
    pendingFile_.reset();
    fileRemaining_ = 0;
    preFileBytes_ = 0;

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
    myself::metrics::error();
    handleClose();
}

}  // namespace myself

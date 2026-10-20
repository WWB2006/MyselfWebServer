#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "myself/net/Buffer.h"
#include "myself/net/Channel.h"
#include "myself/net/Socket.h"
#include "myself/timer/Timer.h"
#include "myself/util/FileBody.h"

namespace myself {

class EventLoop;

/// 一条 TCP 连接：持有 socket、Channel 与收发缓冲区。
/// 生命周期用 shared_ptr 管理，回调期间通过 Channel::tie 保活。
/// 所有成员函数都必须在连接所属的 IO 线程调用（阶段 4 起）。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using MessageCallback =
        std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*)>;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;

    TcpConnection(EventLoop* loop, int connfd, std::string peerIp, uint16_t peerPort);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void setMessageCallback(MessageCallback cb) { messageCallback_ = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { closeCallback_ = std::move(cb); }

    /// 注册读事件，开始接收数据。
    void start();

    /// 发送数据；写不完的部分进入输出缓冲区并注册写事件。
    void send(const std::string& data);

    /// 零拷贝发送一段文件（sendfile(2)）。必须紧跟在 send(响应头) 之后调用：
    /// 文件数据会排在「当前已缓冲的所有字节」之后、「后续 send」之前。
    /// 上一个大文件还没发完时会自动退化成内存缓冲，保证响应顺序不乱。
    void sendFile(const FileBodyPtr& file);

    void shutdownWrite();
    void forceClose();

    /// 设置空闲超时（秒）；>0 时开启定时器，超时未收到数据自动断开。
    void setIdleTimeout(double seconds);

    int fd() const { return socket_.fd(); }
    const std::string& peerIp() const { return peerIp_; }
    uint16_t peerPort() const { return peerPort_; }
    bool connected() const { return !closed_; }

    /// 该连接所属的事件循环。跨线程投递任务时用它回到正确的 IO 线程。
    EventLoop* loop() const { return loop_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& data);

    /// 从输出缓冲写出至多 limit 字节；返回 false 表示遇到 EAGAIN 或已关闭
    bool flushOutput(size_t limit);
    /// 用 sendfile 推进待发文件；返回 false 表示遇到 EAGAIN 或已关闭
    bool sendPendingFile();

    void refreshIdleTimer();
    void handleIdleTimeout();

    EventLoop* loop_;
    Socket socket_;
    Channel channel_;
    Buffer input_;
    Buffer output_;
    std::string peerIp_;
    uint16_t peerPort_;
    bool closed_{false};
    bool shutdownAfterWrite_{false};
    // 零拷贝：文件体必须排在它前面那批字节（响应头/更早的响应）之后
    FileBodyPtr pendingFile_;
    off_t fileOffset_{0};
    size_t fileRemaining_{0};
    size_t preFileBytes_{0};
    double idleTimeoutSeconds_{0.0};
    TimerId idleTimer_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
};

}  // namespace myself

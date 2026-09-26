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

namespace myself {

class EventLoop;

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

    void start();
    void send(const std::string& data);
    void shutdownWrite();
    void forceClose();

    /// 设置空闲超时（秒）；<=0 表示关闭。必须在所属 IO 线程调用。
    void setIdleTimeout(double seconds);

    int fd() const { return socket_.fd(); }
    const std::string& peerIp() const { return peerIp_; }
    uint16_t peerPort() const { return peerPort_; }
    bool connected() const { return !closed_; }
    EventLoop* loop() const { return loop_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& data);
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
    double idleTimeoutSeconds_{0};
    TimerId idleTimer_;
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
};

}  // namespace myself

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "myself/net/Buffer.h"
#include "myself/net/Channel.h"
#include "myself/net/Socket.h"

namespace myself {

class EventLoop;

/// 一条 TCP 连接：持有 socket、Channel 与收发缓冲区。
/// 生命周期用 shared_ptr 管理，回调期间通过 Channel::tie 保活。
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

    void shutdownWrite();
    void forceClose();

    int fd() const { return socket_.fd(); }
    const std::string& peerIp() const { return peerIp_; }
    uint16_t peerPort() const { return peerPort_; }
    bool connected() const { return !closed_; }

private:
    void handleRead();
    void handleWrite();
    void handleClose();
    void handleError();
    void sendInLoop(const std::string& data);

    EventLoop* loop_;
    Socket socket_;
    Channel channel_;
    Buffer input_;
    Buffer output_;
    std::string peerIp_;
    uint16_t peerPort_;
    bool closed_{false};
    MessageCallback messageCallback_;
    CloseCallback closeCallback_;
};

}  // namespace myself


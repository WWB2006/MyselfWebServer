#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "myself/net/Channel.h"
#include "myself/net/Socket.h"

namespace myself {

class EventLoop;

/// 监听套接字的封装：只负责 accept，新连接交给回调处理。
class Acceptor {
public:
    using NewConnectionCallback =
        std::function<void(int connfd, std::string peerIp, uint16_t peerPort)>;

    Acceptor(EventLoop* loop, const std::string& ip, int port);
    ~Acceptor();

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;

    void setNewConnectionCallback(NewConnectionCallback cb) {
        newConnectionCallback_ = std::move(cb);
    }

    void listen();
    int port() const { return port_; }

private:
    void handleRead();

    EventLoop* loop_;
    Socket listenSocket_;
    Channel channel_;
    NewConnectionCallback newConnectionCallback_;
    int port_;
};

}  // namespace myself


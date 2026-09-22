#include "myself/net/Acceptor.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "myself/net/EventLoop.h"

namespace myself {

Acceptor::Acceptor(EventLoop* loop, const std::string& ip, int port)
    : loop_(loop),
      listenSocket_(Socket::listenOn(ip, port, 128)),
      channel_(loop, listenSocket_.fd()),
      port_(port) {
    channel_.setReadCallback([this] { handleRead(); });
}

Acceptor::~Acceptor() {
    channel_.disableAll();
}

void Acceptor::listen() {
    channel_.enableReading();
    std::cout << "[acceptor] listening on port " << port_ << "\n";
}

void Acceptor::handleRead() {
    // ET 模式：一次可读事件要把新连接全部取走，直到 EAGAIN
    for (;;) {
        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);

        const int connfd = ::accept4(listenSocket_.fd(), reinterpret_cast<sockaddr*>(&peer),
                                     &peerLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (connfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 新连接已取完
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[acceptor] accept failed: " << std::strerror(errno) << "\n";
            break;
        }

        std::string ip = ::inet_ntoa(peer.sin_addr);
        const uint16_t peerPort = ntohs(peer.sin_port);
        if (newConnectionCallback_) {
            newConnectionCallback_(connfd, ip, peerPort);
        } else {
            ::close(connfd);  // 没有处理器，直接关闭避免 fd 泄漏
        }
    }
}

}  // namespace myself


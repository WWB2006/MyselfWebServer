// 阶段 1：阻塞式单连接回显服务器。
// 故意保留“一次只能服务一个客户端”的限制，阶段 2 会用 epoll 解决。

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "myself/net/Socket.h"
#include "myself/util/Version.h"

namespace {

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [-p port] [-h]\n"
              << "  -p port   监听端口，默认 8080\n"
              << "  -h        显示帮助\n";
}

/// 阻塞地处理一个连接，把收到的数据原样回写。
void handleConnection(int connfd) {
    char buffer[4096];

    for (;;) {
        const ssize_t n = ::read(connfd, buffer, sizeof(buffer));
        if (n > 0) {
            const ssize_t written = ::write(connfd, buffer, static_cast<size_t>(n));
            if (written < 0) {
                std::cerr << "[conn] write failed: " << std::strerror(errno) << "\n";
                return;
            }
            continue;
        }
        if (n == 0) {
            std::cout << "[conn] client closed\n";
            return;
        }
        if (errno == EINTR) {
            continue;  // 被信号打断，重试
        }
        std::cerr << "[conn] read failed: " << std::strerror(errno) << "\n";
        return;
    }
}

void serveLoop(const myself::Socket& listenSock) {
    for (;;) {
        sockaddr_in peer{};
        socklen_t peerLen = sizeof(peer);

        const int connfd =
            ::accept(listenSock.fd(), reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "[accept] failed: " << std::strerror(errno) << "\n";
            continue;
        }

        std::cout << "[accept] client " << ::inet_ntoa(peer.sin_addr)
                  << " connected, fd=" << connfd << "\n";

        handleConnection(connfd);

        ::close(connfd);
        std::cout << "[accept] fd=" << connfd << " closed\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (arg == "-p" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
            continue;
        }
        std::cerr << "unknown argument: " << arg << "\n";
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    // 对端提前关闭时不要让进程直接退出（阶段 9 会做更完整的信号处理）
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const myself::Socket listenSock = myself::Socket::listenOn("0.0.0.0", port, 128);
        std::cout << myself::buildInfo() << " listening on 0.0.0.0:" << port << "\n";
        serveLoop(listenSock);
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

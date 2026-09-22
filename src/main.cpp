// 阶段 2：非阻塞 + epoll 的单线程 Reactor 回显服务器。
// 与阶段 1 的区别：一个线程可以同时服务多个连接，不再被单个连接阻塞。

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "myself/net/Acceptor.h"
#include "myself/net/EventLoop.h"
#include "myself/net/TcpConnection.h"
#include "myself/util/Version.h"

namespace {

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [-p port] [-h]\n"
              << "  -p port   监听端口，默认 8080\n"
              << "  -h        显示帮助\n";
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

    std::signal(SIGPIPE, SIG_IGN);

    try {
        myself::EventLoop loop;
        myself::Acceptor acceptor(&loop, "0.0.0.0", port);

        // 连接表：fd -> 连接对象，连接关闭时移除
        std::map<int, std::shared_ptr<myself::TcpConnection>> connections;

        acceptor.setNewConnectionCallback(
            [&loop, &connections](int connfd, std::string ip, uint16_t peerPort) {
                auto conn =
                    std::make_shared<myself::TcpConnection>(&loop, connfd, ip, peerPort);
                const int fd = conn->fd();

                conn->setMessageCallback(
                    [](const std::shared_ptr<myself::TcpConnection>& c, myself::Buffer* input) {
                        const std::string data = input->retrieveAllAsString();
                        std::cout << "[echo] " << data.size() << " bytes from " << c->peerIp()
                                  << ":" << c->peerPort() << "\n";
                        c->send(data);  // 原样回写
                    });

                conn->setCloseCallback(
                    [&connections, fd](const std::shared_ptr<myself::TcpConnection>& c) {
                        std::cout << "[close] fd=" << fd << " peer=" << c->peerIp() << ":"
                                  << c->peerPort() << "\n";
                        connections.erase(fd);
                    });

                connections.emplace(fd, conn);
                conn->start();

                std::cout << "[accept] fd=" << fd << " peer=" << ip << ":" << peerPort
                          << ", online=" << connections.size() << "\n";
            });

        acceptor.listen();
        std::cout << myself::buildInfo() << " epoll reactor started\n";
        loop.loop();
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


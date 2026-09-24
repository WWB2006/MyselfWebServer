// 阶段 4：主从 Reactor + 线程池。
// 主线程只做 Acceptor，读写事件由多个 IO 线程分担；可选的 worker 线程池负责耗时任务。

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>

#include "myself/http/HttpParser.h"
#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"
#include "myself/http/Router.h"
#include "myself/net/Acceptor.h"
#include "myself/net/EventLoop.h"
#include "myself/net/EventLoopThreadPool.h"
#include "myself/net/TcpConnection.h"
#include "myself/thread/ThreadPool.h"
#include "myself/util/Version.h"

namespace {

struct ServerOptions {
    int port{8080};
    std::string wwwRoot{"www"};
    size_t ioThreads{4};      // 0 表示单线程（阶段 3 的行为，便于做对比压测）
    size_t workerThreads{0};  // >0 时把静态文件处理放到工作线程池
};

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [-p port] [-r wwwRoot] [-t ioThreads] "
                 "[-w workerThreads] [-h]\n"
              << "  -p port          监听端口，默认 8080\n"
              << "  -r wwwRoot       静态资源目录，默认 www\n"
              << "  -t ioThreads     IO 线程数，默认 4，0 表示单线程\n"
              << "  -w workerThreads 计算线程池大小，默认 0（关闭）\n"
              << "  -h               显示帮助\n";
}

enum class ParseResult { kOk, kExit, kError };

ParseResult parseArgs(int argc, char* argv[], ServerOptions* options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return ParseResult::kExit;
        }
        const bool hasValue = (i + 1) < argc;
        if (arg == "-p" && hasValue) {
            options->port = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "-r" && hasValue) {
            options->wwwRoot = argv[++i];
            continue;
        }
        if (arg == "-t" && hasValue) {
            options->ioThreads = static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
            continue;
        }
        if (arg == "-w" && hasValue) {
            options->workerThreads =
                static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
            continue;
        }
        std::cerr << "unknown or incomplete argument: " << arg << "\n";
        printUsage(argv[0]);
        return ParseResult::kError;
    }
    return ParseResult::kOk;
}

/// 连接表：accept 在主线程，close 在各自的 IO 线程，因此需要加锁。
class ConnectionRegistry {
public:
    void add(int fd, const std::shared_ptr<myself::TcpConnection>& conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_[fd] = conn;
    }

    void remove(int fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.erase(fd);
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return connections_.size();
    }

    /// 退出前调用：把每条连接交回它所属的 IO 线程关闭，避免在错误的线程析构。
    void closeAll() {
        std::map<int, std::shared_ptr<myself::TcpConnection>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(connections_);
        }
        for (auto& item : pending) {
            const std::shared_ptr<myself::TcpConnection> conn = item.second;
            myself::EventLoop* loop = conn->loop();
            loop->runInLoop([conn] { conn->forceClose(); });
        }
    }

private:
    mutable std::mutex mutex_;
    std::map<int, std::shared_ptr<myself::TcpConnection>> connections_;
};

}  // namespace

int main(int argc, char* argv[]) {
    ServerOptions options;
    switch (parseArgs(argc, argv, &options)) {
        case ParseResult::kExit:
            return EXIT_SUCCESS;
        case ParseResult::kError:
            return EXIT_FAILURE;
        case ParseResult::kOk:
            break;
    }

    std::signal(SIGPIPE, SIG_IGN);

    try {
        myself::EventLoop baseLoop;   // 主线程：只负责 accept
        baseLoop.setName("main");

        myself::EventLoopThreadPool ioPool(&baseLoop, "io");
        ioPool.setThreadCount(options.ioThreads);
        ioPool.start();

        myself::ThreadPool workerPool(options.workerThreads, "worker");
        if (options.workerThreads > 0) {
            workerPool.start();
        }

        myself::Router router(options.wwwRoot);
        router.registerHandler("/api/status", [&ioPool, &workerPool](const myself::HttpRequest&) {
            myself::HttpResponse response;
            response.setStatus(200, "OK");
            response.setContentType("application/json; charset=utf-8");
            response.setBody(std::string("{\"name\":\"MyselfWebServer\",\"version\":\"") +
                             myself::version() + "\",\"ioThreads\":" +
                             std::to_string(ioPool.threadCount()) + ",\"workerThreads\":" +
                             std::to_string(workerPool.threadCount()) + ",\"status\":\"ok\"}");
            return response;
        });

        ConnectionRegistry registry;
        myself::Acceptor acceptor(&baseLoop, "0.0.0.0", options.port);

        acceptor.setNewConnectionCallback([&](int connfd, std::string ip, uint16_t peerPort) {
            myself::EventLoop* ioLoop = ioPool.nextLoop();

            // 连接必须在它所属的 IO 线程里创建并注册事件，否则就是跨线程操作 epoll
            ioLoop->runInLoop([&router, &workerPool, &registry, connfd, ip, peerPort, ioLoop] {
                auto conn = std::make_shared<myself::TcpConnection>(ioLoop, connfd, ip, peerPort);
                const int fd = conn->fd();
                auto parser = std::make_shared<myself::HttpParser>();

                conn->setMessageCallback(
                    [&router, &workerPool, parser, ioLoop](
                        const std::shared_ptr<myself::TcpConnection>& c,
                        myself::Buffer* input) {
                        for (;;) {
                            const auto result = parser->parse(input);
                            if (result == myself::HttpParser::Result::kBadRequest) {
                                myself::HttpResponse response =
                                    myself::HttpResponse::badRequest("Bad Request");
                                c->send(response.serialize());
                                c->shutdownWrite();
                                return;
                            }
                            if (result != myself::HttpParser::Result::kComplete) {
                                return;  // 半包：等下一次可读事件
                            }

                            const myself::HttpRequest request = parser->takeRequest();
                            const bool headOnly =
                                request.method() == myself::HttpRequest::Method::kHead;
                            const bool keepAlive = request.isKeepAlive();

                            // 统一出口：无论谁算出响应，最终都在 IO 线程里写回
                            auto respond = [c, headOnly, keepAlive, ioLoop](
                                               myself::HttpResponse response) {
                                response.setCloseConnection(!keepAlive);
                                c->send(response.serialize(!headOnly));
                                std::cout << "[http] -> " << response.statusCode()
                                          << " on " << ioLoop->name() << "\n";
                                if (!keepAlive) {
                                    c->shutdownWrite();
                                }
                            };

                            const bool offload = workerPool.threadCount() > 0 &&
                                                 request.path().rfind("/api/", 0) != 0;
                            if (offload) {
                                // 文件读取等耗时操作交给 worker 线程，回写再回到 IO 线程
                                workerPool.submit([&router, request, ioLoop, respond] {
                                    myself::HttpResponse response = router.handle(request);
                                    ioLoop->runInLoop(
                                        [respond, response]() mutable { respond(response); });
                                });
                            } else {
                                respond(router.handle(request));
                            }

                            if (!keepAlive) {
                                return;  // 短连接：响应写完就结束本次解析
                            }
                        }
                    });

                conn->setCloseCallback(
                    [&registry, fd](const std::shared_ptr<myself::TcpConnection>& c) {
                        std::cout << "[close] fd=" << fd << " peer=" << c->peerIp() << ":"
                                  << c->peerPort() << "\n";
                        registry.remove(fd);
                    });

                registry.add(fd, conn);
                conn->start();

                std::cout << "[accept] fd=" << fd << " peer=" << ip << ":" << peerPort
                          << " -> " << ioLoop->name() << ", online=" << registry.size()
                          << "\n";
            });
        });

        acceptor.listen();
        std::cout << myself::buildInfo() << " http server started, io threads="
                  << options.ioThreads << ", worker threads=" << options.workerThreads
                  << ", www root=" << options.wwwRoot << "\n";

        baseLoop.loop();

        // 退出：先让每条连接在自己的 IO 线程里关闭，再回收线程池
        registry.closeAll();
        workerPool.stop();
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

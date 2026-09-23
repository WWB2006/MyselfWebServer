// 阶段 3：在 Reactor 之上接入 HTTP 解析、静态文件服务与简单路由。

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "myself/http/HttpParser.h"
#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"
#include "myself/http/Router.h"
#include "myself/net/Acceptor.h"
#include "myself/net/EventLoop.h"
#include "myself/net/TcpConnection.h"
#include "myself/util/Version.h"

namespace {

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [-p port] [-r wwwRoot] [-h]\n"
              << "  -p port     监听端口，默认 8080\n"
              << "  -r wwwRoot  静态资源目录，默认 www\n"
              << "  -h          显示帮助\n";
}

/// 每条连接的状态：连接对象 + 独立的 HTTP 解析器。
struct ConnectionContext {
    std::shared_ptr<myself::TcpConnection> conn;
    myself::HttpParser parser;
};

}  // namespace

int main(int argc, char* argv[]) {
    int port = 8080;
    std::string wwwRoot = "www";

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
        if (arg == "-r" && i + 1 < argc) {
            wwwRoot = argv[++i];
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

        myself::Router router(wwwRoot);
        // 运行状态接口：阶段 7 会扩展成完整的指标接口
        router.registerHandler("/api/status", [](const myself::HttpRequest&) {
            myself::HttpResponse response;
            response.setStatus(200, "OK");
            response.setContentType("application/json; charset=utf-8");
            response.setBody(std::string("{\"name\":\"MyselfWebServer\",\"version\":\"") +
                             myself::version() + "\",\"status\":\"ok\"}");
            return response;
        });

        std::map<int, ConnectionContext> connections;

        acceptor.setNewConnectionCallback(
            [&loop, &connections, &router](int connfd, std::string ip, uint16_t peerPort) {
                auto conn =
                    std::make_shared<myself::TcpConnection>(&loop, connfd, ip, peerPort);
                const int fd = conn->fd();

                conn->setMessageCallback(
                    [&router, &connections](const std::shared_ptr<myself::TcpConnection>& c,
                                            myself::Buffer* input) {
                        auto it = connections.find(c->fd());
                        if (it == connections.end()) {
                            return;
                        }
                        myself::HttpParser& parser = it->second.parser;

                        // 循环解析，支持一次收到多条请求（pipelining）与半包
                        for (;;) {
                            const auto result = parser.parse(input);
                            if (result == myself::HttpParser::Result::kBadRequest) {
                                myself::HttpResponse response =
                                    myself::HttpResponse::badRequest("Bad Request");
                                c->send(response.serialize());
                                c->shutdownWrite();
                                return;
                            }
                            if (result != myself::HttpParser::Result::kComplete) {
                                return;  // 数据不完整，等下一次可读事件
                            }

                            const myself::HttpRequest request = parser.takeRequest();
                            myself::HttpResponse response = router.handle(request);
                            response.setCloseConnection(!request.isKeepAlive());

                            const bool headOnly =
                                request.method() == myself::HttpRequest::Method::kHead;
                            c->send(response.serialize(!headOnly));

                            std::cout << "[http] " << myself::HttpRequest::methodToString(
                                             request.method())
                                      << " " << request.path() << " -> "
                                      << response.statusCode() << "\n";

                            if (!request.isKeepAlive()) {
                                c->shutdownWrite();  // 短连接：响应发完即关闭写端
                                return;
                            }
                        }
                    });

                conn->setCloseCallback(
                    [&connections, fd](const std::shared_ptr<myself::TcpConnection>& c) {
                        std::cout << "[close] fd=" << fd << " peer=" << c->peerIp() << ":"
                                  << c->peerPort() << "\n";
                        connections.erase(fd);
                    });

                connections.emplace(fd, ConnectionContext{conn, myself::HttpParser()});
                conn->start();

                std::cout << "[accept] fd=" << fd << " peer=" << ip << ":" << peerPort
                          << ", online=" << connections.size() << "\n";
            });

        acceptor.listen();
        std::cout << myself::buildInfo() << " http server started, www root=" << wwwRoot
                  << "\n";
        loop.loop();
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


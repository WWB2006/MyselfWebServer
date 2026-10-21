// 阶段 9：性能与健壮性加固。
// 新增 -m 过载保护（连接数超限返回 503）与 SIGINT/SIGTERM 优雅退出，
// 指标侧补充连接拒绝计数，供压测矩阵与故障注入脚本核对。

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "myself/http/HttpParser.h"
#include "myself/http/HttpRequest.h"
#include "myself/http/HttpResponse.h"
#include "myself/http/ReverseProxy.h"
#include "myself/http/Router.h"
#include "myself/log/AsyncLogging.h"
#include "myself/log/LogFile.h"
#include "myself/log/Logger.h"
#include "myself/net/Acceptor.h"
#include "myself/net/EventLoop.h"
#include "myself/net/EventLoopThreadPool.h"
#include "myself/net/TcpConnection.h"
#include "myself/thread/ThreadPool.h"
#include "myself/util/Metrics.h"
#include "myself/util/TokenBucket.h"
#include "myself/util/Version.h"

namespace {

/// 日志输出目标：可选异步文件 + 控制台。用全局指针是因为 Logger 的输出函数
/// 是普通函数指针，不能捕获局部变量。
myself::AsyncLogging* g_asyncLog = nullptr;
bool g_consoleOutput = true;
std::mutex g_consoleMutex;
std::atomic<bool> g_shutdownRequested{false};

/// 信号处理函数里只能做异步信号安全的操作，因此只设置标志，
/// 真正的收尾由事件循环里的定时任务完成。
void onTerminationSignal(int) {
    g_shutdownRequested.store(true);
}

void logOutput(const char* data, size_t len) {
    if (g_asyncLog != nullptr) {
        g_asyncLog->append(data, len);
    }
    if (g_consoleOutput) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        std::fwrite(data, 1, len, stdout);
    }
}

void logFlush() {
    if (g_asyncLog != nullptr) {
        g_asyncLog->flush();
    }
    if (g_consoleOutput) {
        std::lock_guard<std::mutex> lock(g_consoleMutex);
        std::fflush(stdout);
    }
}

struct ServerOptions {
    int port{8080};
    std::string wwwRoot{"www"};
    size_t ioThreads{4};      // 0 表示单线程（阶段 3 的行为，便于做对比压测）
    size_t workerThreads{0};  // >0 时把静态文件处理放到工作线程池
    double idleTimeoutSeconds{0.0};  // >0 时启用空闲连接超时
    std::string logLevel{"info"};    // trace/debug/info/warn/error
    std::string logDir;              // 为空表示只输出到控制台
    size_t logRollSizeMb{32};        // 单个日志文件大小上限
    bool quiet{false};               // 只写文件，不输出到控制台
    size_t maxConnections{0};        // >0 时启用过载保护，超出返回 503
    double rateLimitQps{0.0};         // >0 时启用令牌桶限流，超出返回 429
    double rateLimitBurst{0.0};       // 令牌桶突发容量，默认等于 rateLimitQps
    size_t zeroCopyThreshold{64 * 1024};  // >= 该字节数的静态文件走 sendfile，0 关闭
    std::string proxyPass{"127.0.0.1:8081"};  // 反向代理下游地址，例如 127.0.0.1:8081（none 关闭）
    std::string proxyPrefix{"/api/gateway"};  // 反向代理路由前缀
    double proxyTimeoutSeconds{3.0};          // 反向代理读写超时
};

void printUsage(const char* program) {
    std::cout << "usage: " << program << " [-p port] [-r wwwRoot] [-t ioThreads] "
                 "[-w workerThreads] [-i idleSeconds] [-m maxConnections]\n"
                 "       [-R rateLimitQps] [-B rateLimitBurst] [-Z zeroCopyBytes]\n"
                 "       [-P proxyTarget] [--proxy-prefix prefix]\n"
                 "       [-l logLevel] [-g logDir] [-s rollSizeMb] [-q] [-h]\n"
              << "  -p port          监听端口，默认 8080\n"
              << "  -r wwwRoot       静态资源目录，默认 www\n"
              << "  -t ioThreads     IO 线程数，默认 4，0 表示单线程\n"
              << "  -w workerThreads 计算线程池大小，默认 0（关闭）\n"
              << "  -i idleSeconds   空闲连接超时秒数，默认 0（不启用）\n"
              << "  -m maxConnections 最大并发连接数，默认 0（不限制），超出返回 503\n"
              << "  -R rateLimitQps  令牌桶限流速率 (QPS)，默认 0（不限制），超出返回 429\n"
              << "  -B burst         令牌桶突发容量 (Burst)，默认与 rateLimitQps 相同\n"
              << "  -Z zeroCopyBytes 零拷贝阈值：>=(字节) 的静态文件走 sendfile，"
                 "默认 65536，0 表示关闭\n"
              << "  -P proxyTarget   反向代理上游地址，默认 127.0.0.1:8081（设为 none 关闭）\n"
              << "  --proxy-prefix prefix 反向代理路由前缀，默认 /api/gateway\n"
              << "  -l logLevel      日志级别 trace/debug/info/warn/error，默认 info\n"
              << "  -g logDir        日志目录（启用异步落盘），默认只输出控制台\n"
              << "  -s rollSizeMb    单个日志文件大小上限，默认 32MB\n"
              << "  -q               静默模式：只写文件或丢弃，不输出到控制台\n"
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
        if (arg == "-i" && hasValue) {
            options->idleTimeoutSeconds = std::strtod(argv[++i], nullptr);
            continue;
        }
        if (arg == "-l" && hasValue) {
            options->logLevel = argv[++i];
            continue;
        }
        if (arg == "-g" && hasValue) {
            options->logDir = argv[++i];
            continue;
        }
        if (arg == "-s" && hasValue) {
            options->logRollSizeMb =
                static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
            continue;
        }
        if (arg == "-q") {
            options->quiet = true;
            continue;
        }
        if (arg == "-m" && hasValue) {
            options->maxConnections =
                static_cast<size_t>(std::strtoul(argv[++i], nullptr, 10));
            continue;
        }
        if (arg == "-R" && hasValue) {
            options->rateLimitQps = std::strtod(argv[++i], nullptr);
            continue;
        }
        if (arg == "-B" && hasValue) {
            options->rateLimitBurst = std::strtod(argv[++i], nullptr);
            continue;
        }
        if (arg == "-Z" && hasValue) {
            options->zeroCopyThreshold =
                static_cast<size_t>(std::strtoull(argv[++i], nullptr, 10));
            continue;
        }
        if ((arg == "-P" || arg == "--proxy-pass") && hasValue) {
            options->proxyPass = argv[++i];
            continue;
        }
        if (arg == "--proxy-prefix" && hasValue) {
            options->proxyPrefix = argv[++i];
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

myself::Metrics::MethodKind methodKindOf(myself::HttpRequest::Method method) {
    switch (method) {
        case myself::HttpRequest::Method::kGet:
            return myself::Metrics::kMethodGet;
        case myself::HttpRequest::Method::kHead:
            return myself::Metrics::kMethodHead;
        case myself::HttpRequest::Method::kPost:
            return myself::Metrics::kMethodPost;
        default:
            return myself::Metrics::kMethodOther;
    }
}

/// /api/status 的 JSON：模块指标 + 线程与配置信息
std::string statusJson(const myself::Metrics::Snapshot& data, size_t ioThreads,
                       size_t workerThreads, double idleSeconds, size_t timers,
                       uint64_t logLines) {
    std::ostringstream out;
    out << std::setprecision(4);
    const double averageMs =
        data.latencyCount == 0
            ? 0.0
            : data.latencySumMs / static_cast<double>(data.latencyCount);

    out << "{"
        << "\"name\":\"MyselfWebServer\","
        << "\"version\":\"" << myself::version() << "\","
        << "\"uptimeSeconds\":" << data.uptimeSeconds << ","
        << "\"ioThreads\":" << ioThreads << ","
        << "\"workerThreads\":" << workerThreads << ","
        << "\"idleTimeoutSeconds\":" << idleSeconds << ","
        << "\"timers\":" << timers << ","
        << "\"connections\":{\"current\":" << data.connectionsCurrent
        << ",\"total\":" << data.connectionsTotal << ",\"max\":" << data.connectionsMax
        << "},"
        << "\"requests\":{\"total\":" << data.requestsTotal
        << ",\"rateLimited\":" << data.requestsRateLimited
        << ",\"notModified\":" << data.requestsNotModified
        << ",\"proxy\":{\"total\":" << data.requestsProxy << ",\"errors\":" << data.requestsProxyErrors << "}"
        << ",\"errors\":" << data.errorsTotal << ",\"byStatusClass\":{\"1xx\":"
        << data.statusClasses[0] << ",\"2xx\":" << data.statusClasses[1]
        << ",\"3xx\":" << data.statusClasses[2] << ",\"4xx\":" << data.statusClasses[3]
        << ",\"5xx\":" << data.statusClasses[4] << "}},"
        << "\"bytes\":{\"read\":" << data.bytesReadTotal
        << ",\"written\":" << data.bytesWrittenTotal << "},"
        << "\"latencyMs\":{\"p50\":" << data.latencyP50Ms << ",\"p95\":" << data.latencyP95Ms
        << ",\"p99\":" << data.latencyP99Ms << ",\"avg\":" << averageMs
        << ",\"count\":" << data.latencyCount << "},"
        << "\"logLines\":" << logLines << ","
        << "\"status\":\"ok\"}";
    return out.str();
}

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
    std::signal(SIGINT, onTerminationSignal);
    std::signal(SIGTERM, onTerminationSignal);

    // 日志系统初始化：级别过滤 + 可选的异步文件输出
    myself::Logger::setLevel(myself::logLevelFromString(options.logLevel));
    g_consoleOutput = !options.quiet;
    std::unique_ptr<myself::AsyncLogging> asyncLog;
    if (!options.logDir.empty()) {
        const std::string baseName = options.logDir + "/server";
        asyncLog = std::make_unique<myself::AsyncLogging>(
            baseName, options.logRollSizeMb * 1024 * 1024);
        asyncLog->start();
        g_asyncLog = asyncLog.get();
        LOG_INFO << "async logging enabled, dir=" << options.logDir
                 << ", roll=" << options.logRollSizeMb << "MB";
    }
    myself::Logger::setOutput(logOutput, logFlush);

    // 运行指标：安装全局实例后，net 层与 worker 线程都能通过 metrics::* 上报
    myself::Metrics metrics;
    myself::Metrics::install(&metrics);

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

        // 跨线程读取的统计数据用原子变量：定时器任务在 main 线程更新，
        // /api/status 可能在任何 IO 线程里读取。
        std::atomic<size_t> timerCount{0};
        ConnectionRegistry registry;

        // 周期性统计：演示 runEvery 的用法，同时刷新定时器数量
        baseLoop.runEvery(10.0, [&baseLoop, &registry, &timerCount] {
            timerCount.store(baseLoop.timerCount());
            LOG_INFO << "stats online=" << registry.size()
                     << " timers=" << timerCount.load()
                     << " up=" << (myself::elapsedMs(myself::now()) / 1000) << "s";
        });

        // 优雅退出：收到 SIGINT/SIGTERM 后停止接收新连接并收尾
        baseLoop.runEvery(0.2, [&baseLoop] {
            if (!g_shutdownRequested.load()) {
                return;
            }
            LOG_INFO << "shutdown requested, stopping event loop";
            baseLoop.quit();
        });

        myself::TokenBucket rateLimiter(options.rateLimitQps, options.rateLimitBurst);
        if (rateLimiter.isEnabled()) {
            LOG_INFO << "rate limiter enabled, rate=" << options.rateLimitQps
                     << " qps, burst=" << rateLimiter.burst();
        }

        myself::Router router(options.wwwRoot);
        router.setZeroCopyThreshold(options.zeroCopyThreshold);
        router.registerHandler(
            "/api/status",
            [&metrics, &ioPool, &workerPool, &timerCount,
             idleSeconds = options.idleTimeoutSeconds](const myself::HttpRequest&) {
                const myself::Metrics::Snapshot data = metrics.snapshot();
                myself::HttpResponse response;
                response.setStatus(200, "OK");
                response.setContentType("application/json; charset=utf-8");
                response.setBody(statusJson(data, ioPool.threadCount(),
                                            workerPool.threadCount(), idleSeconds,
                                            timerCount.load(), myself::Logger::lineCount()));
                return response;
            });

        // Prometheus 文本格式：可直接被 Prometheus 或 VictoriaMetrics 抓取
        router.registerHandler("/metrics", [&metrics](const myself::HttpRequest&) {
            myself::HttpResponse response;
            response.setStatus(200, "OK");
            response.setContentType("text/plain; version=0.0.4; charset=utf-8");
            response.setBody(metrics.renderPrometheus(myself::Logger::lineCount()));
            return response;
        });

        myself::ProxyOptions proxyOpts;
        proxyOpts.prefix = options.proxyPrefix;
        proxyOpts.timeoutSeconds = options.proxyTimeoutSeconds;
        proxyOpts.enabled = (!options.proxyPass.empty() && options.proxyPass != "none");
        if (proxyOpts.enabled) {
            const size_t colon = options.proxyPass.find(':');
            if (colon != std::string::npos) {
                proxyOpts.targetHost = options.proxyPass.substr(0, colon);
                proxyOpts.targetPort = static_cast<uint16_t>(
                    std::strtoul(options.proxyPass.substr(colon + 1).c_str(), nullptr, 10));
            } else {
                proxyOpts.targetHost = options.proxyPass;
                proxyOpts.targetPort = 80;
            }
        }
        myself::ReverseProxy reverseProxy(proxyOpts);
        if (reverseProxy.isEnabled()) {
            LOG_INFO << "reverse proxy enabled: " << reverseProxy.prefix()
                     << " -> " << reverseProxy.targetHost() << ":" << reverseProxy.targetPort();
            router.registerPrefixHandler(
                reverseProxy.prefix(),
                [&reverseProxy](const myself::HttpRequest& req, const std::string& clientIp) {
                    myself::metrics::requestProxied();
                    myself::HttpResponse resp = reverseProxy.forward(req, clientIp);
                    if (resp.statusCode() >= 500) {
                        myself::metrics::requestProxyError();
                    }
                    return resp;
                });
        }

        myself::Acceptor acceptor(&baseLoop, "0.0.0.0", options.port);

        acceptor.setNewConnectionCallback([&](int connfd, std::string ip, uint16_t peerPort) {
            // 过载保护：超过上限直接回 503 并关闭，避免服务被压垮时表现为随机超时
            if (options.maxConnections > 0 && registry.size() >= options.maxConnections) {
                myself::metrics::connectionRejected();
                LOG_WARN << "connection rejected, online=" << registry.size()
                         << " limit=" << options.maxConnections << " peer=" << ip << ":"
                         << peerPort;
                const std::string raw = myself::HttpResponse::serviceUnavailable().serialize();
                const ssize_t ignored = ::write(connfd, raw.data(), raw.size());
                (void)ignored;  // 尽力而为：写不出去就直接关闭
                ::close(connfd);
                return;
            }

            myself::EventLoop* ioLoop = ioPool.nextLoop();

            // 连接必须在它所属的 IO 线程里创建并注册事件，否则就是跨线程操作 epoll
            ioLoop->runInLoop([&router, &workerPool, &registry, &rateLimiter, &reverseProxy, connfd, ip, peerPort,
                               ioLoop, idleSeconds = options.idleTimeoutSeconds] {
                auto conn = std::make_shared<myself::TcpConnection>(ioLoop, connfd, ip, peerPort);
                const int fd = conn->fd();
                auto parser = std::make_shared<myself::HttpParser>();

                conn->setMessageCallback(
                    [&router, &workerPool, &rateLimiter, &reverseProxy, parser, ioLoop](
                        const std::shared_ptr<myself::TcpConnection>& c,
                        myself::Buffer* input) {
                        for (;;) {
                            const auto result = parser->parse(input);
                            if (result == myself::HttpParser::Result::kBadRequest) {
                                myself::HttpResponse response =
                                    myself::HttpResponse::badRequest("Bad Request");
                                c->send(response.serialize());
                                myself::metrics::error();
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
                            const myself::Timestamp requestStart = myself::now();
                            myself::metrics::requestStarted(methodKindOf(request.method()));

                            // 统一出口：无论谁算出响应，最终都在 IO 线程里写回
                            auto respond = [c, headOnly, keepAlive, ioLoop, requestStart](
                                               myself::HttpResponse response) {
                                response.setCloseConnection(!keepAlive);
                                if (response.statusCode() == 304) {
                                    myself::metrics::requestNotModified();
                                }
                                c->send(response.serialize(!headOnly));
                                if (!headOnly && response.hasFileBody()) {
                                    // 零拷贝：头部已入发送队列，文件体交给 sendfile(2)
                                    c->sendFile(response.fileBody());
                                }
                                myself::metrics::requestFinished(
                                    response.statusCode(),
                                    myself::msBetween(requestStart, myself::now()));
                                LOG_INFO << "http status=" << response.statusCode()
                                         << " loop=" << ioLoop->name();
                                if (!keepAlive) {
                                    c->shutdownWrite();
                                }
                            };

                            // 令牌桶限流保护：白名单放行运维诊断与指标采集（/api/status, /metrics），外部请求超限返回 429
                            const bool isManagement =
                                (request.path() == "/api/status" || request.path() == "/metrics");
                            if (!isManagement && !rateLimiter.tryAcquire()) {
                                myself::metrics::requestRateLimited();
                                LOG_WARN << "rate limit exceeded for path=" << request.path()
                                         << " peer=" << c->peerIp() << ":" << c->peerPort();
                                respond(myself::HttpResponse::tooManyRequests(1));
                                if (!keepAlive) {
                                    return;
                                }
                                continue;
                            }

                            const bool isProxyReq =
                                reverseProxy.isEnabled() &&
                                (request.path().rfind(reverseProxy.prefix(), 0) == 0);
                            const bool offload = workerPool.threadCount() > 0 &&
                                                 (isProxyReq || request.path().rfind("/api/", 0) != 0);
                            if (offload) {
                                // 文件读取与反向代理网络请求交给 worker 线程，回写再回到 IO 线程
                                workerPool.submit([&router, request, peerIp = c->peerIp(), ioLoop, respond] {
                                    myself::HttpResponse response = router.handle(request, peerIp);
                                    ioLoop->runInLoop(
                                        [respond, response]() mutable { respond(response); });
                                });
                            } else {
                                respond(router.handle(request, c->peerIp()));
                            }

                            if (!keepAlive) {
                                return;  // 短连接：响应写完就结束本次解析
                            }
                        }
                    });

                conn->setCloseCallback(
                    [&registry, fd](const std::shared_ptr<myself::TcpConnection>& c) {
                        LOG_INFO << "close fd=" << fd << " peer=" << c->peerIp() << ":"
                                 << c->peerPort();
                        myself::metrics::connectionClosed();
                        registry.remove(fd);
                    });

                registry.add(fd, conn);
                conn->start();
                myself::metrics::connectionOpened();
                if (idleSeconds > 0) {
                    conn->setIdleTimeout(idleSeconds);  // 空闲超时在所属 IO 线程内启用
                }

                LOG_INFO << "accept fd=" << fd << " peer=" << ip << ":" << peerPort
                         << " loop=" << ioLoop->name() << " online=" << registry.size();
            });
        });

        acceptor.listen();
        LOG_INFO << myself::buildInfo() << " http server started, io threads="
                 << options.ioThreads << " worker threads=" << options.workerThreads
                 << " idle timeout=" << options.idleTimeoutSeconds
                 << "s rate limit=" << options.rateLimitQps
                 << " qps proxy="
                 << (reverseProxy.isEnabled() ? (reverseProxy.targetHost() + ":" +
                                                 std::to_string(reverseProxy.targetPort()))
                                              : "disabled")
                 << " log level=" << options.logLevel << " www root=" << options.wwwRoot;

        baseLoop.loop();

        LOG_INFO << "event loop stopped, closing connections";
        // 退出：先让每条连接在自己的 IO 线程里关闭，再回收线程池
        registry.closeAll();
        workerPool.stop();
    } catch (const std::exception& ex) {
        LOG_ERROR << "fatal: " << ex.what();
        return EXIT_FAILURE;
    }

    LOG_INFO << "server stopped";
    myself::Logger::flush();
    if (asyncLog != nullptr) {
        asyncLog->stop();
        g_asyncLog = nullptr;
    }

    return EXIT_SUCCESS;
}

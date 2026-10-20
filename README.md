# MyselfWebServer

![ci](https://github.com/your-name/MyselfWebServer/actions/workflows/ci.yml/badge.svg)
![c++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![license](https://img.shields.io/badge/license-MIT-green)

基于 epoll ET 模式与主从 Reactor 的 C++17 高并发 HTTP 服务器，包含事件循环、定时器、
分级异步日志、Prometheus 指标、过载保护与完整测试体系。

项目以学习为目的从零实现，核心模块自行编写，参考了 TinyWebServer 与 muduo 的设计思路。

## 特性

| 能力 | 实现要点 |
| --- | --- |
| 网络模型 | 非阻塞 socket + epoll ET，主线程 accept，N 个 IO 线程读写（one loop per thread） |
| HTTP | HTTP/1.1 请求解析（有限状态机），支持 GET 与 HEAD、keep-alive、静态文件、路由注册、目录穿越防护 |
| 缓存协商 | 静态文件带强 `ETag` 与 `Last-Modified`，`If-None-Match` / `If-Modified-Since` 命中返回零 Body 的 `304 Not Modified`（RFC 9110 §13） |
| 连接管理 | shared_ptr + Channel::tie 保活，写缓冲与 EPOLLOUT 回填，超时与异常统一收口 |
| 定时器 | 由 epoll_wait 超时驱动，支持一次性与重复任务、取消，空闲连接自动断开 |
| 日志 | 分级（trace/debug/info/warn/error）、异步双缓冲落盘、按大小与日期轮转 |
| 可观测性 | `/metrics`（Prometheus 文本格式）与 `/api/status`（JSON），含延迟直方图与错误计数 |
| 健壮性 | 最大连接数限制（超出返回 503）、空闲超时、SIGINT/SIGTERM 优雅退出 |
| 工程化 | CMake 多目标构建、GoogleTest 单元测试、pytest 接口测试、GitHub Actions 持续集成 |

## 架构

```
                    ┌──────────────────────────────┐
   客户端 ──TCP──▶  │ 主线程 EventLoop（只负责 accept）│
                    └───────────────┬──────────────┘
                                    │ 轮询分配（nextLoop）
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
        io-0 EventLoop        io-1 EventLoop        io-N EventLoop
        （epoll ET + 定时器）   （epoll ET + 定时器）   （epoll ET + 定时器）
              │                     │                     │
              └──────────┬──────────┴──────────┬──────────┘
                         ▼                     ▼
                  TcpConnection          TcpConnection
                  （Buffer/Channel）      （Buffer/Channel）
                         │
                         ▼
              HttpParser（状态机，半包/粘包）
                         │
                         ▼
        Router ──▶ 静态文件 / /api/status / /metrics
                         │
                         ▼
              HttpResponse ──▶ 写回（IO 线程）

   可选：worker 线程池负责静态文件读取等耗时任务，回写统一由 ioLoop->runInLoop 回到 IO 线程
```

一次请求的路径：`accept → 分配到某个 ioLoop → TcpConnection 读入 Buffer → HttpParser 解析
→ Router 生成响应 → 写回（必要时经 worker 线程池与 runInLoop）→ 指标与日志记录`。
详细分层与并发约束见 [docs/architecture.md](docs/architecture.md)。

## 快速开始

依赖：Ubuntu 22.04 及以上、GCC 11+、CMake 3.16+。

```bash
# 构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 运行（4 个 IO 线程，最多 512 个连接，空闲 30 秒断开，日志异步落盘）
./build/webserver -t 4 -m 512 -i 30 -l info -g logs -p 8080
```

打开浏览器访问 `http://<主机IP>:8080/`，或：

```bash
curl -i http://127.0.0.1:8080/index.html     # 静态文件
curl -s http://127.0.0.1:8080/api/status     # JSON 状态
curl -s http://127.0.0.1:8080/metrics        # Prometheus 指标
```

### 运行参数

| 参数 | 说明 | 默认值 |
| --- | --- | --- |
| `-p port` | 监听端口 | 8080 |
| `-r wwwRoot` | 静态资源目录 | `www` |
| `-t ioThreads` | IO 线程数，`0` 表示单线程（用于对比压测） | 4 |
| `-w workerThreads` | 计算线程池大小，`0` 表示关闭 | 0 |
| `-i idleSeconds` | 空闲连接超时秒数，`0` 表示不启用 | 0 |
| `-m maxConnections` | 最大并发连接数，超出返回 503，`0` 表示不限制 | 0 |
| `-l logLevel` | 日志级别 trace/debug/info/warn/error | info |
| `-g logDir` | 日志目录（启用异步落盘） | 仅控制台 |
| `-s rollSizeMb` | 单个日志文件大小上限 | 32 |
| `-q` | 静默模式：不输出到控制台 | 关闭 |
| `-h` | 显示帮助 | - |

### 接口

| 路径 | 方法 | 说明 |
| --- | --- | --- |
| `/`、`/index.html` | GET / HEAD | 静态文件，未命中返回 404；带 `ETag` / `Last-Modified`，条件请求命中返回 `304` |
| `/api/status` | GET | JSON：连接数、请求数（含限流与 304 命中数）、错误数、字节数、延迟分位、日志行数、运行时长 |
| `/metrics` | GET | Prometheus 文本格式，含 `myself_*` 计数器与延迟直方图 |

## 测试

```bash
./scripts/run_unit_tests.sh      # GoogleTest 单元测试（61 个用例）
./scripts/run_api_tests.sh       # pytest 接口测试（23 个用例，夹具自动拉起服务）
./scripts/release_check.sh       # 发布门禁：构建 + 单测 + 接口测试 + 故障注入 + 冒烟压测
```

单元测试覆盖 Buffer（含 readFd）、HttpParser（半包、粘包、pipelining、非法输入）、
Router（404、目录穿越、405、Content-Type、ETag 校验器、304 命中与优先级、指纹失效）、
Metrics（直方图累计语义、分位、Prometheus 文本）、TokenBucket（突发、补发、并发竞争）、
TimerQueue（一次性、取消、重复、回调内自取消）与 LogStream。

接口测试覆盖 HTTP 行为（200/404/HEAD/keep-alive/目录穿越/条件请求 304）、限流反压
（429 + `Retry-After`、监控白名单）与指标一致性（`myself_requests_total` 与压测请求数、
4xx 分类、直方图累计语义、连接仪表）。

## 性能与稳定性

> 下表中的数字需要你在自己的机器上跑出来；命令与产物路径已经给出，不要把示例数字当结论。

```bash
./scripts/bench_matrix.sh 8080 "1 10 50 100 200 500" 30s   # 压测客户端请在宿主机执行
sudo ./scripts/flamegraph.sh $(pgrep webserver) 30
./scripts/soak_test.sh 4 8080 3600 30
python3 scripts/fault_injection.py --port 8080 --idle-timeout 30
```

| 并发 | QPS | P99（毫秒） | 备注 |
| --- | --- | --- | --- |
| 1 | 待填 | 待填 | 单连接基线 |
| 50 | 待填 | 待填 | |
| 200 | 待填 | 待填 | 多核生效区间 |
| 500 | 待填 | 待填 | 观察是否出现错误 |

原始数据：`docs/bench/bench-matrix-*.csv`、火焰图 `docs/bench/flame-*.svg`、
长稳采样 `docs/bench/soak-*.csv`（判据：RSS 增长小于 5%，错误数不增长）。

故障注入结果（半包、粘包、慢连接、超长请求头、超长请求体、连接抖动 200 次）：
见 `docs/stage-9-验收记录.md`，六个场景应全部 PASS。

## 项目结构

```
MyselfWebServer/
|-- CMakeLists.txt              顶层构建：myself_core 静态库 + webserver 可执行文件 + 可选测试
|-- include/myself/
|   |-- net/                    Socket、Acceptor、Buffer、Channel、Epoller、EventLoop、
|   |                           EventLoopThread(Pool)、TcpConnection
|   |-- http/                   HttpParser、HttpRequest、HttpResponse、Router
|   |-- timer/                  Timer、TimerQueue
|   |-- thread/                 ThreadPool（通用计算池）
|   |-- log/                    LogStream、Logger、LogFile、AsyncLogging
|   `-- util/                   Timestamp、Metrics、Version
|-- src/                        与 include 对应的实现，main.cpp 只做装配
|-- tests/unit                  GoogleTest 单元测试
|-- tests/api                   pytest 接口测试
|-- scripts/                    构建、测试、压测、火焰图、故障注入、发布门禁
|-- docs/adr/                   设计决策记录（0001-0009）
|-- docs/bench/                 压测与火焰图原始数据
|-- www/                        静态资源
`-- .github/workflows/ci.yml    持续集成
```

## 设计决策

- [ADR 0001：使用 CMake 与 C++17](docs/adr/0001-cmake-and-cpp17.md)
- [ADR 0002：目录分层与依赖方向](docs/adr/0002-layered-directories.md)
- [ADR 0003：主从 Reactor 与线程池](docs/adr/0003-main-sub-reactor.md)
- [ADR 0004：定时器与空闲连接超时](docs/adr/0004-timer-design.md)
- [ADR 0005：日志系统的前后端分离与异步落盘](docs/adr/0005-logging-design.md)
- [ADR 0006：运行指标与 /metrics 接口](docs/adr/0006-metrics-design.md)
- [ADR 0007：测试策略与持续集成](docs/adr/0007-testing-strategy.md)
- [ADR 0008：性能验证与健壮性加固](docs/adr/0008-performance-and-robustness.md)
- [ADR 0009：开源来源标注与许可证](docs/adr/0009-open-source-compliance.md)

## 与参考项目的差异

本项目在学习阶段阅读了 TinyWebServer 与 muduo 的代码，但核心模块自行实现，差异在于：

1. 事件循环支持跨线程任务投递（eventfd 唤醒）与线程归属断言，便于定位跨线程错误；
2. 定时器不依赖 timerfd，而是把最近到期时间交给 `epoll_wait`，实现更轻；
3. 日志系统前后端分离，前端零分配格式化，后端异步双缓冲落盘；
4. 自带 Prometheus 指标、过载保护与优雅退出，覆盖生产环境常见的可观测性需求；
5. 提供完整的测试体系、压测矩阵、火焰图与故障注入脚本，数据可复现。

## 开发环境

| 项 | 版本 |
| --- | --- |
| 操作系统 | Ubuntu 22.04 LTS（虚拟机） |
| 编译器 | GCC 11，C++17 |
| 构建工具 | CMake 3.22、Make/Ninja |
| 测试 | GoogleTest、pytest、requests |
| 压测与分析 | wrk、perf + FlameGraph、AddressSanitizer/TSan、Valgrind |

## 许可证与来源

本项目采用 MIT 许可证，见 [LICENSE](LICENSE)。

学习过程中参考了以下开源项目，仅参考设计思路，未复制其代码：
TinyWebServer（qinguoyi/TinyWebServer）、muduo（chenshuo/muduo）。
测试与工具链使用 GoogleTest、wrk、FlameGraph，均遵循其自身许可证。
如后续引入其他第三方代码，会在本节与 `docs/adr/0009-open-source-compliance.md` 中补充声明。

## 联系

GitHub：<https://github.com/your-name/MyselfWebServer>（替换为你的仓库地址）

# Changelog

本文件按阶段记录可见变化。日期取自仓库提交记录，阶段内的多条提交合并为一条里程碑。

## [1.0.0] - 2026-09-28

阶段 10：交付收尾。

### Added

- `docs/architecture.md`：分层、线程模型与请求时序说明
- `docs/demo-script.md`：三到五分钟演示视频分镜脚本
- `docs/resume-and-interview.md`：简历条目、项目讲解稿与高频追问问答
- `docs/release-checklist.md`：v1.0.0 发布检查清单
- `docs/adr/0009-open-source-compliance.md`：来源标注与许可证决策
- `scripts/release_check.sh`：发布门禁（构建、单元测试、接口测试、故障注入、冒烟压测、文档检查）
- `LICENSE`：MIT 许可证

### Changed

- `README.md` 重写：补充特性表、架构图、运行参数、接口列表、性能与稳定性数据区、
  与参考项目的差异说明

## [0.10.0] - 2026-09-27

阶段 9：性能与健壮性。

### Added

- `-m/--max-connections` 过载保护：超出上限返回 503，并累加 `myself_connections_rejected_total`
- SIGINT/SIGTERM 优雅退出：信号只设置标志，由事件循环的定时任务完成收尾
- `scripts/bench_matrix.sh`：并发梯度压测并输出 CSV
- `scripts/soak_test.sh`：长稳采样（RSS、在线连接、请求数、错误数）
- `scripts/flamegraph.sh`：perf 采样与火焰图生成
- `scripts/fault_injection.py`：六类故障注入用例
- ADR 0008 与阶段 9 验收记录

### Changed

- `.gitignore` 补充 `.pytest_cache/`、`third_party/` 与 perf 原始数据

## [0.9.0] - 2026-09-27

阶段 8：测试体系与持续集成。

### Added

- `myself_core` 静态库拆分：可执行文件与单元测试分别链接
- 单元测试 46 个用例（Buffer、HttpParser、Router、Metrics、TimerQueue、LogStream）
- 接口测试 15 个用例（HTTP 行为与指标一致性）
- `.github/workflows/ci.yml`：构建 + ctest + pytest
- ADR 0007 与阶段 8 验收记录

### Fixed

- Prometheus 直方图桶改为累计语义，修复 `histogram_quantile()` 计算错误

## [0.8.0] - 2026-09-27

阶段 7：运行指标。

### Added

- `Metrics` 模块：原子计数器 + 12 档延迟直方图 + 分位估算
- `/metrics`（Prometheus 文本格式）与扩展后的 `/api/status`
- 读写字节数与 socket 错误统计
- ADR 0006 与阶段 7 验收记录

## [0.7.0] - 2026-09-26

阶段 6：日志系统。

### Added

- `LogStream`/`FixedBuffer`：栈上零分配格式化
- `Logger`：级别过滤、时间戳、线程 id、`LOG_*` 宏
- `LogFile`：按大小与日期轮转
- `AsyncLogging`：后台线程 + 双缓冲异步落盘
- `-l/-g/-s/-q` 运行参数
- ADR 0005 与阶段 6 验收记录

### Changed

- 全部模块的 `std::cout`/`std::cerr` 替换为分级日志，移除调试打印

## [0.6.0] - 2026-09-25

阶段 5：定时器与空闲连接超时。

### Added

- `Timestamp`（单调时钟）与 `Timer`/`TimerQueue`
- `EventLoop::runAt/runAfter/runEvery/cancelTimer`
- 空闲连接超时（`-i` 参数）与周期统计任务
- ADR 0004 与阶段 5 验收记录

## [0.5.0] - 2026-09-24

阶段 4：主从 Reactor 与线程池。

### Added

- `EventLoopThread`、`EventLoopThreadPool`：one loop per thread
- `EventLoop` 跨线程任务投递与 eventfd 唤醒、线程归属断言
- `ThreadPool`：通用计算线程池
- `-t/-w` 运行参数
- ADR 0003 与阶段 4 验收记录

## [0.4.0] - 2026-09-24

阶段 3：HTTP 解析与服务。

### Added

- `HttpParser` 状态机（半包、粘包、pipelining）
- `HttpRequest`/`HttpResponse`/`Router`（静态文件、404、目录穿越防护）
- 阶段 3 验收记录（`curl` 200/404、目录穿越、半包与粘包请求）

## [0.3.0] - 2026-09-23

阶段 2：非阻塞 epoll Reactor。

### Added

- `Buffer`、`Channel`、`Epoller`、`EventLoop`、`TcpConnection`、`Acceptor`
- ET 模式循环读写、写缓冲与 EPOLLOUT 回填
- 阶段 2 验收记录（三连接并发、ET 循环读、半包与粘包现象）

## [0.2.0] - 2026-09-23

阶段 1：阻塞式 TCP 回显服务。

### Added

- `Socket` RAII 封装（含 `listenOn`、SO_REUSEADDR）
- 阻塞式单连接回显与 SIGPIPE 处理
- 阶段 1 验收记录（`nc` 回显、端口复用、单连接阻塞现象）

## [0.1.0] - 2026-09-22

阶段 0：工程骨架。

### Added

- 顶层 CMake、目录结构、`.clang-format`、`.editorconfig`、`.gitignore`
- 构建/运行/压测/消毒器脚本
- README、ADR 0001 与 0002、阶段 0 验收记录

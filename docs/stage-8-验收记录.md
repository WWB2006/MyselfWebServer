# 阶段 8 验收记录

> 填写方式：把命令输出贴进「实际结果」列。CI 通过的链接或截图，是这一阶段最直接的交付证据。

## 一、环境信息

| 项 | 命令 | 实际结果 |
| --- | --- | --- |
| CPU 与内存 | `nproc`、`free -h` |  |
| 系统与内核 | `cat /etc/os-release \| head -2`、`uname -r` |  |
| CMake 与编译器 | `cmake --version`、`g++ --version` |  |
| 测试框架 | `ls /usr/include/gtest` 或首次 FetchContent 下载记录 |  |

## 二、单元测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DWEBSERVER_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

| 测试文件 | 覆盖内容 | 用例数 | 结果 |
| --- | --- | --- | --- |
| `test_buffer.cpp` | 追加/读取/扩容/空间复用/readFd（pipe） | 6 |  |
| `test_http_parser.cpp` | 请求行、查询串、半包、粘包、pipelining、请求体、长连接、非法输入、超长头 | 14 |  |
| `test_router.cpp` | 静态文件、根路径映射、404、目录穿越、405、Content-Type、注册路由优先 | 7 |  |
| `test_metrics.cpp` | 连接计数与峰值、状态码/方法分类、字节与错误、直方图累计语义、分位估算、Prometheus 文本、reset、未安装空操作 | 8 |  |
| `test_timer_queue.cpp` | 一次性定时、取消、重复定时、回调内自取消、nextTimeoutMs、到期派发 | 6 |  |
| `test_log_stream.cpp` | 整数/浮点/布尔、无符号大数、null、std::string、reset | 5 |  |
| **合计** |  | **46** |  |

完整 ctest 输出粘贴处：

## 三、接口测试

```bash
./scripts/run_api_tests.sh      # 或：MYSELF_TEST_PORT=18080 pytest tests/api -v
```

| 测试文件 | 覆盖内容 | 用例数 | 结果 |
| --- | --- | --- | --- |
| `test_http_basic.py` | 200/404、根路径、HEAD 无正文、keep-alive、状态接口字段、目录穿越 | 7 |  |
| `test_metrics.py` | /metrics 格式与指标族、请求计数增量、4xx 计数、累计桶语义、count 与 total 一致、连接仪表、错误计数不误增 | 8 |  |
| **合计** |  | **15** |  |

完整 pytest 输出粘贴处：

## 四、覆盖率（可选但推荐）

```bash
cmake -B build-cov -DCMAKE_BUILD_TYPE=Debug -DWEBSERVER_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0"
cmake --build build-cov -j"$(nproc)"
ctest --test-dir build-cov --output-on-failure
lcov --capture --directory build-cov --output-file coverage.info
lcov --summary coverage.info
```

| 模块 | 行覆盖率 | 说明 |
| --- | --- | --- |
| `util`（Metrics/Timestamp） |  |  |
| `http`（Parser/Router） |  |  |
| `net`（Buffer/Channel/EventLoop/TcpConnection） |  |  |
| 总体 |  | 目标 60% 以上 |

## 五、持续集成

| 检查 | 期望 | 实际 |
| --- | --- | --- |
| 工作流触发 | push 与 PR 都触发 |  |
| 构建步骤 | 成功（无警告新增） |  |
| 单元测试步骤 | 全部通过 |  |
| 接口测试步骤 | 全部通过 |  |
| 运行时长 | 单次 5 分钟以内 |  |
| 失败时日志 | artifact 可下载 |  |

## 六、修复记录

| 问题 | 现象 | 处理 |
| --- | --- | --- |
| Prometheus 直方图非累计 | 阶段 7 的桶计数是各档独立计数，`histogram_quantile` 会算错 | 阶段 8 在渲染时累加（`Metrics.cpp`），并新增两个用例锁定该行为 |
|  |  |  |

## 七、结论与下一步

结论：

下一步：阶段 9 做性能与健壮性——压测基线、火焰图、故障注入与长稳测试，并把数据写进 README。

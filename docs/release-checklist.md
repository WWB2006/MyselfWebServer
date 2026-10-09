# v1.0.0 发布检查清单

发布的对象是"可以放进简历发给面试官"的仓库，而不只是能跑的代码。
逐项确认后再打标签。

## 一、代码

| 检查项 | 标准 | 完成 |
| --- | --- | --- |
| 构建 | `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)` 无错误、无新增警告 | □ |
| 单线程基线 | `./build/webserver -t 0 -p 8080` 正常启动并响应 | □ |
| 多线程 | `-t 4` 下连接分散到多个 IO 线程（日志中的 `loop=io-x`） | □ |
| 过载保护 | `-m 8` 时超出连接收到 503，`myself_connections_rejected_total` 增加 | □ |
| 优雅退出 | 压测中 Ctrl+C，日志出现 `shutdown requested`、`server stopped`，进程正常退出 | □ |

## 二、测试

| 检查项 | 标准 | 完成 |
| --- | --- | --- |
| 单元测试 | `./scripts/run_unit_tests.sh` 全绿 | □ |
| 接口测试 | `./scripts/run_api_tests.sh` 全绿 | □ |
| 故障注入 | `python3 scripts/fault_injection.py --idle-timeout 30` 六项全 PASS | □ |
| 发布门禁 | `./scripts/release_check.sh` 输出 PASS 汇总 | □ |
| CI | GitHub Actions 最近一次运行为绿色 | □ |
| 脏检查 | 引入 ASan/TSan 构建跑一轮，无新增报告 | □ |

## 三、数据（证据）

| 检查项 | 标准 | 完成 |
| --- | --- | --- |
| 压测矩阵 | `docs/bench/bench-matrix-*.csv` 至少 5 档并发，含环境注释 | □ |
| 火焰图 | `docs/bench/flame-*.svg` 存在，能指出顶部热点函数 | □ |
| 长稳采样 | `docs/bench/soak-*.csv` 至少 1 小时，RSS 增长小于 5% | □ |
| README 数据区 | 表格数字与 CSV 一致，不是占位符 | □ |
| 验收记录 | `docs/stage-9-验收记录.md` 三张表填写完整 | □ |

## 四、文档

| 检查项 | 标准 | 完成 |
| --- | --- | --- |
| README | 特性、架构图、快速开始、参数表、接口、测试、数据、结构、ADR、许可、视频链接齐全 | □ |
| 架构文档 | `docs/architecture.md` 描述分层、线程模型、请求时序与并发约束 | □ |
| 演示视频 | `docs/demo-script.md` 的分镜已录制并上传，链接写进 README | □ |
| 简历材料 | `docs/resume-and-interview.md` 中的条目已按真实数据核对 | □ |
| CHANGELOG | 更新到 `[1.0.0]`，含各阶段 Added/Changed/Fixed | □ |
| ADR | 0001 到 0009 齐全，与代码现状一致 | □ |
| 许可证 | 根目录 `LICENSE` 存在，README 声明来源与第三方组件 | □ |
| 占位符清理 | 仓库中不残留 `your-name`、`待填`、示例数字被当作结论的表述 | □ |

## 五、仓库设置

| 检查项 | 标准 | 完成 |
| --- | --- | --- |
| 忽略文件 | `git status --short` 不出现 `build/`、`.venv/`、`logs/`、`third_party/`、`__pycache__/`、`.pytest_cache/` | □ |
| 敏感信息 | `git ls-files \| grep -iE 'key\|pem\|env\|secret'` 无输出 | □ |
| 提交历史 | 每个阶段都有可读的提交信息，没有一次性导入的大提交 | □ |
| 分支 | `main` 保持可构建，阶段分支已合并 | □ |
| 标签 | `git tag -a v1.0.0 -m "release v1.0.0"` 并推送 | □ |
| 仓库简介 | GitHub 仓库描述、Topics（cpp、epoll、http-server、linux）已填 | □ |
| 置顶 | 把仓库 Pin 到个人主页，README 顶部含视频与数据链接 | □ |

## 六、投递前最后一遍

1. 用手机打开仓库首页，确认 README 排版正常、图片能加载；
2. 复制仓库链接发给同学，让对方在**不看提示**的情况下跑起来；
3. 按 `docs/resume-and-interview.md` 的三分钟讲稿自述一遍并录音，检查是否超时；
4. 准备好两个"主动暴露的不足"（如无 TLS、定时器精度），避免被问到时临场找补。

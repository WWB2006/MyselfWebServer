# Changelog

本文件记录每个阶段的可见变化，遵循 Keep a Changelog 的组织方式，版本号对应阶段里程碑。

## [Unreleased]

### 计划中

- 阶段 1：阻塞式 TCP 回显服务（Socket 封装、最小 Acceptor）
- 阶段 2：非阻塞 epoll 事件循环与多连接并发

## [0.1.0] - 2026-09-24

阶段 0：工程骨架。

### Added

- 顶层 `CMakeLists.txt`：C++17、`-Wall -Wextra -g`、链接 Threads、可选单元测试开关
- `src/CMakeLists.txt`：核心代码编译为 `myself_core` 静态库，可执行文件单独链接
- `src/main.cpp` 与 `include/myself/util/Version.h`：可运行的入口与版本常量
- 目录结构：`include/myself/{net,http,timer,thread,log,util}`、`src/*`、`tests/{unit,api}`、
  `scripts`、`docs/{adr,bench}`、`www`、`config`
- 工程规范文件：`.clang-format`、`.editorconfig`、`.gitignore`、`.vscode/launch.json`
- 运行脚本：`build.sh`、`run.sh`、`bench.sh`、`sanitize.sh`
- 文档：`README.md`、`docs/adr/0001`、`docs/adr/0002`、`docs/stage-0-验收记录.md`
- 静态资源示例：`www/index.html`、`www/404.html`
- 运行配置示例：`config/server.json`

### Verified

- Ubuntu 22.04 + GCC 11 下构建通过，可执行文件正常运行并输出启动日志
- 单元测试通道可用（`-DWEBSERVER_BUILD_TESTS=ON` 时可编译并执行用例）
- 调试通道可用（GDB 可在 `main` 处断点）

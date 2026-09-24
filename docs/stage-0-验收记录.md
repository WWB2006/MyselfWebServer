# 阶段 0 验收记录

> 填写方式：把每行「实际输出」替换成你在虚拟机里的真实结果，示例行仅用于说明格式。
> 这份记录的目的不是好看，而是让阶段 1 出问题时能快速判断“环境是不是变了”。

## 一、验收信息

| 项 | 内容 |
| --- | --- |
| 验收日期 | 2026-09-24 |
| 验收人 | （填写你的名字） |
| 项目路径 | `~/code/MyselfWebServer` |
| 提交版本 | `chore: init project skeleton` 对应的提交号：`（填写 git log 里的短哈希）` |

## 二、环境信息

| 项 | 命令 | 实际输出 |
| --- | --- | --- |
| 系统版本 | `cat /etc/os-release \| head -2` | Ubuntu 22.04.x LTS（示例） |
| 内核 | `uname -r` | 5.15.x（示例） |
| CPU 与内存 | `nproc; free -h` | 4 核 / 4Gi（示例） |
| 编译器 | `g++ --version \| head -1` | g++ 11.4.0（示例） |
| 构建工具 | `cmake --version \| head -1` | cmake 3.22.x（示例） |
| 调试器 | `gdb --version \| head -1` | gdb 12.x（示例） |

## 三、验收命令与结果

| 序号 | 检查项 | 命令 | 期望结果 | 实际结果 |
| --- | --- | --- | --- | --- |
| 1 | 配置 | `cmake -B build -DCMAKE_BUILD_TYPE=Debug` | 配置成功，无报错 |  |
| 2 | 构建 | `cmake --build build -j$(nproc)` | 生成 `build/bin/MyselfWebServer` |  |
| 3 | 运行 | `./build/bin/MyselfWebServer -p 8080` | 打印启动日志并退出 |  |
| 4 | 参数解析 | `./build/bin/MyselfWebServer -h` | 打印用法说明 |  |
| 5 | 错误参数 | `./build/bin/MyselfWebServer -x` | 返回非零并提示未知参数 |  |
| 6 | 调试 | `gdb -q ./build/bin/MyselfWebServer -ex 'break main' -ex run -ex quit` | 能停在 main |  |
| 7 | 单元测试 | `cmake -B build -DWEBSERVER_BUILD_TESTS=ON && ctest --test-dir build --output-on-failure` | 用例全部通过 |  |
| 8 | 忽略生效 | `git status --short` | 看不到 `build/` 等产物 |  |

## 四、交付物核对

| 交付物 | 完成标准 | 状态 |
| --- | --- | --- |
| 目录结构 | 与 ADR 0002 中的分层一致 | □ |
| 构建脚本 | 顶层与 src 的 CMakeLists 齐全，产物输出到 build/bin | □ |
| 程序入口 | 只做装配，能打印版本与端口 | □ |
| 规范文件 | `.clang-format`、`.editorconfig`、`.gitignore`、`.vscode` | □ |
| 脚本 | build / run / bench / sanitize 四个脚本可执行 | □ |
| 文档 | README、两篇 ADR、本验收记录 | □ |
| 提交 | 已完成首次提交并推送到 GitHub | □ |

## 五、结论与遗留问题

结论：（填写，例如“八项检查全部通过，可以进入阶段 1”）

遗留问题与处理计划：

| 问题 | 影响 | 计划 |
| --- | --- | --- |
| （示例）FetchContent 下载 GoogleTest 较慢 | 只影响首次配置 | 阶段 8 时改用系统包 `libgtest-dev` |
|  |  |  |

## 六、下一步

1. 进入阶段 1：实现 `Socket` 封装与阻塞式 TCP 回显服务；
2. 用 `nc 127.0.0.1 8080` 验证回显，并记录单连接阻塞的缺陷现象；
3. 完成阶段 1 后补 ADR 0003（为什么阶段 1 先用阻塞模型）并提交。

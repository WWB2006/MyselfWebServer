# 阶段 1 验收记录

> 填写方式：把「实际结果」列替换成你机器上的真实输出。
> 本阶段的重点不是性能，而是把「三次握手 → 收发数据 → 四次挥手」这条链路跑通，
> 并亲眼看到「单连接阻塞」这个缺陷现象——阶段 2 的存在理由就来自它。

## 一、环境信息

| 项 | 命令 | 实际结果 |
| --- | --- | --- |
| 系统与内核 | `cat /etc/os-release \| head -2`、`uname -r` |  |
| 编译器 | `g++ --version \| head -1` |  |
| 构建类型 | `cmake -B build -DCMAKE_BUILD_TYPE=Debug` |  |
| 监听端口 | `-p` 参数 | 8080 |

## 二、功能验收

| 序号 | 检查项 | 命令 | 期望结果 | 实际结果 |
| --- | --- | --- | --- | --- |
| 1 | 配置与构建 | `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j$(nproc)` | 无错误、无新增告警，生成 `build/webserver` |  |
| 2 | 启动并监听 | `./build/webserver -p 8080` | 打印监听地址 `0.0.0.0:8080` |  |
| 3 | 端口处于监听 | `ss -ltnp \| grep 8080` | 状态 `LISTEN`，进程为 `webserver` |  |
| 4 | 回显收发 | 另开终端 `nc 127.0.0.1 8080`，输入若干行 | 输入内容原样返回 |  |
| 5 | 客户端正常断开 | 在 `nc` 中按 Ctrl+C | 服务端 `read` 返回 0，打印连接关闭日志，进程不退出 |  |
| 6 | 端口复用 | 关闭进程后立刻重启 | 不报 `Address already in use`（SO_REUSEADDR 生效） |  |
| 7 | 端口被占用时的提示 | 保持上一个进程运行，再次启动 | 报 `Address already in use` 并给出明确提示 |  |
| 8 | 单连接阻塞现象（本阶段已知缺陷） | 开两个 `nc`，只在第一个里输入 | 第二个连接迟迟得不到响应——这是阶段 2 要解决的问题 |  |
| 9 | 客户端半途断开不崩溃 | `nc` 连接后立即 Ctrl+C，重复 10 次 | 无崩溃、无 SIGPIPE 终止 |  |

## 三、交付物核对

| 交付物 | 完成标准 | 状态 |
| --- | --- | --- |
| `include/myself/net/Socket.h` | RAII 管理 fd，析构自动关闭；拷贝被禁用 | □ |
| `include/myself/net/InetAddress.h` | 封装地址与端口，可转字符串打印 | □ |
| `src/main.cpp` | 只做装配：解析参数、创建对象、启动循环 | □ |
| 关键点落实 | `SO_REUSEADDR`、`listen` 的 backlog ≥ 128、`read` 返回值区分 0 与 -1 并处理 `errno` | □ |
| 提交 | 一个功能一次提交，提交信息带前缀 | □ |

## 四、结论与遗留问题

结论：

| 问题 | 影响 | 计划 |
| --- | --- | --- |
| 单连接串行处理，第二个客户端被阻塞 | 无法服务多个客户端 | 阶段 2 改为非阻塞 + epoll 事件循环 |

## 五、下一步

进入阶段 2：把 fd 设为非阻塞（`O_NONBLOCK`），用 `Epoller` + `Channel` + `EventLoop` 重写事件处理；
ET 模式下读事件必须循环读到 `EAGAIN`，`accept` 也要循环到 `EAGAIN`。

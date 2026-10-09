# 架构说明

## 一、分层与依赖方向

| 模块 | 职责 | 依赖 |
| --- | --- | --- |
| `util` | 时间基准、运行指标、版本信息 | 无 |
| `log` | 日志格式化、级别过滤、异步落盘 | util |
| `net` | 套接字封装、事件循环、连接管理、缓冲区 | util、log |
| `http` | 请求解析、响应构造、路由 | net（仅通过 Buffer 与回调交互） |
| `timer` | 定时器与超时管理 | util、log（事件循环反向持有队列） |
| `thread` | 通用计算线程池 | util、log |
| `main.cpp` | 装配：解析参数、创建对象、启动循环 | 以上全部 |

依赖只能单向流动：下层不感知上层。协议层（`http`）不持有 socket，
因此可以脱离网络做单元测试；`util` 不依赖 `log`，日志行数由调用方传入渲染函数。

## 二、线程模型

```
主线程（baseLoop）
  └─ Acceptor：只处理监听 fd 的可读事件，accept 后轮询分配到某个 IO 线程

IO 线程（io-0 … io-N，每个线程一个 EventLoop）
  ├─ Epoller：epoll_wait（超时值取自该线程 TimerQueue 的最近到期时间）
  ├─ Channel：fd 的事件与回调
  ├─ TcpConnection：读缓冲、写缓冲、空闲超时定时器
  └─ TimerQueue：一次性与重复定时任务

worker 线程池（可选，-w 参数）
  └─ 只做静态文件读取等耗时任务，回写通过 ioLoop->runInLoop 回到 IO 线程
```

并发约束（违反会被断言拦住）：

1. 每条连接只在它所属的 IO 线程里创建、注册、读写与销毁；
2. `EventLoop::updateChannel/removeChannel` 必须在循环线程调用；
3. worker 线程不直接操作 socket，只提交数据与回调；
4. 跨线程共享的连接表用互斥量保护（accept 在主线程插入，close 在 IO 线程删除）。

## 三、一次 HTTP 请求的处理时序

| 步骤 | 位置 | 说明 |
| --- | --- | --- |
| 1 | 主线程 Acceptor | 监听 fd 可读，`accept4` 循环取到 EAGAIN，得到新连接 fd |
| 2 | 主线程 | `nextLoop()` 轮询选 IO 线程，`runInLoop` 把创建任务投递过去 |
| 3 | IO 线程 | 创建 `TcpConnection`，`Channel::tie` 绑定生命周期，注册读事件 |
| 4 | IO 线程 | 可读事件触发，`Buffer::readFd` 用 `readv` 读到 EAGAIN，上报字节数指标 |
| 5 | IO 线程 | `HttpParser` 状态机解析：请求行 → 请求头 → 请求体；半包返回等待，粘包循环解析 |
| 6 | IO 线程或 worker 线程 | `Router` 生成响应：静态文件、`/api/status`、`/metrics` |
| 7 | IO 线程 | 响应写回；写不完的数据进入输出缓冲并注册 `EPOLLOUT` |
| 8 | IO 线程 | 记录指标（状态码、延迟、字节数）与日志；短连接随后 `shutdownWrite` |

## 四、连接生命周期

```
accept → 创建 Connection（绑定 IO 线程）→ 注册读事件 → 读数据/刷新空闲定时器
      → 对端关闭或出错或空闲超时 → disableAll + remove + close
      → closeCallback 通知 main 从连接表移除 → shared_ptr 析构（在所属 IO 线程）
```

关键点：关闭顺序必须是"先取消关注事件、再从 epoll 注销、最后关闭 fd"，
否则事件循环可能继续分发已经关闭的 fd。

## 五、关键数据结构

| 结构 | 位置 | 说明 |
| --- | --- | --- |
| `Buffer` | net | 线性缓冲，读指针之前可复用；`readFd` 用 `readv` 减少系统调用 |
| `Channel` | net | fd 与事件、回调的绑定；`tie()` 在回调期间保住宿主对象 |
| `TimerQueue` | timer | 以到期时间排序的集合 + sequence 索引，只在循环线程访问 |
| `HttpParser` | http | 请求行/请求头/请求体三段状态机，记录 Content-Length 与长连接策略 |
| `Metrics` | util | 原子计数器 + 12 档直方图，`Snapshot` 给出一致性视图 |

## 六、可观测性与排障入口

| 想回答的问题 | 看哪里 |
| --- | --- |
| 现在多少连接、总共处理多少请求 | `curl /api/status` 或 `/metrics` 的 `myself_connections_*`、`myself_requests_total` |
| 延迟分布如何 | `/metrics` 的 `myself_request_latency_ms_bucket`，或 `/api/status` 的 `latencyMs` |
| 是否被过载保护拒绝 | `myself_connections_rejected_total` 与日志中的 `connection rejected` |
| 某个连接的完整过程 | 异步日志按线程 id 与 fd 过滤，格式为 `时间 [级别] 内容 - 文件:行号` |
| 热点在哪里 | `sudo ./scripts/flamegraph.sh $(pgrep webserver) 30` 生成的 SVG |
| 是否优雅退出 | 日志中的 `shutdown requested` / `event loop stopped` / `server stopped` |

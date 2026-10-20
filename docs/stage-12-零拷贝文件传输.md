# 阶段 12 创新特性：sendfile 零拷贝静态文件传输

> 阶段 11 用 304 解决了「内容没变就别传」，阶段 12 解决「内容变了要传，但别白拷贝两次」。
> 两者合起来才是完整的静态资源优化：**该省的省掉，该传的少绕路。**

## 一、改造前的数据通路

```text
磁盘 PageCache ──read(2)──▶ 用户态 std::string ──write(2)──▶ 内核 socket 发送缓冲 ──▶ 网卡
                （拷贝 1）                        （拷贝 2）
```

一次 200MB 的文件，用户态要额外吃下 200MB 内存（峰值），并做两次 200MB 的内存拷贝，
外加两次系统调用。对这个进程来说，它明明只是个「搬运工」，却把货搬进了自己家再搬出去。

## 二、改造后的数据通路

```text
磁盘 PageCache ────────── sendfile(2) ──────────▶ 内核 socket 发送缓冲 ──▶ 网卡
                        （内核态内直接对拷，用户态零参与）
```

`sendfile(out_fd, in_fd, &offset, count)` 的关键点：

- **数据不经过用户态**，`count` 由内核在 PageCache 与 socket buffer 之间搬运；
- **offset 由内核自增**，所以同一个 fd 可以分多次续传，天然支持 EAGAIN 后继续；
- 返回值：`>0` 已发送字节、`0` 输入文件提前结束、`-1` 看 errno（`EAGAIN`/`EINTR`/其他）。

## 三、实现

### 1. `FileBody`：用共享所有权管住 fd 的生命周期

```cpp
class FileBody {          // include/myself/util/FileBody.h
    int fd_; off_t offset_; size_t length_;
    ~FileBody() { if (fd_ >= 0) ::close(fd_); }
};
using FileBodyPtr = std::shared_ptr<FileBody>;
```

不用「裸 fd + 析构函数」的原因很实际：`HttpResponse` 会在 **worker 线程**构造，
再按值穿过 `runInLoop` 投递到 **IO 线程**发送，中间存在多份拷贝。裸 fd 配析构会重复
`close()`，严重时会把刚被别的连接复用的 fd 关掉——这种 bug 在生产里极难复现。

### 2. `Router`：按阈值分流

```cpp
if (zeroCopyThreshold_ > 0 && static_cast<size_t>(info.st_size) >= zeroCopyThreshold_) {
    return serveFileBody(...);   // open(2) + 只回头部
}
```

- 阈值默认 **64KB**，可用 `-Z <字节>` 调整，`-Z 0` 关闭（做 A/B 压测用）。
- **小文件仍走内存缓冲**：为 11 字节的 `404.html` 开一次 fd、多一次 `sendfile` 系统调用，
  比直接 `write` 更亏。分流不是「越零拷贝越好」，而是「别为小数据付大开销」。
- `open()` 带 `O_CLOEXEC`，避免 fork 出的子进程继承这个 fd。
- 文件体不在 `body_` 里，所以 **`Content-Length` 必须显式设置**，否则 `serialize()`
  会按空的 `body_` 写成 0。

### 3. `TcpConnection`：三阶段写，保证响应顺序

这是整个改造最容易写错的地方。一条连接上可能同时存在：

1. 还没写完的响应头（`output_` 里）
2. 待发送的文件体（`pendingFile_`）
3. 客户端 pipelining 进来的下一个响应（也会进 `output_`）

如果不管顺序，文件体可能插到下一个响应头前面，客户端直接解析错位。因此 `handleWrite`
被拆成三个阶段，并用 `preFileBytes_` 记住「文件之前还有多少字节没写完」：

```cpp
void TcpConnection::handleWrite() {
    if (preFileBytes_ > 0 && !flushOutput(preFileBytes_)) return;  // ① 先写头
    preFileBytes_ = 0;
    if (pendingFile_ && !sendPendingFile()) return;                // ② 再 sendfile
    if (output_.readableBytes() > 0 &&
        !flushOutput(output_.readableBytes())) return;             // ③ 最后写后续响应
    channel_.disableWriting();
    if (shutdownAfterWrite_) ::shutdown(socket_.fd(), SHUT_WR);
}
```

配套处理：

| 场景 | 处理 |
| --- | --- |
| socket 缓冲满（`EAGAIN`） | 保持 `EPOLLOUT` 注册，等下一次可写事件继续（ET 模式必须循环到 EAGAIN） |
| 文件发送途中被截断（`sendfile` 返回 0） | 记 WARN 并结束，避免死循环 |
| 客户端 `HEAD` 大文件 | 只发头，不发文件体；fd 随 `HttpResponse` 析构立即关闭 |
| 连接中途断开 | `handleClose()` 里 `pendingFile_.reset()`，fd 随引用归零关闭 |
| 短连接 `Connection: close` | `shutdownWrite()` 判断「还有待发文件」时延迟半关闭（复用阶段 10 的机制） |
| 同一连接 pipelining 两个大文件 | 第二个退化成 `pread` 到内存缓冲并排在文件之后，保证顺序正确 |

## 四、验证

### 1. 单元测试（`tests/unit/test_router.cpp`，新增 3 项）

| 用例 | 断言 |
| --- | --- |
| `LargeFileUsesZeroCopyFileBody` | 阈值调小后 `hasFileBody()` 为真、`body()` 为空、`Content-Length` 正确、从 fd 读回内容一致 |
| `ZeroCopyThresholdZeroKeepsBodyInMemory` | `-Z 0` 时回到内存路径 |
| `ZeroCopyConditionalRequestReturns304WithoutFileBody` | 304 不带文件体（不为 304 白开 fd） |

### 2. 接口测试（`tests/api/test_zero_copy.py`，新增 4 项）

测试自己生成 256KB 文件（大于 64KB 阈值），跑完删除：

- 大文件下载内容 sha256 一致、`Content-Length` 正确
- `HEAD` 只回头部，body 为空
- keep-alive 连续下载两次都对
- **pipelining 顺序**：同一连接先大文件（sendfile）再小页面（内存缓冲），断言第二个响应
  严格排在文件体之后——这条用例专门守上面第 3 节的边界 bug

### 3. A/B 压测脚本

```bash
./scripts/bench_zero_copy.sh 200 3
# 自动生成 www/_bench_200m.bin，分别用 -Z 0 与 -Z 65536 各下载 3 次，打印耗时与速度
```

> 数字必须自己跑出来。同一台机器上，零拷贝的收益主要体现在**用户态 CPU** 与**内存峰值**，
> 网络没打满时 wall time 差距可能很小——这不是没生效，而是瓶颈不在拷贝上。
> 想看得更清楚，用 `perf stat -e cycles,instructions,cache-misses` 或 `pidstat -r` 看 RSS。

## 五、已知边界

1. **只对静态文件生效**：`/api/status`、`/metrics` 是内存里拼出来的字符串，没有 fd 可拷。
2. **不支持 TLS**：`sendfile` 只能从文件拷到 socket，不能顺带加密；上 TLS 时通常退回
   `read + write` 或 `splice + SSL` 方案。
3. **未实现 `Range` 请求**：`FileBody` 已经带 `offset`，补 HTTP Range 只需解析
   `Range: bytes=start-end` 并设置 `206 Partial Content`，这是下一步最自然的扩展。
4. **`pread` 兜底路径有内存拷贝**：仅在「同一连接 pipelining 两个大文件」时触发，
   属于正确性优先的降级，不是常态。

## 六、面试问答

> **Q：sendfile 到底省了什么？**
>
> A：省掉「内核 PageCache → 用户态缓冲区」和「用户态缓冲区 → 内核 socket 缓冲」这两次
> 内存拷贝，以及一次 `read`/`write` 系统调用。用户态进程全程不接触文件内容，
> 所以也不会有 200MB 的内存峰值。

> **Q：非阻塞 socket 上调用 sendfile 会遇到什么？**
>
> A：和 `write` 一样会返回 `EAGAIN`。因为 offset 由内核维护，我只要保存「还剩多少字节」
> 就能续传，不需要自己算文件偏移。注意 epoll 是 ET 模式，一次可写事件必须循环调用到
> EAGAIN，否则后面不会再有事件通知。

> **Q：加了零拷贝之后，响应顺序怎么保证？**
>
> A：响应头在 `output_` 里，文件体不走缓冲区，后续响应又可能继续往 `output_` 追加。
> 我用 `preFileBytes_` 记录「文件之前还剩多少字节没写完」，`handleWrite` 严格按
> 「写头 → sendfile → 写后续」三段推进。专门写了一条 pipelining 接口测试来守这个顺序。

> **Q：为什么小文件不做零拷贝？**
>
> A：`sendfile` 需要额外 `open` 一个 fd，多一次系统调用与一次 fd 表操作。对几 KB 的
> 数据，这些开销比省下的那次拷贝更贵。所以按阈值分流，默认 64KB。

## 七、和前面阶段的关系

| 阶段 | 解决的问题 | 关键手段 |
| --- | --- | --- |
| 10 | 洪峰打不垮 | 令牌桶 429 + 连接数 503 + 延迟半关闭 |
| 11 | 内容没变别重传 | ETag / Last-Modified / 304 |
| 12 | 内容要传别绕路 | sendfile 零拷贝 + 阈值分流 |

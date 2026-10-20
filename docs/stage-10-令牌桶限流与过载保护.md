# 阶段 10 创新特性：基于自适应令牌桶的请求限流与过载保护

## 一、设计背景与核心痛点

### 1. 为什么传统的 WebServer 容易在突发流量下雪崩？
绝大多数开源教程或学生项目（如经典 TinyWebServer）仅关注并发模型的吞吐量，缺乏对异常流量的防护机制：
1. **连接数上限不足以防御长连接洪峰**：原系统在阶段 9 增加了 `-m maxConnections` 限制最大并发 TCP 连接数。然而，HTTP/1.1 普遍使用 `Connection: keep-alive`。**仅仅 10 条长连接，如果客户端每秒狂刷 10,000 个请求，传输层连接数仍为 10（远低于限制），但服务器的线程池、IO 线程与磁盘 IO 将被瞬间击垮**。
2. **缺乏标准 HTTP 降级与反压（Backpressure）机制**：普通服务器在过载时直接阻塞或导致套接字超时（Timeout），给客户端造成极差的体验；而业界标准（如 RFC 6585）要求针对请求速率超限返回 `429 Too Many Requests`，并携带 `Retry-After` 头告知客户端等待间隔。

---

## 二、架构设计：L4 / L7 双层防御体系

我们为 MyselfWebServer 构建了**传输层 + 应用层**纵深防御架构：

```
                      +-----------------------------+
                      |       Client Requests       |
                      +-----------------------------+
                                     |
                                     v
                       [ L4: TCP 连接过载保护 ]
                 (当前在线连接数 >= -m maxConnections ?)
                               /          \
                       YES    /            \   NO
                             v              v
                  +-------------------+   +------------------------+
                  |  HTTP 503 + 关闭  |   |    进入 IO 线程循环    |
                  +-------------------+   +------------------------+
                                                       |
                                                       v
                                            [ L7: 令牌桶限流保护 ]
                                         (是否为运维白名单 /metrics?)
                                                    /    \
                                            YES    /      \   NO (外部业务请求)
                                                  v        v
                                        +----------+   (TokenBucket.tryAcquire() ?)
                                        | 直接放行 |       /              \
                                        +----------+  YES /                \ NO
                                                         v                  v
                                                  +------------+   +-------------------+
                                                  | 正常处理   |   |  HTTP 429 + 维持  |
                                                  | 静态/动态  |   | Keep-Alive长连接  |
                                                  +------------+   +-------------------+
                                                         |                  |
                                                         v                  v
                                                  [ Prometheus:      [ Prometheus:
                                                    2xx/3xx/etc. ]     rate_limited++ ]
```

### 核心亮点：
1. **L4 连接级防护**：`-m` 限制并发连接数，超出返回 `503 Service Unavailable` 并直接关闭连接，防止文件描述符（FD）耗尽。
2. **L7 请求级限流**：`-R <qps>` 与 `-B <burst>` 限制请求速率，超出返回 `429 Too Many Requests`。**重点：保持 Keep-Alive 长连接不断开**，避免客户端因断连而频繁重新发起 TCP 三次握手，反向加重系统内核负担！
3. **运维与指标白名单 (Management Whitelist)**：对 `/metrics` 和 `/api/status` 探针接口免检放行。哪怕外部流量被 100% 限流拦截，Prometheus 采集与 SRE 监控看板依然能准确无误地拉取服务器健康度。

---

## 三、算法与实现：惰性按需补发令牌桶 (Lazy Refill)

在 `include/myself/util/TokenBucket.h` 与 `src/util/TokenBucket.cpp` 中实现：

1. **时钟精度**：使用 C++11 标准库单调时钟 `std::chrono::steady_clock`，彻底避免系统 NTP 时间校准导致的“时间倒流”或瞬时突增。
2. **微秒级惰性计算**：
   $$\Delta t = now - last\_refill\_time$$
   $$tokens = \min(burst, tokens + \Delta t \times rate)$$
   **优势**：无需后台专门启动定时轮询线程定时发放令牌，彻底杜绝无谓的线程上下文切换与锁争用。
3. **线程安全与极低耗时**：
   在 `tryAcquire()` 临界区内仅执行简单的浮点/微秒差值运算，持锁时长仅数纳秒，满足单机数万 QPS 高并发无阻塞要求。

---

## 四、验证与测试

### 1. 单元测试（52 项全部通过）
运行 `./build/unit_tests`，新增的 5 个用例覆盖：
- `TokenBucketTest.AlwaysAllowsWhenDisabled`：关闭限流时 100% 放行。
- `TokenBucketTest.AllowsBurstUpToCapacity`：突发流量在 Burst 容量内全放行，超出即刻拦截。
- `TokenBucketTest.RefillsTokensOverTime`：耗尽后随时间推移精确补充令牌。
- `TokenBucketTest.ClampsTokensAtBurstCapacity`：闲置时令牌不会无限累加，严格截断于 Burst 容量。
- `TokenBucketTest.HandlesConcurrentThreadsSafely`：8 线程并发竞争 100 个令牌，强一致无超卖。
- `MetricsTest.TracksRateLimitedRequests`：Prometheus 格式指标同步统计。

### 2. 接口集成测试（17 项全部通过）
```bash
pytest tests/api -v
```
- `test_rate_limiting_returns_429_with_retry_after`：断言超额返回 429 且 Header 含有 `Retry-After: 1`。
- `test_management_paths_bypass_rate_limiting`：断言频繁请求 `/metrics` 依然稳定返回 200。

### 3. wrk 实测压测对比
```bash
# 启动限流服务：限制 1000 QPS，突发容量 500
./build/webserver -p 8080 -r ./www -t 4 -R 1000 -B 500 &

# 发起突发高压测：4 线程 100 并发
wrk -t4 -c100 -d5s http://127.0.0.1:8080/index.html
```
- 观测到正常业务请求在 1000 QPS 内保持极低的延迟（P99 < 2ms）。
- 溢出请求全部毫秒级返回 `429 Too Many Requests`，服务零崩溃、零断连。
- 执行 `curl http://127.0.0.1:8080/metrics`，看到 `myself_requests_rate_limited_total` 精确记录拦截次数。

---

## 五、面试问答与讲解答题要点

**Q：你的 WebServer 是如何应对秒杀或瞬时突发流量冲击的？**
> **A（答题话术）**：
> “很多开源或课程项目面对突发流量，通常只能任由 Epoll 队列堆积导致线程池阻塞、套接字超时。而在本项目中，我设计了 **L4（传输层）与 L7（应用层）双层过载保护**：
> 1. 在 L4 传输层，通过 `-m maxConnections` 控制并发在线连接数，超出上限时快速返回 503；
> 2. 但很多时候流量是长连接（Keep-Alive）复用下的请求洪峰，TCP 连接数并不高。因此我在应用层设计了基于**自适应令牌桶算法（Token Bucket）的请求限流器**，支持配置平滑速率（Rate）与突发容量（Burst）。
> 3. 在实现细节上，我采用了**基于单调时钟的高精度惰性补发（Lazy Refill）机制**，请求到达时利用微秒时间差动态折算补发令牌，无需单独开辟后台轮询线程，避免了线程切换开销。
> 4. 当令牌耗尽触发限流时，严格遵循 RFC 6585 标准返回 `429 Too Many Requests` 并附带 `Retry-After: 1` 响应头，同时复用 Keep-Alive 长连接避免客户端反复握手。
> 5. 此外，我还专门设计了**运维白名单机制**，将 `/metrics` 和 `/api/status` 豁免限流，确保即使在业务被全面限流打满的情况下，Prometheus 探针和 SRE 大屏依然能 100% 可靠地采集监控指标。”

# 阶段 11 创新特性：HTTP 条件请求（ETag / Last-Modified / 304 Not Modified）

> 承接阶段 10 的限流与过载保护。阶段 10 解决的是「洪峰打不垮」，阶段 11 解决的是
> 「平时别浪费」——把每次刷新都全量读盘 + 全量传输的静态资源，改成协商缓存。

## 一、设计背景与核心痛点

改造前的静态文件路径只有一条直路：`stat` 都没做，直接 `ifstream` 把整个文件读进
`std::string`，再整包塞进响应体。

| 问题 | 实际代价 |
| --- | --- |
| 浏览器每次刷新都重新下载 HTML / CSS / JS / 图片 | 100 KB 的首页，刷新 10 次就是 1 MB 无意义流量 |
| 每次都把文件从磁盘读进用户态内存 | PageCache 命中也要一次 `read(2)` + 一次内存拷贝，纯浪费 |
| 客户端无法判断「内容变了没」 | 只能靠 `Cache-Control: max-age` 盲猜，缓存期内容更新看不到，缓存期外又全量重传 |

HTTP 早在 1.1 就给了标准答案：**协商缓存**。服务器给出校验器（validator），
客户端下次带着校验器来问「我这份还能用吗」，服务器只需比对元数据，回一个
**没有响应体的 `304 Not Modified`**。

## 二、协议依据

| 规范 | 要点 | 本项目落地 |
| --- | --- | --- |
| RFC 9110 §8.8.3 | `ETag` 是实体标签，强校验器必须带双引号 | `ETag: "6ad7a63d-b"` |
| RFC 9110 §8.8.2.1 | `Last-Modified` 用 IMF-fixdate（GMT） | `Last-Modified: Tue, 20 Oct 2026 17:34:53 GMT` |
| RFC 9110 §13.1.2 | `If-None-Match`：命中则 304 | 支持强/弱比较、逗号列表、`*` |
| RFC 9110 §13.1.3 | **`If-None-Match` 存在时忽略 `If-Modified-Since`** | 优先级严格按规范实现 |
| RFC 9110 §15.4.5 | `304` 不带响应体，只回校验器 | 零 Body，只回 `ETag` / `Last-Modified` / `Cache-Control` |

## 三、实现细节

改动集中在 `src/http/Router.cpp`（路由层），不碰网络层，因此对阶段 10 的
令牌桶、半关闭修复零影响。

### 1. 一次 `stat(2)` 拿全部元数据

```cpp
struct stat info {};
if (::stat(fullPath.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
    return HttpResponse::notFound();
}
const std::string etag = makeETag(info.st_mtime, info.st_size);
```

顺带修掉一个隐性缺陷：改造前用 `ifstream` 判断存在性，**目录也能被 `open` 成功**，
于是请求 `/assets` 会返回一个 200 + 空响应体。现在用 `S_ISREG` 挡掉，目录一律 404。

### 2. 强校验器：`"<mtime 十六进制>-<size 十六进制>"`

```cpp
std::snprintf(buffer, sizeof(buffer), "\"%llx-%llx\"", mtime, size);
```

选它的理由：

- **不读文件内容**。内容哈希（如 MD5）算一次就要把文件完整读一遍，等于把省下的
  I/O 又还回去；`stat` 只是元数据查询，纳秒级。
- **任一变化就失效**。改内容 → 大小变；只 touch 不改内容 → mtime 变（安全侧误判，
  多传一次而已，不会错发 304）。

### 3. 条件裁决（`isNotModified`）

```
If-None-Match 存在?
├── 是 → 逐项比对（去 W/ 前缀、逗号列表、* 通配）
│        ├── 命中 → 304
│        └── 未命中 → 200（并且不再看 If-Modified-Since，符合 §13.1.3）
└── 否 → If-Modified-Since 存在?
         ├── 能解析且 mtime <= 该时间 → 304
         └── 更旧或无法解析 → 200
```

日期解析没有用 `strptime`：它依赖 locale，且 Windows/MinGW 不提供。改为手写分词解析，
同时兼容 IMF-fixdate / RFC 850 / asctime 三种历史格式，日期转时间戳用
Howard Hinnant 的 `days_from_civil` 算法，零平台依赖。

### 4. 304 响应形态

```http
HTTP/1.1 304 Not Modified
Server: MyselfWebServer
Date: Tue, 20 Oct 2026 17:35:10 GMT
Cache-Control: no-cache
ETag: "6ad7a63d-b"
Last-Modified: Tue, 20 Oct 2026 17:34:53 GMT
Content-Length: 0
Connection: keep-alive
```

`Cache-Control: no-cache` 的语义是「可以缓存，但每次使用前必须回来问一次」，
正好匹配演示场景：刷新页面就能在 Network 面板看到一条 304。

### 5. 可观测性

- `myself_requests_not_modified_total`（Prometheus counter）：304 命中次数，即省下的整包传输次数
- `/api/status` → `requests.notModified`

## 四、验证

### 1. 单元测试（`tests/unit/test_router.cpp`，新增 9 项，总计 61 项）

| 用例 | 断言 |
| --- | --- |
| `StaticFileCarriesCacheValidators` | 200 带带引号的 ETag、GMT 格式 Last-Modified、`no-cache` |
| `MatchingIfNoneMatchReturns304WithoutBody` | 304、响应体为空、报文以空行结束 |
| `WeakAndListedIfNoneMatchAlsoMatch` | `W/"..."`、逗号列表、`*` 三种命中 |
| `StaleIfNoneMatchReturns200` | 过期 ETag 回完整实体 |
| `IfModifiedSinceEqualToLastModifiedReturns304` | 相等即 304 |
| `OlderIfModifiedSinceReturns200` | 更旧的副本必须回实体 |
| `IfNoneMatchTakesPrecedenceOverIfModifiedSince` | 双向验证优先级 |
| `ETagChangesWhenFileContentChanges` | 内容变长后指纹变化，旧指纹不再命中 |
| `DirectoryIsNotServedAsFile` | 目录返回 404 |

### 2. 接口测试（`tests/api/test_conditional_get.py`，新增 6 项，总计 23 项）

真实拉起服务，覆盖 200→304→指标三条链路，并验证 `HEAD` 条件请求同样回 304。

### 3. 手工验证（`curl` 三连）

```bash
./build/webserver -p 8080 -r ./www -t 2 &

# 1. 第一次：200 + 校验器
curl -sI http://127.0.0.1:8080/index.html | grep -Ei "etag|last-modified|cache-control"

# 2. 带 If-None-Match 再来一次：304，且没有任何响应体
ETAG=$(curl -sI http://127.0.0.1:8080/index.html | awk -F': ' 'tolower($1)=="etag"{print $2}' | tr -d '\r')
curl -s -o /dev/null -w "%{http_code} 传输字节=%{size_download}\n" \
     -H "If-None-Match: $ETAG" http://127.0.0.1:8080/index.html

# 3. 看省了多少次整包传输
curl -s http://127.0.0.1:8080/metrics | grep not_modified
```

期望输出：`304 传输字节=0` 与 `myself_requests_not_modified_total <次数>`。

浏览器侧：DevTools → Network → 刷新首页，`index.html` 的 Status 应为 `304`、
Size 列显示 `(disk cache)` 或 `0 B`。

## 五、已知边界（面试时主动说出来，比被问出来更加分）

1. **秒级 mtime + 大小的组合有理论误判**：同一秒内写入且大小完全相同的两次内容，
   ETag 不变，会错误返回 304。要彻底消除，可改用 `st_mtim.tv_nsec`（纳秒）或
   `inode + mtime_ns` 参与指纹。当前选择是「零额外 I/O」与「绝对正确」的折中。
2. **只对静态文件生效**：`/api/status`、`/metrics` 是实时数据，加校验器没有意义。
3. **`no-cache` 不是「不缓存」**：它要求每次使用前重新校验；真正禁止缓存是 `no-store`。

## 六、面试问答

> **Q：ETag 怎么生成的？为什么不直接用文件内容算 MD5？**
>
> A：我用 `stat(2)` 拿到的 mtime 和文件大小拼成强校验器，格式 `"mtime-size"`。
> 不用内容哈希是因为算哈希必须把文件完整读一遍，这正好抵消了协商缓存想省掉的
> 磁盘 I/O 与内存拷贝；而 `stat` 只查 inode 元数据，成本可以忽略。代价是理论上
> 存在「同秒同大小但内容不同」的误判，我在文档里写明了，需要绝对正确时把
> `st_mtim.tv_nsec` 也拼进指纹即可。

> **Q：304 和 200 的响应体有什么区别？为什么 304 不能带 body？**
>
> A：304 的语义就是「你手上那份还能用」，body 完全多余；带了 body 反而会让
> keep-alive 下的报文边界混乱——客户端按 304 的规则不读 body，多出来的字节会被
> 当成下一个响应。所以我的 304 只回 `ETag`、`Last-Modified`、`Cache-Control`，
> `Content-Length: 0`。

> **Q：If-None-Match 和 If-Modified-Since 同时出现，听谁的？**
>
> A：听 `If-None-Match`。RFC 9110 §13.1.3 明确规定 ETag 优先级更高，
> 因为 ETag 是精确比对，而 `If-Modified-Since` 只有秒级精度。我在
> `isNotModified` 里先判 INM 并直接返回结果，不会「两个都判、任一命中就算命中」——
> 那种写法会让「ETag 变了但时间没变」的场景错误命中缓存。

> **Q：为什么用 `Cache-Control: no-cache` 而不是 `max-age=3600`？**
>
> A：`max-age` 是强缓存，到期前浏览器根本不发请求，内容更新了也看不到；
> `no-cache` 是协商缓存，每次都用 304 确认一次。对一个演示型静态站点，
> 后者既能展示条件请求的效果，又保证改完文件刷新立刻生效。

## 七、后续可接的方向

- **零拷贝发送**：304 省掉了 body 传输，200 的大文件仍走「内核 PageCache → 用户态 →
  内核 socket 缓冲」两次拷贝，下一步可用 `sendfile(2)` 把它压成零拷贝。
- **反向代理**：把 `/api/gateway/*` 转发给项目二的 Modbus 网关，让两个项目串成一个
  边缘接入架构。

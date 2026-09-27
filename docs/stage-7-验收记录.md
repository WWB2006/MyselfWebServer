# 阶段 7 验收记录

> 填写方式：把「实际结果」列替换成真实输出。指标与实际请求数是否一致，是本阶段最重要的验收点。

## 一、环境信息

| 项 | 命令 | 实际结果 |
| --- | --- | --- |
| CPU 与内存 | `nproc`、`free -h` |  |
| 系统与内核 | `cat /etc/os-release \| head -2`、`uname -r` |  |
| 启动参数 | `./build/webserver -t 4 -l warn -g logs -p 8080` |  |

## 二、功能验收

| 序号 | 检查项 | 命令 | 期望结果 | 实际结果 |
| --- | --- | --- | --- | --- |
| 1 | /metrics 可访问 | `curl -s http://127.0.0.1:8080/metrics \| head -20` | 输出 HELP/TYPE/值三段式 |  |
| 2 | 内容类型 | `curl -sI http://127.0.0.1:8080/metrics` | `Content-Type: text/plain; version=0.0.4` |  |
| 3 | 解析合法性 | `curl -s .../metrics \| promtool check metrics` | 无错误（未装 promtool 可跳过） |  |
| 4 | /api/status 字段 | `curl -s http://127.0.0.1:8080/api/status` | 含 connections/requests/bytes/latencyMs/logLines |  |
| 5 | 状态码分类 | 连续请求 10 次不存在的路径 | `4xx` 计数增加 10 |  |
| 6 | 方法分类 | 用 `curl -I` 请求一次 | `method="HEAD"` 计数增加 |  |
| 7 | 连接计数 | 压测前后对比 | `current` 回到 0，`total` 增加，`max` 记录峰值 |  |
| 8 | 字节数 | 压测后查看 | read/written 都远大于 0，量级与响应体大小相符 |  |
| 9 | 日志行数 | 间隔 10 秒两次抓取 | `myself_log_lines_total` 单调增长 |  |
| 10 | 运行时长 | `myself_uptime_seconds` | 与进程实际运行时间一致 |  |

## 三、指标与实际请求数的核对（核心）

```bash
# 1. 记录压测前的计数器
curl -s http://127.0.0.1:8080/metrics | grep myself_requests_total

# 2. 压测（客户端在宿主机执行）
wrk -t4 -c200 -d60s --latency http://<虚拟机IP>:8080/index.html

# 3. 再取一次计数器
curl -s http://127.0.0.1:8080/metrics | grep myself_requests_total
```

| 指标 | 压测前 | 压测后 | 差值 | wrk 报告值 | 是否一致 |
| --- | --- | --- | --- | --- | --- |
| `myself_requests_total` |  |  |  | Requests |  |
| `myself_requests_by_status{class="2xx"}` |  |  |  | 非 2xx 数应为 0 |  |
| `myself_request_latency_ms_count` |  |  |  | Requests |  |
| `myself_request_latency_ms_quantile`（0.99） | - | - | - | wrk 的 P99 | 应同量级 |
| `myself_connections_total` |  |  |  | 连接数（keep-alive 下小于请求数） | 说明即可 |

结论（说明差值来源，例如 keep-alive 复用连接，或部分请求在压测结束后才完成）：

## 四、指标开销

| 配置 | 并发 | QPS | P99（毫秒） | 说明 |
| --- | --- | --- | --- | --- |
| 阶段 6 基线（无指标） | 200 |  |  | 引用阶段 6 记录 |
| 阶段 7（开启指标） | 200 |  |  | 差值即为指标开销 |

## 五、可选：接入 Prometheus 与 Grafana（加分项）

`prometheus.yml` 片段：

```yaml
scrape_configs:
  - job_name: myselfwebserver
    scrape_interval: 5s
    static_configs:
      - targets: ["<虚拟机IP>:8080"]
```

| 检查 | 期望 | 实际 |
| --- | --- | --- |
| Prometheus targets 页面 | 目标任务状态为 UP |  |
| 查询 `rate(myself_requests_total[1m])` | 能看到实时 QPS 曲线 |  |
| 查询 `histogram_quantile(0.99, rate(myself_request_latency_ms_bucket[5m]))` | 能看到 P99 曲线 |  |

## 六、结论与下一步

结论：

| 问题 | 影响 | 计划 |
| --- | --- | --- |
|  |  |  |

下一步：阶段 8 把单元测试与接口测试补齐（含指标一致性检查）并接入 GitHub Actions。

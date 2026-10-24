# 工业物联网三项目全流程测试与实机验证操作手册

> **文档版本**：v2.0 (全特性工业发布版)  
> **适用环境**：Linux (Ubuntu 22.04 LTS / 虚拟机 IP: `192.168.71.129`) & Windows 宿主机浏览器  
> **涵盖范围**：项目一单项分阶段测试（阶段 1~13）、项目二/三独立自动化测试、三大系统联合闭环测试  

---

## 目录
1. [终端规划全景速查表（用到几个终端？）](#一终端规划全景速查表用到几个终端)
2. [项目一：MyselfWebServer 分阶段专项测试（终端 1 + 终端 2）](#二项目一myselfwebserver-分阶段专项测试)
   - [准备工作：编译最新服务端与单测套件](#准备工作编译最新服务端与单测套件)
   - [阶段 1~4：多线程 Reactor 与基础 HTTP 服务验证](#阶段-14多线程-reactor-与基础-http-服务验证)
   - [阶段 5：空闲连接超时剔除验证（-i 参数）](#阶段-5空闲连接超时剔除验证-i-参数)
   - [阶段 6：双缓冲异步日志系统验证（-l, -g, -s 参数）](#阶段-6双缓冲异步日志系统验证-l--g--s-参数)
   - [阶段 7：运行指标与 Prometheus 监控切面验证](#阶段-7运行指标与-prometheus-监控切面验证)
   - [阶段 8：企业级单元测试矩阵（99 项用例 100% PASS）](#阶段-8企业级单元测试矩阵99-项用例-100-pass)
   - [阶段 9：高并发压测与连接数上限保护验证（-m 参数）](#阶段-9高并发压测与连接数上限保护验证-m-参数)
   - [阶段 10：自适应无锁令牌桶限流与 429 协议级削峰（-L, -b 参数）](#阶段-10自适应无锁令牌桶限流与-429-协议级削峰-l--b-参数)
   - [阶段 11：RFC 9110 协商缓存与 ETag 304 零包体验证](#阶段-11rfc-9110-协商缓存与-etag-304-零包体验证)
   - [阶段 12：Linux sendfile(2) 零拷贝静态文件传输 A/B 压测](#阶段-12linux-sendfile2-零拷贝静态文件传输-ab-压测)
   - [阶段 13：微反向代理网关与 502/504 熔断断路器验证（-P 参数）](#阶段-13微反向代理网关与-502504-熔断断路器验证-p-参数)
3. [项目二与项目三：独立自动化测试套件](#三项目二与项目三独立自动化测试套件)
   - [项目二：ModbusGateway 单元测试与协议测试](#1-项目二modbusgateway-自动化测试)
   - [项目三：collector-platform 平台回归测试](#2-项目三collector-platform-自动化测试)
4. [三大项目全链路联合闭环实机演示（需 4~5 个终端）](#四三大项目全链路联合闭环实机演示)
5. [Windows 宿主机 4 大控制台访问清单](#五windows-宿主机-4-大控制台访问清单)

---

## 一、终端规划全景速查表（用到几个终端？）

根据测试场景不同，终端的使用分配如下：

### 场景 A：单独测试项目一（`MyselfWebServer` 阶段 1 ~ 阶段 13）
只需要 **2 个终端**：
*   **终端 1（服务端终端）**：负责前台或后台启动不同参数的 `webserver`，观察日志输出。
*   **终端 2（客户端测试终端）**：负责执行 `curl`、`nc`、`wrk`、`pytest` 等测试指令，验证响应状态。

### 场景 B：三大系统全链路端到端闭环联合验证
需要 **4 ~ 5 个终端**（或借助后台 `&` 运行，推荐开 4 个终端便于观察各模块实时日志）：
*   **终端 1**：运行项目二 **Modbus 软从站模拟器**（监听 5020 端口，可随时注入故障）。
*   **终端 2**：运行项目二 **Modbus 采集 Daemon 与 Web 看板后端**（8081 端口）。
*   **终端 3**：运行项目一 **MyselfWebServer 边缘反向代理网关**（8080 端口，代理 `/api/gateway` -> 8081）。
*   **终端 4**：运行项目三 **FastAPI 质量分析平台**（8000 端口）并触发自动化采集与 HTML 报表生成。
*   **宿主机 Windows**：打开 Chrome/Edge 浏览器，同时监控 4 个交互看板。

---

## 二、项目一：MyselfWebServer 分阶段专项测试

### 准备工作：编译最新服务端与单测套件
打开 **终端 1** 执行：
```bash
cd ~/MyselfWebServer
# 开启单测编译开关，采用 Release 高性能优化
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWEBSERVER_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```
> **检查点**：`build/webserver` 和 `build/tests/unit/unit_tests` 均成功生成。

---

### 阶段 1~4：多线程 Reactor 与基础 HTTP 服务验证

*   **终端 1（启动服务）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -r ./www
    ```
    > 终端输出 `io pool io started with 4 thread(s)`，主线程与 4 个 IO 线程进入 Epoll ET 事件循环。

*   **终端 2（发送测试请求）**：
    ```bash
    # 1. 验证基础 200 OK 与静态首页
    curl -i http://127.0.0.1:8080/index.html
    # 期望输出：HTTP/1.1 200 OK，包含首页 HTML

    # 2. 验证 404 Not Found 容错
    curl -i http://127.0.0.1:8080/not_exist.html
    # 期望输出：HTTP/1.1 404 Not Found
    ```

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 5：空闲连接超时剔除验证（-i 参数）

*   **终端 1（启动带 3 秒超时的服务）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -i 3 -r ./www
    ```

*   **终端 2（模拟空闲连接）**：
    ```bash
    nc -v 127.0.0.1 8080
    ```
    > 连上后显示：`Connection to 127.0.0.1 8080 port [tcp/http-alt] succeeded!`  
    > **静置 3 秒不要输入任何内容**。

*   **观察点**：
    1. 在 **终端 1** 会看到黄色警告日志：  
       `[WARN ] timeout fd=... peer=127.0.0.1:... idle over 3s, closing`
    2. 在 **终端 2** 的 `nc` 界面按一下【回车键（Enter）】，`nc` 尝试发送换行符瞬间感知对端已关闭，终端立刻退出返回提示符。

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 6：双缓冲异步日志系统验证（-l, -g, -s 参数）

*   **终端 1（启动服务：开启 info 级别日志，输出到 logs 目录，32MB 轮转）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -l info -g logs -s 32 -r ./www
    ```

*   **终端 2（发起请求并查看日志落地）**：
    ```bash
    curl http://127.0.0.1:8080/index.html
    sleep 1
    ls -lh logs/
    cat logs/webserver_*.log | tail -n 5
    ```
    > **期望输出**：`logs/` 下生成形如 `webserver_20261025-xxxx.log`，日志包含毫秒级时间戳、日志等级、线程 tid 及请求详情。

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 7：运行指标与 Prometheus 监控切面验证

*   **终端 1（启动服务）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -r ./www
    ```

*   **终端 2（拉取可观测性指标）**：
    ```bash
    # 1. 查询 JSON 运行状态
    curl -s http://127.0.0.1:8080/api/status
    # 期望输出包含："online":0, "requests":..., "qps":..., "ioThreads":4

    # 2. 查询标准 Prometheus 监控打点
    curl -s http://127.0.0.1:8080/metrics
    # 期望输出包含：myself_requests_total, myself_connections_active 等打点
    ```

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 8：企业级单元测试矩阵（99 项用例 100% PASS）

本阶段只需 **1 个终端**，验证系统整体代码健康度与回归兼容性：

*   **终端 1（执行全部单测）**：
    ```bash
    cd ~/MyselfWebServer

    # 1. 运行 68 个 GoogleTest C++ 单元测试
    ./build/tests/unit/unit_tests
    # 期望输出：[  PASSED  ] 68 tests.

    # 2. 运行 31 个 Python Pytest 接口自动化测试
    pytest tests/api -v
    # 期望输出：31 passed in ...s (100%)
    ```

---

### 阶段 9：高并发压测与连接数上限保护验证（-m 参数）

*   **终端 1（启动服务：限制最大并发连接数 100）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -m 100 -r ./www
    ```

*   **终端 2（发起高并发压力测试）**：
    ```bash
    # 使用 wrk 发起 200 个并发连接测试 5 秒
    wrk -t4 -c200 -d5s http://127.0.0.1:8080/index.html
    # 检查状态机拒绝统计
    curl -s http://127.0.0.1:8080/api/status | grep "rejected"
    ```
    > **期望现象**：服务平稳承压不崩溃，超出 100 的多余连接被统计在 `rejected` 计数器中。

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 10：自适应无锁令牌桶限流与 429 协议级削峰（-L, -b 参数）

*   **终端 1（启动服务：限流 QPS=50，突发桶上限 100）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -L 50 -b 100 -r ./www
    ```

*   **终端 2（瞬时打入 150 次突发请求）**：
    ```bash
    for i in {1..150}; do curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8080/index.html; done | sort | uniq -c
    ```
    > **期望输出**：
    > - 约 100 个请求返回 `200`（桶内初始配额）
    > - 约 50 个溢出请求返回 `429`（协议级削峰）

*   **终端 2（验证 429 响应头与白名单特权通道）**：
    ```bash
    # 观察 429 与 Retry-After
    curl -i http://127.0.0.1:8080/index.html | grep -E "429|Retry-After"
    # 期望输出：HTTP/1.1 429 Too Many Requests, Retry-After: 1

    # 验证 /api/status 与 /metrics 白名单穿透
    curl -i http://127.0.0.1:8080/api/status | head -n 1
    # 期望输出：HTTP/1.1 200 OK（不受限流影响，确保运维不盲目）
    ```

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 11：RFC 9110 协商缓存与 ETag 304 零包体验证

*   **终端 1（启动服务）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -r ./www
    ```

*   **终端 2（验证协商缓存状态机）**：
    ```bash
    # 1. 首次请求：获取服务端的 ETag 指纹
    curl -i http://127.0.0.1:8080/index.html | grep -E "ETag|Last-Modified"
    # 期望输出：ETag: "6715-xxxx"

    # 2. 二次请求：携带 If-None-Match 条件头发起验证
    ETAG=$(curl -sI http://127.0.0.1:8080/index.html | grep -i "ETag" | awk '{print $2}' | tr -d '\r')
    curl -i -H "If-None-Match: $ETAG" http://127.0.0.1:8080/index.html
    ```
    > **期望输出**：
    > ```text
    > HTTP/1.1 304 Not Modified
    > ETag: "..."
    > Content-Length: 0
    > ```
    > *(完全零包体传输，消除磁盘与网络冗余 I/O)*

*   **清理**：在终端 1 按 `Ctrl + C` 退出服务。

---

### 阶段 12：Linux sendfile(2) 零拷贝静态文件传输 A/B 压测

本阶段使用自动化脚本对比传统用户态内存拷贝与 Linux 内核零拷贝的性能差距：

*   **终端 1（执行一键 A/B 基准压测）**：
    ```bash
    cd ~/MyselfWebServer
    # 1. 生成 50MB 静态测试大文件
    dd if=/dev/urandom of=www/test_50mb.dat bs=1M count=50

    # 2. 运行自动化零拷贝对比脚本
    bash scripts/bench_zero_copy.sh
    ```
    > **期望输出**：
    > ```text
    > [-Z 0    传统读取] 传输耗时: 0.538s | 吞吐量:  92.9 MB/s
    > [-Z 64KB 零拷贝]   传输耗时: 0.072s | 吞吐量: 688.6 MB/s
    > [性能提升] 吞吐量暴涨 +641.3%
    > ```

---

### 阶段 13：微反向代理网关与 502/504 熔断断路器验证（-P 参数）

*   **终端 1（启动后端模拟服务，模拟监听 8081 端口）**：
    ```bash
    cd ~/ModbusGateway
    python3 scripts/web_dashboard.py 8081
    ```
    > 提示：`Modbus Industrial Gateway Web Dashboard listening on port 8081`

*   **终端 2（启动 MyselfWebServer，开启反向代理透传）**：
    ```bash
    cd ~/MyselfWebServer
    ./build/webserver -p 8080 -t 4 -r ./www -P 127.0.0.1:8081
    ```
    > 提示：`reverse proxy enabled: /api/gateway -> 127.0.0.1:8081`

*   **终端 3（客户端测试请求）**：
    ```bash
    # 1. 验证透明代理透传与响应头注入
    curl -i http://127.0.0.1:8080/api/gateway/api/status
    ```
    > **期望输出**：
    > - `HTTP/1.1 200 OK`
    > - `X-Proxy-By: MyselfWebServer`
    > - `X-Upstream-Addr: 127.0.0.1:8081`

    ```bash
    # 2. 模拟后端宕机，测试 502 熔断
    # 在 终端 1 按 Ctrl+C 杀掉 8081 进程
    # 再次在 终端 3 发送请求：
    curl -i http://127.0.0.1:8080/api/gateway/api/status
    ```
    > **期望输出**：`HTTP/1.1 502 Bad Gateway`（触发毫秒级快速断路保护，而非卡死挂起）。

*   **清理**：在终端 2 按 `Ctrl + C` 退出服务。

---

## 三、项目二与项目三：独立自动化测试套件

### 1. 项目二：ModbusGateway 自动化测试
打开 **终端 1**：
```bash
cd ~/ModbusGateway

# (1) 运行 C++ 核心单元测试（CRC-16 查表法、报文帧解析）
cd build && ctest --output-on-failure && cd ..

# (2) 运行 Python API 协议交叉测试（重试、故障注入、数据完整性）
pytest tests/api -v
```
> **期望结果**：CTest 与 Pytest 全部用例 100% PASSED。

### 2. 项目三：collector-platform 自动化测试
打开 **终端 1**：
```bash
cd ~/collector-platform
# 运行全量 370+ 项工业数据调度、质量码评估、时序图表引擎测试
pytest -v
```
> **期望结果**：`370+ passed in ...s`，测试覆盖率 96%。

---

## 四、三大项目全链路联合闭环实机演示

这是求职面试现场最震撼的**“软硬件解耦全闭环”**演示流程：

```
[终端1: 从站模拟器 5020] <--- (Modbus-TCP) --- [终端2: 采集网关 8081]
                                                    ^
                                                    | (反向代理 /api/gateway)
[Windows 浏览器看板] <--- (HTTP:8080) --- [终端3: MyselfWebServer 8080]
                                                    ^
                                                    |
[终端4: collector-platform 8000/8082 质量分析与离线 SVG HTML 报表]
```

### 步骤 1：终端 1 启动 Modbus 软从站模拟器（5020 端口）
```bash
cd ~/ModbusGateway
./build/src/slave_simulator -p 5020 -u 1
```
> 保持该终端打开，显示数据区初始化完毕，正在监听 5020。

### 步骤 2：终端 2 启动 Modbus 网关控制台（8081 端口）
```bash
cd ~/ModbusGateway
python3 scripts/web_dashboard.py 8081
```

### 步骤 3：终端 3 启动 MyselfWebServer 边缘接入网关（8080 端口）
```bash
cd ~/MyselfWebServer
./build/webserver -p 8080 -t 4 -r ./www -P 127.0.0.1:8081
```

### 步骤 4：终端 4 启动工业质量平台大屏与报表预览服务
```bash
cd ~/collector-platform
# 启动实时 API 与看板
uvicorn app.main:app --host 0.0.0.0 --port 8000 &
# 启动报表预览 HTTP 服务
python3 -m http.server 8082 --directory reports &
```

### 步骤 5：在终端 4 执行一次真实采集并生成 HTML 报告
```bash
cd ~/collector-platform
python3 scripts/run_collector.py --seconds 20
python3 scripts/generate_report.py --range 1h
```
> 采集 20 秒真实从站数据并自动生成嵌入 SVG 图表的离线质量报告。

### 步骤 6：极具说服力的“故障注入与自愈”闭环演示
1. **注入 CRC 错码故障**：
   在 **终端 1** 按 `Ctrl + C`，然后加载故障模式重新启动：
   ```bash
   ./build/src/slave_simulator -p 5020 -u 1 -f config/fault_profiles/crc_error.json
   ```
2. **观察链路告警响应**：
   - 在 **终端 4** 再次运行采集：`python3 scripts/run_collector.py --seconds 20`
   - 终端精准输出：`WARNING 设备 plc-01 采集失败：CRC 校验错误 -> 触发告警`；
   - 刷新浏览器 `http://192.168.71.129:8000/`，告警看板瞬间变红。
3. **故障恢复（自愈）**：
   - 在 **终端 1** 按 `Ctrl + C` 恢复正常启动：`./build/src/slave_simulator -p 5020 -u 1`
   - 再次采集后告警自动解除，重新生成报告展示完整的故障恢复甘特图。

---

## 五、Windows 宿主机 4 大控制台访问清单

在 Windows 的 Edge / Chrome 浏览器中直接打开以下地址：

| 序号 | 监控与看板入口 | 浏览器访问链接 | 核心验证看点 |
| :---: | :--- | :--- | :--- |
| **1** | **项目一：高性能网关控制台** | [`http://192.168.71.129:8080/index.html`](http://192.168.71.129:8080/index.html) | • 在线连接数、实时 QPS<br>• 验证静态资源 sendfile 零拷贝直传 |
| **2** | **项目一透传 -> 项目二工业网关控制台** | [`http://192.168.71.129:8080/api/gateway/`](http://192.168.71.129:8080/api/gateway/) | • **亮点**：经由 8080 反向代理透传！<br>• 5020 从站保持寄存器跳变与实时点位 |
| **3** | **项目三：工业质量与告警实时大屏** | [`http://192.168.71.129:8000/`](http://192.168.71.129:8000/) | • FastAPI 驱动的 ECharts 动态曲线<br>• 设备在线状态与报警规则引擎 |
| **4** | **项目三：自包含离线 HTML 测试报表** | [`http://192.168.71.129:8082/`](http://192.168.71.129:8082/) | • 原生内嵌 SVG 时序波形图<br>• P95/P99 响应耗时与故障自愈事件甘特轴 |

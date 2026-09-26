# 阶段 4 验收记录

> 填写方式：把「实际结果」列替换成你机器上的真实输出。压测数据是本阶段的核心交付物，
> 没有数据的“性能提升”不能写进简历。

## 一、环境信息

| 项 | 命令 | 实际结果 |
| --- | --- | --- |
| CPU 核数 | `nproc` |  |
| 内存 | `free -h` |  |
| 系统与内核 | `cat /etc/os-release \| head -2`、`uname -r` |  |
| 编译器 | `g++ --version \| head -1` |  |
| 构建类型 | `cmake -B build -DCMAKE_BUILD_TYPE=Release` |  |
| 压测客户端位置 | 宿主机 / 另一台机器 |  |

## 二、功能验收

| 序号 | 检查项 | 命令 | 期望结果 | 实际结果 |
| --- | --- | --- | --- | --- |
| 1 | 单线程基线可运行 | `./build/webserver -t 0 -p 8080` | 正常启动，日志显示 0 io 线程 |  |
| 2 | 多线程启动 | `./build/webserver -t 4 -p 8080` | 日志显示 4 个 io 线程 |  |
| 3 | 连接分散到多个线程 | 开 6 个 `nc` 连接，观察 `[accept] ... -> io-x` | 至少命中两个不同 io 线程 |  |
| 4 | 静态文件 | `curl -i http://127.0.0.1:8080/index.html` | 200 且带 Content-Length |  |
| 5 | 404 | `curl -i http://127.0.0.1:8080/not-exist` | 404 |  |
| 6 | 状态接口 | `curl -s http://127.0.0.1:8080/api/status` | JSON 中含 ioThreads 字段 |  |
| 7 | worker 池 | `./build/webserver -t 4 -w 4`，压测静态文件 | 正常返回，无崩溃 |  |
| 8 | 线程数核对 | `grep Threads /proc/$(pgrep webserver)/status` | 1 + io 线程 + worker 线程 |  |
| 9 | 无 fd 泄漏 | 压测后 `ss -tn state established \| wc -l` | 回到接近 0 |  |

## 三、压测对比（核心交付物）

压测命令（在宿主机执行，不要和服务器挤在同一台虚拟机里）：

```bash
wrk -t4 -c200 -d60s --latency http://<虚拟机IP>:8080/index.html
wrk -t4 -c200 -d60s -H "Connection: keep-alive" --latency http://<虚拟机IP>:8080/index.html
```

| 版本 | 线程配置 | 并发 | QPS | P99（毫秒） | CPU 占用 | 备注 |
| --- | --- | --- | --- | --- | --- | --- |
| 阶段 3 单线程 | `-t 0` | 200 |  |  |  | 基线 |
| 阶段 4 多 IO | `-t 4` | 200 |  |  |  | 只用 IO 池 |
| 阶段 4 加 worker | `-t 4 -w 4` | 200 |  |  |  | 静态文件 offload |

结论（一句话说明提升多少、瓶颈在哪里）：

## 四、稳定性与竞态检查

| 检查 | 命令 | 期望 | 实际 |
| --- | --- | --- | --- |
| 数据竞争 | `cmake -B build-tsan -DCMAKE_CXX_FLAGS="-fsanitize=thread" && ./build-tsan/webserver -t 4` 压测 | 无 TSan 报告 |  |
| 内存问题 | `cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"` 跑一轮 | 无泄漏与越界 |  |
| 长稳 | `-t 4` 连续运行 30 分钟并压测 | 无崩溃、内存平稳 |  |

## 五、结论与遗留问题

结论：

| 问题 | 影响 | 计划 |
| --- | --- | --- |
|  |  |  |

## 六、下一步

进入阶段 5：定时器与空闲连接超时；注意定时任务必须在连接所属的 IO 线程执行。

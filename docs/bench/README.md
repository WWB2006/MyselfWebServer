# docs/bench 数据目录

这里存放阶段 9 产生的原始数据。README 只引用文件名与结论，不粘贴大段输出。

| 文件模式 | 产生方式 | 说明 |
| --- | --- | --- |
| `bench-matrix-<时间戳>.csv` | `scripts/bench_matrix.sh` | 并发梯度压测结果：并发、QPS、P50、P99、请求数、错误 |
| `soak-<时间戳>.csv` | `scripts/soak_test.sh` | 长稳采样：运行秒数、RSS(MB)、在线连接、请求总数、错误总数、拒绝总数 |
| `flame-<时间戳>.svg` | `scripts/flamegraph.sh` | 火焰图，直接用浏览器打开 |
| `perf-<时间戳>.data` / `.script` | `scripts/flamegraph.sh` | perf 原始采样，默认不提交（见 .gitignore） |
| `fault-injection-<时间戳>.txt` | 手工重定向 `fault_injection.py` 的输出 | 故障注入用例结果 |

## 记录规范

1. **每条数据都要能对上环境**：CSV 头部注释里记录了核数、内存、内核版本与压测参数；
2. **一次只改一个变量**：对比不同版本时，压测参数、机器、客户端位置保持一致；
3. **数字要能复跑**：README 里给出命令与文件路径，而不是孤立的数字；
4. **客户端与服务端分离**：同机压测必须在备注中标注，否则参考价值有限。

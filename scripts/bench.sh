#!/usr/bin/env bash
# 单点压测并把结果落盘。更完整的并发梯度压测见 scripts/bench_matrix.sh（阶段 9 交付）。
set -euo pipefail
cd "$(dirname "$0")/.."

# 压测客户端建议跑在宿主机上，避免与服务器争抢 CPU
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8080}"
THREADS="${THREADS:-4}"
CONNS="${CONNS:-100}"
DURATION="${DURATION:-30s}"
PATH_UNDER_TEST="${PATH_UNDER_TEST:-/index.html}"
OUT="docs/bench/result-$(date +%Y%m%d-%H%M%S).txt"

mkdir -p docs/bench
{
  echo "date: $(date -Iseconds)"
  echo "target: http://${HOST}:${PORT}${PATH_UNDER_TEST}"
  echo "cmd: wrk -t${THREADS} -c${CONNS} -d${DURATION} --latency http://${HOST}:${PORT}${PATH_UNDER_TEST}"
  echo "cpu: $(nproc) cores, $(free -h | awk '/Mem:/{print $2}') memory"
  echo "---"
  wrk -t"${THREADS}" -c"${CONNS}" -d"${DURATION}" --latency \
      "http://${HOST}:${PORT}${PATH_UNDER_TEST}"
} | tee "$OUT"

echo "结果已写入 $OUT"

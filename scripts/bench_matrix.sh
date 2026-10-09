#!/usr/bin/env bash
# 按并发梯度压测并写入 CSV，用于回答"QPS 从哪个并发开始不再增长"。
#
# 用法：./scripts/bench_matrix.sh [端口] [并发列表] [每档时长]
# 例：  ./scripts/bench_matrix.sh 8080 "1 10 50 100 200 500" 30s
#
# 注意：压测客户端建议跑在宿主机，不要与服务端挤在同一台机器。
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-8080}"
CONCURRENCY_LIST="${2:-1 10 50 100 200 500}"
DURATION="${3:-30s}"
HOST="${HOST:-127.0.0.1}"
PATH_UNDER_TEST="${PATH_UNDER_TEST:-/index.html}"
THREADS="${THREADS:-4}"

if ! command -v wrk >/dev/null 2>&1; then
    echo "缺少 wrk，请先安装：sudo apt install -y wrk（或从源码编译）" >&2
    exit 1
fi

mkdir -p docs/bench
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="docs/bench/bench-matrix-${STAMP}.csv"

{
    echo "# 环境: $(nproc) cores, $(free -h | awk '/Mem:/{print $2}') memory, kernel $(uname -r)"
    echo "# 目标: http://${HOST}:${PORT}${PATH_UNDER_TEST}  时长: ${DURATION}  线程: ${THREADS}"
    echo "concurrency,qps,p50_ms,p99_ms,requests,errors,notes"
} > "$OUT"

for CONCURRENCY in $CONCURRENCY_LIST; do
    echo "==> 并发 ${CONCURRENCY}"
    RAW="$(mktemp)"
    wrk -t"${THREADS}" -c"${CONCURRENCY}" -d"${DURATION}" --latency \
        "http://${HOST}:${PORT}${PATH_UNDER_TEST}" | tee "$RAW"

    QPS="$(awk '/Requests\/sec/{print $2}' "$RAW" | head -1)"
    P50="$(awk '/50%/{print $2}' "$RAW" | head -1)"
    P99="$(awk '/99%/{print $2}' "$RAW" | head -1)"
    REQUESTS="$(awk '/[0-9]+ requests in/{print $1}' "$RAW" | head -1)"
    ERRORS="$(awk '/Socket errors/{print $0}' "$RAW" | head -1)"
    ERRORS="${ERRORS:-none}"

    echo "${CONCURRENCY},${QPS:-0},${P50:-0},${P99:-0},${REQUESTS:-0},\"${ERRORS}\"," >> "$OUT"
    rm -f "$RAW"
done

echo
echo "结果已写入 $OUT"
column -s, -t "$OUT" 2>/dev/null || cat "$OUT"

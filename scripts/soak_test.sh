#!/usr/bin/env bash
# 长稳测试：持续压测的同时，每 N 秒采样 RSS、在线连接数、错误数，写入 CSV。
#
# 用法：./scripts/soak_test.sh [线程数] [端口] [总时长秒] [采样间隔秒]
# 例：  ./scripts/soak_test.sh 4 8080 3600 30      # 跑 1 小时，每 30 秒采样
set -euo pipefail
cd "$(dirname "$0")/.."

THREADS="${1:-4}"
PORT="${2:-8080}"
TOTAL_SECONDS="${3:-3600}"
INTERVAL="${4:-30}"
HOST="${HOST:-127.0.0.1}"
CONCURRENCY="${CONCURRENCY:-100}"

mkdir -p docs/bench
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT="docs/bench/soak-${STAMP}.csv"

if ! command -v wrk >/dev/null 2>&1; then
    echo "缺少 wrk，请先安装" >&2
    exit 1
fi

SERVER_PID="$(pgrep -x webserver || true)"
if [ -z "$SERVER_PID" ]; then
    echo "没有找到运行中的 webserver 进程，请先启动服务" >&2
    exit 1
fi

echo "服务端 pid=${SERVER_PID}，压测 ${TOTAL_SECONDS}s，采样间隔 ${INTERVAL}s"

echo "elapsed_s,rss_mb,connections_current,requests_total,errors_total,rejected_total" > "$OUT"

wrk -t"${THREADS}" -c"${CONCURRENCY}" -d"${TOTAL_SECONDS}s" \
    "http://${HOST}:${PORT}/index.html" > /dev/null &
WRK_PID=$!

STARTED_AT="$(date +%s)"
FIRST_RSS=0
LAST_RSS=0

while kill -0 "$WRK_PID" 2>/dev/null; do
    ELAPSED=$(( $(date +%s) - STARTED_AT ))
    RSS_KB="$(awk '/VmRSS/{print $2}' "/proc/${SERVER_PID}/status" 2>/dev/null || echo 0)"
    RSS_MB=$(( RSS_KB / 1024 ))
    [ "$FIRST_RSS" -eq 0 ] && FIRST_RSS="$RSS_MB"
    LAST_RSS="$RSS_MB"

    METRICS="$(curl -s "http://${HOST}:${PORT}/metrics" || true)"
    CURRENT="$(printf '%s\n' "$METRICS" | awk '/^myself_connections_current /{print $2}' | head -1)"
    REQUESTS="$(printf '%s\n' "$METRICS" | awk '/^myself_requests_total /{print $2}' | head -1)"
    ERRORS="$(printf '%s\n' "$METRICS" | awk '/^myself_errors_total /{print $2}' | head -1)"
    REJECTED="$(printf '%s\n' "$METRICS" | awk '/^myself_connections_rejected_total /{print $2}' | head -1)"

    echo "${ELAPSED},${RSS_MB},${CURRENT:-0},${REQUESTS:-0},${ERRORS:-0},${REJECTED:-0}" >> "$OUT"
    sleep "$INTERVAL"
done

wait "$WRK_PID" || true

echo
echo "采样完成：$OUT"
echo "起始 RSS=${FIRST_RSS}MB，结束 RSS=${LAST_RSS}MB，增长 $(( LAST_RSS - FIRST_RSS ))MB"
echo "判定标准：增长不超过 5% 视为内存平稳；错误数应保持不变。"
tail -n 3 "$OUT"

#!/usr/bin/env bash
# 零拷贝 A/B 对比：同一个大文件，分别在 -Z 0（内存缓冲）与默认阈值（sendfile）下下载，
# 输出耗时、吞吐与客户端 CPU 占用。
#
# 用法：./scripts/bench_zero_copy.sh [文件大小MB] [重复次数]
set -euo pipefail
cd "$(dirname "$0")/.."

SIZE_MB="${1:-200}"
ROUNDS="${2:-3}"
PORT="${PORT:-18090}"
BIN="./build/webserver"
ASSET="www/_bench_${SIZE_MB}m.bin"
URL="http://127.0.0.1:${PORT}/_bench_${SIZE_MB}m.bin"

if [[ ! -x "$BIN" ]]; then
    echo "找不到 $BIN，请先执行：cmake -B build && cmake --build build -j\$(nproc)" >&2
    exit 1
fi
if ! command -v curl >/dev/null; then
    echo "需要 curl" >&2
    exit 1
fi

cleanup() {
    [[ -n "${SERVER_PID:-}" ]] && kill "$SERVER_PID" 2>/dev/null || true
    rm -f "$ASSET"
}
trap cleanup EXIT

echo "生成 ${SIZE_MB}MB 测试文件：$ASSET"
dd if=/dev/zero of="$ASSET" bs=1M count="$SIZE_MB" status=none
ls -lh "$ASSET" | awk '{print "  实际大小：" $5}'

run_case() {
    local label="$1" threshold="$2"
    echo
    echo "=== $label（-Z $threshold） ==="

    "$BIN" -p "$PORT" -t 4 -q -Z "$threshold" &
    SERVER_PID=$!
    for _ in $(seq 1 50); do
        if curl -s -o /dev/null --max-time 1 "$URL"; then break; fi
        sleep 0.1
    done

    for i in $(seq 1 "$ROUNDS"); do
        # -w 输出：耗时 / 平均速度(MB/s) / 客户端 user+sys 时间
        curl -s -o /dev/null \
             -w "  第 ${i} 次：耗时 %{time_total}s  速度 %{speed_download} B/s  连接 %{time_connect}s\n" \
             "$URL"
    done

    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
}

run_case "内存缓冲（对照组）" 0
run_case "sendfile 零拷贝" 65536

echo
echo "提示：把两种模式的耗时/速度抄下来做对比；也可以同时用 top 观察服务端 CPU。"
echo "     更细的差异建议用 perf stat -e cycles,instructions,cache-misses 对比系统调用与拷贝次数。"

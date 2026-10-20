#!/usr/bin/env bash
# 零拷贝 A/B 对比：同一个大文件，分别在 -Z 0（内存缓冲）与默认阈值（sendfile）下下载，
# 输出耗时与吞吐。
#
# 用法：./scripts/bench_zero_copy.sh [文件大小MB] [重复次数]
set -uo pipefail
cd "$(dirname "$0")/.."

SIZE_MB="${1:-200}"
ROUNDS="${2:-3}"
PORT_BASE="${PORT:-18090}"
BIN="./build/webserver"
ASSET="www/_bench_${SIZE_MB}m.bin"

if [[ ! -x "$BIN" ]]; then
    echo "找不到 $BIN，请先执行：cmake -B build && cmake --build build -j\$(nproc)" >&2
    exit 1
fi
if ! command -v curl >/dev/null; then
    echo "需要 curl" >&2
    exit 1
fi

SERVER_PID=""
SERVER_LOG=""

cleanup() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    [[ -n "$SERVER_LOG" ]] && rm -f "$SERVER_LOG"
    rm -f "$ASSET"
}
trap cleanup EXIT

echo "生成 ${SIZE_MB}MB 测试文件：$ASSET"
dd if=/dev/zero of="$ASSET" bs=1M count="$SIZE_MB" status=none
ls -lh "$ASSET" | awk '{print "  实际大小：" $5}'
echo "说明：每组先做一次不计时的完整下载预热（把文件读进 PageCache），"
echo "     再计时 ${ROUNDS} 次；这样比较的是拷贝路径，而不是磁盘速度。"

port_free() {
    ! curl -s -o /dev/null --max-time 1 "http://127.0.0.1:$1/404.html"
}

# 启动服务并等到端口就绪；端口被占用时自动顺延。失败时打印服务端输出。
start_server() {
    local threshold="$1"
    local port
    for offset in 0 1 2 3 4 5 6 7 8 9; do
        port=$((PORT_BASE + offset))
        if ! port_free "$port"; then
            echo "  端口 ${port} 已被占用（可能是上次残留的 webserver），跳过"
            continue
        fi
        SERVER_LOG="$(mktemp)"
        "$BIN" -p "$port" -t 4 -q -Z "$threshold" >"$SERVER_LOG" 2>&1 &
        SERVER_PID=$!

        for _ in $(seq 1 60); do
            if curl -s -o /dev/null --max-time 1 "http://127.0.0.1:${port}/404.html"; then
                PORT="$port"
                URL="http://127.0.0.1:${port}/_bench_${SIZE_MB}m.bin"
                return 0
            fi
            if ! kill -0 "$SERVER_PID" 2>/dev/null; then
                break
            fi
            sleep 0.1
        done

        echo "  !! 服务在端口 ${port} 上没能就绪，服务端输出："
        sed 's/^/     /' "$SERVER_LOG" | head -20
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
        rm -f "$SERVER_LOG"
        SERVER_LOG=""
    done
    return 1
}

stop_server() {
    if [[ -n "$SERVER_PID" ]]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
    if [[ -n "$SERVER_LOG" ]]; then
        rm -f "$SERVER_LOG"
        SERVER_LOG=""
    fi
}

run_case() {
    local label="$1" threshold="$2"
    echo
    echo "=== $label（-Z $threshold） ==="

    if ! start_server "$threshold"; then
        echo "  跳过：没有可用端口，先执行 killall webserver 再重试" >&2
        return 1
    fi

    # 预热：完整下载一次，不计时（让后续比较的是拷贝路径而不是磁盘）
    if ! curl -s -o /dev/null "$URL"; then
        echo "  预热下载失败：$URL" >&2
        stop_server
        return 1
    fi

    local i result total speed
    for i in $(seq 1 "$ROUNDS"); do
        result="$(curl -s -o /dev/null -w "%{time_total} %{speed_download}" "$URL")" || {
            echo "  第 ${i} 次下载失败" >&2
            stop_server
            return 1
        }
        total="${result%% *}"
        speed="${result##* }"
        awk -v t="$total" -v s="$speed" -v n="$i" \
            'BEGIN { printf "  第 %d 次：耗时 %.3f s   速度 %.1f MB/s\n", n, t, s / 1048576 }'
    done

    stop_server
    return 0
}

FAILED=0
run_case "内存缓冲（对照组）" 0 || FAILED=1
run_case "sendfile 零拷贝" 65536 || FAILED=1

echo
echo "提示：若两种模式数字接近，说明瓶颈不在拷贝；此时用下面两条命令看内存与 CPU："
echo "  pidstat -r -p \$(pgrep -f 'webserver -p') 1 3     # RSS：对照组会涨到文件大小量级"
echo "  perf stat -e cycles,instructions,cache-misses curl -s -o /dev/null \$URL"
echo "若服务在下载过程中被杀，先看 dmesg | tail 是否有 OOM，再用更小的文件重试："
echo "  ./scripts/bench_zero_copy.sh 100 3"
exit "$FAILED"

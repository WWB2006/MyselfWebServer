#!/usr/bin/env bash
# 用 perf 采样运行中的服务并生成火焰图。
#
# 用法：sudo ./scripts/flamegraph.sh <pid> [采样秒数]
# 例：  sudo ./scripts/flamegraph.sh $(pgrep webserver) 30
#
# 如果 perf 不可用（容器或缺少 CAP_PERFMON），脚本会给出替代方案。
set -euo pipefail
cd "$(dirname "$0")/.."

PID_ARG="${1:-}"
DURATION="${2:-30}"

if [ -z "$PID_ARG" ]; then
    echo "用法：sudo $0 <pid> [采样秒数]" >&2
    exit 1
fi

if ! command -v perf >/dev/null 2>&1; then
    cat >&2 <<'EOF'
没有找到 perf。可选方案：
  1) 安装：sudo apt install -y linux-tools-common linux-tools-generic
  2) 无权限采样时，改用轻量替代：每 0.5 秒采样 /proc/<pid>/stat 的 utime/stime，
     或用 valgrind --tool=callgrind 跑一个短用例（速度慢但不需要 perf 权限）。
EOF
    exit 1
fi

mkdir -p docs/bench
STAMP="$(date +%Y%m%d-%H%M%S)"
PERF_DATA="docs/bench/perf-${STAMP}.data"
FOLDED="docs/bench/perf-${STAMP}.folded"
SVG="docs/bench/flame-${STAMP}.svg"

echo "==> 采样 ${DURATION}s（pid=${PID_ARG}）"
if ! perf record -F 999 -g -p "$PID_ARG" -o "$PERF_DATA" -- sleep "$DURATION"; then
    echo "perf record 失败：可能是权限不足（需要 root 或 CAP_PERFMON），也可能是内核限制 perf_event_paranoid。" >&2
    echo "可尝试：sudo sysctl -w kernel.perf_event_paranoid=1" >&2
    exit 1
fi

perf script -i "$PERF_DATA" > "docs/bench/perf-${STAMP}.script"

FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-third_party/FlameGraph}"
if [ ! -x "${FLAMEGRAPH_DIR}/flamegraph.pl" ]; then
    echo "==> 未找到 FlameGraph 脚本，尝试克隆到 ${FLAMEGRAPH_DIR}"
    mkdir -p "$(dirname "$FLAMEGRAPH_DIR")"
    git clone --depth 1 https://github.com/brendangregg/FlameGraph.git "$FLAMEGRAPH_DIR"
fi

"${FLAMEGRAPH_DIR}/stackcollapse-perf.pl" "docs/bench/perf-${STAMP}.script" > "$FOLDED"
"${FLAMEGRAPH_DIR}/flamegraph.pl" "$FOLDED" > "$SVG"

echo
echo "火焰图已生成：$SVG"
echo "原始数据：$PERF_DATA（可用 perf report -i 打开）"

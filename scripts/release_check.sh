#!/usr/bin/env bash
# 发布门禁：构建、单元测试、接口测试、故障注入、冒烟压测与文档检查。
#
# 用法：./scripts/release_check.sh [端口]
# 退出码：0 全部通过；1 有步骤失败（输出会指出是哪一步）
#
# 说明：故障注入中的慢连接用例需要服务端启用 -i，本脚本用 -i 5 启动一个临时服务完成检查。
set -uo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-18080}"
HOST="${HOST:-127.0.0.1}"

PASS=0
FAIL=0
RESULTS=()

step() {
    local name="$1"
    shift
    echo
    echo "==> ${name}"
    if "$@"; then
        RESULTS+=("PASS  ${name}")
        PASS=$((PASS + 1))
    else
        RESULTS+=("FAIL  ${name}")
        FAIL=$((FAIL + 1))
    fi
}

build_project() {
    cmake -B build -DCMAKE_BUILD_TYPE=Release >/dev/null &&
        cmake --build build -j"$(nproc)"
}

run_unit_tests() {
    cmake -B build -DCMAKE_BUILD_TYPE=Release -DWEBSERVER_BUILD_TESTS=ON >/dev/null &&
        cmake --build build -j"$(nproc)" >/dev/null &&
        ctest --test-dir build --output-on-failure
}

run_api_tests() {
    if [ ! -x .venv/bin/pytest ]; then
        echo "未找到 .venv，先创建虚拟环境并安装依赖"
        python3 -m venv .venv || return 1
        # shellcheck disable=SC1091
        source .venv/bin/activate
        pip install -q -r tests/api/requirements.txt || return 1
    else
        # shellcheck disable=SC1091
        source .venv/bin/activate
    fi
    MYSELF_TEST_PORT="$PORT" MYSELF_WEBSERVER_BIN=./build/webserver pytest tests/api -q
}

run_fault_injection() {
    ./build/webserver -p "$PORT" -t 2 -i 5 -l warn -q &
    local server_pid=$!
    sleep 1
    python3 scripts/fault_injection.py --host "$HOST" --port "$PORT" --idle-timeout 5
    local result=$?
    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
    return $result
}

run_smoke_bench() {
    if ! command -v wrk >/dev/null 2>&1; then
        echo "未安装 wrk，跳过冒烟压测（安装后重跑本项）"
        return 0
    fi
    ./build/webserver -p "$PORT" -t 4 -l warn -q &
    local server_pid=$!
    sleep 1
    wrk -t2 -c50 -d10s --latency "http://${HOST}:${PORT}/index.html" | tail -n 8
    local result=${PIPESTATUS[0]}
    kill "$server_pid" 2>/dev/null
    wait "$server_pid" 2>/dev/null
    return $result
}

check_documents() {
    local missing=0
    for file in README.md CHANGELOG.md LICENSE docs/architecture.md \
                docs/demo-script.md docs/resume-and-interview.md \
                docs/release-checklist.md docs/adr/0009-open-source-compliance.md; do
        if [ ! -f "$file" ]; then
            echo "缺少文件：$file"
            missing=1
        fi
    done
    if grep -rn "your-name" README.md >/dev/null 2>&1; then
        echo "提醒：README 里仍有 your-name 占位，需要替换成真实仓库地址"
        missing=1
    fi
    return $missing
}

echo "发布门禁开始（端口 ${PORT}）"
step "构建（Release）" build_project
step "单元测试" run_unit_tests
step "接口测试" run_api_tests
step "故障注入" run_fault_injection
step "冒烟压测" run_smoke_bench
step "文档与许可证检查" check_documents

echo
echo "==================== 汇总 ===================="
for line in "${RESULTS[@]}"; do
    echo "$line"
done
echo "通过 ${PASS} 项，失败 ${FAIL} 项"

if [ "$FAIL" -eq 0 ]; then
    echo "结论：可以打 v1.0.0 标签并发布。"
    exit 0
fi
echo "结论：先修复失败项，再执行发布检查清单。"
exit 1

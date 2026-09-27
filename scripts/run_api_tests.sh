#!/usr/bin/env bash
# 构建服务并运行接口测试（夹具会自动启动与关闭服务进程）
set -euo pipefail
cd "$(dirname "$0")/.."

export MYSELF_TEST_PORT="${MYSELF_TEST_PORT:-18080}"
export MYSELF_WEBSERVER_BIN="${MYSELF_WEBSERVER_BIN:-./build/webserver}"

python3 -m venv .venv
# shellcheck disable=SC1091
source .venv/bin/activate
pip install -q -r tests/api/requirements.txt

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

pytest tests/api -v

#!/usr/bin/env bash
# 以前台方式启动服务（Ctrl+C 退出）。可用环境变量覆盖端口：
#   PORT=9090 ./scripts/run.sh
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${PORT:-8080}"
exec ./build/webserver -p "$PORT"

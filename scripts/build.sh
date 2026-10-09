#!/usr/bin/env bash
# 一键构建。产物：build/webserver
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}"
cmake --build build -j"$(nproc)"
echo "构建完成：build/webserver"

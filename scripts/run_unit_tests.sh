#!/usr/bin/env bash
# 构建并运行单元测试。
# 优先使用系统 GoogleTest（sudo apt install libgtest-dev），缺失时自动联网下载。
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -B build -DCMAKE_BUILD_TYPE=Debug -DWEBSERVER_BUILD_TESTS=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

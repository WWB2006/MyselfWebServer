#!/usr/bin/env bash
# 带消毒器构建：./scripts/sanitize.sh [asan|tsan]
# 产物在 build-<mode>/webserver，构建后手动运行即可（例如 ASan 下跑一遍压测与接口测试）。
set -euo pipefail
cd "$(dirname "$0")/.."

MODE="${1:-asan}"
case "$MODE" in
  asan) FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" ;;
  tsan) FLAGS="-fsanitize=thread" ;;
  *) echo "用法：$0 [asan|tsan]" >&2; exit 1 ;;
esac

cmake -B "build-$MODE" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="$FLAGS"
cmake --build "build-$MODE" -j"$(nproc)"
echo "带 ${MODE} 的构建完成：build-$MODE/webserver"

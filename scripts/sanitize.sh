#!/usr/bin/env bash
# ASan / TSan 构建：./scripts/sanitize.sh asan | tsan
set -euo pipefail

MODE="${1:-asan}"
cd "$(dirname "$0")/.."

case "$MODE" in
  asan)
    FLAGS="-fsanitize=address -fno-omit-frame-pointer"
    BUILD_DIR="build-asan"
    ;;
  tsan)
    FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
    BUILD_DIR="build-tsan"
    ;;
  *)
    echo "usage: $0 asan|tsan" >&2
    exit 1
    ;;
esac

cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="$FLAGS" \
  -DCMAKE_EXE_LINKER_FLAGS="$FLAGS"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "构建完成：$BUILD_DIR/webserver"
echo "运行：$BUILD_DIR/webserver -p 8080 -t 4 -i 5"

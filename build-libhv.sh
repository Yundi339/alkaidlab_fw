#!/bin/bash
# ============================================================================
# 构建 libhv 静态库
# 用法: bash build-libhv.sh [--openssl-root <dir>]
#
# 环境变量:
#   WITH_IO_URING=1    启用 io_uring 事件循环后端（需 Linux 5.1+ 且安装 liburing-devel）
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBHV_DIR="$SCRIPT_DIR/third_party/libhv"
BUILD_DIR="$SCRIPT_DIR/build_cache/libhv_build"
INSTALL_DIR="$SCRIPT_DIR/build_cache/libhv_install"

OPENSSL_ROOT=""

# 解析参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --openssl-root) OPENSSL_ROOT="$2"; shift 2 ;;
        *)              shift ;;
    esac
done

if [ ! -f "$LIBHV_DIR/CMakeLists.txt" ]; then
    echo "Error: libhv source not found at $LIBHV_DIR"
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

# 增量：已有产物则跳过
if [[ -f "$INSTALL_DIR/lib/libhv_static.a" || -f "$INSTALL_DIR/lib/libhv.a" ]]; then
    echo "libhv 已有缓存，跳过 ($INSTALL_DIR)"
    exit 0
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED=OFF
    -DBUILD_STATIC=ON
    -DWITH_OPENSSL=ON
    -DWITH_HTTP=ON
    -DWITH_HTTP_SERVER=ON
    -DWITH_HTTP_CLIENT=ON
    -DWITH_EVPP=ON
    -DBUILD_EXAMPLES=OFF
    -DBUILD_UNITTEST=OFF
)

if [ -n "$OPENSSL_ROOT" ]; then
    CMAKE_ARGS+=(-DOPENSSL_ROOT_DIR="$OPENSSL_ROOT")
fi

# io_uring 事件循环后端（Linux 5.1+, 需要 liburing-devel）
if [ "${WITH_IO_URING:-0}" = "1" ]; then
    if ! find /usr/include /usr/local/include -name "liburing.h" -print -quit 2>/dev/null | grep -q .; then
        echo "Error: liburing-devel not found. Install it first:"
        echo "  RHEL/CentOS: dnf install liburing-devel"
        echo "  Debian/Ubuntu: apt install liburing-dev"
        exit 1
    fi
    CMAKE_ARGS+=(-DWITH_IO_URING=ON)
    echo "io_uring backend: ENABLED"
else
    echo "io_uring backend: disabled (set WITH_IO_URING=1 to enable)"
fi

echo "=== Building libhv ==="
cmake -B "$BUILD_DIR" -S "$LIBHV_DIR" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"
cmake --install "$BUILD_DIR"

echo "libhv done:"
ls -lh "$INSTALL_DIR/lib/"*hv* 2>/dev/null

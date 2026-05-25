#!/bin/bash
# ============================================================================
# alkaidlab_fw — 自包含构建脚本
# ============================================================================
# 用法：
#   ./build.sh                                    # 独立构建（自动 clone vcpkg）
#   ./build.sh --vcpkg-root /path/to/vcpkg        # 复用已有 vcpkg
#   ./build.sh --vcpkg-installed-dir /path/to/dir   # 指定 vcpkg_installed 目录
#   ./build.sh --install-dir /path/to/out         # 指定安装目录
#   ./build.sh --triplet x64-mingw-dynamic        # 指定 vcpkg triplet（默认自动检测）
#   ./build.sh --test                             # 构建 + 测试
#   ./build.sh --clean                            # 清空重建
#
# 内部流程（严格顺序，每步依赖前一步完成）：
#   [1/4] vcpkg 准备 + 安装依赖（Boost/OpenSSL/spdlog/json）
#   [2/4] 构建 libhv（依赖 OpenSSL ← 步骤 1）
#   [3/4] 构建 fw 库（依赖 libhv ← 步骤 2 + Boost ← 步骤 1）
#   [4/4] 安装到 --install-dir
#
# 环境变量：
#   GTEST_PREFIX — GTest cmake config 路径（--test 时需要）
# ============================================================================
set -e

FW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$FW_DIR/build"
LIBHV_INSTALL="$FW_DIR/build_cache/libhv_install"

# 默认值
INSTALL_DIR=""
VCPKG_ROOT_OVERRIDE=""
VCPKG_INSTALLED_OVERRIDE=""
VCPKG_TRIPLET=""
RUN_TESTS=false
DO_CLEAN=false

# 解析参数
while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-dir)          INSTALL_DIR="$2"; shift 2 ;;
        --vcpkg-root)           VCPKG_ROOT_OVERRIDE="$2"; shift 2 ;;
        --vcpkg-installed-dir)  VCPKG_INSTALLED_OVERRIDE="$2"; shift 2 ;;
        --triplet)              VCPKG_TRIPLET="$2"; shift 2 ;;
        --test)                 RUN_TESTS=true; shift ;;
        --clean)                DO_CLEAN=true; shift ;;
        *)                      echo "未知选项: $1"; exit 1 ;;
    esac
done

# 默认安装目录
if [[ -z "$INSTALL_DIR" ]]; then
    PROJECT_ROOT="$(cd "$FW_DIR/../.." && pwd)"
    INSTALL_DIR="$PROJECT_ROOT/build_cache/alkaidlab_fw_install"
fi

echo "================================================"
echo "  alkaidlab_fw 构建"
echo "================================================"

# ── [1/4] vcpkg 发现或 bootstrap ──
if [[ -n "$VCPKG_ROOT_OVERRIDE" ]]; then
    VCPKG_DIR="$VCPKG_ROOT_OVERRIDE"
    if [[ ! -f "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" ]]; then
        echo "错误：--vcpkg-root 路径无效: $VCPKG_DIR"
        exit 1
    fi
    echo "[1/4] 复用 vcpkg: $VCPKG_DIR"
else
    VCPKG_DIR="$FW_DIR/vcpkg"
    if [[ ! -f "$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake" ]]; then
        echo "[1/4] vcpkg 未找到，clone 到 $VCPKG_DIR..."
        git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
        "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
    fi
fi
VCPKG_TOOLCHAIN="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake"

# vcpkg_installed 目录
if [[ -n "$VCPKG_INSTALLED_OVERRIDE" ]]; then
    VCPKG_INSTALLED="$VCPKG_INSTALLED_OVERRIDE"
else
    VCPKG_INSTALLED="$BUILD_DIR/vcpkg_installed"
fi

# vcpkg triplet（默认自动检测）
if [[ -z "$VCPKG_TRIPLET" ]]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) VCPKG_TRIPLET="x64-mingw-dynamic" ;;
        *)                    VCPKG_TRIPLET="x64-linux" ;;
    esac
fi
OPENSSL_ROOT="$VCPKG_INSTALLED/$VCPKG_TRIPLET"

if [[ ! -f "$OPENSSL_ROOT/lib/libssl.a" ]]; then
    echo "  安装 vcpkg 依赖..."
    "$VCPKG_DIR/vcpkg" install \
        --triplet="$VCPKG_TRIPLET" \
        --x-install-root="$VCPKG_INSTALLED" \
        --x-manifest-root="$FW_DIR"
fi

if [[ ! -f "$OPENSSL_ROOT/lib/libssl.a" ]]; then
    echo "错误：OpenSSL 安装失败，检查 vcpkg 日志"
    exit 1
fi
echo "  vcpkg 依赖就绪"

# ── [2/4] 构建 libhv ──
if [[ ! -f "$LIBHV_INSTALL/lib/libhv_static.a" && \
      ! -f "$LIBHV_INSTALL/lib/libhv.a" ]]; then
    echo "[2/4] 构建 libhv..."
    bash "$FW_DIR/build-libhv.sh" --openssl-root "$OPENSSL_ROOT"
else
    echo "[2/4] libhv 已有缓存，跳过"
fi

# 验证 libhv
LIBHV_LIB=""
for candidate in "$LIBHV_INSTALL/lib/libhv_static.a" \
                 "$LIBHV_INSTALL/lib/libhv.a"; do
    [[ -f "$candidate" ]] && LIBHV_LIB="$candidate" && break
done
if [[ -z "$LIBHV_LIB" ]]; then
    echo "错误：libhv 构建失败"
    exit 1
fi
echo "  libhv 就绪: $LIBHV_LIB"

# ── [3/4] 构建 fw ──
echo "[3/4] 构建 fw 库..."
if $DO_CLEAN && [[ -d "$BUILD_DIR" ]]; then
    rm -rf "$BUILD_DIR"
fi

CMAKE_ARGS=(
    -B "$BUILD_DIR" -S "$FW_DIR"
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_TOOLCHAIN"
    -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET"
    -DVCPKG_INSTALLED_DIR="$VCPKG_INSTALLED"
    -DVCPKG_INSTALL_OPTIONS="--no-print-usage"
    --log-level=NOTICE
    -DLIBHV_INCLUDE_DIR="$LIBHV_INSTALL/include"
    -DLIBHV_LIB="$LIBHV_LIB"
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
)

# 测试模式：提供 GTest
if $RUN_TESTS; then
    echo "  测试: 启用"
    if [[ -n "$GTEST_PREFIX" ]]; then
        CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$GTEST_PREFIX")
    fi
fi

# 增量构建：CMakeCache 存在且 install prefix 匹配时跳过 configure
NEED_CONFIGURE=true
if [[ -f "$BUILD_DIR/CMakeCache.txt" ]] && ! $DO_CLEAN; then
    CACHED_PREFIX=$(grep 'CMAKE_INSTALL_PREFIX:PATH=' "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d= -f2)
    if [[ "$CACHED_PREFIX" = "$INSTALL_DIR" ]]; then
        NEED_CONFIGURE=false
    fi
fi

if $NEED_CONFIGURE; then
    cmake "${CMAKE_ARGS[@]}" 2>&1
fi

NPROC=$(nproc 2>/dev/null || echo 4)
cmake --build "$BUILD_DIR" -j"$NPROC" 2>&1

# ── [4/4] 安装 / 测试 ──
if $RUN_TESTS; then
    echo "[4/4] 运行测试..."
    cd "$BUILD_DIR" && ctest --output-on-failure 2>&1
else
    echo "[4/4] 安装到 $INSTALL_DIR..."
    cmake --install "$BUILD_DIR" 2>&1
fi

echo ""
echo "安装完成: $INSTALL_DIR"

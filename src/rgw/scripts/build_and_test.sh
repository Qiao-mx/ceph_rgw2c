#!/bin/bash
#==============================================================================
# SAL C 项目构建和测试脚本
# 用于在 WSL/Linux 环境中执行
#==============================================================================

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示分隔线
separator() {
    echo "============================================================"
}

#==============================================================================
# 第一部分: 检查环境
#==============================================================================
check_environment() {
    separator
    log_info "检查构建环境..."
    
    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake 未安装"
        exit 1
    fi
    log_success "CMake $(cmake --version | head -n1)"
    
    # 检查 GCC
    if ! command -v gcc &> /dev/null; then
        log_error "GCC 未安装"
        exit 1
    fi
    log_success "GCC $(gcc --version | head -n1)"
    
    # 检查 Make
    if ! command -v make &> /dev/null; then
        log_error "Make 未安装"
        exit 1
    fi
    log_success "Make $(make --version | head -n1)"
    
    # 检查 librados
    if ldconfig -p | grep -q librados; then
        log_success "librados 已安装"
        ldconfig -p | grep librados | head -n1
    else
        log_warning "librados 未检测到，系统测试可能失败"
    fi
    
    separator
}

#==============================================================================
# 第二部分: 构建 c_common 库
#==============================================================================
build_c_common() {
    separator
    log_info "构建 c_common 库..."
    
    # 确定项目路径
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    C_COMMON_DIR="$PROJECT_ROOT/src/rgw/c_common"
    
    log_info "c_common 目录: $C_COMMON_DIR"
    
    # 创建构建目录
    BUILD_DIR="$C_COMMON_DIR/build"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 配置 CMake
    log_info "配置 CMake (Debug + ASAN)..."
    cmake "$C_COMMON_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DWITH_ASAN=ON \
        -DWITH_VALGRIND=OFF \
        -DWITH_UBSAN=OFF
    
    # 编译
    log_info "编译 c_common..."
    make -j$(nproc)
    
    # 检查库文件
    if [ -f "librgw_c_common.a" ] || [ -f "librgw_c_common.so" ]; then
        log_success "c_common 库构建成功"
    else
        log_error "c_common 库构建失败"
        exit 1
    fi
    
    separator
}

#==============================================================================
# 第三部分: 构建 sal_c 测试
#==============================================================================
build_sal_c() {
    separator
    log_info "构建 sal_c 测试..."
    
    # 确定项目路径
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    SAL_C_DIR="$PROJECT_ROOT/src/rgw/sal_c"
    
    log_info "sal_c 目录: $SAL_C_DIR"
    
    # 设置 librados 路径
    if ldconfig -p | grep -q librados; then
        LIBRADOS_PATH=$(ldconfig -p | grep librados.so | head -n1 | awk '{print $NF}')
        LIBRADOS_DIR=$(dirname "$LIBRADOS_PATH")
        export LD_LIBRARY_PATH="$LIBRADOS_DIR:$LD_LIBRARY_PATH"
        log_info "librados 路径: $LIBRADOS_PATH"
    fi
    
    # 创建构建目录
    BUILD_DIR="$SAL_C_DIR/build"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # 配置 CMake
    log_info "配置 CMake (Debug + ASAN)..."
    cmake "$SAL_C_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DWITH_ASAN=ON
    
    # 编译
    log_info "编译 sal_c..."
    make -j$(nproc)
    
    separator
}

#==============================================================================
# 第四部分: 运行 c_common 测试
#==============================================================================
test_c_common() {
    separator
    log_info "运行 c_common 单元测试..."
    
    # 确定项目路径
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    C_COMMON_BUILD="$PROJECT_ROOT/src/rgw/c_common/build"
    
    cd "$C_COMMON_BUILD"
    
    # 运行所有测试
    log_info "执行 ctest..."
    ctest --output-on-failure -V
    
    # 如果有失败的测试
    if ctest --output-on-failure | grep -q "Failed"; then
        log_warning "部分测试失败，请检查上述输出"
    else
        log_success "所有 c_common 测试通过"
    fi
    
    separator
}

#==============================================================================
# 第五部分: 运行 sal_c 测试
#==============================================================================
test_sal_c() {
    separator
    log_info "运行 sal_c 测试..."
    
    # 确定项目路径
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    SAL_C_BUILD="$PROJECT_ROOT/src/rgw/sal_c/build"
    
    cd "$SAL_C_BUILD"
    
    # 检查是否有可执行的测试文件
    cd tests
    
    # 运行基础测试
    if [ -x "./test_basic" ]; then
        log_info "运行 test_basic..."
        ./test_basic && log_success "test_basic 通过" || log_warning "test_basic 失败"
    fi
    
    # 运行 RADOS 驱动测试（如果有 librados）
    if ldconfig -p | grep -q librados; then
        if [ -x "./test_rados_driver" ]; then
            log_info "运行 test_rados_driver..."
            ./test_rados_driver && log_success "test_rados_driver 通过" || log_warning "test_rados_driver 失败"
        fi
        
        if [ -x "./test_integration" ]; then
            log_info "运行 test_integration..."
            ./test_integration && log_success "test_integration 通过" || log_warning "test_integration 失败"
        fi
    else
        log_warning "librados 不可用，跳过驱动测试"
    fi
    
    # 运行 ctest
    cd "$SAL_C_BUILD"
    log_info "执行 ctest..."
    ctest --output-on-failure -V || log_warning "部分 sal_c 测试失败"
    
    separator
}

#==============================================================================
# 第六部分: 性能测试
#==============================================================================
run_performance_tests() {
    separator
    log_info "运行性能测试..."
    
    # 确定项目路径
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
    C_COMMON_BUILD="$PROJECT_ROOT/src/rgw/c_common/build"
    
    cd "$C_COMMON_BUILD"
    
    if [ -x "./test_benchmark" ]; then
        log_info "执行性能基准测试..."
        ./test_benchmark
        log_success "性能测试完成"
    else
        log_warning "性能测试可执行文件不存在"
    fi
    
    separator
}

#==============================================================================
# 主函数
#==============================================================================
main() {
    log_info "SAL C 构建和测试脚本"
    log_info "项目根目录: $(pwd)"
    
    # 检查是否指定了命令
    if [ $# -eq 0 ]; then
        # 默认执行全部步骤
        COMMAND="all"
    else
        COMMAND="$1"
    fi
    
    case "$COMMAND" in
        check)
            check_environment
            ;;
        build-c-common)
            check_environment
            build_c_common
            ;;
        build-sal-c)
            check_environment
            build_sal_c
            ;;
        build)
            check_environment
            build_c_common
            build_sal_c
            ;;
        test-c-common)
            test_c_common
            ;;
        test-sal-c)
            test_sal_c
            ;;
        test)
            test_c_common
            test_sal_c
            ;;
        bench)
            run_performance_tests
            ;;
        all)
            check_environment
            build_c_common
            test_c_common
            build_sal_c
            test_sal_c
            run_performance_tests
            ;;
        clean)
            log_info "清理构建目录..."
            rm -rf "$SCRIPT_DIR/src/rgw/c_common/build"/*
            rm -rf "$SCRIPT_DIR/src/rgw/sal_c/build"/*
            log_success "清理完成"
            ;;
        *)
            echo "用法: $0 [命令]"
            echo ""
            echo "可用命令:"
            echo "  check         - 检查环境"
            echo "  build-c-common - 仅构建 c_common"
            echo "  build-sal-c   - 仅构建 sal_c"
            echo "  build         - 构建全部"
            echo "  test-c-common - 仅运行 c_common 测试"
            echo "  test-sal-c    - 仅运行 sal_c 测试"
            echo "  test          - 运行所有测试"
            echo "  bench         - 运行性能测试"
            echo "  all           - 完整构建和测试 (默认)"
            echo "  clean         - 清理构建目录"
            ;;
    esac
}

# 执行主函数
main "$@"

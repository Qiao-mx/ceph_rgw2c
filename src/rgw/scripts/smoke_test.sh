#!/bin/bash
#==============================================================================
# 快速冒烟测试脚本
# 在 WSL 中执行，用于快速验证构建是否成功
#==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }

separator() { echo "=============================================="; }

# 项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo ""
log_info "SAL C 快速冒烟测试"
log_info "项目目录: $PROJECT_ROOT"
echo ""

#==============================================================================
# 第一部分: 检查可执行文件
#==============================================================================
separator
log_info "检查测试可执行文件..."

C_COMMON_BUILD="$PROJECT_ROOT/src/rgw/c_common/build"
SAL_C_BUILD="$PROJECT_ROOT/src/rgw/sal_c/build/tests"

PASSED=0
FAILED=0

# 检查 c_common 测试
log_info "检查 c_common 测试..."
cd "$C_COMMON_BUILD"

if [ -x "./test_carray" ]; then
    log_success "test_carray 存在"
    ((PASSED++))
else
    log_error "test_carray 不存在或不可执行"
    ((FAILED++))
fi

if [ -x "./test_cstring" ]; then
    log_success "test_cstring 存在"
    ((PASSED++))
else
    log_error "test_cstring 不存在或不可执行"
    ((FAILED++))
fi

if [ -x "./test_cmap" ]; then
    log_success "test_cmap 存在"
    ((PASSED++))
else
    log_error "test_cmap 不存在或不可执行"
    ((FAILED++))
fi

if [ -x "./test_benchmark" ]; then
    log_success "test_benchmark 存在"
    ((PASSED++))
else
    log_warning "test_benchmark 不存在"
fi

# 检查 sal_c 测试
log_info "检查 sal_c 测试..."
cd "$SAL_C_BUILD"

if [ -x "./test_basic" ]; then
    log_success "test_basic 存在"
    ((PASSED++))
else
    log_error "test_basic 不存在或不可执行"
    ((FAILED++))
fi

# 检查 librados
if ldconfig -p | grep -q librados; then
    log_success "librados 可用"

    if [ -x "./test_rados_driver" ]; then
        log_success "test_rados_driver 存在"
        ((PASSED++))
    else
        log_error "test_rados_driver 不存在"
        ((FAILED++))
    fi
else
    log_warning "librados 不可用，跳过驱动测试"
fi

#==============================================================================
# 第二部分: 执行冒烟测试
#==============================================================================
separator
log_info "执行冒烟测试..."

cd "$C_COMMON_BUILD"

log_info "运行 test_carray..."
if ./test_carray > /dev/null 2>&1; then
    log_success "test_carray 通过"
else
    log_error "test_carray 失败"
fi

log_info "运行 test_cstring..."
if ./test_cstring > /dev/null 2>&1; then
    log_success "test_cstring 通过"
else
    log_error "test_cstring 失败"
fi

log_info "运行 test_cmap..."
if ./test_cmap > /dev/null 2>&1; then
    log_success "test_cmap 通过"
else
    log_error "test_cmap 失败"
fi

log_info "运行 test_cset..."
if ./test_cset > /dev/null 2>&1; then
    log_success "test_cset 通过"
else
    log_error "test_cset 失败"
fi

log_info "运行 test_cdeque..."
if ./test_cdeque > /dev/null 2>&1; then
    log_success "test_cdeque 通过"
else
    log_error "test_cdeque 失败"
fi

log_info "运行 test_cstack..."
if ./test_cstack > /dev/null 2>&1; then
    log_success "test_cstack 通过"
else
    log_error "test_cstack 失败"
fi

log_info "运行 test_cqueue..."
if ./test_cqueue > /dev/null 2>&1; then
    log_success "test_cqueue 通过"
else
    log_error "test_cqueue 失败"
fi

log_info "运行 test_coptional..."
if ./test_coptional > /dev/null 2>&1; then
    log_success "test_coptional 通过"
else
    log_error "test_coptional 失败"
fi

# sal_c 测试
cd "$SAL_C_BUILD"

log_info "运行 test_basic..."
if ./test_basic > /dev/null 2>&1; then
    log_success "test_basic 通过"
else
    log_error "test_basic 失败"
fi

# 如果 librados 可用，运行驱动测试
if ldconfig -p | grep -q librados; then
    log_info "运行 test_rados_driver..."
    if ./test_rados_driver > /dev/null 2>&1; then
        log_success "test_rados_driver 通过"
    else
        log_error "test_rados_driver 失败"
    fi
fi

#==============================================================================
# 第三部分: 总结
#==============================================================================
separator
echo ""
log_info "冒烟测试完成"
echo ""
log_info "下一步操作:"
echo "  1. 查看详细测试: cd $C_COMMON_BUILD && ctest -V"
echo "  2. 运行性能测试: cd $C_COMMON_BUILD && ./test_benchmark"
echo "  3. 查看构建日志: 检查编译错误"
echo ""

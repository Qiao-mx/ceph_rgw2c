#!/bin/bash
#==============================================================================
# SAL C 项目 - 一键安装和构建脚本
# 在 WSL Ubuntu 22.04 中运行
#==============================================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_step() { echo -e "${CYAN}[STEP]${NC} $1"; }

separator() { echo "============================================================"; }

# 项目路径
PROJECT_ROOT="/mnt/c/Users/10070/Desktop/ceph_rgw2c"
CCOMMON_DIR="$PROJECT_ROOT/src/rgw/c_common"
SALC_DIR="$PROJECT_ROOT/src/rgw/sal_c"

#==============================================================================
# 第一部分: 检查系统
#==============================================================================
log_step "检查系统环境..."
echo ""

# 检查是否为 root
if [ "$EUID" -eq 0 ]; then
    log_warning "以 root 运行，部分命令不需要 sudo"
else
    log_info "将以 sudo 权限安装"
fi

# 检查系统
log_info "系统信息:"
uname -a
echo ""
cat /etc/os-release | head -3
echo ""

#==============================================================================
# 第二部分: 更新系统并安装依赖
#==============================================================================
separator
log_step "更新系统并安装依赖..."
echo ""

log_info "更新 apt..."
sudo apt update -qq

log_info "安装基础工具..."
sudo apt install -y build-essential cmake git libtool pkg-config

log_info "安装 Ceph 依赖..."
sudo apt install -y \
    libboost-dev \
    libboost-system-dev \
    libboost-random-dev \
    libboost-thread-dev \
    libnuma-dev \
    liboath-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libxml2-dev \
    python3-dev \
    python3-pip \
    libsqlite3-dev

log_info "安装调试工具..."
sudo apt install -y gdb valgrind

log_success "依赖安装完成"

#==============================================================================
# 第三部分: 检查 librados
#==============================================================================
separator
log_step "检查 librados..."
echo ""

if ldconfig -p | grep -q librados; then
    log_success "librados 已安装"
    ldconfig -p | grep librados | head -3
else
    log_warning "librados 未安装"
    log_info "安装 librados-dev..."
    sudo apt install -y librados-dev || true
    
    if ldconfig -p | grep -q librados; then
        log_success "librados 安装成功"
    else
        log_warning "librados 需要从源码编译"
    fi
fi

#==============================================================================
# 第四部分: 创建 python 符号链接
#==============================================================================
separator
log_step "配置 Python..."
echo ""

if [ ! -f /usr/bin/python ]; then
    sudo ln -sf /usr/bin/python3 /usr/bin/python
    log_success "创建 python -> python3 符号链接"
else
    log_info "python 符号链接已存在"
fi

python --version

#==============================================================================
# 第五部分: 构建 c_common
#==============================================================================
separator
log_step "构建 c_common 库..."
echo ""

mkdir -p "$CCOMMON_DIR/build"
cd "$CCOMMON_DIR/build"

log_info "配置 CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_ASAN=ON \
    -DWITH_VALGRIND=OFF

log_info "编译..."
make -j$(nproc)

log_success "c_common 构建完成"

#==============================================================================
# 第六部分: 构建 sal_c
#==============================================================================
separator
log_step "构建 sal_c 测试..."
echo ""

mkdir -p "$SALC_DIR/build"
cd "$SALC_DIR/build"

log_info "配置 CMake..."
cmake "$SALC_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_ASAN=ON

log_info "编译..."
make -j$(nproc)

log_success "sal_c 构建完成"

#==============================================================================
# 第七部分: 运行测试
#==============================================================================
separator
log_step "运行测试..."
echo ""

# c_common 测试
cd "$CCOMMON_DIR/build"
log_info "运行 c_common 测试..."
ctest --output-on-failure -V || true

# sal_c 测试
cd "$SALC_DIR/build/tests"
log_info "运行 sal_c 测试..."
ctest --output-on-failure -V || true

#==============================================================================
# 完成
#==============================================================================
separator
log_success "全部完成!"
separator

echo ""
log_info "测试执行路径:"
echo "  c_common: cd $CCOMMON_DIR/build"
echo "  sal_c:    cd $SALC_DIR/build"
echo ""
log_info "运行单个测试:"
echo "  $CCOMMON_DIR/build/test_carray"
echo "  $SALC_DIR/build/tests/test_basic"
echo ""

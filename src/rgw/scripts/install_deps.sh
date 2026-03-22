#!/bin/bash
#==============================================================================
# Ubuntu 22.04 依赖安装脚本
# 在 WSL Ubuntu 中运行
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

separator() { echo "============================================================"; }

echo ""
log_info "Ubuntu 22.04 依赖安装脚本"
log_info "SAL C 项目环境配置"
echo ""

#==============================================================================
# 检查是否为 root 用户
#==============================================================================
if [ "$EUID" -eq 0 ]; then
    log_warning "以 root 用户运行，部分命令不需要 sudo"
else
    log_info "将以 sudo 权限安装依赖"
fi

#==============================================================================
# 第一部分: 更新系统
#==============================================================================
separator
log_info "更新系统包..."
sudo apt update -qq
sudo apt upgrade -y
log_success "系统更新完成"

#==============================================================================
# 第二部分: 安装基础编译工具
#==============================================================================
separator
log_info "安装基础编译工具..."

sudo apt install -y \
    build-essential \
    cmake \
    git \
    libtool \
    pkg-config \
    autoconf \
    automake \
    libtool-bin \
    uuid-dev

log_success "基础编译工具安装完成"

#==============================================================================
# 第三部分: 安装 Ceph 编译依赖
#==============================================================================
separator
log_info "安装 Ceph 编译依赖..."

sudo apt install -y \
    libboost-dev \
    libboost-system-dev \
    libboost-random-dev \
    libboost-thread-dev \
    libboost-date-time-dev \
    libboost-iostreams-dev \
    libnuma-dev \
    liboath-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libxml2-dev \
    libbabeltrace-dev \
    libblkid-dev \
    libudev-dev \
    python3-dev \
    python3-pip \
    python3-setuptools \
    libsqlite3-dev \
    libfuse-dev

log_success "Ceph 编译依赖安装完成"

#==============================================================================
# 第四部分: 安装其他必要工具
#==============================================================================
separator
log_info "安装其他必要工具..."

sudo apt install -y \
    curl \
    wget \
    unzip \
    htop \
    net-tools \
    iputils-ping \
    dnsutils \
    ca-certificates

log_success "其他工具安装完成"

#==============================================================================
# 第五部分: 安装调试工具
#==============================================================================
separator
log_info "安装调试工具..."

sudo apt install -y \
    gdb \
    gdbserver \
    valgrind \
    linux-tools-common \
    linux-tools-generic

# 启用 perf (如果可用)
if dpkg -l | grep -q linux-tools; then
    log_success "perf 工具安装完成"
else
    log_warning "perf 工具未安装，可能需要手动配置"
fi

#==============================================================================
# 第六部分: 安装 librados
#==============================================================================
separator
log_info "安装 librados..."

# 检查是否已安装
if ldconfig -p | grep -q librados; then
    log_success "librados 已安装"
    ldconfig -p | grep librados
else
    log_warning "系统 librados 未找到"
    log_info "尝试安装 librados-dev..."
    sudo apt install -y librados-dev || true

    # 检查 again
    if ldconfig -p | grep -q librados; then
        log_success "librados 安装成功"
    else
        log_warning "需要从源码编译 librados"
    fi
fi

#==============================================================================
# 第七部分: 配置 Python
#==============================================================================
separator
log_info "配置 Python..."

# 创建 python 符号链接
if [ ! -f /usr/bin/python ]; then
    sudo ln -sf /usr/bin/python3 /usr/bin/python
    log_success "创建 python -> python3 符号链接"
fi

# 安装 Python 包
pip3 install --upgrade pip
pip3 install boto3 awscli pytest || true

log_success "Python 配置完成"

#==============================================================================
# 第八部分: 配置 git
#==============================================================================
separator
log_info "配置 git..."

# 设置 git 用户信息 (如果未设置)
if [ -z "$(git config --global user.name)" ]; then
    read -p "请输入 git 用户名: " git_user
    git config --global user.name "$git_user"
fi

if [ -z "$(git config --global user.email)" ]; then
    read -p "请输入 git 邮箱: " git_email
    git config --global user.email "$git_email"
fi

# 启用 git 颜色
git config --global color.ui auto

log_success "git 配置完成"

#==============================================================================
# 第九部分: 验证安装
#==============================================================================
separator
log_info "验证安装..."

echo ""
echo "检查安装的工具版本:"
echo ""

echo "GCC:"
gcc --version | head -n1
echo ""

echo "CMake:"
cmake --version | head -n1
echo ""

echo "Make:"
make --version | head -n1
echo ""

echo "GDB:"
gdb --version | head -n1
echo ""

echo "Python:"
python --version
echo ""

echo "librados:"
if ldconfig -p | grep -q librados; then
    ldconfig -p | grep librados | head -n1
else
    echo "未安装 (需要从源码编译)"
fi

#==============================================================================
# 完成
#==============================================================================
separator
log_success "依赖安装完成!"
separator

echo ""
log_info "下一步操作:"
echo ""
echo "1. 克隆/导航到项目目录:"
echo "   cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw"
echo ""
echo "2. 设置脚本执行权限:"
echo "   chmod +x scripts/build_and_test.sh"
echo "   chmod +x scripts/smoke_test.sh"
echo ""
echo "3. 检查环境:"
echo "   ./scripts/build_and_test.sh check"
echo ""
echo "4. 完整构建和测试:"
echo "   ./scripts/build_and_test.sh all"
echo ""
echo "5. 或者分步执行:"
echo "   ./scripts/build_and_test.sh build    # 构建"
echo "   ./scripts/build_and_test.sh test     # 测试"
echo ""

#==============================================================================
# WSL Ubuntu 终端命令 - 请复制粘贴执行
#==============================================================================
# 打开 WSL Ubuntu 终端，然后依次运行以下命令

#------------------
# 步骤 1: 检查系统
#------------------
echo "=== 步骤 1: 检查系统 ==="
uname -a
cat /etc/os-release | head -5
gcc --version 2>/dev/null | head -1 || echo "gcc 未安装"
cmake --version 2>/dev/null | head -1 || echo "cmake 未安装"

#------------------
# 步骤 2: 更新系统并安装依赖
#------------------
echo ""
echo "=== 步骤 2: 安装依赖 (需要 sudo 密码) ==="
sudo apt update
sudo apt upgrade -y
sudo apt install -y build-essential cmake git libtool pkg-config \
    libboost-dev libboost-system-dev libboost-random-dev libboost-thread-dev \
    libnuma-dev liboath-dev libcurl4-openssl-dev libssl-dev libxml2-dev \
    python3-dev python3-pip libsqlite3-dev gdb valgrind

#------------------
# 步骤 3: 检查 librados
#------------------
echo ""
echo "=== 步骤 3: 检查 librados ==="
ldconfig -p | grep librados || echo "librados 未找到"
sudo apt install -y librados-dev || echo "librados-dev 安装失败"

#------------------
# 步骤 4: 创建 python 符号链接
#------------------
echo ""
echo "=== 步骤 4: Python 配置 ==="
if [ ! -f /usr/bin/python ]; then
    sudo ln -sf /usr/bin/python3 /usr/bin/python
    echo "创建 python -> python3 链接"
fi
python --version

#------------------
# 步骤 5: 设置脚本权限
#------------------
echo ""
echo "=== 步骤 5: 设置脚本权限 ==="
cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw
chmod +x scripts/*.sh
ls -la scripts/*.sh

#------------------
# 步骤 6: 运行一键安装和构建
#------------------
echo ""
echo "=== 步骤 6: 运行一键安装和构建 ==="
./scripts/setup_and_build.sh

echo ""
echo "=========================================="
echo "安装和构建完成!"
echo "=========================================="

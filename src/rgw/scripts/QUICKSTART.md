#==============================================================================
# WSL2 + Ubuntu 22.04 快速开始指南
#==============================================================================
#
# 本文档描述如何快速开始使用 WSL2 进行 SAL C 项目开发
#
#==============================================================================

## 一、快速安装脚本

### 1.1 Windows 端 - 安装 WSL2 (需要管理员权限)

在 PowerShell 中以管理员身份运行:

```powershell
cd C:\Users\10070\Desktop\ceph_rgw2c\src\rgw\scripts
.\install_wsl.ps1
```

或者手动安装:

```powershell
# 启用 WSL
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

# 重启电脑后安装 Ubuntu
wsl --install -d Ubuntu-22.04

# 设置默认版本
wsl --set-default-version 2
```

### 1.2 WSL Ubuntu 端 - 安装依赖

首次启动 Ubuntu 后:

```bash
# 复制安装脚本到主目录
cp /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw/scripts/install_deps.sh ~/

# 运行安装脚本
chmod +x ~/install_deps.sh
sudo ~/install_deps.sh
```

或者一行命令:

```bash
curl -fsSL https://raw.githubusercontent.com/your-repo/ceph_rgw2c/main/src/rgw/scripts/install_deps.sh | bash
```

---

## 二、快速验证

### 2.1 检查环境

```bash
# 检查 GCC
gcc --version

# 检查 CMake
cmake --version

# 检查 GDB
gdb --version

# 检查 librados
ldconfig -p | grep librados
```

### 2.2 运行构建脚本

```bash
# 进入项目目录
cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw

# 设置执行权限
chmod +x scripts/*.sh

# 检查环境
./scripts/build_and_test.sh check

# 完整构建
./scripts/build_and_test.sh build

# 运行测试
./scripts/build_and_test.sh test
```

---

## 三、WSL 优化配置

### 3.1 Windows 端配置 (.wslconfig)

在 `C:\Users\10070\.wslconfig` 创建文件:

```ini
[wsl2]
memory=8GB
processors=4
localhostForwarding=true
nestedVirtualization=true

[network]
generateResolvConf=false
```

### 3.2 Ubuntu 端配置 (.bashrc)

在 `~/.bashrc` 末尾添加:

```bash
# SAL C Project
alias salc-build='cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw && ./scripts/build_and_test.sh'
alias salc-test='cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw && ./scripts/build_and_test.sh test'
alias salc-smoke='cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw && ./scripts/smoke_test.sh'

# 启用颜色
export CLICOLOR=1

# GCC 颜色输出
export GCC_COLORS='error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'
```

应用更改:

```bash
source ~/.bashrc
```

---

## 四、常见问题

### 4.1 WSL 安装失败

**问题**: `dism.exe` 命令失败

**解决方案**:
1. 确保 Windows 已更新到最新版本
2. 启用 BIOS 中的虚拟化
3. 运行 Windows Update

### 4.2 Ubuntu 启动失败

**问题**: Ubuntu 无法启动

**解决方案**:
```powershell
# 重置 WSL
wsl --shutdown
wsl --unregister Ubuntu-22.04
wsl --install -d Ubuntu-22.04
```

### 4.3 访问 Windows 文件慢

**问题**: 在 WSL 中访问 `/mnt/c` 很慢

**解决方案**: 尽可能在 WSL 文件系统中工作

```bash
# 复制项目到 WSL
cp -r /mnt/c/Users/10070/Desktop/ceph_rgw2c ~/

# 在 WSL 中工作
cd ~/ceph_rgw2c/src/rgw
```

### 4.4 librados 找不到

**问题**: `ldconfig -p | grep librados` 返回空

**解决方案**: 需要从源码编译 librados

```bash
# 克隆 Ceph
git clone https://github.com/ceph/ceph.git ~/ceph-build
cd ~/ceph-build

# 编译 librados
./install-deps.sh
mkdir build && cd build
cmake .. -DWITH_RADOSGW=OFF -DWITH_MGR=OFF -DWITH_MDS=OFF -DWITH_TESTS=ON
make -j$(nproc) rados
```

---

## 五、每日工作流程

### 5.1 早上开始工作

```bash
# 启动 WSL
wsl

# 同步代码 (如果使用 git)
cd ~/ceph_rgw2c
git pull

# 快速验证
salc-smoke
```

### 5.2 修改代码后测试

```bash
# 构建
salc-build

# 测试
salc-test
```

### 5.3 晚上结束工作

```bash
# 提交代码
git add .
git commit -m "描述你的更改"
git push

# 关闭 WSL
exit
```

---

## 六、性能基准

### 6.1 预期构建时间

| 操作 | 预期时间 | 说明 |
|------|----------|------|
| c_common 首次构建 | 1-2 分钟 | 依赖下载和编译 |
| sal_c 首次构建 | 2-3 分钟 | 依赖下载和编译 |
| 增量构建 | 10-30 秒 | 仅重新编译修改的文件 |
| 完整测试 | 5-10 分钟 | 所有单元测试和驱动测试 |

### 6.2 预期测试结果

```
c_common Tests:
  22/22 PASSED

sal_c Tests:
  test_basic: PASSED
  test_rados_driver: PASSED
  test_integration: PARTIAL (60% 功能完整)

Performance:
  test_benchmark: < 10s
```

---

## 七、联系与支持

如有问题:
1. 查看构建日志错误信息
2. 检查 ASAN/Valgrind 报告
3. 查看项目文档

---

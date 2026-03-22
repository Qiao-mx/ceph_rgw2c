#==============================================================================
# SAL C 测试环境配置指南
#==============================================================================
#
# 本文档描述如何在 WSL2/Linux 环境中配置和运行 SAL C 测试
#
#==============================================================================

## 一、环境要求

### 1.1 硬件要求

| 组件 | 最低要求 | 推荐配置 |
|------|----------|----------|
| CPU | 4 核心 | 8+ 核心 |
| 内存 | 8 GB | 16 GB |
| 磁盘 | 20 GB 可用空间 | SSD |
| 操作系统 | Ubuntu 22.04 / WSL2 | Ubuntu 22.04 LTS |

### 1.2 软件依赖

```bash
# 基础编译工具
sudo apt install -y \
    build-essential \
    cmake \
    git \
    libtool \
    pkg-config

# Ceph 编译依赖
sudo apt install -y \
    libboost-dev \
    libboost-system-dev \
    libboost-random-dev \
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
    libsqlite3-dev \
    libfuse-dev \
    uuid-dev

# 调试工具（可选但推荐）
sudo apt install -y \
    gdb \
    valgrind \
    linux-tools-common \
    linux-tools-generic

# Python 依赖
pip3 install boto3 awscli
```

---

## 二、项目结构

```
ceph_rgw2c/
├── src/
│   └── rgw/
│       ├── c_common/                 # C 通用库
│       │   ├── include/             # 头文件
│       │   ├── src/                 # 源文件
│       │   ├── containers/          # 容器实现
│       │   ├── tests/              # 测试文件
│       │   └── build/              # 构建目录
│       │
│       ├── sal_c/                   # SAL C 实现
│       │   ├── include/            # 头文件
│       │   │   └── drivers/        # 驱动接口
│       │   ├── src/                # 源文件
│       │   │   └── drivers/        # 驱动实现
│       │   ├── tests/              # 测试文件
│       │   │   └── benchmark/      # 性能测试
│       │   └── build/              # 构建目录
│       │
│       ├── scripts/                # 脚本
│       │   └── build_and_test.sh   # 构建测试脚本
│       └── vstart.sh              # Ceph 开发集群
```

---

## 三、构建步骤

### 3.1 快速开始

```bash
# 1. 进入项目目录
cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw

# 2. 使用构建脚本（推荐）
chmod +x scripts/build_and_test.sh
./scripts/build_and_test.sh all

# 或者分步执行
./scripts/build_and_test.sh check     # 检查环境
./scripts/build_and_test.sh build      # 构建
./scripts/build_and_test.sh test       # 测试
./scripts/build_and_test.sh bench      # 性能测试
```

### 3.2 手动构建 c_common

```bash
# 进入 c_common 目录
cd src/rgw/c_common

# 创建构建目录
mkdir -p build && cd build

# 配置 CMake（Debug + ASAN）
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_ASAN=ON

# 编译
make -j$(nproc)

# 运行测试
make run_tests

# 或使用 ctest
ctest --output-on-failure -V
```

### 3.3 手动构建 sal_c

```bash
# 进入 sal_c 目录
cd src/rgw/sal_c

# 创建构建目录
mkdir -p build && cd build

# 配置 CMake（Debug + ASAN）
cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DWITH_ASAN=ON

# 编译
make -j$(nproc)

# 运行测试
ctest --output-on-failure -V
```

---

## 四、运行测试

### 4.1 c_common 测试

```bash
cd src/rgw/c_common/build

# 运行所有测试
ctest --output-on-failure -V

# 运行单个测试
./test_carray
./test_cstring
./test_cmap
./test_cset
./test_cdeque
./test_cstack
./test_cqueue
./test_cpriority_queue
./test_coptional
./test_clist
./test_oop
./test_errors
./test_hex
./test_b64
./test_buffer

# 综合测试
./test_comprehensive

# 性能测试
./test_benchmark
```

### 4.2 sal_c 测试

```bash
cd src/rgw/sal_c/build/tests

# 基础类型测试（不依赖 librados）
./test_basic

# RADOS 驱动测试（需要 librados）
./test_rados_driver

# DBStore 驱动测试
./test_dbstore_driver

# 集成测试
./test_integration
```

### 4.3 预期输出示例

```
[INFO] Running test_carray...
Test: array creation... PASS
Test: array append... PASS
Test: array insert... PASS
Test: array erase... PASS
Test: array get... PASS
Test: array destroy... PASS
All tests PASSED!

[INFO] Running test_cstring...
Test: string creation... PASS
Test: string append... PASS
Test: string find... PASS
Test: string compare... PASS
All tests PASSED!
```

---

## 五、调试配置

### 5.1 GDB 调试

```bash
# 使用 GDB 运行测试
gdb --args ./test_rados_driver

# 在 GDB 中设置断点
(gdb) break main
(gdb) break rgw_sal_user_create
(gdb) run

# 查看调用栈
(gdb) bt

# 查看变量
(gdb) print user
(gdb) print *user

# 单步执行
(gdb) next
(gdb) step

# 查看内存
(gdb) x/100x buffer
```

### 5.2 AddressSanitizer (ASAN)

ASAN 已集成到构建中，自动启用内存错误检测：

```bash
# 运行测试，ASAN 会自动检测内存错误
./test_rados_driver

# 常见 ASAN 错误类型
# - heap-buffer-overflow: 堆缓冲区溢出
# - stack-buffer-overflow: 栈缓冲区溢出
# - use-after-free: 使用已释放内存
# - double-free: 双重释放
# - memory leaks: 内存泄漏
```

### 5.3 Valgrind 内存分析

```bash
# 安装 Valgrind
sudo apt install valgrind

# 运行内存检测
valgrind --leak-check=full --show-leak-kinds=all \
    --track-origins=yes ./test_rados_driver

# 输出到文件
valgrind --log-file=valgrind.log ./test_rados_driver

# 检测未初始化内存
valgrind --track-origins=yes ./test_basic
```

---

## 六、Ceph 集群配置（可选）

### 6.1 使用 vstart 启动开发集群

```bash
cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src

# 设置环境变量
export CEPH_NUM_MON=1
export CEPH_NUM_OSD=1
export CEPH_NUM_MGR=1
export CEPH_NUM_RGW=0

# 启动集群（使用 memstore 后端）
./vstart.sh -n -l -d --memstore

# 停止集群
./stop.sh
```

### 6.2 使用 Docker 部署 Ceph

```bash
# 拉取 Ceph 镜像
docker pull ceph/daemon:latest-stable-reef

# 启动 MON
docker run -d --name ceph-mon \
  --network host \
  -v /mnt/ceph:/etc/ceph \
  ceph/daemon:latest-stable-reef mon

# 检查状态
docker exec ceph-mon ceph -s
```

---

## 七、常见问题

### 7.1 librados 找不到

```bash
# 检查 librados 是否安装
ldconfig -p | grep librados

# 如果没有安装，需要从源码编译 Ceph
# 或者安装系统包
sudo apt install librados-dev
```

### 7.2 编译错误

```bash
# 清理构建目录
rm -rf build/*

# 重新配置
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 重新编译
make clean && make -j$(nproc)
```

### 7.3 测试失败

```bash
# 使用 ASAN 重新运行
./test_rados_driver

# 使用 Valgrind 检查
valgrind ./test_rados_driver

# 查看详细输出
ctest --output-on-failure -V
```

### 7.4 WSL2 性能问题

```bash
# 在 Windows 端创建 .wslconfig 文件
# 文件位置: C:\Users\<用户名>\.wslconfig

[wsl2]
memory=8GB
processors=4
localhostForwarding=true

# 重启 WSL
wsl --shutdown
wsl -d Ubuntu-22.04
```

---

## 八、性能基准

### 8.1 关键性能指标

| 测试项 | 目标值 | 说明 |
|--------|--------|------|
| test_benchmark (总时间) | < 10s | 包含所有容器操作 |
| 用户创建 | < 10ms | 单用户操作 |
| 桶创建 | < 20ms | 单桶操作 |
| 对象写入 (1KB) | < 5ms | 单对象写入 |

### 8.2 运行性能测试

```bash
cd src/rgw/c_common/build
./test_benchmark

# 预期输出
# ============================================================
# SAL C Benchmark Results
# ============================================================
# Carray:     100000 ops in 1.23s (81234 ops/sec)
# Cmap:        10000 ops in 0.45s (22222 ops/sec)
# Cstring:     50000 ops in 2.10s (23809 ops/sec)
# ...
# ============================================================
```

---

## 九、联系与支持

如有问题，请检查：
1. 构建日志输出
2. 测试失败的详细错误信息
3. ASAN/Valgrind 的错误报告

---

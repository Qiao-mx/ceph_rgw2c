# Ceph WSL 编译指南

## 环境信息
- WSL: Ubuntu 24.04
- 代码位置: /mnt/d/NAS/ceph-20.1.1

## 方案 1: 仅编译 sal_c 模块（推荐用于开发）

```bash
# 进入代码目录
cd /mnt/d/NAS/ceph-20.1.1/src/rgw/sal_c

# 创建构建目录
mkdir -p build
cd build

# CMake 配置（仅 sal_c）
cmake .. -DCMAKE_BUILD_TYPE=Debug

# 编译
make -j$(nproc)

# 运行测试
make run_tests
ctest --verbose
```

## 方案 2: 编译 RGW 相关模块

```bash
cd /mnt/d/NAS/ceph-20.1.1/build

# 重新配置（确保 sal_c 被包含）
cmake .. -DWITH_RADOSGW=ON -DWITH_RADOSGW_DBSTORE=ON

# 编译 radosgw 和相关库
ninja -j$(nproc) radosgw librgw rgw_common

# 运行测试
ctest -R rgw -j$(nproc)
```

## 方案 3: 完整编译（需要数小时）

```bash
cd /mnt/d/NAS/ceph-20.1.1
rm -rf build  # 可选：清理旧构建

# 创建新构建目录
mkdir build && cd build

# 配置（RelWithDebInfo 是较好的平衡）
./do_cmake.sh -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 编译（限制并行数以避免内存不足）
ninja -j3

# 安装
ninja install
```

## 常用调试命令

```bash
# 查看可用的编译目标
ninja -t targets | grep -i sal_c

# 只编译特定目标
ninja rgw_sal_c

# 清理特定目标
ninja -t clean rgw_sal_c

# 查看构建状态
ninja -n  # 预览将要执行的操作
```

## 运行 vstart 测试集群

```bash
cd /mnt/d/NAS/ceph-20.1.1/build

# 仅启动 RGW
./bin/radosgw -d -c /mnt/d/NAS/ceph-20.1.1/build/ceph.conf --rgw-frontends=beast port=7480

# 使用 vstart 脚本
cd /mnt/d/NAS/ceph-20.1.1
./src/vstart.sh --debug -x --localhost --bluestore -n
```

## 常见问题解决

### 内存不足
```bash
# 限制编译并行数
ninja -j2  # 或 -j1 如果内存非常紧张
```

### 编译错误
```bash
# 清理并重新配置
cd build
rm -rf *
cmake ..

# 重新编译
ninja -j$(nproc)
```

### WSL 访问 Windows 文件性能问题
```bash
# 如果编译太慢，考虑将代码复制到 WSL 内部
cp -r /mnt/d/NAS/ceph-20.1.1 ~/ceph-dev
cd ~/ceph-dev/build
cmake ..
ninja -j$(nproc)
```

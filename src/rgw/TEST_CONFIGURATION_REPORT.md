#==============================================================================
# SAL C 测试配置报告
#
# 本文档记录了当前项目的测试配置状态
# 生成时间: 2026-03-22
#==============================================================================

## 一、项目信息

| 属性 | 值 |
|------|-----|
| 项目名称 | ceph_rgw2c |
| 项目类型 | Ceph RGW C++ 到 C 转换 |
| 源码路径 | `C:\Users\10070\Desktop\ceph_rgw2c` |
| WSL路径 | `/mnt/c/Users/10070/Desktop/ceph_rgw2c` |

---

## 二、构建配置状态

### 2.1 c_common 库

| 配置项 | 状态 | 说明 |
|--------|------|------|
| 构建目录 | ✅ 已存在 | `src/rgw/c_common/build` |
| CMake 缓存 | ✅ 已配置 | Debug + ASAN |
| librgw_c_common 库 | ⚠️ 未编译 | 需要在 WSL 中执行 make |
| 测试可执行文件 | ⚠️ 未编译 | 需要在 WSL 中执行 make |
| WITH_ASAN | ✅ 已启用 | `-DWITH_ASAN=ON` |
| WITH_VALGRIND | ✅ 已配置 | `-DWITH_VALGRIND=OFF` |

**CMakeCache 关键配置:**
```cmake
CMAKE_BUILD_TYPE=Debug
WITH_ASAN=ON
WITH_VALGRIND=OFF
WITH_MSAN=OFF
WITH_UBSAN=OFF
```

### 2.2 sal_c 库

| 配置项 | 状态 | 说明 |
|--------|------|------|
| 构建目录 | ✅ 已存在 | `src/rgw/sal_c/build` |
| CMake 缓存 | ✅ 已配置 | Debug + ASAN |
| librados | ✅ 已检测 | `/lib/x86_64-linux-gnu/librados.so` |
| 测试可执行文件 | ⚠️ 未编译 | 需要在 WSL 中执行 make |
| WITH_ASAN | ✅ 已启用 | `-DWITH_ASAN=ON` |

**CMakeCache 关键配置:**
```cmake
LIBRADOS_LIBRARY=/lib/x86_64-linux-gnu/librados.so
LIBRADOS_INCLUDE_DIR=/usr/include
WITH_ASAN=ON
CMAKE_BUILD_TYPE=Debug
```

---

## 三、测试文件清单

### 3.1 c_common 测试文件 (22个)

| 测试名称 | 对应模块 | 编译状态 |
|----------|----------|----------|
| test_carray | rgw_carray | ⚠️ 待编译 |
| test_cstring | rgw_cstring | ⚠️ 待编译 |
| test_cmap | rgw_cmap | ⚠️ 待编译 |
| test_cset | rgw_cset | ⚠️ 待编译 |
| test_cdeque | rgw_cdeque | ⚠️ 待编译 |
| test_cstack | rgw_cstack | ⚠️ 待编译 |
| test_cqueue | rgw_cqueue | ⚠️ 待编译 |
| test_cpriority_queue | rgw_cpriority_queue | ⚠️ 待编译 |
| test_coptional | rgw_coptional | ⚠️ 待编译 |
| test_clist | rgw_clist | ⚠️ 待编译 |
| test_oop | rgw_oop | ⚠️ 待编译 |
| test_errors | rgw_errors | ⚠️ 待编译 |
| test_buffer | rgw_buffer | ⚠️ 待编译 |
| test_hex | rgw_hex | ⚠️ 待编译 |
| test_b64 | rgw_b64 | ⚠️ 待编译 |
| test_xml | rgw_xml | ⚠️ 待编译 |
| test_memory | rgw_cmemory | ⚠️ 待编译 |
| test_ccontainer | 综合 | ⚠️ 待编译 |
| test_ccontainer_edge_cases | 边界测试 | ⚠️ 待编译 |
| test_comprehensive | 综合 | ⚠️ 待编译 |
| test_cpp_to_c | 示例 | ⚠️ 待编译 |
| test_benchmark | 性能测试 | ⚠️ 待编译 |

### 3.2 sal_c 测试文件 (5个)

| 测试名称 | 对应模块 | librados依赖 | 编译状态 |
|----------|----------|--------------|----------|
| test_basic | 核心类型 | ❌ 无依赖 | ⚠️ 待编译 |
| test_rados_driver | RADOS驱动 | ✅ 需要 | ⚠️ 待编译 |
| test_dbstore_driver | DBStore驱动 | ✅ 需要 | ⚠️ 待编译 |
| test_daos_driver | DAOS驱动 | ⚠️ 可选 | ⚠️ 待编译 |
| test_integration | 集成测试 | ✅ 需要 | ⚠️ 待编译 |

---

## 四、待办事项

### 4.1 环境配置 (Day 1)

- [ ] **WSL2 安装与配置**
  - [ ] 安装 Ubuntu 22.04 WSL2
  - [ ] 配置 .wslconfig (内存 8GB, 4核心)
  - [ ] 安装基础依赖
  - [ ] 创建符号链接 (python -> python3)

- [ ] **librados 开发环境**
  - [ ] 克隆 Ceph 源码 (如果需要)
  - [ ] 从源码构建 librados
  - [ ] 验证 librados 可用

- [ ] **Ceph vstart 集群**
  - [ ] 配置 CEPH_NUM_MON=1
  - [ ] 配置 CEPH_NUM_OSD=1
  - [ ] 使用 --memstore 启动

### 4.2 构建与测试 (Day 1-2)

- [ ] **c_common 构建**
  - [ ] 运行 `mkdir -p build && cd build`
  - [ ] 运行 `cmake .. -DCMAKE_BUILD_TYPE=Debug -DWITH_ASAN=ON`
  - [ ] 运行 `make -j$(nproc)`
  - [ ] 运行 `make run_tests`

- [ ] **sal_c 构建**
  - [ ] 运行 `mkdir -p build && cd build`
  - [ ] 运行 `cmake .. -DCMAKE_BUILD_TYPE=Debug -DWITH_ASAN=ON`
  - [ ] 运行 `make -j$(nproc)`

### 4.3 测试执行 (Day 2-3)

- [ ] **单元测试**
  - [ ] 运行 c_common 容器测试
  - [ ] 验证所有容器测试通过
  - [ ] 运行性能基准测试
  - [ ] 记录性能数据

- [ ] **驱动测试**
  - [ ] 运行 test_basic (无依赖)
  - [ ] 运行 test_rados_driver (需要 librados)
  - [ ] 运行 test_dbstore_driver (需要 librados)
  - [ ] 运行 test_integration (需要 librados)

### 4.4 调试与优化 (Day 4-5)

- [ ] **内存调试**
  - [ ] 使用 ASAN 检测内存错误
  - [ ] 使用 Valgrind 检测内存泄漏
  - [ ] 修复发现的内存问题

- [ ] **性能优化**
  - [ ] 运行 perf 分析
  - [ ] 生成火焰图
  - [ ] 识别热点代码
  - [ ] 实施优化

---

## 五、快速开始命令

在 WSL 中执行以下命令快速开始:

```bash
# 1. 进入项目目录
cd /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw

# 2. 设置执行权限
chmod +x scripts/build_and_test.sh

# 3. 检查环境
./scripts/build_and_test.sh check

# 4. 完整构建和测试
./scripts/build_and_test.sh all

# 5. 或者分步执行
./scripts/build_and_test.sh build    # 构建
./scripts/build_and_test.sh test     # 测试
./scripts/build_and_test.sh bench     # 性能测试
```

---

## 六、预期测试结果

### 6.1 c_common 测试预期

所有 22 个测试应该全部通过:
- ✅ test_carray - 动态数组
- ✅ test_cstring - 字符串
- ✅ test_cmap - 有序 Map
- ✅ test_cset - 有序 Set
- ✅ test_cdeque - 双端队列
- ✅ test_cstack - 栈
- ✅ test_cqueue - 队列
- ✅ test_cpriority_queue - 优先队列
- ✅ test_coptional - 可选类型
- ✅ test_clist - 双向链表
- ✅ test_oop - OOP 框架
- ✅ test_errors - 错误处理
- ✅ test_buffer - Buffer 管理
- ✅ test_hex - Hex 编码
- ✅ test_b64 - Base64 编码
- ✅ test_xml - XML 解析
- ✅ test_memory - 内存管理
- ✅ test_ccontainer - 综合容器
- ✅ test_ccontainer_edge_cases - 边界测试
- ✅ test_comprehensive - 综合测试
- ✅ test_cpp_to_c - 示例代码
- ✅ test_benchmark - 性能测试

### 6.2 sal_c 测试预期

| 测试 | 依赖 | 预期结果 |
|------|------|----------|
| test_basic | 无 | ✅ 应通过 |
| test_rados_driver | librados | ✅ 应通过 (90%功能完整) |
| test_dbstore_driver | librados | ⚠️ 部分通过 (60%功能完整) |
| test_integration | librados | ⚠️ 部分通过 |

---

## 七、已知问题

### 7.1 librados 版本兼容

**问题**: 不同版本的 Ceph 可能有不同的 librados API

**解决方案**: 确保使用的 librados 版本与测试代码兼容

### 7.2 ASAN 性能影响

**问题**: ASAN 会显著降低运行速度

**解决方案**: 在性能测试时使用非 ASAN 构建

### 7.3 WSL 文件系统性能

**问题**: 在 WSL 中访问 Windows 文件系统较慢

**解决方案**: 尽可能将项目放在 WSL 文件系统中

---

## 八、文档更新记录

| 日期 | 更新内容 | 更新人 |
|------|----------|--------|
| 2026-03-22 | 初始创建 | Claude |
| - | - | - |

---

## 九、参考链接

- [Ceph 官方文档](https://docs.ceph.com/)
- [Ceph RGW 文档](https://docs.ceph.com/en/latest/radosgw/)
- [WSL2 文档](https://docs.microsoft.com/en-us/windows/wsl/)
- [AddressSanitizer 文档](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Valgrind 文档](https://valgrind.org/docs/)

---

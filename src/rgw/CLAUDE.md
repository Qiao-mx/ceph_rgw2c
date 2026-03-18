# CLAUDE.md

此文件为 Claude Code (claude.ai/code) 提供在此仓库中处理代码的指导。

## 项目概述

RGW (RADOS Gateway) 是 Ceph 对象存储的 S3/Swift 兼容网关。当前项目正在进行从 C++ 到 C 语言的渐进式转换，同时保持功能完整性和性能指标。转换后的代码应具备更好的可移植性、更小的运行时依赖。

## 构建系统

### 主要构建目标
项目使用 CMake 构建系统，主要目标包括：

- **rgw_common**: 核心静态库，包含 RGW 基础功能
- **rgw_a**: 辅助静态库，包含前端和客户端功能
- **rgw_schedulers**: 调度器静态库
- **radosgw**: 主要的 RGW 守护进程可执行文件
- **radosgw-admin**: 管理工具
- **rgw**: 共享库 (librgw)
- **rgw_c_common**: C 语言容器库（用于 C++ 到 C 转换）

### 构建命令
```bash
# 在主项目目录中
cd /d/NAS/ceph-20.1.1
mkdir build && cd build
cmake .. -DWITH_RADOSGW=ON [其他选项]
make -j$(nproc)

# 仅构建 RGW 相关目标
make radosgw radosgw-admin rgw_common rgw_a

# 构建并运行 c_common 测试
cd src/rgw/c_common
mkdir build && cd build
cmake .. && make
make run_tests  # 运行所有测试
ctest --verbose  # 使用 CMake 测试运行器
```

### 关键 CMake 选项
- `WITH_RADOSGW=ON`: 启用 RGW 构建（默认开启）
- `WITH_RADOSGW_DBSTORE=ON`: 启用 DBStore 后端
- `WITH_RADOSGW_POSIX=ON`: 启用 POSIX 后端
- `WITH_RADOSGW_DAOS=ON`: 启用 DAOS 后端（实验性）
- `WITH_RADOSGW_ARROW_FLIGHT=ON`: 启用 Arrow Flight 支持
- `WITH_RADOSGW_AMQP_ENDPOINT=ON`: 启用 AMQP 端点
- `WITH_RADOSGW_KAFKA_ENDPOINT=ON`: 启用 Kafka 端点

## 代码架构

### 核心模块层次结构
```
应用层 (radosgw, radosgw-admin)
    ↓
前端层 (rgw_a) - HTTP 前端、客户端 I/O、认证
    ↓
服务层 (services/) - 各类服务（用户、桶、配额、元数据等）
    ↓
REST 层 (rgw_rest_*, rgw_op) - S3/Swift 协议实现
    ↓
存储抽象层 (SAL) (rgw_sal.h) - 统一存储接口
    ↓
驱动层 (driver/) - 具体存储后端实现
    ↓
Ceph 存储层 (librados) - RADOS 原生接口
```

### 存储抽象层 (SAL)
SAL 是 RGW 的核心设计，提供统一的存储接口：
- **rgw_sal.h**: 定义抽象基类和接口
- **driver/rados/rgw_sal_rados.cc**: RADOS 后端实现（主要）
- **driver/dbstore/rgw_sal_dbstore.cc**: DBStore 后端
- **driver/posix/rgw_sal_posix.cc**: POSIX 文件系统后端

SAL 使用经典的面向对象设计模式，所有存储操作通过虚函数接口调用。

### C 语言转换架构
正在进行的 C++ 到 C 转换采用以下架构：

```
C++ RGW 原代码
    ↓ (渐进式转换)
C 语言接口层 (使用 extern "C" 包装)
    ↓
C 语言实现 (使用 c_common 容器库)
```

**c_common 容器库**提供 STL 等效功能：
- `rgw_array_t` ↔ `std::vector`
- `rgw_map_t` ↔ `std::map` (红黑树实现)
- `rgw_clist_t` ↔ `std::list` (双向链表)
- `rgw_cstring_t` ↔ `std::string`
- `rgw_hash_map_t` ↔ `std::unordered_map` (uthash 实现)
- `rgw_optional_t` ↔ `std::optional`

### 关键目录结构
```
src/rgw/
├── c_common/              # C 语言容器库（转换基础设施）
│   ├── include/          # 公共头文件
│   ├── src/             # 实现文件
│   ├── containers/      # 容器实现
│   └── tests/           # 单元测试
├── driver/              # 存储驱动实现
│   ├── rados/          # RADOS 后端（主要）
│   ├── dbstore/        # 数据库存储后端
│   ├── posix/          # POSIX 文件系统后端
│   └── daos/           # DAOS 后端（实验性）
├── services/           # 服务层实现
│   ├── svc_user.cc     # 用户服务
│   ├── svc_bucket.cc   # 桶服务
│   └── svc_quota.cc    # 配额服务
├── rgw_common.h        # 核心数据类型定义
├── rgw_sal.h           # 存储抽象层接口
├── rgw_op.cc           # 操作处理器（最大的文件）
├── rgw_rest_*.cc       # REST API 实现
└── rgw_auth_*.cc       # 认证授权模块
```

## 开发工作流

### 运行单个测试
```bash
# c_common 测试
cd src/rgw/c_common/build
./test_cmap      # 测试映射容器
./test_carray    # 测试数组容器
./test_cstring   # 测试字符串
./test_all       # 运行所有测试

# 主项目测试（如果配置了 WITH_TESTS）
cd /d/NAS/ceph-20.1.1/build
./bin/ceph_rgw_jsonparser
./bin/ceph_rgw_multiparser
```

### 调试构建
```bash
cd /d/NAS/ceph-20.1.1/build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make clean && make -j$(nproc) radosgw

# 使用调试器
gdb --args ./bin/radosgw -d -c /path/to/ceph.conf
```

### 代码转换工作流
1. **分析阶段**: 使用 `rgw项目分析.md` 中的统计数据识别 C++ 特性使用情况
2. **容器替换**: 将 STL 容器替换为 c_common 等效容器
3. **接口转换**: 将类转换为 C 结构体 + 虚函数表
4. **错误处理**: 将异常转换为错误码返回
5. **测试验证**: 运行现有测试确保功能一致

### 混合编译支持
转换期间支持 C++ 和 C 代码共存：
```c
// C++ 调用 C
extern "C" {
    #include "rgw_core.h"
    int rgw_c_function(...);
}

// C 调用 C++（通过包装层）
#ifdef __cplusplus
extern "C" {
#endif
    int rgw_cpp_wrapper_function(...);
#ifdef __cplusplus
}
#endif
```

## 依赖管理

### 主要第三方依赖
- **librados**: Ceph RADOS C 客户端（C 接口）
- **Boost**: context, filesystem（部分功能）
- **OpenSSL**: 加密和 TLS
- **ICU**: Unicode 支持
- **Lua**: 脚本支持
- **RapidJSON**: JSON 解析
- **curl**: HTTP 客户端
- **fmt**: 格式化库（可能被替换）

### 转换相关依赖
- **uthash**: C 语言哈希表实现（已集成到 c_common）
- **rbt_tree**: 红黑树实现（已集成到 c_common）

## 配置与部署

### 快速测试集群
```bash
# 使用 vstart.sh 启动测试集群
cd /d/NAS/ceph-20.1.1
RGW=1 MON=1 OSD=1 ../src/vstart.sh -d -n

# 配置 DAOS 后端（实验性）
echo -e "[client]\nrgw backend store = daos" >> ceph.conf
```

### RGW 配置示例
```conf
[client.rgw.instance]
    rgw frontends = beast port=7480
    rgw backend store = rados
    rgw log file = /var/log/ceph/radosgw.log
```

## 转换状态跟踪

当前转换状态记录在 `cpp2c-document.md` 中，包含：
- 阶段划分和时间估算
- 模块转换优先级
- 风险缓解策略
- 测试和质量保障计划

## 注意事项

1. **内存管理**: C 语言需要手动管理内存，务必使用 c_common 提供的分配/释放函数
2. **错误处理**: 所有 C 函数必须返回错误码，禁止使用异常
3. **线程安全**: 部分容器非线程安全，需注意并发访问
4. **性能基准**: 转换前后需进行性能对比测试，确保关键路径性能不低于 90%
5. **向后兼容**: 保持 API 兼容性，确保现有客户端不受影响

## 扩展阅读

- `rgw项目分析.md`: 详细的代码分析和转换评估
- `c_common/docs/DIRECTORY_STRUCTURE.md`: C 容器库详细文档
- `cpp2c-document.md`: 完整的转换项目设计文档
- Ceph 官方文档: https://docs.ceph.com/
+ 如果存在 .cursorrules 或 Copilot 规则文件，请确保包含重要部分。

---

*本文件最后更新于 2026-03-16，基于代码库分析和现有文档生成。*
# RGW C++ 到 C 转换项目架构文档

## 1. 项目概述

本项目旨在将 Ceph RGW (RADOS Gateway) 模块从 C++ 语言逐步转换为 C 语言，同时保持功能完整性和性能指标。

## 2. 文件结构

### 2.1 根目录结构

```
ceph-20.1.1/
├── src/rgw/
│   ├── c_common/                    # C 通用库（转换的基础设施）
│   │   ├── include/                 # 头文件
│   │   │   ├── containers/          # 容器头文件
│   │   │   │   ├── rgw_carray.h    # 动态数组
│   │   │   │   ├── rgw_cstring.h    # 字符串
│   │   │   │   ├── rgw_cmap.h      # 有序 Map
│   │   │   │   ├── rgw_cset.h      # 有序 Set
│   │   │   │   ├── rgw_clist.h      # 双向链表
│   │   │   │   ├── rgw_cdeque.h    # 双端队列
│   │   │   │   ├── rgw_cstack.h    # 栈
│   │   │   │   ├── rgw_cqueue.h    # 队列
│   │   │   │   ├── rgw_cpriority_queue.h # 优先队列
│   │   │   │   ├── rgw_chash_map.h  # 哈希表
│   │   │   │   └── rgw_coptional.h  # 可选类型
│   │   │   ├── internal/            # 内部头文件（第三方库）
│   │   │   ├── rgw_ccommon.h       # 统一头文件
│   │   │   ├── rgw_oop.h           # OOP 框架
│   │   │   └── rgw_errors.h         # 错误处理
│   │   ├── src/                     # 源文件
│   │   │   ├── rgw_cmemory.c        # 内存管理
│   │   │   ├── rgw_ctypes.c         # 基础类型
│   │   │   ├── rgw_carray.c         # 动态数组
│   │   │   ├── rgw_cstring.c        # 字符串
│   │   │   ├── rgw_cmap.c           # 有序 Map
│   │   │   ├── rgw_cset.c           # 有序 Set
│   │   │   ├── rgw_coptional.c      # 可选类型
│   │   │   ├── rgw_cdeque.c         # 双端队列
│   │   │   ├── rgw_cstack.c         # 栈
│   │   │   ├── rgw_cqueue.c         # 队列
│   │   │   ├── rgw_oop.c            # OOP 实现
│   │   │   ├── rgw_errors.c         # 错误处理实现
│   │   │   ├── rgw_xml.c           # XML 解析器
│   │   │   ├── rgw_b64.c           # Base64 编解码
│   │   │   └── rgw_hex.c           # 十六进制编解码
│   │   ├── containers/              # 容器实现（第三方库封装）
│   │   │   ├── rgw_clist.c         # 双向链表
│   │   │   └── rgw_chash_map.c     # 哈希表
│   │   ├── tests/                   # 测试文件
│   │   ├── build/                   # 构建目录
│   │   ├── CMakeLists.txt          # 构建配置
│   │   └── memory-bank/            # 项目文档
│   │       ├── architecture.md      # 本文档
│   │       ├── cpp2c-document.md   # 转换概要设计
│   │       ├── implementation-plan.md # 实施计划
│   │       ├── tech-stack.md       # 技术栈
│   │       └── progress.md         # 进度跟踪
│   │
│   ├── sal_c/                       # SAL C 接口（存储抽象层转换）
│   │   ├── include/                 # SAL C 头文件
│   │   │   ├── rgw_sal_errors.h    # 错误码定义
│   │   │   ├── rgw_sal_types.h     # 核心类型定义
│   │   │   ├── rgw_sal.h           # 主 SAL C 接口 (含 vtable)
│   │   │   ├── rgw_sal_c.h         # 统一头文件
│   │   │   ├── rgw_sal_rados.h     # RADOS 驱动 C 接口
│   │   │   ├── rgw_sal_dbstore.h   # DBStore 驱动 C 接口
│   │   │   ├── rgw_sal_posix.h     # POSIX 驱动 C 接口
│   │   │   ├── rgw_sal_d4n.h       # D4N 驱动 C 接口
│   │   │   ├── rgw_sal_motr.h      # Motr 驱动 C 接口
│   │   │   └── rgw_sal_daos.h      # DAOS 驱动 C 接口
│   │   ├── src/                     # SAL C 源文件
│   │   │   ├── rgw_sal_types.c     # 类型实现
│   │   │   ├── rgw_sal.c           # 基础 SAL API
│   │   │   ├── rgw_sal_rados.c     # RADOS 驱动实现
│   │   │   ├── rgw_sal_dbstore.c   # DBStore 驱动实现
│   │   │   ├── rgw_sal_posix.c     # POSIX 驱动实现
│   │   │   ├── rgw_sal_d4n.c       # D4N 驱动实现
│   │   │   ├── rgw_sal_motr.c      # Motr 驱动实现
│   │   │   └── rgw_sal_daos.c      # DAOS 驱动实现
│   │   ├── tests/                   # SAL C 测试
│   │   │   ├── CMakeLists.txt      # 测试构建配置
│   │   │   ├── test_rados_driver.c  # RADOS 驱动测试 (35个测试)
│   │   │   ├── test_dbstore_driver.c # DBStore 驱动测试
│   │   │   └── test_integration.c   # 集成测试
│   │   ├── build/                   # 构建目录
│   │   ├── CMakeLists.txt           # 构建配置
│   │   ├── MAPPING_DETAIL.md        # C++ 到 C 详细映射
│   │   └── IMPLEMENTATION_SUMMARY.md # 实现总结
│   │
│   └── (其他 RGW 代码)
```

### 2.2 目录规范

| 目录 | 内容 | 说明 |
|------|------|------|
| `c_common/include/containers/` | 容器头文件 | 公开 API |
| `c_common/include/internal/` | 内部头文件 | 第三方库封装 |
| `c_common/include/` | 核心头文件 | OOP、错误处理、统一入口 |
| `c_common/src/` | 核心源文件 | 主要实现 |
| `c_common/containers/` | 容器源文件 | 容器实现 |
| `c_common/tests/` | 测试文件 | 单元测试 |
| `c_common/build/` | 构建目录 | CMake 构建输出 |
| `sal_c/include/` | SAL C 头文件 | SAL 接口定义 |
| `sal_c/src/` | SAL C 源文件 | 驱动实现 |
| `sal_c/tests/` | SAL C 测试 | 驱动测试 |

## 3. C 版本代码结构

### 3.1 容器 API 设计模式

所有容器遵循统一的 API 设计模式：

```c
// 1. 不透明句柄类型
typedef struct rgw_xxx rgw_xxx_t;

// 2. 创建/销毁
rgw_xxx_t* rgw_xxx_create(void (*value_free)(void*));
void rgw_xxx_destroy(rgw_xxx_t* xxx);

// 3. 基本操作
int rgw_xxx_xxx(rgw_xxx_t* xxx, ...);
```

**关键特性**:
- `value_free`: 元素释放回调，NULL 表示不自动释放
- 返回值: 成功返回 0 (RGW_OK)，失败返回错误码
- 内存管理: 使用 `rgw_c_alloc` / `rgw_c_free`

### 3.2 已实现的容器

| STL 容器 | C 实现 | 底层实现 | 状态 |
|-----------|--------|----------|------|
| `std::vector` | rgw_carray | utarray.h | ✅ 已完成 |
| `std::string` | rgw_cstring | utstring.h | ✅ 已完成 |
| `std::map` | rgw_cmap | 红黑树 | ✅ 已完成 |
| `std::set` | rgw_cset | 红黑树 | ✅ 已完成 |
| `std::list` | rgw_clist | 双向链表 | ✅ 已完成 |
| `std::deque` | rgw_cdeque | utringbuffer.h | ✅ 已完成 |
| `std::stack` | rgw_stack_t | utstack.h | ✅ 已完成 |
| `std::queue` | rgw_queue_t | 双向链表 | ✅ 已完成 |
| `std::unordered_map` | rgw_chash_map | uthash.h | ✅ 已完成 |
| `std::optional` | rgw_coptional | 自定义 | ✅ 已完成 |
| `std::priority_queue` | rgw_priority_queue_t | 二叉堆 | ✅ 已完成 |

### 3.3 核心框架

#### 3.3.1 OOP 框架 (rgw_oop.h)

```c
// 虚函数表模式
typedef struct rgw_object_vtable {
    void (*destroy)(rgw_object_t* self);
    const char* (*type_name)(void);
    bool (*is_a)(const rgw_object_t* self, const char* type_name);
} rgw_object_vtable_t;

typedef struct rgw_object {
    rgw_object_vtable_t* vtable;
    uint32_t ref_count;
    char type_id[32];
} rgw_object_t;
```

#### 3.3.2 错误处理 (rgw_errors.h)

```c
typedef enum rgw_error_code {
    RGW_OK = 0,
    RGW_ERR_INVALID_ARG = 1,
    RGW_ERR_OUT_OF_MEMORY = 2,
    // ... 其他错误码
} rgw_error_code_t;

// 错误传播宏
#define RGW_CHECK(expr) ...
#define RGW_CHECK_MSG(expr, msg) ...
#define RGW_CHECK_GOTO(expr, label) ...
```

### 3.4 文件命名规范

| 类型 | 命名规范 | 示例 |
|------|----------|------|
| 头文件 | `rgw_xxx.h` | `rgw_carray.h` |
| 源文件 | `rgw_xxx.c` | `rgw_carray.c` |
| 测试文件 | `test_xxx.c` | `test_carray.c` |

## 4. SAL C 接口架构

### 4.1 SAL 概述

SAL (Storage Abstraction Layer) 是 RGW 的核心抽象层，将上层协议处理与底层存储后端解耦。

### 4.2 SAL 架构图

```
┌─────────────────────────────────────────────────────────────┐
│                        RGW 上层                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ S3 协议处理 │  │Swift 协议处理│  │ REST 管理接口       │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    SAL C 抽象层 (本阶段目标)                 │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Driver (抽象存储驱动)                       │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐   │ │
│  │  │  User   │  │ Bucket  │  │ Object  │  │  其他    │   │ │
│  │  └─────────┘  └─────────┘  └─────────┘  └──────────┘   │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    存储后端实现                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  RADOS   │  │ DBStore  │  │  POSIX   │  │  其他    │  │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 4.3 SAL C 接口核心类型

#### 4.3.1 用户类型

```c
typedef struct rgw_sal_user_id {
    char* id;           // 用户 ID
    char* tenant;        // 租户
    char* ns;           // 命名空间
    uint32_t type;      // 用户类型
} rgw_sal_user_id_t;

typedef struct rgw_sal_user {
    const rgw_sal_user_vtable_t* vtable;
    void* impl;         // 驱动特定实现
    rgw_sal_driver_t* driver;
} rgw_sal_user_t;
```

#### 4.3.2 桶类型

```c
typedef struct rgw_sal_bucket_id {
    char* name;         // 桶名称
    char* tenant;       // 租户
    char* marker;        // 桶标记
    char* bucket_id;    // 桶 ID
} rgw_sal_bucket_id_t;

typedef struct rgw_sal_bucket {
    const rgw_sal_bucket_vtable_t* vtable;
    void* impl;
    rgw_sal_driver_t* driver;
} rgw_sal_bucket_t;
```

#### 4.3.3 对象类型

```c
typedef struct rgw_sal_obj_key {
    char* name;         // 对象名称
    char* instance;     // 版本 ID
    bool is_null;       // 是否为空
    bool is_current;    // 是否为当前版本
} rgw_sal_obj_key_t;

typedef struct rgw_sal_object {
    const rgw_sal_object_vtable_t* vtable;
    void* impl;
    rgw_sal_bucket_t* bucket;
} rgw_sal_object_t;
```

#### 4.3.4 驱动类型

```c
typedef struct rgw_sal_driver {
    const rgw_sal_driver_vtable_t* vtable;
    const rgw_sal_user_vtable_t* user_vtable;
    const rgw_sal_bucket_vtable_t* bucket_vtable;
    const rgw_sal_object_vtable_t* object_vtable;
    void* impl;
    char name[64];
} rgw_sal_driver_t;
```

### 4.4 虚函数表 (vtable) 模式

SAL C 使用虚函数表实现多态：

```c
typedef struct rgw_sal_driver_vtable {
    void (*destroy)(rgw_sal_driver_t* driver);
    int (*initialize)(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp);
    const char* (*get_name)(const rgw_sal_driver_t* driver);
    int (*get_cluster_id)(rgw_sal_driver_t* driver, char** cluster_id,
                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    // ... 更多函数指针
} rgw_sal_driver_vtable_t;
```

### 4.5 已实现的存储驱动

| 驱动 | 说明 | 源文件 | 状态 |
|------|------|--------|------|
| RADOS | Ceph 对象存储后端 | rgw_sal_rados.c/h | ✅ 已完成 |
| DBStore | SQLite 数据库后端 | rgw_sal_dbstore.c/h | ✅ 已完成 |
| POSIX | 文件系统后端 | rgw_sal_posix.c/h | ✅ 已完成 |
| D4N | Data for Nginx 缓存 | rgw_sal_d4n.c/h | ✅ 已完成 |
| Motr | Dell EMC Motr 后端 | rgw_sal_motr.c/h | ✅ 已完成 |
| DAOS | Intel DAOS 后端 | rgw_sal_daos.c/h | ✅ 已完成 |

## 5. 当前转换进度

### 5.1 阶段完成状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段 0 | 基础设施准备 | ✅ 已完成 |
| 阶段 1 | 核心数据类型转换 | ✅ 已完成 |
| 阶段 2 | 存储抽象层转换 (SAL) | ✅ 已完成 |
| 阶段 3 | REST 核心转换 | ⏳ 待开始 |
| 阶段 4 | 上层模块转换 | ⏳ 待开始 |
| 阶段 5 | 优化与收尾 | ⏳ 待开始 |

### 5.2 阶段 0 - 基础设施准备 ✅ 已完成

- ✅ c_common 容器库完善
  - ✅ 动态数组 (rgw_carray)
  - ✅ 字符串 (rgw_cstring)
  - ✅ 有序 Map (rgw_cmap)
  - ✅ 有序 Set (rgw_cset)
  - ✅ 双向链表 (rgw_clist)
  - ✅ 双端队列 (rgw_cdeque)
  - ✅ 栈 (rgw_stack)
  - ✅ 队列 (rgw_queue)
  - ✅ 优先队列 (rgw_cpriority_queue)
  - ✅ 哈希表 (rgw_chash_map)
  - ✅ 可选类型 (rgw_coptional)
- ✅ OOP 框架 (rgw_oop)
- ✅ 错误处理 (rgw_errors)
- ✅ 统一头文件 (rgw_ccommon)

### 5.3 阶段 1 - 核心数据类型转换 ✅ 已完成

- ✅ rgw_string - 字符串处理 C 实现
- ✅ rgw_xml - XML 解析器 C 实现
- ✅ rgw_b64 - Base64 编解码 C 实现
- ✅ rgw_hex - 十六进制编解码 C 实现

### 5.4 阶段 2 - 存储抽象层转换 (SAL) ✅ 已完成

- ✅ SAL C 接口设计 (vtable 模式)
- ✅ RADOS 驱动实现
- ✅ DBStore 驱动实现
- ✅ POSIX 驱动实现
- ✅ D4N 驱动实现
- ✅ Motr 驱动实现
- ✅ DAOS 驱动实现
- ✅ 完整测试套件 (35 个测试)

### 5.5 测试状态

#### 5.5.1 c_common 容器测试

| 测试 | 状态 |
|------|------|
| test_memory | ✅ PASSED |
| test_types | ✅ PASSED |
| test_cmap | ✅ PASSED |
| test_cset | ✅ PASSED |
| test_carray | ✅ PASSED |
| test_cstring | ✅ PASSED |
| test_coptional | ✅ PASSED |
| test_all | ✅ PASSED |
| test_ccontainer | ✅ PASSED |

#### 5.5.2 SAL C 驱动测试

| 测试 | 测试数 | 通过 |
|------|--------|------|
| RADOS 驱动测试 | 35 | ✅ 35/35 |
| DBStore 驱动测试 | 7 | ✅ 7/7 |
| 综合集成测试 | 9 | ✅ 9/9 |
| **总计** | **51** | **51 ✅** |

#### 5.5.3 AddressSanitizer 检测

- ✅ 内存泄漏检测
- ✅ 使用后释放检测
- ✅ 双重释放检测
- ✅ 缓冲区溢出检测

## 6. 后续计划

### 6.1 REST 核心转换 (待开始)

- rgw_op.cc - 操作处理器
- rgw_rest_*.cc - REST 框架
- S3/Swift 协议实现

### 6.2 上层模块转换 (待开始)

- 认证授权模块
- 服务层
- 前端网络层

---

## 7. 参考文档

| 文档 | 说明 |
|------|------|
| `implementation-plan.md` | 详细实施计划 |
| `progress.md` | 进度跟踪 |
| `cpp2c-document.md` | C++ 到 C 转换规范 |
| `tech-stack.md` | 技术栈说明 |
| `coding-standards.md` | 编程规范 |
| `sal_c/MAPPING_DETAIL.md` | SAL C++ 到 C 详细映射 |
| `sal_c/IMPLEMENTATION_SUMMARY.md` | SAL 实现总结 |

---

**文档版本**: 1.2
**最后更新**: 2026-03-19
**维护团队**: RGW C++ 到 C 转换项目组

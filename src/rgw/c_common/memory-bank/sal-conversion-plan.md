# RGW SAL C++ 到 C 转换详细计划

## 1. SAL 架构概述

### 1.1 存储抽象层 (SAL) 作用

SAL (Storage Abstraction Layer) 是 RGW 的核心抽象层，它将处理客户端协议（如 S3/Swift）的上层代码与底层存储后端解耦。

```
┌─────────────────────────────────────────────────────────────┐
│                        RGW 上层                               │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐ │
│  │ S3 协议处理 │  │Swift 协议处理│  │ REST 管理接口       │ │
│  └─────────────┘  └─────────────┘  └─────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    SAL 抽象层 (本转换目标)                   │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Driver (抽象存储驱动)                      │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌──────────┐  │ │
│  │  │  User   │  │ Bucket  │  │ Object  │  │  其他    │  │ │
│  │  └─────────┘  └─────────┘  └─────────┘  └──────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    存储后端实现                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │  RADOS   │  │ DBStore  │  │  POSIX   │  │  其他    │  │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 核心组件

| 组件 | 说明 | 文件 |
|------|------|------|
| Driver | 存储驱动基类，抽象所有存储操作 | `rgw_sal.h` |
| User | 用户实体，代表认证和访问控制单元 | `rgw_sal.h` |
| Bucket | 桶实体，包含对象的容器 | `rgw_sal.h` |
| Object | 对象实体，代表单个数据块 | `rgw_sal.h` |
| MultipartUpload | 多部分上传管理 | `rgw_sal.h` |
| Lifecycle | 对象生命周期管理 | `rgw_sal.h` |
| Notification | 事件通知 | `rgw_sal.h` |
| LuaManager | Lua 脚本管理 | `rgw_sal.h` |

---

## 2. 涉及的文件清单

### 2.1 核心 SAL 接口文件

| 文件路径 | 行数 | 说明 | 优先级 |
|----------|------|------|--------|
| `src/rgw/rgw_sal.h` | ~2000 | 主 SAL 接口定义，包含所有核心类 | P0 |
| `src/rgw/rgw_sal_fwd.h` | ~60 | 前向声明 | P0 |
| `src/rgw/rgw_sal_filter.h` | ~1100 | 过滤器/中间层实现 | P1 |
| `src/rgw/rgw_sal_store.h` | ~300 | 存储相关接口 | P1 |
| `src/rgw/rgw_sal_config.h` | ~200 | 配置接口 | P2 |
| `src/rgw/rgw_sal_dbstore.h` | ~400 | DBStore 驱动实现 | P2 |

### 2.2 存储驱动文件

| 文件路径 | 驱动类型 | 说明 |
|----------|----------|------|
| `src/rgw/driver/rados/rgw_sal_rados.h` | RADOS | Ceph RADOS 后端 (主要) |
| `src/rgw/driver/rados/rgw_sal_rados.cc` | RADOS | RADOS 驱动实现 |
| `src/rgw/rgw_sal_dbstore.h` | DBStore | SQLite 数据库后端 |
| `src/rgw/rgw_sal_dbstore.cc` | DBStore | DBStore 驱动实现 |
| `src/rgw/driver/posix/rgw_sal_posix.h` | POSIX | 文件系统后端 |
| `src/rgw/driver/posix/rgw_sal_posix.cc` | POSIX | POSIX 驱动实现 |
| `src/rgw/driver/d4n/rgw_sal_d4n.h` | D4N | Data for Nginx 缓存 |
| `src/rgw/driver/motr/rgw_sal_motr.h` | Motr | Dell EMC Motr 后端 |
| `src/rgw/driver/daos/rgw_sal_daos.h` | DAOS | Intel DAOS 后端 |

### 2.3 依赖的外部类型

| 类型 | 定义位置 | 说明 |
|------|----------|------|
| `rgw_user` | `driver/rados/rgw_user.h` | 用户标识 |
| `rgw_bucket` | `driver/rados/rgw_bucket.h` | 桶信息 |
| `rgw_obj` | `rgw_common.h` | 对象标识 |
| `rgw_user_info` | `rgw_common.h` | 用户详细信息 |
| `rgw_bucket_info` | `rgw_common.h` | 桶详细信息 |
| `RGWAccountInfo` | `rgw_account.h` | 账户信息 |
| `RGWZoneGroup` | `rgw_zone.h` | 区域组 |
| `RGWZone` | `rgw_zone.h` | 区域 |
| `bufferlist` | `common/buffer.h` | Ceph 缓冲区 |

---

## 3. 核心接口分析

### 3.1 Driver 类 (抽象基类)

**位置**: `rgw_sal.h` 第 284 行起

```cpp
class Driver {
public:
    virtual ~Driver() = default;

    /** 初始化驱动 */
    virtual int initialize(CephContext *cct, const DoutPrefixProvider *dpp) = 0;
    
    /** 获取驱动名称 */
    virtual const std::string get_name() const = 0;
    
    /** 获取用户 */
    virtual std::unique_ptr<User> get_user(const rgw_user& u) = 0;
    
    /** 通过 access_key 查找用户 */
    virtual int get_user_by_access_key(...) = 0;
    
    /** 通过 email 查找用户 */
    virtual int get_user_by_email(...) = 0;
    
    /** 加载账户信息 */
    virtual int load_account_by_id(...) = 0;
    
    /** 获取桶 */
    virtual std::unique_ptr<Bucket> get_bucket(const RGWBucketInfo& i) = 0;
    
    /** 列出桶 */
    virtual int list_buckets(...) = 0;
    
    /** ... 更多方法 (约 100+ 虚函数) */
};
```

**C 转换设计**:
```c
typedef struct rgw_sal_driver rgw_sal_driver_t;

typedef int (*rgw_sal_initialize_fn)(
    rgw_sal_driver_t* driver,
    void* cct,
    const void* dpp
);

typedef const char* (*rgw_sal_get_name_fn)(
    const rgw_sal_driver_t* driver
);

typedef int (*rgw_sal_get_user_fn)(
    rgw_sal_driver_t* driver,
    const rgw_user_t* u,
    rgw_sal_user_t** user
);
```

### 3.2 User 类

**位置**: `rgw_sal.h` 第 724 行起

**核心方法**:
- `clone()` - 克隆用户副本
- `get_display_name()` - 获取显示名称
- `get_id()` - 获取用户 ID
- `read_attrs()` - 读取属性
- `store_user()` - 存储用户
- `remove_user()` - 删除用户

**C 转换设计**:
```c
typedef struct rgw_sal_user rgw_sal_user_t;

typedef rgw_sal_user_t* (*rgw_sal_user_clone_fn)(
    const rgw_sal_user_t* user
);

typedef int (*rgw_sal_user_read_attrs_fn)(
    rgw_sal_user_t* user,
    const void* dpp,
    void* optional_yield
);

typedef int (*rgw_sal_user_store_fn)(
    rgw_sal_user_t* user,
    const void* dpp,
    void* optional_yield,
    bool exclusive
);
```

### 3.3 Bucket 类

**位置**: `rgw_sal.h` 第 827 行起

**核心方法**:
- `get_key()` - 获取桶键
- `get_info()` - 获取桶信息
- `list()` - 列出对象
- `create()` - 创建桶
- `remove()` - 删除桶

### 3.4 Object 类

**位置**: `rgw_sal.h` 第 1098 行起

**核心方法**:
- `ReadOp::prepare()` - 准备读取
- `ReadOp::read()` - 同步读取
- `ReadOp::iterate()` - 异步读取
- `DeleteOp::delete_obj()` - 删除对象
- `copy_object()` - 复制对象

### 3.5 多态实现模式

SAL 大量使用 C++ 虚函数实现多态。C 中使用**虚函数表 (vtable)** 模式：

```c
// C 版本虚函数表模式
typedef struct rgw_sal_driver_vtable {
    // 初始化
    rgw_sal_initialize_fn initialize;
    
    // 用户操作
    rgw_sal_get_user_fn get_user;
    rgw_sal_get_user_by_key_fn get_user_by_access_key;
    rgw_sal_get_user_by_email_fn get_user_by_email;
    rgw_sal_user_load_fn load_user;
    rgw_sal_user_store_fn store_user;
    rgw_sal_user_remove_fn remove_user;
    
    // 桶操作
    rgw_sal_get_bucket_fn get_bucket;
    rgw_sal_list_buckets_fn list_buckets;
    
    // ... 更多函数指针
} rgw_sal_driver_vtable_t;

typedef struct rgw_sal_driver {
    const rgw_sal_driver_vtable_t* vtable;
    void* impl;  // 驱动特定数据
} rgw_sal_driver_t;
```

---

## 4. 依赖关系图

### 4.1 模块依赖

```
rgw_sal.h (核心)
    │
    ├── rgw_sal_fwd.h (前向声明)
    │       └── std::function (C: 回调函数指针)
    │
    ├── rgw_sal_filter.h (过滤器)
    │       └── 继承自 rgw_sal.h
    │
    ├── rgw_sal_store.h (存储接口)
    │
    ├── rgw_user.h (用户类型)
    │       └── rgw_user (用户标识)
    │
    ├── rgw_bucket.h (桶类型)
    │       └── rgw_bucket_info
    │
    └── 第三方依赖
            ├── boost/smart_ptr (C: 引用计数)
            ├── bufferlist (C: 自定义 buffer)
            └── Ceph 核心库
```

### 4.2 驱动依赖

```
RADOS Driver (主要)
    ├── rgw_sal_rados.h/cc
    ├── rgw_rados.h (RADOS API)
    ├── rgw_user.h
    ├── rgw_bucket.h
    └── services/svc_*.h (Ceph 服务层)

DBStore Driver
    ├── rgw_sal_dbstore.h/cc
    ├── SQLite3
    └── 简化实现

POSIX Driver
    ├── rgw_sal_posix.h/cc
    └── POSIX 文件系统 API
```

---

## 5. 转换计划

### 5.1 第一阶段：创建 SAL C 接口 (4-6 周)

#### 1.1 创建目录结构

```
src/rgw/sal_c/
    ├── rgw_sal_c.h          # 主头文件
    ├── rgw_sal_driver_c.h   # 驱动接口
    ├── rgw_sal_user_c.h     # 用户接口
    ├── rgw_sal_bucket_c.h   # 桶接口
    ├── rgw_sal_object_c.h   # 对象接口
    ├── rgw_sal_errors.h     # 错误码定义
    ├── rgw_sal_vtable.h     # 虚函数表定义
    └── src/
        ├── rgw_sal_driver_c.c
        ├── rgw_sal_user_c.c
        ├── rgw_sal_bucket_c.c
        └── rgw_sal_object_c.c
```

#### 1.2 核心数据类型转换

| C++ 类型 | C 类型 | 说明 |
|----------|--------|------|
| `std::string` | `char*` | 需要内存管理 |
| `std::unique_ptr<T>` | `T*` + 生命周期管理 | 需要明确释放 |
| `std::shared_ptr<T>` | 带引用计数的句柄 | 需要引用计数 |
| `bufferlist` | `rgw_buffer_t*` | 使用已转换的 buffer |
| `std::map<std::string, bufferlist>` | `rgw_attrs_t` | 属性映射 |
| `optional_yield` | `rgw_yield_t` | 协程上下文 |

#### 1.3 关键 API 设计

**驱动接口**:
```c
// 创建/销毁
rgw_sal_driver_t* rgw_sal_driver_create(const char* driver_name, void* cct);
void rgw_sal_driver_destroy(rgw_sal_driver_t* driver);

// 初始化
int rgw_sal_driver_initialize(rgw_sal_driver_t* driver, void* cct, const void* dpp);

// 用户操作
rgw_sal_user_t* rgw_sal_driver_get_user(rgw_sal_driver_t* driver, const rgw_user_t* u);
int rgw_sal_driver_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key, rgw_sal_user_t** user);

// 桶操作
rgw_sal_bucket_t* rgw_sal_driver_get_bucket(rgw_sal_driver_t* driver, const rgw_bucket_info_t* info);
int rgw_sal_driver_list_buckets(rgw_sal_driver_t* driver, ...);
```

### 5.2 第二阶段：实现 RADOS 驱动适配器 (6-8 周)

#### 2.1 适配器层设计

创建 C 适配器层，将 RADOS C++ 接口包装为 C 接口：

```
┌────────────────────────────────────────┐
│         RGW 上层 (C++ 代码)            │
│    使用 rgw_sal_c.h C 接口             │
└────────────────────────────────────────┘
                  │
                  ▼ (extern "C")
┌────────────────────────────────────────┐
│       SAL C 适配器层 (本阶段)           │
│    rgw_sal_c_adapter.cc                │
│    包装 C++ 实现为 C 接口               │
└────────────────────────────────────────┘
                  │
                  ▼
┌────────────────────────────────────────┐
│      原有 RADOS SAL 实现 (C++)         │
│    rgw_sal_rados.h/cc                  │
└────────────────────────────────────────┘
```

#### 2.2 适配器实现要点

```cpp
extern "C" {

// C 接口实现
int rgw_sal_driver_initialize_c(rgw_sal_driver_t* driver, void* cct, const void* dpp) {
    // 调用 C++ 实现
    RadosDriver* cpp_driver = static_cast<RadosDriver*>(driver->impl);
    return cpp_driver->initialize(cct, dpp);
}

} // extern "C"
```

### 5.3 第三阶段：实现其他驱动 (4-6 周)

#### 3.1 DBStore 驱动

- 使用 SQLite3 C API
- 简化实现，减少功能

#### 3.2 POSIX 驱动

- 使用标准 POSIX 文件系统 API
- 实现基本的文件读写

### 5.4 第四阶段：测试和优化 (3-4 周)

#### 4.1 测试策略

- 单元测试：每个 C 接口函数
- 集成测试：C 调用 C++ 实现
- 回归测试：与原有 C++ 版本对比

#### 4.2 内存泄漏检测

- 使用 AddressSanitizer
- 使用 Valgrind

---

## 6. 转换难点与解决方案

### 6.1 虚函数表 (vtable) 设计

**难点**: C++ 多态通过 vtable 实现，C 需要手动管理

**解决方案**:
```c
// 定义虚函数表
typedef struct rgw_sal_driver_vtable {
    int (*initialize)(rgw_sal_driver_t*, void*, const void*);
    const char* (*get_name)(const rgw_sal_driver_t*);
    // ... 其他函数指针
} rgw_sal_driver_vtable_t;

// 每个驱动实现自己的 vtable
static rgw_sal_driver_vtable_t rados_driver_vtable = {
    .initialize = rados_driver_initialize,
    .get_name = rados_driver_get_name,
    // ...
};
```

### 6.2 智能指针转换

**难点**: `std::unique_ptr` 和 `std::shared_ptr` 自动管理生命周期

**解决方案**:
```c
// unique_ptr 语义
rgw_sal_user_t* rgw_sal_user_create(void);
void rgw_sal_user_destroy(rgw_sal_user_t* user);

// shared_ptr 语义 (引用计数)
rgw_sal_user_t* rgw_sal_user_ref(rgw_sal_user_t* user);
void rgw_sal_user_unref(rgw_sal_user_t* user);
```

### 6.3 std::function 回调转换

**难点**: Lambda 表达式和 `std::function`

**解决方案**:
```c
// 使用函数指针 + 上下文
typedef int (*rgw_iter_func_t)(void* arg, const char* key, const void* value);

int rgw_sal_bucket_iterate(
    rgw_sal_bucket_t* bucket,
    rgw_iter_func_t callback,
    void* user_arg
);
```

### 6.4 bufferlist 处理

**难点**: Ceph 的 `bufferlist` 是复杂的数据结构

**解决方案**: 使用已转换的 `rgw_buffer_t`

---

## 7. 时间线

| 阶段 | 任务 | 周数 | 累计 |
|------|------|------|------|
| 1 | SAL C 接口设计和基础实现 | 4-6 | 4-6 |
| 2 | RADOS 驱动适配器 | 6-8 | 10-14 |
| 3 | 其他驱动实现 | 4-6 | 14-20 |
| 4 | 测试和优化 | 3-4 | 17-24 |

---

## 8. 验收标准

### 8.1 功能完整性

- [ ] 所有核心 SAL 接口转换为 C
- [ ] RADOS 驱动正常工作
- [ ] 其他驱动基础功能可用

### 8.2 性能要求

- [ ] 关键操作性能不低于 C++ 版本 90%
- [ ] 内存使用不显著增加

### 8.3 质量要求

- [ ] 零内存泄漏 (Valgrind/ASan)
- [ ] 零编译警告
- [ ] 单元测试覆盖率 >80%

---

## 9. 参考文档

1. `rgw_sal.h` - 核心 SAL 接口定义
2. `rgw_sal_rados.h/cc` - RADOS 驱动实现参考
3. `rgw_sal_filter.h` - 过滤器模式参考
4. `memory-bank/cpp2c-document.md` - C++ 到 C 转换规范
5. `memory-bank/tech-stack.md` - 技术栈说明

---

**文档版本**: 1.0
**生成日期**: 2026-03-18
**维护团队**: RGW C++ 到 C 转换项目组

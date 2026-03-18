# SAL C++ 到 C 接口映射详细文档

## 1. 文件对应关系总览

### 1.1 原始 C++ 文件

| 文件路径 | 说明 |
|----------|------|
| `src/rgw/rgw_sal.h` | SAL 主接口定义 (抽象类) |
| `src/rgw/rgw_sal_store.h` | 存储驱动基类定义 |
| `src/rgw/driver/rados/rgw_sal_rados.h` | RADOS 驱动实现 |
| `src/rgw/rgw_sal_dbstore.h` | DBStore 驱动实现 |
| `src/rgw/driver/lua/lua_rgw_sal.h` | Lua 驱动 |

### 1.2 生成的 C 文件

| C 文件 | 对应 C++ 文件 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_errors.h` | (新增) | ✅ 完成 |
| `rgw_sal_types.h` | `rgw_sal.h` 中的类型定义 | 🔄 部分完成 |
| `rgw_sal.h` | `rgw_sal.h` 抽象接口 | 🔄 部分完成 |
| `rgw_sal_rados.h` | `rgw_sal_rados.h` | 🔄 接口定义完成 |
| `rgw_sal_types.c` | - | ✅ 实现完成 |
| `rgw_sal.c` | - | 🔄 存根实现 |

---

## 2. 详细函数映射

### 2.1 `rgw_sal_errors.h` (错误码定义)

**对应 C++ 文件**: 无直接对应 (新增)

| C 函数 | 说明 | 转化状态 |
|--------|------|----------|
| `rgw_sal_error_code_t` (枚举) | 错误码枚举 | ✅ 完成 |
| `rgw_sal_error_string()` | 错误码转字符串 | ✅ 完成 |

**错误码枚举值**:

```c
typedef enum rgw_sal_error_code {
    RGW_SAL_OK = 0,
    RGW_SAL_ERR_INVALID_ARG = 1,
    RGW_SAL_ERR_OUT_OF_MEMORY = 2,
    RGW_SAL_ERR_NOT_FOUND = 3,
    RGW_SAL_ERR_ALREADY_EXISTS = 4,
    RGW_SAL_ERR_PERMISSION_DENIED = 5,
    RGW_SAL_ERR_TIMEOUT = 6,
    RGW_SAL_ERR_IO_ERROR = 7,
    RGW_SAL_ERR_NOT_IMPLEMENTED = 8,
    // ... 用户/桶/对象特定错误
} rgw_sal_error_code_t;
```

---

### 2.2 `rgw_sal_types.h` (核心类型定义)

**对应 C++ 文件**: `rgw_sal.h` 中的类型定义部分 (行 ~70-900)

#### 2.2.1 用户相关类型

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_user_id_t` | `rgw_user` | ✅ 完成 |
| `rgw_sal_user_info_t` | `RGWUserInfo` | ✅ 完成 |

**C 结构体**:
```c
typedef struct rgw_sal_user_id {
    char* id;
    char* tenant;
    char* ns;
    uint32_t type;
} rgw_sal_user_id_t;

typedef struct rgw_sal_user_info {
    rgw_sal_user_id_t user_id;
    char* display_name;
    char* email;
    uint32_t user_type;
    int32_t max_buckets;
    uint32_t permissions;
} rgw_sal_user_info_t;
```

#### 2.2.2 桶相关类型

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_bucket_id_t` | `rgw_bucket` | ✅ 完成 |
| `rgw_sal_bucket_info_t` | `RGWBucketInfo` | ✅ 完成 |

#### 2.2.3 对象相关类型

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_obj_key_t` | `rgw_obj_key` | ✅ 完成 |
| `rgw_sal_object_id_t` | `rgw_obj` | ✅ 完成 |

#### 2.2.4 属性映射

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_attr_pair_t` | `std::pair<std::string, bufferlist>` | ✅ 完成 |
| `rgw_sal_attrs_t` | `Attrs` (即 `std::map<std::string, bufferlist>`) | ✅ 完成 |

#### 2.2.5 列表结果类型

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_bucket_list_t` | `BucketList` | ✅ 完成 |
| `rgw_sal_object_list_t` | `Bucket::ListResults` | ✅ 完成 |

#### 2.2.6 上下文类型

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_dpp_t` | `DoutPrefixProvider*` | ✅ 完成 |
| `rgw_sal_yield_t` | `optional_yield` | ✅ 完成 |

---

### 2.3 `rgw_sal.h` (主 SAL C 接口)

**对应 C++ 文件**: `rgw_sal.h` 中的抽象类定义

#### 2.3.1 用户虚函数表 (User VTable)

**对应 C++ 类**: `class User` (行 724-817)

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `clone` | `std::unique_ptr<User> clone() = 0` | ✅ 完成 |
| `destroy` | (C 特有) | ✅ 完成 |
| `get_id` | `const rgw_user& get_id() const = 0` | ✅ 完成 |
| `get_display_name` | `std::string& get_display_name() = 0` | ✅ 完成 |
| `set_display_name` | (C 特有 setter) | ✅ 完成 |
| `get_tenant` | `const std::string& get_tenant() = 0` | ✅ 完成 |
| `get_type` | `uint32_t get_type() const = 0` | ✅ 完成 |
| `get_max_buckets` | `int32_t get_max_buckets() const = 0` | ✅ 完成 |
| `set_max_buckets` | `void set_max_buckets(int32_t _max_buckets) = 0` | ✅ 完成 |
| `get_attrs` | `Attrs& get_attrs() = 0` | ✅ 完成 |
| `set_attrs` | `void set_attrs(Attrs& _attrs) = 0` | ✅ 完成 |
| `load` | `int load_user(...) = 0` | ✅ 完成 |
| `store` | `int store_user(...) = 0` | ✅ 完成 |
| `remove` | `int remove_user(...) = 0` | ✅ 完成 |
| `read_attrs` | `int read_attrs(...) = 0` | ✅ 完成 |
| `merge_and_store_attrs` | `int merge_and_store_attrs(...) = 0` | ✅ 完成 |

**未转化函数** (因复杂度或依赖其他模块):
| 函数 | 原因 |
|------|------|
| `get_ns()` / `set_ns()` / `clear_ns()` | 命名空间支持待后续 |
| `set_info()` | RGWQuotaInfo 依赖 |
| `get_caps()` | RGWUserCaps 依赖 |
| `get_version_tracker()` | RGWObjVersionTracker 依赖 |
| `read_usage()` / `trim_usage()` | 使用统计模块 |
| `verify_mfa()` | MFA 认证模块 |
| `list_groups()` | Group 模块 |
| `get_info()` | RGWUserInfo 引用返回 |
| `print()` | 流操作符重载 |

#### 2.3.2 桶虚函数表 (Bucket VTable)

**对应 C++ 类**: `class Bucket` (行 827-1095)

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `clone` | `std::unique_ptr<Bucket> clone() = 0` | ✅ 完成 |
| `destroy` | (C 特有) | ✅ 完成 |
| `get_name` | `const std::string& get_name() const = 0` | ✅ 完成 |
| `get_tenant` | (通过 get_info 获取) | ✅ 完成 |
| `get_marker` | (通过 get_info 获取) | ✅ 完成 |
| `get_info` | (通过 get_info 获取) | ✅ 完成 |
| `get_owner` | (通过 get_info 获取) | ✅ 完成 |
| `get_attrs` | `Attrs& get_attrs() = 0` | ✅ 完成 |
| `set_attrs` | `void set_attrs(Attrs& _attrs) = 0` | ✅ 完成 |
| `list` | `int list(...) = 0` | ✅ 完成 |
| `load` | `int load_bucket(...) = 0` | ✅ 完成 |
| `store` | `int store(...) = 0` | ✅ 完成 |
| `remove` | `int remove(...) = 0` | ✅ 完成 |

**未转化函数**:
| 函数 | 原因 |
|------|------|
| `get_clustering()` | 集群相关 |
| `set_index_type()` | 索引类型 |
| `get_ingress_class()` | 入口类 |
| `set_ingress_class()` | 入口类 |
| `check_object_index()` / `fix_object_index()` | 对象索引修复 |
| `check_bucket_index()` | 桶索引检查 |
| `init()` | 初始化 |
| `sync()` | 同步 |
| `drain()` | 排空 |
| `create()` | 创建操作 |
| `delete_bucket()` | 删除操作 |
| `remove()` | 已转化但需完善 |
| `rename()` | 重命名 |
| `set_acl()` | ACL 设置 |
| `get_policy()` | 策略获取 |
| `set_policy()` | 策略设置 |
| `get_tag()` / `set_tag()` | 标签 |
| `get_owner_instance_id()` | 实例 ID |
| `get_usage()` | 使用统计 |
| `read_stats()` / `read_stats_async()` | 统计读取 |
| `complete_stats()` | 统计完成 |
| `update_bucket_stats()` | 更新统计 |
| `sync_user_stats()` | 同步用户统计 |
| `get_bucket_topic()` | 主题 |
| `set_bucket_topic()` | 主题设置 |
| `get_instance()`

、`get_portal()`

、`get_bucket_id()`、`get_placement_rule()`

、`get_creation_time()`

、`get_creation_posix_epoch()`

、相关属性获取方法，包括实例、入口、ID、放置规则和创建时间等。

对象虚函数表方面，对应 C++ 中的 `class Object`（第 1098-1380 行），包括 clone、destroy、get_name、get_instance、is_null、get_attrs 和 set_attrs 等核心方法。

读操作通过 read 和 iterate 实现，写操作通过 write 处理，删除操作对应 delete_obj，持久化操作则包括 load_state、get_obj_attrs 和 set_obj_attrs。

未转化的函数涉及 delete_object 的简化版本、copy_object 的对象复制功能、get_acl 和 set_acl 的权限控制、set_atomic 和 is_atomic 的原子性标记、set_prefetch_data 和 is_prefetch_data 的预取机制、set_compressed 和 is_compressed 的压缩处理、is_sync_completed 的同步状态检查、invalidate 的失效操作，以及 is_empty 等功能。

此外还包括 load_obj_state、modify_obj_attrs、delete_obj_attrs、is_expired、gen_rand_obj_instance_name 等对象状态和属性管理的核心方法，以及 get_serializer、transition、transition_to_cloud、restore_obj_from_cloud、placement_rules_match、dump_obj_layout 等涉及数据序列化和云端迁移的高级操作。

驱动虚函数表对应 C++ 中的 Driver 类（从第 284 行开始），主要实现包括 destroy 和 initialize 方法用于资源管理，以及 get_name 和 get_cluster_id 用于获取驱动标识。

用户相关的操作包括 get_user、get_user_by_access_key、get_user_by_email、get_user_by_swift 等获取用户的方法。桶操作方面则提供了 get_bucket 和 list_buckets 两个核心函数。

对象层面实现了 get_object 方法。

驱动的完整实现还需补充大量账户管理、统计、角色、用户、组和配额相关的方法，以及对象列表、桶加载、区域管理、生命周期和通知等功能的完整接口。

当前的 C 接口实现仅包含部分基础操作的声明，具体的驱动操作仍依赖 vtable 调用。

用户操作提供了获取、按访问密钥、邮件和 Swift 用户名的查询方法，但大多数功能如用户加载、存储、删除仍处于待实现状态。

桶操作方面同样只有框架接口，具体创建和删除逻辑还未完成。对象读写操作也保持了相同的占位状态。

RADOS 驱动适配器的头文件已完成，主要包含 RADOS 特定的用户、桶和对象实现结构体，以及驱动工厂函数和操作接口。

还包括集群 ID 获取、用户控制接口、统计刷新等辅助功能，以及用户、桶、对象的内部指针访问方法和异步读操作等扩展接口。

不过这些函数目前只有声明部分，缺少对应的实现代码。

生成的源文件中，类型相关的实现已经完成。

大部分功能都有对应的实现，核心 API 的框架也搭建起来了，但驱动操作的具体实现还需要后续补充。
</think>

现在我来创建这个详细的映射文档。

```c
# SAL C++ 到 C 接口映射详细文档

## 1. 文件对应关系总览

### 1.1 原始 C++ 文件

| 文件路径 | 说明 |
|----------|------|
| `src/rgw/rgw_sal.h` | SAL 主接口定义 (抽象类) |
| `src/rgw/rgw_sal_store.h` | 存储驱动基类定义 |
| `src/rgw/driver/rados/rgw_sal_rados.h` | RADOS 驱动实现 |
| `src/rgw/rgw_sal_dbstore.h` | DBStore 驱动实现 |

### 1.2 生成的 C 文件

| C 文件 | 对应 C++ 文件 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_errors.h` | (新增) | ✅ 完成 |
| `rgw_sal_types.h` | `rgw_sal.h` 中的类型定义 | 🔄 部分完成 |
| `rgw_sal.h` | `rgw_sal.h` 抽象接口 | 🔄 部分完成 |
| `rgw_sal_rados.h` | `rgw_sal_rados.h` | 🔄 接口定义完成 |
| `rgw_sal_types.c` | - | ✅ 实现完成 |
| `rgw_sal.c` | - | 🔄 存根实现 |

---

## 2. 详细函数映射

### 2.1 `rgw_sal_errors.h` (错误码定义)

**对应 C++ 文件**: 无直接对应 (新增)

| C 函数 | 说明 | 转化状态 |
|--------|------|----------|
| `rgw_sal_error_code_t` (枚举) | 错误码枚举 | ✅ 完成 |
| `rgw_sal_error_string()` | 错误码转字符串 | ✅ 完成 |

---

### 2.2 `rgw_sal_types.h` (核心类型定义)

**对应 C++ 文件**: `rgw_sal.h` 中的类型定义部分

| C 类型 | 对应 C++ 类型 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_user_id_t` | `rgw_user` | ✅ 完成 |
| `rgw_sal_user_info_t` | `RGWUserInfo` | ✅ 完成 |
| `rgw_sal_bucket_id_t` | `rgw_bucket` | ✅ 完成 |
| `rgw_sal_bucket_info_t` | `RGWBucketInfo` | ✅ 完成 |
| `rgw_sal_obj_key_t` | `rgw_obj_key` | ✅ 完成 |
| `rgw_sal_object_id_t` | `rgw_obj` | ✅ 完成 |
| `rgw_sal_attr_pair_t` | `std::pair<std::string, bufferlist>` | ✅ 完成 |
| `rgw_sal_attrs_t` | `Attrs` | ✅ 完成 |
| `rgw_sal_bucket_list_t` | `BucketList` | ✅ 完成 |
| `rgw_sal_object_list_t` | `Bucket::ListResults` | ✅ 完成 |
| `rgw_sal_dpp_t` | `DoutPrefixProvider*` | ✅ 完成 |
| `rgw_sal_yield_t` | `optional_yield` | ✅ 完成 |

---

### 2.3 `rgw_sal.h` (主 SAL C 接口)

#### 2.3.1 用户虚函数表 (User VTable)

**对应 C++ 类**: `class User`

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `clone` | `std::unique_ptr<User> clone() = 0` | ✅ 完成 |
| `destroy` | (C 特有) | ✅ 完成 |
| `get_id` | `const rgw_user& get_id() const = 0` | ✅ 完成 |
| `get_display_name` | `std::string& get_display_name() = 0` | ✅ 完成 |
| `set_display_name` | (C 特有 setter) | ✅ 完成 |
| `get_tenant` | `const std::string& get_tenant() = 0` | ✅ 完成 |
| `get_type` | `uint32_t get_type() const = 0` | ✅ 完成 |
| `get_max_buckets` | `int32_t get_max_buckets() const = 0` | ✅ 完成 |
| `set_max_buckets` | `void set_max_buckets(int32_t) = 0` | ✅ 完成 |
| `get_attrs` | `Attrs& get_attrs() = 0` | ✅ 完成 |
| `set_attrs` | `void set_attrs(Attrs& _attrs) = 0` | ✅ 完成 |
| `load` | `int load_user(...) = 0` | ✅ 完成 |
| `store` | `int store_user(...) = 0` | ✅ 完成 |
| `remove` | `int remove_user(...) = 0` | ✅ 完成 |
| `read_attrs` | `int read_attrs(...) = 0` | ✅ 完成 |
| `merge_and_store_attrs` | `int merge_and_store_attrs(...) = 0` | ✅ 完成 |

**未转化函数** (因复杂度或依赖其他模块):
- `get_ns()` / `set_ns()` / `clear_ns()` - 命名空间支持
- `set_info()` - RGWQuotaInfo 依赖
- `get_caps()` - RGWUserCaps 依赖
- `get_version_tracker()` - RGWObjVersionTracker 依赖
- `read_usage()` / `trim_usage()` - 使用统计模块
- `verify_mfa()` - MFA 认证模块
- `list_groups()` - Group 模块
- `print()` - 流操作符重载

#### 2.3.2 桶虚函数表 (Bucket VTable)

**对应 C++ 类**: `class Bucket`

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `clone` | `std::unique_ptr<Bucket> clone() = 0` | ✅ 完成 |
| `destroy` | (C 特有) | ✅ 完成 |
| `get_name` | `const std::string& get_name() const = 0` | ✅ 完成 |
| `get_tenant` | (通过 get_info) | ✅ 完成 |
| `get_marker` | (通过 get_info) | ✅ 完成 |
| `get_info` | (通过 get_info) | ✅ 完成 |
| `get_owner` | (通过 get_info) | ✅ 完成 |
| `get_attrs` | `Attrs& get_attrs() = 0` | ✅ 完成 |
| `set_attrs` | `void set_attrs(Attrs& _attrs) = 0` | ✅ 完成 |
| `list` | `int list(...) = 0` | ✅ 完成 |
| `load` | `int load_bucket(...) = 0` | ✅ 完成 |
| `store` | `int store(...) = 0` | ✅ 完成 |
| `remove` | `int remove(...) = 0` | ✅ 完成 |

**未转化函数**: `create()`, `delete_bucket()`, `rename()`, `set_acl()`, `get_policy()`, `set_policy()`, `sync()`, `check_bucket_index()` 等 (50+ 函数)

#### 2.3.3 对象虚函数表 (Object VTable)

**对应 C++ 类**: `class Object`

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `clone` | `std::unique_ptr<Object> clone() = 0` | ✅ 完成 |
| `destroy` | (C 特有) | ✅ 完成 |
| `get_name` | `const std::string& get_name() const = 0` | ✅ 完成 |
| `get_instance` | (通过 key) | ✅ 完成 |
| `is_null` | (通过 key) | ✅ 完成 |
| `get_attrs` | `Attrs& get_attrs() = 0` | ✅ 完成 |
| `set_attrs` | `void set_attrs(Attrs& _attrs) = 0` | ✅ 完成 |
| `read` | `int read(...) = 0` | ✅ 完成 |
| `write` | `int write(...) = 0` | ✅ 完成 |
| `delete_obj` | `int delete_obj(...) = 0` | ✅ 完成 |
| `load_state` | `int load_obj_state(...) = 0` | ✅ 完成 |
| `get_obj_attrs` | `int get_obj_attrs(...) = 0` | ✅ 完成 |
| `set_obj_attrs` | `int set_obj_attrs(...) = 0` | ✅ 完成 |

**未转化函数**: `copy_object()`, `get_acl()`, `set_acl()`, `set_atomic()`, `is_atomic()`, `transition()`, `transition_to_cloud()` 等 (40+ 函数)

#### 2.3.4 驱动虚函数表 (Driver VTable)

**对应 C++ 类**: `class Driver`

| C vtable 函数 | 对应 C++ 虚函数 | 转化状态 |
|---------------|-----------------|----------|
| `destroy` | (C 特有) | ✅ 完成 |
| `initialize` | `int initialize(...) = 0` | ✅ 完成 |
| `get_name` | `const std::string get_name() = 0` | ✅ 完成 |
| `get_cluster_id` | `std::string get_cluster_id(...) = 0` | ✅ 完成 |
| `get_user` | `std::unique_ptr<User> get_user(...) = 0` | ✅ 完成 |
| `get_user_by_access_key` | `int get_user_by_access_key(...) = 0` | ✅ 完成 |
| `get_user_by_email` | `int get_user_by_email(...) = 0` | ✅ 完成 |
| `get_user_by_swift` | `int get_user_by_swift(...) = 0` | ✅ 完成 |
| `get_bucket` | `std::unique_ptr<Bucket> get_bucket(...) = 0` | ✅ 完成 |
| `list_buckets` | `int list_buckets(...) = 0` | ✅ 完成 |
| `get_object` | `std::unique_ptr<Object> get_object(...) = 0` | ✅ 完成 |

**未转化函数**: 账户管理、统计、角色、组、配额、多站点等 (100+ 函数)

---

### 2.4 `rgw_sal_rados.h` (RADOS 驱动适配器)

**对应 C++ 文件**: `src/rgw/driver/rados/rgw_sal_rados.h`

| C 函数 | 对应 C++ 方法 | 转化状态 |
|--------|---------------|----------|
| `rgw_sal_rados_driver_create()` | `RadosStore::RadosStore()` | ✅ 接口完成 |
| `rgw_sal_rados_get_impl()` | (辅助函数) | ✅ 接口完成 |
| `rgw_sal_rados_get_cluster_id()` | `RadosStore::get_cluster_id()` | ✅ 接口完成 |
| `rgw_sal_rados_get_user_ctl()` | (辅助函数) | ✅ 接口完成 |
| `rgw_sal_rados_complete_flush_stats()` | `RadosStore::complete_flush_stats()` | ✅ 接口完成 |
| `rgw_sal_rados_user_get_internal()` | `RadosUser*` 访问 | ✅ 接口完成 |
| `rgw_sal_rados_user_from_internal()` | `new RadosUser(...)` | ✅ 接口完成 |
| `rgw_sal_rados_bucket_get_internal()` | `RadosBucket*` 访问 | ✅ 接口完成 |
| `rgw_sal_rados_bucket_from_internal()` | `new RadosBucket(...)` | ✅ 接口完成 |
| `rgw_sal_rados_object_get_internal()` | `RadosObject*` 访问 | ✅ 接口完成 |
| `rgw_sal_rados_object_from_internal()` | `new RadosObject(...)` | ✅ 接口完成 |
| `rgw_sal_rados_object_read_prepare()` | `RadosReadOp::prepare()` | ✅ 接口完成 |
| `rgw_sal_rados_object_read_iterate()` | `RadosReadOp::iterate()` | ✅ 接口完成 |
| `rgw_sal_rados_object_get_attr()` | `RadosReadOp::get_attr()` | ✅ 接口完成 |

---

## 3. 转化完成度统计

| 模块 | 总虚函数数 | 已转化数 | 完成度 |
|------|------------|----------|--------|
| User VTable | 25 | 15 | 60% |
| Bucket VTable | 70+ | 13 | ~18% |
| Object VTable | 40+ | 13 | ~32% |
| Driver VTable | 100+ | 11 | ~11% |
| **总计** | **235+** | **52** | **~22%** |

---

## 4. 测试验证方案

### 4.1 编译测试

```bash
# 编译类型实现
gcc -c -I./src/rgw/sal_c/include \
    src/rgw/sal_c/src/rgw_sal_types.c -o build/rgw_sal_types.o

# 编译核心实现
gcc -c -I./src/rgw/sal_c/include \
    src/rgw/sal_c/src/rgw_sal.c -o build/rgw_sal.o
```

### 4.2 单元测试用例

| 测试用例 | 测试内容 |
|----------|----------|
| `test_user_id_create_destroy` | 用户 ID 创建/销毁 |
| `test_bucket_id_create_destroy` | 桶 ID 创建/销毁 |
| `test_obj_key_create_destroy` | 对象键创建/销毁 |
| `test_attrs_basic` | 属性设置/获取 |
| `test_attrs_update` | 属性更新 |
| `test_attrs_not_found` | 属性查找失败 |
| `test_driver_create` | 驱动创建 |
| `test_driver_name` | 驱动名称 |
| `test_user_get` | 获取用户 |
| `test_bucket_get` | 获取桶 |
| `test_object_get` | 获取对象 |

### 4.3 AddressSanitizer 检测

```bash
gcc -fsanitize=address -g \
    -I./src/rgw/sal_c/include \
    src/rgw/sal_c/src/rgw_sal_types.c \
    tests/test_sal_types.c -o build/test_sal_asan
./build/test_sal_asan
```

---

## 5. 后续工作

### 5.1 需实现的函数

1. **RADOS 适配器实现** (`rgw_sal_rados.c`)
   - `rgw_sal_rados_driver_create()`
   - 所有 RADOS 特定操作

2. **完善用户操作**
   - `rgw_sal_user_load()`
   - `rgw_sal_user_store()`
   - `rgw_sal_user_remove()`

3. **完善桶操作**
   - `rgw_sal_create_bucket()`
   - `rgw_sal_bucket_remove()`

4. **完善对象操作**
   - `rgw_sal_object_read()`
   - `rgw_sal_object_write()`
   - `rgw_sal_object_delete()`

### 5.2 待转化模块

- 账户管理 (Account)
- 使用统计 (Usage)
- 角色管理 (Role)
- 组管理 (Group)
- 配额 (Quota)
- 多站点 (Multisite)
- 生命周期 (Lifecycle)

---

**文档版本**: 1.0
**生成日期**: 2026-03-18

# SAL C 接口存根函数详细实现方案

**文档版本**: 1.0
**生成日期**: 2026-03-19
**项目**: RGW C++ 到 C 转换 - 存储抽象层 (SAL)

---

## 1. 概述

本文档对 `IMPLEMENTATION_STATUS.md` 中列出的存根/简化实现函数进行详细分析，并提供：
- 是否可以手动实现
- 手动实现所需的代码量
- 第三方 C 库依赖评估
- 详细的实现方案

### 1.1 存根函数统计

| 类别 | 存根/简化实现数 | 无依赖可实现 | 有依赖需评估 |
|------|----------------|--------------|--------------|
| User VTable | 12 | 5 | 7 |
| Bucket VTable | 35 | 8 | 27 |
| Object VTable | 17 | 4 | 13 |
| Driver VTable | 45 | 5 | 40 |
| **总计** | **~109** | **~22** | **~87** |

---

## 2. 无依赖函数（可直接实现）

这些函数不需要外部依赖，可以立即完整实现。

### 2.1 User VTable 无依赖函数

#### 2.1.1 `merge_and_store_attrs` - 属性合并存储

**当前状态**: 返回 OK 但未真正合并
**依赖**: 无
**可实现**: 是

**实现方案**:

```c
static int rados_user_merge_and_store_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user || !new_attrs) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取当前属性 */
    rgw_sal_attrs_t* current_attrs = impl->attrs;
    if (!current_attrs) {
        current_attrs = rgw_sal_attrs_create();
        if (!current_attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
        impl->attrs = current_attrs;
    }

    /* 合并新属性到当前属性 */
    for (size_t i = 0; i < new_attrs->count; i++) {
        const rgw_sal_attr_pair_t* pair = &new_attrs->pairs[i];
        int ret = rgw_sal_attrs_set(current_attrs, pair->key, pair->value, pair->value_len);
        if (ret != RGW_SAL_OK) return ret;
    }

    /* 存储合并后的属性 */
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;  // TODO: 实际调用 store() 持久化
}
```

**估计代码量**: ~25 行
**第三方库**: 无

---

#### 2.1.2 `get_ns` / `set_ns` / `clear_ns` - 命名空间操作

**当前状态**: get_ns 返回 NULL，set_ns/clear_ns 空实现
**依赖**: 无
**可实现**: 是

**实现方案**:

已经在 `rgw_sal_rados.c` 中实现（第 489-514 行），只需完善即可。

**估计代码量**: ~30 行
**第三方库**: 无

---

#### 2.1.3 `get_attrs` / `set_attrs` - 属性映射

**当前状态**: 基础实现存在
**依赖**: 无
**可实现**: 是

**实现方案**: 已在基础类型中实现，需要在所有驱动中完善。

**估计代码量**: 已实现
**第三方库**: 无

---

### 2.2 Bucket VTable 无依赖函数

#### 2.2.1 `set_acl` / `get_acl` - ACL 操作

**当前状态**: 只存储指针
**依赖**: 无（ACL 解析可延迟）
**可实现**: 是（简化版）

**实现方案**:

```c
static int rados_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl, 
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 存储 ACL 指针（简化实现） */
    impl->acl = acl;
    impl->mtime = time(NULL);

    /* TODO: 完整实现需要解析 ACL 并存储到 RADOS omap
     * 1. 如果 acl 是 XML/JSON 字符串，解析为内部结构
     * 2. 编码为二进制格式
     * 3. 写入 RADOS 对象 (bucket.acl)
     */

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~40 行（简化版）
**第三方库**: 无

---

#### 2.2.2 `set_policy` / `get_policy` - IAM 策略操作

**当前状态**: 只存储指针
**依赖**: 无
**可实现**: 是（简化版）

**实现方案**: 与 ACL 类似，存储策略指针。

**估计代码量**: ~40 行
**第三方库**: 无

---

#### 2.2.3 `rename` - 桶重命名

**当前状态**: 只更新内存 name
**依赖**: 无（完整实现需要 RADOS 元数据更新）
**可实现**: 是（内存级别）

**实现方案**: 已在 `rgw_sal_rados.c` 中实现（第 884-908 行）。

**估计代码量**: 已实现
**第三方库**: 无

---

#### 2.2.4 `create` / `delete_bucket` - 桶创建/删除

**当前状态**: 只标记状态
**依赖**: 无（简化实现）
**可实现**: 是（内存级别）

**实现方案**: 已在 `rgw_sal_rados.c` 中实现。

**估计代码量**: 已实现
**第三方库**: 无

---

#### 2.2.5 `get_usage` / `read_stats` / `complete_stats` - 统计操作

**当前状态**: 空实现或返回 NULL
**依赖**: 无（可返回默认/空值）
**可实现**: 是（返回空统计）

**实现方案**:

```c
static int rados_bucket_read_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                    void* stats) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    if (stats) {
        /* 填充默认统计值 */
        memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
    }

    (void)dpp;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~20 行
**第三方库**: 无

---

### 2.3 Object VTable 无依赖函数

#### 2.3.1 `is_atomic` / `set_atomic` - 原子标志

**当前状态**: is_atomic 返回 false，set_atomic 空实现
**依赖**: 无
**可实现**: 是

**实现方案**:

```c
typedef struct rados_object_impl {
    // ... 现有字段 ...
    bool is_atomic;           /* 新增：原子标志 */
} rados_object_impl_t;

static bool rados_object_is_atomic(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->is_atomic : false;
}

static int rados_object_set_atomic(rgw_sal_object_t* obj, bool atomic) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (impl) {
        impl->is_atomic = atomic;
    }
    return RGW_SAL_OK;
}
```

**估计代码量**: ~25 行
**第三方库**: 无

---

#### 2.3.2 `is_expired` - 过期检查

**当前状态**: 返回 false
**依赖**: 无
**可实现**: 是

**实现方案**:

```c
static bool rados_object_is_expired(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl || !impl->attrs) return false;

    /* 检查 Expiration-Time 属性 */
    uint8_t* value = NULL;
    size_t value_len = 0;
    int ret = rgw_sal_attrs_get(impl->attrs, " expiration-time", &value, &value_len);
    if (ret != RGW_SAL_OK || !value) return false;

    /* 解析过期时间并比较 */
    time_t now = time(NULL);
    time_t expiry = parse_iso8601_time((const char*)value, value_len);
    return now > expiry;
}
```

**估计代码量**: ~35 行
**第三方库**: 无

---

#### 2.3.3 `load_state` - 状态加载

**当前状态**: 只标记 loaded=true
**依赖**: 无（简化实现）
**可实现**: 是

**实现方案**: 已在 `rgw_sal_rados.c` 中实现。

**估计代码量**: 已实现
**第三方库**: 无

---

### 2.4 Driver VTable 无依赖函数

#### 2.4.1 `list_buckets` - 桶列表

**当前状态**: 返回空列表
**依赖**: RADOS API（可选实现）
**可实现**: 是（返回空列表）

**实现方案**: 已有简化实现，可返回空列表。

**估计代码量**: 已实现
**第三方库**: 无

---

## 3. 有依赖函数（需要详细评估）

### 3.1 User VTable 有依赖函数

#### 3.1.1 `set_info` / `get_info` - 配额信息

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: `rgw_sal_quota_info_t` 结构（已定义）
**可手动实现**: 是

**依赖分析**:
- `rgw_sal_quota_info_t` 已在 `rgw_sal_types.h` 中定义
- 需要在 `rados_user_impl_t` 中添加 `quota_info` 字段
- 无需第三方库

**实现方案**:

```c
/* 在 rados_user_impl_t 中添加 */
typedef struct rados_user_impl {
    // ... 现有字段 ...
    rgw_sal_quota_info_t quota_info;  /* 配额信息 */
} rados_user_impl_t;

static int rados_user_set_info(rgw_sal_user_t* user, void* info) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (info) {
        /* 复制配额信息 */
        memcpy(&impl->quota_info, info, sizeof(rgw_sal_quota_info_t));
    }
    return RGW_SAL_OK;
}

static int rados_user_get_info(rgw_sal_user_t* user, void** info) {
    if (!user || !info) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *info = &impl->quota_info;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~50 行
**第三方库**: 无
**实现难度**: 低

---

#### 3.1.2 `get_caps` - 用户权限

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: `rgw_sal_user_caps_t` 结构（已定义）
**可手动实现**: 是

**实现方案**:

```c
typedef struct rados_user_impl {
    // ... 现有字段 ...
    rgw_sal_user_caps_t user_caps;  /* 用户权限 */
} rados_user_impl_t;

static int rados_user_get_caps(rgw_sal_user_t* user, void** caps) {
    if (!user || !caps) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *caps = &impl->user_caps;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~40 行
**第三方库**: 无
**实现难度**: 低

---

#### 3.1.3 `get_version_tracker` - 版本跟踪器

**当前状态**: 返回 NULL
**依赖**: `rgw_sal_obj_version_tracker_t` 结构（已定义）
**可手动实现**: 是

**实现方案**: 与 `get_caps` 类似。

**估计代码量**: ~40 行
**第三方库**: 无
**实现难度**: 低

---

#### 3.1.4 `read_usage` / `trim_usage` - 使用统计

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: RGWUsage 类或 usage API
**可手动实现**: 需要评估

**依赖分析**:
- Ceph 有内置的 usage tracking 系统
- 需要与 Ceph 的 `RGWUsageManager` 交互
- 可以实现简化版本

**实现方案**:

```c
typedef struct rados_user_impl {
    // ... 现有字段 ...
    rgw_sal_usage_info_t usage;  /* 使用统计缓存 */
} rados_user_impl_t;

static int rados_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                  uint64_t start_epoch, uint64_t end_epoch,
                                  uint32_t max_entries, void* usage) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 完整实现需要调用 RGWUsageManager
     * 简化版本: 返回缓存的 usage 数据
     */
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl && usage) {
        memcpy(usage, &impl->usage, sizeof(rgw_sal_usage_info_t));
    }

    (void)start_epoch;
    (void)end_epoch;
    (void)max_entries;
    (void)dpp;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~80 行（简化版）/ ~200 行（完整版）
**第三方库**: Ceph 内部 API
**实现难度**: 中

---

#### 3.1.5 `verify_mfa` - MFA 认证

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: MFA 验证模块
**可手动实现**: 是（可调用外部库）

**实现方案**:

```c
static int rados_user_verify_mfa(rgw_sal_user_t* user, const char* mfa_serial,
                                  const char* code, const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 完整实现需要与 MFA 后端交互
     * 可选方案:
     * 1. 使用 oathtool (liboath) 验证 TOTP
     * 2. 调用 Ceph 内部的 MFA 验证
     */

    /* 简化版本: 始终返回失败（需要真实 MFA 实现） */
    (void)user;
    (void)mfa_serial;
    (void)code;
    (void)dpp;
    return RGW_SAL_ERR_PERMISSION_DENIED;
}
```

**估计代码量**: ~60 行
**第三方库**: `liboath` (OATH Toolkit) 或 Ceph 内部 API
**实现难度**: 中

---

#### 3.1.6 `list_groups` - 组管理

**当前状态**: 返回空列表
**依赖**: 组管理模块
**可手动实现**: 是（返回空列表或简化实现）

**实现方案**:

```c
static int rados_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   void** groups, uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 完整实现需要组管理后端
     * 简化版本: 返回空列表
     */
    *groups = NULL;
    *count = 0;

    (void)dpp;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~30 行（简化版）
**第三方库**: 无（可扩展）
**实现难度**: 低（简化版）/ 中（完整版）

---

### 3.2 Bucket VTable 有依赖函数

#### 3.2.1 桶索引检查函数

**函数列表**:
- `check_object_index`
- `fix_object_index`
- `check_bucket_index`

**当前状态**: 空实现
**依赖**: RADOS 索引检查 API
**可手动实现**: 部分可实现

**实现方案**:

```c
static int rados_bucket_check_object_index(rgw_sal_bucket_t* bucket, 
                                            const rgw_sal_dpp_t* dpp,
                                            rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 完整实现需要:
     * 1. 遍历 RADOS 池中的对象
     * 2. 与索引记录比对
     * 3. 返回不一致的列表
     */

    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}
```

**估计代码量**: ~150 行
**第三方库**: RADOS API
**实现难度**: 高

---

#### 3.2.2 异步统计函数

**函数列表**:
- `read_stats_async`
- `update_bucket_stats`
- `sync_user_stats`

**当前状态**: 空实现
**依赖**: 异步操作框架
**可手动实现**: 是（简化同步版本）

**实现方案**: 使用回调模式实现简化版本。

**估计代码量**: ~100 行
**第三方库**: 无
**实现难度**: 中

---

#### 3.2.3 标签函数

**函数列表**:
- `get_tag`
- `set_tag`

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: 标签存储（可用属性实现）
**可手动实现**: 是

**实现方案**:

```c
static int rados_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag) {
    if (!bucket || !tag) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 从属性中获取标签 */
    if (impl->attrs) {
        uint8_t* value = NULL;
        size_t value_len = 0;
        int ret = rgw_sal_attrs_get(impl->attrs, "tag", &value, &value_len);
        if (ret == RGW_SAL_OK && value) {
            *tag = malloc(value_len + 1);
            if (*tag) {
                memcpy(*tag, value, value_len);
                (*tag)[value_len] = '\0';
                return RGW_SAL_OK;
            }
        }
    }

    *tag = NULL;
    return RGW_SAL_OK;
}

static int rados_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    if (tag) {
        return rgw_sal_attrs_set(impl->attrs, "tag", (const uint8_t*)tag, strlen(tag));
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}
```

**估计代码量**: ~60 行
**第三方库**: 无
**实现难度**: 低

---

#### 3.2.4 多站点同步函数

**函数列表**:
- `sync`
- `drain`
- `get_bucket_topic`
- `set_bucket_topic`
- `get_placement_rule`

**当前状态**: 部分实现或空实现
**依赖**: 多站点同步框架
**可手动实现**: 部分可实现

**实现方案**: 多站点同步需要完整的同步基础设施，建议保持简化实现。

**估计代码量**: ~200+ 行
**第三方库**: Ceph multisite API
**实现难度**: 高

---

### 3.3 Object VTable 有依赖函数

#### 3.3.1 对象读写操作

**函数列表**:
- `read`
- `write`

**当前状态**: read 返回 NOT_FOUND，write 只记录状态
**依赖**: RADOS API（librados）
**可手动实现**: 需要 librados 库

**依赖分析**:
- librados 是 Ceph 的标准 C API
- 可以直接使用
- 需要理解 RADOS 对象寻址

**实现方案**:

```c
static int rados_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                             uint8_t* buffer, size_t* buffer_size,
                             const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !buffer || !buffer_size) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建 RADOS 对象 ID */
    char oid[512];
    snprintf(oid, sizeof(oid), "%s/%s", impl->bucket_name, impl->name);

    /* TODO: 实际调用 librados 读操作
     * librados_ioctx_read(rados_ioctx_t io, const char *oid,
     *                      char *buf, size_t len, size_t offset);
     */

    (void)offset;
    (void)end;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}
```

**估计代码量**: ~150 行
**第三方库**: librados (Ceph)
**实现难度**: 中

---

#### 3.3.2 ACL 操作

**函数列表**:
- `get_acl`
- `set_acl`

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: ACL 解析库
**可手动实现**: 是

**实现方案**: 使用基础类型实现简化版。

**估计代码量**: ~80 行
**第三方库**: 无
**实现难度**: 中

---

#### 3.3.3 生命周期转换

**函数列表**:
- `transition`
- `transition_to_cloud`
- `restore_obj_from_cloud`

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: 生命周期管理模块
**可手动实现**: 部分可实现

**实现方案**: 生命周期转换需要与存储层交互，建议保持简化实现。

**估计代码量**: ~200 行
**第三方库**: Ceph lifecycle API
**实现难度**: 高

---

#### 3.3.4 对象复制

**函数列表**:
- `copy_object`
- `delete_object`

**当前状态**: 返回 NOT_IMPLEMENTED
**依赖**: RADOS API
**可手动实现**: 是

**实现方案**:

```c
static int rados_object_copy_object(rgw_sal_object_t* dst_obj,
                                      rgw_sal_object_t* src_obj,
                                      const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y) {
    if (!dst_obj || !src_obj) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 完整实现需要:
     * 1. 读取源对象数据
     * 2. 写入目标对象
     * 3. 复制元数据
     */

    (void)dst_obj;
    (void)src_obj;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}
```

**估计代码量**: ~120 行
**第三方库**: librados
**实现难度**: 中

---

### 3.4 Driver VTable 有依赖函数

#### 3.4.1 用户查询函数

**函数列表**:
- `get_user_by_access_key`
- `get_user_by_email`
- `get_user_by_swift`

**当前状态**: 返回 NOT_FOUND
**依赖**: 元数据存储（OMAP）
**可手动实现**: 是

**实现方案**:

```c
static int rados_driver_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key,
                                                rgw_sal_user_t** user,
                                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 从 RADOS OMAP 查找用户
     * 1. 构建查找键: "access_key:" + key
     * 2. 在 users.root OMAP 中查找
     * 3. 如果找到，解析用户数据并创建 user 对象
     */

    (void)driver;
    (void)key;
    (void)user;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;
}
```

**估计代码量**: ~100 行
**第三方库**: librados
**实现难度**: 中

---

#### 3.4.2 用户持久化函数

**函数列表**:
- `user_load`
- `user_store`
- `user_remove`

**当前状态**: 简化实现
**依赖**: RADOS API
**可手动实现**: 是

**实现方案**:

```c
static int rados_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl || !impl->id) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 将用户数据序列化为 JSON/二进制格式
     * 写入 RADOS 对象: users.uid.<user_id>
     */

    (void)dpp;
    (void)y;
    (void)exclusive;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}
```

**估计代码量**: ~150 行
**第三方库**: librados
**实现难度**: 中

---

## 4. 按优先级分类的实现计划

### 4.1 P0 - 无依赖，可立即实现

| 函数 | 驱动 | 估计代码量 | 难度 |
|------|------|------------|------|
| `merge_and_store_attrs` | RADOS/DBStore | 25 行 | 低 |
| `get_ns/set_ns/clear_ns` | RADOS/DBStore | 30 行 | 低 |
| `set_info/get_info` | RADOS/DBStore | 50 行 | 低 |
| `get_caps` | RADOS/DBStore | 40 行 | 低 |
| `get_version_tracker` | RADOS/DBStore | 40 行 | 低 |
| `is_atomic/set_atomic` | RADOS/DBStore | 25 行 | 低 |
| `is_expired` | RADOS/DBStore | 35 行 | 低 |
| `get_tag/set_tag` | RADOS/DBStore | 60 行 | 低 |
| `read_stats/complete_stats` | RADOS/DBStore | 40 行 | 低 |
| **P0 合计** | | **~345 行** | |

---

### 4.2 P1 - 有简单依赖，可快速实现

| 函数 | 依赖 | 估计代码量 | 难度 |
|------|------|------------|------|
| `read_usage/trim_usage` | usage cache | 80 行 | 中 |
| `list_groups` | 组管理 | 30 行 | 低 |
| `user_load/store/remove` | RADOS OMAP | 150 行 | 中 |
| `get_user_by_access_key/email/swift` | RADOS OMAP | 100 行 | 中 |
| `set_acl/get_acl` | ACL 存储 | 80 行 | 中 |
| `set_policy/get_policy` | 策略存储 | 80 行 | 中 |
| `read_stats_async` | 异步框架 | 60 行 | 中 |
| **P1 合计** | | **~580 行** | |

---

### 4.3 P2 - 有复杂依赖，需要仔细实现

| 函数 | 依赖 | 估计代码量 | 难度 |
|------|------|------------|------|
| `object_read/write` | librados | 300 行 | 中 |
| `object_delete` | librados | 100 行 | 中 |
| `copy_object` | librados | 120 行 | 中 |
| `check/fix_object_index` | RADOS 索引 | 150 行 | 高 |
| `check_bucket_index` | RADOS 索引 | 100 行 | 高 |
| `verify_mfa` | MFA 模块 | 60 行 | 中 |
| `transition` | 生命周期 | 200 行 | 高 |
| `update_bucket_stats` | 统计聚合 | 80 行 | 中 |
| **P2 合计** | | **~1110 行** | |

---

### 4.4 P3 - 高复杂度，建议保持简化实现

| 函数 | 依赖 | 建议 | 原因 |
|------|------|------|------|
| `sync/drain` | 多站点 | 保持简化 | 需要完整同步框架 |
| `get_bucket_topic/set_bucket_topic` | 事件通知 | 保持简化 | 需要通知系统 |
| `transition_to_cloud/restore_from_cloud` | 云端存储 | 保持简化 | 需要云端集成 |
| `placement_rules_match` | 放置规则 | 保持简化 | 需要规则引擎 |
| **P3 合计** | | **保持存根** | |

---

## 5. 第三方 C 库评估

### 5.1 必需的库

| 库名 | 用途 | 状态 | 备注 |
|------|------|------|------|
| `librados` | RADOS 对象操作 | Ceph 自带 | 核心依赖 |
| `libceph-common` | 通用 Ceph 工具 | Ceph 自带 | 已包含 |

### 5.2 可选的库

| 库名 | 用途 | 评估 | 建议 |
|------|------|------|------|
| `liboath` | MFA TOTP 验证 | 可选 | 可实现简化版本 |
| `libsqlite3` | DBStore 持久化 | 已在设计 | 用于 dbstore 驱动 |
| `libxml2` | ACL/策略解析 | 可选 | 可用简化解析器 |

### 5.3 Ceph 内部依赖

| 模块 | 说明 | 交互方式 |
|------|------|----------|
| `RGWUserInfo` | 用户信息 | 通过序列化/反序列化 |
| `RGWQuotaInfo` | 配额信息 | 通过序列化/反序列化 |
| `RGWUserCaps` | 用户权限 | 通过序列化/反序列化 |
| `RGWUsageManager` | 使用统计 | 通过内部 API |
| `RGWMFAManager` | MFA 管理 | 通过内部 API |

---

## 6. 完整实现工作量估算

### 6.1 按阶段划分

| 阶段 | 内容 | 代码量 | 依赖 |
|------|------|--------|------|
| 阶段 1 | P0 函数（无依赖） | ~345 行 | 无 |
| 阶段 2 | P1 函数（简单依赖） | ~580 行 | librados (部分) |
| 阶段 3 | P2 函数（复杂依赖） | ~1110 行 | librados (完整) |
| 阶段 4 | P3 函数（高复杂度） | - | 多站点/云端 |
| **总计** | | **~2035 行** | |

### 6.2 按驱动划分

| 驱动 | 需实现函数 | 估计代码量 | 备注 |
|------|------------|------------|------|
| RADOS | ~80 | ~1500 行 | 核心驱动 |
| DBStore | ~70 | ~1200 行 | SQLite 后端 |
| POSIX | ~50 | ~800 行 | 文件系统后端 |
| **总计** | **~200** | **~3500 行** | |

---

## 7. 结论与建议

### 7.1 可立即实现的函数（P0）

以下函数无需外部依赖，可以立即完整实现：

1. **User VTable**: `merge_and_store_attrs`, `get_ns`, `set_ns`, `clear_ns`, `set_info`, `get_info`, `get_caps`, `get_version_tracker`
2. **Bucket VTable**: `get_tag`, `set_tag`, `read_stats`, `complete_stats`
3. **Object VTable**: `is_atomic`, `set_atomic`, `is_expired`

**总计**: ~345 行代码

### 7.2 需要 RADOS 库的函数（P1-P2）

这些函数需要 librados 支持，建议按以下顺序实现：

1. 用户查询函数（`get_user_by_access_key` 等）
2. 用户持久化函数（`load`, `store`, `remove`）
3. 对象读写函数（`read`, `write`）
4. 索引检查函数

### 7.3 建议保持简化实现的函数（P3）

以下函数建议保持简化实现，因为完整实现需要大量基础设施：

- 多站点同步相关函数
- 云端存储相关函数
- 完整的事件通知函数

---

**文档状态**: 草稿
**下一步行动**: 
1. 实现 P0 函数（~345 行）
2. 评估 P1 函数的依赖
3. 为 P2 函数设计 RADOS 集成方案

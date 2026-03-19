# SAL C 接口 P1 函数详细实现计划

**文档版本**: 1.0
**生成日期**: 2026-03-19
**项目**: RGW C++ 到 C 转换 - 存储抽象层 (SAL)
**优先级**: P1 - 有简单依赖

---

## 1. P1 函数总览

### 1.1 P1 函数分类

| 类别 | 函数 | 驱动 | 依赖 | 估计代码量 | 难度 |
|------|------|------|------|------------|------|
| **User 统计** | `read_usage` | 所有驱动 | usage cache | ~80 行/驱动 | 中 |
| **User 统计** | `trim_usage` | 所有驱动 | usage cache | ~60 行/驱动 | 中 |
| **User 组管理** | `list_groups` | 所有驱动 | 组管理 | ~40 行/驱动 | 低 |
| **User MFA** | `verify_mfa` | 所有驱动 | MFA 模块 | ~60 行/驱动 | 中 |
| **Driver 查询** | `get_user_by_access_key` | RADOS/DBStore | OMAP/SQLite | ~100 行/驱动 | 中 |
| **Driver 查询** | `get_user_by_email` | RADOS/DBStore | OMAP/SQLite | ~100 行/驱动 | 中 |
| **Driver 查询** | `get_user_by_swift` | RADOS/DBStore | OMAP/SQLite | ~80 行/驱动 | 中 |
| **User 持久化** | `user_load` | POSIX | 文件系统 | ~50 行 | 低 |
| **User 持久化** | `user_store` | POSIX | 文件系统 | ~80 行 | 中 |
| **User 持久化** | `user_remove` | POSIX | 文件系统 | ~50 行 | 低 |
| **User 属性** | `read_attrs` | 所有驱动 | 属性读取 | ~40 行/驱动 | 低 |
| **Bucket ACL** | `get_acl` | 所有驱动 | ACL 存储 | ~60 行/驱动 | 低 |
| **Object ACL** | `get_acl` / `set_acl` | 所有驱动 | ACL 存储 | ~80 行/驱动 | 中 |
| **Object 读写** | `read` / `write` | POSIX | 文件系统 | ~150 行/驱动 | 中 |
| **P1 合计** | **~15 个函数** | | | **~1230 行** | |

---

## 2. 按功能模块的实现计划

### 2.1 模块 A: User 统计功能

#### 函数列表
- `read_usage` - 读取用户使用统计
- `trim_usage` - 修剪用户使用统计

#### 当前状态
- **RADOS**: 返回 `NOT_IMPLEMENTED`
- **DBStore**: 返回 `NOT_IMPLEMENTED`  
- **POSIX**: 返回 `NOT_IMPLEMENTED`

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                      P1 Module A: User Stats                 │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────┐      ┌─────────────────┐               │
│  │  rados_user    │      │  dbstore_user   │               │
│  │  _read_usage   │      │  _read_usage    │               │
│  └────────┬────────┘      └────────┬────────┘               │
│           │                        │                         │
│           ▼                        ▼                         │
│  ┌─────────────────────────────────────────┐                 │
│  │        rados_impl_t.usage              │                 │
│  │        (rgw_sal_usage_info_t)         │                 │
│  └─────────────────────────────────────────┘                 │
│           │                                                   │
│           ▼                                                   │
│  ┌─────────────────────────────────────────┐                 │
│  │     RADOS OMAP: .users.<uid>.usage     │  ◄── 需要 librados│
│  └─────────────────────────────────────────┘                 │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

**方案 1: 简化实现（推荐先实现）**
```c
typedef struct rados_user_impl {
    // ... 现有字段 ...
    rgw_sal_usage_info_t usage;  /* 使用统计缓存 */
    bool usage_loaded;
} rados_user_impl_t;

static int rados_user_read_usage(rgw_sal_user_t* user, 
                                 const rgw_sal_dpp_t* dpp,
                                 uint64_t start_epoch, 
                                 uint64_t end_epoch,
                                 uint32_t max_entries, 
                                 void* usage) {
    if (!user || !usage) return RGW_SAL_ERR_INVALID_ARG;
    
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 简化实现: 返回缓存的使用统计 */
    rgw_sal_usage_info_t* usage_info = (rgw_sal_usage_info_t*)usage;
    
    /* 过滤指定时间范围 (简化版本不实际过滤) */
    usage_info->total_bytes = impl->usage.total_bytes;
    usage_info->total_bytes_rounded = impl->usage.total_bytes_rounded;
    usage_info->total_entries = impl->usage.total_entries;
    
    (void)start_epoch;
    (void)end_epoch;
    (void)max_entries;
    (void)dpp;
    
    return RGW_SAL_OK;
}
```

**方案 2: 完整实现（需要 RADOS）**
- 优点: 支持真实的使用统计
- 缺点: 需要 librados API

#### 实现步骤

1. **阶段 1a**: 在 `rados_user_impl_t` 中添加 `usage` 字段
2. **阶段 1b**: 实现 `read_usage` 简化版本
3. **阶段 1c**: 实现 `trim_usage` 简化版本
4. **阶段 2**: 添加 RADOS OMAP 读取/写入支持

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1a | 添加 usage 字段到 impl 结构 | 10 行 | 🔄 待完成 |
| 1b | 实现 read_usage 简化版 | 30 行/驱动 | 🔄 待完成 |
| 1c | 实现 trim_usage 简化版 | 25 行/驱动 | 🔄 待完成 |
| 2 | 完整实现 (RADOS OMAP) | 100 行/驱动 | 🔄 后续 |

---

### 2.2 模块 B: 组管理功能

#### 函数列表
- `list_groups` - 列出用户所属组

#### 当前状态
- **所有驱动**: 返回空列表 `*groups = NULL; *count = 0;`

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                    P1 Module B: Groups                       │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────┐                                         │
│  │  rados_user    │                                         │
│  │  _list_groups  │                                         │
│  └────────┬────────┘                                         │
│           │                                                  │
│           ▼                                                  │
│  ┌─────────────────────────────────────────┐                 │
│  │     返回值: (rgw_sal_group_info_t*)    │                 │
│  │              groups[]                   │                 │
│  │              count                     │                 │
│  └─────────────────────────────────────────┘                 │
│           │                                                   │
│           ▼                                                   │
│  ┌─────────────────────────────────────────┐                 │
│  │     RADOS OMAP: .users.<uid>.groups    │  ◄── 需要 librados│
│  └─────────────────────────────────────────┘                 │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

**简化实现（先实现）**:
```c
static int rados_user_list_groups(rgw_sal_user_t* user, 
                                   const rgw_sal_dpp_t* dpp,
                                   void** groups, 
                                   uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 简化实现: 返回空列表 */
    /* 完整实现需要: 从组管理服务获取用户所属组列表 */
    
    *groups = NULL;
    *count = 0;
    
    (void)dpp;
    return RGW_SAL_OK;
}
```

**扩展实现（使用 JSON 存储）**:
```c
/* 添加组信息到 impl 结构 */
typedef struct rados_user_impl {
    // ... 现有字段 ...
    char** groups;          /* 组 ID 数组 */
    uint32_t num_groups;     /* 组数量 */
} rados_user_impl_t;

static int rados_user_list_groups(rgw_sal_user_t* user,
                                   const rgw_sal_dpp_t* dpp,
                                   void** groups,
                                   uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;
    
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    if (impl->num_groups > 0 && impl->groups) {
        *groups = impl->groups;  /* 返回内部数组的指针 */
        *count = impl->num_groups;
    } else {
        *groups = NULL;
        *count = 0;
    }
    
    (void)dpp;
    return RGW_SAL_OK;
}
```

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1 | 实现简化版 list_groups | 20 行/驱动 | 🔄 待完成 |
| 2 | 添加组信息存储字段 | 15 行 | 🔄 待完成 |
| 3 | 实现组信息加载/保存 | 50 行 | 🔄 待完成 |

---

### 2.3 模块 C: MFA 认证功能

#### 函数列表
- `verify_mfa` - 验证 MFA 代码

#### 当前状态
- **所有驱动**: 返回 `NOT_IMPLEMENTED`

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                     P1 Module C: MFA                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────┐                                         │
│  │  rados_user     │                                         │
│  │  _verify_mfa   │                                         │
│  └────────┬────────┘                                         │
│           │                                                  │
│           ▼                                                  │
│  ┌─────────────────────────────────────────┐                 │
│  │  输入参数:                               │                 │
│  │    - mfa_serial: MFA 设备序列号         │                 │
│  │    - code: TOTP 验证码 (6位数字)        │                 │
│  │                                          │                 │
│  │  处理流程:                               │                 │
│  │    1. 获取用户的 MFA IDs                 │                 │
│  │    2. 查找匹配的设备                      │                 │
│  │    3. 验证 TOTP 代码                     │                 │
│  │    4. 返回验证结果                        │                 │
│  └─────────────────────────────────────────┘                 │
│           │                                                   │
│           ▼                                                  │
│  ┌─────────────────────────────────────────┐                 │
│  │  第三方库选项:                           │                 │
│  │    - liboath (OATH Toolkit)            │                 │
│  │    - 内部实现 (使用 time.h)             │                 │
│  └─────────────────────────────────────────┘                 │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

**简化实现（返回失败）**:
```c
static int rados_user_verify_mfa(rgw_sal_user_t* user,
                                  const char* mfa_serial,
                                  const char* code,
                                  const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) {
        return RGW_SAL_ERR_INVALID_ARG;
    }
    
    /* 简化实现: 暂时不支持 MFA */
    /* 完整实现需要 TOTP 验证库 */
    
    (void)user;
    (void)mfa_serial;
    (void)code;
    (void)dpp;
    
    return RGW_SAL_ERR_PERMISSION_DENIED;
}
```

**完整实现（使用 liboath）**:
```c
/* 需要在 CMakeLists.txt 中添加: find_package(OATH REQUIRED) */
#include <oath.h>

static int rados_user_verify_mfa(rgw_sal_user_t* user,
                                  const char* mfa_serial,
                                  const char* code,
                                  const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) {
        return RGW_SAL_ERR_INVALID_ARG;
    }
    
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 获取 MFA 密钥 (从 impl->mfa_ids 中查找) */
    char* mfa_secret = NULL;  /* TODO: 从存储获取 */
    
    if (!mfa_secret) {
        return RGW_SAL_ERR_NOT_FOUND;  /* MFA 未配置 */
    }
    
    /* 使用 liboath 验证 TOTP */
    int ret = oath_totp_validate(
        mfa_secret,        /* Base32 编码的密钥 */
        strlen(mfa_secret),
        time(NULL),        /* 当前时间 */
        30,               /* 时间步长 (30秒) */
        0,                /* 窗口大小 */
        code              /* 用户输入的验证码 */
    );
    
    if (ret == OATH_OK) {
        return RGW_SAL_OK;  /* 验证成功 */
    } else if (ret == OATH_INVALID_OTP) {
        return RGW_SAL_ERR_PERMISSION_DENIED;  /* 验证码错误 */
    }
    
    return RGW_SAL_ERR_INTERNAL_ERROR;
}
```

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1 | 实现简化版 (返回失败) | 15 行/驱动 | 🔄 待完成 |
| 2 | 添加 MFA 相关字段 | 20 行 | 🔄 待完成 |
| 3 | 使用 liboath 实现 TOTP | 60 行 | 🔄 可选 |

---

### 2.4 模块 D: 用户查询功能

#### 函数列表
- `get_user_by_access_key` - 通过 Access Key 获取用户
- `get_user_by_email` - 通过邮箱获取用户
- `get_user_by_swift` - 通过 Swift 用户名获取用户

#### 当前状态
- **RADOS**: 返回 `NOT_FOUND`
- **DBStore**: 返回 `NOT_FOUND`

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                 P1 Module D: User Queries                     │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                  用户查询入口                           │ │
│  │                                                          │ │
│  │  get_user_by_access_key(key)                            │ │
│  │  get_user_by_email(email)                               │ │
│  │  get_user_by_swift(user_str)                           │ │
│  └──────────────────────────┬────────────────────────────┘ │
│                               │                               │
│           ┌───────────────────┼───────────────────┐          │
│           ▼                   ▼                   ▼          │
│  ┌────────────────┐ ┌────────────────┐ ┌────────────────┐   │
│  │  Access Key     │ │  Email         │ │  Swift User   │   │
│  │  OMAP Index     │ │  OMAP Index    │ │  OMAP Index    │   │
│  │                 │ │                │ │                │   │
│  │  .users.by_key  │ │  .users.by_eml  │ │  .users.by_sw  │   │
│  └────────┬─────────┘ └────────┬────────┘ └────────┬────────┘   │
│           │                   │                   │           │
│           └───────────────────┼───────────────────┘           │
│                               ▼                               │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              RADOS Object: users.<uid>                   │ │
│  │              (用户数据 JSON/二进制)                       │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

**RADOS 实现**:

```c
/* 用户查询实现 - RADOS */
static int rados_driver_get_user_by_access_key(rgw_sal_driver_t* driver,
                                                const char* key,
                                                rgw_sal_user_t** user,
                                                const rgw_sal_dpp_t* dpp,
                                                rgw_sal_yield_t* y) {
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;
    
    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* TODO: 完整实现需要:
     * 1. 在 .users.by_key OMAP 中查找 key -> uid
     * 2. 在 .users.<uid> 对象中读取用户数据
     * 3. 解析用户数据并创建 user 对象
     */
    
    /*
     * 伪代码:
     * rados_ioctx_t ioctx = impl->users_ioctx;
     * char uid[64];
     * 
     * // 查找 uid
     * int ret = rados_omap_get_val(ioctx, ".users.by_key", 
     *                               key, uid, sizeof(uid));
     * if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;
     * 
     * // 读取用户数据
     * char user_obj[128];
     * snprintf(user_obj, sizeof(user_obj), "users.%s", uid);
     * 
     * char user_data[4096];
     * ret = rados_read(ioctx, user_obj, user_data, 
     *                   sizeof(user_data), 0);
     * if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;
     * 
     * // 解析用户数据
     * // ... JSON 解析或二进制解析 ...
     * 
     * // 创建 user 对象
     * *user = rados_driver_get_user(driver, uid);
     * // ... 填充用户数据 ...
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;  /* 简化版本 */
}

static int rados_driver_get_user_by_email(rgw_sal_driver_t* driver,
                                           const char* email,
                                           rgw_sal_user_t** user,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    if (!driver || !email || !user) return RGW_SAL_ERR_INVALID_ARG;
    
    /* TODO: 类似 get_user_by_access_key 的实现
     * 使用 .users.by_email OMAP 索引
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;
}

static int rados_driver_get_user_by_swift(rgw_sal_driver_t* driver,
                                           const char* user_str,
                                           rgw_sal_user_t** user,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    if (!driver || !user_str || !user) return RGW_SAL_ERR_INVALID_ARG;
    
    /* TODO: 类似 get_user_by_access_key 的实现
     * 使用 .users.by_swift OMAP 索引
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;
}
```

**DBStore 实现**:

```c
/* 用户查询实现 - DBStore */
static int dbstore_driver_get_user_by_access_key(rgw_sal_driver_t* driver,
                                                 const char* key,
                                                 rgw_sal_user_t** user,
                                                 const rgw_sal_dpp_t* dpp,
                                                 rgw_sal_yield_t* y) {
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;
    
    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* TODO: 从 SQLite 查询用户
     * 
     * SQL:
     * SELECT user_id FROM access_keys WHERE key_id = ?;
     * SELECT * FROM users WHERE user_id = ?;
     */
    
    /*
     * 伪代码:
     * sqlite3_stmt *stmt;
     * const char *sql = "SELECT user_id FROM access_keys WHERE key_id = ?";
     * 
     * ret = sqlite3_prepare_v2(impl->db, sql, -1, &stmt, NULL);
     * if (ret != SQLITE_OK) return RGW_SAL_ERR_IO_ERROR;
     * 
     * sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
     * 
     * if (sqlite3_step(stmt) == SQLITE_ROW) {
     *     const char *uid = (const char*)sqlite3_column_text(stmt, 0);
     *     *user = dbstore_driver_get_user(driver, uid);
     *     // 加载用户数据
     * }
     * 
     * sqlite3_finalize(stmt);
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;
}
```

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1a | 实现 get_user_by_access_key (RADOS) | 80 行 | 🔄 待完成 |
| 1b | 实现 get_user_by_email (RADOS) | 80 行 | 🔄 待完成 |
| 1c | 实现 get_user_by_swift (RADOS) | 70 行 | 🔄 待完成 |
| 2a | 实现 get_user_by_access_key (DBStore) | 80 行 | 🔄 待完成 |
| 2b | 实现 get_user_by_email (DBStore) | 80 行 | 🔄 待完成 |
| 2c | 实现 get_user_by_swift (DBStore) | 70 行 | 🔄 待完成 |

---

### 2.5 模块 E: POSIX 用户持久化

#### 函数列表
- `posix_user_load` - 从文件系统加载用户
- `posix_user_store` - 保存用户到文件系统
- `posix_user_remove` - 从文件系统删除用户
- `posix_user_read_attrs` - 读取用户属性

#### 当前状态
- **POSIX**: 仅标记 `loaded = true`，其他为简化实现

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                  P1 Module E: POSIX User                    │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  用户数据存储结构                                         │ │
│  │                                                          │ │
│  │  ${POSIX_ROOT}/users/                                   │ │
│  │    ├── by_id/                                           │ │
│  │    │   └── {user_id}.json        # 用户数据              │ │
│  │    ├── by_key/                                          │ │
│  │    │   └── {access_key}.link     # 软链接到用户 ID      │ │
│  │    ├── by_email/                                        │ │
│  │    │   └── {email}.link          # 软链接到用户 ID      │ │
│  │    └── attrs/                                           │ │
│  │        └── {user_id}.attrs       # 用户属性              │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

```c
/* POSIX 用户持久化实现 */

/* 辅助函数: 加载用户数据 */
static int posix_load_user_data(posix_user_impl_t* impl, const char* filepath) {
    FILE* fp = fopen(filepath, "r");
    if (!fp) return RGW_SAL_ERR_NOT_FOUND;
    
    /* 读取 JSON 格式的用户数据 */
    char buffer[4096];
    size_t len = fread(buffer, 1, sizeof(buffer) - 1, fp);
    buffer[len] = '\0';
    fclose(fp);
    
    /* TODO: 解析 JSON 并填充 impl 结构
     * 简化版本可以使用手动解析
     */
    
    return RGW_SAL_OK;
}

/* 辅助函数: 保存用户数据 */
static int posix_save_user_data(posix_user_impl_t* impl, const char* filepath) {
    FILE* fp = fopen(filepath, "w");
    if (!fp) return RGW_SAL_ERR_IO_ERROR;
    
    /* 序列化为 JSON 格式 */
    fprintf(fp, "{\n");
    fprintf(fp, "  \"user_id\": \"%s\",\n", impl->user_id ? impl->user_id : "");
    fprintf(fp, "  \"tenant\": \"%s\",\n", impl->tenant ? impl->tenant : "");
    fprintf(fp, "  \"display_name\": \"%s\",\n", 
            impl->display_name ? impl->display_name : "");
    fprintf(fp, "  \"max_buckets\": %d,\n", impl->max_buckets);
    fprintf(fp, "  \"user_type\": %u\n", impl->user_type);
    fprintf(fp, "}\n");
    
    fclose(fp);
    return RGW_SAL_OK;
}

/* 加载用户 */
static int posix_user_load(rgw_sal_user_t* user,
                           const rgw_sal_dpp_t* dpp,
                           rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 构建用户文件路径 */
    char filepath[512];
    snprintf(filepath, sizeof(filepath), 
             "%s/users/by_id/%s.json",
             posix_driver_get_root_path(user->driver),
             impl->user_id);
    
    int ret = posix_load_user_data(impl, filepath);
    if (ret == RGW_SAL_OK) {
        impl->loaded = true;
    }
    
    (void)dpp;
    (void)y;
    return ret;
}

/* 保存用户 */
static int posix_user_store(rgw_sal_user_t* user,
                            const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y,
                            bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 构建用户目录和文件路径 */
    char dirpath[512];
    char filepath[512];
    const char* root = posix_driver_get_root_path(user->driver);
    
    snprintf(dirpath, sizeof(dirpath), "%s/users/by_id", root);
    snprintf(filepath, sizeof(filepath), "%s/%s.json", dirpath, impl->user_id);
    
    /* 检查 exclusive 模式 */
    if (exclusive) {
        FILE* fp = fopen(filepath, "r");
        if (fp) {
            fclose(fp);
            return RGW_SAL_ERR_EXISTS;  /* 用户已存在 */
        }
    }
    
    /* 创建目录 */
    mkdir(dirpath, 0755);
    
    /* 保存用户数据 */
    int ret = posix_save_user_data(impl, filepath);
    
    (void)dpp;
    (void)y;
    return ret;
}

/* 删除用户 */
static int posix_user_remove(rgw_sal_user_t* user,
                             const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 构建用户文件路径 */
    char filepath[512];
    snprintf(filepath, sizeof(filepath),
             "%s/users/by_id/%s.json",
             posix_driver_get_root_path(user->driver),
             impl->user_id);
    
    if (unlink(filepath) == 0) {
        return RGW_SAL_OK;
    } else if (errno == ENOENT) {
        return RGW_SAL_ERR_NOT_FOUND;
    }
    
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_IO_ERROR;
}
```

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1 | 实现辅助函数 (加载/保存) | 50 行 | 🔄 待完成 |
| 2 | 实现 posix_user_load | 30 行 | 🔄 待完成 |
| 3 | 实现 posix_user_store | 40 行 | 🔄 待完成 |
| 4 | 实现 posix_user_remove | 25 行 | 🔄 待完成 |
| 5 | 实现 posix_user_read_attrs | 30 行 | 🔄 待完成 |

---

### 2.6 模块 F: ACL 功能

#### 函数列表
- `get_acl` - 获取 ACL
- `set_acl` - 设置 ACL

#### 当前状态
- **RADOS**: `set_acl` 只存储指针，`get_acl` 未在 vtable 中
- **DBStore**: `set_acl` 只存储指针
- **POSIX**: `set_acl` 只存储指针

#### 依赖分析

```
依赖关系图:
┌─────────────────────────────────────────────────────────────┐
│                     P1 Module F: ACL                        │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  ACL 数据结构 (简化版)                                  │ │
│  │                                                          │ │
│  │  typedef struct {                                       │ │
│  │      char* owner_id;        /* 所有者 */                │ │
│  │      char* owner_display;   /* 所有者显示名 */          │ │
│  │      uint32_t permissions;  /* 权限位掩码 */            │ │
│  │  } rgw_sal_acl_t;                                     │ │
│  │                                                          │ │
│  │  typedef struct {                                       │ │
│  │      rgw_sal_acl_t grants[32];  /* 授权列表 */          │ │
│  │      uint32_t num_grants;      /* 授权数量 */           │ │
│  │  } rgw_sal_acl_info_t;                                  │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

#### 实现方案

```c
/* ACL 数据结构定义 (添加到 rgw_sal_types.h) */
typedef struct rgw_sal_acl_grant {
    uint32_t type;          /* 授权类型 */
    char* id;               /* 用户/组 ID */
    char* display_name;     /* 显示名称 */
    uint32_t permission;     /* READ, WRITE, READ_ACP, WRITE_ACP, FULL_CONTROL */
} rgw_sal_acl_grant_t;

typedef struct rgw_sal_acl_info {
    char* owner_id;
    char* owner_display_name;
    rgw_sal_acl_grant_t* grants;
    uint32_t num_grants;
    uint32_t size;          /* ACL 数据大小 */
} rgw_sal_acl_info_t;

/* ACL 获取/设置实现 (RADOS) */
static int rados_bucket_get_acl(rgw_sal_bucket_t* bucket,
                                  void** acl,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    if (!bucket || !acl) return RGW_SAL_ERR_INVALID_ARG;
    
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 返回存储的 ACL 指针 */
    *acl = impl->acl;
    
    /* TODO: 完整实现可以从 RADOS 读取 ACL
     * char acl_oid[256];
     * snprintf(acl_oid, sizeof(acl_oid), 
     *           "%s.acl", impl->bucket_id);
     * rados_read(ioctx, acl_oid, ...);
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int rados_bucket_set_acl(rgw_sal_bucket_t* bucket,
                                  void* acl,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    
    /* 存储 ACL 指针 (简化实现) */
    impl->acl = acl;
    impl->mtime = time(NULL);
    
    /* TODO: 完整实现可以将 ACL 写入 RADOS
     * char acl_oid[256];
     * snprintf(acl_oid, sizeof(acl_oid),
     *          "%s.acl", impl->bucket_id);
     * // 序列化 ACL 并写入
     * rados_write_full(ioctx, acl_oid, acl_data, acl_size);
     */
    
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}
```

#### 估计工作量

| 阶段 | 任务 | 代码量 | 状态 |
|------|------|--------|------|
| 1 | 定义 ACL 数据结构 | 40 行 | 🔄 待完成 |
| 2 | 实现 bucket_get_acl | 30 行/驱动 | 🔄 待完成 |
| 3 | 实现 bucket_set_acl (完整版) | 50 行/驱动 | 🔄 待完成 |
| 4 | 实现 object_get_acl | 30 行/驱动 | 🔄 待完成 |
| 5 | 实现 object_set_acl | 40 行/驱动 | 🔄 待完成 |

---

## 3. 实施时间表

### 3.1 建议的实施顺序

```
┌────────────────────────────────────────────────────────────────────────┐
│                     P1 函数实施时间表                                   │
├────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  第1周: 模块 A - User 统计功能 (read_usage, trim_usage)                 │
│  ├── Day 1-2: 添加 usage 字段到 impl 结构                              │
│  ├── Day 3-4: 实现 read_usage 简化版                                   │
│  └── Day 5: 实现 trim_usage 简化版                                     │
│                                                                         │
│  第2周: 模块 B + C - 组管理和 MFA                                       │
│  ├── Day 1-2: 实现 list_groups                                        │
│  └── Day 3-5: 实现 verify_mfa (简化版)                                │
│                                                                         │
│  第3周: 模块 D - 用户查询功能 (RADOS)                                  │
│  ├── Day 1-2: 实现 get_user_by_access_key                              │
│  ├── Day 3: 实现 get_user_by_email                                    │
│  └── Day 4-5: 实现 get_user_by_swift                                   │
│                                                                         │
│  第4周: 模块 E - POSIX 用户持久化                                       │
│  ├── Day 1-2: 实现辅助函数                                             │
│  ├── Day 3: 实现 load/store/remove                                    │
│  └── Day 4-5: 实现 read_attrs                                         │
│                                                                         │
│  第5周: 模块 F - ACL 功能                                              │
│  ├── Day 1-2: 定义 ACL 数据结构                                        │
│  └── Day 3-5: 实现 ACL 获取/设置函数                                   │
│                                                                         │
│  第6周: 测试和优化                                                    │
│  ├── Day 1-3: 编写测试用例                                            │
│  └── Day 4-5: 性能和内存测试                                           │
│                                                                         │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.2 每周工作量估算

| 周次 | 模块 | 预计代码量 | 复杂度 |
|------|------|------------|--------|
| 第1周 | 模块 A: User 统计 | ~150 行 | 中 |
| 第2周 | 模块 B+C: 组管理/MFA | ~100 行 | 低-中 |
| 第3周 | 模块 D: 用户查询 | ~350 行 | 中 |
| 第4周 | 模块 E: POSIX 持久化 | ~200 行 | 中 |
| 第5周 | 模块 F: ACL | ~250 行 | 中 |
| 第6周 | 测试和优化 | - | - |
| **总计** | | **~1050 行** | |

---

## 4. 依赖关系

### 4.1 内部依赖

```
依赖关系图:
                                    ┌─────────────┐
                                    │  类型定义   │
                                    │(rgw_sal_   │
                                    │ types.h)    │
                                    └──────┬──────┘
                                           │
                    ┌──────────────────────┼──────────────────────┐
                    │                      │                      │
                    ▼                      ▼                      ▼
           ┌────────────────┐     ┌────────────────┐     ┌────────────────┐
           │   模块 A       │     │   模块 B       │     │   模块 C       │
           │  User 统计    │     │  组管理       │     │  MFA          │
           │(usage fields) │     │(groups array) │     │(verify func)  │
           └───────┬────────┘     └────────────────┘     └────────────────┘
                   │
                   │ 依赖于:
                   │  - rados_user_impl_t
                   │  - rgw_sal_usage_info_t
                   ▼
           ┌────────────────────────────────────────────────┐
           │              模块 D: 用户查询                  │
           │    (get_user_by_access_key/email/swift)        │
           │                                               │
           │    依赖于:                                     │
           │    - rados_driver_impl_t                     │
           │    - RADOS OMAP API                          │
           └──────────────────────┬─────────────────────────┘
                                  │
                                  │ 依赖于:
                                  │  - 用户数据存储
                                  ▼
           ┌────────────────────────────────────────────────┐
           │              模块 E: POSIX 持久化              │
           │         (load/store/remove/read_attrs)          │
           │                                               │
           │    依赖于:                                     │
           │    - 文件系统操作                             │
           │    - JSON 序列化                             │
           └──────────────────────┬─────────────────────────┘
                                  │
                                  │ 依赖于:
                                  │  - ACL 数据结构
                                  ▼
           ┌────────────────────────────────────────────────┐
           │              模块 F: ACL 功能                   │
           │         (get_acl/set_acl)                      │
           │                                               │
           │    依赖于:                                     │
           │    - rgw_sal_acl_info_t                      │
           └────────────────────────────────────────────────┘
```

### 4.2 外部依赖

| 依赖项 | 用途 | 简化版是否需要 | 完整版是否需要 |
|--------|------|----------------|---------------|
| librados | RADOS OMAP 读写 | ❌ 否 | ✅ 是 |
| libsqlite3 | DBStore 查询 | ❌ 否 | ✅ 是 |
| liboath | TOTP 验证 | ❌ 否 | ⚠️ 可选 |
| 文件系统 | POSIX 存储 | ✅ 是 | ✅ 是 |

---

## 5. 测试计划

### 5.1 测试用例列表

| 模块 | 测试用例 | 预期结果 |
|------|----------|----------|
| 模块 A | `test_user_read_usage` | 返回 usage 信息 |
| 模块 A | `test_user_trim_usage` | 清理过期统计 |
| 模块 B | `test_user_list_groups_empty` | 返回空列表 |
| 模块 B | `test_user_list_groups_with_groups` | 返回组列表 |
| 模块 C | `test_user_verify_mfa_invalid` | 返回 PERMISSION_DENIED |
| 模块 D | `test_get_user_by_access_key` | 找到用户或返回 NOT_FOUND |
| 模块 D | `test_get_user_by_email` | 找到用户或返回 NOT_FOUND |
| 模块 E | `test_posix_user_load` | 从文件加载用户 |
| 模块 E | `test_posix_user_store` | 保存用户到文件 |
| 模块 E | `test_posix_user_remove` | 删除用户文件 |
| 模块 F | `test_bucket_set_acl` | ACL 被存储 |
| 模块 F | `test_bucket_get_acl` | 返回 ACL 或 NULL |

### 5.2 测试覆盖率目标

- **模块 A**: 100% (2 个函数)
- **模块 B**: 100% (1 个函数)
- **模块 C**: 50% (简化版)
- **模块 D**: 80% (3 个函数)
- **模块 E**: 100% (4 个函数)
- **模块 F**: 70% (简化版)

---

## 6. 风险和缓解措施

| 风险 | 影响 | 可能性 | 缓解措施 |
|------|------|--------|----------|
| RADOS OMAP API 使用不当 | 高 | 中 | 先实现简化版本，后续再添加 RADOS 支持 |
| JSON 解析库选择 | 中 | 低 | 使用标准 C 库手写解析或使用 cJSON 库 |
| 文件系统竞争条件 | 中 | 低 | 添加适当的文件锁 |
| 内存管理错误 | 高 | 中 | 使用 AddressSanitizer 检测 |
| ACL 格式兼容 | 中 | 低 | 保持简化版使用指针存储 |

---

## 7. 总结

### 7.1 P1 函数清单

| 序号 | 函数 | 驱动 | 优先级 | 估计代码量 |
|------|------|------|--------|------------|
| 1 | `read_usage` | RADOS/DBStore/POSIX | P1 | ~80 行 |
| 2 | `trim_usage` | RADOS/DBStore/POSIX | P1 | ~60 行 |
| 3 | `list_groups` | 所有驱动 | P1 | ~40 行 |
| 4 | `verify_mfa` | 所有驱动 | P1 | ~60 行 |
| 5 | `get_user_by_access_key` | RADOS | P1 | ~100 行 |
| 6 | `get_user_by_email` | RADOS | P1 | ~100 行 |
| 7 | `get_user_by_swift` | RADOS | P1 | ~80 行 |
| 8 | `get_user_by_access_key` | DBStore | P1 | ~100 行 |
| 9 | `get_user_by_email` | DBStore | P1 | ~100 行 |
| 10 | `get_user_by_swift` | DBStore | P1 | ~80 行 |
| 11 | `posix_user_load` | POSIX | P1 | ~50 行 |
| 12 | `posix_user_store` | POSIX | P1 | ~80 行 |
| 13 | `posix_user_remove` | POSIX | P1 | ~50 行 |
| 14 | `posix_user_read_attrs` | POSIX | P1 | ~30 行 |
| 15 | `bucket_get_acl` | 所有驱动 | P1 | ~30 行/驱动 |
| **总计** | **15+** | | | **~1230 行** |

### 7.2 下一步行动

1. ✅ 确认 P1 函数清单
2. ⬜ 实施模块 A (User 统计) - 建议第1周
3. ⬜ 实施模块 B+C (组管理/MFA) - 建议第2周
4. ⬜ 实施模块 D (用户查询) - 建议第3周
5. ⬜ 实施模块 E (POSIX 持久化) - 建议第4周
6. ⬜ 实施模块 F (ACL) - 建议第5周
7. ⬜ 测试和优化 - 建议第6周

---

**文档状态**: 草稿
**下一步**: 开始实施模块 A (User 统计功能)

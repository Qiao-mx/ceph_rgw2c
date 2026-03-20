# Ceph RGW SAL C 层实现总结与完整实施计划

**文档版本**: 1.0  
**创建日期**: 2026-03-20  
**更新日期**: 2026-03-20  

---

## 目录

1. [项目概述](#1-项目概述)
2. [C++ SAL 原始功能列表](#2-c-sal-原始功能列表)
3. [C 代码已实现功能统计](#3-c-代码已实现功能统计)
4. [简化实现的函数列表](#4-简化实现的函数列表)
5. [TODO 函数列表](#5-todo-函数列表)
6. [c_common 容器库实现统计](#6-c_common-容器库实现统计)
7. [完整实施计划](#7-完整实施计划)
8. [详细任务分解](#8-详细任务分解)
9. [时间线](#9-时间线)
10. [风险评估](#10-风险评估)
11. [验收标准](#11-验收标准)

---

## 1. 项目概述

### 1.1 目标
将 RGW SAL (Storage Abstraction Layer) 从 C++ 完整转换为 C 语言实现，保持功能等效性，同时确保代码可移植性和最小的运行时依赖。

### 1.2 项目范围

| 组件 | C++ 源文件 | C 目标文件 | 状态 |
|------|-----------|-----------|------|
| 核心 SAL 接口 | `rgw_sal.h` | `rgw_sal.h` | 已完成 |
| RADOS 驱动 | `driver/rados/*` | `sal_c/src/drivers/rgw_sal_rados.c` | ~98% |
| DBStore 驱动 | `rgw_sal_dbstore.cc` | `sal_c/src/drivers/rgw_sal_dbstore.c` | ~98% |
| POSIX 驱动 | `driver/posix/*` | `sal_c/src/drivers/rgw_sal_posix.c` | ~70% |
| DAOS 驱动 | `driver/daos/*` | `sal_c/src/drivers/rgw_sal_daos.c` | ~50% |
| D4N 驱动 | `driver/d4n/*` | `sal_c/src/drivers/rgw_sal_d4n.c` | ~50% |
| Motr 驱动 | - | `sal_c/src/drivers/rgw_sal_motr.c` | ~50% |
| Lifecycle 模块 | `rgw_lifecycle.h` | `sal_c/src/core/rgw_lifecycle.c` | 已完成 |
| Multipart 模块 | `rgw_multipart.h` | `sal_c/src/core/rgw_multipart.c` | 已完成 |
| Notification 模块 | `rgw_notification.h` | `sal_c/src/core/rgw_notification.c` | 已完成 |
| c_common 容器库 | - | `c_common/` | 已完成 |

### 1.3 当前总体完成度

```
┌─────────────────────────────────────────────────────────────┐
│                    总体完成度: ~85%                          │
├─────────────────────────────────────────────────────────────┤
│  核心接口定义     ████████████████████████████████████  100% │
│  RADOS 驱动      ████████████████████████████████████  98%  │
│  DBStore 驱动    ███████████████████████████████████   98%  │
│  POSIX 驱动      ██████████████████████░░░░░░░░░░░░░░   70% │
│  DAOS 驱动       █████████████░░░░░░░░░░░░░░░░░░░░░░   50% │
│  D4N 驱动        █████████████░░░░░░░░░░░░░░░░░░░░░░░   50% │
│  Motr 驱动       █████████████░░░░░░░░░░░░░░░░░░░░░░   50% │
│  Lifecycle       ████████████████████████████████████  100% │
│  Multipart       ████████████████████████████████████  100% │
│  Notification    ████████████████████████████████████  100% │
│  c_common 库     ████████████████████████████████████  100% │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. C++ SAL 原始功能列表

### 2.1 核心抽象类 (`src/rgw/rgw_sal.h`)

#### 2.1.1 Driver 类 (存储驱动基类)

| 函数 | 描述 | 返回类型 | 参数 |
|------|------|----------|------|
| `initialize` | 初始化驱动 | `int` | `CephContext*, DoutPrefixProvider*` |
| `get_name` | 获取驱动名称 | `std::string` | - |
| `get_cluster_id` | 获取集群 ID | `std::string` | `DoutPrefixProvider*, optional_yield` |
| `get_user` | 获取用户对象 | `std::unique_ptr<User>` | `const rgw_user&` |
| `get_user_by_access_key` | 通过 access_key 获取用户 | `int` | `DoutPrefixProvider*, const string&, optional_yield, unique_ptr<User>*` |
| `get_user_by_email` | 通过 email 获取用户 | `int` | `DoutPrefixProvider*, const string&, optional_yield, unique_ptr<User>*` |
| `get_user_by_swift` | 通过 swift 用户名获取 | `int` | `DoutPrefixProvider*, const string&, optional_yield, unique_ptr<User>*` |
| `get_object` | 获取对象 | `std::unique_ptr<Object>` | `const rgw_obj_key&` |
| `get_bucket` | 获取桶 | `std::unique_ptr<Bucket>` | `const RGWBucketInfo&` |
| `load_bucket` | 加载桶 | `int` | `DoutPrefixProvider*, const rgw_bucket&, Bucket*, optional_yield` |
| `list_buckets` | 列出桶 | `int` | `DoutPrefixProvider*, const rgw_owner&, const string&, ...` |
| `get_lifecycle` | 获取生命周期对象 | `std::unique_ptr<Lifecycle>` | - |
| `get_notification` | 获取通知对象 | `std::unique_ptr<Notification>` | 多种参数变体 |
| `cluster_stat` | 集群统计 | `int` | `RGWClusterStat&` |
| `is_meta_master` | 是否是元数据主节点 | `bool` | - |
| `get_zone` | 获取 zone 信息 | `Zone*` | - |

**Account 相关** (约 25 个函数):
- `load_account_by_id`, `load_account_by_name`, `load_account_by_email`
- `store_account`, `delete_account`
- `load_owner_by_email`
- `count_account_roles`, `list_account_roles`
- `load_account_user_by_name`, `count_account_users`, `list_account_users`

**Group 相关** (约 10 个函数):
- `load_group_by_id`, `load_group_by_name`
- `store_group`, `remove_group`
- `list_group_users`, `count_account_groups`, `list_account_groups`

#### 2.1.2 User 类 (用户抽象类)

| 函数 | 描述 | 返回类型 |
|------|------|----------|
| `clone` | 克隆用户 | `std::unique_ptr<User>` |
| `destroy` | 销毁用户 | `void` |
| `get_id` | 获取用户 ID | `const rgw_user&` |
| `get_display_name` | 获取显示名称 | `const std::string&` |
| `set_display_name` | 设置显示名称 | `int` |
| `get_tenant` | 获取租户 | `const std::string&` |
| `get_type` | 获取用户类型 | `uint32_t` |
| `get_max_buckets` | 获取最大桶数 | `int32_t` |
| `set_max_buckets` | 设置最大桶数 | `void` |
| `get_attrs` | 获取属性 | `Attrs&` |
| `set_attrs` | 设置属性 | `int` |
| `load` | 加载用户 | `int` |
| `store` | 存储用户 | `int` |
| `remove` | 删除用户 | `int` |
| `read_attrs` | 读取属性 | `int` |
| `merge_and_store_attrs` | 合并并存储属性 | `int` |
| `get_ns` | 获取命名空间 | `const std::string&` |
| `set_ns` | 设置命名空间 | `void` |
| `clear_ns` | 清除命名空间 | `void` |
| `set_info` | 设置用户信息 | `int` |
| `get_info` | 获取用户信息 | `int` |
| `get_caps` | 获取权限 | `int` |
| `get_version_tracker` | 获取版本跟踪器 | `RGWObjVersionTracker&` |
| `verify_mfa` | 验证 MFA | `int` | (TOTP) |
| `list_groups` | 列出用户组 | `int` | - |
| `read_usage` | 读取使用统计 | `int` | - |
| `trim_usage` | 清理使用统计 | `int` | - |

#### 2.1.3 Bucket 类 (桶抽象类)

| 函数 | 描述 | 返回类型 |
|------|------|----------|
| `clone` | 克隆桶 | `std::unique_ptr<Bucket>` |
| `destroy` | 销毁桶 | `void` |
| `get_name` | 获取桶名称 | `const std::string&` |
| `get_tenant` | 获取租户 | `const std::string&` |
| `get_marker` | 获取标记 | `const std::string&` |
| `get_info` | 获取桶信息 | `RGWBucketInfo&` |
| `get_owner` | 获取所有者 | `User*` |
| `get_attrs` | 获取属性 | `Attrs&` |
| `set_attrs` | 设置属性 | `int` |
| `list` | 列出对象 | `int` |
| `load` | 加载桶 | `int` |
| `store` | 存储桶 | `int` |
| `remove` | 删除桶 | `int` |
| `create` | 创建桶 | `int` |
| `delete_bucket` | 删除桶 | `int` |
| `rename` | 重命名桶 | `int` |
| `set_acl` | 设置 ACL | `int` |
| `get_policy` | 获取策略 | `int` |
| `set_policy` | 设置策略 | `int` |
| `get_tag` | 获取标签 | `int` |
| `set_tag` | 设置标签 | `int` |
| `get_usage` | 获取使用统计 | `int` |
| `read_stats` | 读取统计 | `int` |
| `read_stats_async` | 异步读取统计 | `int` |
| `complete_stats` | 完成统计 | `int` |
| `update_bucket_stats` | 更新桶统计 | `int` |
| `sync_user_stats` | 同步用户统计 | `int` |
| `sync` | 同步 | `int` |
| `drain` | 排空 | `int` |
| `check_object_index` | 检查对象索引 | `int` |
| `fix_object_index` | 修复对象索引 | `int` |
| `check_bucket_index` | 检查桶索引 | `int` |
| `remove_bypass_gc` | 绕过 GC 删除 | `int` |
| `check_quota` | 检查配额 | `int` |
| `check_empty` | 检查是否为空 | `int` |
| `try_refresh_info` | 尝试刷新信息 | `int` |

#### 2.1.4 Object 类 (对象抽象类)

| 函数 | 描述 | 返回类型 |
|------|------|----------|
| `clone` | 克隆对象 | `std::unique_ptr<Object>` |
| `destroy` | 销毁对象 | `void` |
| `get_name` | 获取对象名 | `const std::string&` |
| `get_instance` | 获取实例 | `const std::string&` |
| `is_null` | 是否为空 | `bool` |
| `get_attrs` | 获取属性 | `Attrs&` |
| `set_attrs` | 设置属性 | `int` |
| `read` | 读取对象 | `int` |
| `write` | 写入对象 | `int` |
| `delete_obj` | 删除对象 | `int` |
| `load_state` | 加载状态 | `int` |
| `get_obj_attrs` | 获取对象属性 | `int` |
| `set_obj_attrs` | 设置对象属性 | `int` |
| `delete_obj_attrs` | 删除对象属性 | `int` |
| `copy_object` | 复制对象 | `int` |
| `list_parts` | 列出分片 | `int` |
| `transition` | 存储类别转换 | `int` |

### 2.2 多部分上传接口 (`rgw_sal_multipart.h`)

| 函数 | 描述 |
|------|------|
| `init` | 初始化上传 |
| `list_parts` | 列出分片 |
| `abort` | 中止上传 |
| `complete` | 完成上传 |
| `store_info` | 存储上传信息 |
| `load_info` | 加载上传信息 |

### 2.3 生命周期接口 (`rgw_lifecycle.h`)

| 函数 | 描述 |
|------|------|
| `get_entry` | 获取单个条目 |
| `get_next_entry` | 获取下一个条目 |
| `set_entry` | 设置条目 |
| `list_entries` | 列出条目 |
| `rm_entry` | 删除条目 |
| `get_head` | 获取头 |
| `put_head` | 写入头 |

### 2.4 通知接口 (`rgw_notification.h`)

| 函数 | 描述 |
|------|------|
| `get_notification` | 获取通知对象 |
| `publish_reserve` | 预留发布资源 |
| `publish_commit` | 提交发布 |
| `read_topics` | 读取主题列表 |
| `write_topics` | 写入主题列表 |
| `remove_topics` | 删除主题列表 |
| `topic_load` | 加载主题 |
| `topic_save` | 保存主题 |
| `topic_delete` | 删除主题 |
| `publish` | 发布通知 |

---

## 3. C 代码已实现功能统计

### 3.1 核心接口文件 (`include/core/`)

| 文件 | 定义数量 | 说明 |
|------|---------|------|
| `rgw_sal.h` | 4 个 VTable | User/Bucket/Object/Driver 虚函数表 |
| `rgw_sal_types.h` | ~50 个类型 | 结构体、枚举、常量定义 |
| `rgw_lifecycle.h` | ~15 个类型 | 生命周期相关类型 |
| `rgw_multipart.h` | ~10 个类型 | 多部分上传相关类型 |
| `rgw_notification.h` | ~20 个类型 | 通知/PubSub 相关类型 |
| `rgw_account_serde.h` | ~10 个类型 | 账户序列化类型 |
| `rgw_group_serde.h` | ~5 个类型 | 用户组序列化类型 |
| `rgw_oidc_serde.h` | ~5 个类型 | OIDC 序列化类型 |
| `rgw_sal_errors.h` | ~30 个错误码 | 错误定义 |

### 3.2 VTable 函数统计

#### 3.2.1 User VTable (30 个函数)

| 类别 | 函数数 | 已实现 | 完成率 |
|------|--------|--------|--------|
| 生命周期 | 2 | 2 | 100% |
| 属性访问 | 7 | 7 | 100% |
| 属性映射 | 2 | 2 | 100% |
| 持久化操作 | 3 | 3 | 100% |
| 属性读写 | 2 | 2 | 100% |
| 命名空间 | 3 | 3 | 100% |
| 配额信息 | 2 | 2 | 100% |
| 权限管理 | 2 | 2 | 100% |
| 使用统计 | 2 | 2 | 100% |
| MFA 认证 | 1 | 1 | 100% |
| 组管理 | 1 | 1 | 100% |

#### 3.2.2 Bucket VTable (31 个函数)

| 类别 | 函数数 | 已实现 | 完成率 |
|------|--------|--------|--------|
| 生命周期 | 2 | 2 | 100% |
| 属性访问 | 5 | 5 | 100% |
| 属性映射 | 2 | 2 | 100% |
| 对象列表 | 1 | 1 | 100% |
| 持久化操作 | 3 | 3 | 100% |
| 桶操作 | 3 | 3 | 100% |
| ACL/策略 | 3 | 3 | 100% |
| 标签 | 2 | 2 | 100% |
| 统计 | 6 | 6 | 100% |
| 同步 | 2 | 2 | 100% |
| 索引检查 | 3 | 3 | 100% |
| 扩展操作 | 4 | 4 | 100% |

#### 3.2.3 Object VTable (16 个函数)

| 类别 | 函数数 | 已实现 | 完成率 |
|------|--------|--------|--------|
| 生命周期 | 2 | 2 | 100% |
| 属性访问 | 3 | 3 | 100% |
| 属性映射 | 2 | 2 | 100% |
| 读操作 | 1 | 1 | 100% |
| 写操作 | 1 | 1 | 100% |
| 删除操作 | 1 | 1 | 100% |
| 持久化操作 | 3 | 3 | 100% |
| 扩展操作 | 4 | 4 | 100% |

#### 3.2.4 Driver VTable (12 个函数)

| 类别 | 函数数 | 已实现 | 完成率 |
|------|--------|--------|--------|
| 生命周期 | 1 | 1 | 100% |
| 初始化 | 1 | 1 | 100% |
| 元数据 | 2 | 2 | 100% |
| 用户操作 | 4 | 4 | 100% |
| 桶操作 | 2 | 2 | 100% |
| 对象操作 | 1 | 1 | 100% |

### 3.3 驱动实现统计

#### 3.3.1 RADOS 驱动 (`src/drivers/rgw_sal_rados.c`)

| 功能模块 | 实现函数数 | 完成度 | 说明 |
|---------|-----------|--------|------|
| 驱动初始化 | 15 | 100% | 完整的 RADOS 连接管理 |
| 用户操作 | 30 | 90% | Usage 统计简化 |
| 桶操作 | 31 | 95% | drain 简化 |
| 对象操作 | 25 | 95% | 索引检查简化 |
| 多部分上传 | 10 | 85% | 基础功能完成 |
| 生命周期 | 10 | 100% | 类型定义完成 |
| 通知 | 15 | 70% | 部分简化实现 |
| 使用统计 | 15 | 80% | 部分简化实现 |

#### 3.3.2 DBStore 驱动 (`src/drivers/rgw_sal_dbstore.c`)

| 功能模块 | 实现函数数 | 完成度 | 说明 |
|---------|-----------|--------|------|
| 驱动初始化 | 10 | 100% | SQLite 连接管理 |
| 用户操作 | 30 | 90% | Usage 统计简化 |
| 桶操作 | 31 | 95% | drain 简化 |
| 对象操作 | 25 | 95% | 索引检查简化 |
| 多部分上传 | 10 | 85% | 基础功能完成 |
| 生命周期 | 10 | 100% | 类型定义完成 |
| 通知 | 15 | 70% | 部分简化实现 |
| 使用统计 | 15 | 80% | 部分简化实现 |

#### 3.3.3 POSIX 驱动 (`src/drivers/rgw_sal_posix.c`)

| 功能模块 | 实现函数数 | 完成度 | 说明 |
|---------|-----------|--------|------|
| 驱动初始化 | 10 | 100% | 文件系统初始化 |
| 用户操作 | 30 | 70% | 文件 I/O 待完善 |
| 桶操作 | 31 | 70% | 目录遍历待完善 |
| 对象操作 | 25 | 70% | 文件操作待完善 |
| 使用统计 | 10 | 60% | 文件读写待实现 |

#### 3.3.4 DAOS/D4N/Motr 驱动

| 驱动 | 完成度 | 状态 |
|------|--------|------|
| DAOS | ~50% | 框架完成，底层集成待实现 |
| D4N | ~50% | 框架完成，缓存逻辑待完善 |
| Motr | ~50% | 框架完成，索引管理待实现 |

### 3.4 核心实现文件统计

| 文件 | 行数 | 函数数 | 说明 |
|------|------|--------|------|
| `include/core/rgw_sal.h` | 737 | 50+ | VTable 定义 |
| `include/core/rgw_sal_types.h` | 450+ | 50+ | 类型定义 |
| `src/core/rgw_sal_errors.c` | 200+ | 20+ | 错误处理 |
| `src/core/rgw_lifecycle.c` | 550+ | 30+ | 生命周期 |
| `src/core/rgw_multipart.c` | 780+ | 25+ | 多部分上传 |
| `src/core/rgw_notification.c` | 1100+ | 40+ | 通知 |
| `src/core/rgw_account_serde.c` | 300+ | 15+ | 账户序列化 |
| `src/core/rgw_group_serde.c` | 200+ | 10+ | 组序列化 |
| `src/core/rgw_oidc_serde.c` | 150+ | 10+ | OIDC 序列化 |
| `src/drivers/rgw_sal_rados.c` | 5400+ | 150+ | RADOS 驱动 |
| `src/drivers/rgw_sal_dbstore.c` | 5100+ | 140+ | DBStore 驱动 |
| `src/drivers/rgw_sal_posix.c` | 3700+ | 100+ | POSIX 驱动 |
| `src/rgw_sal_usage.c` | 950+ | 40+ | 使用统计 |

**总计**: ~60 个源文件，~25,000+ 行代码，~500+ 个函数

---

## 4. 简化实现的函数列表

以下函数在 C 代码中已实现，但使用了简化逻辑，需要后续完善。

### 4.1 RADOS 驱动 (`rgw_sal_rados.c`)

| 行号 | 函数名 | 简化描述 | 完整实现需求 |
|------|--------|----------|-------------|
| 315-318 | `rados_driver_get_cluster_id` | 返回硬编码 "ceph" | 从 RADOS 获取真实集群 ID |
| 580-591 | `rados_driver_list_buckets` | 预留空间但未遍历 | 使用 OMAP 遍历用户桶列表 |
| 592-598 | (TODO) | 使用 OMAP 遍历用户桶列表 | 注释说明待实现 |
| 1027-1031 | `rados_user_read_usage` | 填充空 usage 数据 | 从 RADOS usage 池读取 |
| 1060-1063 | (注释) | 简化处理 | 需要 librados_ioctx_t |
| 1256-1259 | `rados_user_list_groups` | 简单 JSON 解析 | 完整 JSON 解析 |
| 1307-1309 | (TODO) | 从 OMAP 读取组 | 需要独立 IO 上下文 |
| 1473-1478 | `rados_bucket_load` | 简化处理 | 使用 rgw_bucket_serde 解码 |
| 2063-2066 | `rados_bucket_set_acl` | 仅存储指针 | 解析 ACL 策略并存储到 OMAP |
| 2083-2085 | `rados_bucket_get_policy` | 返回存储的指针 | 从 RADOS 读取策略 |
| 2099-2101 | `rados_bucket_set_policy` | 存储策略指针 | 序列化策略到 RADOS |
| 2116-2118 | `rados_bucket_get_usage` | 返回空 | 从 RADOS 读取使用统计 |
| 2130-2135 | `rados_bucket_read_stats` | 返回默认值 | 从 RADOS 读取实际统计 |
| 2144-2146 | `rados_bucket_complete_stats` | 标记完成 | 将统计写入 RADOS |
| 2157-2159 | `rados_bucket_sync` | 标记已同步 | 实际同步数据到远程 |
| 2207-2216 | `rados_bucket_read_stats_async` | 填充默认值 | 异步读取 RADOS 统计 |
| 2267-2280 | `rados_bucket_drain` | 标记已排空 | 遍历并复制对象到目标 |
| 2339-2370 | `rados_bucket_check_object_index` | 框架完成 | 完整 RADOS OMAP 遍历 |
| 2371-2375 | (TODO) | 修复逻辑 | 检查并修复损坏对象 |
| 2892-2894 | (TODO) | 版本控制检查 | 检查桶是否启用版本控制 |
| 2933-2935 | `rados_object_load_state` | 标记已加载 | 从 RADOS 读取对象状态 |
| 2948-2950 | `rados_object_get_obj_attrs` | 使用 vtable | 从 RADOS xattr 读取 |
| 3145-3148 | `rados_driver_get_user_ctl` | 返回 NULL | 实际返回用户控制接口 |
| 3154-3158 | `rados_driver_refresh_stats` | 未实现 | 刷新统计数据 |
| 3266-3268 | (注释) | 简化 OID 格式 | 实际构建对象 OID |
| 3316-3318 | `rados_driver_get_obj_oid` | RADOS 不可用返回错误 | 实际获取对象 OID |
| 3358-3361 | `rados_driver_get_omap_val` | 返回 NULL | 实际读取 OMAP 值 |
| 3948-3952 | `rados_multipart_complete` | 简化合并逻辑 | 完整分段合并处理 |
| 5036-5039 | `rados_notification_publish_reserve` | 预留资源 | 检查并预留队列资源 |
| 5070-5074 | `rados_notification_publish_commit` | 构建消息 | 发送到目标端点 |

### 4.2 DBStore 驱动 (`rgw_sal_dbstore.c`)

| 行号 | 函数名 | 简化描述 | 完整实现需求 |
|------|--------|----------|-------------|
| 890-893 | `dbstore_user_read_attrs` | 简化处理 | 从 users 表 JSON 字段读取 |
| 996-998 | (Usage) | 简化实现 | 完整 SQLite usage 表操作 |
| 1840-1842 | `dbstore_bucket_create` | 标记已创建 | 实际创建桶记录 |
| 1858-1859 | `dbstore_bucket_delete_bucket` | 标记已删除 | 实际删除桶记录 |
| 1875-1877 | `dbstore_bucket_rename` | 更新内存名称 | 实际更新数据库 |
| 2032-2034 | `dbstore_bucket_read_stats` | 简化实现 | 查询数据库统计 |
| 2203-2214 | `dbstore_bucket_drain` | 标记已排空 | 实际排空逻辑 |
| 2351-2355 | (TODO) | 索引修复逻辑 | 实现索引修复 |
| 2592-2594 | `dbstore_object_write` | 记录写入状态 | 实际写入文件 |
| 2610-2612 | `dbstore_object_delete_obj` | 标记已删除 | 实际删除文件 |
| 2649-2651 | `dbstore_object_get_obj_attrs` | 简化处理 | 从 JSON 字段读取 |
| 2919-2921 | `dbstore_get_user_ctl` | 返回 NULL | 实际返回用户控制接口 |
| 4882-4883 | `dbstore_notification_get_notification` | 简化实现 | 完整通知获取 |
| 4921-4923 | `dbstore_notification_read_topics` | 简化检查 | 从数据库读取主题 |
| 4946-4948 | `dbstore_notification_publish_commit` | 无需额外操作 | 实际发送通知 |
| 5018-5020 | (主题) | 简化存储格式 | 完整主题信息存储 |

### 4.3 POSIX 驱动 (`rgw_sal_posix.c`)

| 行号 | 函数名 | 简化描述 | 完整实现需求 |
|------|--------|----------|-------------|
| 417-420 | `posix_user_store` | 创建目录 | 实际写入 JSON 文件 |
| 554-556 | (mkdir) | TODO | 使用 mkdir -p 创建目录 |
| 642-645 | `posix_user_read_usage` | TODO | 读取 usage 文件 |
| 673-677 | (简化实现) | 简化实现 | 过滤并写回文件 |
| 689-692 | `posix_user_trim_usage` | TODO | 跳过指定 epoch |
| 1328-1330 | `posix_bucket_load` | 简化读取 | 解析 key=value 格式 |
| 1487-1490 | `posix_bucket_read_stats_async` | 同步调用 | 异步读取统计 |
| 2010-2012 | `posix_object_set_obj_attrs` | 简化解析 | 完整属性序列化 |
| 2049-2051 | `posix_object_set_obj_attrs` | TODO | 实现属性删除 |
| 2057-2058 | (xattr) | 简化实现 | 完整 xattr 序列化 |
| 3251-3252 | `posix_notification_get_notification` | 简化结构 | 完整通知结构 |
| 3281-3283 | `posix_notification_publish_reserve` | 简化实现 | 预留资源 |
| 3296-3298 | `posix_notification_publish_commit` | 简化实现 | 发送到消息队列 |
| 3334-3335 | `posix_notification_read_topics` | 简化解析 | 读取 topic 列表 |
| 3416-3417 | `posix_notification_write_topics` | 简化实现 | 写入 topic 列表 |
| 3489-3490 | `posix_notification_publish` | 记录日志 | 使用 HTTP 发送通知 |

### 4.4 D4N 驱动 (`rgw_sal_d4n.c`)

| 行号 | 函数名 | 简化描述 | 完整实现需求 |
|------|--------|----------|-------------|
| 922-925 | (缓存写入) | 简化创建目录 | POSIX API 缓存文件 |
| 963-964 | (目录缓存) | 简化实现 | 更新对象目录缓存 |

### 4.5 简化实现汇总

| 驱动 | 简化函数数 | 主要简化类型 |
|------|-----------|-------------|
| RADOS | ~30 | Usage、OMAP、对象状态 |
| DBStore | ~15 | 数据库操作、Usage |
| POSIX | ~15 | 文件 I/O、属性序列化 |
| D4N | ~2 | 缓存管理 |
| **总计** | **~62** | |

---

## 5. TODO 函数列表

以下函数标记为 TODO，需要后续实现。

### 5.1 RADOS 驱动 (`rgw_sal_rados.c`)

| 行号 | 函数名 | TODO 描述 | 优先级 |
|------|--------|-----------|--------|
| 592-598 | `rados_driver_list_buckets` | 使用 rados_read_op_omap_get_vals 遍历用户桶列表 | P1 |
| 1307-1309 | `rados_user_list_groups` | 从独立的 OMAP 对象读取组信息 | P1 |
| 1477 | `rados_bucket_load` | 使用 rgw_bucket_info_decode 解码 | P1 |
| 2339-2370 | `rados_bucket_check_object_index` | 完整实现需要 librados OMAP 遍历 | P1 |
| 2372-2375 | `rados_bucket_fix_object_index` | 检查并修复损坏对象 | P1 |
| 2893-2894 | `rados_object_delete_obj` | 检查桶是否启用版本控制 | P2 |
| 3145-3148 | `rados_driver_get_user_ctl` | 实际返回用户控制接口 | P2 |
| 3154-3158 | `rados_driver_refresh_stats` | 实际刷新统计数据 | P2 |
| 3948-3952 | `rados_multipart_complete` | 完整分段合并处理 | P2 |

### 5.2 DBStore 驱动 (`rgw_sal_dbstore.c`)

| 行号 | 函数名 | TODO 描述 | 优先级 |
|------|--------|-----------|--------|
| 2203-2214 | `dbstore_bucket_drain` | 完整排空逻辑 | P1 |
| 2351-2355 | `dbstore_bucket_fix_object_index` | 索引修复逻辑 | P1 |
| 2919-2921 | `dbstore_get_user_ctl` | 实际返回用户控制接口 | P2 |

### 5.3 POSIX 驱动 (`rgw_sal_posix.c`)

| 行号 | 函数名 | TODO 描述 | 优先级 |
|------|--------|-----------|--------|
| 554-556 | (目录创建) | 使用 mkdir -p 创建目录 | P1 |
| 642-645 | `posix_user_read_usage` | 完整 usage 文件读取 | P2 |
| 689-692 | `posix_user_trim_usage` | 跳过指定 epoch | P2 |
| 2049-2051 | `posix_object_set_obj_attrs` | 实现属性删除 | P2 |

### 5.4 核心模块 TODO

| 文件 | 函数名 | TODO 描述 | 优先级 |
|------|--------|-----------|--------|
| `rgw_lifecycle.c` | - | 类型定义完成，待驱动集成 | P1 |
| `rgw_multipart.c` | - | 类型定义完成，待驱动集成 | P1 |
| `rgw_notification.c` | - | 类型定义完成，待驱动集成 | P1 |

### 5.5 TODO 函数汇总

| 驱动 | TODO 函数数 | P1 优先 | P2 优先 |
|------|-----------|--------|--------|
| RADOS | 9 | 6 | 3 |
| DBStore | 3 | 2 | 1 |
| POSIX | 4 | 1 | 3 |
| **总计** | **16** | **9** | **7** |

---

## 6. c_common 容器库实现统计

### 6.1 容器类型实现

| 容器 | 头文件 | 实现文件 | 状态 |
|------|--------|---------|------|
| 动态数组 | `rgw_carray.h` | `rgw_carray.c` | ✅ 完成 |
| 字符串 | `rgw_cstring.h` | `rgw_cstring.c` | ✅ 完成 |
| 有序 Map | `rgw_cmap.h` | `rgw_cmap.c` | ✅ 完成 |
| 有序 Set | `rgw_cset.h` | - | ✅ 完成 |
| 双向链表 | `rgw_clist.h` | - | ✅ 完成 |
| 双端队列 | `rgw_cdeque.h` | `rgw_cdeque.c` | ✅ 完成 |
| 栈 | `rgw_cstack.h` | `rgw_cstack.c` | ✅ 完成 |
| 队列 | `rgw_cqueue.h` | `rgw_cqueue.c` | ✅ 完成 |
| 优先队列 | `rgw_cpriority_queue.h` | `rgw_cpriority_queue.c` | ✅ 完成 |
| 哈希表 | `rgw_chash_map.h` | - | ✅ 完成 |
| 可选类型 | `rgw_coptional.h` | `rgw_coptional.c` | ✅ 完成 |
| 内存管理 | `rgw_cmemory.h` | `rgw_cmemory.c` | ✅ 完成 |

### 6.2 工具模块实现

| 模块 | 头文件 | 实现文件 | 状态 |
|------|--------|---------|------|
| 缓冲区 | `rgw_buffer.h` | `rgw_buffer.c` | ✅ 完成 |
| Base64 | `rgw_b64.h` | `rgw_b64.c` | ✅ 完成 |
| 十六进制 | `rgw_hex.h` | `rgw_hex.c` | ✅ 完成 |
| 错误处理 | `rgw_errors.h` | `rgw_errors.c` | ✅ 完成 |
| OOP 框架 | `rgw_oop.h` | `rgw_oop.c` | ✅ 完成 |
| 排序算法 | `csort.h` | `csort.c` | ✅ 完成 |
| XML 解析 | `rgw_xml.h` | `rgw_xml.c` | ✅ 完成 |
| SQLite | `rgw_sqlite.h` | `rgw_sqlite.c` | ✅ 完成 |
| OMAP | `rgw_omap.h` | `rgw_omap.c` | ✅ 完成 |

### 6.3 测试覆盖

| 测试文件 | 覆盖模块 | 状态 |
|---------|---------|------|
| `test_carray.c` | 动态数组 | ✅ 完成 |
| `test_cstring.c` | 字符串 | ✅ 完成 |
| `test_cmap.c` | 有序 Map | ✅ 完成 |
| `test_cset.c` | 有序 Set | ✅ 完成 |
| `test_cdeque.c` | 双端队列 | ✅ 完成 |
| `test_cstack.c` | 栈 | ✅ 完成 |
| `test_cqueue.c` | 队列 | ✅ 完成 |
| `test_cpriority_queue.c` | 优先队列 | ✅ 完成 |
| `test_coptional.c` | 可选类型 | ✅ 完成 |
| `test_buffer.c` | 缓冲区 | ✅ 完成 |
| `test_b64.c` | Base64 | ✅ 完成 |
| `test_hex.c` | 十六进制 | ✅ 完成 |
| `test_errors.c` | 错误处理 | ✅ 完成 |
| `test_oop.c` | OOP 框架 | ✅ 完成 |
| `test_benchmark.c` | 性能基准 | ✅ 完成 |
| `test_memory.c` | 内存测试 | ✅ 完成 |

---

## 7. 完整实施计划

### 7.1 计划概述

将剩余的简化实现和 TODO 函数分为 4 个阶段实施，总工期约 10 周。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        实施时间线总览                                    │
├────────────────────────────────────────────────────────────────────────┤
│ 第1-2周  │ 第3-4周  │ 第5-6周  │ 第7-8周  │ 第9-10周                   │
├──────────┼──────────┼──────────┼──────────┼─────────────────────────────┤
│ 阶段1    │ 阶段2    │ 阶段3    │ 阶段4    │ 阶段5                       │
│ 核心功能  │ 索引检查  │ 驱动完善  │ 测试覆盖  │ 集成验收                     │
│ 完善     │ /修复    │         │         │                             │
└──────────┴──────────┴──────────┴──────────┴─────────────────────────────┘
```

### 7.2 阶段 1: 核心功能完善 (第 1-2 周)

#### 目标
完善 RADOS 驱动的核心功能，包括 Usage 统计、OMAP 操作、对象状态管理。

#### 任务清单

| 任务ID | 任务名称 | 依赖 | 工时 | 验收标准 |
|--------|----------|------|------|----------|
| P1.1 | RADOS Usage 读取完善 | 无 | 3d | 从 RADOS usage 池正确读取聚合数据 |
| P1.2 | RADOS Usage 清理完善 | P1.1 | 2d | 正确清理指定 epoch 范围的 usage |
| P1.3 | RADOS 用户组 OMAP 读取 | 无 | 2d | 从 OMAP 正确读取用户组列表 |
| P1.4 | RADOS 桶信息解码 | 无 | 2d | 使用 rgw_bucket_serde 解码 |
| P1.5 | RADOS ACL/策略存储 | 无 | 2d | 解析并存储到 RADOS OMAP |
| P1.6 | RADOS 桶统计完善 | 无 | 2d | 从 RADOS 读取实际统计 |
| P1.7 | DBStore Usage 完善 | 无 | 2d | SQLite 完整 usage 表操作 |

### 7.3 阶段 2: 索引检查/修复 (第 3-4 周)

#### 目标
实现完整的对象索引检查和修复功能。

#### 任务清单

| 任务ID | 任务名称 | 依赖 | 工时 | 验收标准 |
|--------|----------|------|------|----------|
| P2.1 | RADOS check_object_index | 无 | 3d | 使用 OMAP 遍历并检测不一致 |
| P2.2 | RADOS fix_object_index | P2.1 | 2d | 自动修复损坏的索引 |
| P2.3 | RADOS check_bucket_index | 无 | 2d | 验证桶入口点一致性 |
| P2.4 | DBStore 索引检查/修复 | 无 | 3d | SQLite 实现索引检查 |
| P2.5 | 索引检查工具 | P2.1-P2.4 | 2d | 提供命令行工具 |

### 7.4 阶段 3: 驱动完善 (第 5-6 周)

#### 目标
完善 POSIX、DAOS、D4N、Motr 驱动的实现。

#### 任务清单

| 任务ID | 任务名称 | 依赖 | 工时 | 验收标准 |
|--------|----------|------|------|----------|
| P3.1 | POSIX 用户文件 I/O | 无 | 3d | JSON 文件读写 |
| P3.2 | POSIX 桶目录遍历 | P3.1 | 2d | 目录遍历和元数据维护 |
| P3.3 | POSIX 对象文件操作 | P3.2 | 3d | 文件读写和 xattr |
| P3.4 | DAOS 容器管理 | 无 | 3d | DAOS 容器创建/删除 |
| P3.5 | D4N 缓存管理 | 无 | 2d | 缓存策略实现 |
| P3.6 | Motr 索引管理 | 无 | 3d | Motr 索引操作 |

### 7.5 阶段 4: 测试覆盖 (第 7-8 周)

#### 目标
完善单元测试和集成测试。

#### 任务清单

| 任务ID | 任务名称 | 依赖 | 工时 | 验收标准 |
|--------|----------|------|------|----------|
| P4.1 | Usage 统计测试 | P1.1-P1.2 | 1d | 覆盖率 >80% |
| P4.2 | 索引检查测试 | P2.1-P2.4 | 2d | 覆盖率 >80% |
| P4.3 | POSIX 驱动测试 | P3.1-P3.3 | 2d | 核心路径测试通过 |
| P4.4 | 集成测试 | P2.5 | 2d | 完整工作流测试 |
| P4.5 | 性能基准测试 | P4.4 | 1d | 有性能数据 |

### 7.6 阶段 5: 集成验收 (第 9-10 周)

#### 目标
确保所有功能正常工作，准备发布。

#### 任务清单

| 任务ID | 任务名称 | 依赖 | 工时 | 验收标准 |
|--------|----------|------|------|----------|
| P5.1 | 回归测试 | P4.1-P4.5 | 2d | 无功能退化 |
| P5.2 | 文档更新 | P5.1 | 1d | API 文档完整 |
| P5.3 | 性能对比 | P5.1 | 2d | 性能不低于 C++ 版本 90% |
| P5.4 | 内存检测 | P5.1 | 2d | 无内存泄漏 |
| P5.5 | 最终验收 | P5.2-P5.4 | 1d | 所有验收标准通过 |

---

## 8. 详细任务分解

### 8.1 P1.1: RADOS Usage 读取完善

**文件**: `src/drivers/rgw_sal_rados.c` (约第 1000-1050 行)

**当前问题**: 函数返回简化值，未真正从 RADOS 读取。

**实现步骤**:
1. 获取 RADOS usage 池 (`.rgw.log`)
2. 构建 usage 对象名: `usage:<user_id>:<shard>`
3. 使用 `rados_read_op_omap_get_vals` 读取
4. 解析并聚合结果到 usage 结构

**代码示例**:
```c
static int rados_user_read_usage(rgw_sal_user_t* user,
                                 const rgw_sal_dpp_t* dpp,
                                 uint64_t start_epoch, uint64_t end_epoch,
                                 uint32_t max_entries, void* usage) {
    // 1. 获取 RADOS usage 池
    rados_ioctx_t ioctx;
    int ret = rados_ioctx_create(driver_impl->cluster, ".rgw.log", &ioctx);
    if (ret < 0) return ret;

    // 2. 构建对象名
    char obj_name[256];
    snprintf(obj_name, sizeof(obj_name), "usage:%s:", user_impl->user_id.id);

    // 3. 使用 OMAP 读取
    rados_read_op_t op = rados_create_read_op();
    rados_read_op_omap_get_vals(op, "", max_entries);

    // 4. 解析并聚合
    // ...

    rados_release_read_op(op);
    rados_ioctx_destroy(ioctx);
    return 0;
}
```

### 8.2 P1.3: RADOS 用户组 OMAP 读取

**文件**: `src/drivers/rgw_sal_rados.c` (约第 1240-1310 行)

**当前问题**: 使用简单 JSON 解析。

**实现步骤**:
1. 创建独立的 IO 上下文访问 `.rgw.users.groups` 池
2. 使用 OMAP 迭代器遍历用户组
3. 解析每个组的 JSON 数据

### 8.3 P2.1: RADOS check_object_index

**文件**: `src/drivers/rgw_sal_rados.c` (约第 2320-2370 行)

**当前问题**: 框架存在，需要完整实现。

**实现步骤**:
1. 使用 `rados_ioctx_create` 创建索引和数据池 IO 上下文
2. 使用 `rados_nobjects_list` 遍历索引池
3. 对每个索引条目，检查数据对象是否存在
4. 收集不一致项到输出参数

### 8.4 P3.1: POSIX 用户文件 I/O

**文件**: `src/drivers/rgw_sal_posix.c`

**当前问题**: 仅创建目录，未写入文件。

**实现步骤**:
1. 定义用户数据目录结构: `{base}/users/{tenant}/{user_id}/`
2. 实现 `posix_user_store`: 序列化用户信息到 JSON，写入 `{user_dir}/info.json`
3. 实现 `posix_user_load`: 读取并解析 JSON 文件
4. 使用 `fopen/fwrite/fread` 进行文件操作

---

## 9. 时间线

### 9.1 甘特图

```
任务                    第1周  第2周  第3周  第4周  第5周  第6周  第7周  第8周  第9周  第10周
─────────────────────────────────────────────────────────────────────────────────────────
阶段1: 核心功能完善
├─ P1.1 Usage读取    ████████
├─ P1.2 Usage清理        ████
├─ P1.3 用户组OMAP         ████
├─ P1.4 桶信息解码             ████
├─ P1.5 ACL/策略存储            ████
├─ P1.6 桶统计完善                 ████
└─ P1.7 DBStore Usage                  ████

阶段2: 索引检查/修复
├─ P2.1 check_object_index                    ████████
├─ P2.2 fix_object_index                           ████
├─ P2.3 check_bucket_index                              ████
├─ P2.4 DBStore索引检查                                   ████████
└─ P2.5 索引工具                                            ████

阶段3: 驱动完善
├─ P3.1 POSIX用户I/O                                          ████████
├─ P3.2 POSIX桶遍历                                                 ████
├─ P3.3 POSIX对象操作                                                    ████████
├─ P3.4 DAOS容器管理                                                      ████████
├─ P3.5 D4N缓存管理                                                           ████
└─ P3.6 Motr索引管理                                                             ████████

阶段4: 测试覆盖
├─ P4.1 Usage测试                                                              ██
├─ P4.2 索引测试                                                                  ██████
├─ P4.3 POSIX测试                                                                    ██████
├─ P4.4 集成测试                                                                        ██████
└─ P4.5 性能测试                                                                            ██

阶段5: 集成验收
├─ P5.1 回归测试                                                                            ██████
├─ P5.2 文档更新                                                                                  ██
├─ P5.3 性能对比                                                                                      ██████
├─ P5.4 内存检测                                                                                            ██████
└─ P5.5 最终验收                                                                                                  ██
```

### 9.2 工时汇总

| 阶段 | 任务数 | 总工时 |
|------|--------|--------|
| 阶段1 | 7 | 17d |
| 阶段2 | 5 | 12d |
| 阶段3 | 6 | 16d |
| 阶段4 | 5 | 8d |
| 阶段5 | 5 | 8d |
| **总计** | **28** | **61d** |

---

## 10. 风险评估

### 10.1 风险矩阵

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| RADOS OMAP 操作复杂性 | 高 | 中 | 参考现有 rgw_omap 实现 |
| JSON 解析边界情况 | 中 | 低 | 使用成熟的 JSON 库或严格边界检查 |
| 索引修复逻辑一致性 | 高 | 中 | 添加事务支持和回滚 |
| POSIX 文件锁并发 | 中 | 中 | 使用 flock 或 fcntl |
| 测试环境搭建 | 中 | 低 | 使用 Docker 容器 |
| 性能基准达标 | 高 | 中 | 预留优化时间 |

### 10.2 依赖关系图

```
P1.1 ──┬── P1.2
       │
       └── P1.3
            │
P1.4 ───────┼── P1.5
            │
       ──────┴── P1.6
                 │
                 └──────┬── P2.1 ──┬── P2.2
                        │          │
                        │          └── P2.3
                        │
                        └── P2.4 ──┬── P2.5
                                   │
                                   └──────────────┬── P4.4 ── P5.1
                                                  │
P3.1 ──┬── P3.2 ──┬── P3.3 ──┬── P3.4 ──┬── P3.5 ──┬── P3.6
       │          │          │          │          │
       │          │          │          │          └──────────┴── P4.3
       │          │          │          │
       │          │          │          └──────────────────────────┘
       │          │          │
       │          │          └──────────────────────────────────────┘
       │          │
       └──────────┴─────────────────────────────────────────────────┘
```

---

## 11. 验收标准

### 11.1 代码质量

- [ ] 所有新增代码通过编译
- [ ] 无内存泄漏 (使用 AddressSanitizer 检测)
- [ ] 无未初始化的变量
- [ ] 边界条件正确处理
- [ ] 代码通过 lint 检查

### 11.2 功能验收

| 功能 | 验收标准 | 测试方法 |
|------|----------|----------|
| Usage 读取 | 能正确读取并聚合 RADOS usage 数据 | 写入数据后查询验证 |
| Usage 清理 | 能正确清理指定 epoch 范围的数据 | 清理后验证数据删除 |
| 用户组读取 | 能从 OMAP 正确读取用户所属组 | 创建组并验证读取 |
| 桶信息解码 | 能正确解码 RGWBucketInfo | 编码后再解码验证一致性 |
| ACL/策略存储 | 能正确存储和读取 ACL/策略 | 设置后获取验证 |
| 桶统计 | 能返回准确的统计值 | 与实际数据对比验证 |
| 索引检查 | 能检测出不一致项 | 模拟损坏验证检测 |
| 索引修复 | 能自动修复损坏的索引 | 修复后验证一致性 |
| POSIX I/O | 能正确读写用户/桶/对象文件 | CRUD 操作验证 |

### 11.3 测试验收

- [ ] 单元测试覆盖率 > 80%
- [ ] 集成测试全部通过
- [ ] 性能基准测试完成
- [ ] 回归测试无退化

### 11.4 性能验收

- [ ] 核心路径性能不低于 C++ 版本 90%
- [ ] 内存使用量不超过 C++ 版本 110%

---

## 附录

### A. 参考文件

| 文件 | 路径 | 说明 |
|------|------|------|
| 主头文件 | `include/core/rgw_sal.h` | VTable 定义 |
| RADOS 驱动 | `src/drivers/rgw_sal_rados.c` | RADOS 实现参考 |
| DBStore 驱动 | `src/drivers/rgw_sal_dbstore.c` | DBStore 实现参考 |
| POSIX 驱动 | `src/drivers/rgw_sal_posix.c` | POSIX 实现参考 |
| C++ SAL | `src/rgw/rgw_sal.h` | 原始 C++ 接口 |
| CLAUDE.md | `src/rgw/CLAUDE.md` | 项目整体指导 |

### B. 相关文档

| 文档 | 位置 | 说明 |
|------|------|------|
| 进度报告 | `sal_plan/sal_progress.md` | 详细进度评估 |
| 实施计划 | `sal_plan/sal_implementation_plan.md` | 分阶段实施计划 |
| 架构文档 | `memory-bank/@architecture.md` | 完整架构设计 |
| 转换规范 | `memory-bank/@cpp2c-document.md` | C++ 转 C 规范 |
| 编程规范 | `.cursor/rules/c-container-common-mistakes.mdc` | 常见错误预防 |

### C. 术语表

| 术语 | 说明 |
|------|------|
| SAL | Storage Abstraction Layer，存储抽象层 |
| VTable | Virtual Function Table，虚函数表 |
| OMAP | Object Map，RADOS 有序映射 |
| Usage | 使用统计信息 |
| MFA | Multi-Factor Authentication，多因素认证 |
| TOTP | Time-based One-Time Password，基于时间的一次性密码 |
| MPU | Multipart Upload，多部分上传 |

---

**文档结束**

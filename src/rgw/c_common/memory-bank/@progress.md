# RGW SAL C Common 实现进度

## 项目概述

本项目旨在将 RGW (RADOS Gateway) 的存储抽象层 (SAL) 从 C++ 转换为纯 C 实现，使用 librados.h 官方 C API。

## 实施方案

### 方案 C: 使用 librados.h 官方 C API

**选择原因**:
- 使用官方 C API，稳定且有文档支持
- 避免 C++ 封装层的复杂性
- 代码可直接编译到 librados 库

**配置选择**:
- 事务支持: 完整事务 (A)
- bucket_id 计数器: 内存计数器 (A)
- 元数据存储: 多个池 (A)
- 多站点同步: 支持 (A)

---

## 已完成任务

| 日期 | 任务 | 状态 | 文件 |
|------|------|------|------|
| 2026-03-18 | RADOS 上下文管理 | ✅ 完成 | `rgw_rados_ctx.h/.c` |
| 2026-03-18 | OMAP 工具封装 | ✅ 完成 | `rgw_omap.h/.c` |
| 2026-03-18 | 用户信息序列化 | ✅ 完成 | `rgw_user_serde.h/.c` |
| 2026-03-18 | 桶信息序列化 | ✅ 完成 | `rgw_bucket_serde.h/.c` |
| 2026-03-18 | 用户存储层 (RADOS) | ✅ 完成 | `rgw_rados_user.c` |
| 2026-03-18 | 桶存储层 (RADOS) | ✅ 完成 | `rgw_rados_bucket.c` |
| 2026-03-18 | 对象读取 | ✅ 完成 | `rgw_rados_object.h/.c` |
| 2026-03-18 | 对象写入 | ✅ 完成 | `rgw_rados_object.h/.c` |
| 2026-03-18 | 对象删除 | ✅ 完成 | `rgw_rados_object.h/.c` |
| 2026-03-18 | 对象属性 (xattr) | ✅ 完成 | `rgw_rados_object.h/.c` |
| 2026-03-18 | SAL 对象操作 | ✅ 完成 | `rgw_rados_obj.c` |

---

## 待完成任务

| 日期 | 任务 | 状态 | 依赖 |
|------|------|------|------|
| - | 阶段 5: 测试与集成 | 🔄 待开始 | 全部 |

---

## 文件清单

### 头文件 (`include/`)

| 文件 | 说明 |
|------|------|
| `rgw_rados_ctx.h` | RADOS 上下文管理接口 |
| `rgw_omap.h` | OMAP 操作封装接口 |
| `rgw_user_serde.h` | 用户信息序列化接口 |
| `rgw_bucket_serde.h` | 桶信息序列化接口 |

### 源文件 (`src/`)

| 文件 | 说明 |
|------|------|
| `rgw_rados_ctx.c` | RADOS 上下文管理实现 |
| `rgw_omap.c` | OMAP 操作封装实现 |
| `rgw_user_serde.c` | 用户信息序列化实现 |
| `rgw_bucket_serde.c` | 桶信息序列化实现 |
| `rgw_rados_user.c` | RADOS 用户存储实现 |
| `rgw_rados_bucket.c` | RADOS 桶存储实现 (create_bucket) |
| `rgw_rados_object.c` | 对象基础操作实现 |
| `rgw_rados_obj.c` | SAL 对象操作实现 |

### 头文件 (`include/`)

| 文件 | 说明 |
|------|------|
| `rgw_rados_object.h` | RADOS 对象操作接口 |

---

## 核心功能实现状态

### 用户存储

| 功能 | 状态 | 说明 |
|------|-------|------|
| `rados_user_load` | ✅ 已实现 | 从 RADOS 加载用户 |
| `rados_user_store` | ✅ 已实现 | 保存用户到 RADOS |
| `rados_user_remove` | ✅ 已实现 | 从 RADOS 删除用户 |
| `rados_user_get_by_access_key` | ✅ 已实现 | 通过 Access Key 查找用户 |
| `rados_user_get_by_email` | ✅ 已实现 | 通过 Email 查找用户 |

### 桶存储

| 功能 | 状态 | 说明 |
|------|-------|------|
| `rados_bucket_create` | ✅ 已实现 | 创建桶 (核心功能) |
| `rados_bucket_load` | ✅ 已实现 | 从 RADOS 加载桶 |
| `rados_bucket_delete` | ✅ 已实现 | 从 RADOS 删除桶 |

### create_bucket 完整流程

```
rados_bucket_create()
├── 1. 生成 bucket_id 和 marker (UUID)
├── 2. 构建 RGWBucketInfo
│   ├── 设置桶标识 (tenant, name, marker, bucket_id)
│   ├── 设置所有者 (owner)
│   ├── 获取 zonegroup_id
│   └── 获取放置规则和布局
├── 3. 初始化桶布局 (分片配置)
├── 4. 编码桶信息为二进制
├── 5. 写入桶实例信息 (OMAP)
│   └── 对象: .bucket.info.{bucket_id}
├── 6. 写入桶入口点 (OMAP)
│   └── 对象: .bucket.{tenant}:{name}
└── 完成
```

---

## 编码规范

遵循 `coding-standard.mdc` 中的规范：

- ✅ 使用 Doxygen 格式的中文注释 (`/** */`)
- ✅ 使用空格缩进 (4 空格)
- ✅ 禁止使用魔法数
- ✅ 函数命名遵循规范

---

## 下一步计划

### 阶段 4: 对象操作层

- `rados_object_read` - 读取对象
- `rados_object_write` - 写入对象
- `rados_object_delete` - 删除对象

### 阶段 5: 测试与集成

- 单元测试
- 集成测试
- 与原有 C++ 代码集成

---

## 文档版本

- 版本: 1.0
- 创建日期: 2026-03-18
- 最后更新: 2026-03-18

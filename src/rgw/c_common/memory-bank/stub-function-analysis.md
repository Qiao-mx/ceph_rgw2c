# SAL 存根函数依赖评估报告

## 概述

基于对 `rgw_sal_rados.c` 的分析，当前共有 **13 个存根函数**需要外部依赖才能完整实现。

---

## 存根函数详细清单

### 1. User VTable 存根 (8 个)

| # | 函数名 | 当前实现 | 依赖 | 估计代码量 |
|---|--------|----------|------|------------|
| 1 | `set_info` | 返回 NOT_IMPLEMENTED | RGWUserInfo 结构、RGWQuotaInfo | ~80 行 |
| 2 | `get_info` | 返回 NOT_IMPLEMENTED | RGWUserInfo 结构 | ~60 行 |
| 3 | `get_caps` | 返回 NOT_IMPLEMENTED | RGWUserCaps 类 | ~100 行 |
| 4 | `get_version_tracker` | 返回 NOT_IMPLEMENTED | RGWVersionTracker 类 | ~80 行 |
| 5 | `read_usage` | 返回 NOT_IMPLEMENTED | RGWUsage 类、usage API | ~120 行 |
| 6 | `trim_usage` | 返回 NOT_IMPLEMENTED | RGWUsage 类、usage API | ~80 行 |
| 7 | `verify_mfa` | 返回 NOT_IMPLEMENTED | RGWMFA 类 | ~60 行 |
| 8 | `list_groups` | 返回 NOT_IMPLEMENTED | RGWGroupInfo 类 | ~100 行 |

**User 存根小计: ~680 行**

---

### 2. Bucket VTable 存根 (0 个)

Bucket VTable 当前无存根函数，已完整实现。

---

### 3. Object VTable 存根 (0 个)

Object VTable 当前无存根函数，已完整实现。

---

### 4. Driver VTable 存根 (5 个)

| # | 函数名 | 当前实现 | 依赖 | 估计代码量 |
|---|--------|----------|------|------------|
| 1 | `list_buckets` | 简化实现 | RADOS 索引迭代器 | ~150 行 |
| 2 | `object_read` | 返回 NOT_IMPLEMENTED | librados 读操作 | ~200 行 |
| 3 | `object_write` | 返回 NOT_IMPLEMENTED | librados 写操作 | ~200 行 |
| 4 | `object_delete` | 返回 NOT_IMPLEMENTED | librados 删除操作 | ~100 行 |
| 5 | `complete_flush_stats` | 返回 NOT_IMPLEMENTED | 统计聚合 | ~80 行 |

**Driver 存根小计: ~730 行**

---

## 依赖分析

### 外部依赖列表

| 依赖项 | 类型 | 说明 | 优先级 |
|--------|------|------|--------|
| `RGWUserInfo` | C++ 结构体 | 用户信息结构 | 高 |
| `RGWQuotaInfo` | C++ 结构体 | 配额信息 | 高 |
| `RGWUserCaps` | C++ 类 | 用户权限 | 中 |
| `RGWVersionTracker` | C++ 类 | 版本跟踪 | 中 |
| `RGWUsage` | C++ 类 | 使用统计 | 中 |
| `librados` | C 库 | RADOS 操作 | 高 |

### 依赖复杂度分类

1. **高复杂度** (需要 librados 集成)
   - `object_read`: ~200 行
   - `object_write`: ~200 行
   - `object_delete`: ~100 行

2. **中复杂度** (需要 Ceph 内部类)
   - `get_caps`: ~100 行
   - `read_usage`: ~120 行
   - `list_groups`: ~100 行

3. **低复杂度** (需要简单结构映射)
   - `set_info`: ~80 行
   - `get_info`: ~60 行

---

## 实现工作量估算

### 按复杂度分类

| 复杂度 | 函数数 | 每函数平均行数 | 总行数 |
|--------|--------|----------------|--------|
| 高 | 3 | 167 | ~500 |
| 中 | 4 | 100 | ~400 |
| 低 | 6 | 70 | ~420 |
| **合计** | **13** | **102** | **~1320** |

### 按模块分类

| 模块 | 存根数 | 估计行数 | 依赖难度 |
|------|--------|----------|----------|
| User | 8 | ~680 | 中等 |
| Driver | 5 | ~730 | 困难 |
| **总计** | **13** | **~1410** | - |

---

## 实现建议

### 方案 A: 分阶段实现

```
阶段 1: User 模块 (8 个)
├── 优先级 1: set_info, get_info (简单结构映射)
├── 优先级 2: get_caps, list_groups (内部类)
└── 优先级 3: read_usage, trim_usage, verify_mfa, get_version_tracker

阶段 2: Driver 模块 (5 个)
├── 优先级 1: list_buckets (索引迭代)
├── 优先级 2: object_read, object_write, object_delete (librados)
└── 优先级 3: complete_flush_stats
```

### 方案 B: 按依赖分类

```
依赖类型 1: C 结构体映射 (简单)
- RGWUserInfo → C 结构体
- RGWQuotaInfo → C 结构体

依赖类型 2: Ceph 内部类 (中等)
- RGWUserCaps
- RGWVersionTracker
- RGWUsage

依赖类型 3: librados 集成 (困难)
- 读操作 (带偏移量、部分读取)
- 写操作 (原子写入、追加)
- 删除操作 (软删除、版本控制)
```

---

## 风险评估

| 风险项 | 影响 | 缓解措施 |
|--------|------|----------|
| librados 依赖 | 高 | 使用 C++ 包装器封装 librados 调用 |
| C++ 类型转换 | 中 | 创建 C++/C 边界层 |
| 内存管理 | 中 | 使用内存池、RAII |
| 线程安全 | 高 | 添加适当的锁机制 |

---

## 结论

- **总存根数**: 13 个
- **估计总代码量**: ~1400 行
- **主要瓶颈**: librados 集成 (3 个函数，约 500 行)
- **建议优先级**: 先实现 User 模块，再实现 Driver 模块

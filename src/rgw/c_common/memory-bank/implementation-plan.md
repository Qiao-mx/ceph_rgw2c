# RGW C++ 到 C 转换项目实施计划

## 项目概述

本项目旨在将 Ceph RGW (RADOS Gateway) 模块从 C++ 语言逐步转换为 C 语言，同时保持功能完整性和性能指标。

## 项目阶段总览

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段 0 | 基础设施准备 | ✅ 完成 |
| 阶段 1 | 核心数据类型转换 | ✅ 完成 |
| 阶段 2 | 存储抽象层转换 (SAL) | 🔄 进行中 |
| 阶段 3 | REST 核心转换 | ⏳ 待开始 |
| 阶段 4 | 上层模块转换 | ⏳ 待开始 |
| 阶段 5 | 优化与收尾 | ⏳ 待开始 |

---

## 阶段 0: 基础设施准备 ✅ 已完成

### 完成的任务

| 任务 | 说明 | 状态 |
|------|------|------|
| 容器库完善 | c_common 容器库补充队列、栈等 | ✅ 完成 |
| OOP 框架 | rgw_oop 实现虚函数表和引用计数 | ✅ 完成 |
| 错误处理 | rgw_errors 实现统一错误码 | ✅ 完成 |
| 统一头文件 | rgw_ccommon 整合所有容器 | ✅ 完成 |

### 已实现的容器

- ✅ 动态数组 (rgw_carray)
- ✅ 字符串 (rgw_cstring)
- ✅ 有序 Map (rgw_cmap)
- ✅ 有序 Set (rgw_cset)
- ✅ 双向链表 (rgw_clist)
- ✅ 双端队列 (rgw_cdeque)
- ✅ 栈 (rgw_stack)
- ✅ 队列 (rgw_queue)
- ✅ 优先队列 (rgw_priority_queue)
- ✅ 哈希表 (rgw_chash_map)
- ✅ 可选类型 (rgw_coptional)

---

## 阶段 1: 核心数据类型转换 ✅ 已完成

### 完成的任务

| 任务 | 说明 | 状态 |
|------|------|------|
| rgw_string | 字符串处理 C 实现 | ✅ 完成 |
| rgw_xml | XML 解析器 C 实现 | ✅ 完成 |
| rgw_b64 | Base64 编解码 C 实现 | ✅ 完成 |
| rgw_buffer | 缓冲区管理 C 实现 | ✅ 完成 |
| rgw_hex | 十六进制编解码 C 实现 | ✅ 完成 |

### 待完成的任务

| 任务 | 说明 | 状态 |
|------|------|------|
| Valgrind 内存泄漏检测 | 使用 Valgrind 检测内存泄漏 | 🔄 待开始 |
| 性能基准测试 | 对比 C++ 版本性能 | 🔄 待开始 |

### 测试状态

| 测试 | 状态 |
|------|------|
| test_cstring | ✅ PASSED |
| test_cdeque | ✅ PASSED |
| test_errors | ✅ PASSED |
| test_buffer | ✅ PASSED |
| test_hex | ✅ PASSED |
| test_types | ✅ PASSED |

---

## 阶段 2: 存储抽象层转换 (SAL) 🔄 进行中

### 概述

SAL (Storage Abstraction Layer) 是 RGW 的核心抽象层，将上层协议处理与底层存储后端解耦。

### 架构图

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
│                    SAL 抽象层 (本阶段目标)                   │
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

### 已完成的工作

#### 2.1.1 接口框架设计 ✅

| 组件 | 说明 | 状态 |
|------|------|------|
| 目录结构 | src/rgw/sal_c/ 目录 | ✅ 完成 |
| 数据类型 | User/Bucket/Object C 类型定义 | ✅ 完成 |
| vtable 模式 | 虚函数表框架 | ✅ 完成 |

#### 2.1.2 创建的文件

| 文件 | 说明 | 状态 |
|------|------|------|
| `sal_c/include/rgw_sal_errors.h` | 错误码定义 | ✅ 完成 |
| `sal_c/include/rgw_sal_types.h` | 核心类型定义 | ✅ 完成 |
| `sal_c/include/rgw_sal.h` | 主 SAL C 接口 (含 vtable) | ✅ 完成 |
| `sal_c/include/rgw_sal_c.h` | 统一头文件 | ✅ 完成 |
| `sal_c/include/rgw_sal_rados.h` | RADOS 驱动 C 接口 | ✅ 完成 |
| `sal_c/src/rgw_sal_types.c` | 类型实现 | ✅ 完成 |
| `sal_c/src/rgw_sal.c` | 基础 SAL API 实现 | 🔄 存根 |
| `sal_c/CMakeLists.txt` | CMake 配置 | ✅ 完成 |

### 转化完成度统计

| 模块 | 总虚函数数 | 已转化数 | 完成度 |
|------|------------|----------|--------|
| User VTable | 25 | 15 | 60% |
| Bucket VTable | 70+ | 13 | ~18% |
| Object VTable | 40+ | 13 | ~32% |
| Driver VTable | 100+ | 11 | ~11% |
| **总计** | **235+** | **52** | **~22%** |

### 未完成的组件 (需要进一步完善)

#### User VTable 未转化

- 命名空间操作: `get_ns()`, `set_ns()`, `clear_ns()`
- 配额信息: `set_info()`, `get_info()`
- 权限管理: `get_caps()`, `get_version_tracker()`
- 使用统计: `read_usage()`, `trim_usage()`
- MFA 认证: `verify_mfa()`
- 组管理: `list_groups()`
- 流输出: `print()`

#### Bucket VTable 未转化 (~57 函数)

- 桶操作: `create()`, `delete_bucket()`, `rename()`, `set_acl()`, `get_policy()`, `set_policy()`
- 同步: `sync()`, `drain()`
- 索引: `check_bucket_index()`, `fix_object_index()`
- 统计: `get_usage()`, `read_stats()`, `complete_stats()`

#### Object VTable 未转化 (~27 函数)

- 复制: `copy_object()`
- ACL: `get_acl()`, `set_acl()`
- 原子性: `set_atomic()`, `is_atomic()`, `set_prefetch_data()`, `is_prefetch_data()`
- 压缩: `set_compressed()`, `is_compressed()`
- 转换: `transition()`, `transition_to_cloud()`, `restore_obj_from_cloud()`

#### Driver VTable 未转化 (~89 函数)

- 账户管理: `load_account_*()`, `store_account()`, `delete_account()`
- 统计: `load_stats()`, `reset_stats()`, `complete_flush_stats()`
- 角色: `count_account_roles()`, `list_account_roles()`
- 用户: `count_account_users()`, `list_account_users()`
- 组: `load_group_*()`, `store_group()`, `remove_group()`, `list_group_users()`
- 桶操作: `load_bucket()`, `list_buckets()`, `create_bucket()`
- 区域: `get_zone()`, `is_meta_master()`, `get_zonegroup()`

### 核心组件

| 组件 | 说明 | C++ 定义文件 |
|------|------|--------------|
| Driver | 存储驱动基类 | rgw_sal.h |
| User | 用户实体 | rgw_sal.h |
| Bucket | 桶实体 | rgw_sal.h |
| Object | 对象实体 | rgw_sal.h |
| MultipartUpload | 多部分上传 | rgw_sal.h |
| Lifecycle | 生命周期管理 | rgw_sal.h |
| Notification | 事件通知 | rgw_sal.h |

### 涉及的文件

#### 核心接口文件

| 文件 | 行数 | 说明 |
|------|------|------|
| `src/rgw/rgw_sal.h` | ~2000 | 主 SAL 接口定义 |
| `src/rgw/rgw_sal_fwd.h` | ~60 | 前向声明 |
| `src/rgw/rgw_sal_filter.h` | ~1100 | 过滤器实现 |
| `src/rgw/rgw_sal_store.h` | ~300 | 存储接口 |
| `src/rgw/rgw_sal_config.h` | ~200 | 配置接口 |

#### 存储驱动文件

| 驱动 | 文件 | 状态 |
|------|------|------|
| RADOS | driver/rados/rgw_sal_rados.h/cc | 待转换 |
| DBStore | rgw_sal_dbstore.h/cc | 待转换 |
| POSIX | driver/posix/rgw_sal_posix.h/cc | 待转换 |
| D4N | driver/d4n/rgw_sal_d4n.h/cc | 待转换 |
| Motr | driver/motr/rgw_sal_motr.h/cc | 待转换 |
| DAOS | driver/daos/rgw_sal_daos.h/cc | 待转换 |

### 详细实施计划

#### 子阶段 2.1: SAL 接口设计 ✅ 已完成

| 任务 | 说明 | 状态 |
|------|------|------|
| 创建目录结构 | src/rgw/sal_c/ 目录 | ✅ 完成 |
| 设计数据类型 C 接口 | User/Bucket/Object C 类型定义 | ✅ 完成 |
| 实现 vtable 模式 | 虚函数表框架 | ✅ 完成 |

#### 子阶段 2.2: RADOS 驱动转换 🔄 进行中

| 任务 | 说明 | 状态 |
|------|------|------|
| 分析 RADOS 驱动 | 理解现有实现 | ✅ 完成 |
| 创建 C 接口头文件 | 设计 API | 🔄 待开始 |
| 实现底层操作 | 基础 CRUD | 🔄 待开始 |
| 实现高级功能 | 版本控制等 | 🔄 待开始 |

#### 子阶段 2.3: 其他驱动转换

| 任务 | 说明 | 状态 |
|------|------|------|
| DBStore 驱动 | SQLite 后端 | 🔄 待开始 |
| POSIX 驱动 | 文件系统后端 | 🔄 待开始 |
| 其他驱动 | D4N/Motr/DAOS | 🔄 待开始 |

#### 子阶段 2.4: 综合测试

| 任务 | 说明 | 状态 |
|------|------|------|
| 单元测试 | 各模块测试 | 🔄 待开始 |
| 集成测试 | C/C++ 互操作 | 🔄 待开始 |
| 性能测试 | 基准测试 | 🔄 待开始 |
| 内存检测 | Valgrind/ASan | 🔄 待开始 |

### 工作要求确认

根据用户确认，SAL 阶段工作要求如下：

#### 执行顺序

SAL 阶段将按以下顺序执行：

1. **SAL-006**: 实现 RADOS 驱动适配器
2. **SAL-007**: 创建 RADOS 驱动测试用例
3. **SAL-008**: 实现 DBStore 驱动
4. **SAL-009**: 实现 POSIX 驱动
5. **SAL-010**: 综合集成测试

#### 实现要求

- **完整实现**: 所有驱动需要完整实现所有 vtable 函数
- **无省略**: 不省略任何功能模块

#### 测试要求

- **完整测试用例**: 为每个驱动创建完整的测试用例
- **测试覆盖**: 用户 CRUD、桶 CRUD、对象 CRUD、属性操作
- **内存检测**: 使用 AddressSanitizer 进行内存错误检测
- **集成测试**: C/C++ 互操作性测试

### C 接口设计示例

#### 驱动接口

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
```

#### 虚函数表模式

```c
typedef struct rgw_sal_driver_vtable {
    int (*initialize)(rgw_sal_driver_t*, void*, const void*);
    const char* (*get_name)(const rgw_sal_driver_t*);
    rgw_sal_user_t* (*get_user)(rgw_sal_driver_t*, const rgw_user_t*);
    int (*get_user_by_access_key)(rgw_sal_driver_t*, const char*, rgw_sal_user_t**);
    // ... 更多函数指针
} rgw_sal_driver_vtable_t;
```

### 转换难点与解决方案

| 难点 | 解决方案 |
|------|----------|
| 虚函数多态 | 使用 vtable 函数指针表 |
| std::unique_ptr | 手动管理生命周期，create/destroy 配对 |
| std::shared_ptr | 引用计数实现 |
| std::function | 函数指针 + 上下文参数 |
| bufferlist | 使用已转换的 rgw_buffer_t |

---

## 阶段 3: REST 核心转换 ⏳ 待开始

### 目标

转换 REST 框架和协议实现。

### 涉及模块

- rgw_op.cc - 操作处理器
- rgw_rest_*.cc - REST 框架
- S3 协议实现
- Swift 协议实现

---

## 阶段 4: 上层模块转换 ⏳ 待开始

### 目标

转换认证授权、服务层等。

### 涉及模块

- 认证授权模块
- 服务层
- 前端网络层
- 异步协程框架

---

## 阶段 5: 优化与收尾 ⏳ 待开始

### 目标

性能优化、文档完善、最终验收。

### 任务

- 性能优化和内存泄漏修复
- 文档编写
- 最终验收测试
- 生产环境灰度发布

---

## 成功标准

### 技术标准

| 标准 | 要求 |
|------|------|
| 功能完整性 | 100% 通过原有功能测试 |
| 性能指标 | 关键操作不低于原版 90% |
| 内存安全 | 零内存泄漏 |
| 代码质量 | 零编译警告 |
| 测试覆盖 | 单元测试 >80% |

### 业务标准

- API 完全兼容
- 部署简便
- 维护成本降低

---

## 风险管理

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| 性能下降 | 中 | 高 | 关键路径保留 C++ |
| 内存泄漏 | 高 | 高 | 自动化检测工具 |
| 异常遗漏 | 中 | 中 | 静态分析检查返回值 |
| 第三方库无 C 替代 | 低 | 中 | 最小化包装层 |

---

## 参考文档

- `memory-bank/architecture.md` - 架构文档
- `memory-bank/tech-stack.md` - 技术栈说明
- `memory-bank/cpp2c-document.md` - C++ 到 C 转换规范
- `memory-bank/sal-conversion-plan.md` - SAL 转换详细计划
- `memory-bank/coding-standards.md` - 编程规范
- `src/rgw/sal_c/MAPPING_DETAIL.md` - SAL C++ 到 C 详细映射
- `src/rgw/sal_c/IMPLEMENTATION_SUMMARY.md` - 实现总结

---

**文档版本**: 2.1
**更新日期**: 2026-03-18
**维护团队**: RGW C++ 到 C 转换项目组

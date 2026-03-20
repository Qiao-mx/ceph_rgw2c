# SAL C 完整架构文档

**文档版本**: 1.0
**生成日期**: 2026-03-20
**基于代码**: `d:\NAS\ceph-20.1.1\src\rgw\sal_c` 和 `d:\NAS\ceph-20.1.1\src\rgw\c_common`

---

## 目录

1. [整体架构概览](#1-整体架构概览)
2. [sal_c 模块目录结构](#2-sal_c-模块目录结构)
3. [c_common 模块目录结构](#3-c_common-模块目录结构)
4. [核心数据类型](#4-核心数据类型)
5. [API 函数列表](#5-api-函数列表)
6. [驱动实现](#6-驱动实现)
7. [内存管理](#7-内存管理)
8. [错误处理](#8-错误处理)

---

## 1. 整体架构概览

### 1.1 项目背景

SAL C 是 Ceph RGW (RADOS Gateway) 存储抽象层的 C 语言实现，目的是将原有的 C++ SAL 实现转换为纯 C 接口，以提高可移植性和减少运行时依赖。

### 1.2 架构层次

```
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (radosgw, radosgw-admin)           │
├─────────────────────────────────────────────────────────────┤
│                    前端层 (rgw_a)                            │
├─────────────────────────────────────────────────────────────┤
│                    服务层 (services/)                        │
├─────────────────────────────────────────────────────────────┤
│                    REST 层 (rgw_rest_*, rgw_op)              │
├─────────────────────────────────────────────────────────────┤
│              存储抽象层 (SAL C) - rgw_sal.h                  │
│  ┌─────────────┬─────────────┬─────────────┬──────────────┐ │
│  │ Driver VTable│ User VTable │Bucket VTable│Object VTable │ │
│  └─────────────┴─────────────┴─────────────┴──────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                    驱动层 (drivers/)                         │
│  ┌──────────┬──────────┬──────────┬──────────┬────────────┐│
│  │  RADOS   │ DBStore  │  POSIX   │   D4N    │   DAOS     ││
│  └──────────┴──────────┴──────────┴──────────┴────────────┘│
├─────────────────────────────────────────────────────────────┤
│                    c_common 容器库                           │
│  ┌──────────────────────────────────────────────────────────┤
│  │ rgw_carray │ rgw_cstring │ rgw_cmap │ rgw_cset │ ...    ││
│  └──────────────────────────────────────────────────────────┘│
├─────────────────────────────────────────────────────────────┤
│                    Ceph 存储层 (librados)                    │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 模块依赖关系

```
sal_c
├── include/core/rgw_sal.h         # 核心SAL接口
├── include/core/rgw_sal_types.h  # 类型定义
├── include/core/rgw_sal_errors.h  # 错误码定义
├── include/rgw_sal_usage.h        # Usage统计
├── include/drivers/*.h           # 驱动接口
├── src/core/rgw_sal.c            # SAL核心实现
├── src/rgw_sal_usage.c           # Usage实现
└── src/drivers/*.c              # 驱动实现
    │
    └── 依赖 c_common:
        ├── containers/           # 容器库
        ├── rgw_rados_*.h/c       # RADOS操作封装
        ├── rgw_omap.h/c          # OMAP操作封装
        ├── rgw_bucket_serde.h/c  # 桶序列化
        ├── rgw_user_serde.h/c    # 用户序列化
        └── rgw_errors.h/c        # 错误处理
```

---

## 2. sal_c 模块目录结构

### 2.1 完整目录树

```
d:\NAS\ceph-20.1.1\src\rgw\sal_c\
├── CMakeLists.txt
│
├── sal_plan/
│   ├── sal_progress.md           # 进度报告
│   ├── sal_implementation_plan.md # 实施计划
│   └── sal_completion_report.md  # 完成报告
│
├── include/
│   ├── rgw_sal_usage.h          # Usage统计接口 (407行)
│   ├── rgw_sal_c_wrapper.h      # C++包装器
│   │
│   ├── core/
│   │   ├── rgw_sal.h           # SAL主接口 (563行)
│   │   ├── rgw_sal_types.h     # 类型定义 (421行)
│   │   └── rgw_sal_errors.h    # 错误码定义
│   │
│   └── drivers/
│       ├── rgw_sal_rados.h      # RADOS驱动 (220行)
│       ├── rgw_sal_dbstore.h    # DBStore驱动
│       ├── rgw_sal_posix.h      # POSIX驱动
│       ├── rgw_sal_d4n.h        # D4N缓存驱动
│       ├── rgw_sal_daos.h       # DAOS驱动
│       └── rgw_sal_motr.h       # Motr驱动
│
├── src/
│   ├── rgw_sal_usage.c          # Usage实现 (939行)
│   │
│   ├── core/
│   │   ├── rgw_sal.c           # SAL核心实现
│   │   └── rgw_sal_types.c     # 类型管理实现
│   │
│   └── drivers/
│       ├── rgw_sal_rados.c     # RADOS驱动 (~2700行)
│       ├── rgw_sal_dbstore.c   # DBStore驱动 (~2300行)
│       ├── rgw_sal_posix.c     # POSIX驱动 (~1100行)
│       ├── rgw_sal_d4n.c       # D4N驱动 (~900行)
│       ├── rgw_sal_daos.c      # DAOS驱动
│       └── rgw_sal_motr.c       # Motr驱动
│
├── tests/
│   ├── CMakeLists.txt          # 测试构建配置 (172行)
│   ├── test_basic.c             # 基础类型测试
│   ├── test_rados_driver.c     # RADOS驱动测试 (~2000行)
│   ├── test_dbstore_driver.c   # DBStore驱动测试
│   ├── test_integration.c      # 集成测试
│   │
│   └── benchmark/
│       ├── CMakeLists.txt
│       ├── sal_c_test_framework.h
│       └── sal_c_test_framework.c
│
└── build/                      # 构建输出目录
```

---

## 3. c_common 模块目录结构

### 3.1 完整目录树

```
d:\NAS\ceph-20.1.1\src\rgw\c_common\
├── CMakeLists.txt
├── MEMORY_TESTING.md
├── run_memory_tests.bat
├── run_memory_tests.sh
│
├── include/
│   ├── rgw_ccommon.h           # 统一头文件 (100行)
│   ├── rgw_buffer.h            # 缓冲区
│   ├── rgw_b64.h              # Base64编解码
│   ├── rgw_hex.h              # 十六进制编解码
│   ├── rgw_errors.h           # 错误处理框架
│   ├── rgw_xml.h              # XML解析器
│   ├── rgw_oop.h              # OOP框架
│   ├── rgw_rados_object.h     # RADOS对象操作
│   ├── rgw_rados_ctx.h        # RADOS上下文管理
│   ├── rgw_omap.h             # OMAP操作封装
│   ├── rgw_bucket_serde.h     # 桶信息序列化
│   ├── rgw_user_serde.h       # 用户信息序列化
│   ├── rgw_sqlite.h           # SQLite封装
│   ├── csort.h                # 排序算法
│   │
│   ├── containers/
│   │   ├── rgw_cmemory.h      # 内存管理 (与rgw_ccommon.h同名)
│   │   ├── rgw_carray.h       # 动态数组 (174行)
│   │   ├── rgw_cstring.h      # 字符串 (251行)
│   │   ├── rgw_cmap.h         # 有序Map (215行)
│   │   ├── rgw_cset.h         # 有序Set
│   │   ├── rgw_clist.h        # 双向链表
│   │   ├── rgw_cdeque.h       # 双端队列
│   │   ├── rgw_cstack.h       # 栈
│   │   ├── rgw_cqueue.h       # 队列
│   │   ├── rgw_cpriority_queue.h # 优先队列
│   │   ├── rgw_chash_map.h    # 哈希表
│   │   ├── rgw_coptional.h    # 可选类型
│   │   └── rgw_ctypes.h       # 基础类型
│   │
│   └── internal/
│       ├── rgw_list.h         # 链表宏
│       ├── rgw_rbtree.h       # 红黑树
│       ├── rgw_rbt_node.h     # 红黑树节点
│       ├── rgw_avltree.h      # AVL树
│       ├── utarray.h          # uthash数组
│       ├── uthash.h           # 哈希表宏
│       ├── utlist.h           # 链表宏
│       ├── utringbuffer.h     # 环形缓冲区
│       ├── utstring.h          # 字符串宏
│       └── utstack.h          # 栈宏
│
├── src/
│   ├── rgw_buffer.c
│   ├── rgw_b64.c
│   ├── rgw_hex.c
│   ├── rgw_errors.c
│   ├── rgw_xml.c
│   ├── rgw_oop.c
│   ├── rgw_carray.c
│   ├── rgw_cstring.c
│   ├── rgw_cmap.c
│   ├── rgw_cset.c
│   ├── rgw_clist.c
│   ├── rgw_cdeque.c
│   ├── rgw_cstack.c
│   ├── rgw_cqueue.c
│   ├── rgw_cpriority_queue.c
│   ├── rgw_coptional.c
│   ├── rgw_cmemory.c
│   ├── csort.c
│   ├── rgw_rados_obj.c
│   ├── rgw_rados_object.c
│   ├── rgw_rados_bucket.c
│   ├── rgw_rados_user.c
│   ├── rgw_bucket_serde.c
│   ├── rgw_user_serde.c
│   ├── rgw_omap.c
│   ├── rgw_rados_ctx.c
│   └── rgw_sqlite.c
│
├── containers/
│   ├── rgw_chash_map.c
│   ├── rgw_clist.c
│   ├── rgw_splaytree.c
│   ├── rgw_bst.c
│   ├── rgw_avl.c
│   └── rgw_rbtree.c
│
├── tests/
│   ├── test_rgw_xml.c
│   ├── test_benchmark.c
│   ├── test_buffer.c
│   ├── test_b64.c
│   ├── test_hex.c
│   ├── test_cstack.c
│   ├── test_cqueue.c
│   ├── test_cpriority_queue.c
│   ├── test_cstring.c
│   ├── test_cpp_to_c.c
│   ├── test_carray.c
│   ├── test_errors.c
│   ├── test_ccontainer_edge_cases.c
│   ├── test_cdeque.c
│   ├── test_oop.c
│   ├── test_ccontainer.c
│   ├── test_comprehensive.c
│   ├── test_coptional.c
│   ├── test_cset.c
│   ├── test_cmap.c
│   └── test_memory.c
│
└── build/                      # 构建输出目录
```

---

## 4. 核心数据类型

### 4.1 SAL 核心结构体

#### 4.1.1 驱动结构

```c:29:56:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
struct rgw_sal_driver {
    const rgw_sal_driver_vtable_t* vtable;
    const rgw_sal_user_vtable_t* user_vtable;    /**< 用户操作 vtable */
    const rgw_sal_bucket_vtable_t* bucket_vtable; /**< 桶操作 vtable */
    const rgw_sal_object_vtable_t* object_vtable;  /**< 对象操作 vtable */
    void* impl;                          /**< 驱动特定实现 */
    char name[64];                       /**< 驱动名称 */
};
```

#### 4.1.2 用户结构

```c:262:266:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
struct rgw_sal_user {
    const rgw_sal_user_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_driver_t* driver;             /**< 所属驱动 */
};
```

#### 4.1.3 桶结构

```c:271:275:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
struct rgw_sal_bucket {
    const rgw_sal_bucket_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_driver_t* driver;             /**< 所属驱动 */
};
```

#### 4.1.4 对象结构

```c:280:284:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
struct rgw_sal_object {
    const rgw_sal_object_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_bucket_t* bucket;             /**< 所属桶 */
};
```

### 4.2 虚函数表定义

#### 4.2.1 用户 VTable (30个函数指针)

```c:33:89:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
typedef struct rgw_sal_user_vtable {
    /* 生命周期 (2个) */
    void* (*clone)(const rgw_sal_user_t* user);
    void (*destroy)(rgw_sal_user_t* user);

    /* 属性访问 (6个) */
    const char* (*get_id)(const rgw_sal_user_t* user);
    const char* (*get_display_name)(rgw_sal_user_t* user);
    int (*set_display_name)(rgw_sal_user_t* user, const char* name);
    const char* (*get_tenant)(const rgw_sal_user_t* user);
    uint32_t (*get_type)(const rgw_sal_user_t* user);
    int32_t (*get_max_buckets)(const rgw_sal_user_t* user);
    void (*set_max_buckets)(rgw_sal_user_t* user, int32_t max);

    /* 属性映射 (2个) */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_user_t* user);
    int (*set_attrs)(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs);

    /* 持久化操作 (3个) */
    int (*load)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*store)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                 rgw_sal_yield_t* y, bool exclusive);
    int (*remove)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 属性读取/写入 (2个) */
    int (*read_attrs)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*merge_and_store_attrs)(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 命名空间操作 (3个) */
    const char* (*get_ns)(const rgw_sal_user_t* user);
    int (*set_ns)(rgw_sal_user_t* user, const char* ns);
    void (*clear_ns)(rgw_sal_user_t* user);

    /* 配额信息 (2个) */
    int (*set_info)(rgw_sal_user_t* user, void* info);
    int (*get_info)(rgw_sal_user_t* user, void** info);

    /* 权限管理 (2个) */
    int (*get_caps)(rgw_sal_user_t* user, void** caps);
    int (*get_version_tracker)(rgw_sal_user_t* user, void** tracker);

    /* 使用统计 (2个) */
    int (*read_usage)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                      uint64_t start_epoch, uint64_t end_epoch,
                      uint32_t max_entries, void* usage);
    int (*trim_usage)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                      uint64_t start_epoch, uint64_t end_epoch);

    /* MFA 认证 (1个) */
    int (*verify_mfa)(rgw_sal_user_t* user, const char* mfa, const char* code,
                      const rgw_sal_dpp_t* dpp);

    /* 组管理 (1个) */
    int (*list_groups)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                       void** groups, uint32_t* count);
} rgw_sal_user_vtable_t;
```

#### 4.2.2 桶 VTable (27个函数指针)

```c:94:169:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
typedef struct rgw_sal_bucket_vtable {
    /* 生命周期 (2个) */
    void* (*clone)(const rgw_sal_bucket_t* bucket);
    void (*destroy)(rgw_sal_bucket_t* bucket);

    /* 属性访问 (5个) */
    const char* (*get_name)(const rgw_sal_bucket_t* bucket);
    const char* (*get_tenant)(const rgw_sal_bucket_t* bucket);
    const char* (*get_marker)(const rgw_sal_bucket_t* bucket);
    rgw_sal_bucket_info_t* (*get_info)(rgw_sal_bucket_t* bucket);
    rgw_sal_user_t* (*get_owner)(rgw_sal_bucket_t* bucket);

    /* 属性映射 (2个) */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_bucket_t* bucket);
    int (*set_attrs)(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs);

    /* 对象列表 (1个) */
    int (*list)(rgw_sal_bucket_t* bucket, ...);

    /* 持久化操作 (3个) */
    int (*load)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*store)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                 rgw_sal_yield_t* y, bool exclusive);
    int (*remove)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 桶操作 (3个) */
    int (*create)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                  rgw_sal_yield_t* y, bool create_obj);
    int (*delete_bucket)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                         rgw_sal_yield_t* y, bool delete_objects);
    int (*rename)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                  rgw_sal_yield_t* y, const char* new_name);

    /* ACL/策略 (3个) */
    int (*set_acl)(rgw_sal_bucket_t* bucket, void* acl, const rgw_sal_dpp_t* dpp,
                   rgw_sal_yield_t* y);
    int (*get_policy)(rgw_sal_bucket_t* bucket, void** policy, const rgw_sal_dpp_t* dpp,
                      rgw_sal_yield_t* y);
    int (*set_policy)(rgw_sal_bucket_t* bucket, void* policy, const rgw_sal_dpp_t* dpp,
                      rgw_sal_yield_t* y);

    /* 标签 (2个) */
    int (*get_tag)(rgw_sal_bucket_t* bucket, char** tag);
    int (*set_tag)(rgw_sal_bucket_t* bucket, const char* tag, const rgw_sal_dpp_t* dpp,
                   rgw_sal_yield_t* y);

    /* 统计 (6个) */
    int (*get_usage)(rgw_sal_bucket_t* bucket, void** usage, const rgw_sal_dpp_t* dpp,
                     rgw_sal_yield_t* y);
    int (*read_stats)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, void* stats);
    int (*read_stats_async)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, void* cb);
    int (*complete_stats)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp);
    int (*update_bucket_stats)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, void* stats);
    int (*sync_user_stats)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                           rgw_sal_yield_t* y);

    /* 同步 (2个) */
    int (*sync)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*drain)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 索引检查 (3个) */
    int (*check_object_index)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y);
    int (*fix_object_index)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y);
    int (*check_bucket_index)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y);
} rgw_sal_bucket_vtable_t;
```

#### 4.2.3 对象 VTable (16个函数指针)

```c:174:209:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
typedef struct rgw_sal_object_vtable {
    /* 生命周期 (2个) */
    void* (*clone)(const rgw_sal_object_t* obj);
    void (*destroy)(rgw_sal_object_t* obj);

    /* 属性访问 (3个) */
    const char* (*get_name)(const rgw_sal_object_t* obj);
    const char* (*get_instance)(const rgw_sal_object_t* obj);
    bool (*is_null)(const rgw_sal_object_t* obj);

    /* 属性映射 (2个) */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_object_t* obj);
    int (*set_attrs)(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs);

    /* 读操作 (1个) */
    int (*read)(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                uint8_t* buffer, size_t* buffer_size,
                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 写操作 (1个) */
    int (*write)(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                 const uint8_t* data, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 删除操作 (1个) */
    int (*delete_obj)(rgw_sal_object_t* obj, uint32_t flags,
                      const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 持久化操作 (3个) */
    int (*load_state)(rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp,
                      rgw_sal_yield_t* y, bool follow_olh);
    int (*get_obj_attrs)(rgw_sal_object_t* obj, rgw_sal_yield_t* y,
                          const rgw_sal_dpp_t* dpp);
    int (*set_obj_attrs)(rgw_sal_object_t* obj, rgw_sal_attrs_t* setattrs,
                          rgw_sal_attrs_t* delattrs, rgw_sal_yield_t* y,
                          uint32_t flags);
} rgw_sal_object_vtable_t;
```

#### 4.2.4 驱动 VTable (11个函数指针)

```c:214:253:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal.h
typedef struct rgw_sal_driver_vtable {
    /* 生命周期 (1个) */
    void (*destroy)(rgw_sal_driver_t* driver);

    /* 初始化 (1个) */
    int (*initialize)(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp);

    /* 元数据 (2个) */
    const char* (*get_name)(const rgw_sal_driver_t* driver);
    int (*get_cluster_id)(rgw_sal_driver_t* driver, char** cluster_id,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 用户操作 (4个) */
    rgw_sal_user_t* (*get_user)(rgw_sal_driver_t* driver, const rgw_sal_user_id_t* uid);
    int (*get_user_by_access_key)(rgw_sal_driver_t* driver, const char* key,
                                   rgw_sal_user_t** user,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_email)(rgw_sal_driver_t* driver, const char* email,
                              rgw_sal_user_t** user,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_swift)(rgw_sal_driver_t* driver, const char* user_str,
                              rgw_sal_user_t** user,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 桶操作 (2个) */
    rgw_sal_bucket_t* (*get_bucket)(rgw_sal_driver_t* driver,
                                    const rgw_sal_bucket_info_t* info);
    int (*list_buckets)(rgw_sal_driver_t* driver, ...);

    /* 对象操作 (1个) */
    rgw_sal_object_t* (*get_object)(rgw_sal_driver_t* driver,
                                     rgw_sal_bucket_t* bucket,
                                     const rgw_sal_obj_key_t* key);
} rgw_sal_driver_vtable_t;
```

### 4.3 类型定义

#### 4.3.1 用户相关类型

```c:52:155:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal_types.h
// 用户配额信息
typedef struct rgw_sal_quota_info {
    int64_t max_size;            /**< 最大存储大小 (-1 表示无限制) */
    int64_t max_objects;         /**< 最大对象数 (-1 表示无限制) */
    bool enabled;                /**< 是否启用配额 */
    bool check_on_raw;           /**< 是否检查原始大小 */
} rgw_sal_quota_info_t;

// 用户版本跟踪器
typedef struct rgw_sal_obj_version {
    uint64_t ver;                /**< 版本号 */
    uint32_t epoch;              /**< 时代 */
    bool exists;                 /**< 是否存在 */
} rgw_sal_obj_version_t;

// 用户标识
typedef struct rgw_sal_user_id {
    char* id;           /**< 用户 ID */
    char* tenant;       /**< 租户 */
    char* ns;           /**< 命名空间 */
    uint32_t type;      /**< 用户类型 */
} rgw_sal_user_id_t;

// 访问密钥
typedef struct rgw_sal_access_key {
    char* id;               /**< 访问密钥 ID */
    char* key;              /**< 秘密密钥 */
    char* subuser;          /**< 子用户 */
} rgw_sal_access_key_t;

// 用户信息
typedef struct rgw_sal_user_info {
    rgw_sal_user_id_t user_id;          /**< 用户 ID */
    char* display_name;                  /**< 显示名称 */
    char* email;                         /**< 邮箱 */
    rgw_sal_access_key_t* access_keys;   /**< S3 访问密钥数组 */
    size_t num_access_keys;              /**< S3 访问密钥数量 */
    // ... 更多字段
} rgw_sal_user_info_t;
```

#### 4.3.2 桶相关类型

```c:182:201:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal_types.h
// 桶标识
typedef struct rgw_sal_bucket_id {
    char* name;         /**< 桶名称 */
    char* tenant;       /**< 租户 */
    char* marker;       /**< 桶标记 */
    char* bucket_id;    /**< 桶 ID */
} rgw_sal_bucket_id_t;

// 桶信息
typedef struct rgw_sal_bucket_info {
    rgw_sal_bucket_id_t bucket;
    rgw_sal_user_id_t owner;
    char* zone_group;
    uint32_t placement_rule;
    uint64_t size;
    uint64_t size_rounded;
    uint32_t object_count;
} rgw_sal_bucket_info_t;
```

#### 4.3.3 对象相关类型

```c:207:223:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal_types.h
// 对象键
typedef struct rgw_sal_obj_key {
    char* name;         /**< 对象名称 */
    char* instance;     /**< 版本 ID */
    bool is_null;       /**< 是否为空 */
    bool is_current;    /**< 是否为当前版本 */
} rgw_sal_obj_key_t;

// 对象标识
typedef struct rgw_sal_object_id {
    rgw_sal_bucket_id_t bucket;
    rgw_sal_obj_key_t key;
} rgw_sal_object_id_t;
```

#### 4.3.4 属性映射

```c:229:247:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\core\rgw_sal_types.h
// 属性键值对
typedef struct rgw_sal_attr_pair {
    char* key;
    uint8_t* value;
    size_t value_len;
} rgw_sal_attr_pair_t;

// 属性映射
struct rgw_sal_attrs {
    rgw_sal_attr_pair_t* pairs;
    size_t count;
    size_t capacity;
};
```

### 4.4 Usage 统计类型

```c:47:152:d:\NAS\ceph-20.1.1\src\rgw\sal_c\include\rgw_sal_usage.h
// Usage 读取迭代器
typedef struct rgw_usage_iter {
    char* read_iter;    /**< 读取位置迭代器 (序列化字符串) */
    uint32_t index;    /**< 当前分片索引 */
} rgw_usage_iter_t;

// Usage 数据
typedef struct rgw_usage_data {
    uint64_t bytes_sent;       /**< 发送字节数 */
    uint64_t bytes_received;   /**< 接收字节数 */
    uint64_t ops;              /**< 操作数 */
    uint64_t successful_ops;   /**< 成功操作数 */
} rgw_usage_data_t;

// Usage 类别映射
typedef struct rgw_usage_map {
    rgw_usage_map_entry_t* entries;
    size_t count;
    size_t capacity;
} rgw_usage_map_t;

// Usage 日志条目
typedef struct rgw_usage_log_entry {
    char* owner_id;
    char* payer_id;
    char* bucket;
    uint64_t epoch;
    rgw_usage_data_t total_usage;
    rgw_usage_map_t usage_map;
    rgw_sal_s3select_usage_t s3select_usage;
} rgw_usage_log_entry_t;

// User-Bucket 键
typedef struct rgw_sal_user_bucket {
    char* user;
    char* bucket;
} rgw_sal_user_bucket_t;

// Usage 集合
typedef struct rgw_usage_entries {
    struct {
        char* key;
        rgw_usage_log_entry_t entry;
    }* entries;
    size_t count;
    size_t capacity;
} rgw_usage_entries_t;
```

---

## 5. API 函数列表

### 5.1 SAL 核心 API

#### 5.1.1 驱动操作

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_create_driver` | 创建SAL驱动 | rgw_sal.h:308 |
| `rgw_sal_destroy_driver` | 销毁SAL驱动 | rgw_sal.h:314 |
| `rgw_sal_init_driver` | 初始化驱动 | rgw_sal.h:327 |
| `rgw_sal_get_driver_name` | 获取驱动名称 | rgw_sal.h:334 |

#### 5.1.2 用户操作

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_get_user` | 获取用户 | rgw_sal.h:346 |
| `rgw_sal_get_user_by_access_key` | 通过access_key获取用户 | rgw_sal.h:358 |
| `rgw_sal_get_user_by_email` | 通过email获取用户 | rgw_sal.h:371 |
| `rgw_sal_user_load` | 加载用户 | rgw_sal.h:382 |
| `rgw_sal_user_store` | 存储用户 | rgw_sal.h:393 |
| `rgw_sal_user_remove` | 删除用户 | rgw_sal.h:404 |
| `rgw_sal_user_destroy` | 销毁用户 | rgw_sal.h:411 |

#### 5.1.3 桶操作

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_get_bucket` | 获取桶 | rgw_sal.h:423 |
| `rgw_sal_list_buckets` | 列出桶 | rgw_sal.h:441 |
| `rgw_sal_create_bucket` | 创建桶 | rgw_sal.h:459 |
| `rgw_sal_bucket_remove` | 删除桶 | rgw_sal.h:472 |
| `rgw_sal_bucket_destroy` | 销毁桶 | rgw_sal.h:479 |

#### 5.1.4 对象操作

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_get_object` | 获取对象 | rgw_sal.h:492 |
| `rgw_sal_object_read` | 读取对象 | rgw_sal.h:507 |
| `rgw_sal_object_write` | 写入对象 | rgw_sal.h:522 |
| `rgw_sal_object_delete` | 删除对象 | rgw_sal.h:535 |
| `rgw_sal_object_destroy` | 销毁对象 | rgw_sal.h:542 |

#### 5.1.5 列表操作

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_bucket_list_destroy` | 销毁桶列表 | rgw_sal.h:552 |
| `rgw_sal_object_list_destroy` | 销毁对象列表 | rgw_sal.h:558 |

### 5.2 Usage 统计 API

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_usage_iter_create` | 创建迭代器 | rgw_sal_usage.h:56 |
| `rgw_usage_iter_reset` | 重置迭代器 | rgw_sal_usage.h:62 |
| `rgw_usage_iter_destroy` | 销毁迭代器 | rgw_sal_usage.h:68 |
| `rgw_usage_data_create` | 创建Usage数据 | rgw_sal_usage.h:158 |
| `rgw_usage_data_destroy` | 销毁Usage数据 | rgw_sal_usage.h:164 |
| `rgw_usage_data_aggregate` | 聚合Usage数据 | rgw_sal_usage.h:171 |
| `rgw_usage_log_entry_create` | 创建日志条目 | rgw_sal_usage.h:177 |
| `rgw_usage_log_entry_destroy` | 销毁日志条目 | rgw_sal_usage.h:183 |
| `rgw_usage_log_entry_aggregate` | 聚合日志条目 | rgw_sal_usage.h:190 |
| `rgw_usage_log_entry_add_usage` | 添加类别Usage | rgw_sal_usage.h:198 |
| `rgw_usage_log_entry_sum` | 计算Usage总和 | rgw_sal_usage.h:207 |
| `rgw_usage_entries_create` | 创建Usage集合 | rgw_sal_usage.h:214 |
| `rgw_usage_entries_destroy` | 销毁Usage集合 | rgw_sal_usage.h:220 |
| `rgw_usage_entries_add` | 添加Usage条目 | rgw_sal_usage.h:229 |
| `rgw_usage_entries_aggregate` | 聚合Usage条目 | rgw_sal_usage.h:240 |
| `rgw_usage_log_entry_encode` | 序列化日志条目 | rgw_sal_usage.h:286 |
| `rgw_usage_log_entry_decode` | 反序列化日志条目 | rgw_sal_usage.h:296 |
| `rgw_usage_log_hash` | 计算Usage哈希 | rgw_sal_usage.h:314 |
| `rgw_usage_generate_oid` | 生成Usage对象OID | rgw_sal_usage.h:324 |
| `rgw_usage_get_default_config` | 获取默认配置 | rgw_sal_usage.h:343 |
| `rgw_usage_read_omap` | 读取Usage OMAP | rgw_sal_usage.h:366 |
| `rgw_usage_trim_omap` | 清理Usage OMAP | rgw_sal_usage.h:388 |
| `rgw_usage_clear_omap` | 清空Usage OMAP | rgw_sal_usage.h:402 |

### 5.3 RADOS 驱动 API

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_rados_driver_create` | 创建RADOS驱动 | rgw_sal_rados.h:64 |
| `rgw_sal_rados_get_impl` | 获取驱动实现 | rgw_sal_rados.h:71 |
| `rgw_sal_rados_get_cluster_id` | 获取集群ID | rgw_sal_rados.h:85 |
| `rgw_sal_rados_get_user_ctl` | 获取用户控制接口 | rgw_sal_rados.h:95 |
| `rgw_sal_rados_complete_flush_stats` | 刷新统计 | rgw_sal_rados.h:105 |
| `rgw_sal_rados_user_get_internal` | 获取内部用户指针 | rgw_sal_rados.h:119 |
| `rgw_sal_rados_user_from_internal` | 从内部用户创建SAL用户 | rgw_sal_rados.h:127 |
| `rgw_sal_rados_bucket_get_internal` | 获取内部桶指针 | rgw_sal_rados.h:139 |
| `rgw_sal_rados_bucket_from_internal` | 从内部桶创建SAL桶 | rgw_sal_rados.h:147 |
| `rgw_sal_rados_object_get_internal` | 获取内部对象指针 | rgw_sal_rados.h:159 |
| `rgw_sal_rados_object_from_internal` | 从内部对象创建SAL对象 | rgw_sal_rados.h:167 |
| `rgw_sal_rados_object_read_prepare` | 读操作准备 | rgw_sal_rados.h:177 |
| `rgw_sal_rados_object_read_iterate` | 异步读操作 | rgw_sal_rados.h:194 |
| `rgw_sal_rados_object_get_attr` | 获取对象属性 | rgw_sal_rados.h:211 |

### 5.4 类型管理 API

| 函数名 | 描述 | 头文件 |
|--------|------|--------|
| `rgw_sal_user_id_create` | 创建用户ID | rgw_sal_types.h:352 |
| `rgw_sal_user_id_destroy` | 销毁用户ID | rgw_sal_types.h:358 |
| `rgw_sal_bucket_id_create` | 创建桶ID | rgw_sal_types.h:364 |
| `rgw_sal_bucket_id_destroy` | 销毁桶ID | rgw_sal_types.h:370 |
| `rgw_sal_obj_key_create` | 创建对象键 | rgw_sal_types.h:376 |
| `rgw_sal_obj_key_destroy` | 销毁对象键 | rgw_sal_types.h:382 |
| `rgw_sal_attrs_create` | 创建属性映射 | rgw_sal_types.h:388 |
| `rgw_sal_attrs_destroy` | 销毁属性映射 | rgw_sal_types.h:394 |
| `rgw_sal_attrs_set` | 设置属性 | rgw_sal_types.h:404 |
| `rgw_sal_attrs_get` | 获取属性 | rgw_sal_types.h:415 |

### 5.5 c_common 容器 API

#### 5.5.1 动态数组 (rgw_carray.h) - 等效于 `std::vector`

| 函数名 | 描述 |
|--------|------|
| `rgw_array_create` | 创建动态数组 |
| `rgw_array_destroy` | 销毁动态数组 |
| `rgw_array_append` | 追加元素 |
| `rgw_array_insert` | 插入元素 |
| `rgw_array_erase` | 删除元素 |
| `rgw_array_get` | 获取元素(const) |
| `rgw_array_get_mut` | 获取元素(可变) |
| `rgw_array_size` | 获取元素数量 |
| `rgw_array_empty` | 检查是否为空 |
| `rgw_array_capacity` | 获取容量 |
| `rgw_array_clear` | 清空数组 |
| `rgw_array_reserve` | 预留容量 |
| `rgw_array_resize` | 调整大小 |
| `rgw_array_swap` | 交换元素 |

#### 5.5.2 字符串 (rgw_cstring.h) - 等效于 `std::string`

| 函数名 | 描述 |
|--------|------|
| `rgw_string_create` | 从C字符串创建 |
| `rgw_string_create_with_capacity` | 创建指定容量字符串 |
| `rgw_string_create_from_data` | 从二进制数据创建 |
| `rgw_string_destroy` | 销毁字符串 |
| `rgw_string_assign` | 赋值 |
| `rgw_string_append` | 追加C字符串 |
| `rgw_string_append_format` | 追加格式化字符串 |
| `rgw_string_append_data` | 追加二进制数据 |
| `rgw_string_insert` | 插入字符串 |
| `rgw_string_erase` | 删除字符 |
| `rgw_string_replace` | 替换字符串 |
| `rgw_string_c_str` | 获取C字符串 |
| `rgw_string_data` | 获取数据指针 |
| `rgw_string_length` | 获取长度 |
| `rgw_string_capacity` | 获取容量 |
| `rgw_string_empty` | 检查是否为空 |
| `rgw_string_clear` | 清空字符串 |
| `rgw_string_compare` | 比较字符串 |
| `rgw_string_compare_cstr` | 与C字符串比较 |
| `rgw_string_find` | 查找子串 |
| `rgw_string_find_char` | 查找字符 |
| `rgw_string_substring` | 获取子串 |
| `rgw_string_trim` | 去除首尾空白 |
| `rgw_string_to_upper` | 转换为大写 |
| `rgw_string_to_lower` | 转换为小写 |
| `rgw_string_dup` | 复制字符串 |

#### 5.5.3 有序Map (rgw_cmap.h) - 等效于 `std::map`

| 函数名 | 描述 |
|--------|------|
| `rgw_map_create` | 创建Map |
| `rgw_map_destroy` | 销毁Map |
| `rgw_map_insert` | 插入键值对 |
| `rgw_map_find` | 查找键 |
| `rgw_map_erase` | 删除键 |
| `rgw_map_contains` | 检查键是否存在 |
| `rgw_map_size` | 获取元素数量 |
| `rgw_map_empty` | 检查是否为空 |
| `rgw_map_clear` | 清空Map |
| `rgw_map_begin` | 获取起始迭代器 |
| `rgw_map_end` | 获取结束迭代器 |
| `rgw_map_iterator_next` | 迭代器前进 |
| `rgw_map_iterator_valid` | 迭代器是否有效 |
| `rgw_map_iterator_key` | 获取迭代器当前键 |
| `rgw_map_iterator_value` | 获取迭代器当前值 |
| `rgw_map_iterator_destroy` | 销毁迭代器 |

#### 5.5.4 其他容器

| 容器 | C++ 等价 | 主要 API |
|------|----------|----------|
| `rgw_cset.h` | `std::set` | `rgw_set_create_string`, `rgw_set_insert_string`, `rgw_set_contains_string` |
| `rgw_clist.h` | `std::list` | `rgw_clist_create`, `rgw_clist_add_tail`, `rgw_clist_get`, `rgw_clist_remove` |
| `rgw_cdeque.h` | `std::deque` | `rgw_deque_create`, `rgw_deque_push_back`, `rgw_deque_push_front`, `rgw_deque_pop_back` |
| `rgw_cstack.h` | `std::stack` | `rgw_stack_create`, `rgw_stack_push`, `rgw_stack_pop`, `rgw_stack_top` |
| `rgw_cqueue.h` | `std::queue` | `rgw_queue_create`, `rgw_queue_push`, `rgw_queue_pop`, `rgw_queue_front` |
| `rgw_cpriority_queue.h` | `std::priority_queue` | `rgw_priority_queue_create`, `rgw_priority_queue_push`, `rgw_priority_queue_pop`, `rgw_priority_queue_top` |
| `rgw_chash_map.h` | `std::unordered_map` | `rgw_hash_map_create`, `rgw_hash_map_insert`, `rgw_hash_map_find`, `rgw_hash_map_erase` |
| `rgw_coptional.h` | `std::optional` | `rgw_optional_init`, `rgw_optional_set`, `rgw_optional_has_value`, `rgw_optional_get` |

### 5.6 编解码 API

#### 5.6.1 Base64 (rgw_b64.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_b64_encode` | Base64编码 |
| `rgw_b64_decode` | Base64解码 |
| `rgw_b64_encode_binary` | Base64编码(内存到内存) |
| `rgw_b64_decode_binary` | Base64解码(内存到内存) |
| `rgw_b64_encoded_len` | 计算编码后长度 |
| `rgw_b64_decoded_len_max` | 计算解码后最大长度 |

#### 5.6.2 十六进制 (rgw_hex.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_hex_encode` | 十六进制编码 |
| `rgw_hex_decode` | 十六进制解码 |
| `rgw_hex_encode_upper` | 十六进制编码(大写) |
| `rgw_hex_encoded_length` | 计算编码后长度 |
| `rgw_hex_decoded_length` | 计算解码后长度 |
| `rgw_hex_is_valid_char` | 检查是否有效十六进制字符 |

### 5.7 RADOS 操作 API

#### 5.7.1 对象操作 (rgw_rados_object.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_object_read_ctx_create` | 创建读取上下文 |
| `rgw_object_read_ctx_destroy` | 销毁读取上下文 |
| `rgw_object_read` | 读取对象数据 |
| `rgw_object_read_full` | 读取完整对象 |
| `rgw_object_write_ctx_create` | 创建写入上下文 |
| `rgw_object_write_ctx_destroy` | 销毁写入上下文 |
| `rgw_object_write` | 写入对象数据 |
| `rgw_object_write_flush` | 刷新写入缓冲区 |
| `rgw_object_write_full` | 写入完整对象 |
| `rgw_object_append` | 追加数据到对象 |
| `rgw_object_delete` | 删除对象 |
| `rgw_object_delete_async` | 异步删除对象 |
| `rgw_object_stat` | 获取对象元数据 |
| `rgw_object_meta_free` | 释放对象元数据 |
| `rgw_object_exists` | 检查对象是否存在 |
| `rgw_object_set_xattr` | 设置对象扩展属性 |
| `rgw_object_get_xattr` | 获取对象扩展属性 |
| `rgw_object_del_xattr` | 删除对象扩展属性 |
| `rgw_object_xattr_free` | 释放扩展属性值 |
| `rgw_object_truncate` | 截断对象 |
| `rgw_object_copy` | 复制对象 |

#### 5.7.2 OMAP操作 (rgw_omap.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_omap_get` | 获取单个OMAP值 |
| `rgw_omap_set` | 设置单个OMAP键值对 |
| `rgw_omap_del` | 删除单个OMAP键 |
| `rgw_omap_set_multi` | 设置多个OMAP键值对 |
| `rgw_omap_del_multi` | 删除多个OMAP键 |
| `rgw_omap_get_multi` | 获取多个OMAP键值对 |
| `rgw_omap_get_all` | 获取所有OMAP键值对 |
| `rgw_omap_get_keys` | 获取所有OMAP键 |
| `rgw_omap_write_ctx_create` | 创建写入上下文 |
| `rgw_omap_write_ctx_execute` | 执行写入操作 |
| `rgw_omap_write_ctx_destroy` | 销毁写入上下文 |
| `rgw_omap_read_ctx_create` | 创建读取上下文 |
| `rgw_omap_read_ctx_execute` | 执行读取操作 |
| `rgw_omap_read_ctx_destroy` | 销毁读取上下文 |
| `rgw_omap_iter_create` | 创建迭代器 |
| `rgw_omap_iter_next` | 获取下一个键值对 |
| `rgw_omap_iter_destroy` | 销毁迭代器 |

#### 5.7.3 RADOS上下文 (rgw_rados_ctx.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_rados_ctx_create` | 创建RADOS上下文 |
| `rgw_rados_ctx_connect` | 连接到RADOS集群 |
| `rgw_rados_ctx_disconnect` | 断开连接 |
| `rgw_rados_ctx_destroy` | 销毁上下文 |
| `rgw_rados_ctx_open_pool` | 打开池的IO上下文 |
| `rgw_rados_ctx_open_meta_pool` | 打开预定义元数据池 |
| `rgw_rados_ctx_get_pool` | 获取池的IO上下文 |

### 5.8 序列化 API

#### 5.8.1 桶序列化 (rgw_bucket_serde.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_bucket_info_calc_encode_size` | 计算编码后大小 |
| `rgw_bucket_info_encode` | 编码桶信息 |
| `rgw_bucket_info_encode_alloc` | 动态编码桶信息 |
| `rgw_bucket_info_decode` | 解码桶信息 |
| `rgw_bucket_info_free_members` | 释放动态内存 |
| `rgw_bucket_info_init` | 初始化桶信息 |
| `rgw_bucket_info_destroy` | 销毁桶信息 |
| `rgw_bucket_info_create` | 创建桶信息 |
| `rgw_bucket_info_deep_copy` | 深拷贝桶信息 |
| `rgw_bucket_entrypoint_encode` | 编码桶入口点 |
| `rgw_bucket_entrypoint_decode` | 解码桶入口点 |

#### 5.8.2 用户序列化 (rgw_user_serde.h)

| 函数名 | 描述 |
|--------|------|
| `rgw_user_info_calc_encode_size` | 计算编码后大小 |
| `rgw_user_info_encode` | 编码用户信息 |
| `rgw_user_info_encode_alloc` | 动态编码用户信息 |
| `rgw_user_info_decode` | 解码用户信息 |
| `rgw_user_info_free_members` | 释放动态内存 |
| `rgw_user_info_init` | 初始化用户信息 |
| `rgw_user_info_destroy` | 销毁用户信息 |
| `rgw_user_info_create` | 创建用户信息 |
| `rgw_user_info_deep_copy` | 深拷贝用户信息 |
| `rgw_user_id_equal` | 比较两个用户ID |
| `rgw_user_id_to_string` | 获取用户ID字符串表示 |
| `rgw_user_id_parse` | 解析用户ID字符串 |

---

## 6. 驱动实现

### 6.1 RADOS 驱动内部结构

```c:29:115:d:\NAS\ceph-20.1.1\src\rgw\sal_c\src\drivers\rgw_sal_rados.c
// RADOS 用户实现
typedef struct rados_user_impl {
    char* id;
    char* tenant;
    char* display_name;
    char* email;
    char* ns;
    uint32_t user_type;
    int32_t max_buckets;
    rgw_sal_attrs_t* attrs;
    rgw_sal_quota_info_t quota_info;
    rgw_sal_user_caps_t user_caps;
    rgw_sal_obj_version_tracker_t version_tracker;
    rgw_sal_usage_info_t usage;
    bool usage_loaded;
    bool loaded;
    rgw_user_info_t user_info;
    bool user_info_stored;
} rados_user_impl_t;

// RADOS 桶实现
typedef struct rados_bucket_impl {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    rgw_sal_attrs_t* attrs;
    void* acl;
    void* policy;
    char* tag;
    bool loaded;
    bool created;
    bool deleted;
    time_t mtime;
} rados_bucket_impl_t;

// RADOS 对象实现
typedef struct rados_object_impl {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    char* bucket_id;
    rgw_sal_attrs_t* attrs;
    bool is_null;
    int64_t size;
    time_t mtime;
    bool written;
    bool deleted;
    bool loaded;
    bool is_atomic;
    bool is_expired;
    rados_ioctx_t data_ioctx;
} rados_object_impl_t;

// RADOS 驱动实现
typedef struct rados_driver_impl {
    char name[64];
    void* rados_handle;
    void* cct;
    bool initialized;
    rados_ioctx_t users_uid_ioctx;
    rados_ioctx_t users_email_ioctx;
    rados_ioctx_t users_keys_ioctx;
    rados_ioctx_t users_swift_ioctx;
    rados_ioctx_t buckets_index_ioctx;
    rados_ioctx_t buckets_data_ioctx;
    bool ioctxs_initialized;
} rados_driver_impl_t;
```

### 6.2 驱动 IO 上下文池

RADOS 驱动使用多个 RADOS 池来存储不同的元数据：

| 池名称 | 用途 | 驱动变量 |
|--------|------|----------|
| `.rgw.meta.users.uid` | 用户 UID 索引 | `users_uid_ioctx` |
| `.rgw.meta.users.email` | 用户 Email 索引 | `users_email_ioctx` |
| `.rgw.meta.users.keys` | 用户 Keys 索引 | `users_keys_ioctx` |
| `.rgw.meta.users.swift` | 用户 Swift 索引 | `users_swift_ioctx` |
| `.rgw.buckets.index` | 桶索引 | `buckets_index_ioctx` |
| `.rgw.buckets.data` | 桶数据 | `buckets_data_ioctx` |

### 6.3 驱动初始化流程

```
rgw_sal_create_driver()
    ↓
rados_driver_create()
    ├── 分配 rados_driver_impl_t
    ├── 设置 vtable 指针
    └── 返回 rgw_sal_driver_t*
    ↓
rgw_sal_init_driver()
    ├── rados_create() → 获取 rados_t
    ├── rados_conf_read_file()
    ├── rados_connect()
    └── rados_init_ioctxs()
        ├── rados_ioctx_create() × 6
        └── 初始化所有 IO 上下文
```

---

## 7. 内存管理

### 7.1 内存分配规范

| 分配方式 | 释放方式 | 用途 |
|----------|----------|------|
| `rgw_c_alloc()` | `rgw_c_free()` | 通用内存分配 |
| `malloc()` | `free()` | 临时/短期内存 |
| `strdup()` | `free()` | 字符串复制 |

### 7.2 内存管理规范

1. **分配释放配对**: 每个 `rgw_c_alloc` 或 `malloc` 必须有对应的释放
2. **避免双重释放**: 手动释放后避免再次通过析构函数释放
3. **避免 use-after-free**: 释放后将指针置为 NULL
4. **使用 AddressSanitizer**: 开发阶段使用 ASan 检测内存错误

### 7.3 字符串管理

```c
// 正确的字符串管理示例
char* str = rgw_c_alloc(size);
strcpy(str, "hello");
rgw_c_free(str);
str = NULL;  // 避免 use-after-free

// 或使用 strdup
char* str = strdup("hello");
free(str);
```

---

## 8. 错误处理

### 8.1 错误码定义

```c
typedef enum rgw_sal_error_code {
    RGW_SAL_OK = 0,
    
    // 通用错误 (1-10)
    RGW_SAL_ERR_INVALID_ARG = 1,
    RGW_SAL_ERR_OUT_OF_MEMORY = 2,
    RGW_SAL_ERR_NOT_FOUND = 3,
    RGW_SAL_ERR_ALREADY_EXISTS = 4,
    RGW_SAL_ERR_PERMISSION_DENIED = 5,
    RGW_SAL_ERR_TIMEOUT = 6,
    RGW_SAL_ERR_IO_ERROR = 7,
    RGW_SAL_ERR_NOT_IMPLEMENTED = 8,
    RGW_SAL_ERR_NOT_INITIALIZED = 9,
    RGW_SAL_ERR_INTERNAL_ERROR = 10,
    
    // 用户相关 (100-199)
    RGW_SAL_ERR_USER_NOT_FOUND = 100,
    RGW_SAL_ERR_USER_EXISTS = 101,
    RGW_SAL_ERR_INVALID_USER = 102,
    
    // 桶相关 (200-299)
    RGW_SAL_ERR_BUCKET_NOT_FOUND = 200,
    RGW_SAL_ERR_BUCKET_EXISTS = 201,
    RGW_SAL_ERR_BUCKET_NOT_EMPTY = 202,
    RGW_SAL_ERR_INVALID_BUCKET = 203,
    
    // 对象相关 (300-399)
    RGW_SAL_ERR_OBJECT_NOT_FOUND = 300,
    RGW_SAL_ERR_OBJECT_EXISTS = 301,
    RGW_SAL_ERR_INVALID_OBJECT = 302,
    RGW_SAL_ERR_OBJECT_TOO_LARGE = 303,
    
    // 版本控制 (400-499)
    RGW_SAL_ERR_VERSION_CONFLICT = 400,
    RGW_SAL_ERR_NO_SUCH_VERSION = 401,
    
    // 多部分上传 (500-599)
    RGW_SAL_ERR_UPLOAD_NOT_FOUND = 500,
    RGW_SAL_ERR_UPLOAD_PART_NOT_FOUND = 501,
    
    // 配额 (600-699)
    RGW_SAL_ERR_QUOTA_EXCEEDED = 600,
    
    RGW_SAL_ERR_UNKNOWN = 999
} rgw_sal_error_code_t;
```

### 8.2 错误处理规范

1. **所有函数返回错误码**: C 函数不能使用异常
2. **检查返回值**: 调用者必须检查返回值
3. **资源清理**: 错误发生时必须清理已分配的资源
4. **传播错误**: 底层错误应传播到调用者

---

## 附录 A: 驱动对比

| 驱动 | 完成度 | 状态 | 文件 |
|------|--------|------|------|
| RADOS | ~90% | 进行中 | rgw_sal_rados.c (~2700行) |
| DBStore | ~90% | 进行中 | rgw_sal_dbstore.c (~2300行) |
| POSIX | ~40% | 待完善 | rgw_sal_posix.c (~1100行) |
| D4N | ~30% | 待完善 | rgw_sal_d4n.c (~900行) |
| DAOS | ~30% | 待完善 | rgw_sal_daos.c |
| Motr | ~30% | 待完善 | rgw_sal_motr.c |

---

## 附录 B: c_common 容器与 C++ 等价对照

| C 容器 | C++ 等价 | 实现方式 | 头文件 |
|--------|----------|----------|--------|
| `rgw_array_t` | `std::vector` | 动态数组 | rgw_carray.h |
| `rgw_string_t` | `std::string` | 动态字符串 | rgw_cstring.h |
| `rgw_map_t` | `std::map` | 红黑树 | rgw_cmap.h |
| `rgw_set_t` | `std::set` | 红黑树 | rgw_cset.h |
| `rgw_clist_t` | `std::list` | 双向链表 | rgw_clist.h |
| `rgw_deque_t` | `std::deque` | 双端队列 | rgw_cdeque.h |
| `rgw_stack_t` | `std::stack` | 栈封装 | rgw_cstack.h |
| `rgw_queue_t` | `std::queue` | 队列封装 | rgw_cqueue.h |
| `rgw_priority_queue_t` | `std::priority_queue` | 优先队列 | rgw_cpriority_queue.h |
| `rgw_hash_map_t` | `std::unordered_map` | 哈希表 | rgw_chash_map.h |
| `rgw_optional_t` | `std::optional` | 可选类型 | rgw_coptional.h |

---

**文档结束**

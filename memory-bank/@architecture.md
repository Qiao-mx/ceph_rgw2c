# RGW C 代码架构设计文档

## 1. 项目概述

本项目是将 Ceph RGW (RADOS Gateway) 的 C++ 代码转换为 C 语言实现的项目，主要包含两个核心模块：

- **c_common** - 提供基础容器、内存管理、序列化等通用功能
- **sal_c** - 提供存储抽象层 (SAL) 的 C 语言实现和多种存储驱动

## 2. 目录结构

### 2.1 c_common 目录结构

```
src/rgw/c_common/
├── include/                    # 头文件
│   ├── containers/             # 容器类头文件
│   │   ├── rgw_carray.h       # 动态数组 (std::vector)
│   │   ├── rgw_cstring.h      # 字符串 (std::string)
│   │   ├── rgw_cmap.h          # 有序 Map (std::map)
│   │   ├── rgw_cset.h          # 有序 Set (std::set)
│   │   ├── rgw_clist.h         # 双向链表 (std::list)
│   │   ├── rgw_cdeque.h       # 双端队列 (std::deque)
│   │   ├── rgw_cstack.h       # 栈 (std::stack)
│   │   ├── rgw_cqueue.h       # 队列 (std::queue)
│   │   ├── rgw_cpriority_queue.h  # 优先队列 (std::priority_queue)
│   │   ├── rgw_chash_map.h    # 哈希表 (std::unordered_map)
│   │   ├── rgw_coptional.h    # 可选类型 (std::optional)
│   │   ├── rgw_cmemory.h      # 内存管理
│   │   └── rgw_ctypes.h       # 基础类型定义
│   ├── internal/               # 内部实现头文件
│   │   ├── rgw_list.h        # 链表定义
│   │   ├── rgw_rbtree.h       # 红黑树定义
│   │   ├── rgw_rbt_node.h     # 红黑树节点定义
│   │   ├── rgw_avltree.h      # AVL 树定义
│   │   ├── uthash.h           # uthash 库
│   │   ├── utarray.h          # uthash 动态数组
│   │   ├── utstring.h         # uthash 字符串
│   │   ├── utlist.h           # uthash 链表
│   │   ├── utstack.h          # uthash 栈
│   │   └── utringbuffer.h      # uthash 环形缓冲区
│   ├── rgw_ccommon.h          # 统一头文件（包含所有容器）
│   ├── rgw_b64.h              # Base64 编码/解码
│   ├── rgw_hex.h              # Hex 编码/解码
│   ├── rgw_xml.h              # XML 解析
│   ├── rgw_buffer.h           # 缓冲区管理
│   ├── rgw_oop.h              # OOP 框架
│   ├── rgw_errors.h           # 错误处理
│   ├── rgw_sal.h              # SAL 接口
│   ├── rgw_sal_types.h        # SAL 类型定义
│   ├── rgw_omap.h             # OMAP 操作
│   ├── rgw_sqlite.h           # SQLite 封装
│   ├── csort.h                # 排序工具
│   ├── rgw_user_serde.h       # 用户信息序列化
│   ├── rgw_bucket_serde.h     # 桶信息序列化
│   ├── rgw_account_serde.h   # 账户信息序列化
│   ├── rgw_acl_serde.h        # ACL 序列化
│   ├── rgw_policy_serde.h     # IAM 策略序列化
│   ├── rgw_lifecycle.h       # 生命周期序列化
│   ├── rgw_multipart.h        # 多部分上传序列化
│   ├── rgw_notification.h     # 通知序列化
│   ├── rgw_group_serde.h     # 组序列化
│   ├── rgw_oidc_serde.h       # OIDC 序列化
│   ├── rgw_rados_object.h    # RADOS 对象操作
│   ├── rgw_rados_ctx.h        # RADOS 上下文
│   ├── rgw_rados_user.h       # RADOS 用户操作
│   ├── rgw_rados_bucket.h    # RADOS 桶操作
│   └── rgw_rados_obj.h       # RADOS 对象操作
├── src/                       # 源代码
│   ├── rgw_carray.c           # 动态数组实现
│   ├── rgw_cstring.c          # 字符串实现
│   ├── rgw_cmap.c             # 有序 Map 实现
│   ├── rgw_cdeque.c           # 双端队列实现
│   ├── rgw_cstack.c           # 栈实现
│   ├── rgw_cqueue.c           # 队列实现
│   ├── rgw_cpriority_queue.c  # 优先队列实现
│   ├── rgw_coptional.c        # 可选类型实现
│   ├── rgw_cmemory.c          # 内存管理实现
│   ├── rgw_b64.c              # Base64 实现
│   ├── rgw_hex.c              # Hex 实现
│   ├── rgw_xml.c              # XML 实现
│   ├── rgw_buffer.c           # 缓冲区实现
│   ├── rgw_oop.c              # OOP 框架实现
│   ├── rgw_errors.c           # 错误处理实现
│   ├── rgw_omap.c             # OMAP 操作实现
│   ├── rgw_sqlite.c           # SQLite 封装实现
│   ├── csort.c                # 排序实现
│   ├── rgw_sal.c              # SAL 实现
│   ├── rgw_sal_types.c        # SAL 类型实现
│   ├── rgw_sal_attrs.c        # SAL 属性实现
│   ├── rgw_user_serde.c       # 用户序列化实现
│   ├── rgw_bucket_serde.c     # 桶序列化实现
│   ├── rgw_account_serde.c    # 账户序列化实现
│   ├── rgw_acl_serde.c        # ACL 序列化实现
│   ├── rgw_policy_serde.c     # 策略序列化实现
│   ├── rgw_lifecycle.c        # 生命周期序列化实现
│   ├── rgw_multipart.c        # 多部分上传序列化实现
│   ├── rgw_notification.c     # 通知序列化实现
│   ├── rgw_group_serde.c      # 组序列化实现
│   ├── rgw_oidc_serde.c       # OIDC 序列化实现
│   ├── rgw_rados_object.c    # RADOS 对象操作实现
│   ├── rgw_rados_ctx.c        # RADOS 上下文实现
│   ├── rgw_rados_user.c       # RADOS 用户操作实现
│   └── rgw_rados_bucket.c     # RADOS 桶操作实现
├── containers/                 # 容器实现 (第三方库)
│   ├── rgw_chash_map.c        # 哈希表实现
│   ├── rgw_clist.c            # 链表实现
│   ├── rgw_cset.c             # 有序 Set 实现
│   ├── rgw_splaytree.c        # 伸展树实现
│   ├── rgw_avl.c              # AVL 树实现
│   ├── rgw_bst.c              # 二叉搜索树实现
│   └── rgw_rbtree.c           # 红黑树实现
├── tests/                     # 测试文件
│   ├── test_carray.c          # 数组测试
│   ├── test_cstring.c         # 字符串测试
│   ├── test_cmap.c            # Map 测试
│   ├── test_cset.c            # Set 测试
│   ├── test_clist.c           # 链表测试
│   ├── test_cdeque.c          # 双端队列测试
│   ├── test_cstack.c          # 栈测试
│   ├── test_cqueue.c          # 队列测试
│   ├── test_cpriority_queue.c  # 优先队列测试
│   ├── test_coptional.c        # 可选类型测试
│   ├── test_b64.c             # Base64 测试
│   ├── test_hex.c             # Hex 测试
│   ├── test_buffer.c          # 缓冲区测试
│   ├── test_xml.c             # XML 测试
│   ├── test_oop.c             # OOP 框架测试
│   ├── test_errors.c          # 错误处理测试
│   ├── test_memory.c          # 内存管理测试
│   ├── test_ccontainer.c      # 容器测试
│   ├── test_comprehensive.c   # 综合测试
│   ├── test_benchmark.c       # 性能测试
│   └── test_cpp_to_c.c        # C++ 转 C 测试
└── build/                     # 构建目录
```

### 2.2 sal_c 目录结构

```
src/rgw/sal_c/
├── include/                   # 头文件
│   ├── core/                  # 核心接口
│   │   ├── rgw_sal.h          # SAL 核心接口（虚函数表定义）
│   │   ├── rgw_sal_types.h    # SAL 类型定义
│   │   ├── rgw_sal_errors.h   # SAL 错误码定义
│   │   └── rgw_account_serde.h  # 账户序列化
│   ├── drivers/               # 驱动头文件
│   │   ├── rgw_sal_rados.h    # RADOS 驱动
│   │   ├── rgw_sal_dbstore.h  # DBStore 驱动
│   │   ├── rgw_sal_daos.h     # DAOS 驱动
│   │   ├── rgw_sal_daos_serde.h  # DAOS 序列化
│   │   ├── rgw_sal_daos_types.h  # DAOS 类型
│   │   ├── rgw_sal_posix.h    # POSIX 驱动
│   │   ├── rgw_sal_motr.h     # MOTR 驱动
│   │   └── rgw_sal_d4n.h      # D4N 驱动
│   ├── rgw_sal_errors.h       # SAL 错误定义
│   ├── rgw_sal_usage.h       # 使用统计
│   └── rgw_sal_c_wrapper.h   # C++ 包装器
├── src/                       # 源代码
│   ├── rgw_sal_usage.c       # 使用统计实现
│   └── drivers/               # 驱动实现
│       ├── rgw_sal_rados.c   # RADOS 驱动实现 (主要)
│       ├── rgw_sal_dbstore.c # DBStore 驱动实现
│       ├── rgw_sal_daos.c    # DAOS 驱动实现
│       ├── rgw_sal_daos_serde.c  # DAOS 序列化实现
│       ├── rgw_sal_posix.c   # POSIX 驱动实现
│       ├── rgw_sal_motr.c    # MOTR 驱动实现
│       └── rgw_sal_d4n.c     # D4N 驱动实现
├── tests/                     # 测试文件
│   ├── test_basic.c           # 基础测试
│   ├── test_rados_driver.c   # RADOS 驱动测试
│   ├── test_rados_types.c    # RADOS 类型测试
│   ├── test_integration.c    # 集成测试
│   ├── test_ceph_cluster.c   # Ceph 集群测试
│   ├── benchmark/             # 基准测试
│   │   ├── sal_c_test_framework.c
│   │   └── sal_c_test_framework.h
│   └── CMakeLists.txt        # 测试构建配置
└── docs/                      # 文档
```

## 3. c_common 模块详细分析

### 3.1 容器库 (containers/)

| 文件 | 对应 C++ | 功能描述 |
|------|----------|----------|
| `rgw_carray.h/c` | `std::vector` | 动态数组，支持自动扩容 |
| `rgw_cstring.h` | `std::string` | 字符串管理 |
| `rgw_cmap.h` | `std::map` | 有序映射，红黑树实现 |
| `rgw_cset.h` | `std::set` | 有序集合，红黑树实现 |
| `rgw_clist.h` | `std::list` | 双向链表 |
| `rgw_cdeque.h/c` | `std::deque` | 双端队列 |
| `rgw_cstack.h/c` | `std::stack` | 栈 |
| `rgw_cqueue.h/c` | `std::queue` | 队列 |
| `rgw_cpriority_queue.h/c` | `std::priority_queue` | 优先队列 |
| `rgw_chash_map.h/c` | `std::unordered_map` | 哈希表，uthash 实现 |
| `rgw_coptional.h/c` | `std::optional` | 可选类型 |
| `rgw_cmemory.h` | - | 内存分配/释放封装 |

### 3.2 序列化库 (src/)

| 文件 | 功能描述 | 主要函数 |
|------|----------|----------|
| `rgw_user_serde.h/c` | 用户信息序列化 | `rgw_user_info_encode/decode`, `rgw_user_info_make_omap_key` |
| `rgw_bucket_serde.h/c` | 桶信息序列化 | `rgw_bucket_info_encode/decode`, `rgw_bucket_info_make_omap_key` |
| `rgw_account_serde.h/c` | 账户信息序列化 | `rgw_account_info_encode/decode` |
| `rgw_acl_serde.h/c` | ACL 序列化 | `rgw_acl_encode/decode`, `rgw_acl_to/from_json` |
| `rgw_policy_serde.h/c` | IAM 策略序列化 | `rgw_policy_encode/decode`, `rgw_policy_to/from_json` |
| `rgw_lifecycle.h/c` | 生命周期序列化 | `rgw_lc_entry_encode/decode`, `rgw_lc_head_encode/decode` |
| `rgw_multipart.h/c` | 多部分上传序列化 | `rgw_multipart_info_encode/decode` |
| `rgw_notification.h/c` | 通知序列化 | `rgw_notification_event_encode/decode` |
| `rgw_group_serde.h/c` | 组信息序列化 | - |
| `rgw_oidc_serde.h/c` | OIDC 序列化 | - |

### 3.3 工具库 (src/)

| 文件 | 功能描述 | 主要函数 |
|------|----------|----------|
| `rgw_b64.h/c` | Base64 编解码 | `rgw_b64_encode`, `rgw_b64_decode` |
| `rgw_hex.h/c` | Hex 编解码 | `rgw_hex_encode`, `rgw_hex_decode` |
| `rgw_xml.h/c` | XML 解析 | `rgw_xml_parse`, `rgw_xml_write` |
| `rgw_buffer.h/c` | 缓冲区管理 | `rgw_buffer_create`, `rgw_buffer_append` |
| `rgw_oop.h/c` | OOP 框架 | 虚函数表、引用计数、智能指针 |
| `rgw_errors.h/c` | 错误处理 | 统一错误码定义 |
| `rgw_omap.h/c` | OMAP 操作 | `rgw_omap_get/set/remove` |
| `rgw_sqlite.h/c` | SQLite 封装 | 数据库操作封装 |
| `csort.h/c` | 排序工具 | 各种排序算法 |

## 4. sal_c 模块详细分析

### 4.1 核心接口 (include/core/)

| 文件 | 功能描述 |
|------|----------|
| `rgw_sal.h` | SAL 核心接口，定义虚函数表 (vtable) 和核心结构体 |
| `rgw_sal_types.h` | SAL 类型定义 (用户、桶、对象、配额等) |
| `rgw_sal_errors.h` | SAL 错误码定义 |
| `rgw_account_serde.h` | 账户信息序列化接口 |

### 4.2 存储驱动 (include/drivers/, src/drivers/)

| 驱动 | 文件 | 功能描述 |
|------|------|----------|
| **RADOS** | `rgw_sal_rados.h/c` | Ceph RADOS 存储后端（主要驱动） |
| **DBStore** | `rgw_sal_dbstore.h/c` | 数据库存储后端 |
| **DAOS** | `rgw_sal_daos.h/c`, `rgw_sal_daos_serde.h/c`, `rgw_sal_daos_types.h` | DAOS 存储后端 |
| **POSIX** | `rgw_sal_posix.h/c` | POSIX 文件系统后端 |
| **MOTR** | `rgw_sal_motr.h/c` | MOTR 存储后端 |
| **D4N** | `rgw_sal_d4n.h/c` | D4N 存储后端 |

### 4.3 核心虚函数表

```c
// 用户操作虚函数表
typedef struct rgw_sal_user_vtable {
    void* (*clone)(const rgw_sal_user_t* user);
    void (*destroy)(rgw_sal_user_t* user);
    const char* (*get_id)(const rgw_sal_user_t* user);
    // ... 更多函数指针
} rgw_sal_user_vtable_t;

// 桶操作虚函数表
typedef struct rgw_sal_bucket_vtable {
    void* (*clone)(const rgw_sal_bucket_t* bucket);
    void (*destroy)(rgw_sal_bucket_t* bucket);
    // ... 更多函数指针
} rgw_sal_bucket_vtable_t;

// 对象操作虚函数表
typedef struct rgw_sal_object_vtable {
    void* (*clone)(const rgw_sal_object_t* obj);
    void (*destroy)(rgw_sal_object_t* obj);
    // ... 更多函数指针
} rgw_sal_object_vtable_t;

// 驱动虚函数表
typedef struct rgw_sal_driver_vtable {
    void (*destroy)(rgw_sal_driver_t* driver);
    int (*initialize)(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp);
    // ... 更多函数指针
} rgw_sal_driver_vtable_t;
```

## 5. 核心 API 设计

### 5.1 c_common 核心 API

| 模块 | 主要 API | 功能描述 |
|------|---------|----------|
| 内存管理 | `rgw_c_alloc`, `rgw_c_free` | 内存分配和释放 |
| 动态数组 | `rgw_array_create`, `rgw_array_push`, `rgw_array_get` | 创建和操作动态数组 |
| 字符串 | `rgw_string_create`, `rgw_string_append`, `rgw_string_cstr` | 创建和操作字符串 |
| 映射 | `rgw_map_create`, `rgw_map_insert`, `rgw_map_find` | 创建和操作有序映射 |
| 集合 | `rgw_set_create`, `rgw_set_insert`, `rgw_set_find` | 创建和操作有序集合 |
| 链表 | `rgw_list_create`, `rgw_list_push_back`, `rgw_list_iterate` | 创建和操作双向链表 |
| 哈希表 | `rgw_hash_map_create`, `rgw_hash_map_insert`, `rgw_hash_map_find` | 创建和操作哈希表 |
| Base64 | `rgw_b64_encode`, `rgw_b64_decode` | Base64 编码/解码 |
| XML | `rgw_xml_parse`, `rgw_xml_write` | XML 解析和生成 |
| 错误处理 | `rgw_error_create`, `rgw_error_free` | 错误创建和管理 |

### 5.2 sal_c 核心 API

| 模块 | 主要 API | 功能描述 |
|------|---------|----------|
| 驱动管理 | `rgw_sal_driver_create`, `rgw_sal_destroy_driver` | 创建和销毁驱动 |
| 用户管理 | `rgw_sal_get_user`, `rgw_sal_user_load`, `rgw_sal_user_store` | 用户创建、加载、存储 |
| 桶管理 | `rgw_sal_get_bucket`, `rgw_sal_bucket_create`, `rgw_sal_bucket_store` | 桶创建和管理 |
| 对象管理 | `rgw_sal_get_object`, `rgw_sal_object_read`, `rgw_sal_object_write` | 对象创建和读写 |
| 生命周期 | `rgw_sal_lifecycle_*` | 生命周期管理 |
| 多部分上传 | `rgw_sal_multipart_*` | 多部分上传操作 |
| 通知 | `rgw_sal_notification_*` | 通知管理 |

## 6. 文件命名规范（防止重复生成）

### 6.1 c_common 文件命名

| 类型 | 命名模式 | 示例 |
|------|----------|------|
| 头文件 | `rgw_<module>.h` | `rgw_buffer.h`, `rgw_xml.h` |
| 实现文件 | `rgw_<module>.c` | `rgw_buffer.c`, `rgw_xml.c` |
| 容器头文件 | `rgw_c<container>.h` | `rgw_carray.h`, `rgw_cstring.h` |
| 容器实现 | `rgw_c<container>.c` | `rgw_carray.c`, `rgw_cstring.c` |
| 序列化头文件 | `rgw_<entity>_serde.h` | `rgw_user_serde.h`, `rgw_bucket_serde.h` |
| 序列化实现 | `rgw_<entity>_serde.c` | `rgw_user_serde.c`, `rgw_bucket_serde.c` |
| 内部头文件 | `rgw_<internal>.h` | `rgw_rbtree.h`, `rgw_list.h` |
| 第三方库 | `ut*.h` | `uthash.h`, `utarray.h` |

### 6.2 sal_c 文件命名

| 类型 | 命名模式 | 示例 |
|------|----------|------|
| 核心头文件 | `rgw_sal.h`, `rgw_sal_types.h` | - |
| 驱动头文件 | `rgw_sal_<driver>.h` | `rgw_sal_rados.h`, `rgw_sal_dbstore.h` |
| 驱动实现 | `rgw_sal_<driver>.c` | `rgw_sal_rados.c`, `rgw_sal_dbstore.c` |

### 6.3 已有文件清单（防止重复生成）

**c_common 头文件 (47 个)**:
- 容器: `rgw_carray.h`, `rgw_cstring.h`, `rgw_cmap.h`, `rgw_cset.h`, `rgw_clist.h`, `rgw_cdeque.h`, `rgw_cstack.h`, `rgw_cqueue.h`, `rgw_cpriority_queue.h`, `rgw_chash_map.h`, `rgw_coptional.h`, `rgw_cmemory.h`, `rgw_ctypes.h`
- 序列化: `rgw_user_serde.h`, `rgw_bucket_serde.h`, `rgw_account_serde.h`, `rgw_acl_serde.h`, `rgw_policy_serde.h`, `rgw_lifecycle.h`, `rgw_multipart.h`, `rgw_notification.h`, `rgw_group_serde.h`, `rgw_oidc_serde.h`
- 工具: `rgw_b64.h`, `rgw_hex.h`, `rgw_xml.h`, `rgw_buffer.h`, `rgw_oop.h`, `rgw_errors.h`, `rgw_omap.h`, `rgw_sqlite.h`, `rgw_rados_object.h`, `rgw_rados_ctx.h`, `rgw_rados_user.h`, `rgw_rados_bucket.h`, `rgw_rados_obj.h`
- 核心: `rgw_ccommon.h`, `rgw_sal.h`, `rgw_sal_types.h`
- 内部: `rgw_list.h`, `rgw_rbtree.h`, `rgw_rbt_node.h`, `rgw_avltree.h`
- 第三方: `uthash.h`, `utarray.h`, `utstring.h`, `utlist.h`, `utstack.h`, `utringbuffer.h`, `csort.h`

**c_common 实现文件 (36 个)**:
- 容器: `rgw_carray.c`, `rgw_cstring.c`, `rgw_cmap.c`, `rgw_cdeque.c`, `rgw_cstack.c`, `rgw_cqueue.c`, `rgw_cpriority_queue.c`, `rgw_coptional.c`, `rgw_cmemory.c`
- 序列化: `rgw_user_serde.c`, `rgw_bucket_serde.c`, `rgw_account_serde.c`, `rgw_acl_serde.c`, `rgw_policy_serde.c`, `rgw_lifecycle.c`, `rgw_multipart.c`, `rgw_notification.c`, `rgw_group_serde.c`, `rgw_oidc_serde.c`
- 工具: `rgw_b64.c`, `rgw_hex.c`, `rgw_xml.c`, `rgw_buffer.c`, `rgw_oop.c`, `rgw_errors.c`, `rgw_omap.c`, `rgw_sqlite.c`, `csort.c`
- 核心: `rgw_sal.c`, `rgw_sal_types.c`, `rgw_sal_attrs.c`
- RADOS: `rgw_rados_object.c`, `rgw_rados_ctx.c`, `rgw_rados_user.c`, `rgw_rados_bucket.c`

**c_common 容器实现 (7 个)**:
`rgw_chash_map.c`, `rgw_clist.c`, `rgw_cset.c`, `rgw_splaytree.c`, `rgw_avl.c`, `rgw_bst.c`, `rgw_rbtree.c`

**sal_c 头文件 (14 个)**:
- 核心: `rgw_sal.h`, `rgw_sal_types.h`, `rgw_sal_errors.h`, `rgw_account_serde.h`
- 驱动: `rgw_sal_rados.h`, `rgw_sal_dbstore.h`, `rgw_sal_daos.h`, `rgw_sal_daos_serde.h`, `rgw_sal_daos_types.h`, `rgw_sal_posix.h`, `rgw_sal_motr.h`, `rgw_sal_d4n.h`
- 其他: `rgw_sal_errors.h`, `rgw_sal_usage.h`, `rgw_sal_c_wrapper.h`

**sal_c 实现文件 (9 个)**:
`rgw_sal_usage.c`, `rgw_sal_rados.c`, `rgw_sal_dbstore.c`, `rgw_sal_daos.c`, `rgw_sal_daos_serde.c`, `rgw_sal_posix.c`, `rgw_sal_motr.c`, `rgw_sal_d4n.c`

## 7. 测试架构

### 7.1 c_common 测试 (23 个)

| 测试文件 | 测试内容 |
|----------|----------|
| `test_carray.c` | 动态数组 |
| `test_cstring.c` | 字符串 |
| `test_cmap.c` | 有序映射 |
| `test_cset.c` | 有序集合 |
| `test_clist.c` | 链表 |
| `test_cdeque.c` | 双端队列 |
| `test_cstack.c` | 栈 |
| `test_cqueue.c` | 队列 |
| `test_cpriority_queue.c` | 优先队列 |
| `test_coptional.c` | 可选类型 |
| `test_b64.c` | Base64 编解码 |
| `test_hex.c` | Hex 编解码 |
| `test_buffer.c` | 缓冲区管理 |
| `test_xml.c` | XML 解析 |
| `test_oop.c` | OOP 框架 |
| `test_errors.c` | 错误处理 |
| `test_memory.c` | 内存管理 |
| `test_ccontainer.c` | 容器综合测试 |
| `test_comprehensive.c` | 综合测试 |
| `test_benchmark.c` | 性能测试 |
| `test_cpp_to_c.c` | C++ 转 C 兼容性测试 |

### 7.2 sal_c 测试 (6 个)

| 测试文件 | 测试内容 |
|----------|----------|
| `test_basic.c` | 基础功能测试 |
| `test_rados_driver.c` | RADOS 驱动测试 |
| `test_rados_types.c` | RADOS 类型测试 |
| `test_integration.c` | 集成测试 |
| `test_ceph_cluster.c` | Ceph 集群测试 |

## 8. 依赖关系

### 8.1 c_common 依赖

**内部依赖**:
- 容器实现依赖内部数据结构 (红黑树、AVL树、uthash)
- 工具函数依赖基础库

**外部依赖**:
- librados (用于 RADOS 相关功能)
- SQLite (用于 DBStore 相关功能)

### 8.2 sal_c 依赖

**内部依赖**:
- c_common 库
- C++ 标准库 (用于包装器)

**外部依赖**:
- librados (用于 RADOS 驱动)
- SQLite (用于 DBStore 驱动)
- DAOS 库 (用于 DAOS 驱动)
- MOTR 库 (用于 MOTR 驱动)

## 9. 设计原则

1. **模块化**: 每个功能独立实现，便于维护和测试
2. **可扩展性**: 支持多种存储后端，易于添加新驱动
3. **兼容性**: 保持与原有 C++ 代码的兼容性
4. **可靠性**: 完善的错误处理和资源管理
5. **性能**: 优化内存使用和执行效率

## 10. 代码转换策略

C++ 到 C 的转换策略：

1. **类转换**: 将 C++ 类转换为 C 结构体 + 函数指针
2. **继承转换**: 使用虚函数表实现多态
3. **模板转换**: 使用 void* 和函数指针实现通用容器
4. **异常转换**: 使用错误码替代异常
5. **内存管理**: 使用显式的内存分配和释放

## 11. 防止重复文件生成规则

### 11.1 新增文件前检查

在创建新文件前，必须检查以下位置是否已存在相同或相似的文件：

1. **同名文件检查**: 检查目标目录是否存在同名文件
2. **功能相似检查**: 检查是否存在功能相似的文件（如不同的命名模式）
3. **命名规范检查**: 确保新文件符合上述命名规范

### 11.2 文件搜索优先级

1. `include/` 目录下的头文件
2. `src/` 目录下的实现文件
3. `containers/` 目录下的容器实现
4. `tests/` 目录下的测试文件

### 11.3 禁止重复的内容

以下情况必须避免：

1. **同名不同位置**: 不要在多个位置创建功能相同的文件
2. **功能重叠**: 不要创建与现有文件功能重叠的新文件
3. **名称冲突**: 避免使用可能与现有文件冲突的名称

### 11.4 已有实现清单

**序列化模块** (必须使用现有实现，不要重复创建):
- 用户序列化: `rgw_user_serde.h/c`
- 桶序列化: `rgw_bucket_serde.h/c`
- 账户序列化: `rgw_account_serde.h/c`
- ACL 序列化: `rgw_acl_serde.h/c`
- 策略序列化: `rgw_policy_serde.h/c`
- 生命周期: `rgw_lifecycle.h/c`
- 多部分上传: `rgw_multipart.h/c`
- 通知: `rgw_notification.h/c`

**容器模块** (必须使用现有实现，不要重复创建):
- 数组: `rgw_carray.h/c`
- 字符串: `rgw_cstring.h/c`
- 映射: `rgw_cmap.h/c`
- 集合: `rgw_cset.h/c`
- 链表: `rgw_clist.h/c`
- 哈希表: `rgw_chash_map.h/c`
- 可选: `rgw_coptional.h/c`

**工具模块** (必须使用现有实现，不要重复创建):
- Base64: `rgw_b64.h/c`
- Hex: `rgw_hex.h/c`
- XML: `rgw_xml.h/c`
- 缓冲区: `rgw_buffer.h/c`
- OOP 框架: `rgw_oop.h/c`
- 错误处理: `rgw_errors.h/c`

## 12. 总结

本项目成功将 Ceph RGW 的 C++ 代码转换为 C 语言实现，主要成果包括：

- 实现了完整的 C 语言容器库，替代 C++ 标准库
- 实现了存储抽象层的 C 语言接口
- 支持多种存储后端驱动 (RADOS, DBStore, DAOS, POSIX, MOTR, D4N)
- 提供了 C++ 包装器，保持与原有代码的兼容性
- 完善的测试和错误处理机制

该架构设计为 RGW 提供了更广泛的平台支持，同时保持了代码的可维护性和扩展性。

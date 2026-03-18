# RGW C++ 到 C 转换项目架构文档

## 1. 项目概述

本项目旨在将 Ceph RGW (RADOS Gateway) 模块从 C++ 语言逐步转换为 C 语言，同时保持功能完整性和性能指标。

## 2. 文件结构

### 2.1 根目录结构

```
ceph-20.1.1/
├── src/rgw/
│   ├── c_common/                    # C 通用库（转换的基础设施）
│   │   ├── include/                 # 头文件
│   │   │   ├── containers/          # 容器头文件
│   │   │   │   ├── rgw_carray.h    # 动态数组
│   │   │   │   ├── rgw_cstring.h  # 字符串
│   │   │   │   ├── rgw_cmap.h    # 有序 Map
│   │   │   │   ├── rgw_cset.h     # 有序 Set
│   │   │   │   ├── rgw_clist.h    # 双向链表
│   │   │   │   ├── rgw_cdeque.h   # 双端队列
│   │   │   │   ├── rgw_cstack.h   # 栈
│   │   │   │   ├── rgw_cqueue.h   # 队列
│   │   │   │   ├── rgw_cpriority_queue.h # 优先队列
│   │   │   │   ├── rgw_chash_map.h # 哈希表
│   │   │   │   └── rgw_coptional.h # 可选类型
│   │   │   ├── internal/            # 内部头文件（第三方库）
│   │   │   ├── rgw_ccommon.h        # 统一头文件
│   │   │   ├── rgw_oop.h            # OOP 框架
│   │   │   └── rgw_errors.h         # 错误处理
│   │   ├── src/                     # 源文件
│   │   │   ├── rgw_cmemory.c        # 内存管理
│   │   │   ├── rgw_ctypes.c         # 基础类型
│   │   │   ├── rgw_carray.c         # 动态数组
│   │   │   ├── rgw_cstring.c        # 字符串
│   │   │   ├── rgw_cmap.c           # 有序 Map
│   │   │   ├── rgw_cset.c           # 有序 Set
│   │   │   ├── rgw_coptional.c      # 可选类型
│   │   │   ├── rgw_cdeque.c         # 双端队列
│   │   │   ├── rgw_cstack.c         # 栈
│   │   │   ├── rgw_cqueue.c         # 队列
│   │   │   ├── rgw_oop.c            # OOP 实现
│   │   │   ├── rgw_errors.c         # 错误处理实现
│   │   ├── containers/               # 容器实现（第三方库封装）
│   │   │   ├── rgw_clist.c          # 双向链表
│   │   │   ├── rgw_chash_map.c      # 哈希表
│   │   │   └── (其他容器)
│   │   ├── tests/                    # 测试文件
│   │   ├── build/                    # 构建目录
│   │   ├── CMakeLists.txt           # 构建配置
│   │   └── memory-bank/             # 项目文档
│   │       ├── architecture.md      # 本文档
│   │       ├── cpp2c-document.md    # 转换概要设计
│   │       ├── implementation-plan.md # 实施计划
│   │       ├── tech-stack.md        # 技术栈
│   │       └── FILE_STRUCTURE.md    # 文件结构说明
│   └── (其他 RGW 代码)
```

### 2.2 目录规范

| 目录 | 内容 | 说明 |
|------|------|------|
| `include/containers/` | 容器头文件 | 公开 API |
| `include/internal/` | 内部头文件 | 第三方库封装 |
| `include/` | 核心头文件 | OOP、错误处理、统一入口 |
| `src/` | 核心源文件 | 主要实现 |
| `containers/` | 容器源文件 | 容器实现 |
| `tests/` | 测试文件 | 单元测试 |
| `build/` | 构建目录 | CMake 构建输出 |

## 3. C 版本代码结构

### 3.1 容器 API 设计模式

所有容器遵循统一的 API 设计模式：

```c
// 1. 不透明句柄类型
typedef struct rgw_xxx rgw_xxx_t;

// 2. 创建/销毁
rgw_xxx_t* rgw_xxx_create(void (*value_free)(void*));
void rgw_xxx_destroy(rgw_xxx_t* xxx);

// 3. 基本操作
int rgw_xxx_xxx(rgw_xxx_t* xxx, ...);
```

**关键特性**:
- `value_free`: 元素释放回调，NULL 表示不自动释放
- 返回值: 成功返回 0 (RGW_OK)，失败返回错误码
- 内存管理: 使用 `rgw_c_alloc` / `rgw_c_free`

### 3.2 已实现的容器

| STL 容器 | C 实现 | 底层实现 | 状态 |
|-----------|--------|----------|------|
| `std::vector` | rgw_carray | utarray.h | ✅ 已完成 |
| `std::string` | rgw_cstring | utstring.h | ✅ 已完成 |
| `std::map` | rgw_cmap | 红黑树 | ✅ 已完成 |
| `std::set` | rgw_cset | 红黑树 | ✅ 已完成 |
| `std::list` | rgw_clist | 双向链表 | ✅ 已完成 |
| `std::deque` | rgw_cdeque | utringbuffer.h | ✅ 已完成 |
| `std::stack` | rgw_stack_t | utstack.h | ✅ 已完成 |
| `std::queue` | rgw_queue_t | 双向链表 | ✅ 已完成 |
| `std::unordered_map` | rgw_chash_map | uthash.h | ✅ 已完成 |
| `std::optional` | rgw_coptional | 自定义 | ✅ 已完成 |
| `std::priority_queue` | rgw_priority_queue_t | 二叉堆 | ✅ 已完成 |

### 3.3 核心框架

#### 3.3.1 OOP 框架 (rgw_oop.h)

```c
// 虚函数表模式
typedef struct rgw_object_vtable {
    void (*destroy)(rgw_object_t* self);
    const char* (*type_name)(void);
    bool (*is_a)(const rgw_object_t* self, const char* type_name);
} rgw_object_vtable_t;

typedef struct rgw_object {
    rgw_object_vtable_t* vtable;
    uint32_t ref_count;
    char type_id[32];
} rgw_object_t;
```

#### 3.3.2 错误处理 (rgw_errors.h)

```c
typedef enum rgw_error_code {
    RGW_OK = 0,
    RGW_ERR_INVALID_ARG = 1,
    RGW_ERR_OUT_OF_MEMORY = 2,
    // ... 其他错误码
} rgw_error_code_t;

// 错误传播宏
#define RGW_CHECK(expr) ...
#define RGW_CHECK_MSG(expr, msg) ...
#define RGW_CHECK_GOTO(expr, label) ...
```

### 3.4 文件命名规范

| 类型 | 命名规范 | 示例 |
|------|----------|------|
| 头文件 | `rgw_xxx.h` | `rgw_carray.h` |
| 源文件 | `rgw_xxx.c` | `rgw_carray.c` |
| 测试文件 | `test_xxx.c` | `test_carray.c` |

## 4. 当前转换进度

### 4.1 阶段完成状态

| 阶段 | 内容 | 状态 |
|------|------|------|
| 阶段 0 | 基础设施准备 | 🔄 进行中 |
| 阶段 1 | 核心数据类型转换 | ⏳ 待开始 |
| 阶段 2 | 存储抽象层转换 | ⏳ 待开始 |
| 阶段 3 | REST 核心转换 | ⏳ 待开始 |
| 阶段 4 | 上层模块转换 | ⏳ 待开始 |
| 阶段 5 | 优化与收尾 | ⏳ 待开始 |

### 阶段 0 - 基础设施准备 (已完成)

- ✅ c_common 容器库完善
  - ✅ 动态数组 (rgw_carray)
  - ✅ 字符串 (rgw_cstring)
  - ✅ 有序 Map (rgw_cmap)
  - ✅ 有序 Set (rgw_cset)
  - ✅ 双向链表 (rgw_clist)
  - ✅ 双端队列 (rgw_cdeque)
  - ✅ 栈 (rgw_cstack)
  - ✅ 队列 (rgw_cqueue)
- ✅ 优先队列 (rgw_cpriority_queue)
- ✅ OOP 框架 (rgw_oop)
- ✅ 错误处理 (rgw_errors)
- ✅ 统一头文件 (rgw_ccommon)
  - ✅ 哈希表 (rgw_chash_map)
  - ✅ 可选类型 (rgw_coptional)
  - ✅ 优先队列 (rgw_priority_queue_t)

- ✅ C 语言面向对象框架 (rgw_oop)
  - 🔄 虚函数表模式 - 待实现
  - 🔄 引用计数智能指针 - 待实现

- ✅ 错误处理机制 (rgw_errors)
  - 🔄 统一错误码 - 待实现
  - 🔄 错误链支持 - 待实现

- ✅ 测试框架（基础测试通过，完善中）
  - ✅ 基础测试通过
  - 🔄 完善容器测试用例 - 进行中

### 4.2 测试状态

| 测试 | 状态 |
|------|------|
| test_memory | ✅ PASSED |
| test_types | ✅ PASSED |
| test_cmap | ✅ PASSED |
| test_cset | ✅ PASSED |
| test_carray | ✅ PASSED |
| test_cstring | ✅ PASSED |
| test_coptional | ✅ PASSED |
| test_all | ✅ PASSED |
| test_ccontainer | ✅ PASSED |
| test_cpp_to_c | ✅ PASSED |

## 5. 后续计划

### 5.1 SAL 存储抽象层转换 (当前阶段)

详细计划见 `sal-conversion-plan.md`

**核心任务**:

| 任务 | 说明 | 状态 |
|------|------|------|
| 分析 SAL C++ 接口 | 理解现有架构 | ✅ 完成 |
| 创建 SAL C 接口目录 | src/rgw/sal_c/ | 🔄 待开始 |
| 设计 vtable 模式 | C 多态机制 | 🔄 待开始 |
| RADOS 驱动适配器 | 主要存储后端 | 🔄 待开始 |
| DBStore 驱动 | SQLite 后端 | 🔄 待开始 |

**涉及文件**:

- `rgw_sal.h` - 主接口 (2000+ 行)
- `rgw_sal_fwd.h` - 前向声明
- `rgw_sal_filter.h` - 过滤器
- `driver/rados/rgw_sal_rados.h` - RADOS 驱动

### 5.2 REST 核心转换 (待开始)

- rgw_op.cc - 操作处理器
- rgw_rest_*.cc - REST 框架
- S3/Swift 协议实现

### 5.3 上层模块转换 (待开始)

- 认证授权模块
- 服务层
- 前端网络层

---

## 6. 参考文档

| 文档 | 说明 |
|------|------|
| `implementation-plan.md` | 详细实施计划 |
| `sal-conversion-plan.md` | SAL 转换详细计划 |
| `tech-stack.md` | 技术栈说明 |
| `cpp2c-document.md` | C++ 到 C 转换规范 |
| `coding-standards.md` | 编程规范 |

---

**文档版本**: 1.1
**最后更新**: 2026-03-18
**维护团队**: RGW C++ 到 C 转换项目组

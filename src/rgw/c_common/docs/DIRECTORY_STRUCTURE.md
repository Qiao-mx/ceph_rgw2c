# RGW C Common 代码目录结构详解

## 概述

`c_common` 是 Ceph RGW 项目中的 C 语言数据结构库，旨在为 RGW 从 C++ 到 C 的重构提供替代 STL 容器的实现。该库采用分层架构设计，上层封装提供类似 STL 的简洁 API，底层复用经过验证的成熟 C 实现。

---

## 目录结构总览

```
c_common/
├── include/              # 公共头文件（对外 API）
│   ├── containers/       # 容器接口定义
│   └── internal/        # 内部数据结构实现
├── src/                 # 容器实现文件
├── containers/          # C 容器实现
├── tests/               # 单元测试
├── docs/                # 文档（本目录）
└── CMakeLists.txt       # 构建配置
```

---

## 容器实现详解

### 1. 动态数组 (rgw_carray.h / rgw_carray.c)

**对应 C++ 容器**: `std::vector`

**功能特性**:
- O(1) 均摊时间的末尾追加操作
- O(1) 按索引随机访问
- 自动容量增长（1.5x 或 2x）
- 支持元素拷贝/释放回调
- 元素类型：buffer (void* + length)

**关键 API**:
```c
rgw_array_t* rgw_array_create(size_t initial_capacity);    // 创建数组
void rgw_array_destroy(rgw_array_t *array);                // 销毁数组
int rgw_array_append(rgw_array_t *array, const void *data, uint32_t len);  // 追加元素
const void* rgw_array_get(const rgw_array_t *array, size_t index, uint32_t *len);  // 获取元素
int rgw_array_erase(rgw_array_t *array, size_t index);     // 删除元素
size_t rgw_array_size(const rgw_array_t *array);           // 获取大小
```

**使用场景**:
- 对象数据存储
- 批量操作缓冲区
- 动态增长的序列数据

---

### 2. 有序映射 (rgw_cmap.h / rgw_cmap.c)

**对应 C++ 容器**: `std::map`

**底层数据结构**: 红黑树 (rbt_tree)

**功能特性**:
- O(log n) 的插入、查找、删除操作
- 维护按键排序（字符串字典序）
- 支持自定义值类型（通过 void* 和释放回调）

**关键 API**:
```c
rgw_map_t* rgw_map_create(void (*value_free)(void*));      // 创建 map
void rgw_map_destroy(rgw_map_t *map);                       // 销毁 map
int rgw_map_insert(rgw_map_t *map, const char *key, const void *value, uint32_t value_len);  // 插入
const void* rgw_map_find(const rgw_map_t *map, const char *key, uint32_t *value_len);      // 查找
int rgw_map_erase(rgw_map_t *map, const char *key);        // 删除
bool rgw_map_contains(const rgw_map_t *map, const char *key);  // 检查存在
rgw_map_iterator_t rgw_map_begin(const rgw_map_t *map);    // 迭代器起始
```

**使用场景**:
- 对象扩展属性: `std::map<std::string, bufferlist> xattrs`
- 认证键值映射: `std::map<std::string, std::string> val_map`
- 存储统计: `std::map<RGWObjCategory, RGWStorageStats> stats`
- 配置管理

---

### 3. 双向链表 (rgw_clist.h / rgw_clist.c)

**对应 C++ 容器**: `std::list`

**底层数据结构**: rgw_list (源自 NFS-Ganesha 项目)

**功能特性**:
- O(1) 头部/尾部插入
- O(1) 任意位置删除
- 支持双向迭代
- 支持自定义值类型

**关键 API**:
```c
rgw_clist_t* rgw_clist_create(void (*value_free)(void*));  // 创建链表
void rgw_clist_destroy(rgw_clist_t *list);                  // 销毁链表
int rgw_clist_add_tail(rgw_clist_t *list, const void *value, uint32_t value_len);  // 尾部添加
int rgw_clist_add_head(rgw_clist_t *list, const void *value, uint32_t value_len);  // 头部添加
int rgw_clist_remove(rgw_clist_t *list, size_t index);     // 按索引删除
const void* rgw_clist_get(const rgw_clist_t *list, size_t index, uint32_t *value_len);  // 获取元素
bool rgw_clist_contains(const rgw_clist_t *list, const void *value, uint32_t value_len);  // 检查存在
```

**使用场景**:
- LRU 缓存链表
- 任务队列
- 需要频繁插入/删除的场景

---

### 4. 有序集合 (rgw_cset.h / rgw_cset.c)

**对应 C++ 容器**: `std::set`

**底层数据结构**: 红黑树 (rbt_tree)

**功能特性**:
- O(log n) 插入、查找、删除
- 维护元素有序（字符串字典序）
- 不支持重复元素

**关键 API**:
```c
rgw_set_t* rgw_set_create_string(void);                    // 创建集合
void rgw_set_destroy(rgw_set_t *set);                       // 销毁集合
int rgw_set_insert_string(rgw_set_t *set, const char *key);  // 插入元素
bool rgw_set_contains_string(const rgw_set_t *set, const char *key);  // 检查存在
int rgw_set_erase_string(rgw_set_t *set, const char *key);  // 删除元素
size_t rgw_set_size(const rgw_set_t *set);                 // 获取大小
```

**使用场景**:
- 去重场景
- 有序唯一元素集合
- 范围查询

---

### 5. 字符串 (rgw_cstring.h / rgw_cstring.c)

**对应 C++ 容器**: `std::string`

**功能特性**:
- 自动内存管理
- O(1) 长度查询
- O(n) 拷贝、追加、比较
- C 字符串兼容性
- 子串操作

**关键 API**:
```c
rgw_string_t* rgw_string_create(const char *cstr);         // 从 C 字符串创建
void rgw_string_destroy(rgw_string_t *str);                // 销毁字符串
int rgw_string_assign(rgw_string_t *str, const char *cstr);  // 赋值
int rgw_string_append(rgw_string_t *str, const char *cstr);  // 追加
int rgw_string_append_format(rgw_string_t *str, const char *format, ...);  // 格式化追加
const char* rgw_string_c_str(const rgw_string_t *str);     // 获取 C 字符串
size_t rgw_string_length(const rgw_string_t *str);         // 获取长度
int rgw_string_compare(const rgw_string_t *str1, const rgw_string_t *str2);  // 比较
size_t rgw_string_find(const rgw_string_t *str, const char *substr);  // 查找子串
rgw_string_t* rgw_string_substring(const rgw_string_t *str, size_t pos, size_t len);  // 获取子串
```

**使用场景**:
- 对象键名
- 用户元数据
- HTTP 头部处理

---

### 6. 可选值 (rgw_coptional.h / rgw_coptional.c)

**对应 C++ 容器**: `std::optional`

**功能特性**:
- 类型安全的可选值容器
- 可持有值或为空
- 支持任意数据类型（void* + length）
- 值语义（拷贝时赋值）

**关键 API**:
```c
void rgw_optional_init(rgw_optional_t *opt);               // 初始化为空
int rgw_optional_set(rgw_optional_t *opt, const void *value, uint32_t len);  // 设置值
bool rgw_optional_has_value(const rgw_optional_t *opt);     // 检查是否有值
const void* rgw_optional_get(const rgw_optional_t *opt, uint32_t *len);  // 获取值
void rgw_optional_clear(rgw_optional_t *opt);              // 清除值
void rgw_optional_destroy(rgw_optional_t *opt);             // 销毁
int rgw_optional_copy(rgw_optional_t *dest, const rgw_optional_t *src);  // 拷贝
```

**使用场景**:
- 可选配置参数
- 可能不存在的返回值
- 区分"未设置"和"空值"

---

### 7. 哈希映射 (rgw_chash_map.h / rgw_chash_map.c)

**对应 C++ 容器**: `std::unordered_map`

**底层数据结构**: uthash

**功能特性**:
- O(1) 平均情况插入、查找、删除
- 基于 uthash 库
- 字符串键 + 通用 void* 值

**关键 API**:
```c
rgw_hash_map_t* rgw_hash_map_create(void (*value_free)(void*));  // 创建哈希表
void rgw_hash_map_destroy(rgw_hash_map_t *map);          // 销毁
int rgw_hash_map_insert(rgw_hash_map_t *map, const char *key, const void *value, uint32_t value_len);  // 插入
const void* rgw_hash_map_find(const rgw_hash_map_t *map, const char *key, uint32_t *value_len);  // 查找
int rgw_hash_map_erase(rgw_hash_map_t *map, const char *key);  // 删除
size_t rgw_hash_map_size(const rgw_hash_map_t *map);   // 获取大小
```

**使用场景**:
- 快速查找场景
- 缓存实现
- 字典查询

---

### 8. 内存管理 (rgw_cmemory.h / rgw_cmemory.c)

**功能特性**:
- 统一的内存分配接口
- 支持自定义内存操作
- 可嵌入到现有内存池

**关键 API**:
```c
typedef struct rgw_memory_ops {
    void* (*allocate)(size_t size);     // 分配
    void (*deallocate)(void *ptr);      // 释放
    void* (*reallocate)(void *ptr, size_t new_size);  // 重新分配
    void (*copy)(void *dest, const void *src, size_t size);  // 拷贝
    int (*compare)(const void *a, const void *b, size_t size);  // 比较
} rgw_memory_ops_t;

void rgw_set_memory_ops(const rgw_memory_ops_t *ops);     // 设置内存操作
const rgw_memory_ops_t* rgw_get_memory_ops(void);         // 获取当前内存操作
void* rgw_c_alloc(size_t size);                            // 分配内存
void rgw_c_free(void *ptr);                                // 释放内存
char* rgw_c_strdup(const char *s);                         // 字符串复制
```

---

## 内部数据结构（include/internal/）

### 红黑树 (rgw_rbtree.h)

源自 GNU ISO C++ Library 改编，提供:
- `std::map` / `std::set` 的底层实现
- O(log n) 操作复杂度
- 自动平衡

### 双向链表 (rgw_list.h)

源自 NFS-Ganesha 项目，提供:
- 内核风格的双向链表
- 嵌入到用户结构体中使用
- 高效的插入/删除

---

## 容器映射关系总结

| C++ STL | C 实现 | 底层数据结构 | 头文件 |
|---------|--------|-------------|--------|
| `std::vector` | rgw_array_t | 动态数组 | rgw_carray.h |
| `std::list` | rgw_clist_t | rgw_list | rgw_clist.h |
| `std::map` | rgw_map_t | rbt_tree | rgw_cmap.h |
| `std::set` | rgw_set_t | rbt_tree | rgw_cset.h |
| `std::unordered_map` | rgw_hash_map_t | uthash | rgw_chash_map.h |
| `std::string` | rgw_string_t | 动态数组 | rgw_cstring.h |
| `std::optional` | rgw_optional_t | void* + flag | rgw_coptional.h |

---

## 性能对比

| 操作 | std::vector | rgw_array | std::list | gsh_list | std::map | rbt_tree | std::unordered_map | uthash |
|------|-------------|-----------|-----------|----------|---------|----------|-------------------|--------|
| 随机访问 | O(1) | O(1) | O(n) | O(n) | O(log n) | O(log n) | N/A | N/A |
| 查找 | O(n) | O(n) | O(n) | O(n) | O(log n) | O(log n) | O(1) | O(1) |
| 插入(末尾) | O(1)* | O(1)* | O(1) | O(1) | O(log n) | O(log n) | O(1) | O(1) |
| 删除 | O(n) | O(n) | O(1)** | O(1)** | O(log n) | O(log n) | O(1) | O(1) |

*均摊时间复杂度  
**已知位置情况下

---

## 编译与测试

### 编译
```bash
cd c_common
mkdir build && cd build
cmake ..
make
```

### 运行测试
```bash
./tests/test_cmap       # 测试 map
./tests/test_carray    # 测试 array
./tests/test_all       # 运行所有测试
ctest --verbose        # CMake 测试
```

---

## 注意事项

### 内存管理
C 语言需要手动管理内存，务必在创建容器时传入正确的值释放回调：
```c
// 正确做法
rgw_map_t *map = rgw_map_create(free);  // 传入 free 回调
rgw_map_insert(map, "key", data, size);
rgw_map_destroy(map);  // 自动清理所有内存
```

### 迭代器安全
删除元素时使用 safe 版本：
```c
// 错误
glist_for_each(node, &list) {
    glist_del(node);  // 可能导致迭代器失效
}

// 正确
glist_for_each_safe(node, tmp, &list) {
    glist_del(node);  // 安全删除
}
```

---

## 许可证

LGPL-3.0-or-later

## 贡献者

基于以下经典 C 实现：
- `rgw_rbtree.h`: GNU ISO C++ Library 改编
- `rgw_list.h`: NFS-Ganesha 项目
- `uthash.h`: Troy D. Hanson 的 utarray 项目

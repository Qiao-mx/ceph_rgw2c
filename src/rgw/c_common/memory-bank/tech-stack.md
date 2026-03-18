# RGW C++ 到 C 转换项目技术栈

## 1. 编译器与工具链

### 1.1 编译器选择
- **主编译器**: GCC 11+ 或 Clang 14+
- **C 语言标准**: C11 (ISO/IEC 9899:2011)
- **C++ 语言标准**: C++17 (转换期间兼容)
- **编译模式**: 支持混合编译 (C 和 C++ 代码共存)

**编译器要求**:
```bash
# GCC 最低版本
gcc --version ≥ 11.0
# Clang 最低版本
clang --version ≥ 14.0

# 编译标志
C_FLAGS = -std=c11 -Wall -Wextra -Werror -fPIC -pthread
CXX_FLAGS = -std=c++17 -Wall -Wextra -Werror -fPIC -pthread
```

### 1.2 跨平台支持
| 平台 | 编译器 | 特殊要求 |
|------|--------|----------|
| Linux (x86_64) | GCC/Clang | 标准支持 |
| Linux (ARM64) | GCC/Clang | 交叉编译工具链 |
| Windows (MinGW) | MinGW-w64 | POSIX 兼容层 |
| 嵌入式系统 | GCC (交叉编译) | 精简运行时库 |

## 2. 构建系统

### 2.1 CMake 配置
```cmake
# 基础配置
cmake_minimum_required(VERSION 3.10)
project(rgw_c_conversion C CXX)

# 语言标准设置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 混合编译支持
set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")

# 条件编译选项
option(WITH_RGW_C_CONVERSION "启用 C 语言转换" ON)
option(WITH_MIXED_COMPILATION "启用混合编译" ON)
option(WITH_C_COMMON_EXTENDED "启用扩展 c_common 容器" ON)
```

### 2.2 构建目标
```cmake
# 主要构建目标
add_library(rgw_c_common STATIC ${C_COMMON_SOURCES})  # C 容器库
add_library(rgw_core STATIC ${CORE_C_SOURCES})        # C 核心实现
add_library(rgw_legacy STATIC ${LEGACY_CXX_SOURCES})  # 遗留 C++ 代码

# 混合链接
add_executable(radosgw ${MAIN_SOURCES})
target_link_libraries(radosgw
    PRIVATE rgw_core rgw_legacy rgw_c_common
    PUBLIC ${DEPENDENCIES}
)
```

## 3. C 语言基础设施库

### 3.1 c_common 扩展方案
基于现有 c_common 库，补充以下容器实现：

| 容器类型 | C 实现 | 底层数据结构 | 优先级 | 状态 |
|----------|--------|--------------|--------|------|
| 动态数组 | `rgw_array_t` | utarray (宏库) | P0 | ✅ 已实现 |
| 有序映射 | `rgw_map_t` | 红黑树 (rbt_tree) | P0 | ✅ 已实现 |
| 双向链表 | `rgw_clist_t` | rgw_list (NFS-Ganesha) | P0 | ✅ 已实现 |
| 有序集合 | `rgw_set_t` | 红黑树 (rbt_tree) | P0 | ✅ 已实现 |
| 哈希映射 | `rgw_hash_map_t` | uthash (宏库) | P0 | ✅ 已实现 |
| 字符串 | `rgw_string_t` | utstring (宏库) | P0 | ✅ 已实现 |
| 可选值 | `rgw_optional_t` | 标志位 + union | P0 | ✅ 已实现 |
| 队列 | `rgw_queue_t` | 双向链表 | P1 | ✅ 已实现 |
| 栈 | `rgw_stack_t` | 动态数组封装 | P1 | ✅ 已实现 |
| 双端队列 | `rgw_deque_t` | 分块数组 | P2 | 🔄 待实现 |
| 优先队列 | `rgw_priority_queue_t` | 二叉堆 | P2 | ✅ 已实现 |
| 元组 | 结构体替代 | 固定大小结构体 | P1 | 📋 设计完成 |
| 变体 | `rgw_variant_t` | union + 类型标签 | P2 | 🔄 待实现 |
| 任意类型 | `rgw_any_t` | void* + 类型信息 | P2 | 🔄 待实现 |

### 3.2 内存管理框架
```c
// 统一内存管理接口
typedef struct rgw_memory_ops {
    void* (*allocate)(size_t size, const char* file, int line);
    void (*deallocate)(void* ptr, const char* file, int line);
    void* (*reallocate)(void* ptr, size_t new_size, const char* file, int line);
    size_t (*get_size)(const void* ptr);
} rgw_memory_ops_t;

// 智能指针模拟
typedef struct rgw_ref_counted {
    int ref_count;
    void (*destroy)(struct rgw_ref_counted*);
} rgw_ref_counted_t;

typedef struct rgw_shared_ptr {
    rgw_ref_counted_t* rc;
    void* data;
} rgw_shared_ptr_t;

// RAII 包装器
#define RGWAutoFree(type, var, init) \
    type var = init; \
    __attribute__((cleanup(rgw_auto_free_##type))) type* var##_ptr = &var
```

### 3.3 错误处理机制
```c
// 统一错误码
typedef enum rgw_error_code {
    RGW_OK = 0,
    RGW_ERR_INVALID_ARG = 1,
    RGW_ERR_OUT_OF_MEMORY = 2,
    RGW_ERR_IO_ERROR = 3,
    RGW_ERR_NOT_FOUND = 4,
    RGW_ERR_ALREADY_EXISTS = 5,
    RGW_ERR_PERMISSION_DENIED = 6,
    RGW_ERR_TIMEOUT = 7,
    RGW_ERR_NOT_IMPLEMENTED = 8,
    // ... 其他错误码
} rgw_error_code_t;

// 错误上下文
typedef struct rgw_error_context {
    rgw_error_code_t code;
    const char* message;
    const char* file;
    int line;
    struct rgw_error_context* cause;
} rgw_error_context_t;

// 错误传播宏
#define RGW_CHECK(expr) \
    do { \
        int __rc = (expr); \
        if (__rc != RGW_OK) { \
            return __rc; \
        } \
    } while(0)

#define RGW_CHECK_MSG(expr, msg) \
    do { \
        int __rc = (expr); \
        if (__rc != RGW_OK) { \
            rgw_set_error(__rc, msg, __FILE__, __LINE__); \
            return __rc; \
        } \
    } while(0)
```

### 3.4 面向对象框架
```c
// 虚函数表模式
typedef struct rgw_object_vtable {
    // 虚函数
    rgw_error_code_t (*method1)(struct rgw_object* self, ...);
    rgw_error_code_t (*method2)(struct rgw_object* self, ...);

    // 生命周期管理
    void (*destroy)(struct rgw_object* self);

    // RTTI 支持
    const char* (*type_name)(void);
    bool (*is_a)(const struct rgw_object* self, const char* type_name);
} rgw_object_vtable_t;

// 基类定义
typedef struct rgw_object {
    rgw_object_vtable_t* vtable;
    uint32_t ref_count;
    char type_id[32];  // 用于 RTTI
} rgw_object_t;

// 多态调用
static inline rgw_error_code_t rgw_object_method1(rgw_object_t* obj, ...) {
    return obj->vtable->method1(obj, ...);
}
```

## 4. C++ 特性转换方案

### 4.1 模板处理策略
```c
// 类型擦除方案
typedef struct rgw_generic_container {
    void* data;
    size_t element_size;

    // 操作函数表
    void (*copy_element)(void* dest, const void* src);
    void (*destroy_element)(void* element);
    int (*compare_elements)(const void* a, const void* b);
} rgw_generic_container_t;

// 常用类型特化
#define RGW_DECLARE_CONTAINER(type, name) \
    typedef struct rgw_##name##_t rgw_##name##_t; \
    rgw_##name##_t* rgw_##name##_create(void); \
    void rgw_##name##_destroy(rgw_##name##_t* container); \
    int rgw_##name##_push_back(rgw_##name##_t* container, type value);

RGW_DECLARE_CONTAINER(int, int_vector)
RGW_DECLARE_CONTAINER(char*, string_vector)
RGW_DECLARE_CONTAINER(void*, pointer_vector)
```

### 4.2 异常转换方案
```c
// C++ 异常 → C 错误码映射
static rgw_error_code_t rgw_exception_to_error(const std::exception& e) {
    if (dynamic_cast<const std::invalid_argument*>(&e)) {
        return RGW_ERR_INVALID_ARG;
    } else if (dynamic_cast<const std::out_of_range*>(&e)) {
        return RGW_ERR_OUT_OF_RANGE;
    } else if (dynamic_cast<const std::bad_alloc*>(&e)) {
        return RGW_ERR_OUT_OF_MEMORY;
    }
    return RGW_ERR_UNKNOWN;
}

// 包装层示例
extern "C" rgw_error_code_t rgw_cpp_function_wrapper(...) {
    try {
        cpp_function(...);
        return RGW_OK;
    } catch (const std::exception& e) {
        return rgw_exception_to_error(e);
    } catch (...) {
        return RGW_ERR_UNKNOWN;
    }
}
```

### 4.3 Lambda 表达式转换
```c
// Lambda 上下文结构
typedef struct rgw_lambda_context {
    void* user_data;
    void (*destroy)(void* user_data);
} rgw_lambda_context_t;

// Lambda 函数签名
typedef rgw_error_code_t (*rgw_lambda_func_t)(void* context, ...);

// Lambda 包装器
typedef struct rgw_lambda {
    rgw_lambda_func_t func;
    rgw_lambda_context_t* context;
} rgw_lambda_t;

// 使用示例
rgw_error_code_t rgw_for_each(
    rgw_array_t* array,
    rgw_lambda_func_t callback,
    void* context
) {
    for (size_t i = 0; i < rgw_array_size(array); i++) {
        void* element = rgw_array_get(array, i);
        rgw_error_code_t rc = callback(context, element);
        if (rc != RGW_OK) return rc;
    }
    return RGW_OK;
}
```

## 5. 第三方库处理方案

### 5.1 必需第三方库
| C++ 库 | C 替代方案 | 处理策略 | 优先级 |
|--------|------------|----------|--------|
| STL 容器 | c_common 库 | 完全替换 | P0 |
| Boost.Context | libcoroutine / libco | 寻找 C 替代 | P1 |
| Boost.Filesystem | POSIX API + 自定义封装 | 重新封装 | P1 |
| Fmt | snprintf + 自定义格式化 | 重新实现核心功能 | P2 |
| RapidJSON | json-c / jansson | 替换为 C 库 | P1 |
| JWT-CPP | libjwt / cjose | 替换为 C 库 | P2 |
| picojson | json-c | 替换为 C 库 | P1 |

### 5.2 保留的 C++ 库
| 库名 | 保留原因 | 包装策略 |
|------|----------|----------|
| librados++ | Ceph 官方库，无纯 C 替代 | 创建精简 C 包装层 |
| Boost.Asio | 网络库复杂，转换成本高 | 保留核心部分，逐步替换 |
| 特定算法库 | 性能关键或实现复杂 | 保持原样，通过 C 接口调用 |

### 5.3 外部 C 库引入
```bash
# 构建依赖
- json-c (>= 0.15): sudo apt-get install libjson-c-dev
- libjwt (>= 1.10): sudo apt-get install libjwt-dev
- uthash (已集成): 源码集成
- libcoroutine: 可选，用于协程支持
```

## 6. 测试与质量保障

### 6.1 测试框架
```cmake
# 单元测试框架
enable_testing()

# CTest 配置
set(BUILD_TESTING ON)
add_subdirectory(tests)

# 测试目标
add_executable(test_core test_core.c)
target_link_libraries(test_core rgw_core rgw_c_common)

# 集成测试
add_executable(test_integration test_integration.c)
target_link_libraries(test_integration rgw_core rgw_legacy)
```

### 6.2 静态分析工具
| 工具 | 用途 | 集成方式 |
|------|------|----------|
| Clang Static Analyzer | 静态代码分析 | CMake 集成，CI 运行 |
| Coverity Scan | 深度静态分析 | 定期扫描，云端分析 |
| cppcheck | C/C++ 代码检查 | 预提交钩子 |
| clang-tidy | 代码质量检查 | 开发环境集成 |
| include-what-you-use | 头文件依赖检查 | 定期运行 |

### 6.3 动态分析工具
| 工具 | 用途 | 配置 |
|------|------|------|
| Valgrind | 内存泄漏检测 | `valgrind --leak-check=full` |
| AddressSanitizer | 地址错误检测 | `-fsanitize=address` |
| UndefinedBehaviorSanitizer | 未定义行为检测 | `-fsanitize=undefined` |
| ThreadSanitizer | 线程竞争检测 | `-fsanitize=thread` |
| gcov/lcov | 代码覆盖率 | `--coverage` 标志 |

### 6.4 性能测试工具
| 工具 | 用途 | 指标 |
|------|------|------|
| perf (Linux) | 系统性能分析 | CPU 周期、缓存命中率 |
| gprof | 函数调用分析 | 调用图、执行时间 |
| Google Benchmark | 微基准测试 | 纳秒级精度 |
| wrk/ab | HTTP 压力测试 | QPS、延迟、吞吐量 |

## 7. 持续集成与部署

### 7.1 CI/CD 流水线
```yaml
# .gitlab-ci.yml 示例
stages:
  - build
  - test
  - analyze
  - deploy

variables:
  CMAKE_BUILD_TYPE: "Debug"

build_job:
  stage: build
  script:
    - mkdir build && cd build
    - cmake .. -DWITH_RGW_C_CONVERSION=ON -DWITH_TESTS=ON
    - make -j$(nproc)
  artifacts:
    paths:
      - build/

test_job:
  stage: test
  script:
    - cd build
    - ctest --output-on-failure
  dependencies:
    - build_job

analyze_job:
  stage: analyze
  script:
    - scan-build cmake ..
    - scan-build make -j$(nproc)
    - valgrind --leak-check=full ./tests/test_core
```

### 7.2 代码质量门禁
1. **编译要求**: 零警告 (`-Werror`)
2. **测试要求**: 单元测试覆盖率 >90%
3. **静态分析**: 无严重级别问题
4. **动态分析**: 无内存泄漏
5. **性能要求**: 关键路径性能不低于原版 90%
6. **API 兼容性**: 100% 通过回归测试

## 8. 开发环境与工具

### 8.1 开发工具配置
```json
// .vscode/c_cpp_properties.json
{
    "configurations": [
        {
            "name": "Linux",
            "includePath": [
                "${workspaceFolder}/include",
                "${workspaceFolder}/c_common/include",
                "/usr/include/json-c"
            ],
            "defines": ["RGW_C_CONVERSION=1"],
            "compilerPath": "/usr/bin/gcc",
            "cStandard": "c11",
            "cppStandard": "c++17"
        }
    ]
}
```

### 8.2 调试配置
```bash
# GDB 配置
set pagination off
set print pretty on
set history save on

# 调试脚本
define rgw_debug
  set args -d -c /path/to/ceph.conf
  break main
  run
end

# LLDB 配置 (macOS)
settings set target.source-map ./ /absolute/path
```

## 9. 性能优化策略

### 9.1 关键路径优化
1. **热点分析**: 使用 perf 识别性能瓶颈
2. **内存布局优化**: 结构体对齐、缓存友好设计
3. **算法优化**: 选择时间复杂度更优的算法
4. **减少拷贝**: 使用引用或指针传递大数据

### 9.2 内存管理优化
```c
// 内存池实现
typedef struct rgw_memory_pool {
    void** blocks;
    size_t block_size;
    size_t block_count;
    size_t free_list;
} rgw_memory_pool_t;

// 对象池
typedef struct rgw_object_pool {
    rgw_memory_pool_t* pool;
    size_t object_size;
    void (*constructor)(void* obj);
    void (*destructor)(void* obj);
} rgw_object_pool_t;
```

### 9.3 并发优化
```c
// 线程安全容器
typedef struct rgw_thread_safe_array {
    rgw_array_t* array;
    pthread_mutex_t mutex;
    pthread_rwlock_t rwlock;  // 读写锁优化
} rgw_thread_safe_array_t;

// 无锁数据结构 (CAS 实现)
typedef struct rgw_lock_free_queue {
    void** buffer;
    size_t capacity;
    _Atomic size_t head;
    _Atomic size_t tail;
} rgw_lock_free_queue_t;
```

## 10. 迁移与回滚策略

### 10.1 渐进迁移方案
1. **文件级迁移**: 逐个文件转换，转换完成后立即测试
2. **接口兼容层**: 保留原 C++ 接口，内部调用 C 实现
3. **A/B 测试**: 并行运行新旧版本，对比结果
4. **功能开关**: 运行时切换 C/C++ 实现

### 10.2 回滚机制
```bash
# 快速回滚脚本
#!/bin/bash
# rollback_rgw.sh
if [ $# -ne 1 ]; then
    echo "Usage: $0 <commit_hash>"
    exit 1
fi

git checkout $1
cd build && make clean && make -j$(nproc)
sudo systemctl restart radosgw
```

### 10.3 监控与告警
| 指标 | 阈值 | 告警方式 |
|------|------|----------|
| 内存使用增长 | > 20% | 邮件/Slack |
| 性能下降 | > 10% | 实时告警 |
| 错误率上升 | > 1% | 立即通知 |
| API 响应时间 | > 200ms | 性能告警 |

## 11. 文档与知识管理

### 11.1 文档结构
```
docs/
├── architecture/          # 架构文档
├── api/                  # API 文档
├── conversion-guide/     # 转换指南
├── performance/          # 性能文档
└── troubleshooting/      # 故障排除
```

### 11.2 知识库管理
- **Confluence/Wiki**: 架构决策记录 (ADR)
- **代码注释**: Doxygen 格式，自动生成文档
- **示例代码**: 每种模式的完整示例
- **FAQ**: 常见问题解答

## 12. 风险评估与缓解

### 12.1 技术风险
| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|----------|
| 性能不达标 | 中 | 高 | 早期性能测试，关键路径优化 |
| 内存泄漏 | 高 | 高 | 严格代码审查，自动化检测 |
| 第三方库不兼容 | 低 | 中 | 备用方案，逐步替换 |
| 混合编译问题 | 中 | 低 | 持续集成验证 |

### 12.2 项目管理风险
| 风险 | 缓解措施 |
|------|----------|
| 进度延迟 | 敏捷迭代，每2周可交付成果 |
| 知识断层 | 结对编程，详细文档 |
| 需求变更 | 模块化设计，接口稳定 |
| 团队协作 | 清晰角色分工，定期沟通 |

---

**技术栈版本**: 1.0
**最后更新**: 2026-03-16
**适用范围**: RGW C++ 到 C 转换项目
**维护团队**: 架构组 + 核心开发组

> 注意：本技术栈为实际实施提供具体指导，需根据实际情况调整。所有技术选型均经过可行性验证。
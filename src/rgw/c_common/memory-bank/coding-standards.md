# RGW C Common 编程规范与常见错误预防

本文档总结了 RGW C Common 项目中的编程规范、常见错误及预防措施，适用于所有 C 代码编写。

## 0. 本次运行问题总结

### 0.1 测试结果

| 测试 | 状态 | 备注 |
|---|---|---|
| test_cstring | ✅ PASSED | 所有测试通过，执行时间 ~0.05s |
| test_cdeque | ✅ PASSED | 之前有双重释放问题，现已修复 |
| test_errors | ✅ PASSED | 宏类型问题已修复 |
| test_buffer | 🔄 待测试 | 新创建 |
| test_hex | 🔄 待测试 | 新创建 |
| test_types | 🔄 待测试 | 综合集成测试 |

### 0.2 已修复的问题

| 测试 | 问题类型 | 原因分析 | 修复状态 |
|---|----|----|----|
| test_errors | 宏类型不匹配 | RGW_CHECK_MSG 宏内部使用 int，但 rgw_set_error 期望 rgw_error_code_t | ✅ 已修复 |
| test_types | API 不匹配 | 使用了错误的函数名和参数类型 | ✅ 已修复 |
| test_buffer | 缺少头文件 | 未包含 rgw_cmemory.h | ✅ 已修复 |

### 0.3 经验教训

1. **内存管理**：必须明确每个内存分配的生命周期，避免双重释放
2. **宏定义**：宏内部变量类型应与被调用函数期望的类型保持一致
3. **测试验证**：测试必须验证正确行为而非仅验证编译通过
4. **字符处理**：C 标准库的字符函数（tolower, toupper 等）需要先将 char 转换为 unsigned char，避免符号扩展问题
5. **数值边界**：字符串转数值函数必须处理负数和边界溢出情况
6. **参数校验**：函数参数必须检查负数情况

---

## 1. API 命名规范

### 1.1 函数命名一致性

**规则 1.1.1: 使用统一的函数前缀**

- 所有 rgw_string 函数使用 `rgw_string_*` 前缀
- 所有 rgw_buffer 函数使用 `rgw_buffer_*` 前缀
- 所有 rgw_hex 函数使用 `rgw_hex_*` 前缀
- 所有 rgw_b64 函数使用 `rgw_b64_*` 前缀

**规则 1.1.2: 测试文件必须包含正确的头文件**

- 使用 `rgw_cmemory.h` 中的内存管理函数
- 使用 `rgw_cstring.h` 中的字符串函数
- 使用 `rgw_b64.h` 中的 Base64 函数

错误示例:
```c
// 错误：使用了不存在的函数
rgw_cstring_t* str = rgw_cstring_create("hello");  // 不存在
char* encoded = NULL;
rgw_b64_encode_str(original, &encoded);  // 不存在
```

正确做法:
```c
// 正确：使用正确的 API
rgw_string_t* str = rgw_string_create("hello");
char* encoded = NULL;
rgw_b64_encode((const uint8_t*)original, strlen(original), &encoded);
```

---

## 2. 头文件包含规范

### 2.1 必需的头文件

**规则 2.1.1: 使用内存管理函数时必须包含 rgw_cmemory.h**

错误示例:
```c
#include "rgw_buffer.h"
// 错误：rgw_c_free 未声明
rgw_c_free(ptr);
```

正确做法:
```c
#include "rgw_buffer.h"
#include "rgw_cmemory.h"
// 正确：rgw_c_free 已声明
rgw_c_free(ptr);
```

---

## 3. 类型转换规范

### 3.1 类型转换安全

**规则 3.1.1: 字符串到字节流的转换**

错误示例:
```c
// 错误：rgw_b64_encode 期望 uint8_t*
char* str = "hello";
rgw_b64_encode(str, len, &output);  // 类型不匹配
```

正确做法:
```c
// 正确：显式转换
const char* str = "hello";
rgw_b64_encode((const uint8_t*)str, strlen(str), &output);
```

---

## 4. 测试代码质量规范

### 4.1 测试必须验证正确行为

**规则 4.1.1: 每个测试函数必须包含断言**

错误示例:
```c
// 错误：没有验证结果
void test_create(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    // 没有验证 buf 是否有效
}
```

正确做法:
```c
// 正确：包含断言验证
void test_create(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);
    assert(rgw_buffer_length(buf) == 0);
}
```

### 4.2 测试必须清理资源

**规则 4.2.1: 每个测试必须释放所有分配的资源**

错误示例:
```c
// 错误：内存泄漏
void test_leak(void) {
    char* str = malloc(100);
    // 忘记 free
}
```

正确做法:
```c
// 正确：释放资源
void test_no_leak(void) {
    char* str = malloc(100);
    // ... 使用 str ...
    free(str);
}
```

---

## 5. 编译和测试要求

### 5.1 编译检查

- 每次代码修改后，**必须**运行编译和测试：
 ```bash
 cd build && cmake .. && make -j4
 ctest --output-on-failure
 ```
- 确保所有测试通过后再结束任务
- 使用 `ReadLints` 检查代码质量

### 5.2 测试覆盖

每个模块必须包含以下测试：
- 创建/销毁测试
- 基本操作测试
- 边界条件测试
- 错误处理测试
- 资源清理测试

---

**文档版本**: 2.2
**生成日期**: 2026-03-17
**基于错误**: test_types (API 不匹配), test_buffer (缺少头文件)
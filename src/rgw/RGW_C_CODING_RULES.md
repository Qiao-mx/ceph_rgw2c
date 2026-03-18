# RGW C 语言开发规范与最佳实践

## 目的

本文档记录在 RGW C 语言转换项目中遇到的常见问题、解决方案和最佳实践，为后续开发提供指导。

## 1. 通配符匹配函数实现规范

### 1.1 问题案例

**问题文件**: `rgw_string_c.c` - `wildcard_match()` 函数

**原始实现的问题**:
```c
// 错误的实现
static bool wildcard_match(const char *pattern, const char *str, bool case_insensitive)
{
    const char *s = str;
    const char *p = pattern;
    const char *s_backup = NULL;
    const char *p_backup = NULL;

    while (*s != '\0' || *p != '\0') {  // 问题 1: 循环条件不当
        if (*p == '*') {
            s_backup = s;
            p_backup = p;
            p++;  // 问题 2: 没有处理连续的 *
        } else if (*s != '\0' && ...) {
            s++;
            p++;
        } else if (s_backup != NULL) {
            s_backup++;
            s = s_backup;
            p = p_backup;  // 问题 3: 回溯逻辑不完善
        } else {
            return false;
        }
    }

    return true;  // 问题 4: 没有检查 pattern 是否完全匹配
}
```

**导致的问题**:
1. `*.txt` 错误匹配 `file.pdf`（应该返回 false）
2. 某些情况下可能进入死循环
3. 没有正确处理 pattern 以 `*` 结尾的情况

### 1.2 正确的实现

```c
static bool wildcard_match(const char *pattern, const char *str, bool case_insensitive)
{
    const char *s = str;
    const char *p = pattern;
    const char *s_backup = NULL;
    const char *p_backup = NULL;

    while (*s != '\0') {
        if (*p == '*') {
            /* 记录回溯点 */
            s_backup = s;
            p_backup = p;
            /* 跳过连续的 * */
            do {
                p++;
            } while (*p == '*');
            if (*p == '\0') {
                /* pattern 以 * 结尾，匹配所有剩余字符 */
                return true;
            }
        } else if (*p == '?' ||
                   (case_insensitive ?
                    (tolower((unsigned char)*p) == tolower((unsigned char)*s)) :
                    (*p == *s))) {
            /* 匹配单个字符 */
            s++;
            p++;
        } else if (s_backup != NULL) {
            /* 回溯：从上一个 * 后多匹配一个字符 */
            s_backup++;
            s = s_backup;
            p = p_backup;
        } else {
            return false;
        }
    }

    /* 处理 pattern 剩余的 * */
    while (*p == '*') {
        p++;
    }

    /* 只有当 pattern 也用完时才匹配成功 */
    return *p == '\0';
}
```

### 1.3 关键改进点

1. **循环条件**: 改为 `while (*s != '\0')`，先确保字符串遍历完成
2. **连续 * 处理**: 使用 `do-while` 跳过连续的 `*` 字符
3. **提前返回优化**: 当 pattern 以 `*` 结尾时直接返回 true
4. **后续检查**: 循环结束后检查 pattern 剩余的 `*`，确保完全匹配
5. **最终验证**: 返回 `*p == '\0'` 确保 pattern 也完全消耗

### 1.4 测试用例

```c
/* 必须通过的测试用例 */
assert(rgw_str_match_wildcards("*.txt", "file.txt", 0) == true);
assert(rgw_str_match_wildcards("*.txt", "file.pdf", 0) == false);
assert(rgw_str_match_wildcards("test?.dat", "test1.dat", 0) == true);
assert(rgw_str_match_wildcards("*", "anything", 0) == true);
assert(rgw_str_match_wildcards("a*b*c", "aXXbYYc", 0) == true);
assert(rgw_str_match_wildcards("***", "anything", 0) == true);  // 连续 *
assert(rgw_str_match_wildcards("test*", "test", 0) == true);   // * 在末尾
```

## 2. C 语言字符串处理最佳实践

### 2.1 内存管理

**规则**:
1. 所有动态分配的内存必须在文档中明确说明由调用者释放
2. 使用标准的 `malloc/free`，不要混用不同的分配器
3. NULL 输入必须返回 NULL，不要尝试"智能"处理

**示例**:
```c
char *rgw_str_dup(const char *s)
{
    if (s == NULL) {
        return NULL;  // 正确：NULL 输入返回 NULL
    }
    
    size_t len = strlen(s);
    char *result = (char *)malloc(len + 1);
    if (result == NULL) {
        return NULL;  // 内存分配失败也要返回 NULL
    }
    
    memcpy(result, s, len + 1);
    return result;  // 调用者负责 free()
}
```

### 2.2 边界检查

**规则**:
1. 所有带偏移量的函数必须检查负数
2. 所有数组访问必须检查边界
3. 使用 `size_t` 表示长度和大小，避免符号问题

**示例**:
```c
int rgw_str_casecmp_with_offset(const char *s1, int ofs, int size, const char *s2)
{
    if (s1 == NULL || s2 == NULL) {
        return -1;
    }
    
    /* 必须检查负数参数 */
    if (ofs < 0 || size < 0) {
        return -1;
    }
    
    size_t s1_len = strlen(s1);
    if ((size_t)ofs >= s1_len) {
        return -1;
    }
    
    /* 确保不超出范围 */
    if (ofs + size > (int)s1_len) {
        size = (int)(s1_len - ofs);
    }
    
    return strncasecmp(s1 + ofs, s2, (size_t)size);
}
```

### 2.3 错误码规范

**规则**:
1. 成功返回 0，失败返回负的错误码
2. 使用标准 errno 值（-EINVAL, -ERANGE 等）
3. 转换函数必须检查所有可能的错误情况

**示例**:
```c
int rgw_str_to_ll(const char *s, int64_t *val)
{
    char *end;
    
    if (s == NULL || val == NULL) {
        return -EINVAL;  // 参数无效
    }
    
    /* 跳过前导空格 */
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    long long result = strtoll(s, &end, 10);
    
    /* 检查溢出 */
    if (result == LLONG_MAX && errno == ERANGE) {
        return -ERANGE;
    }
    if (result == LLONG_MIN && errno == ERANGE) {
        return -ERANGE;
    }
    
    /* 检查是否有未解析的字符 */
    if (*end != '\0') {
        return -EINVAL;
    }
    
    *val = neg ? (int64_t)(-result) : (int64_t)result;
    return 0;  // 成功返回 0
}
```

## 3. 测试规范

### 3.1 单元测试编写要求

**必须包含的测试类型**:
1. **正常路径测试**: 验证函数的基本功能
2. **边界条件测试**: NULL 输入、空字符串、极值等
3. **错误处理测试**: 验证错误返回值
4. **内存泄漏测试**: 确保所有分配的内存都被释放

**测试模板**:
```c
int test_function_name(void)
{
    /* 测试 1: 正常情况 */
    if (function_normal_case() != EXPECTED_RESULT) {
        TEST_FAILED("normal case failed");
    }
    
    /* 测试 2: 边界条件 */
    if (function_boundary_case() != EXPECTED_RESULT) {
        TEST_FAILED("boundary case failed");
    }
    
    /* 测试 3: NULL/无效输入 */
    if (function(NULL) != NULL) {
        TEST_FAILED("should handle NULL input");
    }
    
    TEST_PASSED;
    return 0;
}
```

### 3.2 测试调试技巧

当测试卡住或无输出时：
1. 使用 `strace` 查看系统调用
2. 分段测试，逐步缩小问题范围
3. 检查是否有死循环（特别是 while 循环）
4. 使用 `timeout` 命令防止无限等待

**调试命令**:
```bash
# 带超时运行
timeout 5 ./test_program

# 使用 strace 跟踪
strace -e trace=write ./test_program 2>&1 | head -50

# 编译时添加调试信息
gcc -g -o test test.c -Wall -Wextra
```

## 4. 代码审查清单

在提交 C 语言代码前，必须检查以下项目：

### 4.1 内存安全
- [ ] 所有 malloc 都有对应的 free
- [ ] 没有双重释放
- [ ] 没有使用已释放的内存
- [ ] 数组访问不越界

### 4.2 错误处理
- [ ] 所有可能失败的函数都返回错误码
- [ ] NULL 指针检查
- [ ] 整数溢出检查
- [ ] 缓冲区溢出保护

### 4.3 循环和递归
- [ ] 所有循环都有明确的退出条件
- [ ] 没有潜在的死循环
- [ ] 递归有终止条件
- [ ] 回溯算法正确实现

### 4.4 测试覆盖
- [ ] 正常路径测试通过
- [ ] 边界条件测试通过
- [ ] 错误处理测试通过
- [ ] 无内存泄漏

## 5. 经验总结

### 5.1 从本次修复中学到的

**问题**: 通配符匹配函数 `wildcard_match()` 存在逻辑缺陷

**根本原因**:
1. 循环条件设计不当 (`*s != '\0' || *p != '\0'`)
2. 没有正确处理 pattern 剩余的字符
3. 回溯逻辑不够完善
4. 缺少最终的匹配验证

**解决方法**:
1. 重新设计循环结构，先遍历完字符串
2. 添加 pattern 剩余字符的后处理
3. 增加最终验证步骤
4. 优化连续 `*` 的处理

**验证**:
- 修复后所有 17 个测试用例全部通过
- 包括之前失败的 `*.txt` vs `file.pdf` 测试

### 5.2 通用原则

1. **防御性编程**: 始终检查输入参数的有效性
2. **最小惊讶原则**: 行为符合调用者的预期
3. **显式优于隐式**: 明确处理所有边界情况
4. **测试驱动**: 先写测试，再实现功能
5. **代码审查**: 关键算法必须经过审查

## 6. 参考资源

- C 语言标准库文档：https://en.cppreference.com/w/c
- CERT C 编码标准：https://wiki.sei.cmu.edu/confluence/display/c/SEI+CERT+C+Coding+Standard
- C 语言常见陷阱：《C Traps and Pitfalls》
- 本项目 c_common 容器库文档

---

*本文件最后更新：2026-03-17*
*维护者：RGW C++ 到 C 转换项目组*

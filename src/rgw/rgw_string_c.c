/**
 * @file rgw_string_c.c
 * @brief RGW 字符串 C 接口实现
 *
 * 本文件实现 rgw_string_c.h 中定义的字符串工具函数。
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-16
 */

#include "rgw_string_c.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief 简单的通配符匹配实现
 *
 * 支持:
 * - * 匹配任意多个字符
 * - ? 匹配任意单个字符
 *
 * @param pattern 通配符模式
 * @param str 待匹配字符串
 * @param case_insensitive 是否忽略大小写
 * @return true 匹配成功, false 匹配失败
 */
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

/**
 * @brief 不区分大小写的字符串比较
 */
int rgw_str_casecmp(const char *s1, const char *s2)
{
    if (s1 == NULL && s2 == NULL) {
        return 0;
    }
    if (s1 == NULL) {
        return -1;
    }
    if (s2 == NULL) {
        return 1;
    }
    return strcasecmp(s1, s2);
}

/**
 * @brief 不区分大小写的字符串比较（带长度限制）
 */
int rgw_str_ncasecmp(const char *s1, const char *s2, size_t n)
{
    if (s1 == NULL && s2 == NULL) {
        return 0;
    }
    if (s1 == NULL) {
        return -1;
    }
    if (s2 == NULL) {
        return 1;
    }
    return strncasecmp(s1, s2, n);
}

/**
 * @brief 字符串转换为长整型
 */
int rgw_str_to_ll(const char *s, int64_t *val)
{
    char *end;

    if (s == NULL || val == NULL) {
        return -EINVAL;
    }

    /* 跳过前导空格 */
    while (*s == ' ' || *s == '\t') {
        s++;
    }

    /* 检查是否有负号 */
    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    long long result = strtoll(s, &end, 10);
    if (result == LLONG_MAX && errno == ERANGE) {
        return -ERANGE;
    }
    if (result == LLONG_MIN && errno == ERANGE) {
        return -ERANGE;
    }

    if (*end != '\0') {
        return -EINVAL;
    }

    *val = neg ? (int64_t)(-result) : (int64_t)result;
    return 0;
}

/**
 * @brief 字符串转换为无符号长整型
 */
int rgw_str_to_ull(const char *s, uint64_t *val)
{
    char *end;

    if (s == NULL || val == NULL) {
        return -EINVAL;
    }

    unsigned long long result = strtoull(s, &end, 10);
    if (result == ULLONG_MAX && errno == ERANGE) {
        return -ERANGE;
    }

    if (*end != '\0') {
        return -EINVAL;
    }

    *val = (uint64_t)result;
    return 0;
}

/**
 * @brief 字符串转换为整型
 */
int rgw_str_to_l(const char *s, int32_t *val)
{
    char *end;

    if (s == NULL || val == NULL) {
        return -EINVAL;
    }

    long result = strtol(s, &end, 10);
    if (result == LONG_MAX && errno == ERANGE) {
        return -ERANGE;
    }

    if (*end != '\0') {
        return -EINVAL;
    }

    *val = (int32_t)result;
    return 0;
}

/**
 * @brief 字符串转换为无符号整型
 */
int rgw_str_to_ul(const char *s, uint32_t *val)
{
    char *end;

    if (s == NULL || val == NULL) {
        return -EINVAL;
    }

    unsigned long result = strtoul(s, &end, 10);
    if (result == ULONG_MAX && errno == ERANGE) {
        return -ERANGE;
    }

    if (*end != '\0') {
        return -EINVAL;
    }

    *val = (uint32_t)result;
    return 0;
}

/**
 * @brief 通配符模式匹配
 */
bool rgw_str_match_wildcards(const char *pattern,
                             const char *input,
                             uint32_t flags)
{
    bool case_insensitive = false;

    if (pattern == NULL || input == NULL) {
        return false;
    }

    if (flags & RGW_STR_MATCH_CASE_INSENSITIVE) {
        case_insensitive = true;
    }

    return wildcard_match(pattern, input, case_insensitive);
}

/**
 * @brief 计算字符串总长度（内部实现）
 */
static size_t calc_str_len_va(va_list args)
{
    size_t total = 0;
    const char *s;

    while ((s = va_arg(args, const char *)) != NULL) {
        total += strlen(s);
    }

    return total;
}

/**
 * @brief 计算字符串总长度（用于预分配内存）
 */
size_t rgw_str_size_calc(const char *first, ...)
{
    size_t total = 0;
    va_list args;

    if (first == NULL) {
        return 0;
    }

    total = strlen(first);

    va_start(args, first);
    total += calc_str_len_va(args);
    va_end(args);

    return total;
}

/**
 * @brief 字符串拼接（内部实现）
 */
static size_t cat_str_va(char *dest, size_t dest_size, const char *first, va_list args)
{
    size_t written = 0;
    const char *s = first;
    size_t len;

    while (s != NULL) {
        len = strlen(s);
        if (written + len < dest_size) {
            memcpy(dest + written, s, len);
            written += len;
        }
        s = va_arg(args, const char *);
    }

    if (written < dest_size) {
        dest[written] = '\0';
    } else if (dest_size > 0) {
        dest[dest_size - 1] = '\0';
    }

    return written;
}

/**
 * @brief 字符串拼接（预分配版本）
 */
size_t rgw_str_cat(char *dest, size_t dest_size, const char *first, ...)
{
    size_t result;
    va_list args;

    if (dest == NULL || dest_size == 0) {
        return 0;
    }

    dest[0] = '\0';

    if (first == NULL) {
        return 0;
    }

    va_start(args, first);
    result = cat_str_va(dest, dest_size, first, args);
    va_end(args);

    return result;
}

/**
 * @brief 带分隔符的字符串拼接
 */
size_t rgw_str_join(char delim, char *dest, size_t dest_size, const char *first, ...)
{
    size_t written = 0;
    va_list args;
    const char *s = first;
    size_t len;
    char delims[2] = {delim, '\0'};

    if (dest == NULL || dest_size == 0) {
        return 0;
    }

    dest[0] = '\0';

    if (first == NULL) {
        return 0;
    }

    /* 第一个字符串 */
    len = strlen(s);
    if (written + len < dest_size) {
        memcpy(dest + written, s, len);
        written += len;
    }

    /* 后续字符串，前面加上分隔符 */
    va_start(args, first);
    while ((s = va_arg(args, const char *)) != NULL) {
        if (written + 1 < dest_size) {
            dest[written++] = delim;
        }
        len = strlen(s);
        if (written + len < dest_size) {
            memcpy(dest + written, s, len);
            written += len;
        }
    }
    va_end(args);

    if (written < dest_size) {
        dest[written] = '\0';
    } else if (dest_size > 0) {
        dest[dest_size - 1] = '\0';
    }

    return written;
}

/**
 * @brief 字符串数组排序比较函数（不区分大小写）
 */
int rgw_str_casecmp_qsort(const void *a, const void *b)
{
    const char * const *sa = a;
    const char * const *sb = b;

    if (sa == NULL && sb == NULL) {
        return 0;
    }
    if (sa == NULL) {
        return -1;
    }
    if (sb == NULL) {
        return 1;
    }

    return strcasecmp(*sa, *sb);
}

/**
 * @brief 带偏移量的不区分大小写字符串比较
 */
int rgw_str_casecmp_with_offset(const char *s1, int ofs, int size, const char *s2)
{
    if (s1 == NULL || s2 == NULL) {
        return -1;
    }

    /* 检查负数参数 */
    if (ofs < 0 || size < 0) {
        return -1;
    }

    /* 确保偏移量有效 */
    size_t s1_len = strlen(s1);
    if ((size_t)ofs >= s1_len) {
        return -1;
    }

    /* 确保不超出 s1 范围 */
    if (ofs + size > (int)s1_len) {
        size = (int)(s1_len - ofs);
    }

    return strncasecmp(s1 + ofs, s2, (size_t)size);
}

/**
 * @brief 字符串复制（动态分配版本）
 */
char *rgw_str_dup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s);
    char *result = (char *)malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }

    memcpy(result, s, len + 1);
    return result;
}

/**
 * @brief 字符串复制（带长度限制）
 */
char *rgw_str_dup_n(const char *s, size_t max_len)
{
    if (s == NULL) {
        return NULL;
    }

    size_t s_len = strlen(s);
    size_t copy_len = (s_len < max_len) ? s_len : max_len;

    char *result = (char *)malloc(copy_len + 1);
    if (result == NULL) {
        return NULL;
    }

    memcpy(result, s, copy_len);
    result[copy_len] = '\0';
    return result;
}

/**
 * @brief 字符是否为空白字符
 */
static bool is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

/**
 * @brief 字符大小写转换
 */
static char char_case(char c, uint32_t flags)
{
    if (flags & RGW_STR_CASE_LOWER) {
        return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
    }
    if (flags & RGW_STR_CASE_UPPER) {
        return (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    return c;
}

/**
 * @brief 字符串大小写转换
 */
char *rgw_str_case(const char *s, uint32_t flags)
{
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s);
    char *result = (char *)malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < len; i++) {
        result[i] = char_case(s[i], flags);
    }
    result[len] = '\0';

    return result;
}

/**
 * @brief 字符串修剪
 */
char *rgw_str_trim(const char *s, uint32_t flags)
{
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s);
    if (len == 0) {
        return rgw_str_dup("");
    }

    size_t start = 0;
    size_t end = len - 1;

    /* 去除左侧空白 */
    if (flags & RGW_STR_TRIM_LEFT) {
        while (start < len && is_whitespace(s[start])) {
            start++;
        }
    }

    /* 去除右侧空白 */
    if (flags & RGW_STR_TRIM_RIGHT) {
        while (end > start && is_whitespace(s[end])) {
            end--;
        }
    }

    /* 计算修剪后的长度 */
    size_t trimmed_len = (end >= start) ? (end - start + 1) : 0;

    char *result = (char *)malloc(trimmed_len + 1);
    if (result == NULL) {
        return NULL;
    }

    if (trimmed_len > 0) {
        memcpy(result, s + start, trimmed_len);
    }
    result[trimmed_len] = '\0';

    return result;
}

/**
 * @brief 字符串前缀检查
 */
bool rgw_str_starts_with(const char *s, const char *prefix)
{
    if (s == NULL || prefix == NULL) {
        return false;
    }

    size_t prefix_len = strlen(prefix);
    size_t s_len = strlen(s);

    if (prefix_len > s_len) {
        return false;
    }

    return strncmp(s, prefix, prefix_len) == 0;
}

/**
 * @brief 字符串后缀检查
 */
bool rgw_str_ends_with(const char *s, const char *suffix)
{
    if (s == NULL || suffix == NULL) {
        return false;
    }

    size_t suffix_len = strlen(suffix);
    size_t s_len = strlen(s);

    if (suffix_len > s_len) {
        return false;
    }

    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

/**
 * @brief 字符串替换（单次）
 */
char *rgw_str_replace(const char *s, const char *old_substr, const char *new_substr)
{
    if (s == NULL || old_substr == NULL || new_substr == NULL) {
        return NULL;
    }

    size_t s_len = strlen(s);
    size_t old_len = strlen(old_substr);
    size_t new_len = strlen(new_substr);

    /* 查找第一个匹配位置 */
    const char *match = strstr(s, old_substr);
    if (match == NULL) {
        return rgw_str_dup(s);
    }

    size_t match_pos = match - s;

    /* 分配新字符串内存 */
    char *result = (char *)malloc(s_len - old_len + new_len + 1);
    if (result == NULL) {
        return NULL;
    }

    /* 复制前缀 */
    if (match_pos > 0) {
        memcpy(result, s, match_pos);
    }

    /* 复制新子串 */
    memcpy(result + match_pos, new_substr, new_len);

    /* 复制后缀 */
    size_t suffix_start = match_pos + old_len;
    size_t suffix_len = s_len - suffix_start;
    if (suffix_len > 0) {
        memcpy(result + match_pos + new_len, s + suffix_start, suffix_len);
    }

    result[s_len - old_len + new_len] = '\0';

    return result;
}

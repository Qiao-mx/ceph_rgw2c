/**
 * @file rgw_string_c.h
 * @brief RGW 字符串 C 接口
 *
 * 本文件提供 RGW 字符串工具的 C 语言实现，
 * 作为原 rgw_string.h/cc 的 C 语言替代版本。
 *
 * 功能包括:
 * - 字符串比较（区分大小写/不区分大小写）
 * - 字符串到数字的转换
 * - 通配符匹配
 * - 字符串拼接
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-16
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 不区分大小写的字符串比较
 *
 * 比较两个字符串，忽略大小写差异。
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @return 0 相等, <0 s1 < s2, >0 s1 > s2
 */
int rgw_str_casecmp(const char *s1, const char *s2);

/**
 * @brief 不区分大小写的字符串比较（带长度限制）
 *
 * 比较两个字符串的前 n 个字符，忽略大小写差异。
 *
 * @param s1 第一个字符串
 * @param s2 第二个字符串
 * @param n 比较的字符数
 * @return 0 相等, <0 s1 < s2, >0 s1 > s2
 */
int rgw_str_ncasecmp(const char *s1, const char *s2, size_t n);

/**
 * @brief 字符串转换为长整型
 *
 * 将字符串转换为 int64_t 值。
 *
 * @param s 输入字符串
 * @param val 输出值
 * @return 0 成功, -EINVAL 格式错误, -ERANGE 值溢出
 */
int rgw_str_to_ll(const char *s, int64_t *val);

/**
 * @brief 字符串转换为无符号长整型
 *
 * 将字符串转换为 uint64_t 值。
 *
 * @param s 输入字符串
 * @param val 输出值
 * @return 0 成功, -EINVAL 格式错误, -ERANGE 值溢出
 */
int rgw_str_to_ull(const char *s, uint64_t *val);

/**
 * @brief 字符串转换为整型
 *
 * 将字符串转换为 int32_t 值。
 *
 * @param s 输入字符串
 * @param val 输出值
 * @return 0 成功, -EINVAL 格式错误, -ERANGE 值溢出
 */
int rgw_str_to_l(const char *s, int32_t *val);

/**
 * @brief 字符串转换为无符号整型
 *
 * 将字符串转换为 uint32_t 值。
 *
 * @param s 输入字符串
 * @param val 输出值
 * @return 0 成功, -EINVAL 格式错误, -ERANGE 值溢出
 */
int rgw_str_to_ul(const char *s, uint32_t *val);

/**
 * @brief 通配符匹配标志
 */
typedef enum rgw_str_match_flags {
    RGW_STR_MATCH_NONE = 0,         /**< 无特殊标志 */
    RGW_STR_MATCH_CASE_INSENSITIVE = 0x01  /**< 不区分大小写 */
} rgw_str_match_flags_t;

/**
 * @brief 通配符模式匹配
 *
 * 使用通配符模式匹配输入字符串。
 * 支持的通配符:
 * - * 匹配任意多个字符
 * - ? 匹配任意单个字符
 *
 * @param pattern 通配符模式
 * @param input 输入字符串
 * @param flags 匹配标志
 * @return true 匹配成功, false 匹配失败
 */
bool rgw_str_match_wildcards(const char *pattern,
                             const char *input,
                             uint32_t flags);

/**
 * @brief 计算字符串总长度（用于预分配内存）
 *
 * 计算多个字符串的总长度，可用于 string_cat_reserve 预分配空间。
 *
 * @param ... 可变数量的字符串参数，以 NULL 结尾
 * @return 总长度
 */
size_t rgw_str_size_calc(const char *first, ...);

/**
 * @brief 字符串拼接（预分配版本）
 *
 * 拼接多个字符串到一个目标缓冲区。
 *
 * @param dest 目标缓冲区（必须足够大）
 * @param dest_size 目标缓冲区大小
 * @param ... 要拼接的字符串，以 NULL 结尾
 * @return 写入的字符数（不包括终止空字符）
 */
size_t rgw_str_cat(char *dest, size_t dest_size, const char *first, ...);

/**
 * @brief 带分隔符的字符串拼接
 *
 * 使用指定分隔符拼接多个字符串。
 *
 * @param delim 分隔符
 * @param dest 目标缓冲区
 * @param dest_size 目标缓冲区大小
 * @param ... 要拼接的字符串，以 NULL 结尾
 * @return 写入的字符数
 */
size_t rgw_str_join(char delim, char *dest, size_t dest_size, const char *first, ...);

/**
 * @brief 字符串数组排序比较函数（不区分大小写）
 *
 * 用于 qsort 等排序函数的比较回调。
 *
 * @param a 第一个字符串指针的指针
 * @param b 第二个字符串指针的指针
 * @return 比较结果
 */
int rgw_str_casecmp_qsort(const void *a, const void *b);

/**
 * @brief 带偏移量的不区分大小写字符串比较
 *
 * 比较 s1 从 ofs 位置开始的 size 个字符与 s2，忽略大小写。
 *
 * @param s1 第一个字符串
 * @param ofs 偏移量
 * @param size 比较的字符数
 * @param s2 第二个字符串
 * @return 0 相等, <0 s1 < s2, >0 s1 > s2
 */
int rgw_str_casecmp_with_offset(const char *s1, int ofs, int size, const char *s2);

/**
 * @brief 计算字符串字面量的长度
 *
 * 计算字符数组的长度，等同于 C++ 的 sarrlen。
 * 注意：返回的是数组大小减1（不包括终止空字符）。
 *
 * @param arr 字符串字面量
 * @return 字符串长度（不包括终止空字符）
 */
#define RGW_SARRLEN(arr) (sizeof(arr) - 1)

/**
 * @brief 字符串复制（动态分配版本）
 *
 * 复制字符串到新分配的内存中。
 * 调用者需要使用 free() 释放返回的内存。
 *
 * @param s 输入字符串
 * @return 新分配的字符串副本，失败返回 NULL
 */
char *rgw_str_dup(const char *s);

/**
 * @brief 字符串复制（带长度限制）
 *
 * 复制指定长度的字符串到新分配的内存中。
 * 调用者需要使用 free() 释放返回的内存。
 *
 * @param s 输入字符串
 * @param max_len 最大复制长度（不包括终止空字符）
 * @return 新分配的字符串副本，失败返回 NULL
 */
char *rgw_str_dup_n(const char *s, size_t max_len);

/**
 * @brief 字符串大小写转换标志
 */
typedef enum rgw_str_case_flags {
    RGW_STR_CASE_LOWER = 0x01,  /**< 转换为小写 */
    RGW_STR_CASE_UPPER = 0x02   /**< 转换为大写 */
} rgw_str_case_flags_t;

/**
 * @brief 字符串大小写转换（动态分配版本）
 *
 * 将字符串转换为大写或小写。
 * 调用者需要使用 free() 释放返回的内存。
 *
 * @param s 输入字符串
 * @param flags 转换标志 (RGW_STR_CASE_LOWER 或 RGW_STR_CASE_UPPER)
 * @return 新分配的转换后的字符串，失败返回 NULL
 */
char *rgw_str_case(const char *s, uint32_t flags);

/**
 * @brief 字符串修剪标志
 */
typedef enum rgw_str_trim_flags {
    RGW_STR_TRIM_LEFT = 0x01,   /**< 去除左侧空白 */
    RGW_STR_TRIM_RIGHT = 0x02,  /**< 去除右侧空白 */
    RGW_STR_TRIM_BOTH = 0x03    /**< 去除两侧空白 */
} rgw_str_trim_flags_t;

/**
 * @brief 去除字符串首尾空白（动态分配版本）
 *
 * 去除字符串左侧、右侧或两侧的空白字符。
 * 调用者需要使用 free() 释放返回的内存。
 *
 * @param s 输入字符串
 * @param flags 修剪标志
 * @return 新分配的修剪后的字符串，失败返回 NULL
 */
char *rgw_str_trim(const char *s, uint32_t flags);

/**
 * @brief 字符串前缀检查
 *
 * 检查字符串是否以指定前缀开头。
 *
 * @param s 要检查的字符串
 * @param prefix 前缀
 * @return true 以指定前缀开头, false 不是
 */
bool rgw_str_starts_with(const char *s, const char *prefix);

/**
 * @brief 字符串后缀检查
 *
 * 检查字符串是否以指定后缀结尾。
 *
 * @param s 要检查的字符串
 * @param suffix 后缀
 * @return true 以指定后缀结尾, false 不是
 */
bool rgw_str_ends_with(const char *s, const char *suffix);

/**
 * @brief 字符串替换（单次）
 *
 * 替换字符串中第一个出现的子串。
 * 调用者需要使用 free() 释放返回的内存。
 *
 * @param s 输入字符串
 * @param old_substr 要替换的子串
 * @param new_substr 替换为的子串
 * @return 新分配的替换后的字符串，失败返回 NULL
 */
char *rgw_str_replace(const char *s, const char *old_substr, const char *new_substr);

#ifdef __cplusplus
}
#endif

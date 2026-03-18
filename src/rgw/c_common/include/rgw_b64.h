/**
 * @file rgw_b64.h
 * @brief RGW Base64 编解码 C 接口
 *
 * 本文件提供 Base64 编解码功能，用于替代原 C++ 版本的 rgw_b64。
 *
 * 功能包括:
 * - Base64 编码
 * - Base64 解码
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-17
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base64 错误码 */
typedef enum rgw_b64_error {
    RGW_B64_OK = 0,
    RGW_B64_ERR_INVALID = -1,
    RGW_B64_ERR_NO_MEMORY = -2,
    RGW_B64_ERR_INVALID_INPUT = -3
} rgw_b64_error_t;

/**
 * @brief Base64 编码
 *
 * @param input 输入数据
 * @param input_len 输入数据长度
 * @param output 输出 Base64 字符串（需要调用者释放）
 * @return 0 成功，非0 失败
 */
int rgw_b64_encode(const uint8_t *input, size_t input_len, char **output);

/**
 * @brief Base64 解码
 *
 * @param input Base64 字符串
 * @param output 输出数据（需要调用者释放）
 * @param output_len 输出数据长度
 * @return 0 成功，非0 失败
 */
int rgw_b64_decode(const char *input, uint8_t **output, size_t *output_len);

/**
 * @brief Base64 编码（内存到内存）
 *
 * @param input 输入数据
 * @param input_len 输入长度
 * @param output 输出数据（需要调用者释放）
 * @param output_len 输出长度
 * @return 0 成功，非0 失败
 */
int rgw_b64_encode_binary(const uint8_t *input, size_t input_len,
                          uint8_t **output, size_t *output_len);

/**
 * @brief Base64 解码（内存到内存）
 *
 * @param input 输入数据
 * @param input_len 输入长度
 * @param output 输出数据（需要调用者释放）
 * @param output_len 输出长度
 * @return 0 成功，非0 失败
 */
int rgw_b64_decode_binary(const uint8_t *input, size_t input_len,
                          uint8_t **output, size_t *output_len);

/**
 * @brief 计算 Base64 编码后的长度
 *
 * @param input_len 输入数据长度
 * @return 编码后长度
 */
size_t rgw_b64_encoded_len(size_t input_len);

/**
 * @brief 计算 Base64 解码后的最大长度
 *
 * @param input_len 输入数据长度
 * @return 解码后最大长度
 */
size_t rgw_b64_decoded_len_max(size_t input_len);

#ifdef __cplusplus
}
#endif

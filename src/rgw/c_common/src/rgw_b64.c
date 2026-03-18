/**
 * @file rgw_b64.c
 * @brief RGW Base64 编解码 C 实现
 *
 * 提供 Base64 编解码功能，用于替代原 C++ 版本的 rgw_b64。
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "rgw_b64.h"

/* Base64 编码表 */
static const char b64_table[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 解码表 -1 表示无效字符 */
static int b64_decode_table[256];

static int b64_table_initialized = 0;

static void init_decode_table(void)
{
    if (b64_table_initialized) return;
    
    memset(b64_decode_table, -1, sizeof(b64_decode_table));
    for (int i = 0; i < 64; i++) {
        b64_decode_table[(unsigned char)b64_table[i]] = i;
    }
    b64_table_initialized = 1;
}

size_t rgw_b64_encoded_len(size_t input_len)
{
    /* Base64 编码: 3 字节 -> 4 字符, 填充到 4 的倍数 */
    return ((input_len + 2) / 3) * 4;
}

size_t rgw_b64_decoded_len_max(size_t input_len)
{
    /* Base64 解码: 4 字符 -> 3 字节 */
    return (input_len / 4) * 3;
}

int rgw_b64_encode_binary(const uint8_t *input, size_t input_len,
                          uint8_t **output, size_t *output_len)
{
    if (!input || !output) {
        return RGW_B64_ERR_INVALID;
    }
    
    size_t encoded_len = rgw_b64_encoded_len(input_len);
    char *out = (char *)malloc(encoded_len + 1);
    if (!out) {
        return RGW_B64_ERR_NO_MEMORY;
    }
    
    size_t i = 0;
    size_t j = 0;
    
    while (i < input_len) {
        uint32_t octet_a = i < input_len ? input[i++] : 0;
        uint32_t octet_b = i < input_len ? input[i++] : 0;
        uint32_t octet_c = i < input_len ? input[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        out[j++] = b64_table[(triple >> 18) & 0x3F];
        out[j++] = b64_table[(triple >> 12) & 0x3F];
        out[j++] = b64_table[(triple >> 6) & 0x3F];
        out[j++] = b64_table[triple & 0x3F];
    }
    
    /* 添加填充 */
    size_t mod = input_len % 3;
    if (mod > 0) {
        /* mod=1 时需要 2 个填充，mod=2 时需要 1 个填充 */
        size_t pad_count = 3 - mod;
        for (size_t p = 0; p < pad_count; p++) {
            out[encoded_len - 1 - p] = '=';
        }
    }
    
    out[encoded_len] = '\0';
    *output = (uint8_t *)out;
    if (output_len) {
        *output_len = encoded_len;
    }
    
    return RGW_B64_OK;
}

int rgw_b64_encode(const uint8_t *input, size_t input_len, char **output)
{
    return rgw_b64_encode_binary(input, input_len, (uint8_t **)output, NULL);
}

int rgw_b64_decode_binary(const uint8_t *input, size_t input_len,
                          uint8_t **output, size_t *output_len)
{
    if (!input || !output) {
        return RGW_B64_ERR_INVALID;
    }
    
    init_decode_table();
    
    /* 验证输入长度 */
    if (input_len % 4 != 0) {
        return RGW_B64_ERR_INVALID_INPUT;
    }
    
    /* 保存原始长度用于后续计算 */
    size_t original_input_len = input_len;
    
    /* 移除末尾的填充字符 '=' 并计算实际长度 */
    size_t padding = 0;
    while (input_len > 0 && input[input_len - 1] == '=') {
        padding++;
        input_len--;
    }
    
    if (input_len == 0) {
        *output = (uint8_t *)malloc(1);
        if (!*output) return RGW_B64_ERR_NO_MEMORY;
        (*output)[0] = '\0';
        if (output_len) *output_len = 0;
        return RGW_B64_OK;
    }
    
    /* 计算解码后的长度：使用原始长度计算 */
    size_t decoded_len = (original_input_len / 4) * 3 - padding;
    uint8_t *out = (uint8_t *)malloc(decoded_len + 1);
    if (!out) {
        return RGW_B64_ERR_NO_MEMORY;
    }
    
    size_t i = 0;
    size_t j = 0;
    
    while (i < input_len) {
        int sextet_a = b64_decode_table[input[i++]];
        int sextet_b = b64_decode_table[input[i++]];
        /* 读取 sextet_c 和 sextet_d，如果是填充则设为 0 */
        int sextet_c = (i < input_len && input[i] != '=') ? b64_decode_table[input[i]] : 0;
        i++;
        int sextet_d = (i < input_len && input[i] != '=') ? b64_decode_table[input[i]] : 0;
        i++;
        
        if (sextet_a < 0 || sextet_b < 0) {
            free(out);
            return RGW_B64_ERR_INVALID_INPUT;
        }
        
        uint32_t triple = (sextet_a << 18) + (sextet_b << 12) +
                          (sextet_c << 6) + sextet_d;
        
        if (j < decoded_len) out[j++] = (triple >> 16) & 0xFF;
        if (j < decoded_len) out[j++] = (triple >> 8) & 0xFF;
        if (j < decoded_len) out[j++] = triple & 0xFF;
    }
    
    out[decoded_len] = '\0';
    *output = out;
    if (output_len) *output_len = decoded_len;
    
    return RGW_B64_OK;
}

int rgw_b64_decode(const char *input, uint8_t **output, size_t *output_len)
{
    if (!input || !output) {
        return RGW_B64_ERR_INVALID;
    }
    
    /* 移除空白字符 */
    size_t input_len = strlen(input);
    size_t new_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (!isspace((unsigned char)input[i])) {
            new_len++;
        }
    }
    
    if (new_len == 0) {
        *output = (uint8_t *)malloc(1);
        if (!*output) return RGW_B64_ERR_NO_MEMORY;
        (*output)[0] = '\0';
        if (output_len) *output_len = 0;
        return RGW_B64_OK;
    }
    
    /* 分配临时缓冲区并移除空白 */
    char *filtered = (char *)malloc(new_len + 1);
    if (!filtered) {
        return RGW_B64_ERR_NO_MEMORY;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (!isspace((unsigned char)input[i])) {
            filtered[j++] = input[i];
        }
    }
    filtered[j] = '\0';
    
    int ret = rgw_b64_decode_binary((const uint8_t *)filtered, new_len, output, output_len);
    free(filtered);
    
    return ret;
}

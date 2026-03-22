/**
 * @file rgw_account_serde.c
 * @brief 账户信息序列化实现
 *
 * 账户信息的序列化/反序列化实现。
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "rgw_account_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 账户信息实现
 *============================================================================*/

rgw_account_info_t* rgw_account_info_create(void) {
    rgw_account_info_t* info = (rgw_account_info_t*)calloc(1, sizeof(rgw_account_info_t));
    return info;
}

void rgw_account_info_destroy(rgw_account_info_t* info) {
    if (!info) return;

    free(info->account_id);
    free(info->email);
    free(info->display_name);
    free(info);
}

void rgw_account_info_free_members(rgw_account_info_t* info) {
    if (!info) return;

    free(info->account_id);
    info->account_id = NULL;

    free(info->email);
    info->email = NULL;

    free(info->display_name);
    info->display_name = NULL;
}

size_t rgw_account_info_calc_encode_size(const rgw_account_info_t* info) {
    if (!info) return 0;

    /* 估算大小: account_id (64) + email (128) + display_name (256) + 固定字段 */
    size_t size = 64 + 128 + 256 + 32;
    return size;
}

int rgw_account_info_encode(const rgw_account_info_t* info,
                           uint8_t* buf,
                           size_t buf_size) {
    if (!info) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t needed = rgw_account_info_calc_encode_size(info);
    if (buf_size < needed) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 简单文本格式编码 */
    size_t offset = 0;
    int ret;

    if (info->account_id) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "account_id=%s\n", info->account_id);
        if (ret > 0) offset += (size_t)ret;
    }

    if (info->email) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "email=%s\n", info->email);
        if (ret > 0) offset += (size_t)ret;
    }

    if (info->display_name) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "display_name=%s\n", info->display_name);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf((char*)buf + offset, buf_size - offset, "suspended=%d\n", info->suspended ? 1 : 0);
    if (ret > 0) offset += (size_t)ret;

    return RGW_OK;
}

int rgw_account_info_decode(const uint8_t* buf,
                            size_t buf_size,
                            rgw_account_info_t* info) {
    if (!buf || !info) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 确保字符串以 null 结尾 */
    char* buffer = (char*)malloc(buf_size + 1);
    if (!buffer) return RGW_ERR_OUT_OF_MEMORY;
    memcpy(buffer, buf, buf_size);
    buffer[buf_size] = '\0';

    /* 解析 key=value 格式 */
    char* line = buffer;
    char* next;
    char* equals;

    while (line && *line) {
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* key = line;
            char* value = equals + 1;

            if (strcmp(key, "account_id") == 0) {
                free(info->account_id);
                info->account_id = strdup(value);
            } else if (strcmp(key, "email") == 0) {
                free(info->email);
                info->email = strdup(value);
            } else if (strcmp(key, "display_name") == 0) {
                free(info->display_name);
                info->display_name = strdup(value);
            } else if (strcmp(key, "suspended") == 0) {
                info->suspended = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
            }
        }

        line = next;
    }

    free(buffer);
    return RGW_OK;
}

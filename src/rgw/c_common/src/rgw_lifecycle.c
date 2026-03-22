/**
 * @file rgw_lifecycle.c
 * @brief 生命周期管理序列化实现
 *
 * 生命周期规则的序列化/反序列化实现。
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

#include "rgw_lifecycle.h"
#include "rgw_errors.h"

/*============================================================================
 * 生命周期条目序列化实现
 *============================================================================*/

size_t rgw_lc_entry_calc_encode_size(const rgw_lc_entry_t* entry) {
    if (!entry) return 0;

    /* 估算大小: bucket (256) + marker (256) + 固定字段 (sizeof(uint32_t)*2 + sizeof(time_t)) */
    size_t size = 256 + 256 + 32;
    return size;
}

int rgw_lc_entry_encode(const rgw_lc_entry_t* entry,
                        uint8_t* buf,
                        size_t buf_size,
                        size_t* actual_size) {
    if (!entry || !actual_size) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 计算需要的空间 */
    size_t needed = rgw_lc_entry_calc_encode_size(entry);
    *actual_size = needed;

    if (!buf) {
        /* 只计算大小 */
        return RGW_OK;
    }

    if (buf_size < needed) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 简单文本格式编码: bucket:marker:opstatus:time:days\n */
    size_t offset = 0;
    int ret;

    if (entry->bucket) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "%s:", entry->bucket);
        if (ret > 0) offset += (size_t)ret;
    }

    if (entry->marker) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "%s:", entry->marker);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf((char*)buf + offset, buf_size - offset, "%u:%lu:%u\n",
                   entry->opstatus,
                   (unsigned long)entry->time,
                   entry->days);
    if (ret > 0) offset += (size_t)ret;

    return RGW_OK;
}

int rgw_lc_entry_decode(const uint8_t* buf,
                        size_t buf_size,
                        rgw_lc_entry_t* entry) {
    if (!buf || !entry) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 确保字符串以 null 结尾 */
    char* buffer = (char*)malloc(buf_size + 1);
    if (!buffer) return RGW_ERR_OUT_OF_MEMORY;
    memcpy(buffer, buf, buf_size);
    buffer[buf_size] = '\0';

    /* 解析格式: bucket:marker:opstatus:time:days */
    char* token;
    char* saveptr;
    int field = 0;

    entry->bucket = NULL;
    entry->marker = NULL;
    entry->opstatus = 0;
    entry->time = 0;
    entry->days = 0;

    token = strtok_r(buffer, ":", &saveptr);
    while (token && field < 5) {
        switch (field) {
            case 0:
                entry->bucket = strdup(token);
                break;
            case 1:
                entry->marker = strdup(token);
                break;
            case 2:
                entry->opstatus = (uint32_t)atoi(token);
                break;
            case 3:
                entry->time = (time_t)strtoul(token, NULL, 10);
                break;
            case 4:
                entry->days = (uint32_t)atoi(token);
                break;
        }
        field++;
        token = strtok_r(NULL, ":\n", &saveptr);
    }

    free(buffer);
    return RGW_OK;
}

/*============================================================================
 * 生命周期头部序列化实现
 *============================================================================*/

size_t rgw_lc_head_calc_encode_size(const rgw_lc_head_t* head) {
    if (!head) return 0;

    /* 估算大小: start_date (64) + marker (256) + 固定字段 */
    size_t size = 64 + 256 + 32;
    return size;
}

int rgw_lc_head_encode(const rgw_lc_head_t* head,
                        uint8_t* buf,
                        size_t buf_size,
                        size_t* actual_size) {
    if (!head || !actual_size) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 计算需要的空间 */
    size_t needed = rgw_lc_head_calc_encode_size(head);
    *actual_size = needed;

    if (!buf) {
        /* 只计算大小 */
        return RGW_OK;
    }

    if (buf_size < needed) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 简单文本格式编码 */
    size_t offset = 0;
    int ret;

    if (head->start_date) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "%s:", head->start_date);
        if (ret > 0) offset += (size_t)ret;
    }

    if (head->marker) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "%s:", head->marker);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf((char*)buf + offset, buf_size - offset, "%u:%lu\n",
                   head->num_shards,
                   (unsigned long)head->now);
    if (ret > 0) offset += (size_t)ret;

    return RGW_OK;
}

int rgw_lc_head_decode(const uint8_t* buf,
                       size_t buf_size,
                       rgw_lc_head_t* head) {
    if (!buf || !head) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 确保字符串以 null 结尾 */
    char* buffer = (char*)malloc(buf_size + 1);
    if (!buffer) return RGW_ERR_OUT_OF_MEMORY;
    memcpy(buffer, buf, buf_size);
    buffer[buf_size] = '\0';

    /* 解析格式: start_date:marker:num_shards:now */
    char* token;
    char* saveptr;
    int field = 0;

    head->start_date = NULL;
    head->marker = NULL;
    head->num_shards = 0;
    head->now = 0;

    token = strtok_r(buffer, ":", &saveptr);
    while (token && field < 4) {
        switch (field) {
            case 0:
                head->start_date = strdup(token);
                break;
            case 1:
                head->marker = strdup(token);
                break;
            case 2:
                head->num_shards = (uint32_t)atoi(token);
                break;
            case 3:
                head->now = (time_t)strtoul(token, NULL, 10);
                break;
        }
        field++;
        token = strtok_r(NULL, ":\n", &saveptr);
    }

    free(buffer);
    return RGW_OK;
}

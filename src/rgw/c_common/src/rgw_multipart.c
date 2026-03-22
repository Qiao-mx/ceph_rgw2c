/**
 * @file rgw_multipart.c
 * @brief 多部分上传序列化实现
 *
 * 多部分上传信息的序列化/反序列化实现。
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

#include "rgw_multipart.h"
#include "rgw_errors.h"

/*============================================================================
 * 多部分上传信息实现
 *============================================================================*/

rgw_multipart_info_t* rgw_multipart_info_create(void) {
    rgw_multipart_info_t* info = (rgw_multipart_info_t*)calloc(1, sizeof(rgw_multipart_info_t));
    return info;
}

void rgw_multipart_info_destroy(rgw_multipart_info_t* info) {
    if (!info) return;

    free(info->bucket);
    free(info->object);
    free(info->upload_id);
    free(info);
}

int rgw_multipart_info_add_part(rgw_multipart_info_t* info,
                                const rgw_multipart_part_t* part) {
    (void)info;
    (void)part;
    /* 简化实现 */
    return RGW_OK;
}

size_t rgw_multipart_info_calc_encode_size(const rgw_multipart_info_t* info) {
    if (!info) return 0;

    /* 估算大小: bucket (256) + object (256) + upload_id (128) + 固定字段 */
    size_t size = 256 + 256 + 128 + 32;
    return size;
}

int rgw_multipart_info_encode(const rgw_multipart_info_t* info,
                              uint8_t* buf,
                              size_t buf_size) {
    if (!info) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t needed = rgw_multipart_info_calc_encode_size(info);
    if (buf_size < needed) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 简单文本格式编码 */
    size_t offset = 0;
    int ret;

    if (info->bucket) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "bucket=%s\n", info->bucket);
        if (ret > 0) offset += (size_t)ret;
    }

    if (info->object) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "object=%s\n", info->object);
        if (ret > 0) offset += (size_t)ret;
    }

    if (info->upload_id) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "upload_id=%s\n", info->upload_id);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf((char*)buf + offset, buf_size - offset, "size=%llu\n",
                   (unsigned long long)info->size);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf((char*)buf + offset, buf_size - offset, "part_count=%u\n",
                   info->part_count);
    if (ret > 0) offset += (size_t)ret;

    return RGW_OK;
}

int rgw_multipart_info_decode(const uint8_t* buf,
                              size_t buf_size,
                              rgw_multipart_info_t* info) {
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

            if (strcmp(key, "bucket") == 0) {
                free(info->bucket);
                info->bucket = strdup(value);
            } else if (strcmp(key, "object") == 0) {
                free(info->object);
                info->object = strdup(value);
            } else if (strcmp(key, "upload_id") == 0) {
                free(info->upload_id);
                info->upload_id = strdup(value);
            } else if (strcmp(key, "size") == 0) {
                info->size = (uint64_t)strtoull(value, NULL, 10);
            } else if (strcmp(key, "part_count") == 0) {
                info->part_count = (uint32_t)atoi(value);
            }
        }

        line = next;
    }

    free(buffer);
    return RGW_OK;
}

/*============================================================================
 * 兼容性函数实现
 *============================================================================*/

int rgw_multipart_upload_info_encode_alloc(const rgw_multipart_info_t* info,
                                          uint8_t** buf_out,
                                          size_t* buf_len_out) {
    if (!info || !buf_out || !buf_len_out) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t needed = rgw_multipart_info_calc_encode_size(info);
    uint8_t* buf = (uint8_t*)malloc(needed);
    if (!buf) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    int ret = rgw_multipart_info_encode(info, buf, needed);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    *buf_out = buf;
    *buf_len_out = needed;
    return RGW_OK;
}

int rgw_multipart_upload_info_decode(const uint8_t* buf,
                                     size_t buf_size,
                                     rgw_multipart_info_t* info) {
    return rgw_multipart_info_decode(buf, buf_size, info);
}

/*============================================================================
 * 分片信息序列化实现
 *============================================================================*/

size_t rgw_upload_part_info_calc_encode_size(const rgw_upload_part_info_t* part) {
    if (!part) return 0;

    /* 估算大小: etag (64) + 固定字段 */
    size_t size = 64 + 32;
    return size;
}

int rgw_upload_part_info_encode(const rgw_upload_part_info_t* part,
                                uint8_t* buf,
                                size_t buf_size,
                                size_t* actual_size) {
    if (!part || !actual_size) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t needed = rgw_upload_part_info_calc_encode_size(part);
    *actual_size = needed;

    if (!buf) {
        return RGW_OK;
    }

    if (buf_size < needed) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 简单文本格式编码 */
    size_t offset = 0;
    int ret;

    ret = snprintf((char*)buf + offset, buf_size - offset, "part_num=%u\n", part->part_num);
    if (ret > 0) offset += (size_t)ret;

    if (part->etag) {
        ret = snprintf((char*)buf + offset, buf_size - offset, "etag=%s\n", part->etag);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf((char*)buf + offset, buf_size - offset, "size=%llu\n",
                   (unsigned long long)part->size);
    if (ret > 0) offset += (size_t)ret;

    return RGW_OK;
}

int rgw_upload_part_info_decode(const uint8_t* buf,
                                size_t buf_size,
                                rgw_upload_part_info_t* part) {
    if (!buf || !part) {
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

            if (strcmp(key, "part_num") == 0) {
                part->part_num = (uint32_t)atoi(value);
            } else if (strcmp(key, "etag") == 0) {
                free(part->etag);
                part->etag = strdup(value);
            } else if (strcmp(key, "size") == 0) {
                part->size = (uint64_t)strtoull(value, NULL, 10);
            }
        }

        line = next;
    }

    free(buffer);
    return RGW_OK;
}

/**
 * @file rgw_group_serde.c
 * @brief 用户组序列化接口实现
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_group_serde.h"
#include "rgw_errors.h"

int rgw_group_info_init(rgw_group_info_t* info) {
    if (!info) return RGW_ERR_INVALID_ARG;
    memset(info, 0, sizeof(rgw_group_info_t));
    return 0;
}

void rgw_group_info_free_members(rgw_group_info_t* info) {
    if (!info) return;
    if (info->id) { free(info->id); info->id = NULL; }
    if (info->name) { free(info->name); info->name = NULL; }
    if (info->tenant) { free(info->tenant); info->tenant = NULL; }
    if (info->namespace_) { free(info->namespace_); info->namespace_ = NULL; }
    if (info->display_name) { free(info->display_name); info->display_name = NULL; }
    if (info->account_id) { free(info->account_id); info->account_id = NULL; }
}

size_t rgw_group_info_calc_encode_size(const rgw_group_info_t* info) {
    size_t size = 0;
    size += sizeof(size_t);  /* id */
    if (info->id) size += strlen(info->id) + 1;
    size += sizeof(size_t);  /* name */
    if (info->name) size += strlen(info->name) + 1;
    size += sizeof(size_t);  /* tenant */
    if (info->tenant) size += strlen(info->tenant) + 1;
    size += sizeof(size_t);  /* namespace_ */
    if (info->namespace_) size += strlen(info->namespace_) + 1;
    size += sizeof(size_t);  /* display_name */
    if (info->display_name) size += strlen(info->display_name) + 1;
    size += sizeof(size_t);  /* account_id */
    if (info->account_id) size += strlen(info->account_id) + 1;
    return size;
}

int rgw_group_info_encode(const rgw_group_info_t* info, uint8_t* buf, size_t buf_size) {
    if (!info || !buf) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    #define ENCODE_STRING(field) \
        do { \
            size_t len = field ? strlen(field) + 1 : 0; \
            if (offset + sizeof(size_t) + len > buf_size) return RGW_ERR_BUFFER_OVERFLOW; \
            memcpy(buf + offset, &len, sizeof(size_t)); \
            offset += sizeof(size_t); \
            if (len > 0) { \
                memcpy(buf + offset, field, len); \
                offset += len; \
            } \
        } while(0)

    ENCODE_STRING(info->id);
    ENCODE_STRING(info->name);
    ENCODE_STRING(info->tenant);
    ENCODE_STRING(info->namespace_);
    ENCODE_STRING(info->display_name);
    ENCODE_STRING(info->account_id);

    return 0;
}

int rgw_group_info_decode(const uint8_t* buf, size_t buf_size, rgw_group_info_t* info) {
    if (!buf || !info) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    #define DECODE_STRING(field) \
        do { \
            size_t len; \
            if (offset + sizeof(size_t) > buf_size) return RGW_ERR_PARSE_ERROR; \
            memcpy(&len, buf + offset, sizeof(size_t)); \
            offset += sizeof(size_t); \
            if (len > 0) { \
                if (offset + len > buf_size) return RGW_ERR_PARSE_ERROR; \
                field = (char*)malloc(len); \
                if (field) memcpy(field, buf + offset, len); \
                offset += len; \
            } else { \
                field = NULL; \
            } \
        } while(0)

    DECODE_STRING(info->id);
    DECODE_STRING(info->name);
    DECODE_STRING(info->tenant);
    DECODE_STRING(info->namespace_);
    DECODE_STRING(info->display_name);
    DECODE_STRING(info->account_id);

    return 0;
}

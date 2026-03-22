/**
 * @file rgw_oidc_serde.c
 * @brief OIDC 配置序列化接口实现
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_oidc_serde.h"
#include "rgw_errors.h"

int rgw_oidc_provider_info_init(rgw_oidc_provider_info_t* info) {
    if (!info) return RGW_ERR_INVALID_ARG;
    memset(info, 0, sizeof(rgw_oidc_provider_info_t));
    return 0;
}

void rgw_oidc_provider_info_free_members(rgw_oidc_provider_info_t* info) {
    if (!info) return;
    if (info->id) { free(info->id); info->id = NULL; }
    if (info->tenant) { free(info->tenant); info->tenant = NULL; }
    if (info->provider_arn) { free(info->provider_arn); info->provider_arn = NULL; }
    if (info->issuer_url) { free(info->issuer_url); info->issuer_url = NULL; }
    if (info->client_id) { free(info->client_id); info->client_id = NULL; }
    if (info->client_secret) { free(info->client_secret); info->client_secret = NULL; }
}

int rgw_oidc_provider_info_encode_alloc(const rgw_oidc_provider_info_t* info,
                                        uint8_t** buf, size_t* buf_size) {
    if (!info || !buf || !buf_size) return RGW_ERR_INVALID_ARG;

    /* 计算编码大小 */
    size_t total_size = 0;

    #define CALC_SIZE(field) \
        do { \
            total_size += sizeof(size_t); \
            if (field) total_size += strlen(field) + 1; \
        } while(0)

    CALC_SIZE(info->id);
    CALC_SIZE(info->tenant);
    CALC_SIZE(info->provider_arn);
    CALC_SIZE(info->issuer_url);
    CALC_SIZE(info->client_id);
    CALC_SIZE(info->client_secret);
    total_size += sizeof(bool);  /* enabled */

    /* 分配内存 */
    *buf = (uint8_t*)malloc(total_size);
    if (!*buf) return RGW_ERR_OUT_OF_MEMORY;
    *buf_size = total_size;

    /* 编码 */
    size_t offset = 0;

    #define ENCODE_STRING(field) \
        do { \
            size_t len = field ? strlen(field) + 1 : 0; \
            memcpy(*buf + offset, &len, sizeof(size_t)); \
            offset += sizeof(size_t); \
            if (len > 0) { \
                memcpy(*buf + offset, field, len); \
                offset += len; \
            } \
        } while(0)

    ENCODE_STRING(info->id);
    ENCODE_STRING(info->tenant);
    ENCODE_STRING(info->provider_arn);
    ENCODE_STRING(info->issuer_url);
    ENCODE_STRING(info->client_id);
    ENCODE_STRING(info->client_secret);
    memcpy(*buf + offset, &info->enabled, sizeof(bool));

    return 0;
}

int rgw_oidc_provider_info_decode(const uint8_t* buf, size_t buf_size,
                                  rgw_oidc_provider_info_t* info) {
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
    DECODE_STRING(info->tenant);
    DECODE_STRING(info->provider_arn);
    DECODE_STRING(info->issuer_url);
    DECODE_STRING(info->client_id);
    DECODE_STRING(info->client_secret);

    if (offset + sizeof(bool) > buf_size) return RGW_ERR_PARSE_ERROR;
    memcpy(&info->enabled, buf + offset, sizeof(bool));

    return 0;
}

/**
 * @file rgw_sal_daos_serde.c
 * @brief DAOS 数据序列化辅助函数实现
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include "rgw_sal_daos_serde.h"
#include "rgw_sal_errors.h"
#include "rgw_ccommon.h"

/*============================================================================
 * 辅助宏
 *============================================================================*/

#define ENCODE_VALUE(ptr, offset, type, value) do { \
    *(type*)((ptr) + (offset)) = (type)(value); \
    (offset) += sizeof(type); \
} while(0)

#define DECODE_VALUE(ptr, offset, type, dest) do { \
    *(type*)(dest) = *(type*)((ptr) + (offset)); \
    (offset) += sizeof(type); \
} while(0)

#define ENCODE_STRING(ptr, offset, max_size, str) do { \
    size_t _len = (str) ? strlen(str) : 0; \
    if (_len > (max_size) - (offset) - sizeof(size_t)) _len = (max_size) - (offset) - sizeof(size_t); \
    *(size_t*)((ptr) + (offset)) = _len; \
    (offset) += sizeof(size_t); \
    if (_len > 0 && (str)) { \
        memcpy((ptr) + (offset), (str), _len); \
    } \
    (offset) += _len; \
} while(0)

#define DECODE_STRING(ptr, offset, dest, free_fn) do { \
    size_t _len = *(size_t*)((ptr) + (offset)); \
    (offset) += sizeof(size_t); \
    if (free_fn) free(*(dest)); \
    if (_len > 0) { \
        *(dest) = malloc(_len + 1); \
        memcpy(*(dest), (ptr) + (offset), _len); \
        (*(dest))[_len] = '\0'; \
    } else { \
        *(dest) = NULL; \
    } \
    (offset) += _len; \
} while(0)

/*============================================================================
 * 用户信息编码/解码
 *============================================================================*/

int rgw_sal_daos_encode_user(const rgw_sal_daos_user_t* user,
                              uint8_t* buffer, size_t* size) {
    if (!user || !buffer || !size) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = *size;

    /* 编码基本字段 */
    ENCODE_STRING(buffer, offset, max_size, user->user_id);
    ENCODE_STRING(buffer, offset, max_size, user->tenant);
    ENCODE_STRING(buffer, offset, max_size, user->display_name);
    ENCODE_STRING(buffer, offset, max_size, user->email);
    ENCODE_STRING(buffer, offset, max_size, user->access_key);
    ENCODE_STRING(buffer, offset, max_size, user->secret_key);
    ENCODE_STRING(buffer, offset, max_size, user->ns);

    ENCODE_VALUE(buffer, offset, uint32_t, user->user_type);
    ENCODE_VALUE(buffer, offset, int32_t, user->max_buckets);
    ENCODE_VALUE(buffer, offset, uint64_t, user->op_mask);
    ENCODE_VALUE(buffer, offset, time_t, user->mtime);
    ENCODE_STRING(buffer, offset, max_size, user->user_oid);

    *size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_daos_decode_user(rgw_sal_daos_user_t* user,
                              const uint8_t* buffer, size_t size) {
    if (!user || !buffer) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = size;

    /* 解码基本字段 */
    DECODE_STRING(buffer, offset, &user->user_id, 1);
    DECODE_STRING(buffer, offset, &user->tenant, 1);
    DECODE_STRING(buffer, offset, &user->display_name, 1);
    DECODE_STRING(buffer, offset, &user->email, 1);
    DECODE_STRING(buffer, offset, &user->access_key, 1);
    DECODE_STRING(buffer, offset, &user->secret_key, 1);
    DECODE_STRING(buffer, offset, &user->ns, 1);

    DECODE_VALUE(buffer, offset, uint32_t, &user->user_type);
    DECODE_VALUE(buffer, offset, int32_t, &user->max_buckets);
    DECODE_VALUE(buffer, offset, uint64_t, &user->op_mask);
    DECODE_VALUE(buffer, offset, time_t, &user->mtime);

    /* user_oid 是固定大小数组 (64字节) */
    {
        size_t _len = *(size_t*)((buffer) + (offset));
        (offset) += sizeof(size_t);
        if (_len > 0 && _len < sizeof(user->user_oid)) {
            memcpy(user->user_oid, (buffer) + (offset), _len);
            user->user_oid[_len] = '\0';
        }
        (offset) += _len;
    }

    return RGW_SAL_OK;
}

/*============================================================================
 * 桶信息编码/解码
 *============================================================================*/

int rgw_sal_daos_encode_bucket(const rgw_sal_daos_bucket_t* bucket,
                                uint8_t* buffer, size_t* size) {
    if (!bucket || !buffer || !size) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = *size;

    /* 编码基本字段 */
    ENCODE_STRING(buffer, offset, max_size, bucket->name);
    ENCODE_STRING(buffer, offset, max_size, bucket->tenant);
    ENCODE_STRING(buffer, offset, max_size, bucket->marker);
    ENCODE_STRING(buffer, offset, max_size, bucket->bucket_id);
    ENCODE_STRING(buffer, offset, max_size, bucket->owner_id);
    ENCODE_STRING(buffer, offset, max_size, bucket->root_path);
    ENCODE_STRING(buffer, offset, max_size, bucket->tag);

    ENCODE_VALUE(buffer, offset, bool, bucket->loaded);
    ENCODE_VALUE(buffer, offset, bool, bucket->created);
    ENCODE_VALUE(buffer, offset, bool, bucket->deleted);
    ENCODE_VALUE(buffer, offset, time_t, bucket->mtime);
    ENCODE_STRING(buffer, offset, max_size, bucket->bucket_oid);

    *size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_daos_decode_bucket(rgw_sal_daos_bucket_t* bucket,
                                const uint8_t* buffer, size_t size) {
    if (!bucket || !buffer) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = size;

    /* 解码基本字段 */
    DECODE_STRING(buffer, offset, &bucket->name, 1);
    DECODE_STRING(buffer, offset, &bucket->tenant, 1);
    DECODE_STRING(buffer, offset, &bucket->marker, 1);
    DECODE_STRING(buffer, offset, &bucket->bucket_id, 1);
    DECODE_STRING(buffer, offset, &bucket->owner_id, 1);
    DECODE_STRING(buffer, offset, &bucket->root_path, 1);
    DECODE_STRING(buffer, offset, &bucket->tag, 1);

    DECODE_VALUE(buffer, offset, bool, &bucket->loaded);
    DECODE_VALUE(buffer, offset, bool, &bucket->created);
    DECODE_VALUE(buffer, offset, bool, &bucket->deleted);
    DECODE_VALUE(buffer, offset, time_t, &bucket->mtime);

    /* bucket_oid 是固定大小数组 (64字节) */
    {
        size_t _len = *(size_t*)((buffer) + (offset));
        (offset) += sizeof(size_t);
        if (_len > 0 && _len < sizeof(bucket->bucket_oid)) {
            memcpy(bucket->bucket_oid, (buffer) + (offset), _len);
            bucket->bucket_oid[_len] = '\0';
        }
        (offset) += _len;
    }

    return RGW_SAL_OK;
}

/*============================================================================
 * 对象元数据编码/解码
 *============================================================================*/

int rgw_sal_daos_encode_object_meta(const rgw_sal_daos_object_meta_t* meta,
                                    uint8_t* buffer, size_t* size) {
    if (!meta || !buffer || !size) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = *size;

    /* 编码基本字段 */
    ENCODE_STRING(buffer, offset, max_size, meta->name);
    ENCODE_STRING(buffer, offset, max_size, meta->instance);
    ENCODE_STRING(buffer, offset, max_size, meta->owner);
    ENCODE_STRING(buffer, offset, max_size, meta->owner_display_name);

    ENCODE_VALUE(buffer, offset, uint64_t, meta->size);
    ENCODE_VALUE(buffer, offset, uint64_t, meta->accounted_size);
    ENCODE_VALUE(buffer, offset, time_t, meta->mtime);

    /* etag 是固定长度 */
    memcpy(buffer + offset, meta->etag, 64);
    offset += 64;

    ENCODE_VALUE(buffer, offset, uint32_t, meta->flags);
    ENCODE_VALUE(buffer, offset, uint32_t, meta->category);

    *size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_daos_decode_object_meta(rgw_sal_daos_object_meta_t* meta,
                                    const uint8_t* buffer, size_t size) {
    if (!meta || !buffer) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = size;

    /* 解码基本字段 */
    DECODE_STRING(buffer, offset, &meta->name, 1);
    DECODE_STRING(buffer, offset, &meta->instance, 1);
    DECODE_STRING(buffer, offset, &meta->owner, 1);
    DECODE_STRING(buffer, offset, &meta->owner_display_name, 1);

    DECODE_VALUE(buffer, offset, uint64_t, &meta->size);
    DECODE_VALUE(buffer, offset, uint64_t, &meta->accounted_size);
    DECODE_VALUE(buffer, offset, time_t, &meta->mtime);

    /* etag 是固定长度 */
    memcpy(meta->etag, buffer + offset, 64);
    meta->etag[63] = '\0';
    offset += 64;

    DECODE_VALUE(buffer, offset, uint32_t, &meta->flags);
    DECODE_VALUE(buffer, offset, uint32_t, &meta->category);

    return RGW_SAL_OK;
}

/*============================================================================
 * 分片上传元数据编码/解码
 *============================================================================*/

int rgw_sal_daos_encode_upload_meta(const rgw_sal_daos_upload_meta_t* meta,
                                     uint8_t* buffer, size_t* size) {
    if (!meta || !buffer || !size) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = *size;

    ENCODE_STRING(buffer, offset, max_size, meta->dest_placement);
    ENCODE_VALUE(buffer, offset, int32_t, meta->cksum_type);

    *size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_daos_decode_upload_meta(rgw_sal_daos_upload_meta_t* meta,
                                     const uint8_t* buffer, size_t size) {
    if (!meta || !buffer) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = size;

    DECODE_STRING(buffer, offset, &meta->dest_placement, 1);
    DECODE_VALUE(buffer, offset, int32_t, &meta->cksum_type);

    return RGW_SAL_OK;
}

/*============================================================================
 * 分片元数据编码/解码
 *============================================================================*/

int rgw_sal_daos_encode_part_meta(const rgw_sal_daos_part_meta_t* meta,
                                   uint8_t* buffer, size_t* size) {
    if (!meta || !buffer || !size) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = *size;

    ENCODE_VALUE(buffer, offset, uint32_t, meta->part_num);

    /* etag 是固定长度 */
    memcpy(buffer + offset, meta->etag, 64);
    offset += 64;

    ENCODE_VALUE(buffer, offset, uint64_t, meta->size);
    ENCODE_VALUE(buffer, offset, uint64_t, meta->accounted_size);
    ENCODE_VALUE(buffer, offset, time_t, meta->modified);

    *size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_daos_decode_part_meta(rgw_sal_daos_part_meta_t* meta,
                                   const uint8_t* buffer, size_t size) {
    if (!meta || !buffer) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;
    size_t max_size = size;

    DECODE_VALUE(buffer, offset, uint32_t, &meta->part_num);

    /* etag 是固定长度 */
    memcpy(meta->etag, buffer + offset, 64);
    meta->etag[63] = '\0';
    offset += 64;

    DECODE_VALUE(buffer, offset, uint64_t, &meta->size);
    DECODE_VALUE(buffer, offset, uint64_t, &meta->accounted_size);
    DECODE_VALUE(buffer, offset, time_t, &meta->modified);

    return RGW_SAL_OK;
}

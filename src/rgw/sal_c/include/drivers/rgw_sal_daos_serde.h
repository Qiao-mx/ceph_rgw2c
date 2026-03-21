/**
 * @file rgw_sal_daos_serde.h
 * @brief DAOS 数据序列化辅助函数
 *
 * 提供用户、桶、对象等数据的编码/解码功能，
 * 将内部结构转换为 DAOS 存储的格式。
 */
#ifndef RGW_SAL_DAOS_SERDE_H
#define RGW_SAL_DAOS_SERDE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "rgw_sal.h"
#include "rgw_sal_daos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 用户信息编码/解码
 *============================================================================*/

/**
 * @brief 编码用户信息
 *
 * @param user 用户实现
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际编码长度
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_encode_user(const rgw_sal_daos_user_t* user,
                              uint8_t* buffer, size_t* size);

/**
 * @brief 解码用户信息
 *
 * @param[out] user 用户实现
 * @param buffer 输入缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_decode_user(rgw_sal_daos_user_t* user,
                              const uint8_t* buffer, size_t size);

/*============================================================================
 * 桶信息编码/解码
 *============================================================================*/

/**
 * @brief 编码桶信息
 *
 * @param bucket 桶实现
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际编码长度
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_encode_bucket(const rgw_sal_daos_bucket_t* bucket,
                                uint8_t* buffer, size_t* size);

/**
 * @brief 解码桶信息
 *
 * @param[out] bucket 桶实现
 * @param buffer 输入缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_decode_bucket(rgw_sal_daos_bucket_t* bucket,
                                const uint8_t* buffer, size_t size);

/*============================================================================
 * 对象元数据编码/解码
 *============================================================================*/

/**
 * @brief 对象元数据结构
 */
typedef struct rgw_sal_daos_object_meta {
    char* name;
    char* instance;
    char* owner;
    char* owner_display_name;
    uint64_t size;
    uint64_t accounted_size;
    time_t mtime;
    char etag[64];
    uint32_t flags;
    uint32_t category;
} rgw_sal_daos_object_meta_t;

/**
 * @brief 编码对象元数据
 *
 * @param meta 对象元数据
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际编码长度
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_encode_object_meta(const rgw_sal_daos_object_meta_t* meta,
                                    uint8_t* buffer, size_t* size);

/**
 * @brief 解码对象元数据
 *
 * @param[out] meta 对象元数据
 * @param buffer 输入缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_decode_object_meta(rgw_sal_daos_object_meta_t* meta,
                                    const uint8_t* buffer, size_t size);

/*============================================================================
 * 分片上传信息编码/解码
 *============================================================================*/

/**
 * @brief 分片上传元数据结构
 */
typedef struct rgw_sal_daos_upload_meta {
    char* dest_placement;
    int32_t cksum_type;
} rgw_sal_daos_upload_meta_t;

/**
 * @brief 编码分片上传元数据
 *
 * @param meta 分片上传元数据
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际编码长度
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_encode_upload_meta(const rgw_sal_daos_upload_meta_t* meta,
                                     uint8_t* buffer, size_t* size);

/**
 * @brief 解码分片上传元数据
 *
 * @param[out] meta 分片上传元数据
 * @param buffer 输入缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_decode_upload_meta(rgw_sal_daos_upload_meta_t* meta,
                                     const uint8_t* buffer, size_t size);

/*============================================================================
 * 分片信息编码/解码
 *============================================================================*/

/**
 * @brief 分片元数据结构
 */
typedef struct rgw_sal_daos_part_meta {
    uint32_t part_num;
    char etag[64];
    uint64_t size;
    uint64_t accounted_size;
    time_t modified;
} rgw_sal_daos_part_meta_t;

/**
 * @brief 编码分片元数据
 *
 * @param meta 分片元数据
 * @param[out] buffer 输出缓冲区
 * @param[in,out] size 输入缓冲区大小，输出实际编码长度
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_encode_part_meta(const rgw_sal_daos_part_meta_t* meta,
                                   uint8_t* buffer, size_t* size);

/**
 * @brief 解码分片元数据
 *
 * @param[out] meta 分片元数据
 * @param buffer 输入缓冲区
 * @param size 缓冲区大小
 * @return 0 成功，负值失败
 */
int rgw_sal_daos_decode_part_meta(rgw_sal_daos_part_meta_t* meta,
                                   const uint8_t* buffer, size_t size);

/*============================================================================
 * DS3 API 绑定
 *============================================================================*/

/**
 * @brief DS3 用户信息绑定
 */
typedef struct rgw_sal_daos_ds3_user {
    const char* name;
    const char* email;
    const char** access_ids;
    size_t access_ids_nr;
    char* encoded;
    size_t encoded_length;
} rgw_sal_daos_ds3_user_t;

/**
 * @brief DS3 桶信息绑定
 */
typedef struct rgw_sal_daos_ds3_bucket {
    char name[256];
    char* encoded;
    size_t encoded_length;
} rgw_sal_daos_ds3_bucket_t;

/**
 * @brief DS3 对象信息绑定
 */
typedef struct rgw_sal_daos_ds3_object {
    char* encoded;
    size_t encoded_length;
} rgw_sal_daos_ds3_object_t;

/**
 * @brief DS3 分片上传信息绑定
 */
typedef struct rgw_sal_daos_ds3_upload {
    char upload_id[256];
    char key[512];
    char* encoded;
    size_t encoded_length;
} rgw_sal_daos_ds3_upload_t;

/**
 * @brief DS3 分片信息绑定
 */
typedef struct rgw_sal_daos_ds3_part {
    uint32_t part_num;
    char* encoded;
    size_t encoded_length;
} rgw_sal_daos_ds3_part_t;

#ifdef __cplusplus
}
#endif

#endif /* RGW_SAL_DAOS_SERDE_H */

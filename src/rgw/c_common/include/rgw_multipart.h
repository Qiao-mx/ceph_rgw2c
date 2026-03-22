/**
 * @file rgw_multipart.h
 * @brief 多部分上传序列化接口 (STUB)
 *
 * 多部分上传信息的序列化/反序列化接口。
 * 此为占位符实现，完整功能需要原始 C++ 代码。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 多部分上传类型
 *============================================================================*/

/**
 * @brief 多部分上传信息
 */
typedef struct {
    char* bucket;                  /**< 桶名 */
    char* object;                 /**< 对象名 */
    char* upload_id;              /**< 上传 ID */
    uint64_t size;                 /**< 总大小 */
    uint32_t part_count;          /**< 分片数量 */
} rgw_multipart_info_t;

/**
 * @brief 多部分分片信息
 */
typedef struct {
    uint32_t part_num;             /**< 分片编号 */
    char* etag;                   /**< ETag */
    uint64_t size;                /**< 分片大小 */
} rgw_multipart_part_t;

/**
 * @brief 多部分上传信息 (别名，用于兼容)
 */
typedef rgw_multipart_info_t rgw_multipart_upload_info_t;

/**
 * @brief 分片信息 (别名，用于兼容)
 */
typedef rgw_multipart_part_t rgw_upload_part_info_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 创建多部分上传信息
 */
rgw_multipart_info_t* rgw_multipart_info_create(void);

/**
 * @brief 销毁多部分上传信息
 */
void rgw_multipart_info_destroy(rgw_multipart_info_t* info);

/**
 * @brief 添加分片信息
 */
int rgw_multipart_info_add_part(rgw_multipart_info_t* info,
                                const rgw_multipart_part_t* part);

/**
 * @brief 计算编码大小
 */
size_t rgw_multipart_info_calc_encode_size(const rgw_multipart_info_t* info);

/**
 * @brief 编码多部分上传信息
 */
int rgw_multipart_info_encode(const rgw_multipart_info_t* info,
                               uint8_t* buf,
                               size_t buf_size);

/**
 * @brief 解码多部分上传信息
 */
int rgw_multipart_info_decode(const uint8_t* buf,
                               size_t buf_size,
                               rgw_multipart_info_t* info);

/*============================================================================
 * 多部分上传信息别名函数 (兼容性)
 *============================================================================*/

/**
 * @brief 分配并编码多部分上传信息 (兼容性函数)
 *
 * @param info 输入信息
 * @param buf_out 输出缓冲区指针（由函数分配）
 * @param buf_len_out 输出缓冲区长度指针
 * @return 成功返回 0，失败返回负数错误码
 */
int rgw_multipart_upload_info_encode_alloc(const rgw_multipart_info_t* info,
                                            uint8_t** buf_out,
                                            size_t* buf_len_out);

/**
 * @brief 解码多部分上传信息 (兼容性函数)
 */
int rgw_multipart_upload_info_decode(const uint8_t* buf,
                                      size_t buf_size,
                                      rgw_multipart_info_t* info);

/*============================================================================
 * 分片信息序列化函数
 *============================================================================*/

/**
 * @brief 计算分片信息编码大小
 */
size_t rgw_upload_part_info_calc_encode_size(const rgw_upload_part_info_t* part);

/**
 * @brief 编码分片信息
 */
int rgw_upload_part_info_encode(const rgw_upload_part_info_t* part,
                                uint8_t* buf,
                                size_t buf_size,
                                size_t* actual_size);

/**
 * @brief 解码分片信息
 */
int rgw_upload_part_info_decode(const uint8_t* buf,
                                size_t buf_size,
                                rgw_upload_part_info_t* part);

#ifdef __cplusplus
}
#endif

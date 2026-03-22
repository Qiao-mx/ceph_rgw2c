/**
 * @file rgw_account_serde.h
 * @brief 账户信息序列化接口 (STUB)
 *
 * 账户信息的序列化/反序列化接口。
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
 * 账户类型
 *============================================================================*/

/**
 * @brief 账户信息
 */
typedef struct {
    char* account_id;              /**< 账户 ID */
    char* email;                   /**< 邮箱 */
    char* display_name;            /**< 显示名称 */
    bool suspended;                 /**< 是否暂停 */
} rgw_account_info_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 创建账户信息
 */
rgw_account_info_t* rgw_account_info_create(void);

/**
 * @brief 销毁账户信息
 */
void rgw_account_info_destroy(rgw_account_info_t* info);

/**
 * @brief 计算编码大小
 */
size_t rgw_account_info_calc_encode_size(const rgw_account_info_t* info);

/**
 * @brief 编码账户信息
 */
int rgw_account_info_encode(const rgw_account_info_t* info,
                           uint8_t* buf,
                           size_t buf_size);

/**
 * @brief 解码账户信息
 */
int rgw_account_info_decode(const uint8_t* buf,
                            size_t buf_size,
                            rgw_account_info_t* info);

/**
 * @brief 释放账户信息成员
 *
 * 释放账户信息中动态分配的成员，但保留结构本身。
 *
 * @param info 账户信息
 */
void rgw_account_info_free_members(rgw_account_info_t* info);

#ifdef __cplusplus
}
#endif

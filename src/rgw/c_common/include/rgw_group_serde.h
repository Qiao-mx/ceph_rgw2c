/**
 * @file rgw_group_serde.h
 * @brief 用户组序列化接口
 *
 * 定义用户组信息的序列化/反序列化接口。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 组信息
 */
typedef struct {
    char* id;           /**< 组 ID */
    char* name;         /**< 组名 */
    char* tenant;       /**< 租户 */
    char* namespace_;   /**< 命名空间 */
    char* display_name; /**< 显示名 */
    char* account_id;   /**< 账户 ID */
} rgw_group_info_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 初始化组信息
 */
int rgw_group_info_init(rgw_group_info_t* info);

/**
 * @brief 释放组信息成员
 */
void rgw_group_info_free_members(rgw_group_info_t* info);

/**
 * @brief 计算组信息编码大小
 */
size_t rgw_group_info_calc_encode_size(const rgw_group_info_t* info);

/**
 * @brief 编码组信息
 */
int rgw_group_info_encode(const rgw_group_info_t* info, uint8_t* buf, size_t buf_size);

/**
 * @brief 解码组信息
 */
int rgw_group_info_decode(const uint8_t* buf, size_t buf_size, rgw_group_info_t* info);

#ifdef __cplusplus
}
#endif

/**
 * @file rgw_oidc_serde.h
 * @brief OIDC 配置序列化接口
 *
 * 定义 OIDC 提供商配置的序列化/反序列化接口。
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
 * @brief OIDC 提供商配置
 */
typedef struct {
    char* provider_arn;          /**< 提供商 ARN */
    char* client_id;            /**< 客户端 ID */
    char* issuer;               /**< 发行者 URL */
    bool enabled;               /**< 是否启用 */
} rgw_oidc_config_t;

/**
 * @brief OIDC 提供商信息
 */
typedef struct {
    char* id;                   /**< 提供商 ID */
    char* tenant;              /**< 租户 */
    char* provider_arn;         /**< 提供商 ARN */
    char* issuer_url;           /**< 发行者 URL */
    char* client_id;           /**< 客户端 ID */
    char* client_secret;        /**< 客户端密钥 */
    bool enabled;               /**< 是否启用 */
} rgw_oidc_provider_info_t;

/**
 * @brief OIDC 用户信息
 */
typedef struct {
    char* sub;                  /**< Subject */
    char* iss;                  /**< Issuer */
    char* aud;                  /**< Audience */
} rgw_oidc_user_info_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 初始化 OIDC 提供商信息
 */
int rgw_oidc_provider_info_init(rgw_oidc_provider_info_t* info);

/**
 * @brief 释放 OIDC 提供商信息成员
 */
void rgw_oidc_provider_info_free_members(rgw_oidc_provider_info_t* info);

/**
 * @brief 编码 OIDC 提供商信息
 */
int rgw_oidc_provider_info_encode_alloc(const rgw_oidc_provider_info_t* info,
                                        uint8_t** buf, size_t* buf_size);

/**
 * @brief 解码 OIDC 提供商信息
 */
int rgw_oidc_provider_info_decode(const uint8_t* buf, size_t buf_size,
                                  rgw_oidc_provider_info_t* info);

#ifdef __cplusplus
}
#endif

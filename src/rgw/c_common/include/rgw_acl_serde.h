/**
 * @file rgw_acl_serde.h
 * @brief ACL 序列化接口
 *
 * 定义 ACL 信息的序列化/反序列化接口，
 * 用于将 ACL 信息存储到 RADOS OMAP 或从 OMAP 加载。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 常量定义
 *============================================================================*/

/** ACL 缓冲区初始大小 */
#define RGW_ACL_BUF_INIT_SIZE              4096

/** ACL 缓冲区最大大小 (防止恶意数据，10MB) */
#define RGW_ACL_BUF_MAX_SIZE               (10 * 1024 * 1024)

/** ACL 版本号 */
#define RGW_ACL_ENCODE_VERSION              1

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief ACL grant 类型
 */
typedef enum {
    RGW_ACL_GRANT_TYPE_USER = 0,      /**< 用户授权 */
    RGW_ACL_GRANT_TYPE_GROUP = 1,     /**< 组授权 */
    RGW_ACL_GRANT_TYPE_EMAIL = 2,     /**< Email 授权 */
    RGW_ACL_GRANT_TYPE_DOMAIN = 3,    /**< 域授权 */
    RGW_ACL_GRANT_TYPE_CANON_ID = 4   /**< Canonical ID 授权 */
} rgw_acl_grant_type_t;

/**
 * @brief ACL 权限
 */
typedef enum {
    RGW_ACL_PERM_NONE = 0,
    RGW_ACL_PERM_READ = (1 << 0),      /**< 读权限 */
    RGW_ACL_PERM_WRITE = (1 << 1),      /**< 写权限 */
    RGW_ACL_PERM_READ_ACP = (1 << 2),  /**< 读取 ACL 权限 */
    RGW_ACL_PERM_WRITE_ACP = (1 << 3), /**< 写入 ACL 权限 */
    RGW_ACL_PERM_FULL_CONTROL = (RGW_ACL_PERM_READ | RGW_ACL_PERM_WRITE | \
                                  RGW_ACL_PERM_READ_ACP | RGW_ACL_PERM_WRITE_ACP)
} rgw_acl_perm_t;

/**
 * @brief ACL 组类型
 */
typedef enum {
    RGW_ACL_GROUP_ALL_USERS = 0,      /**< 所有用户 (AuthenticatedUsers) */
    RGW_ACL_GROUP_AUTHENTICATED_USERS = 1, /**< 已认证用户 */
    RGW_ACL_GROUP_ANONYMOUS_USERS = 2  /**< 匿名用户 */
} rgw_acl_group_type_t;

/**
 * @brief ACL Grant
 *
 * 表示单个 ACL 授权条目。
 */
typedef struct {
    rgw_acl_grant_type_t type;         /**< 授权类型 */
    rgw_acl_perm_t perm;              /**< 权限 */
    char* id;                         /**< 授权目标 ID (用户/组/email/domain) */
    char* display_name;               /**< 显示名称 */
    int32_t group_type;              /**< 组类型 (当 type=GROUP 时) */
    char* url_group;                  /**< URL Referer 组 (当 type=REFERER 时) */
} rgw_acl_grant_t;

/**
 * @brief ACL Owner
 *
 * 表示 ACL 的所有者。
 */
typedef struct {
    char* id;                         /**< 所有者 ID */
    char* display_name;               /**< 显示名称 */
} rgw_acl_owner_t;

/**
 * @brief ACL 信息
 *
 * 完整的 ACL 结构。
 */
typedef struct {
    rgw_acl_owner_t owner;           /**< ACL 所有者 */
    rgw_acl_grant_t* grants;         /**< Grant 数组 */
    size_t num_grants;               /**< Grant 数量 */
    size_t grants_capacity;          /**< Grant 数组容量 */
} rgw_acl_info_t;

/*============================================================================
 * 函数声明 - 生命周期管理
 *============================================================================*/

/**
 * @brief 创建 ACL 信息
 *
 * @return 新创建的 ACL 信息，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_acl_info_destroy() 释放
 */
rgw_acl_info_t* rgw_acl_info_create(void);

/**
 * @brief 销毁 ACL 信息
 *
 * @param acl ACL 信息
 */
void rgw_acl_info_destroy(rgw_acl_info_t* acl);

/**
 * @brief 深拷贝 ACL 信息
 *
 * @param src 源 ACL
 * @param dst 目标 ACL
 *
 * @return 执行结果
 */
int rgw_acl_info_deep_copy(const rgw_acl_info_t* src, rgw_acl_info_t* dst);

/*============================================================================
 * 函数声明 - Grant 管理
 *============================================================================*/

/**
 * @brief 添加 Grant 到 ACL
 *
 * @param acl ACL 信息
 * @param grant Grant
 *
 * @return 执行结果
 */
int rgw_acl_info_add_grant(rgw_acl_info_t* acl, const rgw_acl_grant_t* grant);

/**
 * @brief 创建默认 ACL
 *
 * @param acl ACL 信息
 * @param owner_id 所有者 ID
 * @param owner_name 所有者显示名称
 *
 * @return 执行结果
 */
int rgw_acl_info_create_default(rgw_acl_info_t* acl,
                                 const char* owner_id,
                                 const char* owner_name);

/*============================================================================
 * 函数声明 - 序列化/反序列化
 *============================================================================*/

/**
 * @brief 计算 ACL 编码后的大小
 *
 * @param acl ACL 信息
 *
 * @return 所需缓冲区大小，失败返回 0
 */
size_t rgw_acl_calc_encode_size(const rgw_acl_info_t* acl);

/**
 * @brief 编码 ACL 到缓冲区
 *
 * @param acl ACL 信息
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 编码后的数据长度，失败返回负值
 */
int rgw_acl_encode(const rgw_acl_info_t* acl, uint8_t* buf, size_t buf_size);

/**
 * @brief 动态编码 ACL
 *
 * @param acl ACL 信息
 * @param out_buf 输出参数，返回分配的缓冲区
 * @param out_len 输出参数，返回编码后的长度
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 free() 释放 *out_buf
 */
int rgw_acl_encode_alloc(const rgw_acl_info_t* acl, uint8_t** out_buf, size_t* out_len);

/**
 * @brief 从缓冲区解码 ACL
 *
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param acl 输出 ACL 信息
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 rgw_acl_info_destroy() 释放 acl
 */
int rgw_acl_decode(const uint8_t* buf, size_t buf_len, rgw_acl_info_t* acl);

/*============================================================================
 * 函数声明 - JSON 序列化
 *============================================================================*/

/**
 * @brief 将 ACL 编码为 JSON 字符串
 *
 * @param acl ACL 信息
 * @param json_str 输出参数，返回 JSON 字符串
 * @param json_len 输出参数，返回 JSON 字符串长度
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 free() 释放 *json_str
 */
int rgw_acl_to_json(const rgw_acl_info_t* acl, char** json_str, size_t* json_len);

/**
 * @brief 从 JSON 字符串解码 ACL
 *
 * @param json_str JSON 字符串
 * @param json_len JSON 字符串长度
 * @param acl 输出 ACL 信息
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 rgw_acl_info_destroy() 释放 acl
 */
int rgw_acl_from_json(const char* json_str, size_t json_len, rgw_acl_info_t* acl);

/*============================================================================
 * 函数声明 - OMAP 键
 *============================================================================*/

/**
 * @brief 构建桶 ACL OMAP 键
 *
 * @param bucket_name 桶名称
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_acl_make_bucket_omap_key(const char* bucket_name, char* buf, size_t buf_size);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 检查 ACL 是否为空
 *
 * @param acl ACL 信息
 *
 * @return 是否为空
 */
bool rgw_acl_is_empty(const rgw_acl_info_t* acl);

/**
 * @brief 比较两个 ACL
 *
 * @param a ACL A
 * @param b ACL B
 *
 * @return 是否相等
 */
bool rgw_acl_equal(const rgw_acl_info_t* a, const rgw_acl_info_t* b);

#ifdef __cplusplus
}
#endif

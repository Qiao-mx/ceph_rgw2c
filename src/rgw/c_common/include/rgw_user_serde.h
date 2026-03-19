/**
 * @file rgw_user_serde.h
 * @brief 用户信息序列化接口
 *
 * 定义用户信息的序列化/反序列化接口，
 * 用于将用户信息存储到 RADOS OMAP 或从 OMAP 加载。
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

/** 编码版本号 - 与原 Ceph 保持一致 */
#define RGW_USER_INFO_ENCODE_VERSION_START   23
#define RGW_USER_INFO_ENCODE_VERSION_END     9

/** 缓冲区初始大小 */
#define RGW_USER_INFO_BUF_INIT_SIZE          4096

/** 缓冲区最大大小 (防止恶意数据，10MB) */
#define RGW_USER_INFO_BUF_MAX_SIZE           (10 * 1024 * 1024)

/** 用户 ID 最大长度 */
#define RGW_USER_INFO_MAX_ID_LEN             256

/** 显示名称最大长度 */
#define RGW_USER_INFO_MAX_DISPLAY_NAME_LEN   256

/** Email 最大长度 */
#define RGW_USER_INFO_MAX_EMAIL_LEN          256

/** Access Key 最大长度 */
#define RGW_USER_INFO_MAX_ACCESS_KEY_LEN     256

/** Secret Key 最大长度 */
#define RGW_USER_INFO_MAX_SECRET_KEY_LEN     256

/** 用户类型 */
typedef enum {
    RGW_USER_TYPE_USER = 0,      /**< 普通用户 */
    RGW_USER_TYPE_SUBUSER = 1,  /**< 子用户 */
    RGW_USER_TYPE_ACCOUNT = 2   /**< 账户 (多租户) */
} rgw_user_type_t;

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 用户标识
 *
 * 用户的唯一标识。
 */
typedef struct {
    char* tenant;       /**< 租户 */
    char* id;          /**< 用户 ID */
    char* ns;           /**< 命名空间 */
    uint32_t type;      /**< 用户类型 */
} rgw_user_id_t;

/**
 * @brief 访问密钥
 *
 * 用于认证的 Access Key 和 Secret Key。
 */
typedef struct {
    char* id;           /**< Access Key ID */
    char* key;          /**< Secret Access Key */
    bool active;         /**< 是否激活 */
} rgw_access_key_t;

/**
 * @brief 子用户
 *
 * Swift 子用户信息。
 */
typedef struct {
    char* id;           /**< 子用户 ID */
    char* name;         /**< 子用户名称 */
    char* perm;         /**< 权限 */
} rgw_subuser_t;

/**
 * @brief 用户配额信息
 */
typedef struct {
    int64_t max_size;            /**< 最大存储大小 (-1 表示无限制) */
    int64_t max_objects;         /**< 最大对象数 (-1 表示无限制) */
    bool enabled;                /**< 是否启用配额 */
    bool check_on_raw;           /**< 是否检查原始大小 */
} rgw_quota_info_t;

/**
 * @brief 对象版本
 */
typedef struct {
    uint64_t ver;                /**< 版本号 */
    uint32_t epoch;              /**< 时代 */
    bool exists;                  /**< 是否存在 */
} rgw_obj_version_t;

/**
 * @brief 对象版本跟踪器
 */
typedef struct {
    rgw_obj_version_t read_version;   /**< 读取版本 */
    rgw_obj_version_t write_version;   /**< 写入版本 */
    bool ignore_dirty;                /**< 是否忽略脏数据 */
} rgw_obj_version_tracker_t;

/**
 * @brief 用户信息
 *
 * 完整的用户信息结构。
 */
typedef struct {
    /** 基本信息 */
    rgw_user_id_t user_id;
    char* display_name;              /**< 显示名称 */
    char* email;                     /**< 电子邮箱 */

    /** 用户类型和权限 */
    uint32_t user_type;              /**< 用户类型 */
    uint32_t permissions;            /**< 权限标志 */

    /** 限制 */
    int32_t max_buckets;             /**< 最大桶数量 (-1 表示无限制) */

    /** 配额 */
    rgw_quota_info_t quota;          /**< 存储配额 */

    /** 时间戳 */
    int64_t temp_url_key[2];         /**< 临时 URL 密钥 */
    int64_t mtime;                   /**< 修改时间 */

    /** 额外标志 */
    uint32_t user_stats_quota;      /**< 用户统计配额标志 */
    uint32_t stats_quota;           /**< 统计配额标志 */
    bool suspended;                  /**< 是否暂停 */
    bool system;                     /**< 是否系统用户 */

    /** MFA */
    char* mfa_ids;                  /**< MFA 设备 ID 列表 */

    /** 版本跟踪 */
    rgw_obj_version_tracker_t objv_tracker;
} rgw_user_info_t;

/**
 * @brief 用户访问密钥映射
 *
 * 用于索引用户的 access key。
 */
typedef struct {
    char* key;           /**< Access Key ID */
    char* user_id;       /**< 关联的用户 ID */
} rgw_user_access_key_index_t;

/**
 * @brief 用户邮箱映射
 *
 * 用于索引用户的 email。
 */
typedef struct {
    char* email;         /**< Email 地址 */
    char* user_id;       /**< 关联的用户 ID */
} rgw_user_email_index_t;

/*============================================================================
 * 函数声明 - 序列化/反序列化
 *============================================================================*/

/**
 * @brief 计算编码后的大小
 *
 * 计算用户信息编码后所需的缓冲区大小。
 *
 * @param info 用户信息
 *
 * @return 所需缓冲区大小，失败返回 0
 */
size_t rgw_user_info_calc_encode_size(const rgw_user_info_t* info);

/**
 * @brief 编码用户信息到缓冲区
 *
 * 将用户信息编码为二进制格式。
 *
 * @param info 用户信息
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 编码后的数据长度，失败返回负值
 * @retval -EINVAL 参数无效
 * @retval -ERANGE 缓冲区太小
 */
int rgw_user_info_encode(const rgw_user_info_t* info,
                         uint8_t* buf,
                         size_t buf_size);

/**
 * @brief 动态编码用户信息
 *
 * 分配内存并编码用户信息。
 *
 * @param info 用户信息
 * @param out_buf 输出参数，返回分配的缓冲区（需要调用 free 释放）
 * @param out_len 输出参数，返回编码后的长度
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 free() 释放 *out_buf
 */
int rgw_user_info_encode_alloc(const rgw_user_info_t* info,
                               uint8_t** out_buf,
                               size_t* out_len);

/**
 * @brief 从缓冲区解码用户信息
 *
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param info 输出用户信息
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效或数据格式错误
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 rgw_user_info_free() 释放 info 中的动态成员
 */
int rgw_user_info_decode(const uint8_t* buf,
                         size_t buf_len,
                         rgw_user_info_t* info);

/**
 * @brief 释放用户信息动态内存
 *
 * 释放用户信息结构体中的动态分配成员。
 * 不会释放 info 本身。
 *
 * @param info 用户信息
 */
void rgw_user_info_free_members(rgw_user_info_t* info);

/**
 * @brief 初始化用户信息
 *
 * 初始化用户信息结构体，设置默认值。
 *
 * @param info 用户信息
 */
void rgw_user_info_init(rgw_user_info_t* info);

/**
 * @brief 销毁用户信息
 *
 * 释放用户信息占用的所有内存。
 *
 * @param info 用户信息
 */
void rgw_user_info_destroy(rgw_user_info_t* info);

/*============================================================================
 * 函数声明 - 索引操作
 *============================================================================*/

/**
 * @brief 构建用户 OMAP 键名
 *
 * 用户主对象的 OMAP 键名格式: {tenant}:{uid}
 *
 * @param user_id 用户 ID 结构
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ERANGE 缓冲区太小
 */
int rgw_user_info_make_omap_key(const rgw_user_id_t* user_id,
                                 char* buf,
                                 size_t buf_size);

/**
 * @brief 构建 access key 索引键
 *
 * Access key 索引键格式: {access_key}
 *
 * @param access_key Access Key ID
 *
 * @return 索引键，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_user_access_key_index_make_key(const char* access_key);

/**
 * @brief 构建 email 索引键
 *
 * Email 索引键格式: {email}
 *
 * @param email Email 地址
 *
 * @return 索引键，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_user_email_index_make_key(const char* email);

/**
 * @brief 编码 access key 索引值
 *
 * Access key 索引值: {tenant}:{uid}
 *
 * @param user_id 用户 ID 结构
 *
 * @return 编码后的值，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_user_access_key_index_encode_value(const rgw_user_id_t* user_id);

/**
 * @brief 编码 email 索引值
 *
 * Email 索引值: {tenant}:{uid}
 *
 * @param user_id 用户 ID 结构
 *
 * @return 编码后的值，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_user_email_index_encode_value(const rgw_user_id_t* user_id);

/**
 * @brief 解码索引键获取用户 ID
 *
 * 从 access key 或 email 索引值中提取用户 ID。
 *
 * @param value 索引值 ({tenant}:{uid})
 * @param user_id 输出参数，返回用户 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 */
int rgw_user_id_decode_from_index(const char* value,
                                   rgw_user_id_t* user_id);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 深拷贝用户信息
 *
 * 复制用户信息的完整副本。
 *
 * @param src 源用户信息
 * @param dst 目标用户信息
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 */
int rgw_user_info_deep_copy(const rgw_user_info_t* src,
                             rgw_user_info_t* dst);

/**
 * @brief 比较两个用户 ID
 *
 * @param a 用户 ID A
 * @param b 用户 ID B
 *
 * @return 是否相等
 * @retval true 相等
 * @retval false 不相等
 */
bool rgw_user_id_equal(const rgw_user_id_t* a, const rgw_user_id_t* b);

/**
 * @brief 获取用户 ID 字符串表示
 *
 * 格式: {tenant}:{uid}
 *
 * @param user_id 用户 ID
 *
 * @return 字符串表示，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_user_id_to_string(const rgw_user_id_t* user_id);

/**
 * @brief 解析用户 ID 字符串
 *
 * 解析 {tenant}:{uid} 格式的字符串。
 *
 * @param str 字符串
 * @param user_id 输出参数，返回用户 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效或格式错误
 * @retval -ENOMEM 内存分配失败
 */
int rgw_user_id_parse(const char* str, rgw_user_id_t* user_id);

/*============================================================================
 * 函数声明 - 初始化函数
 *============================================================================*/

/**
 * @brief 创建新的用户信息
 *
 * 分配并初始化一个新的用户信息结构。
 *
 * @param user_id 用户 ID（会被复制）
 * @param display_name 显示名称（会被复制）
 *
 * @return 新创建的用户信息，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_user_info_destroy() 释放
 */
rgw_user_info_t* rgw_user_info_create(const char* user_id,
                                        const char* display_name);

/**
 * @brief 创建用户 ID
 *
 * @param tenant 租户
 * @param id 用户 ID
 * @param ns 命名空间
 *
 * @return 新创建的用户 ID，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的结构，然后释放其成员
 */
rgw_user_id_t* rgw_user_id_create(const char* tenant,
                                    const char* id,
                                    const char* ns);

/**
 * @brief 销毁用户 ID
 *
 * @param user_id 用户 ID
 */
void rgw_user_id_destroy(rgw_user_id_t* user_id);

#ifdef __cplusplus
}
#endif

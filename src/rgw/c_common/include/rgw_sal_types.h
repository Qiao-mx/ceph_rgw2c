#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 核心类型定义 (与 rgw_sal.h 保持一致)
 *============================================================================*/

/* 用户 ID 结构 */
typedef struct {
    char* id;         /**< 用户 ID */
    char* tenant;     /**< 租户 */
    char* swift_name; /**< Swift 用户名 */
    char* swift_subuser; /**< Swift 子用户 */
} rgw_sal_user_id_t;

/* 桶标识结构 */
typedef struct {
    char* name;         /**< 桶名称 */
    char* tenant;       /**< 租户 */
    char* marker;       /**< 桶标记 */
    char* bucket_id;    /**< 桶 ID */
} rgw_sal_bucket_id_t;

/* 对象键结构 */
typedef struct {
    char* name;       /**< 对象名称 */
    char* instance;   /**< 对象实例版本 */
    bool is_null;     /**< 是否为 null 版本 */
    bool is_current;  /**< 是否为当前版本 */
} rgw_sal_obj_key_t;

/* 配额信息 */
typedef struct { 
    bool enabled;                 /**< 是否启用 */
    bool check_on_raw;            /**< 是否检查原始大小 */
    uint64_t max_size;            /**< 最大大小 */
    uint64_t max_size_kb;         /**< 最大大小 (KB) */
    uint64_t max_objects;         /**< 最大对象数 */
    uint64_t quota_bytes;         /**< 字节配额 */
    uint64_t quota_max_objects;   /**< 最大对象数配额 */
} rgw_sal_quota_info_t;

/**
 * @brief 使用统计信息
 */
typedef struct {
    uint64_t bytes_sent;       /**< 发送字节数 */
    uint64_t bytes_received;   /**< 接收字节数 */
    uint64_t ops;              /**< 操作数 */
    uint64_t successful_ops;   /**< 成功操作数 */
} rgw_sal_usage_info_t;

/* 版本信息 */
typedef struct { 
    uint32_t epoch; 
    char* ver; 
    bool committed; 
} rgw_sal_obj_version_t;

/* 版本跟踪器 */
typedef struct { 
    rgw_sal_obj_version_t read_version; 
    rgw_sal_obj_version_t write_version; 
    char* obj_tag; 
    char* instance_tag; 
} rgw_sal_obj_version_tracker_t;

/* 用户权限 */
typedef struct { 
    char* caps; 
} rgw_sal_user_caps_t;

/**
 * @brief 用户组成员关系
 */
typedef struct {
    char* group_id;   /**< 组 ID */
    char* tenant;    /**< 租户 (用作 group_name) */
} rgw_sal_user_group_t;

/**
 * @brief 用户组列表
 */
typedef struct {
    rgw_sal_user_group_t* groups;
    size_t count;
    size_t capacity;
} rgw_sal_user_groups_t;

/*============================================================================
 * 用户 ID 函数
 *============================================================================*/

rgw_sal_user_id_t* rgw_sal_user_id_create(void);
void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid);

/*============================================================================
 * 桶 ID 函数
 *============================================================================*/

rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void);
void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid);

/*============================================================================
 * 对象键函数
 *============================================================================*/

rgw_sal_obj_key_t* rgw_sal_obj_key_create(void);
void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key);

/*============================================================================
 * 用户组函数
 *============================================================================*/

rgw_sal_user_groups_t* rgw_sal_user_groups_create(void);
void rgw_sal_user_groups_destroy(rgw_sal_user_groups_t* groups);
int rgw_sal_user_groups_add(rgw_sal_user_groups_t* groups, const char* group_id, const char* group_name);

/*============================================================================
 * TOTP 验证函数
 *============================================================================*/

bool rgw_sal_verify_totp(const char* secret, const char* code, uint64_t timestamp);

/*============================================================================
 * 调试和协程上下文类型
 *============================================================================*/

/**
 * @brief 调试前缀提供者 (dpp)
 *
 * 用于调试和跟踪的上下文结构。
 * 在 C 实现中可以是一个简单的指针或结构体。
 */
typedef struct rgw_sal_dpp {
    void* opaque;  /**< 内部数据 */
} rgw_sal_dpp_t;

/**
 * @brief 协程上下文 (yield)
 *
 * 用于协程/异步操作的上下文结构。
 * 在 C 实现中可以是一个简单的指针或结构体。
 */
typedef struct rgw_sal_yield {
    void* opaque;  /**< 内部数据 */
} rgw_sal_yield_t;

#ifdef __cplusplus
}
#endif

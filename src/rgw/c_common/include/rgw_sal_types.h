#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 类型定义 */
typedef struct { char* id; char* tenant; int32_t type; } rgw_sal_user_id_t;
typedef struct { char* name; char* tenant; char* marker; char* bucket_id; } rgw_sal_bucket_id_t;
typedef struct { char* name; char* instance; bool is_null; bool is_current; } rgw_sal_obj_key_t;
typedef struct { 
    bool enabled; 
    bool check_on_raw; 
    uint64_t max_size; 
    uint64_t max_size_kb; 
    uint64_t max_objects;
    uint64_t quota_bytes;        /**< 字节配额 */
    uint64_t quota_max_objects;  /**< 最大对象数配额 */
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

typedef struct { uint32_t epoch; char* ver; bool committed; } rgw_sal_obj_version_t;
typedef struct { rgw_sal_obj_version_t read_version; rgw_sal_obj_version_t write_version; char* obj_tag; char* instance_tag; } rgw_sal_obj_version_tracker_t;
typedef struct { char* caps; } rgw_sal_user_caps_t;

/**
 * @brief 用户组成员关系
 */
typedef struct {
    char* group_id;   /**< 组 ID */
    char* tenant;     /**< 租户 */
} rgw_sal_user_group_t;

/**
 * @brief 用户组列表
 */
typedef struct {
    rgw_sal_user_group_t* groups;
    size_t count;
    size_t capacity;
} rgw_sal_user_groups_t;

/* 用户 ID 函数 */
rgw_sal_user_id_t* rgw_sal_user_id_create(void);
void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid);

/* 桶 ID 函数 */
rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void);
void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid);

/* 对象键函数 */
rgw_sal_obj_key_t* rgw_sal_obj_key_create(void);
void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key);

/* 用户组函数 */
rgw_sal_user_groups_t* rgw_sal_user_groups_create(void);
void rgw_sal_user_groups_destroy(rgw_sal_user_groups_t* groups);
int rgw_sal_user_groups_add(rgw_sal_user_groups_t* groups, const char* group_id, const char* group_name);

/* TOTP 验证函数 */
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

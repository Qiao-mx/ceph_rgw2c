/**
 * @file rgw_sal_types.c
 * @brief SAL 类型实现
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_sal_types.h"

/*============================================================================
 * 用户 ID 实现
 *============================================================================*/

rgw_sal_user_id_t* rgw_sal_user_id_create(void) {
    rgw_sal_user_id_t* uid = (rgw_sal_user_id_t*)calloc(1, sizeof(rgw_sal_user_id_t));
    return uid;
}

void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid) {
    if (!uid) return;
    if (uid->id) free(uid->id);
    if (uid->tenant) free(uid->tenant);
    free(uid);
}

/*============================================================================
 * 桶 ID 实现
 *============================================================================*/

rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void) {
    rgw_sal_bucket_id_t* bid = (rgw_sal_bucket_id_t*)calloc(1, sizeof(rgw_sal_bucket_id_t));
    return bid;
}

void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid) {
    if (!bid) return;
    if (bid->name) free(bid->name);
    if (bid->tenant) free(bid->tenant);
    if (bid->marker) free(bid->marker);
    if (bid->bucket_id) free(bid->bucket_id);
    free(bid);
}

/*============================================================================
 * 对象键实现
 *============================================================================*/

rgw_sal_obj_key_t* rgw_sal_obj_key_create(void) {
    rgw_sal_obj_key_t* key = (rgw_sal_obj_key_t*)calloc(1, sizeof(rgw_sal_obj_key_t));
    return key;
}

void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key) {
    if (!key) return;
    if (key->name) free(key->name);
    if (key->instance) free(key->instance);
    free(key);
}

/*============================================================================
 * 简化的创建函数（用于测试）
 *============================================================================*/

#include <stdlib.h>
#include <string.h>

/* 前向声明，需要包含完整的头文件 */
#ifndef RGW_SAL_SKIP_TYPES
#define RGW_SAL_SKIP_TYPES
typedef struct rgw_sal_driver rgw_sal_driver_t;
typedef struct rgw_sal_user rgw_sal_user_t;
typedef struct rgw_sal_bucket rgw_sal_bucket_t;
typedef struct rgw_sal_object rgw_sal_object_t;
typedef struct rgw_user { char *tenant; char *id; char *swift_name; char *swift_subuser; } rgw_user_t;
#endif

/**
 * @brief 用户结构（简化版，用于测试）
 */
struct rgw_sal_user {
    void *ops;
    rgw_user_t *user_id;
    void *driver;
};

/**
 * @brief 桶结构（简化版，用于测试）
 */
struct rgw_sal_bucket {
    void *ops;
    rgw_user_t *owner;
    char *name;
    char *marker;
    char *bucket_id;
    void *driver;
};

/**
 * @brief 对象结构（简化版，用于测试）
 */
struct rgw_sal_object {
    void *ops;
    rgw_sal_bucket_t *bucket;
    char *key;
    uint64_t size;
    void *driver;
};

/**
 * @brief 创建用户对象（简化版本，用于测试）
 */
rgw_sal_user_t *rgw_sal_user_create_simple(void) {
    rgw_sal_user_t *user = (rgw_sal_user_t *)calloc(1, sizeof(struct rgw_sal_user));
    if (user) {
        user->user_id = (rgw_user_t *)calloc(1, sizeof(rgw_user_t));
    }
    return user;
}

/**
 * @brief 创建桶对象（简化版本，用于测试）
 */
rgw_sal_bucket_t *rgw_sal_bucket_create_simple(void) {
    rgw_sal_bucket_t *bucket = (rgw_sal_bucket_t *)calloc(1, sizeof(struct rgw_sal_bucket));
    return bucket;
}

/**
 * @brief 创建对象（简化版本，用于测试）
 */
rgw_sal_object_t *rgw_sal_object_create_simple(void) {
    rgw_sal_object_t *obj = (rgw_sal_object_t *)calloc(1, sizeof(struct rgw_sal_object));
    return obj;
}

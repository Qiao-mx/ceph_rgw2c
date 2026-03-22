/**
 * @file rgw_sal_types.c
 * @brief SAL 类型实现
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
 * 用户组实现
 *============================================================================*/

rgw_sal_user_groups_t* rgw_sal_user_groups_create(void) {
    rgw_sal_user_groups_t* groups = (rgw_sal_user_groups_t*)calloc(1, sizeof(rgw_sal_user_groups_t));
    if (groups) {
        groups->capacity = 16;
        groups->groups = (rgw_sal_user_group_t*)calloc(groups->capacity, sizeof(rgw_sal_user_group_t));
        if (!groups->groups) {
            free(groups);
            return NULL;
        }
    }
    return groups;
}

void rgw_sal_user_groups_destroy(rgw_sal_user_groups_t* groups) {
    if (!groups) return;
    if (groups->groups) {
        for (size_t i = 0; i < groups->count; i++) {
            free(groups->groups[i].group_id);
            free(groups->groups[i].tenant);
        }
        free(groups->groups);
    }
    free(groups);
}

int rgw_sal_user_groups_add(rgw_sal_user_groups_t* groups, const char* group_id, const char* group_name) {
    if (!groups || !group_id) return -1;

    /* 扩容检查 */
    if (groups->count >= groups->capacity) {
        size_t new_capacity = groups->capacity * 2;
        rgw_sal_user_group_t* new_groups = (rgw_sal_user_group_t*)realloc(
            groups->groups, new_capacity * sizeof(rgw_sal_user_group_t));
        if (!new_groups) return -1;
        groups->groups = new_groups;
        groups->capacity = new_capacity;
    }

    /* 添加组 */
    size_t idx = groups->count++;
    groups->groups[idx].group_id = strdup(group_id);
    groups->groups[idx].tenant = group_name ? strdup(group_name) : NULL;

    return 0;
}

/*============================================================================
 * TOTP 验证实现 (简化版本)
 *============================================================================*/

/**
 * @brief 验证 TOTP 代码
 *
 * 简化实现：验证代码是否为 6 位数字。
 * 完整的 TOTP 实现需要 HMAC-SHA1。
 *
 * @param secret 密钥
 * @param code TOTP 代码
 * @param timestamp 时间戳
 * @return 验证结果
 */
bool rgw_sal_verify_totp(const char* secret, const char* code, uint64_t timestamp) {
    (void)secret;
    (void)timestamp;

    if (!code) return false;

    /* 简化验证：检查是否为 6 位数字 */
    if (strlen(code) != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (code[i] < '0' || code[i] > '9') return false;
    }
    return true;
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

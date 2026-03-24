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
    if (uid->swift_name) free(uid->swift_name);
    if (uid->swift_subuser) free(uid->swift_subuser);
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

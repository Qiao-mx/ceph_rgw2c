/**
 * @file rgw_sal_types.c
 * @brief SAL C 类型实现
 *
 * 实现 SAL C 接口中核心数据类型的创建和销毁函数。
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_sal.h"

/*============================================================================
 * 用户 ID 管理
 *============================================================================*/

rgw_sal_user_id_t* rgw_sal_user_id_create(void) {
    rgw_sal_user_id_t* uid = (rgw_sal_user_id_t*)calloc(1, sizeof(rgw_sal_user_id_t));
    return uid;
}

void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid) {
    if (!uid) return;
    free(uid->id);
    free(uid->tenant);
    free(uid->ns);
    free(uid);
}

/*============================================================================
 * 桶 ID 管理
 *============================================================================*/

rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void) {
    rgw_sal_bucket_id_t* bid = (rgw_sal_bucket_id_t*)calloc(1, sizeof(rgw_sal_bucket_id_t));
    return bid;
}

void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid) {
    if (!bid) return;
    free(bid->name);
    free(bid->tenant);
    free(bid->marker);
    free(bid->bucket_id);
    free(bid);
}

/*============================================================================
 * 对象键管理
 *============================================================================*/

rgw_sal_obj_key_t* rgw_sal_obj_key_create(void) {
    rgw_sal_obj_key_t* key = (rgw_sal_obj_key_t*)calloc(1, sizeof(rgw_sal_obj_key_t));
    return key;
}

void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key) {
    if (!key) return;
    free(key->name);
    free(key->instance);
    free(key);
}

/*============================================================================
 * 属性映射管理
 *============================================================================*/

#define INITIAL_ATTRS_CAPACITY 16

rgw_sal_attrs_t* rgw_sal_attrs_create(void) {
    rgw_sal_attrs_t* attrs = (rgw_sal_attrs_t*)calloc(1, sizeof(rgw_sal_attrs_t));
    if (!attrs) return NULL;

    attrs->pairs = (rgw_sal_attr_pair_t*)calloc(INITIAL_ATTRS_CAPACITY, sizeof(rgw_sal_attr_pair_t));
    if (!attrs->pairs) {
        free(attrs);
        return NULL;
    }

    attrs->capacity = INITIAL_ATTRS_CAPACITY;
    attrs->count = 0;
    return attrs;
}

void rgw_sal_attrs_destroy(rgw_sal_attrs_t* attrs) {
    if (!attrs) return;

    for (size_t i = 0; i < attrs->count; i++) {
        free(attrs->pairs[i].key);
        free(attrs->pairs[i].value);
    }
    free(attrs->pairs);
    free(attrs);
}

static int rgw_sal_attrs_expand(rgw_sal_attrs_t* attrs) {
    if (attrs->count >= attrs->capacity) {
        size_t new_capacity = attrs->capacity * 2;
        rgw_sal_attr_pair_t* new_pairs = (rgw_sal_attr_pair_t*)realloc(
            attrs->pairs, new_capacity * sizeof(rgw_sal_attr_pair_t));
        if (!new_pairs) return RGW_SAL_ERR_OUT_OF_MEMORY;
        attrs->pairs = new_pairs;
        attrs->capacity = new_capacity;
    }
    return RGW_SAL_OK;
}

int rgw_sal_attrs_set(rgw_sal_attrs_t* attrs, const char* key,
                       const uint8_t* value, size_t value_len) {
    if (!attrs || !key || !value) return RGW_SAL_ERR_INVALID_ARG;

    /* 检查是否已存在 */
    for (size_t i = 0; i < attrs->count; i++) {
        if (attrs->pairs[i].key && strcmp(attrs->pairs[i].key, key) == 0) {
            /* 更新现有属性 */
            free(attrs->pairs[i].value);
            attrs->pairs[i].value = (uint8_t*)malloc(value_len);
            if (!attrs->pairs[i].value) return RGW_SAL_ERR_OUT_OF_MEMORY;
            memcpy(attrs->pairs[i].value, value, value_len);
            attrs->pairs[i].value_len = value_len;
            return RGW_SAL_OK;
        }
    }

    /* 扩展空间 */
    int ret = rgw_sal_attrs_expand(attrs);
    if (ret != RGW_SAL_OK) return ret;

    /* 添加新属性 */
    attrs->pairs[attrs->count].key = strdup(key);
    if (!attrs->pairs[attrs->count].key) return RGW_SAL_ERR_OUT_OF_MEMORY;

    attrs->pairs[attrs->count].value = (uint8_t*)malloc(value_len);
    if (!attrs->pairs[attrs->count].value) {
        free(attrs->pairs[attrs->count].key);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    memcpy(attrs->pairs[attrs->count].value, value, value_len);
    attrs->pairs[attrs->count].value_len = value_len;
    attrs->count++;

    return RGW_SAL_OK;
}

int rgw_sal_attrs_get(const rgw_sal_attrs_t* attrs, const char* key,
                       uint8_t** value, size_t* value_len) {
    if (!attrs || !key || !value || !value_len) return RGW_SAL_ERR_INVALID_ARG;

    for (size_t i = 0; i < attrs->count; i++) {
        if (attrs->pairs[i].key && strcmp(attrs->pairs[i].key, key) == 0) {
            /* 复制值到输出参数，避免外部修改内部数据 */
            *value = (uint8_t*)malloc(attrs->pairs[i].value_len);
            if (!*value) return RGW_SAL_ERR_OUT_OF_MEMORY;
            memcpy(*value, attrs->pairs[i].value, attrs->pairs[i].value_len);
            *value_len = attrs->pairs[i].value_len;
            return RGW_SAL_OK;
        }
    }

    return RGW_SAL_ERR_NOT_FOUND;
}

/*============================================================================
 * 列表结果管理
 *============================================================================*/

void rgw_sal_bucket_list_destroy(rgw_sal_bucket_list_t* list) {
    if (!list) return;

    if (list->buckets) {
        for (size_t i = 0; i < list->count; i++) {
            if (list->buckets[i]) {
                /* 释放桶信息内的动态分配字段 */
                free(list->buckets[i]->bucket.name);
                free(list->buckets[i]->bucket.tenant);
                free(list->buckets[i]->bucket.marker);
                free(list->buckets[i]->bucket.bucket_id);
                free(list->buckets[i]);
            }
        }
        free(list->buckets);
    }
    free(list->marker);
    free(list);
}

void rgw_sal_object_list_destroy(rgw_sal_object_list_t* list) {
    if (!list) return;

    if (list->objects) {
        for (size_t i = 0; i < list->count; i++) {
            if (list->objects[i]) {
                free(list->objects[i]->key.name);
                free(list->objects[i]->key.instance);
                free(list->objects[i]->bucket.name);
                free(list->objects[i]->bucket.tenant);
                free(list->objects[i]);
            }
        }
        free(list->objects);
    }
    free(list->delimiter);
    free(list->prefix);
    free(list->marker);
    free(list->next_marker);
    free(list);
}

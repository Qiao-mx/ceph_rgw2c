/**
 * @file rgw_sal_attrs.c
 * @brief SAL 属性映射实现
 *
 * 提供 rgw_sal_attrs_t 的实现，使用简单的键值对数组存储。
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "rgw_sal.h"

/* 初始容量 */
#define ATTRS_INITIAL_CAPACITY 8

/* 扩容因子 */
#define ATTRS_GROWTH_FACTOR 2

/*============================================================================
 * 辅助函数
 *============================================================================*/

static int attrs_ensure_capacity(rgw_sal_attrs_t *attrs) {
    if (attrs->count < attrs->capacity) {
        return 0;
    }

    size_t new_capacity = attrs->capacity * ATTRS_GROWTH_FACTOR;
    if (new_capacity == 0) {
        new_capacity = ATTRS_INITIAL_CAPACITY;
    }

    rgw_sal_attr_pair_t *new_pairs = (rgw_sal_attr_pair_t *)realloc(
        attrs->pairs, new_capacity * sizeof(rgw_sal_attr_pair_t));

    if (!new_pairs) {
        return -1;
    }

    attrs->pairs = new_pairs;
    attrs->capacity = new_capacity;
    return 0;
}

static int attrs_find_key(const rgw_sal_attrs_t *attrs, const char *key) {
    for (size_t i = 0; i < attrs->count; i++) {
        if (attrs->pairs[i].key && strcmp(attrs->pairs[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static void attrs_free_pair(rgw_sal_attr_pair_t *pair) {
    if (pair->key) {
        free(pair->key);
        pair->key = NULL;
    }
    if (pair->value) {
        free(pair->value);
        pair->value = NULL;
    }
    pair->value_len = 0;
}

/*============================================================================
 * 公共 API 实现
 *============================================================================*/

rgw_sal_attrs_t *rgw_sal_attrs_create(void) {
    rgw_sal_attrs_t *attrs = (rgw_sal_attrs_t *)calloc(1, sizeof(rgw_sal_attrs_t));
    if (!attrs) {
        return NULL;
    }

    attrs->pairs = (rgw_sal_attr_pair_t *)calloc(ATTRS_INITIAL_CAPACITY,
                                                   sizeof(rgw_sal_attr_pair_t));
    if (!attrs->pairs) {
        free(attrs);
        return NULL;
    }

    attrs->capacity = ATTRS_INITIAL_CAPACITY;
    attrs->count = 0;
    return attrs;
}

void rgw_sal_attrs_destroy(rgw_sal_attrs_t *attrs) {
    if (!attrs) {
        return;
    }

    for (size_t i = 0; i < attrs->count; i++) {
        attrs_free_pair(&attrs->pairs[i]);
    }

    if (attrs->pairs) {
        free(attrs->pairs);
    }

    free(attrs);
}

int rgw_sal_attrs_set(rgw_sal_attrs_t *attrs, const char *key,
                       const uint8_t *value, size_t len) {
    if (!attrs || !key) {
        return -1;
    }

    int idx = attrs_find_key(attrs, key);
    if (idx >= 0) {
        uint8_t *new_value = (uint8_t *)malloc(len);
        if (!new_value) {
            return -1;
        }

        if (attrs->pairs[idx].value) {
            free(attrs->pairs[idx].value);
        }

        memcpy(new_value, value, len);
        attrs->pairs[idx].value = new_value;
        attrs->pairs[idx].value_len = len;
        return 0;
    }

    if (attrs_ensure_capacity(attrs) != 0) {
        return -1;
    }

    attrs->pairs[attrs->count].key = (char *)malloc(strlen(key) + 1);
    if (!attrs->pairs[attrs->count].key) {
        return -1;
    }
    strcpy(attrs->pairs[attrs->count].key, key);

    attrs->pairs[attrs->count].value = (uint8_t *)malloc(len);
    if (!attrs->pairs[attrs->count].value) {
        free(attrs->pairs[attrs->count].key);
        return -1;
    }
    memcpy(attrs->pairs[attrs->count].value, value, len);
    attrs->pairs[attrs->count].value_len = len;

    attrs->count++;
    return 0;
}

const uint8_t *rgw_sal_attrs_get(const rgw_sal_attrs_t *attrs, const char *key,
                                   size_t *len) {
    if (!attrs || !key) {
        return NULL;
    }

    int idx = attrs_find_key(attrs, key);
    if (idx < 0) {
        return NULL;
    }

    if (len) {
        *len = attrs->pairs[idx].value_len;
    }
    return attrs->pairs[idx].value;
}

int rgw_sal_attrs_del(rgw_sal_attrs_t *attrs, const char *key) {
    if (!attrs || !key) {
        return -1;
    }

    int idx = attrs_find_key(attrs, key);
    if (idx < 0) {
        return -1;
    }

    attrs_free_pair(&attrs->pairs[idx]);

    for (size_t i = idx; i < attrs->count - 1; i++) {
        attrs->pairs[i] = attrs->pairs[i + 1];
    }

    attrs->count--;
    return 0;
}

rgw_sal_attrs_t *rgw_sal_attrs_clone(rgw_sal_attrs_t *attrs) {
    if (!attrs) {
        return NULL;
    }

    rgw_sal_attrs_t *clone = rgw_sal_attrs_create();
    if (!clone) {
        return NULL;
    }

    for (size_t i = 0; i < attrs->count; i++) {
        if (rgw_sal_attrs_set(clone, attrs->pairs[i].key,
                               attrs->pairs[i].value,
                               attrs->pairs[i].value_len) != 0) {
            rgw_sal_attrs_destroy(clone);
            return NULL;
        }
    }

    return clone;
}

size_t rgw_sal_attrs_count(const rgw_sal_attrs_t *attrs) {
    return attrs ? attrs->count : 0;
}

const char *rgw_sal_attrs_get_key(const rgw_sal_attrs_t *attrs, size_t index) {
    if (!attrs || index >= attrs->count) {
        return NULL;
    }
    return attrs->pairs[index].key;
}

const uint8_t *rgw_sal_attrs_get_value(const rgw_sal_attrs_t *attrs, size_t index,
                                        size_t *len) {
    if (!attrs || index >= attrs->count) {
        return NULL;
    }
    if (len) {
        *len = attrs->pairs[index].value_len;
    }
    return attrs->pairs[index].value;
}

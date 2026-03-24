/**
 * @file rgw_sal.c
 * @brief SAL 核心实现
 *
 * 提供 SAL (Storage Abstraction Layer) 的核心实现：
 * - rgw_sal_driver_destroy: 驱动销毁
 * - rgw_sal_user_destroy: 用户销毁
 * - rgw_sal_bucket_destroy: 桶销毁
 * - rgw_sal_object_destroy: 对象销毁
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_sal.h"

/*============================================================================
 * 简化结构定义 (与 rgw_sal_types.c 保持一致)
 *============================================================================*/

typedef struct {
    void *ops;
    rgw_user_t *user_id;
    void *driver;
} simple_user_t;

typedef struct {
    void *ops;
    rgw_user_t *owner;
    char *name;
    char *marker;
    char *bucket_id;
    void *driver;
} simple_bucket_t;

typedef struct {
    void *ops;
    void *bucket;
    char *key;
    uint64_t size;
    void *driver;
} simple_object_t;

typedef struct {
    void *ops;
    void *context;
    void *impl;
} simple_driver_t;

/*============================================================================
 * rgw_user 实现
 *============================================================================*/

rgw_user_t *rgw_user_create(const char *tenant, const char *id) {
    rgw_user_t *user = (rgw_user_t *)calloc(1, sizeof(rgw_user_t));
    if (!user) {
        return NULL;
    }

    if (tenant) {
        user->tenant = (char *)malloc(strlen(tenant) + 1);
        if (user->tenant) {
            strcpy(user->tenant, tenant);
        }
    }

    if (id) {
        user->id = (char *)malloc(strlen(id) + 1);
        if (user->id) {
            strcpy(user->id, id);
        }
    }

    return user;
}

void rgw_user_destroy(rgw_user_t *user) {
    if (!user) {
        return;
    }

    if (user->tenant) {
        free(user->tenant);
    }
    if (user->id) {
        free(user->id);
    }
    if (user->swift_name) {
        free(user->swift_name);
    }
    if (user->swift_subuser) {
        free(user->swift_subuser);
    }

    free(user);
}

rgw_user_t *rgw_user_copy(const rgw_user_t *user) {
    if (!user) {
        return NULL;
    }

    rgw_user_t *copy = (rgw_user_t *)calloc(1, sizeof(rgw_user_t));
    if (!copy) {
        return NULL;
    }

    if (user->tenant) {
        copy->tenant = (char *)malloc(strlen(user->tenant) + 1);
        if (copy->tenant) {
            strcpy(copy->tenant, user->tenant);
        }
    }

    if (user->id) {
        copy->id = (char *)malloc(strlen(user->id) + 1);
        if (copy->id) {
            strcpy(copy->id, user->id);
        }
    }

    if (user->swift_name) {
        copy->swift_name = (char *)malloc(strlen(user->swift_name) + 1);
        if (copy->swift_name) {
            strcpy(copy->swift_name, user->swift_name);
        }
    }

    if (user->swift_subuser) {
        copy->swift_subuser = (char *)malloc(strlen(user->swift_subuser) + 1);
        if (copy->swift_subuser) {
            strcpy(copy->swift_subuser, user->swift_subuser);
        }
    }

    return copy;
}

int rgw_user_equal(const rgw_user_t *a, const rgw_user_t *b) {
    if (!a || !b) {
        return (a == b) ? 1 : 0;
    }

    if ((a->tenant && !b->tenant) || (!a->tenant && b->tenant)) {
        return 0;
    }
    if (a->tenant && b->tenant && strcmp(a->tenant, b->tenant) != 0) {
        return 0;
    }

    if ((a->id && !b->id) || (!a->id && b->id)) {
        return 0;
    }
    if (a->id && b->id && strcmp(a->id, b->id) != 0) {
        return 0;
    }

    return 1;
}

/*============================================================================
 * 桶列表实现
 *============================================================================*/

rgw_sal_bucket_list_t *rgw_sal_bucket_list_create(void) {
    rgw_sal_bucket_list_t *list = (rgw_sal_bucket_list_t *)calloc(1, sizeof(rgw_sal_bucket_list_t));
    return list;
}

void rgw_sal_bucket_list_destroy(rgw_sal_bucket_list_t *list) {
    if (!list) {
        return;
    }

    if (list->next_marker) {
        free(list->next_marker);
    }

    /* Note: 'buckets' 内部使用容器，由容器自行管理内存 */

    free(list);
}

size_t rgw_sal_bucket_list_size(const rgw_sal_bucket_list_t *list) {
    if (!list) {
        return 0;
    }
    return list->count;
}

const char *rgw_sal_bucket_list_next_marker(const rgw_sal_bucket_list_t *list) {
    if (!list) {
        return NULL;
    }
    return list->next_marker;
}

/*============================================================================
 * SAL Driver 实现
 *============================================================================*/

rgw_sal_driver_t *rgw_sal_driver_create(const rgw_sal_driver_ops_t *ops, void *context) {
    if (!ops) {
        return NULL;
    }

    rgw_sal_driver_t *driver = (rgw_sal_driver_t *)calloc(1, sizeof(rgw_sal_driver_t));
    if (!driver) {
        return NULL;
    }

    driver->ops = ops;
    driver->context = context;

    return driver;
}

int rgw_sal_driver_initialize(rgw_sal_driver_t *driver, void *cct, void *dpp) {
    if (!driver || !driver->ops || !driver->ops->initialize) {
        return -1;
    }
    return driver->ops->initialize(driver, cct, dpp);
}

const char *rgw_sal_driver_get_name(const rgw_sal_driver_t *driver) {
    if (!driver || !driver->ops || !driver->ops->get_name) {
        return NULL;
    }
    return driver->ops->get_name(driver);
}

void rgw_sal_driver_destroy(rgw_sal_driver_t *driver) {
    if (!driver) {
        return;
    }

    /* 调用驱动特定的销毁函数 */
    if (driver->ops && driver->ops->destroy) {
        driver->ops->destroy(driver);
    }

    /* 释放驱动结构本身 */
    free(driver);
}

/*============================================================================
 * SAL User 实现
 *============================================================================*/

rgw_sal_user_t *rgw_sal_user_create(const rgw_sal_user_ops_t *ops, rgw_user_t *user_id, void *driver) {
    if (!ops) {
        return NULL;
    }

    rgw_sal_user_t *user = (rgw_sal_user_t *)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) {
        return NULL;
    }

    user->ops = ops;
    user->user_id = user_id;
    user->driver = driver;

    return user;
}

int rgw_sal_user_load(rgw_sal_user_t *user, void *dpp, void *y, rgw_user_info_t *info, rgw_sal_attrs_t *attrs, void *objv) {
    if (!user || !user->ops || !user->ops->load) {
        return -1;
    }
    return user->ops->load(user, dpp, y, info, attrs, objv);
}

int rgw_sal_user_store(rgw_sal_user_t *user, void *dpp, void *y, bool exclusive, const rgw_user_info_t *info, const rgw_user_info_t *old_info, const rgw_sal_attrs_t *attrs, void *objv) {
    if (!user || !user->ops || !user->ops->store) {
        return -1;
    }
    return user->ops->store(user, dpp, y, exclusive, info, old_info, attrs, objv);
}

int rgw_sal_user_remove(rgw_sal_user_t *user, void *dpp, void *y, const rgw_user_info_t *info, void *objv) {
    if (!user || !user->ops || !user->ops->remove) {
        return -1;
    }
    return user->ops->remove(user, dpp, y, info, objv);
}

void rgw_sal_user_destroy(rgw_sal_user_t *user) {
    if (!user) {
        return;
    }

    /* 调用用户特定的销毁函数 */
    if (user->ops && user->ops->destroy) {
        user->ops->destroy(user);
    }

    /* 释放用户 ID */
    if (user->user_id) {
        rgw_user_destroy(user->user_id);
    }

    /* 释放用户结构本身 */
    free(user);
}

/*============================================================================
 * SAL Bucket 实现
 *============================================================================*/

rgw_sal_bucket_t *rgw_sal_bucket_create(const rgw_sal_bucket_ops_t *ops, rgw_user_t *owner, const char *name, void *driver) {
    if (!ops) {
        return NULL;
    }

    rgw_sal_bucket_t *bucket = (rgw_sal_bucket_t *)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) {
        return NULL;
    }

    bucket->ops = ops;
    bucket->owner = owner;

    if (name) {
        bucket->name = (char *)malloc(strlen(name) + 1);
        if (bucket->name) {
            strcpy(bucket->name, name);
        }
    }

    bucket->driver = driver;

    return bucket;
}

int rgw_sal_bucket_load(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv) {
    if (!bucket || !bucket->ops || !bucket->ops->load) {
        return -1;
    }
    return bucket->ops->load(bucket, dpp, y, objv);
}

int rgw_sal_bucket_store(rgw_sal_bucket_t *bucket, void *dpp, void *y, bool exclusive, void *objv) {
    if (!bucket || !bucket->ops || !bucket->ops->store) {
        return -1;
    }
    return bucket->ops->store(bucket, dpp, y, exclusive, objv);
}

int rgw_sal_bucket_remove(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv) {
    if (!bucket || !bucket->ops || !bucket->ops->remove) {
        return -1;
    }
    return bucket->ops->remove(bucket, dpp, y, objv);
}

void rgw_sal_bucket_destroy(rgw_sal_bucket_t *bucket) {
    if (!bucket) {
        return;
    }

    /* 调用桶特定的销毁函数 */
    if (bucket->ops && bucket->ops->destroy) {
        bucket->ops->destroy(bucket);
    }

    /* 释放所有者 */
    if (bucket->owner) {
        rgw_user_destroy(bucket->owner);
    }

    /* 释放字符串字段 */
    if (bucket->name) {
        free(bucket->name);
    }
    if (bucket->marker) {
        free(bucket->marker);
    }
    if (bucket->bucket_id) {
        free(bucket->bucket_id);
    }

    /* 释放桶结构本身 */
    free(bucket);
}

/*============================================================================
 * SAL Object 实现
 *============================================================================*/

rgw_sal_object_t *rgw_sal_object_create(const rgw_sal_object_ops_t *ops, rgw_sal_bucket_t *bucket, const char *key, void *driver) {
    if (!ops) {
        return NULL;
    }

    rgw_sal_object_t *obj = (rgw_sal_object_t *)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) {
        return NULL;
    }

    obj->ops = ops;
    obj->bucket = bucket;

    if (key) {
        obj->key = (char *)malloc(strlen(key) + 1);
        if (obj->key) {
            strcpy(obj->key, key);
        }
    }

    obj->driver = driver;

    return obj;
}

int rgw_sal_object_load(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags) {
    if (!obj || !obj->ops || !obj->ops->load) {
        return -1;
    }
    return obj->ops->load(obj, dpp, y, flags);
}

int rgw_sal_object_store(rgw_sal_object_t *obj, void *dpp, void *y, bool exclusive, uint32_t flags) {
    if (!obj || !obj->ops || !obj->ops->store) {
        return -1;
    }
    return obj->ops->store(obj, dpp, y, exclusive, flags);
}

int rgw_sal_object_remove(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags) {
    if (!obj || !obj->ops || !obj->ops->remove) {
        return -1;
    }
    return obj->ops->remove(obj, dpp, y, flags);
}

int rgw_sal_object_read(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, uint8_t *buf, size_t *bytes_read) {
    if (!obj || !obj->ops || !obj->ops->read) {
        return -1;
    }
    return obj->ops->read(obj, dpp, y, offset, len, buf, bytes_read);
}

int rgw_sal_object_write(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, const uint8_t *buf, size_t *bytes_written) {
    if (!obj || !obj->ops || !obj->ops->write) {
        return -1;
    }
    return obj->ops->write(obj, dpp, y, offset, len, buf, bytes_written);
}

void rgw_sal_object_destroy(rgw_sal_object_t *obj) {
    if (!obj) {
        return;
    }

    /* 调用对象特定的销毁函数 */
    if (obj->ops && obj->ops->destroy) {
        obj->ops->destroy(obj);
    }

    /* 注意: bucket 不在这里释放，因为它是外部传入的 */

    /* 释放键名 */
    if (obj->key) {
        free(obj->key);
    }

    /* 释放对象结构本身 */
    free(obj);
}

/*============================================================================
 * 简化的创建函数（用于测试）
 *============================================================================*/

/**
 * @brief 用户空销毁回调（用于简化创建的用户）
 */
static void user_simple_destroy(rgw_sal_user_t* user) {
    /* 什么也不做 - 简化版本不需要特殊清理 */
    (void)user;
}

/**
 * @brief 桶空销毁回调
 */
static void bucket_simple_destroy(rgw_sal_bucket_t* bucket) {
    (void)bucket;
}

/**
 * @brief 对象空销毁回调
 */
static void object_simple_destroy(rgw_sal_object_t* obj) {
    (void)obj;
}

/**
 * @brief 创建用户对象（简化版本，用于测试）
 */
rgw_sal_user_t *rgw_sal_user_create_simple(void) {
    /* 静态 ops 结构 - 用于简化版本 */
    static rgw_sal_user_ops_t simple_ops = {
        .load = NULL,
        .store = NULL,
        .remove = NULL,
        .destroy = user_simple_destroy
    };

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) {
        return NULL;
    }

    user->ops = &simple_ops;
    user->user_id = NULL;
    user->driver = NULL;
    user->impl = NULL;
    user->vtable = NULL;

    return user;
}

/**
 * @brief 创建桶对象（简化版本，用于测试）
 */
rgw_sal_bucket_t *rgw_sal_bucket_create_simple(void) {
    /* 静态 ops 结构 - 用于简化版本 */
    static rgw_sal_bucket_ops_t simple_ops = {
        .load = NULL,
        .store = NULL,
        .remove = NULL,
        .list_objects = NULL,
        .destroy = bucket_simple_destroy
    };

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) {
        return NULL;
    }

    bucket->ops = &simple_ops;
    bucket->owner = NULL;
    bucket->name = NULL;
    bucket->marker = NULL;
    bucket->bucket_id = NULL;
    bucket->driver = NULL;
    bucket->impl = NULL;
    bucket->vtable = NULL;
    bucket->bucket_vtable = NULL;

    return bucket;
}

/**
 * @brief 创建对象（简化版本，用于测试）
 */
rgw_sal_object_t *rgw_sal_object_create_simple(void) {
    /* 静态 ops 结构 - 用于简化版本 */
    static rgw_sal_object_ops_t simple_ops = {
        .load = NULL,
        .store = NULL,
        .remove = NULL,
        .read = NULL,
        .write = NULL,
        .destroy = object_simple_destroy
    };

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) {
        return NULL;
    }

    obj->ops = &simple_ops;
    obj->bucket = NULL;
    obj->key = NULL;
    obj->size = 0;
    obj->driver = NULL;
    obj->impl = NULL;
    obj->vtable = NULL;

    return obj;
}

/*============================================================================
 * 对象列表实现
 *============================================================================*/

/**
 * @brief 创建对象列表
 */
rgw_sal_object_list_t *rgw_sal_object_list_create(void) {
    rgw_sal_object_list_t *list = (rgw_sal_object_list_t*)calloc(1, sizeof(rgw_sal_object_list_t));
    if (!list) {
        return NULL;
    }
    list->objects = NULL;
    list->next_marker = NULL;
    list->truncated = false;
    list->count = 0;
    list->is_truncated = false;
    return list;
}

/**
 * @brief 释放对象列表
 */
void rgw_sal_object_list_destroy(rgw_sal_object_list_t *list) {
    if (!list) {
        return;
    }
    if (list->objects) {
        free(list->objects);
    }
    if (list->next_marker) {
        free(list->next_marker);
    }
    free(list);
}

/**
 * @brief 获取对象列表大小
 */
size_t rgw_sal_object_list_size(const rgw_sal_object_list_t *list) {
    if (!list) {
        return 0;
    }
    return list->count;
}

/**
 * @brief 获取下一个 marker
 */
const char *rgw_sal_object_list_next_marker(const rgw_sal_object_list_t *list) {
    if (!list) {
        return NULL;
    }
    return list->next_marker;
}

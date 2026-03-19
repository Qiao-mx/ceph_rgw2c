/**
 * @file rgw_sal_posix.c
 * @brief POSIX 文件系统存储驱动实现
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

#include "rgw_sal.h"
#include "rgw_sal_posix.h"
#include "rgw_sal_errors.h"

/**
 * @brief POSIX 用户实现
 */
typedef struct posix_user_impl {
    char* user_id;
    char* tenant;
    char* display_name;
    int32_t max_buckets;
    uint32_t user_type;
    char* access_key;
    char* secret_key;
    rgw_sal_attrs_t* attrs;
    bool loaded;
    time_t mtime;
} posix_user_impl_t;

/**
 * @brief POSIX 桶实现
 */
typedef struct posix_bucket_impl {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    char* root_path;
    rgw_sal_attrs_t* attrs;
    void* acl;
    void* policy;
    bool loaded;
    bool created;
    bool deleted;
    time_t mtime;
} posix_bucket_impl_t;

/**
 * @brief POSIX 对象实现
 */
typedef struct posix_object_impl {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    char* file_path;
    rgw_sal_attrs_t* attrs;
    bool is_null;
    int64_t size;
    time_t mtime;
    bool written;
    bool deleted;
    bool loaded;
} posix_object_impl_t;

/**
 * @brief POSIX 驱动实现
 */
typedef struct posix_driver_impl {
    char name[64];
    char root_path[512];
    char db_path[512];
    int max_handles;
    bool initialized;
} posix_driver_impl_t;

/* 驱动初始化 */
static int posix_driver_initialize(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)calloc(1, sizeof(posix_driver_impl_t));
    if (!impl) return RGW_SAL_ERR_OUT_OF_MEMORY;

    strncpy(impl->name, "posix", sizeof(impl->name) - 1);
    impl->max_handles = 256;
    impl->initialized = true;

    driver->impl = impl;

    (void)cct;
    (void)dpp;
    return RGW_SAL_OK;
}

static void posix_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (impl) {
        impl->initialized = false;
        free(impl);
    }
    driver->impl = NULL;
}

static const char* posix_driver_get_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    return impl ? impl->name : NULL;
}

static int posix_driver_get_cluster_id(rgw_sal_driver_t* driver, char** cluster_id,
                                       const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !cluster_id) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *cluster_id = strdup("posix-fs");

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 用户操作 - 使用正确的 vtable 签名 */
static rgw_sal_user_t* posix_driver_get_user(rgw_sal_driver_t* driver,
                                               const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    posix_user_impl_t* impl = (posix_user_impl_t*)calloc(1, sizeof(posix_user_impl_t));
    if (!impl) {
        free(user);
        return NULL;
    }

    if (uid->id) impl->user_id = strdup(uid->id);
    if (uid->tenant) impl->tenant = strdup(uid->tenant);
    impl->max_buckets = -1;
    impl->loaded = false;
    impl->mtime = time(NULL);

    user->impl = impl;
    user->vtable = driver->user_vtable;
    user->driver = driver;

    return user;
}

/* 用户 vtable 函数 */
static void* posix_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* clone = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!clone) return NULL;

    posix_user_impl_t* old_impl = (posix_user_impl_t*)user->impl;
    posix_user_impl_t* new_impl = (posix_user_impl_t*)calloc(1, sizeof(posix_user_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    if (old_impl->user_id) new_impl->user_id = strdup(old_impl->user_id);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->display_name) new_impl->display_name = strdup(old_impl->display_name);
    if (old_impl->access_key) new_impl->access_key = strdup(old_impl->access_key);
    if (old_impl->secret_key) new_impl->secret_key = strdup(old_impl->secret_key);
    new_impl->max_buckets = old_impl->max_buckets;
    new_impl->user_type = old_impl->user_type;
    new_impl->loaded = old_impl->loaded;
    new_impl->mtime = old_impl->mtime;

    clone->impl = new_impl;
    clone->vtable = user->vtable;
    clone->driver = user->driver;

    return clone;
}

static void posix_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (impl) {
        free(impl->user_id);
        free(impl->tenant);
        free(impl->display_name);
        free(impl->access_key);
        free(impl->secret_key);
        if (impl->attrs) rgw_sal_attrs_destroy(impl->attrs);
        free(impl);
    }
    free(user);
}

static const char* posix_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->user_id : NULL;
}

static const char* posix_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->display_name : NULL;
}

static int posix_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->display_name);
    impl->display_name = name ? strdup(name) : NULL;
    return RGW_SAL_OK;
}

static const char* posix_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->tenant : NULL;
}

static uint32_t posix_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->user_type : 0;
}

static int32_t posix_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return 0;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->max_buckets : 0;
}

static void posix_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (impl) {
        impl->max_buckets = max;
    }
}

static rgw_sal_attrs_t* posix_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int posix_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;
    return RGW_SAL_OK;
}

static int posix_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->loaded = true;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    (void)user;
    (void)dpp;
    (void)y;
    (void)exclusive;
    return RGW_SAL_OK;
}

static int posix_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    (void)user;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    (void)user;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_user_merge_and_store_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    (void)user;
    (void)new_attrs;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static const char* posix_user_get_ns(const rgw_sal_user_t* user) {
    (void)user;
    return NULL;
}

static int posix_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    (void)user;
    (void)ns;
    return RGW_SAL_OK;
}

static void posix_user_clear_ns(rgw_sal_user_t* user) {
    (void)user;
}

/* 用户 vtable */
static rgw_sal_user_vtable_t posix_user_vtable = {
    .clone = posix_user_clone,
    .destroy = posix_user_destroy,
    .get_id = posix_user_get_id,
    .get_display_name = posix_user_get_display_name,
    .set_display_name = posix_user_set_display_name,
    .get_tenant = posix_user_get_tenant,
    .get_type = posix_user_get_type,
    .get_max_buckets = posix_user_get_max_buckets,
    .set_max_buckets = posix_user_set_max_buckets,
    .get_attrs = posix_user_get_attrs,
    .set_attrs = posix_user_set_attrs,
    .load = posix_user_load,
    .store = posix_user_store,
    .remove = posix_user_remove,
    .read_attrs = posix_user_read_attrs,
    .merge_and_store_attrs = posix_user_merge_and_store_attrs,
    .get_ns = posix_user_get_ns,
    .set_ns = posix_user_set_ns,
    .clear_ns = posix_user_clear_ns,
};

/* 桶操作 - 使用正确的 vtable 签名 */
static rgw_sal_bucket_t* posix_driver_get_bucket(rgw_sal_driver_t* driver,
                                                 const rgw_sal_bucket_info_t* info) {
    if (!driver || !info) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)calloc(1, sizeof(posix_bucket_impl_t));
    if (!impl) {
        free(bucket);
        return NULL;
    }

    if (info->bucket.name) impl->name = strdup(info->bucket.name);
    if (info->bucket.tenant) impl->tenant = strdup(info->bucket.tenant);
    if (info->bucket.marker) impl->marker = strdup(info->bucket.marker);
    impl->loaded = false;
    impl->mtime = time(NULL);

    bucket->impl = impl;
    bucket->vtable = driver->bucket_vtable;
    bucket->driver = driver;

    return bucket;
}

/* 桶 vtable 函数 */
static void* posix_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* clone = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!clone) return NULL;

    posix_bucket_impl_t* old_impl = (posix_bucket_impl_t*)bucket->impl;
    posix_bucket_impl_t* new_impl = (posix_bucket_impl_t*)calloc(1, sizeof(posix_bucket_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->marker) new_impl->marker = strdup(old_impl->marker);
    if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
    if (old_impl->owner_id) new_impl->owner_id = strdup(old_impl->owner_id);
    if (old_impl->root_path) new_impl->root_path = strdup(old_impl->root_path);
    new_impl->loaded = old_impl->loaded;
    new_impl->created = old_impl->created;
    new_impl->deleted = old_impl->deleted;
    new_impl->mtime = old_impl->mtime;

    clone->impl = new_impl;
    clone->vtable = bucket->vtable;
    clone->driver = bucket->driver;

    return clone;
}

static void posix_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (impl) {
        free(impl->name);
        free(impl->tenant);
        free(impl->marker);
        free(impl->bucket_id);
        free(impl->owner_id);
        free(impl->root_path);
        if (impl->attrs) rgw_sal_attrs_destroy(impl->attrs);
        free(impl);
    }
    free(bucket);
}

static const char* posix_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    return impl ? impl->name : NULL;
}

static const char* posix_bucket_get_tenant(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    return impl ? impl->tenant : NULL;
}

static rgw_sal_bucket_info_t* posix_bucket_get_info(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return NULL;

    return (rgw_sal_bucket_info_t*)impl;
}

static rgw_sal_user_t* posix_bucket_get_owner(rgw_sal_bucket_t* bucket) {
    (void)bucket;
    return NULL;
}

static int posix_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y, bool create_obj) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->created = true;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    (void)create_obj;
    return RGW_SAL_OK;
}

static int posix_bucket_delete_bucket(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y, bool delete_objects) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->deleted = true;

    (void)dpp;
    (void)y;
    (void)delete_objects;
    return RGW_SAL_OK;
}

static int posix_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y, const char* new_name) {
    if (!bucket || !new_name) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->name);
    impl->name = strdup(new_name);
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl, const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->acl = acl;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !policy) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *policy = impl->policy;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->policy = policy;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 桶 vtable */
static rgw_sal_bucket_vtable_t posix_bucket_vtable = {
    .clone = posix_bucket_clone,
    .destroy = posix_bucket_destroy,
    .get_name = posix_bucket_get_name,
    .get_tenant = posix_bucket_get_tenant,
    .get_info = posix_bucket_get_info,
    .get_owner = posix_bucket_get_owner,
    .create = posix_bucket_create,
    .delete_bucket = posix_bucket_delete_bucket,
    .rename = posix_bucket_rename,
    .set_acl = posix_bucket_set_acl,
    .get_policy = posix_bucket_get_policy,
    .set_policy = posix_bucket_set_policy,
};

/* 对象操作 - 使用正确的 vtable 签名 */
static rgw_sal_object_t* posix_driver_get_object(rgw_sal_driver_t* driver,
                                                  rgw_sal_bucket_t* bucket,
                                                  const rgw_sal_obj_key_t* key) {
    if (!driver || !key) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    posix_object_impl_t* impl = (posix_object_impl_t*)calloc(1, sizeof(posix_object_impl_t));
    if (!impl) {
        free(obj);
        return NULL;
    }

    if (key->name) impl->name = strdup(key->name);
    if (key->instance) impl->instance = strdup(key->instance);
    impl->written = false;
    impl->deleted = false;
    impl->loaded = false;

    obj->impl = impl;
    obj->vtable = driver->object_vtable;

    (void)bucket;
    return obj;
}

/* 对象 vtable 函数 */
static void* posix_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* clone = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!clone) return NULL;

    posix_object_impl_t* old_impl = (posix_object_impl_t*)obj->impl;
    posix_object_impl_t* new_impl = (posix_object_impl_t*)calloc(1, sizeof(posix_object_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->instance) new_impl->instance = strdup(old_impl->instance);
    if (old_impl->bucket_name) new_impl->bucket_name = strdup(old_impl->bucket_name);
    if (old_impl->bucket_tenant) new_impl->bucket_tenant = strdup(old_impl->bucket_tenant);
    if (old_impl->file_path) new_impl->file_path = strdup(old_impl->file_path);
    new_impl->is_null = old_impl->is_null;
    new_impl->size = old_impl->size;
    new_impl->mtime = old_impl->mtime;
    new_impl->written = old_impl->written;
    new_impl->deleted = old_impl->deleted;
    new_impl->loaded = old_impl->loaded;

    clone->impl = new_impl;
    clone->vtable = obj->vtable;

    return clone;
}

static void posix_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (impl) {
        free(impl->name);
        free(impl->instance);
        free(impl->bucket_name);
        free(impl->bucket_tenant);
        free(impl->file_path);
        if (impl->attrs) rgw_sal_attrs_destroy(impl->attrs);
        free(impl);
    }
    free(obj);
}

static const char* posix_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    return impl ? impl->name : NULL;
}

static const char* posix_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    return impl ? impl->instance : NULL;
}

static bool posix_object_is_null(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    return impl ? impl->is_null : false;
}

static rgw_sal_attrs_t* posix_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int posix_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int posix_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                               const uint8_t* data,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !data) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->size = size;
    impl->mtime = time(NULL);
    impl->written = true;

    (void)offset;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->deleted = true;
    impl->mtime = time(NULL);

    (void)flags;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 对象 vtable */
static rgw_sal_object_vtable_t posix_object_vtable = {
    .clone = posix_object_clone,
    .destroy = posix_object_destroy,
    .get_name = posix_object_get_name,
    .get_instance = posix_object_get_instance,
    .is_null = posix_object_is_null,
    .get_attrs = posix_object_get_attrs,
    .set_attrs = posix_object_set_attrs,
    .write = posix_object_write,
    .delete_obj = posix_object_delete_obj,
};

/* 驱动 vtable */
static rgw_sal_driver_vtable_t posix_driver_vtable = {
    .destroy = posix_driver_destroy,
    .initialize = posix_driver_initialize,
    .get_name = posix_driver_get_name,
    .get_cluster_id = posix_driver_get_cluster_id,
    .get_user = posix_driver_get_user,
    .get_bucket = posix_driver_get_bucket,
    .get_object = posix_driver_get_object,
};

/**
 * @brief 初始化 POSIX 驱动
 */
int rgw_sal_posix_init(rgw_sal_driver_t* driver, rgw_sal_posix_config_t* config) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    driver->vtable = &posix_driver_vtable;
    driver->user_vtable = &posix_user_vtable;
    driver->bucket_vtable = &posix_bucket_vtable;
    driver->object_vtable = &posix_object_vtable;

    if (config) {
        posix_driver_impl_t* impl = (posix_driver_impl_t*)calloc(1, sizeof(posix_driver_impl_t));
        if (!impl) return RGW_SAL_ERR_OUT_OF_MEMORY;

        strncpy(impl->name, "posix", sizeof(impl->name) - 1);
        if (config->root_path) {
            strncpy(impl->root_path, config->root_path, sizeof(impl->root_path) - 1);
        }
        if (config->db_path) {
            strncpy(impl->db_path, config->db_path, sizeof(impl->db_path) - 1);
        }
        impl->max_handles = config->max_handles > 0 ? config->max_handles : 256;
        impl->initialized = true;

        driver->impl = impl;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 关闭 POSIX 驱动
 */
int rgw_sal_posix_shutdown(rgw_sal_driver_t* driver) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (impl) {
        impl->initialized = false;
        free(impl);
        driver->impl = NULL;
    }

    return RGW_SAL_OK;
}

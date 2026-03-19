/**
 * @file rgw_sal_d4n.c
 * @brief D4N (Data for Nginx) 缓存驱动实现
 * 
 * D4N 是一个过滤器驱动，在底层驱动基础上添加缓存功能：
 * - 对象数据缓存 (SSD/内存)
 * - 元数据缓存 (Redis)
 * - 块目录管理
 * - 对象目录管理
 * - 策略驱动
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>

#include "rgw_sal.h"
#include "rgw_sal_d4n.h"
#include "rgw_sal_errors.h"

/**
 * @brief D4N 用户实现
 * 
 * D4N 用户是底层用户的包装，添加缓存相关状态
 */
typedef struct d4n_user_impl {
    void* next_impl;                /**< 底层用户实现 */
    bool cache_dirty;               /**< 缓存是否脏 */
    time_t cache_time;              /**< 缓存时间 */
} d4n_user_impl_t;

/**
 * @brief D4N 桶实现
 * 
 * D4N 桶是底层桶的包装，添加缓存功能
 */
typedef struct d4n_bucket_impl {
    void* next_impl;                /**< 底层桶实现 */
    char* cache_key;                /**< 缓存键 */
    bool cache_dirty;               /**< 缓存是否脏 */
    time_t cache_time;              /**< 缓存时间 */
    void* bucket_dir;               /**< 桶目录缓存 */
} d4n_bucket_impl_t;

/**
 * @brief D4N 对象实现
 * 
 * D4N 对象是底层对象的包装，添加缓存功能：
 * - 对象数据缓存
 * - 对象目录缓存
 * - 热点分析
 */
typedef struct d4n_object_impl {
    void* next_impl;                /**< 底层对象实现 */
    char* cache_key;                /**< 缓存键 */
    bool cached;                    /**< 是否已缓存 */
    bool cache_dirty;               /**< 缓存是否脏 */
    time_t cache_time;              /**< 缓存时间 */
    int64_t size;                   /**< 对象大小 */
    char* storage_class;            /**< 存储类 */
} d4n_object_impl_t;

/**
 * @brief D4N 驱动实现
 */
typedef struct d4n_driver_impl {
    char name[64];
    char cache_path[512];
    char redis_address[256];
    int cache_size;
    char cache_policy[32];
    rgw_sal_driver_t* next_driver;  /**< 底层驱动 */
    bool initialized;
} d4n_driver_impl_t;

/* 驱动初始化 */
static int d4n_driver_initialize(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)calloc(1, sizeof(d4n_driver_impl_t));
    if (!impl) return RGW_SAL_ERR_OUT_OF_MEMORY;

    strncpy(impl->name, "d4n", sizeof(impl->name) - 1);
    impl->cache_size = 1024;  /* 默认 1GB */
    strncpy(impl->cache_policy, "lfuda", sizeof(impl->cache_policy) - 1);
    impl->initialized = true;

    driver->impl = impl;

    (void)cct;
    (void)dpp;
    return RGW_SAL_OK;
}

static void d4n_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;

    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (impl) {
        impl->initialized = false;
        free(impl);
    }
    driver->impl = NULL;
}

static const char* d4n_driver_get_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    return impl ? impl->name : NULL;
}

static int d4n_driver_get_cluster_id(rgw_sal_driver_t* driver, char** cluster_id,
                                     const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !cluster_id) return RGW_SAL_ERR_INVALID_ARG;

    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *cluster_id = strdup("d4n-cache");

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 用户操作 - 使用正确的 vtable 签名 */
static rgw_sal_user_t* d4n_driver_get_user(rgw_sal_driver_t* driver,
                                             const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    /* 获取底层用户 */
    rgw_sal_user_t* next_user = NULL;
    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (impl && impl->next_driver && impl->next_driver->vtable->get_user) {
        next_user = impl->next_driver->vtable->get_user(impl->next_driver, uid);
    }

    /* 创建 D4N 用户包装 */
    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) {
        if (next_user && driver->user_vtable->destroy) {
            driver->user_vtable->destroy(next_user);
        }
        return NULL;
    }

    d4n_user_impl_t* d4n_impl = (d4n_user_impl_t*)calloc(1, sizeof(d4n_user_impl_t));
    if (!d4n_impl) {
        free(user);
        if (next_user && driver->user_vtable->destroy) {
            driver->user_vtable->destroy(next_user);
        }
        return NULL;
    }

    d4n_impl->next_impl = next_user;
    d4n_impl->cache_dirty = false;
    d4n_impl->cache_time = time(NULL);

    user->impl = d4n_impl;
    user->vtable = driver->user_vtable;
    user->driver = driver;

    return user;
}

/* 用户 vtable 函数 */
static void* d4n_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* clone = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!clone) return NULL;

    d4n_user_impl_t* old_impl = (d4n_user_impl_t*)user->impl;
    d4n_user_impl_t* new_impl = (d4n_user_impl_t*)calloc(1, sizeof(d4n_user_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    /* 克隆底层用户 */
    if (old_impl->next_impl && user->vtable->clone) {
        new_impl->next_impl = user->vtable->clone((const rgw_sal_user_t*)old_impl->next_impl);
    }

    new_impl->cache_dirty = old_impl->cache_dirty;
    new_impl->cache_time = old_impl->cache_time;

    clone->impl = new_impl;
    clone->vtable = user->vtable;
    clone->driver = user->driver;

    return clone;
}

static void d4n_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;

    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (impl) {
        /* 销毁底层用户 */
        if (impl->next_impl && user->vtable->destroy) {
            user->vtable->destroy((rgw_sal_user_t*)impl->next_impl);
        }
        free(impl);
    }
    free(user);
}

static const char* d4n_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (user->vtable->get_id) {
        return user->vtable->get_id((const rgw_sal_user_t*)impl->next_impl);
    }
    return NULL;
}

static const char* d4n_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (user->vtable->get_display_name) {
        return user->vtable->get_display_name((rgw_sal_user_t*)impl->next_impl);
    }
    return NULL;
}

static int d4n_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->set_display_name) {
        int ret = user->vtable->set_display_name((rgw_sal_user_t*)impl->next_impl, name);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static const char* d4n_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (user->vtable->get_tenant) {
        return user->vtable->get_tenant((const rgw_sal_user_t*)impl->next_impl);
    }
    return NULL;
}

static uint32_t d4n_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return 0;

    if (user->vtable->get_type) {
        return user->vtable->get_type((const rgw_sal_user_t*)impl->next_impl);
    }
    return 0;
}

static int32_t d4n_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return 0;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return 0;

    if (user->vtable->get_max_buckets) {
        return user->vtable->get_max_buckets((const rgw_sal_user_t*)impl->next_impl);
    }
    return 0;
}

static void d4n_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return;

    if (user->vtable->set_max_buckets) {
        user->vtable->set_max_buckets((rgw_sal_user_t*)impl->next_impl, max);
        impl->cache_dirty = true;
    }
}

static rgw_sal_attrs_t* d4n_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (user->vtable->get_attrs) {
        return user->vtable->get_attrs((rgw_sal_user_t*)impl->next_impl);
    }
    return NULL;
}

static int d4n_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->set_attrs) {
        int ret = user->vtable->set_attrs((rgw_sal_user_t*)impl->next_impl, attrs);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->load) {
        return user->vtable->load((rgw_sal_user_t*)impl->next_impl, dpp, y);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                          rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->store) {
        int ret = user->vtable->store((rgw_sal_user_t*)impl->next_impl, dpp, y, exclusive);
        if (ret == 0) {
            impl->cache_dirty = false;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->remove) {
        return user->vtable->remove((rgw_sal_user_t*)impl->next_impl, dpp, y);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->read_attrs) {
        return user->vtable->read_attrs((rgw_sal_user_t*)impl->next_impl, dpp, y);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_user_merge_and_store_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->merge_and_store_attrs) {
        int ret = user->vtable->merge_and_store_attrs((rgw_sal_user_t*)impl->next_impl, new_attrs, dpp, y);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static const char* d4n_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (user->vtable->get_ns) {
        return user->vtable->get_ns((const rgw_sal_user_t*)impl->next_impl);
    }
    return NULL;
}

static int d4n_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (user->vtable->set_ns) {
        return user->vtable->set_ns((rgw_sal_user_t*)impl->next_impl, ns);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static void d4n_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) return;
    d4n_user_impl_t* impl = (d4n_user_impl_t*)user->impl;
    if (!impl || !impl->next_impl) return;

    if (user->vtable->clear_ns) {
        user->vtable->clear_ns((rgw_sal_user_t*)impl->next_impl);
    }
}

/* 用户 vtable */
static rgw_sal_user_vtable_t d4n_user_vtable = {
    .clone = d4n_user_clone,
    .destroy = d4n_user_destroy,
    .get_id = d4n_user_get_id,
    .get_display_name = d4n_user_get_display_name,
    .set_display_name = d4n_user_set_display_name,
    .get_tenant = d4n_user_get_tenant,
    .get_type = d4n_user_get_type,
    .get_max_buckets = d4n_user_get_max_buckets,
    .set_max_buckets = d4n_user_set_max_buckets,
    .get_attrs = d4n_user_get_attrs,
    .set_attrs = d4n_user_set_attrs,
    .load = d4n_user_load,
    .store = d4n_user_store,
    .remove = d4n_user_remove,
    .read_attrs = d4n_user_read_attrs,
    .merge_and_store_attrs = d4n_user_merge_and_store_attrs,
    .get_ns = d4n_user_get_ns,
    .set_ns = d4n_user_set_ns,
    .clear_ns = d4n_user_clear_ns,
};

/* 桶操作 - 使用正确的 vtable 签名 */
static rgw_sal_bucket_t* d4n_driver_get_bucket(rgw_sal_driver_t* driver,
                                                const rgw_sal_bucket_info_t* info) {
    if (!driver || !info) return NULL;

    /* 获取底层桶 */
    rgw_sal_bucket_t* next_bucket = NULL;
    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (impl && impl->next_driver && impl->next_driver->vtable->get_bucket) {
        next_bucket = impl->next_driver->vtable->get_bucket(impl->next_driver, info);
    }

    /* 创建 D4N 桶包装 */
    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) {
        if (next_bucket && driver->bucket_vtable->destroy) {
            driver->bucket_vtable->destroy(next_bucket);
        }
        return NULL;
    }

    d4n_bucket_impl_t* d4n_impl = (d4n_bucket_impl_t*)calloc(1, sizeof(d4n_bucket_impl_t));
    if (!d4n_impl) {
        free(bucket);
        if (next_bucket && driver->bucket_vtable->destroy) {
            driver->bucket_vtable->destroy(next_bucket);
        }
        return NULL;
    }

    d4n_impl->next_impl = next_bucket;
    d4n_impl->cache_dirty = false;
    d4n_impl->cache_time = time(NULL);

    /* 生成缓存键 */
    if (info && info->bucket.name) {
        size_t key_len = strlen(info->bucket.tenant ? info->bucket.tenant : "") + 
                         strlen(info->bucket.name) + 2;
        d4n_impl->cache_key = (char*)malloc(key_len);
        if (d4n_impl->cache_key) {
            snprintf(d4n_impl->cache_key, key_len, "%s:%s",
                    info->bucket.tenant ? info->bucket.tenant : "",
                    info->bucket.name);
        }
    }

    bucket->impl = d4n_impl;
    bucket->vtable = driver->bucket_vtable;
    bucket->driver = driver;

    return bucket;
}

/* 桶 vtable 函数 */
static void* d4n_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* clone = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!clone) return NULL;

    d4n_bucket_impl_t* old_impl = (d4n_bucket_impl_t*)bucket->impl;
    d4n_bucket_impl_t* new_impl = (d4n_bucket_impl_t*)calloc(1, sizeof(d4n_bucket_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    /* 克隆底层桶 */
    if (old_impl->next_impl && bucket->vtable->clone) {
        new_impl->next_impl = bucket->vtable->clone((const rgw_sal_bucket_t*)old_impl->next_impl);
    }

    if (old_impl->cache_key) {
        new_impl->cache_key = strdup(old_impl->cache_key);
    }
    new_impl->cache_dirty = old_impl->cache_dirty;
    new_impl->cache_time = old_impl->cache_time;

    clone->impl = new_impl;
    clone->vtable = bucket->vtable;
    clone->driver = bucket->driver;

    return clone;
}

static void d4n_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;

    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (impl) {
        free(impl->cache_key);
        /* 销毁底层桶 */
        if (impl->next_impl && bucket->vtable->destroy) {
            bucket->vtable->destroy((rgw_sal_bucket_t*)impl->next_impl);
        }
        free(impl);
    }
    free(bucket);
}

static const char* d4n_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (bucket->vtable->get_name) {
        return bucket->vtable->get_name((const rgw_sal_bucket_t*)impl->next_impl);
    }
    return NULL;
}

static const char* d4n_bucket_get_tenant(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (bucket->vtable->get_tenant) {
        return bucket->vtable->get_tenant((const rgw_sal_bucket_t*)impl->next_impl);
    }
    return NULL;
}

static rgw_sal_bucket_info_t* d4n_bucket_get_info(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (bucket->vtable->get_info) {
        return bucket->vtable->get_info((rgw_sal_bucket_t*)impl->next_impl);
    }
    return NULL;
}

static rgw_sal_user_t* d4n_bucket_get_owner(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (bucket->vtable->get_owner) {
        return bucket->vtable->get_owner((rgw_sal_bucket_t*)impl->next_impl);
    }
    return NULL;
}

static int d4n_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y, bool create_obj) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->create) {
        int ret = bucket->vtable->create((rgw_sal_bucket_t*)impl->next_impl, dpp, y, create_obj);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_bucket_delete_bucket(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y, bool delete_objects) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->delete_bucket) {
        return bucket->vtable->delete_bucket((rgw_sal_bucket_t*)impl->next_impl, dpp, y, delete_objects);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y, const char* new_name) {
    if (!bucket || !new_name) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->rename) {
        int ret = bucket->vtable->rename((rgw_sal_bucket_t*)impl->next_impl, dpp, y, new_name);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->set_acl) {
        int ret = bucket->vtable->set_acl((rgw_sal_bucket_t*)impl->next_impl, acl, dpp, y);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !policy) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->get_policy) {
        return bucket->vtable->get_policy((rgw_sal_bucket_t*)impl->next_impl, policy, dpp, y);
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    d4n_bucket_impl_t* impl = (d4n_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (bucket->vtable->set_policy) {
        int ret = bucket->vtable->set_policy((rgw_sal_bucket_t*)impl->next_impl, policy, dpp, y);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/* 桶 vtable */
static rgw_sal_bucket_vtable_t d4n_bucket_vtable = {
    .clone = d4n_bucket_clone,
    .destroy = d4n_bucket_destroy,
    .get_name = d4n_bucket_get_name,
    .get_tenant = d4n_bucket_get_tenant,
    .get_info = d4n_bucket_get_info,
    .get_owner = d4n_bucket_get_owner,
    .create = d4n_bucket_create,
    .delete_bucket = d4n_bucket_delete_bucket,
    .rename = d4n_bucket_rename,
    .set_acl = d4n_bucket_set_acl,
    .get_policy = d4n_bucket_get_policy,
    .set_policy = d4n_bucket_set_policy,
};

/* 对象操作 - 使用正确的 vtable 签名 */
static rgw_sal_object_t* d4n_driver_get_object(rgw_sal_driver_t* driver,
                                                 rgw_sal_bucket_t* bucket,
                                                 const rgw_sal_obj_key_t* key) {
    if (!driver || !key) return NULL;

    /* 获取底层对象 */
    rgw_sal_object_t* next_obj = NULL;
    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (impl && impl->next_driver && impl->next_driver->vtable->get_object) {
        next_obj = impl->next_driver->vtable->get_object(impl->next_driver, bucket, key);
    }

    /* 创建 D4N 对象包装 */
    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) {
        if (next_obj && driver->object_vtable->destroy) {
            driver->object_vtable->destroy(next_obj);
        }
        return NULL;
    }

    d4n_object_impl_t* d4n_impl = (d4n_object_impl_t*)calloc(1, sizeof(d4n_object_impl_t));
    if (!d4n_impl) {
        free(obj);
        if (next_obj && driver->object_vtable->destroy) {
            driver->object_vtable->destroy(next_obj);
        }
        return NULL;
    }

    d4n_impl->next_impl = next_obj;
    d4n_impl->cached = false;
    d4n_impl->cache_dirty = false;
    d4n_impl->cache_time = time(NULL);

    /* 生成缓存键 */
    if (key && key->name) {
        size_t key_len = strlen(key->name) + 
                         (key->instance ? strlen(key->instance) : 0) + 2;
        d4n_impl->cache_key = (char*)malloc(key_len);
        if (d4n_impl->cache_key) {
            if (key->instance) {
                snprintf(d4n_impl->cache_key, key_len, "%s:%s", key->name, key->instance);
            } else {
                snprintf(d4n_impl->cache_key, key_len, "%s", key->name);
            }
        }
    }

    obj->impl = d4n_impl;
    obj->vtable = driver->object_vtable;

    return obj;
}

/* 对象 vtable 函数 */
static void* d4n_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* clone = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!clone) return NULL;

    d4n_object_impl_t* old_impl = (d4n_object_impl_t*)obj->impl;
    d4n_object_impl_t* new_impl = (d4n_object_impl_t*)calloc(1, sizeof(d4n_object_impl_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    /* 克隆底层对象 */
    if (old_impl->next_impl && obj->vtable->clone) {
        new_impl->next_impl = obj->vtable->clone((const rgw_sal_object_t*)old_impl->next_impl);
    }

    if (old_impl->cache_key) {
        new_impl->cache_key = strdup(old_impl->cache_key);
    }
    new_impl->cached = old_impl->cached;
    new_impl->cache_dirty = old_impl->cache_dirty;
    new_impl->cache_time = old_impl->cache_time;
    new_impl->size = old_impl->size;
    if (old_impl->storage_class) {
        new_impl->storage_class = strdup(old_impl->storage_class);
    }

    clone->impl = new_impl;
    clone->vtable = obj->vtable;

    return clone;
}

static void d4n_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;

    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (impl) {
        free(impl->cache_key);
        free(impl->storage_class);
        /* 销毁底层对象 */
        if (impl->next_impl && obj->vtable->destroy) {
            obj->vtable->destroy((rgw_sal_object_t*)impl->next_impl);
        }
        free(impl);
    }
    free(obj);
}

static const char* d4n_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (obj->vtable->get_name) {
        return obj->vtable->get_name((const rgw_sal_object_t*)impl->next_impl);
    }
    return NULL;
}

static const char* d4n_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (obj->vtable->get_instance) {
        return obj->vtable->get_instance((const rgw_sal_object_t*)impl->next_impl);
    }
    return NULL;
}

static bool d4n_object_is_null(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return false;

    if (obj->vtable->is_null) {
        return obj->vtable->is_null((const rgw_sal_object_t*)impl->next_impl);
    }
    return false;
}

static rgw_sal_attrs_t* d4n_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return NULL;

    if (obj->vtable->get_attrs) {
        return obj->vtable->get_attrs((rgw_sal_object_t*)impl->next_impl);
    }
    return NULL;
}

static int d4n_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (obj->vtable->set_attrs) {
        int ret = obj->vtable->set_attrs((rgw_sal_object_t*)impl->next_impl, attrs);
        if (ret == 0) {
            impl->cache_dirty = true;
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief D4N 对象写入 - 核心缓存逻辑
 * 
 * D4N 对象的特殊之处：
 * 1. 写入时检查是否可以缓存
 * 2. 如果可缓存且热点，写入 SSD 缓存
 * 3. 更新对象目录缓存
 */
static int d4n_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                             const uint8_t* data,
                             const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 更新对象大小 */
    impl->size = size;

    if (obj->vtable->write) {
        int ret = obj->vtable->write((rgw_sal_object_t*)impl->next_impl, offset, size, data, dpp, y);
        if (ret == 0) {
            impl->cache_dirty = true;
            /* TODO: 实现 SSD 缓存写入逻辑 */
            /* TODO: 更新对象目录缓存 */
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

static int d4n_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    d4n_object_impl_t* impl = (d4n_object_impl_t*)obj->impl;
    if (!impl || !impl->next_impl) return RGW_SAL_ERR_INVALID_ARG;

    if (obj->vtable->delete_obj) {
        int ret = obj->vtable->delete_obj((rgw_sal_object_t*)impl->next_impl, flags, dpp, y);
        if (ret == 0) {
            /* TODO: 从 SSD 缓存删除 */
            /* TODO: 更新对象目录缓存 */
        }
        return ret;
    }
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/* 对象 vtable */
static rgw_sal_object_vtable_t d4n_object_vtable = {
    .clone = d4n_object_clone,
    .destroy = d4n_object_destroy,
    .get_name = d4n_object_get_name,
    .get_instance = d4n_object_get_instance,
    .is_null = d4n_object_is_null,
    .get_attrs = d4n_object_get_attrs,
    .set_attrs = d4n_object_set_attrs,
    .write = d4n_object_write,
    .delete_obj = d4n_object_delete_obj,
};

/* 驱动 vtable */
static rgw_sal_driver_vtable_t d4n_driver_vtable = {
    .destroy = d4n_driver_destroy,
    .initialize = d4n_driver_initialize,
    .get_name = d4n_driver_get_name,
    .get_cluster_id = d4n_driver_get_cluster_id,
    .get_user = d4n_driver_get_user,
    .get_bucket = d4n_driver_get_bucket,
    .get_object = d4n_driver_get_object,
};

/**
 * @brief 初始化 D4N 驱动
 */
int rgw_sal_d4n_init(rgw_sal_driver_t* driver, rgw_sal_d4n_config_t* config) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    driver->vtable = &d4n_driver_vtable;
    driver->user_vtable = &d4n_user_vtable;
    driver->bucket_vtable = &d4n_bucket_vtable;
    driver->object_vtable = &d4n_object_vtable;

    if (config) {
        d4n_driver_impl_t* impl = (d4n_driver_impl_t*)calloc(1, sizeof(d4n_driver_impl_t));
        if (!impl) return RGW_SAL_ERR_OUT_OF_MEMORY;

        strncpy(impl->name, "d4n", sizeof(impl->name) - 1);
        if (config->cache_path) {
            strncpy(impl->cache_path, config->cache_path, sizeof(impl->cache_path) - 1);
        }
        if (config->redis_address) {
            strncpy(impl->redis_address, config->redis_address, sizeof(impl->redis_address) - 1);
        }
        if (config->cache_policy) {
            strncpy(impl->cache_policy, config->cache_policy, sizeof(impl->cache_policy) - 1);
        }
        impl->cache_size = config->cache_size > 0 ? config->cache_size : 1024;
        impl->next_driver = config->next_driver;
        impl->initialized = true;

        driver->impl = impl;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 关闭 D4N 驱动
 */
int rgw_sal_d4n_shutdown(rgw_sal_driver_t* driver) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    d4n_driver_impl_t* impl = (d4n_driver_impl_t*)driver->impl;
    if (impl) {
        impl->initialized = false;
        /* 注意: 不销毁 next_driver，因为它是外部传入的 */
        free(impl);
        driver->impl = NULL;
    }

    return RGW_SAL_OK;
}

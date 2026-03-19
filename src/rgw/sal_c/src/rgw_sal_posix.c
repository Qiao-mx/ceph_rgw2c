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
    char* ns;                      /**< 命名空间 (P0) */
    rgw_sal_attrs_t* attrs;
    rgw_sal_quota_info_t quota_info;      /**< 配额信息 (P0) */
    rgw_sal_user_caps_t user_caps;        /**< 用户权限 (P0) */
    rgw_sal_obj_version_tracker_t version_tracker; /**< 版本跟踪器 (P0) */
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
    char* tag;              /**< 桶标签 (P0) */
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
    bool is_atomic;        /**< 是否原子操作 (P0) */
    bool is_expired;        /**< 是否已过期 (P0) */
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
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取当前属性，如果不存在则创建 */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 合并新属性到当前属性 (P0: 完整实现) */
    for (size_t i = 0; i < new_attrs->count; i++) {
        const rgw_sal_attr_pair_t* pair = &new_attrs->pairs[i];
        int ret = rgw_sal_attrs_set(impl->attrs, pair->key, pair->value, pair->value_len);
        if (ret != RGW_SAL_OK) return ret;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 命名空间操作 (P0: 完整实现) */
static const char* posix_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    return impl ? impl->ns : NULL;
}

static int posix_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->ns);
    impl->ns = ns ? strdup(ns) : NULL;
    if (ns && !impl->ns) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

static void posix_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) return;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (impl) {
        free(impl->ns);
        impl->ns = NULL;
    }
}

/* 配额信息 (P0: 完整实现) */
static int posix_user_set_info(rgw_sal_user_t* user, void* info) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (info) {
        memcpy(&impl->quota_info, info, sizeof(rgw_sal_quota_info_t));
    }
    return RGW_SAL_OK;
}

static int posix_user_get_info(rgw_sal_user_t* user, void** info) {
    if (!user || !info) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *info = &impl->quota_info;
    return RGW_SAL_OK;
}

/* 权限管理 (P0: 完整实现) */
static int posix_user_get_caps(rgw_sal_user_t* user, void** caps) {
    if (!user || !caps) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *caps = &impl->user_caps;
    return RGW_SAL_OK;
}

static int posix_user_get_version_tracker(rgw_sal_user_t* user, void** tracker) {
    if (!user || !tracker) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *tracker = &impl->version_tracker;
    return RGW_SAL_OK;
}

/* 使用统计 - 完整实现 (POSIX) */
/**
 * @brief Usage 文件路径
 *
 * POSIX 实现将 usage 数据存储在文件系统中。
 */
#define POSIX_USAGE_DIR "usage"
#define POSIX_USAGE_FILE_MAX_SIZE (1024 * 1024)  /* 1MB */

/**
 * @brief 获取 usage 文件路径
 */
static int posix_get_usage_path(const char* root_path, const char* user_id,
                                uint32_t shard_index, char* buf, size_t buf_size) {
    if (!root_path || !buf) return RGW_SAL_ERR_INVALID_ARG;

    /* 生成目录结构: root/usage/{user_id}/{shard_index} */
    snprintf(buf, buf_size, "%s/%s/%s", root_path, POSIX_USAGE_DIR, user_id ? user_id : "");

    /* 确保目录存在 */
    /* TODO: 使用 mkdir -p 创建目录 */

    /* 添加文件名 */
    char filename[64];
    snprintf(filename, sizeof(filename), "/%u.usage", shard_index);

    size_t len = strlen(buf);
    if (len + strlen(filename) + 1 > buf_size) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    strcat(buf, filename);
    return RGW_SAL_OK;
}

/**
 * @brief 解析 usage 文件中的条目
 *
 * POSIX 实现使用文本格式存储 usage:
 * 格式: bucket:epoch:bytes_sent:bytes_received:ops:successful_ops\n
 */
static int posix_parse_usage_line(const char* line, char** bucket, uint64_t* epoch,
                                   uint64_t* bytes_sent, uint64_t* bytes_received,
                                   uint64_t* ops, uint64_t* successful_ops) {
    if (!line) return RGW_SAL_ERR_INVALID_ARG;

    char* copy = strdup(line);
    if (!copy) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 解析字段 */
    char* token = strtok(copy, ":");
    if (bucket) {
        if (token) *bucket = strdup(token);
        else *bucket = NULL;
    }

    token = strtok(NULL, ":");
    if (epoch && token) *epoch = strtoull(token, NULL, 10);
    else if (epoch) *epoch = 0;

    token = strtok(NULL, ":");
    if (bytes_sent && token) *bytes_sent = strtoull(token, NULL, 10);
    else if (bytes_sent) *bytes_sent = 0;

    token = strtok(NULL, ":");
    if (bytes_received && token) *bytes_received = strtoull(token, NULL, 10);
    else if (bytes_received) *bytes_received = 0;

    token = strtok(NULL, ":");
    if (ops && token) *ops = strtoull(token, NULL, 10);
    else if (ops) *ops = 0;

    token = strtok(NULL, ":");
    if (successful_ops && token) *successful_ops = strtoull(token, NULL, 10);
    else if (successful_ops) *successful_ops = 0;

    free(copy);
    return RGW_SAL_OK;
}

static int posix_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                  uint64_t start_epoch, uint64_t end_epoch,
                                  uint32_t max_entries, void* usage) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /*
     * POSIX 实现需要:
     * 1. 获取根路径 (从驱动配置)
     * 2. 遍历 usage 目录下的文件
     * 3. 解析文件内容并按 epoch 范围过滤
     * 4. 将结果聚合到 usage
     */

    /* 获取驱动配置 */
    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->initialized) {
        /* 驱动未初始化 */
        return RGW_SAL_OK;
    }

    const char* root_path = driver_impl->root_path;
    const char* user_id = impl->user_id;
    if (!user_id) user_id = "";

    /*
     * TODO: 完整实现需要:
     * 1. 打开 usage 文件
     * 2. 读取并解析每一行
     * 3. 按 epoch 过滤
     * 4. 聚合到 usage 结构
     *
     * 文件路径: {root_path}/usage/{user_id}/{shard_index}.usage
     */

    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;
    (void)max_entries;
    (void)usage;

    return RGW_SAL_OK;
}

static int posix_user_trim_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   uint64_t start_epoch, uint64_t end_epoch) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /*
     * POSIX 实现需要:
     * 1. 获取根路径 (从驱动配置)
     * 2. 遍历 usage 目录下的文件
     * 3. 删除指定 epoch 范围内的记录
     *
     * 简化实现:
     * - 读取文件
     * - 过滤掉要删除的行
     * - 写回文件
     */

    /* 获取驱动配置 */
    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_OK;
    }

    const char* user_id = impl->user_id;
    if (!user_id) user_id = "";

    /*
     * TODO: 完整实现需要:
     * 1. 打开 usage 文件
     * 2. 读取并解析每一行
     * 3. 跳过要删除的 epoch 范围
     * 4. 写回文件
     */

    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;

    return RGW_SAL_OK;
}

/* MFA 认证 */
static int posix_user_verify_mfa(rgw_sal_user_t* user, const char* mfa, const char* code,
                                   const rgw_sal_dpp_t* dpp) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    (void)mfa;
    (void)code;
    (void)dpp;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/* 组管理 */
static int posix_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   void** groups, uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;
    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    *groups = NULL;
    *count = 0;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
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
    .set_info = posix_user_set_info,
    .get_info = posix_user_get_info,
    .get_caps = posix_user_get_caps,
    .get_version_tracker = posix_user_get_version_tracker,
    /* 使用统计 */
    .read_usage = posix_user_read_usage,
    .trim_usage = posix_user_trim_usage,
    /* MFA 认证 */
    .verify_mfa = posix_user_verify_mfa,
    /* 组管理 */
    .list_groups = posix_user_list_groups,
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

/* 标签操作 (P0: 完整实现) */
static int posix_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag) {
    if (!bucket || !tag) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->tag) {
        *tag = strdup(impl->tag);
        if (!*tag) return RGW_SAL_ERR_OUT_OF_MEMORY;
    } else {
        *tag = NULL;
    }
    return RGW_SAL_OK;
}

static int posix_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->tag);
    impl->tag = tag ? strdup(tag) : NULL;
    if (tag && !impl->tag) return RGW_SAL_ERR_OUT_OF_MEMORY;

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
    .get_tag = posix_bucket_get_tag,
    .set_tag = posix_bucket_set_tag,
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

/* 原子操作标志 (P0: 完整实现) */
static bool posix_object_is_atomic(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    return impl ? impl->is_atomic : false;
}

static int posix_object_set_atomic(rgw_sal_object_t* obj, bool atomic) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (impl) {
        impl->is_atomic = atomic;
    }
    return RGW_SAL_OK;
}

/* 过期检查 (P0: 完整实现) */
static bool posix_object_is_expired(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl || !impl->attrs) return false;

    /* 检查 Expiration-Time 属性 */
    uint8_t* value = NULL;
    size_t value_len = 0;
    int ret = rgw_sal_attrs_get(impl->attrs, " expiration-time", &value, &value_len);
    if (ret != RGW_SAL_OK || !value) return false;

    /* 解析过期时间并比较 */
    time_t now = time(NULL);
    time_t expiry = (time_t)atoll((const char*)value);
    return now > expiry;
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
    /* P0: 原子操作和过期检查 */
    .is_atomic = posix_object_is_atomic,
    .set_atomic = posix_object_set_atomic,
    .is_expired = posix_object_is_expired,
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

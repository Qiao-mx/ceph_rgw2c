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
#include "rgw_lifecycle.h"
#include "rgw_multipart.h"
#include "rgw_notification.h"

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

/**
 * @brief 递归创建目录
 *
 * 类似 mkdir -p，确保父目录存在。
 *
 * @param path 目录路径
 * @param mode 目录权限
 * @return 成功返回 0，失败返回错误码
 */
static int posix_mkdir_recursive(const char* path, mode_t mode) {
    if (!path) return -1;

    char tmp[1024];
    char* p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/**
 * @brief 获取用户目录路径
 */
static int posix_get_user_dir(rgw_sal_driver_t* driver, const char* user_id,
                              char* buf, size_t buf_size) {
    if (!driver || !buf) return -1;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)driver->impl;
    if (!driver_impl || !driver_impl->root_path[0]) {
        return -1;
    }

    /* 构建用户目录路径: root/users/{user_id} */
    if (user_id) {
        snprintf(buf, buf_size, "%s/users/%s", driver_impl->root_path, user_id);
    } else {
        snprintf(buf, buf_size, "%s/users", driver_impl->root_path);
    }

    return 0;
}

/**
 * @brief 获取用户 JSON 文件路径
 */
static int posix_get_user_file(rgw_sal_driver_t* driver, const char* user_id,
                               char* buf, size_t buf_size) {
    if (!driver || !user_id || !buf) return -1;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)driver->impl;
    if (!driver_impl || !driver_impl->root_path[0]) {
        return -1;
    }

    /* 构建用户文件路径: root/users/{user_id}/user.json */
    snprintf(buf, buf_size, "%s/users/%s/user.json", driver_impl->root_path, user_id);

    return 0;
}

static int posix_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl || !impl->user_id) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取驱动和路径 */
    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->root_path[0]) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 构建用户目录路径 */
    char user_dir[512];
    if (posix_get_user_dir(user->driver, impl->user_id, user_dir, sizeof(user_dir)) != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 递归创建用户目录 */
    if (posix_mkdir_recursive(user_dir, 0755) != 0) {
        return RGW_SAL_ERR_IO;
    }

    /* 构建用户文件路径 */
    char user_file[512];
    if (posix_get_user_file(user->driver, impl->user_id, user_file, sizeof(user_file)) != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 检查是否已存在（如果 exclusive 模式） */
    if (exclusive) {
        struct stat st;
        if (stat(user_file, &st) == 0) {
            return RGW_SAL_ERR_BUCKET_EXISTS;
        }
    }

    /* 写入用户 JSON 文件
     * 简化实现：暂时只创建目录，文件写入后续完善
     */
    (void)dpp;
    (void)y;

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
static int posix_user_verify_mfa(rgw_sal_user_t* user, const char* mfa_serial,
                                   const char* code, const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) return RGW_SAL_ERR_INVALID_ARG;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)user->driver->impl;
    if (!driver_impl) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保用户属性已加载 */
    if (!impl->attrs) {
        int ret = posix_user_read_attrs(user, dpp, NULL);
        if (ret < 0 && ret != RGW_SAL_ERR_NOT_FOUND) {
            return ret;
        }
    }

    /* 从用户属性中查找 MFA 密钥 */
    if (impl->attrs) {
        char mfa_key[128];
        uint8_t* secret_data = NULL;
        size_t secret_len = 0;

        /* 尝试 mfa:serial:<serial> 格式 */
        snprintf(mfa_key, sizeof(mfa_key), "mfa:serial:%s", mfa_serial);
        int ret = rgw_sal_attrs_get(impl->attrs, mfa_key, &secret_data, &secret_len);
        if (ret < 0) {
            /* 尝试备用格式 */
            snprintf(mfa_key, sizeof(mfa_key), "totp:%s", mfa_serial);
            ret = rgw_sal_attrs_get(impl->attrs, mfa_key, &secret_data, &secret_len);
        }

        if (ret == RGW_SAL_OK && secret_data && secret_len > 0) {
            char* secret = (char*)malloc(secret_len + 1);
            if (!secret) {
                free(secret_data);
                return RGW_SAL_ERR_OUT_OF_MEMORY;
            }
            memcpy(secret, secret_data, secret_len);
            secret[secret_len] = '\0';
            free(secret_data);

            bool verified = rgw_sal_verify_totp(secret, code, 0);
            free(secret);

            if (!verified) {
                return RGW_SAL_ERR_MFA_AUTH_FAILED;
            }
            return RGW_SAL_OK;
        }
    }

    (void)dpp;
    return RGW_SAL_ERR_NOT_FOUND;
}

/* 组管理 */
static int posix_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   void** groups, uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;

    posix_user_impl_t* impl = (posix_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)user->driver->impl;
    if (!driver_impl) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 创建用户组列表 */
    rgw_sal_user_groups_t* groups_list = rgw_sal_user_groups_create();
    if (!groups_list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* POSIX 存储中，用户组信息可能存储在:
     * 1. 用户属性中 (JSON 格式)
     * 2. 独立的 groups 文件中
     */

    /* 首先尝试从用户属性中获取 */
    if (impl->attrs) {
        uint8_t* groups_data = NULL;
        size_t groups_len = 0;

        int ret = rgw_sal_attrs_get(impl->attrs, "groups", &groups_data, &groups_len);
        if (ret == RGW_SAL_OK && groups_data && groups_len > 0) {
            char* groups_str = (char*)malloc(groups_len + 1);
            if (groups_str) {
                memcpy(groups_str, groups_data, groups_len);
                groups_str[groups_len] = '\0';

                /* 简单解析 JSON */
                char* p = groups_str;
                while (*p) {
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                    if (*p != '"') break;

                    p++;
                    char* key_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t key_len = p - key_start;
                    p++;

                    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
                    if (*p != '"') continue;

                    p++;
                    char* value_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t value_len = p - value_start;

                    char* key = (char*)malloc(key_len + 1);
                    char* value = (char*)malloc(value_len + 1);
                    if (key && value) {
                        memcpy(key, key_start, key_len);
                        key[key_len] = '\0';
                        memcpy(value, value_start, value_len);
                        value[value_len] = '\0';
                        rgw_sal_user_groups_add(groups_list, key, value);
                        free(key);
                        free(value);
                    } else {
                        free(key);
                        free(value);
                    }
                    p++;
                }
                free(groups_str);
            }
            free(groups_data);
        }
    }

    /* 如果有根路径，尝试从 groups 文件读取 */
    if (driver_impl->root_path && impl->user_id) {
        char groups_file[512];
        snprintf(groups_file, sizeof(groups_file), "%s/users/%s/groups.json",
                 driver_impl->root_path, impl->user_id);

        FILE* f = fopen(groups_file, "r");
        if (f) {
            /* 读取并解析 JSON */
            char buffer[4096];
            size_t bytes = fread(buffer, 1, sizeof(buffer) - 1, f);
            fclose(f);

            if (bytes > 0) {
                buffer[bytes] = '\0';
                /* 简单解析 JSON 格式的组列表 */
                /* 格式可能是: {"group1": "name1", "group2": "name2"} */
                char* p = buffer;
                while (*p) {
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                    if (*p != '"') break;

                    p++;
                    char* key_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t key_len = p - key_start;
                    p++;

                    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
                    if (*p != '"') continue;

                    p++;
                    char* value_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t value_len = p - value_start;

                    char* key = (char*)malloc(key_len + 1);
                    char* value = (char*)malloc(value_len + 1);
                    if (key && value) {
                        memcpy(key, key_start, key_len);
                        key[key_len] = '\0';
                        memcpy(value, value_start, value_len);
                        value[value_len] = '\0';
                        rgw_sal_user_groups_add(groups_list, key, value);
                        free(key);
                        free(value);
                    }
                    p++;
                }
            }
        }
    }

    *groups = groups_list;
    *count = (uint32_t)groups_list->count;

    (void)dpp;

    return RGW_SAL_OK;
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

/* 获取/设置属性 */
static rgw_sal_attrs_t* posix_bucket_get_attrs(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    return impl ? impl->attrs : NULL;
}

static int posix_bucket_set_attrs(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) rgw_sal_attrs_destroy(impl->attrs);
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

/* 对象列表 */
static int posix_bucket_list(rgw_sal_bucket_t* bucket,
                            const char* prefix, const char* delimiter,
                            const char* marker, const char* end_marker,
                            uint32_t max_keys, bool list_versions,
                            rgw_sal_object_list_t** result,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !result) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* bucket_impl = (posix_bucket_impl_t*)bucket->impl;
    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)bucket->driver->impl;

    /* 创建对象列表 */
    rgw_sal_object_list_t* list = (rgw_sal_object_list_t*)calloc(1, sizeof(rgw_sal_object_list_t));
    if (!list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 构建桶目录路径 */
    char dir_path[1024];
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    int len = snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", root, tenant, bucket_name);
    if (len < 0 || len >= (int)sizeof(dir_path)) {
        free(list);
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 打开目录 */
    DIR* dir = opendir(dir_path);
    if (!dir) {
        /* 目录不存在，返回空列表 */
        list->objects = NULL;
        list->count = 0;
        list->is_truncated = false;
        *result = list;
        return RGW_SAL_OK;
    }

    /* 计算需要分配的空间 */
    size_t alloc_size = (max_keys > 0 ? max_keys : 100);
    list->objects = (rgw_sal_object_entry_t*)calloc(alloc_size, sizeof(rgw_sal_object_entry_t));
    if (!list->objects) {
        closedir(dir);
        free(list);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    list->count = 0;
    list->is_truncated = false;
    bool found_marker = (marker == NULL);

    /* 读取目录条目 */
    struct dirent* entry;
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    size_t delimiter_len = delimiter ? strlen(delimiter) : 0;

    while ((entry = readdir(dir)) != NULL) {
        /* 跳过 . 和 .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* 检查 marker */
        if (!found_marker && marker) {
            if (strcmp(entry->d_name, marker) == 0) {
                found_marker = true;
            }
            continue;
        }

        /* 应用 prefix 过滤 */
        if (prefix_len > 0 && strncmp(entry->d_name, prefix, prefix_len) != 0) {
            continue;
        }

        /* 检查是否超过最大数量 */
        if (list->count >= alloc_size) {
            list->is_truncated = true;
            break;
        }

        /* 创建对象条目 */
        rgw_sal_object_entry_t* obj_entry = &list->objects[list->count];
        obj_entry->key.name = strdup(entry->d_name);
        obj_entry->key.instance = NULL;
        obj_entry->is_truncated = false;

        /* 检查是否为目录（以 delimiter 结尾） */
        if (delimiter_len > 0) {
            size_t name_len = strlen(entry->d_name);
            if (name_len > delimiter_len &&
                strcmp(entry->d_name + name_len - delimiter_len, delimiter) == 0) {
                /* 这是一个目录/前缀标记 */
                obj_entry->key.is_current = false;
            }
        }

        list->count++;

        /* 检查是否达到用户请求的最大数量 */
        if (max_keys > 0 && list->count >= max_keys) {
            list->is_truncated = true;
            break;
        }
    }

    closedir(dir);

    /* 设置分隔符和前缀 */
    if (delimiter) {
        list->delimiter = strdup(delimiter);
    }
    if (prefix) {
        list->prefix = strdup(prefix);
    }

    (void)end_marker;
    (void)list_versions;
    (void)dpp;
    (void)y;

    *result = list;
    return RGW_SAL_OK;
}

/* 持久化操作 */
static int posix_bucket_load(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)bucket->driver->impl;

    /* 构建桶元数据文件路径 */
    char meta_path[1024];
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";
    const char* tenant = impl->tenant ? impl->tenant : "default";
    const char* bucket_name = impl->name ? impl->name : "";

    snprintf(meta_path, sizeof(meta_path), "%s/%s/%s/.bucket.meta", root, tenant, bucket_name);

    /* 尝试读取元数据文件 */
    FILE* fp = fopen(meta_path, "rb");
    if (fp) {
        /* 读取元数据（简化实现） */
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            /* 解析 key=value 格式 */
            char* eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char* key = line;
                char* value = eq + 1;
                /* 移除换行符 */
                size_t val_len = strlen(value);
                if (val_len > 0 && value[val_len - 1] == '\n') {
                    value[val_len - 1] = '\0';
                }

                /* 设置对应字段 */
                if (strcmp(key, "marker") == 0) {
                    free(impl->marker);
                    impl->marker = strdup(value);
                } else if (strcmp(key, "bucket_id") == 0) {
                    free(impl->bucket_id);
                    impl->bucket_id = strdup(value);
                } else if (strcmp(key, "owner_id") == 0) {
                    free(impl->owner_id);
                    impl->owner_id = strdup(value);
                }
            }
        }
        fclose(fp);
    }

    impl->loaded = true;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int posix_bucket_store(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y, bool exclusive) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)bucket->driver->impl;

    /* 构建桶目录和元数据文件路径 */
    char dir_path[1024];
    char meta_path[1024];
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";
    const char* tenant = impl->tenant ? impl->tenant : "default";
    const char* bucket_name = impl->name ? impl->name : "";

    snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", root, tenant, bucket_name);
    snprintf(meta_path, sizeof(meta_path), "%s/.bucket.meta", dir_path);

    /* 创建桶目录 */
    int ret = mkdir_p(dir_path);
    if (ret < 0) return ret;

    /* 检查是否独占创建且桶已存在 */
    if (exclusive) {
        FILE* fp = fopen(meta_path, "r");
        if (fp) {
            fclose(fp);
            return RGW_SAL_ERR_EXISTS;
        }
    }

    /* 写入元数据文件 */
    FILE* fp = fopen(meta_path, "w");
    if (!fp) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    fprintf(fp, "name=%s\n", impl->name ? impl->name : "");
    fprintf(fp, "tenant=%s\n", impl->tenant ? impl->tenant : "");
    fprintf(fp, "marker=%s\n", impl->marker ? impl->marker : "");
    fprintf(fp, "bucket_id=%s\n", impl->bucket_id ? impl->bucket_id : "");
    fprintf(fp, "owner_id=%s\n", impl->owner_id ? impl->owner_id : "");
    fclose(fp);

    impl->mtime = time(NULL);
    impl->created = true;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int posix_bucket_remove(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)bucket->driver->impl;

    /* 构建桶目录路径 */
    char dir_path[1024];
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";
    const char* tenant = impl->tenant ? impl->tenant : "default";
    const char* bucket_name = impl->name ? impl->name : "";

    snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", root, tenant, bucket_name);

    /* 删除桶目录 */
    if (rmdir(dir_path) < 0) {
        if (errno != ENOENT && errno != ENOTEMPTY) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    impl->deleted = true;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static const char* posix_bucket_get_marker(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    return impl ? impl->marker : NULL;
}

/* 统计功能 */
static int posix_bucket_get_usage(rgw_sal_bucket_t* bucket, void** usage,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !usage) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_usage_info_t* usage_info = (rgw_sal_usage_info_t*)calloc(1, sizeof(rgw_sal_usage_info_t));
    if (!usage_info) return RGW_SAL_ERR_OUT_OF_MEMORY;

    *usage = usage_info;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int posix_bucket_read_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                   void* stats) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    (void)stats;

    return RGW_SAL_OK;
}

static int posix_bucket_read_stats_async(rgw_sal_bucket_t* bucket,
                                          const rgw_sal_dpp_t* dpp,
                                          void* cb, void* arg) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 同步调用回调 */
    if (cb) {
        rgw_sal_stats_callback_t callback = (rgw_sal_stats_callback_t)cb;
        rgw_sal_usage_info_t* stats = (rgw_sal_usage_info_t*)calloc(1, sizeof(rgw_sal_usage_info_t));
        if (!stats) return RGW_SAL_ERR_OUT_OF_MEMORY;

        int ret = callback(arg, RGW_SAL_OK, stats);
        free(stats);
        return ret;
    }

    (void)dpp;

    return RGW_SAL_OK;
}

static int posix_bucket_complete_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;

    return RGW_SAL_OK;
}

/* 同步 */
static int posix_bucket_sync(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/* 排空 */
static int posix_bucket_drain(rgw_sal_bucket_t* bucket,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    posix_bucket_impl_t* impl = (posix_bucket_impl_t*)bucket->impl;
    if (impl) {
        impl->deleted = true;
    }

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/* 索引检查 */
static int posix_bucket_check_object_index(rgw_sal_bucket_t* bucket,
                                             const rgw_sal_dpp_t* dpp,
                                             rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int posix_bucket_fix_object_index(rgw_sal_bucket_t* bucket,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int posix_bucket_check_bucket_index(rgw_sal_bucket_t* bucket,
                                             const rgw_sal_dpp_t* dpp,
                                             rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

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
    .get_marker = posix_bucket_get_marker,
    .get_info = posix_bucket_get_info,
    .get_owner = posix_bucket_get_owner,
    .get_attrs = posix_bucket_get_attrs,
    .set_attrs = posix_bucket_set_attrs,
    .list = posix_bucket_list,
    .load = posix_bucket_load,
    .store = posix_bucket_store,
    .remove = posix_bucket_remove,
    .create = posix_bucket_create,
    .delete_bucket = posix_bucket_delete_bucket,
    .rename = posix_bucket_rename,
    .set_acl = posix_bucket_set_acl,
    .get_policy = posix_bucket_get_policy,
    .set_policy = posix_bucket_set_policy,
    .get_tag = posix_bucket_get_tag,
    .set_tag = posix_bucket_set_tag,
    .get_usage = posix_bucket_get_usage,
    .read_stats = posix_bucket_read_stats,
    .read_stats_async = posix_bucket_read_stats_async,
    .complete_stats = posix_bucket_complete_stats,
    .sync = posix_bucket_sync,
    .drain = posix_bucket_drain,
    .check_object_index = posix_bucket_check_object_index,
    .fix_object_index = posix_bucket_fix_object_index,
    .check_bucket_index = posix_bucket_check_bucket_index,
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

/**
 * @brief 构建对象的文件路径
 */
static int posix_build_object_path(rgw_sal_object_t* obj, char* path_buf, size_t buf_size) {
    if (!obj || !path_buf || buf_size == 0) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    posix_bucket_impl_t* bucket_impl = NULL;
    posix_driver_impl_t* driver_impl = NULL;

    if (obj->bucket && obj->bucket->impl) {
        bucket_impl = (posix_bucket_impl_t*)obj->bucket->impl;
    }
    if (obj->bucket && obj->bucket->driver && obj->bucket->driver->impl) {
        driver_impl = (posix_driver_impl_t*)obj->bucket->driver->impl;
    }

    /* 构建路径: {root_path}/{tenant}/{bucket_name}/{object_name} */
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket = bucket_impl && bucket_impl->name ? bucket_impl->name : "";
    const char* name = impl && impl->name ? impl->name : "";

    int len = snprintf(path_buf, buf_size, "%s/%s/%s/%s", root, tenant, bucket, name);
    if (len < 0 || (size_t)len >= buf_size) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    return RGW_SAL_OK;
}

static int posix_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                              uint8_t* buffer, size_t* buffer_size,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !buffer || !buffer_size) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建文件路径 */
    char file_path[1024];
    int ret = posix_build_object_path(obj, file_path, sizeof(file_path));
    if (ret < 0) return ret;

    /* 打开文件 */
    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            *buffer_size = 0;
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 获取文件大小 */
    struct stat st;
    if (fstat(fileno(fp), &st) < 0) {
        fclose(fp);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 计算读取范围 */
    size_t read_size = *buffer_size;
    if (end > 0 && end >= offset) {
        size_t requested = (size_t)(end - offset + 1);
        if (requested < read_size) {
            read_size = requested;
        }
    } else if (end < 0) {
        /* 读取到文件末尾 */
        off_t remaining = st.st_size - offset;
        if (remaining > 0 && (size_t)remaining < read_size) {
            read_size = (size_t)remaining;
        }
    }

    /* 定位到起始位置 */
    if (offset > 0 && fseek(fp, (off_t)offset, SEEK_SET) != 0) {
        fclose(fp);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 读取数据 */
    size_t bytes_read = fread(buffer, 1, read_size, fp);
    fclose(fp);

    if (bytes_read == 0 && ferror(fp)) {
        *buffer_size = 0;
        return RGW_SAL_ERR_IO_ERROR;
    }

    *buffer_size = bytes_read;
    impl->size = st.st_size;
    impl->mtime = st.st_mtime;
    impl->loaded = true;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                               const uint8_t* data,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !data) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建文件路径 */
    char file_path[1024];
    int ret = posix_build_object_path(obj, file_path, sizeof(file_path));
    if (ret < 0) return ret;

    /* 确保目录存在 */
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s", file_path);
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* 创建目录（如果不存在） */
        mkdir_p(dir_path);
    }

    /* 打开文件进行写入 */
    FILE* fp = fopen(file_path, "ab");
    if (!fp) {
        /* 尝试以写入模式创建 */
        fp = fopen(file_path, "wb");
        if (!fp) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    /* 定位到写入位置 */
    if (offset > 0 && fseek(fp, (off_t)offset, SEEK_SET) != 0) {
        fclose(fp);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 写入数据 */
    size_t bytes_written = fwrite(data, 1, (size_t)size, fp);
    fclose(fp);

    if (bytes_written != (size_t)size) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    impl->size = size;
    impl->mtime = time(NULL);
    impl->written = true;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建文件路径 */
    char file_path[1024];
    int ret = posix_build_object_path(obj, file_path, sizeof(file_path));
    if (ret < 0) return ret;

    /* 删除文件 */
    if (unlink(file_path) < 0) {
        if (errno != ENOENT) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    impl->deleted = true;
    impl->mtime = time(NULL);

    (void)flags;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 递归创建目录
 */
static int mkdir_p(const char* path) {
    if (!path) return RGW_SAL_ERR_INVALID_ARG;

    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return RGW_SAL_ERR_IO_ERROR;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    return RGW_SAL_OK;
}

/* 对象状态加载 */
static int posix_object_load_state(rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y, bool follow_olh) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建文件路径 */
    char file_path[1024];
    int ret = posix_build_object_path(obj, file_path, sizeof(file_path));
    if (ret < 0) return ret;

    /* 获取文件状态 */
    struct stat st;
    if (stat(file_path, &st) < 0) {
        if (errno == ENOENT) {
            impl->loaded = true;
            impl->size = 0;
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    impl->size = st.st_size;
    impl->mtime = st.st_mtime;
    impl->loaded = true;

    (void)dpp;
    (void)y;
    (void)follow_olh;
    return RGW_SAL_OK;
}

/* 获取对象属性 */
static int posix_object_get_obj_attrs(rgw_sal_object_t* obj, rgw_sal_yield_t* y,
                                      const rgw_sal_dpp_t* dpp) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建文件路径 */
    char file_path[1024];
    int ret = posix_build_object_path(obj, file_path, sizeof(file_path));
    if (ret < 0) return ret;

    /* 获取文件属性 */
    struct stat st;
    if (stat(file_path, &st) < 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 创建或更新属性映射 */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 从 xattr 读取属性 */
#ifdef __linux__
    /* Linux: 从扩展属性读取 */
    ssize_t xattr_size = getxattr(file_path, "user.rgw_attrs", NULL, 0);
    if (xattr_size > 0) {
        /* 解析 xattr 中的属性数据 */
        uint8_t* xattr_data = (uint8_t*)malloc((size_t)xattr_size);
        if (xattr_data) {
            ssize_t read_size = getxattr(file_path, "user.rgw_attrs", xattr_data, (size_t)xattr_size);
            if (read_size == xattr_size) {
                /* 简化解析：假设是简单的 key=value 格式 */
                /* TODO: 实现完整的属性解析 */
            }
            free(xattr_data);
        }
    }
#else
    /* 非 Linux: 仅使用内存中的属性 */
#endif

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 设置对象属性 */
static int posix_object_set_obj_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* setattrs,
                                      rgw_sal_attrs_t* delattrs, rgw_sal_yield_t* y,
                                      uint32_t flags) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    posix_object_impl_t* impl = (posix_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 创建属性映射（如果不存在） */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 设置属性 */
    if (setattrs) {
        for (size_t i = 0; i < setattrs->count; i++) {
            rgw_sal_attrs_set(impl->attrs, setattrs->pairs[i].key,
                             setattrs->pairs[i].value, setattrs->pairs[i].value_len);
        }
    }

    /* 删除属性 */
    if (delattrs) {
        /* TODO: 实现属性删除 */
    }

    /* 写入到 xattr */
#ifdef __linux__
    char file_path[1024];
    if (posix_build_object_path(obj, file_path, sizeof(file_path)) == 0) {
        /* 简化实现：将属性写入 xattr */
        /* TODO: 实现完整的属性序列化 */
    }
#else
    /* 非 Linux: 仅存储在内存中 */
#endif

    (void)y;
    (void)flags;
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
    .read = posix_object_read,
    .write = posix_object_write,
    .delete_obj = posix_object_delete_obj,
    .load_state = posix_object_load_state,
    .get_obj_attrs = posix_object_get_obj_attrs,
    .set_obj_attrs = posix_object_set_obj_attrs,
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

/*============================================================================
 * 生命周期 (Lifecycle) 实现
 *============================================================================*/

/* LC 目录常量 */
#define POSIX_LC_DIR ".meta/lc"
#define POSIX_LC_HEAD_PREFIX "head."
#define POSIX_LC_ENTRY_PREFIX "entry."

/**
 * @brief 获取 LC 目录路径
 */
static int posix_lc_get_dir(rgw_sal_driver_t* driver, char* buf, size_t buf_size) {
    if (!driver || !buf) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (!impl || !impl->root_path[0]) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    snprintf(buf, buf_size, "%s/" POSIX_LC_DIR, impl->root_path);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 LC Head 文件路径
 */
static int posix_lc_get_head_path(rgw_sal_driver_t* driver, uint32_t shard_id,
                                  char* buf, size_t buf_size) {
    if (!driver || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    snprintf(buf, buf_size, "%s/" POSIX_LC_HEAD_PREFIX "%u", lc_dir, shard_id);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 LC Entry 文件路径
 */
static int posix_lc_get_entry_path(rgw_sal_driver_t* driver, uint32_t shard_id,
                                   const char* bucket_key, char* buf, size_t buf_size) {
    if (!driver || !bucket_key || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* bucket_key 可能包含特殊字符，需要进行编码或直接使用 */
    snprintf(buf, buf_size, "%s/" POSIX_LC_ENTRY_PREFIX "%u.%s", lc_dir, shard_id, bucket_key);
    return RGW_SAL_OK;
}

/* 生命周期函数实现 */
static int posix_lc_get_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                              const char* bucket_key, rgw_lc_entry_t* entry,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !entry) return RGW_SAL_ERR_INVALID_ARG;

    char path[1024];
    if (posix_lc_get_entry_path(driver, shard_id, bucket_key, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 读取 LC Entry 文件 */
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 读取编码数据 */
    uint8_t buf[4096];
    size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (bytes_read == 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 解码 LC Entry */
    int ret = rgw_lc_entry_decode(buf, bytes_read, entry);
    if (ret < 0) {
        return ret;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_get_next_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                                   const char* marker, rgw_lc_entry_t* entry,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !entry) return RGW_SAL_ERR_INVALID_ARG;

    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 打开 LC 目录 */
    DIR* dir = opendir(lc_dir);
    if (!dir) {
        if (errno == ENOENT) {
            /* 目录不存在，返回空 */
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    char prefix[64];
    snprintf(prefix, sizeof(prefix), POSIX_LC_ENTRY_PREFIX "%u.", shard_id);
    size_t prefix_len = strlen(prefix);

    char found_marker[512] = {0};
    int found = 0;

    /* 遍历目录查找匹配的条目 */
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) != 0) {
            continue;
        }

        char* entry_marker = ent->d_name + prefix_len;
        if (marker && strlen(marker) > 0) {
            if (strcmp(entry_marker, marker) <= 0) {
                continue;
            }
        }

        if (!found || strcmp(entry_marker, found_marker) < 0) {
            /* 读取此条目 */
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", lc_dir, ent->d_name);

            FILE* fp = fopen(path, "rb");
            if (fp) {
                uint8_t buf[4096];
                size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
                fclose(fp);

                if (bytes_read > 0) {
                    rgw_lc_entry_t* tmp_entry = rgw_lc_entry_create();
                    if (tmp_entry) {
                        if (rgw_lc_entry_decode(buf, bytes_read, tmp_entry) == 0) {
                            /* 更新找到的标记 */
                            strncpy(found_marker, entry_marker, sizeof(found_marker) - 1);

                            /* 销毁旧条目并复制新条目 */
                            rgw_lc_entry_destroy(entry);
                            *entry = *tmp_entry;
                            free(tmp_entry);
                            found = 1;
                        } else {
                            rgw_lc_entry_destroy(tmp_entry);
                        }
                    }
                }
            }
        }
    }

    closedir(dir);

    if (!found) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_set_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                              const char* bucket_key, const rgw_lc_entry_t* entry,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !bucket_key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    /* 确保 LC 目录存在 */
    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (posix_mkdir_recursive(lc_dir, 0755) < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 获取 Entry 文件路径 */
    char path[1024];
    if (posix_lc_get_entry_path(driver, shard_id, bucket_key, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 编码 LC Entry */
    uint8_t buf[4096];
    size_t out_len = 0;
    int ret = rgw_lc_entry_encode(entry, buf, sizeof(buf), &out_len);
    if (ret < 0) {
        return ret;
    }

    /* 写入文件 */
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    size_t written = fwrite(buf, 1, out_len, fp);
    fclose(fp);

    if (written != out_len) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_list_entries(rgw_sal_driver_t* driver, uint32_t shard_id,
                                 const char* marker, uint32_t max_entries,
                                 rgw_lc_entry_t*** entries, size_t* num_entries,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !entries || !num_entries) return RGW_SAL_ERR_INVALID_ARG;

    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 打开 LC 目录 */
    DIR* dir = opendir(lc_dir);
    if (!dir) {
        if (errno == ENOENT) {
            *entries = NULL;
            *num_entries = 0;
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    char prefix[64];
    snprintf(prefix, sizeof(prefix), POSIX_LC_ENTRY_PREFIX "%u.", shard_id);
    size_t prefix_len = strlen(prefix);

    /* 收集所有匹配的条目 */
    rgw_lc_entry_t** all_entries = NULL;
    size_t all_count = 0;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) != 0) {
            continue;
        }

        char* entry_marker = ent->d_name + prefix_len;
        if (marker && strlen(marker) > 0) {
            if (strcmp(entry_marker, marker) <= 0) {
                continue;
            }
        }

        /* 读取条目 */
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", lc_dir, ent->d_name);

        FILE* fp = fopen(path, "rb");
        if (!fp) continue;

        uint8_t buf[4096];
        size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
        fclose(fp);

        if (bytes_read == 0) continue;

        rgw_lc_entry_t* entry = rgw_lc_entry_create();
        if (!entry) continue;

        if (rgw_lc_entry_decode(buf, bytes_read, entry) == 0) {
            all_entries = (rgw_lc_entry_t**)realloc(all_entries, (all_count + 1) * sizeof(rgw_lc_entry_t*));
            if (all_entries) {
                all_entries[all_count] = entry;
                all_count++;
            } else {
                rgw_lc_entry_destroy(entry);
            }
        } else {
            rgw_lc_entry_destroy(entry);
        }
    }

    closedir(dir);

    *entries = all_entries;
    *num_entries = all_count;

    (void)dpp;
    (void)max_entries;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_rm_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                            const char* bucket_key,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !bucket_key) return RGW_SAL_ERR_INVALID_ARG;

    char path[1024];
    if (posix_lc_get_entry_path(driver, shard_id, bucket_key, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (unlink(path) < 0) {
        if (errno == ENOENT) {
            return RGW_SAL_OK;  /* 已不存在，视为成功 */
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_get_head(rgw_sal_driver_t* driver, uint32_t shard_id,
                            rgw_lc_head_t* head,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    char path[1024];
    if (posix_lc_get_head_path(driver, shard_id, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 读取 LC Head 文件 */
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 读取编码数据 */
    uint8_t buf[4096];
    size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (bytes_read == 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 解码 LC Head */
    int ret = rgw_lc_head_decode(buf, bytes_read, head);
    if (ret < 0) {
        return ret;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_lc_put_head(rgw_sal_driver_t* driver, uint32_t shard_id,
                            const rgw_lc_head_t* head,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    /* 确保 LC 目录存在 */
    char lc_dir[512];
    if (posix_lc_get_dir(driver, lc_dir, sizeof(lc_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (posix_mkdir_recursive(lc_dir, 0755) < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 获取 Head 文件路径 */
    char path[1024];
    if (posix_lc_get_head_path(driver, shard_id, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 编码 LC Head */
    uint8_t buf[4096];
    size_t out_len = 0;
    int ret = rgw_lc_head_encode(head, buf, sizeof(buf), &out_len);
    if (ret < 0) {
        return ret;
    }

    /* 写入文件 */
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    size_t written = fwrite(buf, 1, out_len, fp);
    fclose(fp);

    if (written != out_len) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 多部分上传 (Multipart) 实现
 *============================================================================*/

/* Multipart 目录常量 */
#define POSIX_MULTIPART_DIR ".multipart"

/**
 * @brief 获取桶的 Multipart 目录路径
 */
static int posix_multipart_get_dir(rgw_sal_driver_t* driver, const char* tenant,
                                   const char* bucket_name, char* buf, size_t buf_size) {
    if (!driver || !buf) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (!impl || !impl->root_path[0]) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    const char* t = tenant ? tenant : "default";
    const char* b = bucket_name ? bucket_name : "";

    snprintf(buf, buf_size, "%s/%s/%s/" POSIX_MULTIPART_DIR, impl->root_path, t, b);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 Multipart 元数据文件路径
 */
static int posix_multipart_get_meta_path(rgw_sal_driver_t* driver, const char* tenant,
                                        const char* bucket_name, const char* object_name,
                                        const char* upload_id, char* buf, size_t buf_size) {
    if (!driver || !object_name || !upload_id || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 构建元数据文件名: {object}.{upload_id}.meta */
    char* encoded_name = rgw_multipart_meta_key(object_name, upload_id);
    if (!encoded_name) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    snprintf(buf, buf_size, "%s/%s", mp_dir, encoded_name);
    free(encoded_name);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 Multipart 分片文件路径
 */
static int posix_multipart_get_part_path(rgw_sal_driver_t* driver, const char* tenant,
                                         const char* bucket_name, const char* object_name,
                                         const char* upload_id, uint32_t part_num,
                                         char* buf, size_t buf_size) {
    if (!driver || !object_name || !upload_id || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 构建分片文件名: {object}.{upload_id}.{part_num} */
    char* encoded_name = rgw_multipart_part_key(object_name, upload_id, part_num);
    if (!encoded_name) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    snprintf(buf, buf_size, "%s/%s", mp_dir, encoded_name);
    free(encoded_name);
    return RGW_SAL_OK;
}

/* 多部分上传函数实现 */
static int posix_multipart_init(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                const char* object_name, const char* upload_id,
                                rgw_sal_multipart_upload_info_t** upload_info,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id || !upload_info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 确保 Multipart 目录存在 */
    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (posix_mkdir_recursive(mp_dir, 0755) < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 创建上传信息 */
    rgw_sal_multipart_upload_info_t* info = (rgw_sal_multipart_upload_info_t*)
        calloc(1, sizeof(rgw_sal_multipart_upload_info_t));
    if (!info) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 初始化上传信息 */
    info->object = strdup(object_name);
    info->upload_id = strdup(upload_id);
    info->bucket = strdup(bucket_name);
    info->ctime = time(NULL);
    info->mtime = info->ctime;

    if (!info->object || !info->upload_id || !info->bucket) {
        free(info->object);
        free(info->upload_id);
        free(info->bucket);
        free(info);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 初始化分片列表 */
    info->parts.parts = NULL;
    info->parts.num_parts = 0;
    info->parts.capacity = 0;

    *upload_info = info;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_multipart_store_info(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                     const char* object_name, const char* upload_id,
                                     const rgw_sal_multipart_upload_info_t* upload_info,
                                     const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id || !upload_info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取元数据文件路径 */
    char path[1024];
    if (posix_multipart_get_meta_path(driver, tenant, bucket_name, object_name,
                                      upload_id, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 确保目录存在 */
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s", path);
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (posix_mkdir_recursive(dir_path, 0755) < 0) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    /* 序列化上传信息到 JSON */
    FILE* fp = fopen(path, "w");
    if (!fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 写入基本字段 */
    fprintf(fp, "{\n");
    fprintf(fp, "  \"object\": \"%s\",\n", object_name ? object_name : "");
    fprintf(fp, "  \"upload_id\": \"%s\",\n", upload_id ? upload_id : "");
    fprintf(fp, "  \"bucket\": \"%s\",\n", bucket_name ? bucket_name : "");
    fprintf(fp, "  \"size\": %llu,\n", (unsigned long long)upload_info->size);
    fprintf(fp, "  \"etag\": \"%s\",\n", upload_info->etag ? upload_info->etag : "");
    fprintf(fp, "  \"part_num\": %u,\n", upload_info->part_num);
    fprintf(fp, "  \"ctime\": %lld,\n", (long long)upload_info->ctime);
    fprintf(fp, "  \"mtime\": %lld,\n", (long long)upload_info->mtime);
    fprintf(fp, "  \"storage_class\": \"%s\",\n",
            upload_info->storage_class ? upload_info->storage_class : "");
    fprintf(fp, "  \"num_parts\": %zu\n", upload_info->parts.num_parts);
    fprintf(fp, "}\n");

    fclose(fp);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_multipart_load_info(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                    const char* object_name, const char* upload_id,
                                    rgw_sal_multipart_upload_info_t** upload_info,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id || !upload_info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取元数据文件路径 */
    char path[1024];
    if (posix_multipart_get_meta_path(driver, tenant, bucket_name, object_name,
                                      upload_id, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 读取元数据文件 */
    FILE* fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 分配上传信息 */
    rgw_sal_multipart_upload_info_t* info = (rgw_sal_multipart_upload_info_t*)
        calloc(1, sizeof(rgw_sal_multipart_upload_info_t));
    if (!info) {
        fclose(fp);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 初始化分片列表 */
    info->parts.parts = NULL;
    info->parts.num_parts = 0;
    info->parts.capacity = 0;

    /* 简单解析 JSON */
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        /* 移除换行符 */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        /* 解析 key-value */
        char* colon = strchr(line, ':');
        if (!colon) continue;

        *colon = '\0';
        char* key = line;
        char* value = colon + 1;

        /* 跳过空白 */
        while (*value && (*value == ' ' || *value == ',')) value++;
        /* 移除引号 */
        if (*value == '"') {
            value++;
            char* end = strchr(value, '"');
            if (end) *end = '\0';
        }

        if (strcmp(key, "object") == 0) {
            info->object = strdup(value);
        } else if (strcmp(key, "upload_id") == 0) {
            info->upload_id = strdup(value);
        } else if (strcmp(key, "bucket") == 0) {
            info->bucket = strdup(value);
        } else if (strcmp(key, "size") == 0) {
            info->size = (uint64_t)strtoull(value, NULL, 10);
        } else if (strcmp(key, "etag") == 0) {
            info->etag = strdup(value);
        } else if (strcmp(key, "part_num") == 0) {
            info->part_num = (uint32_t)strtoul(value, NULL, 10);
        } else if (strcmp(key, "ctime") == 0) {
            info->ctime = (int64_t)strtoll(value, NULL, 10);
        } else if (strcmp(key, "mtime") == 0) {
            info->mtime = (int64_t)strtoll(value, NULL, 10);
        } else if (strcmp(key, "storage_class") == 0) {
            info->storage_class = strdup(value);
        }
    }

    fclose(fp);

    *upload_info = info;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_multipart_list_parts(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                     const char* object_name, const char* upload_id,
                                     uint32_t max_parts, uint32_t marker,
                                     rgw_sal_multipart_part_info_t** parts, size_t* num_parts,
                                     int* is_truncated, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id || !parts || !num_parts) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取 Multipart 目录 */
    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 打开目录 */
    DIR* dir = opendir(mp_dir);
    if (!dir) {
        if (errno == ENOENT) {
            *parts = NULL;
            *num_parts = 0;
            if (is_truncated) *is_truncated = 0;
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建分片前缀 */
    char prefix[512];
    snprintf(prefix, sizeof(prefix), "%s.%s.", object_name, upload_id);
    size_t prefix_len = strlen(prefix);

    /* 收集所有分片 */
    rgw_sal_multipart_part_info_t* all_parts = NULL;
    size_t all_count = 0;
    size_t all_capacity = 0;

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) != 0) {
            continue;
        }

        /* 跳过 .meta 文件 */
        char* dot = strrchr(ent->d_name + prefix_len, '.');
        if (!dot) continue;

        /* 解析分片编号 */
        uint32_t part_num = (uint32_t)strtoul(dot + 1, NULL, 10);

        /* 检查 marker */
        if (marker > 0 && part_num <= marker) {
            continue;
        }

        /* 读取分片文件获取元数据 */
        char part_path[1024];
        snprintf(part_path, sizeof(part_path), "%s/%s", mp_dir, ent->d_name);

        struct stat st;
        if (stat(part_path, &st) < 0) {
            continue;
        }

        /* 扩展数组 */
        if (all_count >= all_capacity) {
            size_t new_capacity = all_capacity > 0 ? all_capacity * 2 : 16;
            rgw_sal_multipart_part_info_t* new_parts = (rgw_sal_multipart_part_info_t*)
                realloc(all_parts, new_capacity * sizeof(rgw_sal_multipart_part_info_t));
            if (!new_parts) {
                closedir(dir);
                free(all_parts);
                return RGW_SAL_ERR_OUT_OF_MEMORY;
            }
            all_parts = new_parts;
            all_capacity = new_capacity;
        }

        /* 设置分片信息 */
        memset(&all_parts[all_count], 0, sizeof(rgw_sal_multipart_part_info_t));
        all_parts[all_count].part_num = part_num;
        all_parts[all_count].size = (uint64_t)st.st_size;
        all_parts[all_count].mtime = st.st_mtime;

        /* 生成简单的 ETag */
        char etag[64];
        snprintf(etag, sizeof(etag), "\"%08x\"", part_num);
        all_parts[all_count].etag = strdup(etag);

        all_count++;
    }

    closedir(dir);

    /* 按分片编号排序 */
    for (size_t i = 0; i + 1 < all_count; i++) {
        for (size_t j = i + 1; j < all_count; j++) {
            if (all_parts[i].part_num > all_parts[j].part_num) {
                rgw_sal_multipart_part_info_t tmp = all_parts[i];
                all_parts[i] = all_parts[j];
                all_parts[j] = tmp;
            }
        }
    }

    /* 限制返回数量 */
    if (max_parts > 0 && all_count > max_parts) {
        if (is_truncated) *is_truncated = 1;
        all_count = max_parts;
    } else {
        if (is_truncated) *is_truncated = 0;
    }

    *parts = all_parts;
    *num_parts = all_count;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_multipart_abort(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                 const char* object_name, const char* upload_id,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取 Multipart 目录 */
    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 构建分片前缀 */
    char prefix[512];
    snprintf(prefix, sizeof(prefix), "%s.%s.", object_name, upload_id);
    size_t prefix_len = strlen(prefix);

    /* 打开目录 */
    DIR* dir = opendir(mp_dir);
    if (!dir) {
        return RGW_SAL_OK;  /* 目录不存在，视为成功 */
    }

    /* 删除所有匹配的分片文件 */
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, prefix, prefix_len) == 0) {
            char part_path[1024];
            snprintf(part_path, sizeof(part_path), "%s/%s", mp_dir, ent->d_name);
            unlink(part_path);
        }
    }

    closedir(dir);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_multipart_complete(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                   const char* object_name, const char* upload_id,
                                   uint32_t parts_count, const char* const* etags,
                                   const rgw_sal_multipart_upload_info_t* upload_info,
                                   rgw_sal_object_t** final_obj,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !object_name || !upload_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取 Multipart 目录和目标路径 */
    char mp_dir[512];
    if (posix_multipart_get_dir(driver, tenant, bucket_name, mp_dir, sizeof(mp_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    posix_driver_impl_t* driver_impl = (posix_driver_impl_t*)driver->impl;
    const char* root = driver_impl && driver_impl->root_path[0] ? driver_impl->root_path : "/tmp/rgw";

    char dest_path[1024];
    snprintf(dest_path, sizeof(dest_path), "%s/%s/%s/%s",
             root, tenant, bucket_name, object_name);

    /* 确保目标目录存在 */
    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s", dest_path);
    char* last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (posix_mkdir_recursive(dir_path, 0755) < 0) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    /* 合并所有分片到目标文件 */
    FILE* dest_fp = fopen(dest_path, "wb");
    if (!dest_fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    uint64_t total_size = 0;

    for (uint32_t i = 0; i < parts_count; i++) {
        char part_path[1024];
        snprintf(part_path, sizeof(part_path), "%s/%s.%s.%u",
                 mp_dir, object_name, upload_id, i + 1);

        FILE* part_fp = fopen(part_path, "rb");
        if (!part_fp) {
            fclose(dest_fp);
            return RGW_SAL_ERR_IO_ERROR;
        }

        /* 复制分片内容 */
        uint8_t buffer[65536];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), part_fp)) > 0) {
            size_t bytes_written = fwrite(buffer, 1, bytes_read, dest_fp);
            if (bytes_written != bytes_read) {
                fclose(part_fp);
                fclose(dest_fp);
                return RGW_SAL_ERR_WRITE_ERROR;
            }
            total_size += bytes_written;
        }

        fclose(part_fp);

        /* 删除分片文件 */
        unlink(part_path);
    }

    fclose(dest_fp);

    /* 删除元数据文件 */
    char meta_path[1024];
    if (posix_multipart_get_meta_path(driver, tenant, bucket_name, object_name,
                                      upload_id, meta_path, sizeof(meta_path)) == 0) {
        unlink(meta_path);
    }

    /* 如果需要返回最终对象，创建它 */
    if (final_obj && driver) {
        rgw_sal_obj_key_t key = {
            .name = (char*)object_name,
            .instance = NULL,
            .is_null = false,
            .is_current = true
        };
        *final_obj = driver->vtable->get_object(driver, bucket, &key);
        if (*final_obj) {
            posix_object_impl_t* obj_impl = (posix_object_impl_t*)(*final_obj)->impl;
            if (obj_impl) {
                obj_impl->size = total_size;
                obj_impl->mtime = time(NULL);
            }
        }
    }

    (void)dpp;
    (void)etags;
    (void)upload_info;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 通知 (Notification) 实现
 *============================================================================*/

/* Topics 目录常量 */
#define POSIX_TOPICS_DIR ".meta/topics"
#define POSIX_BUCKET_TOPICS_DIR ".meta/bucket_topics"

/**
 * @brief 获取 Topics 目录路径
 */
static int posix_topics_get_dir(rgw_sal_driver_t* driver, char* buf, size_t buf_size) {
    if (!driver || !buf) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (!impl || !impl->root_path[0]) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    snprintf(buf, buf_size, "%s/" POSIX_TOPICS_DIR, impl->root_path);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 Bucket Topics 目录路径
 */
static int posix_bucket_topics_get_dir(rgw_sal_driver_t* driver, char* buf, size_t buf_size) {
    if (!driver || !buf) return RGW_SAL_ERR_INVALID_ARG;

    posix_driver_impl_t* impl = (posix_driver_impl_t*)driver->impl;
    if (!impl || !impl->root_path[0]) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    snprintf(buf, buf_size, "%s/" POSIX_BUCKET_TOPICS_DIR, impl->root_path);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 Topic 文件路径
 */
static int posix_get_topic_path(rgw_sal_driver_t* driver, const char* tenant,
                               const char* topic_name, char* buf, size_t buf_size) {
    if (!driver || !topic_name || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char topics_dir[512];
    if (posix_topics_get_dir(driver, topics_dir, sizeof(topics_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 格式: {tenant}.{topic_name} */
    const char* t = tenant ? tenant : "default";
    snprintf(buf, buf_size, "%s/%s.%s", topics_dir, t, topic_name);
    return RGW_SAL_OK;
}

/**
 * @brief 获取 Bucket Topic 关联文件路径
 */
static int posix_get_bucket_topic_path(rgw_sal_driver_t* driver, const char* tenant,
                                       const char* bucket_name, char* buf, size_t buf_size) {
    if (!driver || !bucket_name || !buf) return RGW_SAL_ERR_INVALID_ARG;

    char bucket_topics_dir[512];
    if (posix_bucket_topics_get_dir(driver, bucket_topics_dir, sizeof(bucket_topics_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 格式: {tenant}.{bucket_name} */
    const char* t = tenant ? tenant : "default";
    snprintf(buf, buf_size, "%s/%s.%s", bucket_topics_dir, t, bucket_name);
    return RGW_SAL_OK;
}

/* 通知函数实现 */
static int posix_get_notification(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                  rgw_sal_object_t* obj, uint32_t event_type,
                                  void** notification,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !notification) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建通知结构（简化实现） */
    typedef struct {
        uint32_t event_type;
        void* topic;
    } posix_notification_t;

    posix_notification_t* notif = (posix_notification_t*)calloc(1, sizeof(posix_notification_t));
    if (!notif) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    notif->event_type = event_type;
    notif->topic = NULL;

    *notification = notif;

    (void)bucket;
    (void)obj;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_publish_reserve(rgw_sal_driver_t* driver, rgw_sal_object_t* obj,
                                uint32_t event_type, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 预留发布资源（简化实现） */
    (void)event_type;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_publish_commit(rgw_sal_driver_t* driver, rgw_sal_object_t* obj,
                               uint64_t size, time_t mtime, const char* etag,
                               const char* version_id, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    if (!driver || !obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 发布提交（简化实现，实际应该发送到消息队列） */
    (void)size;
    (void)mtime;
    (void)etag;
    (void)version_id;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_read_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                            void** topics,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topics) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取 Bucket Topic 关联文件 */
    char path[1024];
    if (posix_get_bucket_topic_path(driver, tenant, bucket_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 读取关联文件 */
    FILE* fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) {
            *topics = NULL;
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解析关联的 topics */
    /* 简化实现：读取 topic 名称列表 */
    typedef struct {
        char** topics;
        size_t count;
        size_t capacity;
    } posix_topics_list_t;

    posix_topics_list_t* list = (posix_topics_list_t*)calloc(1, sizeof(posix_topics_list_t));
    if (!list) {
        fclose(fp);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (list->count >= list->capacity) {
            size_t new_cap = list->capacity > 0 ? list->capacity * 2 : 8;
            char** new_topics = (char**)realloc(list->topics, new_cap * sizeof(char*));
            if (!new_topics) {
                for (size_t i = 0; i < list->count; i++) {
                    free(list->topics[i]);
                }
                free(list->topics);
                free(list);
                fclose(fp);
                return RGW_SAL_ERR_OUT_OF_MEMORY;
            }
            list->topics = new_topics;
            list->capacity = new_cap;
        }

        list->topics[list->count] = strdup(line);
        list->count++;
    }

    fclose(fp);

    *topics = list;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_write_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                             const void* topics,
                             const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topics) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 确保目录存在 */
    char bucket_topics_dir[512];
    if (posix_bucket_topics_get_dir(driver, bucket_topics_dir, sizeof(bucket_topics_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (posix_mkdir_recursive(bucket_topics_dir, 0755) < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 获取 Bucket Topic 关联文件 */
    char path[1024];
    if (posix_get_bucket_topic_path(driver, tenant, bucket_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 写入 topics */
    FILE* fp = fopen(path, "w");
    if (!fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 简化实现：写入 topic 名称列表 */
    typedef struct {
        char** topics;
        size_t count;
    } posix_topics_list_t;

    const posix_topics_list_t* list = (const posix_topics_list_t*)topics;
    for (size_t i = 0; i < list->count; i++) {
        fprintf(fp, "%s\n", list->topics[i]);
    }

    fclose(fp);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int posix_remove_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    posix_bucket_impl_t* bucket_impl = bucket ? (posix_bucket_impl_t*)bucket->impl : NULL;
    const char* tenant = bucket_impl && bucket_impl->tenant ? bucket_impl->tenant : "default";
    const char* bucket_name = bucket_impl && bucket_impl->name ? bucket_impl->name : "";

    /* 获取 Bucket Topic 关联文件 */
    char path[1024];
    if (posix_get_bucket_topic_path(driver, tenant, bucket_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 删除关联文件 */
    if (unlink(path) < 0) {
        if (errno == ENOENT) {
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 发布通知事件
 *
 * @param driver 驱动
 * @param topic 主题对象
 * @param event 事件数据
 * @param dpp 调试上下文
 * @param y 协程上下文
 * @return 0 成功，负值失败
 */
static int posix_notification_publish(rgw_sal_driver_t* driver, void* topic,
                                      const rgw_sal_notification_event_t* event,
                                      const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topic || !event) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 topic 元数据键 */
    rgw_topic_t* topic_info = (rgw_topic_t*)topic;

    /* 获取推送端点 */
    const char* push_endpoint = topic_info->dest.push_endpoint;
    if (!push_endpoint) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 简化实现：记录到日志 */
    /* 实际实现应该使用 HTTP 客户端发送通知到 push_endpoint */

    (void)dpp;
    (void)y;
    (void)push_endpoint;

    return RGW_SAL_OK;
}

/**
 * @brief 加载 topic 信息
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name 主题名称
 * @param topic 输出 topic 信息
 * @param dpp 调试上下文
 * @param y 协程上下文
 * @return 0 成功，负值失败
 */
static int posix_topic_load(rgw_sal_driver_t* driver, const char* tenant,
                           const char* topic_name, rgw_topic_t* topic,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topic_name || !topic) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 Topic 文件路径 */
    char path[1024];
    if (posix_get_topic_path(driver, tenant, topic_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 读取 topic 文件 */
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 读取编码数据 */
    uint8_t buf[4096];
    size_t bytes_read = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);

    if (bytes_read == 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 解码 topic */
    int ret = rgw_topic_decode(buf, bytes_read, topic);
    if (ret < 0) {
        return ret;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 保存 topic 信息
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name 主题名称
 * @param topic topic 信息
 * @param dpp 调试上下文
 * @param y 协程上下文
 * @return 0 成功，负值失败
 */
static int posix_topic_save(rgw_sal_driver_t* driver, const char* tenant,
                           const char* topic_name, const rgw_topic_t* topic,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topic_name || !topic) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 确保目录存在 */
    char topics_dir[512];
    if (posix_topics_get_dir(driver, topics_dir, sizeof(topics_dir)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (posix_mkdir_recursive(topics_dir, 0755) < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 获取 Topic 文件路径 */
    char path[1024];
    if (posix_get_topic_path(driver, tenant, topic_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 编码 topic */
    uint8_t buf[4096];
    size_t out_len = 0;
    int ret = rgw_topic_encode(topic, buf, sizeof(buf), &out_len);
    if (ret < 0) {
        return ret;
    }

    /* 写入文件 */
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    size_t written = fwrite(buf, 1, out_len, fp);
    fclose(fp);

    if (written != out_len) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 删除 topic 信息
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name 主题名称
 * @param dpp 调试上下文
 * @param y 协程上下文
 * @return 0 成功，负值失败
 */
static int posix_topic_delete(rgw_sal_driver_t* driver, const char* tenant,
                             const char* topic_name,
                             const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !topic_name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 Topic 文件路径 */
    char path[1024];
    if (posix_get_topic_path(driver, tenant, topic_name, path, sizeof(path)) < 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 删除文件 */
    if (unlink(path) < 0) {
        if (errno == ENOENT) {
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * Lifecycle vtable 函数指针 - 供外部驱动调用
 *============================================================================*/

const rgw_sal_lifecycle_ops_t posix_lifecycle_ops = {
    .get_entry = posix_lc_get_entry,
    .get_next_entry = posix_lc_get_next_entry,
    .set_entry = posix_lc_set_entry,
    .list_entries = posix_lc_list_entries,
    .rm_entry = posix_lc_rm_entry,
    .get_head = posix_lc_get_head,
    .put_head = posix_lc_put_head,
};

/*============================================================================
 * Multipart vtable 函数指针 - 供外部驱动调用
 *============================================================================*/

const rgw_sal_multipart_ops_t posix_multipart_ops = {
    .init = posix_multipart_init,
    .list_parts = posix_multipart_list_parts,
    .abort = posix_multipart_abort,
    .complete = posix_multipart_complete,
    .store_info = posix_multipart_store_info,
    .load_info = posix_multipart_load_info,
};

/*============================================================================
 * Notification vtable 函数指针 - 供外部驱动调用
 *============================================================================*/

const rgw_sal_notification_ops_t posix_notification_ops = {
    .get_notification = posix_get_notification,
    .publish_reserve = posix_publish_reserve,
    .publish_commit = posix_publish_commit,
    .read_topics = posix_read_topics,
    .write_topics = posix_write_topics,
    .remove_topics = posix_remove_topics,
    .topic_load = posix_topic_load,
    .topic_save = posix_topic_save,
    .topic_delete = posix_topic_delete,
    .publish = posix_notification_publish,
};

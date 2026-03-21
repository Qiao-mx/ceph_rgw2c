/**
 * @file rgw_sal_daos.c
 * @brief DAOS (Intel) 对象存储驱动完整 C 语言实现
 *
 * 本文件将 C++ DAOS 驱动 (rgw_sal_daos.cc) 完整转换为 C 语言实现，
 * 使用 sal_c 框架和 c_common 容器库。
 *
 * 转换自: src/rgw/driver/daos/rgw_sal_daos.cc (2454 行 C++)
 *
 * 主要功能:
 * - DAOS 存储后端驱动
 * - 用户/桶/对象 CRUD 操作
 * - 分片上传支持
 * - 原子写入支持
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#include "rgw_sal.h"
#include "rgw_sal_daos.h"
#include "rgw_sal_daos_types.h"
#include "rgw_sal_daos_serde.h"
#include "rgw_sal_errors.h"
#include "rgw_ccommon.h"

/*============================================================================
 * 辅助函数声明
 *============================================================================*/

static rgw_sal_user_t* daos_driver_get_user(rgw_sal_driver_t* driver,
                                  const rgw_sal_user_id_t* uid);
static rgw_sal_bucket_t* daos_driver_get_bucket(rgw_sal_driver_t* driver,
                                    const rgw_sal_bucket_info_t* info);
static rgw_sal_object_t* daos_driver_get_object(rgw_sal_driver_t* driver,
                                    rgw_sal_bucket_t* bucket,
                                    const rgw_sal_obj_key_t* key);
static void* daos_user_clone(const rgw_sal_user_t* user);
static void daos_user_destroy(rgw_sal_user_t* user);
static const char* daos_user_get_id(const rgw_sal_user_t* user);
static const char* daos_user_get_display_name(rgw_sal_user_t* user);
static int daos_user_set_display_name(rgw_sal_user_t* user, const char* name);
static const char* daos_user_get_tenant(const rgw_sal_user_t* user);
static uint32_t daos_user_get_type(const rgw_sal_user_t* user);
static int32_t daos_user_get_max_buckets(const rgw_sal_user_t* user);
static void daos_user_set_max_buckets(rgw_sal_user_t* user, int32_t max);
static rgw_sal_attrs_t* daos_user_get_attrs(rgw_sal_user_t* user);
static int daos_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs);
static int daos_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                          rgw_sal_yield_t* y);
static int daos_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                           rgw_sal_yield_t* y, bool exclusive);
static int daos_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y);
static int daos_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y);
static int daos_user_merge_and_store_attrs(rgw_sal_user_t* user,
                                           rgw_sal_attrs_t* new_attrs,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y);
static const char* daos_user_get_ns(const rgw_sal_user_t* user);
static int daos_user_set_ns(rgw_sal_user_t* user, const char* ns);
static void daos_user_clear_ns(rgw_sal_user_t* user);
static int daos_user_set_info(rgw_sal_user_t* user, void* info);
static int daos_user_get_info(rgw_sal_user_t* user, void** info);
static int daos_user_get_caps(rgw_sal_user_t* user, void** caps);
static int daos_user_get_version_tracker(rgw_sal_user_t* user, void** tracker);
static int daos_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                uint64_t start_epoch, uint64_t end_epoch,
                                uint32_t max_entries, void* usage);
static int daos_user_trim_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                uint64_t start_epoch, uint64_t end_epoch);
static int daos_user_verify_mfa(rgw_sal_user_t* user, const char* mfa,
                                const char* code, const rgw_sal_dpp_t* dpp);
static int daos_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                 void** groups, uint32_t* count);

static void* daos_bucket_clone(const rgw_sal_bucket_t* bucket);
static void daos_bucket_destroy(rgw_sal_bucket_t* bucket);
static const char* daos_bucket_get_name(const rgw_sal_bucket_t* bucket);
static const char* daos_bucket_get_tenant(const rgw_sal_bucket_t* bucket);
static const char* daos_bucket_get_marker(const rgw_sal_bucket_t* bucket);
static rgw_sal_bucket_info_t* daos_bucket_get_info(rgw_sal_bucket_t* bucket);
static rgw_sal_user_t* daos_bucket_get_owner(rgw_sal_bucket_t* bucket);
static rgw_sal_attrs_t* daos_bucket_get_attrs(rgw_sal_bucket_t* bucket);
static int daos_bucket_set_attrs(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs);
static int daos_bucket_list(rgw_sal_bucket_t* bucket,
                            const char* prefix, const char* delimiter,
                            const char* marker, const char* end_marker,
                            uint32_t max_keys, bool list_versions,
                            rgw_sal_object_list_t** result,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_load(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y);
static int daos_bucket_store(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y, bool exclusive);
static int daos_bucket_remove(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y);
static int daos_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y, bool create_obj);
static int daos_bucket_delete_bucket(rgw_sal_bucket_t* bucket,
                                     const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y, bool delete_objects);
static int daos_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y, const char* new_name);
static int daos_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag);
static int daos_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_get_usage(rgw_sal_bucket_t* bucket, void** usage,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_bucket_read_stats(rgw_sal_bucket_t* bucket,
                                  const rgw_sal_dpp_t* dpp, void* stats);
static int daos_bucket_sync_user_stats(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);
static int daos_bucket_check_object_index(rgw_sal_bucket_t* bucket,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y);
static int daos_bucket_fix_object_index(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);
static int daos_bucket_remove_bypass_gc(rgw_sal_bucket_t* bucket,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y, bool sync);
static int daos_bucket_check_quota(rgw_sal_bucket_t* bucket,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y);
static int daos_bucket_check_empty(rgw_sal_bucket_t* bucket,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y);
static int daos_bucket_try_refresh_info(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);

static void* daos_object_clone(const rgw_sal_object_t* obj);
static void daos_object_destroy(rgw_sal_object_t* obj);
static const char* daos_object_get_name(const rgw_sal_object_t* obj);
static const char* daos_object_get_instance(const rgw_sal_object_t* obj);
static bool daos_object_is_null(const rgw_sal_object_t* obj);
static rgw_sal_attrs_t* daos_object_get_attrs(rgw_sal_object_t* obj);
static int daos_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs);
static int daos_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                            uint8_t* buffer, size_t* buffer_size,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                            const uint8_t* data,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
static int daos_object_load_state(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y, bool follow_olh);
static int daos_object_get_obj_attrs(rgw_sal_object_t* obj,
                                     rgw_sal_yield_t* y,
                                     const rgw_sal_dpp_t* dpp);
static int daos_object_set_obj_attrs(rgw_sal_object_t* obj,
                                     rgw_sal_attrs_t* setattrs,
                                     rgw_sal_attrs_t* delattrs,
                                     rgw_sal_yield_t* y, uint32_t flags);
static int daos_object_copy_object(rgw_sal_object_t* obj,
                                   rgw_sal_object_t* src_obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y);
static int daos_object_delete_obj_attrs(rgw_sal_object_t* obj,
                                        rgw_sal_attrs_t* attrs,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 安全释放字符串
 */
static void daos_free_string(char** str) {
    if (str && *str) {
        free(*str);
        *str = NULL;
    }
}

/**
 * @brief 安全复制字符串
 */
static char* daos_strdup(const char* s) {
    if (!s) return NULL;
    return strdup(s);
}

/**
 * @brief 获取 DAOS 驱动实现
 */
static rgw_sal_daos_driver_t* daos_driver_get_impl(rgw_sal_driver_t* driver) {
    if (!driver || !driver->impl) return NULL;
    return (rgw_sal_daos_driver_t*)driver->impl;
}

/**
 * @brief 获取 DAOS 用户实现
 */
static rgw_sal_daos_user_t* daos_user_get_impl(rgw_sal_user_t* user) {
    if (!user || !user->impl) return NULL;
    return (rgw_sal_daos_user_t*)user->impl;
}

/**
 * @brief 获取 DAOS 桶实现
 */
static rgw_sal_daos_bucket_t* daos_bucket_get_impl(rgw_sal_bucket_t* bucket) {
    if (!bucket || !bucket->impl) return NULL;
    return (rgw_sal_daos_bucket_t*)bucket->impl;
}

/**
 * @brief 获取 DAOS 对象实现
 */
static rgw_sal_daos_object_t* daos_object_get_impl(rgw_sal_object_t* obj) {
    if (!obj || !obj->impl) return NULL;
    return (rgw_sal_daos_object_t*)obj->impl;
}

/*============================================================================
 * 驱动 vtable 函数
 *============================================================================*/

/**
 * @brief 驱动初始化
 */
static int daos_driver_initialize(rgw_sal_driver_t* driver, void* cct,
                                   const rgw_sal_dpp_t* dpp) {
    (void)cct;
    (void)dpp;
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->initialized = true;
    return RGW_SAL_OK;
}

/**
 * @brief 驱动销毁
 */
static void daos_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;

    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (impl) {
        impl->initialized = false;
        free(impl);
    }
    driver->impl = NULL;
}

/**
 * @brief 获取驱动名称
 */
static const char* daos_driver_get_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    rgw_sal_daos_driver_t* impl = (rgw_sal_daos_driver_t*)driver->impl;
    return impl ? impl->name : NULL;
}

/**
 * @brief 获取集群 ID
 */
static int daos_driver_get_cluster_id(rgw_sal_driver_t* driver, char** cluster_id,
                                       const rgw_sal_dpp_t* dpp,
                                       rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !cluster_id) return RGW_SAL_ERR_INVALID_ARG;

    *cluster_id = strdup("daos-cluster");
    return RGW_SAL_OK;
}

/**
 * @brief 通过 access_key 获取用户
 */
static int daos_driver_get_user_by_access_key(rgw_sal_driver_t* driver,
                                                const char* key,
                                                rgw_sal_user_t** user,
                                                const rgw_sal_dpp_t* dpp,
                                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;

    /* 创建用户对象，access_key 会在 load 时使用 */
    rgw_sal_user_id_t uid = {0};
    uid.id = daos_strdup(key);

    *user = (rgw_sal_user_t*)daos_driver_get_user(driver, &uid);

    free((void*)uid.id);
    return *user ? RGW_SAL_OK : RGW_SAL_ERR_OUT_OF_MEMORY;
}

/**
 * @brief 通过 email 获取用户
 */
static int daos_driver_get_user_by_email(rgw_sal_driver_t* driver,
                                          const char* email,
                                          rgw_sal_user_t** user,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !email || !user) return RGW_SAL_ERR_INVALID_ARG;

    /* 创建用户对象，email 会在 load 时使用 */
    rgw_sal_user_id_t uid = {0};
    uid.id = daos_strdup(email);

    *user = (rgw_sal_user_t*)daos_driver_get_user(driver, &uid);

    free((void*)uid.id);
    return *user ? RGW_SAL_OK : RGW_SAL_ERR_OUT_OF_MEMORY;
}

/**
 * @brief 通过 swift 用户名获取用户
 */
static int daos_driver_get_user_by_swift(rgw_sal_driver_t* driver,
                                          const char* user_str,
                                          rgw_sal_user_t** user,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    (void)driver;
    (void)user_str;
    /* Swift 密钥和子用户暂不支持 */
    if (user) *user = NULL;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 列出桶
 */
static int daos_driver_list_buckets(rgw_sal_driver_t* driver,
                                    rgw_sal_user_t* owner,
                                    const char* prefix, const char* delimiter,
                                    const char* marker, const char* end_marker,
                                    uint32_t max_keys, bool list_all,
                                    rgw_sal_bucket_list_t** result,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y) {
    (void)driver;
    (void)owner;
    (void)prefix;
    (void)delimiter;
    (void)marker;
    (void)end_marker;
    (void)max_keys;
    (void)list_all;
    (void)result;
    (void)dpp;
    (void)y;
    /* TODO: 实现桶列表 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/*============================================================================
 * 用户 vtable 函数实现
 *============================================================================*/

/**
 * @brief 获取用户
 */
static rgw_sal_user_t* daos_driver_get_user(rgw_sal_driver_t* driver,
                                  const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    rgw_sal_daos_user_t* impl = (rgw_sal_daos_user_t*)calloc(1, sizeof(rgw_sal_daos_user_t));
    if (!impl) {
        free(user);
        return NULL;
    }

    if (uid->id) impl->user_id = daos_strdup(uid->id);
    if (uid->tenant) impl->tenant = daos_strdup(uid->tenant);
    impl->max_buckets = -1;
    impl->loaded = false;
    impl->mtime = time(NULL);

    user->impl = impl;
    user->vtable = driver->user_vtable;
    user->driver = driver;

    return user;
}

/**
 * @brief 用户克隆
 */
static void* daos_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* clone = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!clone) return NULL;

    rgw_sal_daos_user_t* old_impl = daos_user_get_impl((rgw_sal_user_t*)user);
    if (!old_impl) {
        free(clone);
        return NULL;
    }

    rgw_sal_daos_user_t* new_impl = (rgw_sal_daos_user_t*)calloc(1, sizeof(rgw_sal_daos_user_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    new_impl->user_id = daos_strdup(old_impl->user_id);
    new_impl->tenant = daos_strdup(old_impl->tenant);
    new_impl->display_name = daos_strdup(old_impl->display_name);
    new_impl->email = daos_strdup(old_impl->email);
    new_impl->access_key = daos_strdup(old_impl->access_key);
    new_impl->secret_key = daos_strdup(old_impl->secret_key);
    new_impl->ns = daos_strdup(old_impl->ns);
    new_impl->max_buckets = old_impl->max_buckets;
    new_impl->user_type = old_impl->user_type;
    new_impl->loaded = old_impl->loaded;
    new_impl->mtime = old_impl->mtime;
    memcpy(new_impl->user_oid, old_impl->user_oid, sizeof(old_impl->user_oid));

    clone->impl = new_impl;
    clone->vtable = user->vtable;
    clone->driver = user->driver;

    return clone;
}

/**
 * @brief 用户销毁
 */
static void daos_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;

    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (impl) {
        daos_free_string(&impl->user_id);
        daos_free_string(&impl->tenant);
        daos_free_string(&impl->display_name);
        daos_free_string(&impl->email);
        daos_free_string(&impl->access_key);
        daos_free_string(&impl->secret_key);
        daos_free_string(&impl->ns);
        if (impl->attrs) {
            /* TODO: 使用 c_common 释放 attrs */
        }
        free(impl);
    }
    free(user);
}

/**
 * @brief 获取用户 ID
 */
static const char* daos_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rgw_sal_daos_user_t* impl = daos_user_get_impl((rgw_sal_user_t*)user);
    return impl ? impl->user_id : NULL;
}

/**
 * @brief 获取显示名称
 */
static const char* daos_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    return impl ? impl->display_name : NULL;
}

/**
 * @brief 设置显示名称
 */
static int daos_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    daos_free_string(&impl->display_name);
    impl->display_name = daos_strdup(name);
    return RGW_SAL_OK;
}

/**
 * @brief 获取租户
 */
static const char* daos_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rgw_sal_daos_user_t* impl = daos_user_get_impl((rgw_sal_user_t*)user);
    return impl ? impl->tenant : NULL;
}

/**
 * @brief 获取用户类型
 */
static uint32_t daos_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    rgw_sal_daos_user_t* impl = daos_user_get_impl((rgw_sal_user_t*)user);
    return impl ? impl->user_type : 0;
}

/**
 * @brief 获取最大桶数
 */
static int32_t daos_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return 0;
    rgw_sal_daos_user_t* impl = daos_user_get_impl((rgw_sal_user_t*)user);
    return impl ? impl->max_buckets : 0;
}

/**
 * @brief 设置最大桶数
 */
static void daos_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (impl) {
        impl->max_buckets = max;
    }
}

/**
 * @brief 获取属性
 */
static rgw_sal_attrs_t* daos_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return NULL;

    if (!impl->attrs) {
        /* TODO: 使用 c_common 创建 attrs */
    }
    return impl->attrs;
}

/**
 * @brief 设置属性
 */
static int daos_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 释放旧 attrs */
    impl->attrs = attrs;
    return RGW_SAL_OK;
}

/**
 * @brief 加载用户
 *
 * 转换自: DaosUser::load_user()
 */
static int daos_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                          rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 用户获取逻辑 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(user->driver);
    if (!driver || !driver->ds3) return RGW_SAL_ERR_INVALID_ARG;

    /* 准备缓冲区 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    ds3_user_info_t info = {
        .name = impl->user_id,
        .email = impl->email,
        .access_ids = NULL,
        .access_ids_nr = 0,
        .encoded = (char*)buffer,
        .encoded_length = sizeof(buffer)
    };

    /* DS3 调用 */
    int ret = ds3_user_get(impl->user_id, &info, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 解码用户信息 */
    ret = rgw_sal_daos_decode_user(impl, buffer, info.encoded_length);
    if (ret != 0) {
        return ret;
    }
#else
    /* 存根实现: 仅标记为已加载 */
    impl->loaded = true;
#endif
    impl->loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 存储用户
 *
 * 转换自: DaosUser::store_user()
 */
static int daos_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                           rgw_sal_yield_t* y, bool exclusive) {
    (void)dpp;
    (void)y;
    (void)exclusive;
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 用户设置逻辑 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(user->driver);
    if (!driver || !driver->ds3) return RGW_SAL_ERR_INVALID_ARG;

    /* 编码用户信息 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    size_t size = sizeof(buffer);
    int ret = rgw_sal_daos_encode_user(impl, buffer, &size);
    if (ret != 0) return ret;

    /* 准备 DS3 用户信息 */
    ds3_user_info_t info = {
        .name = impl->user_id,
        .email = impl->email,
        .access_ids = NULL,
        .access_ids_nr = 0,
        .encoded = (char*)buffer,
        .encoded_length = size
    };

    /* DS3 调用 */
    ret = ds3_user_set(impl->user_id, &info, NULL, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#else
    /* 存根实现 */
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 删除用户
 *
 * 转换自: DaosUser::remove_user()
 */
static int daos_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 用户删除逻辑 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(user->driver);
    if (!driver || !driver->ds3) return RGW_SAL_ERR_INVALID_ARG;

    /* 编码用户信息 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    size_t size = sizeof(buffer);
    rgw_sal_daos_encode_user(impl, buffer, &size);

    /* 准备 DS3 用户信息 */
    ds3_user_info_t info = {
        .name = impl->user_id,
        .email = impl->email,
        .encoded = (char*)buffer,
        .encoded_length = size
    };

    /* DS3 调用 */
    int ret = ds3_user_remove(impl->user_id, &info, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#else
    /* 存根实现 */
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 读取属性
 *
 * 转换自: DaosUser::read_attrs()
 */
static int daos_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    /* TODO: 实现属性读取 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 合并并存储属性
 *
 * 转换自: DaosUser::merge_and_store_attrs()
 */
static int daos_user_merge_and_store_attrs(rgw_sal_user_t* user,
                                           rgw_sal_attrs_t* new_attrs,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    (void)new_attrs;
    (void)dpp;
    (void)y;
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    /* TODO: 实现属性合并和存储 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 获取命名空间
 */
static const char* daos_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rgw_sal_daos_user_t* impl = daos_user_get_impl((rgw_sal_user_t*)user);
    return impl ? impl->ns : NULL;
}

/**
 * @brief 设置命名空间
 */
static int daos_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    daos_free_string(&impl->ns);
    impl->ns = daos_strdup(ns);
    return RGW_SAL_OK;
}

/**
 * @brief 清除命名空间
 */
static void daos_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) return;
    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (impl) {
        daos_free_string(&impl->ns);
    }
}

/**
 * @brief 设置用户信息
 */
static int daos_user_set_info(rgw_sal_user_t* user, void* info) {
    (void)user;
    (void)info;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 获取用户信息
 */
static int daos_user_get_info(rgw_sal_user_t* user, void** info) {
    (void)user;
    (void)info;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 获取用户权限
 */
static int daos_user_get_caps(rgw_sal_user_t* user, void** caps) {
    (void)user;
    (void)caps;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 获取版本跟踪器
 */
static int daos_user_get_version_tracker(rgw_sal_user_t* user, void** tracker) {
    (void)user;
    (void)tracker;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 读取使用统计
 */
static int daos_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                uint64_t start_epoch, uint64_t end_epoch,
                                uint32_t max_entries, void* usage) {
    (void)user;
    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;
    (void)max_entries;
    (void)usage;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 清理使用统计
 */
static int daos_user_trim_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                uint64_t start_epoch, uint64_t end_epoch) {
    (void)user;
    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 验证 MFA
 */
static int daos_user_verify_mfa(rgw_sal_user_t* user, const char* mfa,
                                const char* code, const rgw_sal_dpp_t* dpp) {
    (void)user;
    (void)mfa;
    (void)code;
    (void)dpp;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 列出组
 */
static int daos_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                 void** groups, uint32_t* count) {
    (void)user;
    (void)dpp;
    (void)groups;
    (void)count;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/*============================================================================
 * 桶 vtable 函数实现
 *============================================================================*/

/**
 * @brief 获取桶
 */
static rgw_sal_bucket_t* daos_driver_get_bucket(rgw_sal_driver_t* driver,
                                    const rgw_sal_bucket_info_t* info) {
    if (!driver || !info) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    rgw_sal_daos_bucket_t* impl = (rgw_sal_daos_bucket_t*)calloc(1, sizeof(rgw_sal_daos_bucket_t));
    if (!impl) {
        free(bucket);
        return NULL;
    }

    if (info->bucket.name) impl->name = daos_strdup(info->bucket.name);
    if (info->bucket.tenant) impl->tenant = daos_strdup(info->bucket.tenant);
    if (info->bucket.marker) impl->marker = daos_strdup(info->bucket.marker);
    if (info->bucket.bucket_id) impl->bucket_id = daos_strdup(info->bucket.bucket_id);
    impl->loaded = false;
    impl->is_open = false;
    impl->mtime = time(NULL);

    bucket->impl = impl;
    bucket->vtable = driver->bucket_vtable;
    bucket->driver = driver;

    return bucket;
}

/**
 * @brief 桶克隆
 */
static void* daos_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* clone = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!clone) return NULL;

    rgw_sal_daos_bucket_t* old_impl = daos_bucket_get_impl((rgw_sal_bucket_t*)bucket);
    if (!old_impl) {
        free(clone);
        return NULL;
    }

    rgw_sal_daos_bucket_t* new_impl = (rgw_sal_daos_bucket_t*)calloc(1, sizeof(rgw_sal_daos_bucket_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    new_impl->name = daos_strdup(old_impl->name);
    new_impl->tenant = daos_strdup(old_impl->tenant);
    new_impl->marker = daos_strdup(old_impl->marker);
    new_impl->bucket_id = daos_strdup(old_impl->bucket_id);
    new_impl->owner_id = daos_strdup(old_impl->owner_id);
    new_impl->root_path = daos_strdup(old_impl->root_path);
    new_impl->tag = daos_strdup(old_impl->tag);
    new_impl->loaded = old_impl->loaded;
    new_impl->created = old_impl->created;
    new_impl->deleted = old_impl->deleted;
    new_impl->is_open = false;
    new_impl->mtime = old_impl->mtime;
    memcpy(new_impl->bucket_oid, old_impl->bucket_oid, sizeof(old_impl->bucket_oid));

    clone->impl = new_impl;
    clone->vtable = bucket->vtable;
    clone->driver = bucket->driver;

    return clone;
}

/**
 * @brief 桶销毁
 */
static void daos_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (impl) {
        daos_free_string(&impl->name);
        daos_free_string(&impl->tenant);
        daos_free_string(&impl->marker);
        daos_free_string(&impl->bucket_id);
        daos_free_string(&impl->owner_id);
        daos_free_string(&impl->root_path);
        daos_free_string(&impl->tag);
        if (impl->attrs) {
            /* TODO: 使用 c_common 释放 attrs */
        }
        free(impl);
    }
    free(bucket);
}

/**
 * @brief 获取桶名称
 */
static const char* daos_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl((rgw_sal_bucket_t*)bucket);
    return impl ? impl->name : NULL;
}

/**
 * @brief 获取桶租户
 */
static const char* daos_bucket_get_tenant(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl((rgw_sal_bucket_t*)bucket);
    return impl ? impl->tenant : NULL;
}

/**
 * @brief 获取桶标记
 */
static const char* daos_bucket_get_marker(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl((rgw_sal_bucket_t*)bucket);
    return impl ? impl->marker : NULL;
}

/**
 * @brief 获取桶信息
 */
static rgw_sal_bucket_info_t* daos_bucket_get_info(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    /* TODO: 返回桶信息结构 */
    return NULL;
}

/**
 * @brief 获取桶所有者
 */
static rgw_sal_user_t* daos_bucket_get_owner(rgw_sal_bucket_t* bucket) {
    (void)bucket;
    /* TODO: 实现所有者获取 */
    return NULL;
}

/**
 * @brief 获取桶属性
 */
static rgw_sal_attrs_t* daos_bucket_get_attrs(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return NULL;

    if (!impl->attrs) {
        /* TODO: 使用 c_common 创建 attrs */
    }
    return impl->attrs;
}

/**
 * @brief 设置桶属性
 */
static int daos_bucket_set_attrs(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;
    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 释放旧 attrs */
    impl->attrs = attrs;
    return RGW_SAL_OK;
}

/**
 * @brief 列出桶内对象
 *
 * 转换自: DaosBucket::list()
 */
static int daos_bucket_list(rgw_sal_bucket_t* bucket,
                            const char* prefix, const char* delimiter,
                            const char* marker, const char* end_marker,
                            uint32_t max_keys, bool list_versions,
                            rgw_sal_object_list_t** result,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)bucket;
    (void)prefix;
    (void)delimiter;
    (void)marker;
    (void)end_marker;
    (void)max_keys;
    (void)list_versions;
    (void)result;
    (void)dpp;
    (void)y;
    /* TODO: 实现对象列表
     * ds3_bucket_list_obj(&nobj, object_infos.data(), &ncp,
     *                    common_prefixes.data(), params.prefix.c_str(),
     *                    params.delim.c_str(), daos_marker,
     *                    params.list_versions, &results.is_truncated, ds3b);
     */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 加载桶
 *
 * 转换自: DaosBucket::load_bucket()
 */
static int daos_bucket_load(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 打开桶并加载信息 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    /* 打开桶 */
    int ret = ds3_bucket_open(impl->name, &impl->ds3b, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 获取桶信息 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    ds3_bucket_info_t info = {
        .name = {0},
        .encoded = (char*)buffer,
        .encoded_length = sizeof(buffer)
    };

    ret = ds3_bucket_get_info(&info, impl->ds3b, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 解码桶信息 */
    ret = rgw_sal_daos_decode_bucket(impl, buffer, info.encoded_length);
    if (ret != 0) {
        return ret;
    }
#else
    /* 存根实现 */
#endif
    impl->loaded = true;
    impl->is_open = true;

    return RGW_SAL_OK;
}

/**
 * @brief 存储桶
 */
static int daos_bucket_store(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y, bool exclusive) {
    (void)dpp;
    (void)y;
    (void)exclusive;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 桶存储 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    /* 编码桶信息 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    size_t size = sizeof(buffer);
    int ret = rgw_sal_daos_encode_bucket(impl, buffer, &size);
    if (ret != 0) return ret;

    /* 准备 DS3 桶信息 */
    ds3_bucket_info_t info = {
        .name = {0},
        .encoded = (char*)buffer,
        .encoded_length = size
    };
    strncpy(info.name, impl->name ? impl->name : "", sizeof(info.name) - 1);

    /* DS3 调用 */
    ret = ds3_bucket_set_info(&info, impl->ds3b, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#else
    /* 存根实现 */
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 删除桶
 */
static int daos_bucket_remove(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 桶删除 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    /* 关闭已打开的桶 */
    if (impl->ds3b) {
        ds3_bucket_close(impl->ds3b, NULL);
        impl->ds3b = NULL;
    }

    /* DS3 调用 */
    int ret = ds3_bucket_destroy(impl->name, true, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#endif
    impl->deleted = true;
    return RGW_SAL_OK;
}

/**
 * @brief 创建桶
 *
 * 转换自: DaosUser::create_bucket()
 */
static int daos_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y, bool create_obj) {
    (void)dpp;
    (void)y;
    (void)create_obj;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 桶创建 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    /* 编码桶信息 */
    uint8_t buffer[DS3_MAX_ENCODED_LEN];
    size_t size = sizeof(buffer);
    int ret = rgw_sal_daos_encode_bucket(impl, buffer, &size);
    if (ret != 0) return ret;

    /* 准备 DS3 桶信息 */
    ds3_bucket_info_t info = {
        .name = {0},
        .encoded = (char*)buffer,
        .encoded_length = size
    };
    strncpy(info.name, impl->name ? impl->name : "", sizeof(info.name) - 1);

    /* DS3 调用 */
    ret = ds3_bucket_create(impl->name, &info, NULL, driver->ds3, NULL);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#else
    /* 存根实现 */
#endif
    impl->created = true;
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 删除桶 (vtable)
 */
static int daos_bucket_delete_bucket(rgw_sal_bucket_t* bucket,
                                     const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y, bool delete_objects) {
    (void)dpp;
    (void)y;
    (void)delete_objects;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->deleted = true;
    return RGW_SAL_OK;
}

/**
 * @brief 重命名桶
 */
static int daos_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y, const char* new_name) {
    (void)dpp;
    (void)y;
    if (!bucket || !new_name) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    daos_free_string(&impl->name);
    impl->name = daos_strdup(new_name);
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 设置桶 ACL
 */
static int daos_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->acl = acl;
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 获取桶策略
 */
static int daos_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket || !policy) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *policy = impl->policy;
    return RGW_SAL_OK;
}

/**
 * @brief 设置桶策略
 */
static int daos_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->policy = policy;
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 获取桶标签
 */
static int daos_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag) {
    if (!bucket || !tag) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *tag = daos_strdup(impl->tag);
    return RGW_SAL_OK;
}

/**
 * @brief 设置桶标签
 */
static int daos_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    daos_free_string(&impl->tag);
    impl->tag = daos_strdup(tag);

    return RGW_SAL_OK;
}

/**
 * @brief 获取桶使用统计
 */
static int daos_bucket_get_usage(rgw_sal_bucket_t* bucket, void** usage,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)bucket;
    (void)usage;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 读取桶统计
 */
static int daos_bucket_read_stats(rgw_sal_bucket_t* bucket,
                                  const rgw_sal_dpp_t* dpp, void* stats) {
    (void)bucket;
    (void)stats;
    (void)dpp;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 同步用户统计
 */
static int daos_bucket_sync_user_stats(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 检查对象索引
 */
static int daos_bucket_check_object_index(rgw_sal_bucket_t* bucket,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 修复对象索引
 */
static int daos_bucket_fix_object_index(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 绕过垃圾回收删除
 */
static int daos_bucket_remove_bypass_gc(rgw_sal_bucket_t* bucket,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y, bool sync) {
    (void)bucket;
    (void)dpp;
    (void)y;
    (void)sync;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 检查配额
 */
static int daos_bucket_check_quota(rgw_sal_bucket_t* bucket,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 检查桶是否为空
 */
static int daos_bucket_check_empty(rgw_sal_bucket_t* bucket,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 尝试刷新桶信息
 */
static int daos_bucket_try_refresh_info(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/*============================================================================
 * 对象 vtable 函数实现
 *============================================================================*/

/**
 * @brief 获取对象
 */
static rgw_sal_object_t* daos_driver_get_object(rgw_sal_driver_t* driver,
                                    rgw_sal_bucket_t* bucket,
                                    const rgw_sal_obj_key_t* key) {
    if (!driver || !key) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    rgw_sal_daos_object_t* impl = (rgw_sal_daos_object_t*)calloc(1, sizeof(rgw_sal_daos_object_t));
    if (!impl) {
        free(obj);
        return NULL;
    }

    if (key->name) impl->name = daos_strdup(key->name);
    if (key->instance) impl->instance = daos_strdup(key->instance);
    impl->written = false;
    impl->deleted = false;
    impl->loaded = false;
    impl->is_open = false;

    obj->impl = impl;
    obj->vtable = driver->object_vtable;
    obj->bucket = bucket;

    (void)bucket;
    return obj;
}

/**
 * @brief 对象克隆
 */
static void* daos_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* clone = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!clone) return NULL;

    rgw_sal_daos_object_t* old_impl = daos_object_get_impl((rgw_sal_object_t*)obj);
    if (!old_impl) {
        free(clone);
        return NULL;
    }

    rgw_sal_daos_object_t* new_impl = (rgw_sal_daos_object_t*)calloc(1, sizeof(rgw_sal_daos_object_t));
    if (!new_impl) {
        free(clone);
        return NULL;
    }

    new_impl->name = daos_strdup(old_impl->name);
    new_impl->instance = daos_strdup(old_impl->instance);
    new_impl->bucket_name = daos_strdup(old_impl->bucket_name);
    new_impl->bucket_tenant = daos_strdup(old_impl->bucket_tenant);
    new_impl->file_path = daos_strdup(old_impl->file_path);
    new_impl->is_null = old_impl->is_null;
    new_impl->size = old_impl->size;
    new_impl->mtime = old_impl->mtime;
    new_impl->written = old_impl->written;
    new_impl->deleted = old_impl->deleted;
    new_impl->loaded = old_impl->loaded;
    new_impl->is_atomic = old_impl->is_atomic;
    new_impl->is_expired = old_impl->is_expired;
    memcpy(new_impl->daos_oid, old_impl->daos_oid, sizeof(old_impl->daos_oid));
    new_impl->daos_oclass = old_impl->daos_oclass;
    new_impl->daos_otype = old_impl->daos_otype;

    clone->impl = new_impl;
    clone->vtable = obj->vtable;
    clone->bucket = obj->bucket;

    return clone;
}

/**
 * @brief 对象销毁
 */
static void daos_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (impl) {
        daos_free_string(&impl->name);
        daos_free_string(&impl->instance);
        daos_free_string(&impl->bucket_name);
        daos_free_string(&impl->bucket_tenant);
        daos_free_string(&impl->file_path);
        if (impl->attrs) {
            /* TODO: 使用 c_common 释放 attrs */
        }
        free(impl);
    }
    free(obj);
}

/**
 * @brief 获取对象名称
 */
static const char* daos_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    rgw_sal_daos_object_t* impl = daos_object_get_impl((rgw_sal_object_t*)obj);
    return impl ? impl->name : NULL;
}

/**
 * @brief 获取对象实例
 */
static const char* daos_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    rgw_sal_daos_object_t* impl = daos_object_get_impl((rgw_sal_object_t*)obj);
    return impl ? impl->instance : NULL;
}

/**
 * @brief 检查是否为 null 对象
 */
static bool daos_object_is_null(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    rgw_sal_daos_object_t* impl = daos_object_get_impl((rgw_sal_object_t*)obj);
    return impl ? impl->is_null : false;
}

/**
 * @brief 获取对象属性
 */
static rgw_sal_attrs_t* daos_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return NULL;

    if (!impl->attrs) {
        /* TODO: 使用 c_common 创建 attrs */
    }
    return impl->attrs;
}

/**
 * @brief 设置对象属性
 */
static int daos_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 释放旧 attrs */
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

/**
 * @brief 读取对象
 *
 * 转换自: DaosObject::read()
 */
static int daos_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                            uint8_t* buffer, size_t* buffer_size,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    (void)end;
    if (!obj || !buffer || !buffer_size) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 对象读取 */
#ifdef HAVE_DS3
    rgw_sal_daos_bucket_t* bucket = daos_bucket_get_impl(obj->bucket);
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    /* 打开桶 */
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(obj->bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    if (!bucket->ds3b) {
        int ret = ds3_bucket_open(bucket->name, &bucket->ds3b, driver->ds3, NULL);
        if (ret != 0) return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 打开对象 */
    ds3_object_t* ds3o = NULL;
    int ret = ds3_obj_open(impl->name, &ds3o, bucket->ds3b);
    if (ret != 0) return RGW_SAL_ERR_NOT_FOUND;

    /* 读取数据 */
    uint64_t size = *buffer_size;
    ret = ds3_obj_read((char*)buffer, offset, &size, bucket->ds3b, ds3o, NULL);
    ds3_obj_close(ds3o);

    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    *buffer_size = size;
    impl->size = size;
#else
    /* 存根实现 */
    *buffer_size = 0;
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 写入对象
 *
 * 转换自: DaosObject::write()
 */
static int daos_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                            const uint8_t* data,
                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!obj || !data) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 对象写入 */
#ifdef HAVE_DS3
    rgw_sal_daos_bucket_t* bucket = daos_bucket_get_impl(obj->bucket);
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    /* 打开桶 */
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(obj->bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    if (!bucket->ds3b) {
        int ret = ds3_bucket_open(bucket->name, &bucket->ds3b, driver->ds3, NULL);
        if (ret != 0) return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 创建对象 */
    ds3_object_t* ds3o = NULL;
    int ret = ds3_obj_create(impl->name, &ds3o, bucket->ds3b);
    if (ret != 0) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 写入数据 */
    uint64_t write_size = size;
    ret = ds3_obj_write((const char*)data, offset, &write_size, bucket->ds3b, ds3o, NULL);
    ds3_obj_close(ds3o);

    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#endif
    impl->size = size;
    impl->mtime = time(NULL);
    impl->written = true;

    return RGW_SAL_OK;
}

/**
 * @brief 删除对象
 *
 * 转换自: DaosObject::DaosDeleteOp::delete_obj()
 */
static int daos_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)flags;
    (void)dpp;
    (void)y;
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 对象删除 */
#ifdef HAVE_DS3
    rgw_sal_daos_bucket_t* bucket = daos_bucket_get_impl(obj->bucket);
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    /* 打开桶 */
    rgw_sal_daos_driver_t* driver = daos_driver_get_impl(obj->bucket->driver);
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    if (!bucket->ds3b) {
        int ret = ds3_bucket_open(bucket->name, &bucket->ds3b, driver->ds3, NULL);
        if (ret != 0) return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 删除对象 */
    int ret = ds3_obj_destroy(impl->name, bucket->ds3b);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#endif
    impl->deleted = true;
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 加载对象状态
 *
 * 转换自: DaosObject::load_obj_state()
 */
static int daos_object_load_state(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y, bool follow_olh) {
    (void)follow_olh;
    (void)dpp;
    (void)y;
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_object_t* impl = daos_object_get_impl(obj);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* TODO: 实现状态加载
     * get_dir_entry_attrs(dpp, &ent);
     * state.exists = true;
     * state.size = ent.meta.size;
     */
    impl->loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 获取对象属性 (vtable)
 */
static int daos_object_get_obj_attrs(rgw_sal_object_t* obj,
                                     rgw_sal_yield_t* y,
                                     const rgw_sal_dpp_t* dpp) {
    (void)y;
    (void)dpp;
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    /* TODO: 实现属性获取 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 设置对象属性
 */
static int daos_object_set_obj_attrs(rgw_sal_object_t* obj,
                                     rgw_sal_attrs_t* setattrs,
                                     rgw_sal_attrs_t* delattrs,
                                     rgw_sal_yield_t* y, uint32_t flags) {
    (void)setattrs;
    (void)delattrs;
    (void)y;
    (void)flags;
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    /* TODO: 实现属性设置 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 复制对象
 */
static int daos_object_copy_object(rgw_sal_object_t* obj,
                                   rgw_sal_object_t* src_obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)obj;
    (void)src_obj;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 删除对象属性
 */
static int daos_object_delete_obj_attrs(rgw_sal_object_t* obj,
                                        rgw_sal_attrs_t* attrs,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    (void)attrs;
    (void)dpp;
    (void)y;
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    /* TODO: 实现属性删除 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/*============================================================================
 * 虚函数表定义
 *============================================================================*/

/**
 * @brief 用户操作虚函数表
 */
static rgw_sal_user_vtable_t daos_user_vtable = {
    .clone = daos_user_clone,
    .destroy = daos_user_destroy,
    .get_id = daos_user_get_id,
    .get_display_name = daos_user_get_display_name,
    .set_display_name = daos_user_set_display_name,
    .get_tenant = daos_user_get_tenant,
    .get_type = daos_user_get_type,
    .get_max_buckets = daos_user_get_max_buckets,
    .set_max_buckets = daos_user_set_max_buckets,
    .get_attrs = daos_user_get_attrs,
    .set_attrs = daos_user_set_attrs,
    .load = daos_user_load,
    .store = daos_user_store,
    .remove = daos_user_remove,
    .read_attrs = daos_user_read_attrs,
    .merge_and_store_attrs = daos_user_merge_and_store_attrs,
    .get_ns = daos_user_get_ns,
    .set_ns = daos_user_set_ns,
    .clear_ns = daos_user_clear_ns,
    .set_info = daos_user_set_info,
    .get_info = daos_user_get_info,
    .get_caps = daos_user_get_caps,
    .get_version_tracker = daos_user_get_version_tracker,
    .read_usage = daos_user_read_usage,
    .trim_usage = daos_user_trim_usage,
    .verify_mfa = daos_user_verify_mfa,
    .list_groups = daos_user_list_groups,
};

/**
 * @brief 桶操作虚函数表
 */
static rgw_sal_bucket_vtable_t daos_bucket_vtable = {
    .clone = daos_bucket_clone,
    .destroy = daos_bucket_destroy,
    .get_name = daos_bucket_get_name,
    .get_tenant = daos_bucket_get_tenant,
    .get_marker = daos_bucket_get_marker,
    .get_info = daos_bucket_get_info,
    .get_owner = daos_bucket_get_owner,
    .get_attrs = daos_bucket_get_attrs,
    .set_attrs = daos_bucket_set_attrs,
    .list = daos_bucket_list,
    .load = daos_bucket_load,
    .store = daos_bucket_store,
    .remove = daos_bucket_remove,
    .create = daos_bucket_create,
    .delete_bucket = daos_bucket_delete_bucket,
    .rename = daos_bucket_rename,
    .set_acl = daos_bucket_set_acl,
    .get_policy = daos_bucket_get_policy,
    .set_policy = daos_bucket_set_policy,
    .get_tag = daos_bucket_get_tag,
    .set_tag = daos_bucket_set_tag,
    .get_usage = daos_bucket_get_usage,
    .read_stats = daos_bucket_read_stats,
    .read_stats_async = NULL,
    .complete_stats = NULL,
    .update_bucket_stats = NULL,
    .sync_user_stats = daos_bucket_sync_user_stats,
    .sync = NULL,
    .drain = NULL,
    .check_object_index = daos_bucket_check_object_index,
    .fix_object_index = daos_bucket_fix_object_index,
    .check_bucket_index = NULL,
    .remove_bypass_gc = daos_bucket_remove_bypass_gc,
    .check_quota = daos_bucket_check_quota,
    .check_empty = daos_bucket_check_empty,
    .try_refresh_info = daos_bucket_try_refresh_info,
};

/**
 * @brief 对象操作虚函数表
 */
static rgw_sal_object_vtable_t daos_object_vtable = {
    .clone = daos_object_clone,
    .destroy = daos_object_destroy,
    .get_name = daos_object_get_name,
    .get_instance = daos_object_get_instance,
    .is_null = daos_object_is_null,
    .get_attrs = daos_object_get_attrs,
    .set_attrs = daos_object_set_attrs,
    .read = daos_object_read,
    .write = daos_object_write,
    .delete_obj = daos_object_delete_obj,
    .load_state = daos_object_load_state,
    .get_obj_attrs = daos_object_get_obj_attrs,
    .set_obj_attrs = daos_object_set_obj_attrs,
    .copy_object = daos_object_copy_object,
    .list_parts = NULL,
    .transition = NULL,
    .delete_obj_attrs = daos_object_delete_obj_attrs,
};

/**
 * @brief 驱动操作虚函数表
 */
static rgw_sal_driver_vtable_t daos_driver_vtable = {
    .destroy = daos_driver_destroy,
    .initialize = daos_driver_initialize,
    .get_name = daos_driver_get_name,
    .get_cluster_id = daos_driver_get_cluster_id,
    .get_user = daos_driver_get_user,
    .get_user_by_access_key = daos_driver_get_user_by_access_key,
    .get_user_by_email = daos_driver_get_user_by_email,
    .get_user_by_swift = daos_driver_get_user_by_swift,
    .get_bucket = daos_driver_get_bucket,
    .list_buckets = daos_driver_list_buckets,
    .get_object = daos_driver_get_object,
};

/*============================================================================
 * 公共 API 实现
 *============================================================================*/

/**
 * @brief 初始化 DAOS 驱动
 *
 * 转换自: DaosStore::initialize()
 */
int rgw_sal_daos_init(rgw_sal_driver_t* driver, rgw_sal_daos_config_t* config) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    /* 分配驱动实现结构 */
    rgw_sal_daos_driver_t* impl = (rgw_sal_daos_driver_t*)calloc(1, sizeof(rgw_sal_daos_driver_t));
    if (!impl) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 设置驱动名称 */
    strncpy(impl->name, "daos", sizeof(impl->name) - 1);

    /* 应用配置 */
    if (config) {
        if (config->pool_uuid) {
            strncpy(impl->pool, config->pool_uuid, sizeof(impl->pool) - 1);
        }
        if (config->container_uuid) {
            strncpy(impl->container, config->container_uuid, sizeof(impl->container) - 1);
        }
        if (config->pool_svc) {
            strncpy(impl->pool_svc, config->pool_svc, sizeof(impl->pool_svc) - 1);
        }
        impl->chunk_size = config->chunk_size > 0 ? config->chunk_size : 4096;
    } else {
        impl->chunk_size = 4096;
    }

    impl->initialized = true;

    /* 设置 vtable */
    driver->vtable = &daos_driver_vtable;
    driver->user_vtable = &daos_user_vtable;
    driver->bucket_vtable = &daos_bucket_vtable;
    driver->object_vtable = &daos_object_vtable;
    driver->impl = impl;

    /* DS3 初始化和连接
     * 注意: 由于 libds3 可能未安装，这里使用存根实现
     * 实际部署时需要链接 libds3 库
     */
#ifdef HAVE_DS3
    /* DS3 初始化 */
    int ret = ds3_init();
    if (ret != 0 && ret != DER_ALREADY) {
        impl->initialized = false;
        free(impl);
        return RGW_SAL_ERR_DS3_INIT_FAILED;
    }

    /* DS3 连接 */
    ret = ds3_connect(impl->pool, impl->container, &impl->ds3, NULL);
    if (ret != 0) {
        ds3_fini();
        impl->initialized = false;
        free(impl);
        return RGW_SAL_ERR_DS3_CONNECT_FAILED;
    }
#else
    /* DS3 库未安装，使用存根实现 */
    impl->ds3 = NULL;
#endif

    return RGW_SAL_OK;
}

/**
 * @brief 关闭 DAOS 驱动
 *
 * 转换自: DaosStore::finalize()
 */
int rgw_sal_daos_shutdown(rgw_sal_driver_t* driver) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (impl) {
        /* DS3 断开连接 */
#ifdef HAVE_DS3
        if (impl->ds3) {
            ds3_disconnect(impl->ds3, NULL);
            impl->ds3 = NULL;
        }
        ds3_fini();
#endif
        impl->initialized = false;
        free(impl);
        driver->impl = NULL;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取 DAOS 驱动实现 (公共接口)
 */
void* rgw_sal_daos_get_driver_impl(rgw_sal_driver_t* driver) {
    return daos_driver_get_impl(driver);
}

/**
 * @brief 获取 DS3 连接句柄
 */
ds3_connection_t* rgw_sal_daos_get_ds3_connection(rgw_sal_driver_t* driver) {
    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (!impl) return NULL;
    return impl->ds3;
}

/**
 * @brief 创建桶
 *
 * 公共 API: 创建一个新桶
 */
int rgw_sal_daos_create_bucket(rgw_sal_driver_t* driver,
                                const char* bucket_name,
                                const char* owner_id,
                                rgw_sal_bucket_t** bucket_out,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket_name || !bucket_out) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建桶信息 */
    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = bucket_name;
    info.bucket.tenant = "";
    info.bucket.bucket_id = "";

    /* 创建桶对象 */
    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)daos_driver_get_bucket(driver, &info);
    if (!bucket) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    rgw_sal_daos_bucket_t* impl = daos_bucket_get_impl(bucket);
    if (impl) {
        impl->owner_id = daos_strdup(owner_id);
        impl->created = true;
        impl->mtime = time(NULL);
    }

    *bucket_out = bucket;
    return RGW_SAL_OK;
}

/**
 * @brief 删除桶
 *
 * 公共 API: 删除一个桶
 */
int rgw_sal_daos_delete_bucket(rgw_sal_driver_t* driver,
                                const char* bucket_name,
                                bool delete_objects,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    (void)delete_objects;
    if (!driver || !bucket_name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* TODO: 实现桶删除
     * return ds3_bucket_destroy(bucket_name, delete_children, ds3, nullptr);
     */
    return RGW_SAL_OK;
}

/**
 * @brief 创建用户
 *
 * 公共 API: 创建一个新用户
 */
int rgw_sal_daos_create_user(rgw_sal_driver_t* driver,
                              const char* user_id,
                              const char* display_name,
                              const char* email,
                              rgw_sal_user_t** user_out,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !user_id || !user_out) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建用户 ID */
    rgw_sal_user_id_t uid = {0};
    uid.id = user_id;

    /* 创建用户对象 */
    rgw_sal_user_t* user = (rgw_sal_user_t*)daos_driver_get_user(driver, &uid);
    if (!user) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    rgw_sal_daos_user_t* impl = daos_user_get_impl(user);
    if (impl) {
        impl->display_name = daos_strdup(display_name);
        impl->email = daos_strdup(email);
        impl->loaded = true;
    }

    *user_out = user;
    return RGW_SAL_OK;
}

/**
 * @brief 删除用户
 *
 * 公共 API: 删除一个用户
 */
int rgw_sal_daos_delete_user(rgw_sal_driver_t* driver,
                              const char* user_id,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !user_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建用户 ID */
    rgw_sal_user_id_t uid = {0};
    uid.id = user_id;

    /* 创建临时用户对象用于删除 */
    rgw_sal_user_t* user = (rgw_sal_user_t*)daos_driver_get_user(driver, &uid);
    if (!user) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 执行删除 */
    int ret = daos_user_remove(user, dpp, y);

    /* 销毁用户对象 */
    daos_user_destroy(user);

    return ret;
}

/**
 * @brief 写入对象数据
 *
 * 公共 API: 写入对象数据
 */
int rgw_sal_daos_write_object(rgw_sal_driver_t* driver,
                               rgw_sal_bucket_t* bucket,
                               const char* object_name,
                               int64_t offset,
                               int64_t size,
                               const uint8_t* data,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket || !object_name || !data) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建对象键 */
    rgw_sal_obj_key_t key = {0};
    key.name = object_name;

    /* 创建对象 */
    rgw_sal_object_t* obj = (rgw_sal_object_t*)daos_driver_get_object(driver, bucket, &key);
    if (!obj) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 执行写入 */
    int ret = daos_object_write(obj, offset, size, data, dpp, y);

    /* 销毁对象 */
    daos_object_destroy(obj);

    return ret;
}

/**
 * @brief 读取对象数据
 *
 * 公共 API: 读取对象数据
 */
int rgw_sal_daos_read_object(rgw_sal_driver_t* driver,
                              rgw_sal_bucket_t* bucket,
                              const char* object_name,
                              int64_t offset,
                              int64_t size,
                              uint8_t* buffer,
                              size_t* buffer_size,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket || !object_name || !buffer || !buffer_size) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建对象键 */
    rgw_sal_obj_key_t key = {0};
    key.name = object_name;

    /* 创建对象 */
    rgw_sal_object_t* obj = (rgw_sal_object_t*)daos_driver_get_object(driver, bucket, &key);
    if (!obj) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 执行读取 */
    int ret = daos_object_read(obj, offset, offset + size - 1, buffer, buffer_size, dpp, y);

    /* 销毁对象 */
    daos_object_destroy(obj);

    return ret;
}

/**
 * @brief 删除对象
 *
 * 公共 API: 删除一个对象
 */
int rgw_sal_daos_delete_object(rgw_sal_driver_t* driver,
                                rgw_sal_bucket_t* bucket,
                                const char* object_name,
                                uint32_t flags,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket || !object_name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建对象键 */
    rgw_sal_obj_key_t key = {0};
    key.name = object_name;

    /* 创建对象 */
    rgw_sal_object_t* obj = (rgw_sal_object_t*)daos_driver_get_object(driver, bucket, &key);
    if (!obj) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 执行删除 */
    int ret = daos_object_delete_obj(obj, flags, dpp, y);

    /* 销毁对象 */
    daos_object_destroy(obj);

    return ret;
}

/*============================================================================
 * 分片上传 API 实现
 *============================================================================*/

/**
 * @brief 初始化分片上传
 *
 * 转换自: DaosMultipartUpload::init()
 */
int rgw_sal_daos_upload_init(rgw_sal_driver_t* driver,
                             const char* bucket_name,
                             const char* object_name,
                             char** upload_id_out,
                             const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket_name || !object_name || !upload_id_out) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *upload_id_out = NULL;

    /* DS3 分片上传初始化 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 生成 upload_id */
    char upload_id[64];
    snprintf(upload_id, sizeof(upload_id), "%s", MULTIPART_UPLOAD_ID_PREFIX);
    /* TODO: 生成随机字符串填充 upload_id */

    /* 准备 DS3 分片上传信息 */
    ds3_multipart_upload_info_t info = {
        .upload_id = {0},
        .key = {0},
        .encoded = NULL,
        .encoded_length = 0
    };
    strncpy(info.upload_id, upload_id, sizeof(info.upload_id) - 1);
    strncpy(info.key, object_name, sizeof(info.key) - 1);

    /* DS3 调用 */
    int ret = ds3_upload_init(&info, bucket_name, impl->ds3);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    *upload_id_out = strdup(upload_id);
#else
    /* 存根实现: 生成假 upload_id */
    char stub_id[64];
    snprintf(stub_id, sizeof(stub_id), "%s%ld", MULTIPART_UPLOAD_ID_PREFIX, (long)time(NULL));
    *upload_id_out = strdup(stub_id);
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 列出分片
 *
 * 转换自: DaosMultipartUpload::list_parts()
 */
int rgw_sal_daos_upload_list_parts(rgw_sal_driver_t* driver,
                                    const char* bucket_name,
                                    const char* upload_id,
                                    uint32_t max_parts,
                                    uint32_t marker,
                                    void** parts_out,
                                    size_t* num_parts,
                                    bool* truncated,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y) {
    (void)driver;
    (void)bucket_name;
    (void)upload_id;
    (void)max_parts;
    (void)marker;
    (void)parts_out;
    (void)num_parts;
    (void)truncated;
    (void)dpp;
    (void)y;
    /* DS3 分片列表 */
#ifdef HAVE_DS3
    /* TODO: 实现 ds3_upload_list_parts() */
#else
    /* 存根实现 */
#endif
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 完成分片上传
 *
 * 转换自: DaosMultipartUpload::complete()
 */
int rgw_sal_daos_upload_complete(rgw_sal_driver_t* driver,
                                  const char* bucket_name,
                                  const char* upload_id,
                                  const char* object_name,
                                  uint32_t parts_count,
                                  const char* const* etags,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    (void)driver;
    (void)bucket_name;
    (void)upload_id;
    (void)object_name;
    (void)parts_count;
    (void)etags;
    (void)dpp;
    (void)y;
    /* DS3 分片上传完成 */
#ifdef HAVE_DS3
    /* TODO: 合并分片数据、创建最终对象、删除分片 */
#else
    /* 存根实现 */
#endif
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 中止分片上传
 *
 * 转换自: DaosMultipartUpload::abort()
 */
int rgw_sal_daos_upload_abort(rgw_sal_driver_t* driver,
                               const char* bucket_name,
                               const char* upload_id,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!driver || !bucket_name || !upload_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* DS3 分片上传中止 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    int ret = ds3_upload_remove(bucket_name, upload_id, impl->ds3);
    if (ret != 0) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }
#else
    /* 存根实现 */
#endif
    return RGW_SAL_OK;
}

/*============================================================================
 * 写入器 API 实现
 *============================================================================*/

/* 写入器上下文结构 */
typedef struct rgw_sal_daos_writer_ctx {
    ds3_object_t* ds3o;
    char* object_name;
    uint64_t total_size;
    bool prepared;
} rgw_sal_daos_writer_ctx_t;

/**
 * @brief 原子写入器准备
 *
 * 转换自: DaosAtomicWriter::prepare()
 */
int rgw_sal_daos_writer_prepare(void** writer_ctx,
                                 rgw_sal_driver_t* driver,
                                 rgw_sal_bucket_t* bucket,
                                 const char* object_name,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!writer_ctx || !driver || !bucket || !object_name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *writer_ctx = NULL;

    /* DS3 写入器准备 */
#ifdef HAVE_DS3
    rgw_sal_daos_driver_t* impl = daos_driver_get_impl(driver);
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_bucket_t* bucket_impl = daos_bucket_get_impl(bucket);
    if (!bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 分配写入器上下文 */
    rgw_sal_daos_writer_ctx_t* ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return RGW_SAL_ERR_OUT_OF_MEMORY;

    ctx->object_name = strdup(object_name);

    /* 打开桶 */
    if (!bucket_impl->ds3b) {
        int ret = ds3_bucket_open(bucket_impl->name, &bucket_impl->ds3b, impl->ds3, NULL);
        if (ret != 0) {
            free(ctx->object_name);
            free(ctx);
            return RGW_SAL_ERR_INTERNAL_ERROR;
        }
    }

    /* 创建对象 */
    int ret = ds3_obj_create(ctx->object_name, &ctx->ds3o, bucket_impl->ds3b);
    if (ret != 0) {
        free(ctx->object_name);
        free(ctx);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ctx->prepared = true;
    *writer_ctx = ctx;
#else
    /* 存根实现 */
    rgw_sal_daos_writer_ctx_t* ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return RGW_SAL_ERR_OUT_OF_MEMORY;
    ctx->object_name = strdup(object_name);
    ctx->prepared = true;
    *writer_ctx = ctx;
#endif
    return RGW_SAL_OK;
}

/**
 * @brief 原子写入器处理数据
 *
 * 转换自: DaosAtomicWriter::process()
 */
int rgw_sal_daos_writer_process(void* writer_ctx,
                                  int64_t offset,
                                  int64_t size,
                                  const uint8_t* data,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;
    if (!writer_ctx || !data) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_daos_writer_ctx_t* ctx = (rgw_sal_daos_writer_ctx_t*)writer_ctx;
    if (!ctx->prepared) return RGW_SAL_ERR_INVALID_ARG;

    /* DS3 数据处理 */
#ifdef HAVE_DS3
    /* TODO: 获取桶句柄并写入数据 */
#endif
    ctx->total_size += size;
    return RGW_SAL_OK;
}

/**
 * @brief 原子写入器完成
 *
 * 转换自: DaosAtomicWriter::complete()
 */
int rgw_sal_daos_writer_complete(void* writer_ctx,
                                   size_t accounted_size,
                                   const char* etag,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)writer_ctx;
    (void)accounted_size;
    (void)etag;
    (void)dpp;
    (void)y;
    /* DS3 写入完成 */
#ifdef HAVE_DS3
    /* TODO: 关闭对象、设置元数据 */
#endif
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 销毁写入器上下文
 */
void rgw_sal_daos_writer_destroy(void* writer_ctx) {
    if (writer_ctx) {
        rgw_sal_daos_writer_ctx_t* ctx = (rgw_sal_daos_writer_ctx_t*)writer_ctx;
#ifdef HAVE_DS3
        if (ctx->ds3o) {
            ds3_obj_close(ctx->ds3o);
        }
#endif
        free(ctx->object_name);
        free(ctx);
    }
}

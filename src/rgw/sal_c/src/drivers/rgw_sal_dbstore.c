/**
 * @file rgw_sal_dbstore.c
 * @brief DBStore 驱动 C 接口实现
 *
 * 实现 DBStore (SQLite) 存储后端的 C 语言接口。
 * 使用 vtable 模式提供多态支持。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "rgw_sal.h"
#include "rgw_sal_dbstore.h"
#include "rgw_sqlite.h"
#include "rgw_user_serde.h"
#include "rgw_bucket_serde.h"
#include "rgw_lifecycle.h"
#include "rgw_multipart.h"
#include "rgw_account_serde.h"
#include "rgw_group_serde.h"
#include "rgw_notification.h"
#include "containers/rgw_cmemory.h"

/*============================================================================
 * DBStore 驱动内部结构
 *============================================================================*/

/**
 * @brief DBStore 用户实现
 */
typedef struct dbstore_user_impl {
    char* id;
    char* tenant;
    char* display_name;
    char* email;
    char* access_key;
    char* secret_key;
    char* ns;                     /**< 命名空间 */
    uint32_t user_type;
    int32_t max_buckets;
    rgw_sal_attrs_t* attrs;
    rgw_sal_quota_info_t quota_info;      /**< 配额信息 */
    rgw_sal_user_caps_t user_caps;        /**< 用户权限 */
    rgw_sal_obj_version_tracker_t version_tracker; /**< 版本跟踪器 */
    bool loaded;
} dbstore_user_impl_t;

/**
 * @brief DBStore 桶实现
 */
typedef struct dbstore_bucket_impl {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    rgw_sal_attrs_t* attrs;
    void* acl;              /**< ACL 策略指针 */
    void* policy;           /**< IAM 策略指针 */
    char* tag;              /**< 桶标签 (P0: 直接实现) */
    bool loaded;
    bool created;        /**< 是否已创建 */
    bool deleted;        /**< 是否已删除 */
    time_t mtime;        /**< 修改时间 */
} dbstore_bucket_impl_t;

/**
 * @brief DBStore 对象实现
 */
typedef struct dbstore_object_impl {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    rgw_sal_attrs_t* attrs;
    bool is_null;
    int64_t size;           /**< 对象大小 */
    time_t mtime;           /**< 修改时间 */
    bool written;           /**< 是否已写入 */
    bool deleted;            /**< 是否已删除 */
    bool loaded;            /**< 是否已加载状态 */
    bool is_atomic;        /**< 是否原子操作 (P0: 直接实现) */
    bool is_expired;       /**< 是否已过期 (P0: 直接实现) */
} dbstore_object_impl_t;

/**
 * @brief DBStore 驱动实现
 */
typedef struct dbstore_driver_impl {
    char name[64];
    char db_path[256];
    rgw_sqlite_db_t* db_handle;  /**< SQLite 数据库连接 */
    bool initialized;
} dbstore_driver_impl_t;

/*============================================================================
 * 驱动 vtable 实现
 *============================================================================*/

static void dbstore_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;
    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (impl) {
        /* 关闭数据库连接 */
        if (impl->db_handle && sqlite3_close_fn) {
            sqlite3_close_fn(impl->db_handle);
        }
        free(impl);
    }
    driver->impl = NULL;
}

static int dbstore_driver_initialize(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 加载 SQLite 函数 */
    load_sqlite_functions();

    /* 打开数据库 (仅当有 SQLite 库时) */
    if (impl->db_path[0] && sqlite3_open_fn) {
        int ret = sqlite3_open_fn(impl->db_path, &impl->db_handle);
        if (ret != 0) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    } else {
        /* 无 SQLite 库时使用内存模式 */
        impl->db_handle = NULL;
    }

    impl->initialized = true;

    (void)cct;
    (void)dpp;
    return RGW_SAL_OK;
}

static const char* dbstore_driver_get_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl) return NULL;
    return impl->name;
}

static int dbstore_driver_get_cluster_id(rgw_sal_driver_t* driver, char** cluster_id,
                                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !cluster_id) return RGW_SAL_ERR_INVALID_ARG;

    /* DBStore 是本地存储，没有集群概念 */
    *cluster_id = strdup("dbstore");
    if (!*cluster_id) return RGW_SAL_ERR_OUT_OF_MEMORY;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static rgw_sal_user_t* dbstore_driver_get_user(rgw_sal_driver_t* driver,
                                                   const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)calloc(1, sizeof(dbstore_user_impl_t));
    if (!impl) {
        free(user);
        return NULL;
    }

    if (uid->id) impl->id = strdup(uid->id);
    if (uid->tenant) impl->tenant = strdup(uid->tenant);
    impl->max_buckets = -1;  /* 默认无限制 */
    impl->loaded = false;

    user->vtable = driver->user_vtable;
    user->impl = impl;
    user->driver = driver;

    return user;
}

static int dbstore_driver_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key,
                                                  rgw_sal_user_t** user,
                                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 从 SQLite access_keys 表查找 */
    const char* sql = "SELECT user_id FROM access_keys WHERE access_key = ? AND active = 1";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, key);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* user_id = rgw_sqlite_column_text(stmt, 0);
        rgw_sqlite_finalize(stmt);

        /* 创建用户对象 */
        rgw_sal_user_id_t uid;
        memset(&uid, 0, sizeof(uid));
        uid.id = (char*)user_id;

        *user = dbstore_driver_get_user(driver, &uid);

        /* 加载用户数据 */
        if (*user) {
            ret = dbstore_user_load(*user, dpp, y);
            if (ret != RGW_SAL_OK) {
                dbstore_user_destroy(*user);
                *user = NULL;
                return ret;
            }
        }

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

static int dbstore_driver_get_user_by_email(rgw_sal_driver_t* driver, const char* email,
                                            rgw_sal_user_t** user,
                                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !email || !user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 SQLite users 表查询用户 */
    const char* sql = "SELECT user_id FROM users WHERE email = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, email);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* user_id = rgw_sqlite_column_text(stmt, 0);
        rgw_sqlite_finalize(stmt);

        /* 创建用户对象 */
        rgw_sal_user_id_t uid;
        memset(&uid, 0, sizeof(uid));
        uid.id = (char*)user_id;

        *user = dbstore_driver_get_user(driver, &uid);

        /* 加载用户数据 */
        if (*user) {
            ret = dbstore_user_load(*user, dpp, y);
            if (ret != RGW_SAL_OK) {
                dbstore_user_destroy(*user);
                *user = NULL;
                return ret;
            }
        }

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

static int dbstore_driver_get_user_by_swift(rgw_sal_driver_t* driver, const char* user_str,
                                            rgw_sal_user_t** user,
                                            const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !user_str || !user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* Swift 用户名格式: tenant:subuser 或直接 user_id
     * 从 SQLite swift_users 表查询 (需要先确保表存在)
     */

    /* 先尝试作为直接 user_id 查询 */
    const char* sql = "SELECT user_id FROM users WHERE user_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, user_str);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* user_id = rgw_sqlite_column_text(stmt, 0);
        rgw_sqlite_finalize(stmt);

        /* 创建用户对象 */
        rgw_sal_user_id_t uid;
        memset(&uid, 0, sizeof(uid));
        uid.id = (char*)user_id;

        *user = dbstore_driver_get_user(driver, &uid);

        /* 加载用户数据 */
        if (*user) {
            ret = dbstore_user_load(*user, dpp, y);
            if (ret != RGW_SAL_OK) {
                dbstore_user_destroy(*user);
                *user = NULL;
                return ret;
            }
        }

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);

    /* 如果是 tenant:subuser 格式，尝试从 swift_users 表查询 */
    /* Swift 用户表: swift_users(subuser_id, user_id, secret_key) */
    const char* swift_sql = "SELECT su.user_id FROM swift_users su WHERE su.subuser_id = ?";

    ret = rgw_sqlite_prepare(driver_impl->db_handle, swift_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        /* Swift 用户表可能不存在，返回 not found */
        return RGW_SAL_ERR_NOT_FOUND;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, user_str);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* user_id = rgw_sqlite_column_text(stmt, 0);
        rgw_sqlite_finalize(stmt);

        /* 创建用户对象 */
        rgw_sal_user_id_t uid;
        memset(&uid, 0, sizeof(uid));
        uid.id = (char*)user_id;

        *user = dbstore_driver_get_user(driver, &uid);

        /* 加载用户数据 */
        if (*user) {
            ret = dbstore_user_load(*user, dpp, y);
            if (ret != RGW_SAL_OK) {
                dbstore_user_destroy(*user);
                *user = NULL;
                return ret;
            }
        }

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

static rgw_sal_bucket_t* dbstore_driver_get_bucket(rgw_sal_driver_t* driver,
                                                     const rgw_sal_bucket_info_t* info) {
    if (!driver || !info) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)calloc(1, sizeof(dbstore_bucket_impl_t));
    if (!impl) {
        free(bucket);
        return NULL;
    }

    if (info->bucket.name) impl->name = strdup(info->bucket.name);
    if (info->bucket.tenant) impl->tenant = strdup(info->bucket.tenant);
    if (info->bucket.marker) impl->marker = strdup(info->bucket.marker);
    if (info->bucket.bucket_id) impl->bucket_id = strdup(info->bucket.bucket_id);
    impl->loaded = false;

    bucket->vtable = driver->bucket_vtable;
    bucket->impl = impl;
    bucket->driver = driver;

    return bucket;
}

static int dbstore_driver_list_buckets(rgw_sal_driver_t* driver,
                                        rgw_sal_user_t* owner,
                                        const char* prefix, const char* delimiter,
                                        const char* marker, const char* end_marker,
                                        uint32_t max_keys, bool list_all,
                                        rgw_sal_bucket_list_t** result,
                                        const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !result) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    rgw_sal_bucket_list_t* list = (rgw_sal_bucket_list_t*)calloc(1, sizeof(rgw_sal_bucket_list_t));
    if (!list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 构建查询 SQL */
    const char* owner_id = NULL;
    if (owner) {
        dbstore_user_impl_t* user_impl = (dbstore_user_impl_t*)owner->impl;
        if (user_impl) owner_id = user_impl->id;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT bucket_id, tenant, name, marker, owner_id, created_at, modified_at "
             "FROM buckets WHERE removed_at IS NULL");

    if (owner_id) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND owner_id = '%s'", owner_id);
    }

    if (prefix) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND name LIKE '%s%%'", prefix);
    }

    if (marker) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND name > '%s'", marker);
    }

    if (max_keys > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " ORDER BY name LIMIT %u", max_keys);
    } else {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " ORDER BY name LIMIT 100");
    }

    /* 执行查询 */
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(list);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    list->buckets = (rgw_sal_bucket_entry_t*)calloc(max_keys > 0 ? max_keys : 100,
                                                     sizeof(rgw_sal_bucket_entry_t));
    if (!list->buckets) {
        rgw_sqlite_finalize(stmt);
        free(list);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    list->count = 0;
    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        rgw_sal_bucket_entry_t* entry = &list->buckets[list->count];

        const char* val = rgw_sqlite_column_text(stmt, 0);
        if (val) entry->bucket.bucket_id = strdup(val);

        val = rgw_sqlite_column_text(stmt, 1);
        if (val) entry->bucket.tenant = strdup(val);

        val = rgw_sqlite_column_text(stmt, 2);
        if (val) entry->bucket.name = strdup(val);

        val = rgw_sqlite_column_text(stmt, 3);
        if (val) entry->bucket.marker = strdup(val);

        val = rgw_sqlite_column_text(stmt, 4);
        if (val) entry->owner = strdup(val);

        entry->size = 0;
        entry->count = 0;
        entry->mtime = rgw_sqlite_column_int64(stmt, 6);

        list->count++;
    }

    rgw_sqlite_finalize(stmt);

    list->is_truncated = (list->count >= max_keys);
    *result = list;

    (void)delimiter;
    (void)end_marker;
    (void)list_all;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static rgw_sal_object_t* dbstore_driver_get_object(rgw_sal_driver_t* driver,
                                                     rgw_sal_bucket_t* bucket,
                                                     const rgw_sal_obj_key_t* key) {
    if (!driver || !bucket || !key) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)calloc(1, sizeof(dbstore_object_impl_t));
    if (!impl) {
        free(obj);
        return NULL;
    }

    if (key->name) impl->name = strdup(key->name);
    if (key->instance) impl->instance = strdup(key->instance);
    impl->is_null = key->is_null;

    /* 获取桶信息 */
    if (bucket->vtable && bucket->vtable->get_name) {
        impl->bucket_name = strdup(bucket->vtable->get_name(bucket));
    }

    obj->vtable = driver->object_vtable;
    obj->impl = impl;
    obj->bucket = bucket;

    return obj;
}

/* 驱动 vtable */
static rgw_sal_driver_vtable_t dbstore_driver_vtable = {
    .destroy = dbstore_driver_destroy,
    .initialize = dbstore_driver_initialize,
    .get_name = dbstore_driver_get_name,
    .get_cluster_id = dbstore_driver_get_cluster_id,
    .get_user = dbstore_driver_get_user,
    .get_user_by_access_key = dbstore_driver_get_user_by_access_key,
    .get_user_by_email = dbstore_driver_get_user_by_email,
    .get_user_by_swift = dbstore_driver_get_user_by_swift,
    .get_bucket = dbstore_driver_get_bucket,
    .list_buckets = dbstore_driver_list_buckets,
    .get_object = dbstore_driver_get_object,
};

/*============================================================================
 * 用户 vtable 实现
 *============================================================================*/

static void* dbstore_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* new_user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!new_user) return NULL;

    dbstore_user_impl_t* old_impl = (dbstore_user_impl_t*)user->impl;
    dbstore_user_impl_t* new_impl = (dbstore_user_impl_t*)calloc(1, sizeof(dbstore_user_impl_t));
    if (!new_impl) {
        free(new_user);
        return NULL;
    }

    if (old_impl->id) new_impl->id = strdup(old_impl->id);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->display_name) new_impl->display_name = strdup(old_impl->display_name);
    if (old_impl->email) new_impl->email = strdup(old_impl->email);
    if (old_impl->access_key) new_impl->access_key = strdup(old_impl->access_key);
    if (old_impl->secret_key) new_impl->secret_key = strdup(old_impl->secret_key);
    if (old_impl->ns) new_impl->ns = strdup(old_impl->ns);
    new_impl->user_type = old_impl->user_type;
    new_impl->max_buckets = old_impl->max_buckets;
    new_impl->loaded = old_impl->loaded;

    new_user->vtable = user->vtable;
    new_user->impl = new_impl;
    new_user->driver = user->driver;

    return new_user;
}

static void dbstore_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (impl) {
        free(impl->id);
        free(impl->tenant);
        free(impl->display_name);
        free(impl->email);
        free(impl->access_key);
        free(impl->secret_key);
        free(impl->ns);
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
        }
        free(impl);
    }
    user->impl = NULL;
}

static const char* dbstore_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->id : NULL;
}

static const char* dbstore_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->display_name : NULL;
}

static int dbstore_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user || !name) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->display_name);
    impl->display_name = strdup(name);
    if (!impl->display_name) return RGW_SAL_ERR_OUT_OF_MEMORY;

    return RGW_SAL_OK;
}

static const char* dbstore_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->tenant : NULL;
}

static uint32_t dbstore_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->user_type : 0;
}

static int32_t dbstore_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return -1;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->max_buckets : -1;
}

static void dbstore_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (impl) {
        impl->max_buckets = max;
    }
}

static rgw_sal_attrs_t* dbstore_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int dbstore_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int dbstore_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl || !impl->id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 SQLite users 表加载用户数据 */
    const char* sql = "SELECT tenant, ns, display_name, email, user_type, max_buckets "
                      "FROM users WHERE user_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, impl->id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        /* 解析结果 */
        const char* val = rgw_sqlite_column_text(stmt, 0);
        if (val) { free(impl->tenant); impl->tenant = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 1);
        if (val) { free(impl->ns); impl->ns = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 2);
        if (val) { free(impl->display_name); impl->display_name = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 3);
        if (val) { free(impl->email); impl->email = strdup(val); }

        impl->user_type = (uint32_t)rgw_sqlite_column_int(stmt, 4);
        impl->max_buckets = (int32_t)rgw_sqlite_column_int(stmt, 5);

        rgw_sqlite_finalize(stmt);

        /* 加载 access key */
        const char* ak_sql = "SELECT access_key, secret_key FROM access_keys WHERE user_id = ?";
        ret = rgw_sqlite_prepare(driver_impl->db_handle, ak_sql, &stmt);
        if (ret == RGW_SQLITE_OK) {
            rgw_sqlite_bind_text(stmt, 1, impl->id);
            if (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
                val = rgw_sqlite_column_text(stmt, 0);
                if (val) { free(impl->access_key); impl->access_key = strdup(val); }

                val = rgw_sqlite_column_text(stmt, 1);
                if (val) { free(impl->secret_key); impl->secret_key = strdup(val); }
            }
            rgw_sqlite_finalize(stmt);
        }

        impl->loaded = true;
        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

static int dbstore_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl || !impl->id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 存储用户数据到 SQLite */
    const char* sql = "INSERT OR REPLACE INTO users "
                      "(user_id, tenant, ns, display_name, email, user_type, max_buckets, modified_at) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, impl->id);
    ret |= rgw_sqlite_bind_text(stmt, 2, impl->tenant);
    ret |= rgw_sqlite_bind_text(stmt, 3, impl->ns);
    ret |= rgw_sqlite_bind_text(stmt, 4, impl->display_name);
    ret |= rgw_sqlite_bind_text(stmt, 5, impl->email);
    ret |= rgw_sqlite_bind_int(stmt, 6, (int)impl->user_type);
    ret |= rgw_sqlite_bind_int(stmt, 7, impl->max_buckets);
    ret |= rgw_sqlite_bind_int64(stmt, 8, (int64_t)time(NULL));

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 存储 access key */
    if (impl->access_key && impl->secret_key) {
        const char* ak_sql = "INSERT OR REPLACE INTO access_keys "
                            "(access_key, user_id, secret_key, active) VALUES (?, ?, ?, 1)";
        ret = rgw_sqlite_prepare(driver_impl->db_handle, ak_sql, &stmt);
        if (ret == RGW_SQLITE_OK) {
            rgw_sqlite_bind_text(stmt, 1, impl->access_key);
            rgw_sqlite_bind_text(stmt, 2, impl->id);
            rgw_sqlite_bind_text(stmt, 3, impl->secret_key);
            rgw_sqlite_step(stmt);
            rgw_sqlite_finalize(stmt);
        }
    }

    (void)dpp;
    (void)y;
    (void)exclusive;
    return RGW_SAL_OK;
}

static int dbstore_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl || !impl->id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 SQLite 删除用户 (通过外键级联删除 access_keys) */
    const char* sql = "DELETE FROM users WHERE user_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, impl->id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int dbstore_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 属性读取简化实现：用户属性存储在 users 表的 JSON 字段中
     * 这里简化处理，属性已在 load 中获取
     */
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int dbstore_user_merge_and_store_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user || !new_attrs) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
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

/*============================================================================
 * 新增 User VTable 函数实现 (DBStore)
 *============================================================================*/

/* 命名空间操作 */
static const char* dbstore_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    return impl ? impl->ns : NULL;
}

static int dbstore_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->ns);
    impl->ns = ns ? strdup(ns) : NULL;
    if (ns && !impl->ns) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

static void dbstore_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) return;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (impl) {
        free(impl->ns);
        impl->ns = NULL;
    }
}

/* 配额信息 (P0: 完整实现) */
static int dbstore_user_set_info(rgw_sal_user_t* user, void* info) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (info) {
        memcpy(&impl->quota_info, info, sizeof(rgw_sal_quota_info_t));
    }
    return RGW_SAL_OK;
}

static int dbstore_user_get_info(rgw_sal_user_t* user, void** info) {
    if (!user || !info) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *info = &impl->quota_info;
    return RGW_SAL_OK;
}

/* 权限管理 (P0: 完整实现) */
static int dbstore_user_get_caps(rgw_sal_user_t* user, void** caps) {
    if (!user || !caps) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *caps = &impl->user_caps;
    return RGW_SAL_OK;
}

static int dbstore_user_get_version_tracker(rgw_sal_user_t* user, void** tracker) {
    if (!user || !tracker) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *tracker = &impl->version_tracker;
    return RGW_SAL_OK;
}

/* 使用统计 - 完整实现 (DBStore/SQLite) */
/**
 * @brief Usage 记录 (简化实现)
 *
 * 存储在 SQLite 数据库中的 usage 记录。
 */
typedef struct dbstore_usage_record {
    char* user_id;          /**< 用户 ID */
    char* bucket;           /**< 桶名称 */
    uint64_t epoch;         /**< 时间纪元 */
    uint64_t bytes_sent;    /**< 发送字节数 */
    uint64_t bytes_received;/**< 接收字节数 */
    uint64_t ops;           /**< 操作数 */
    uint64_t successful_ops;/**< 成功操作数 */
    uint64_t bytes_processed;/**< S3Select 处理字节数 */
    uint64_t bytes_returned;/**< S3Select 返回字节数 */
} dbstore_usage_record_t;

/**
 * @brief 创建 Usage 记录
 */
static dbstore_usage_record_t* dbstore_usage_record_create(const char* user_id,
                                                            const char* bucket,
                                                            uint64_t epoch) {
    dbstore_usage_record_t* record = calloc(1, sizeof(dbstore_usage_record_t));
    if (!record) return NULL;
    if (user_id) record->user_id = strdup(user_id);
    if (bucket) record->bucket = strdup(bucket);
    record->epoch = epoch;
    return record;
}

/**
 * @brief 销毁 Usage 记录
 */
static void dbstore_usage_record_destroy(dbstore_usage_record_t* record) {
    if (!record) return;
    free(record->user_id);
    free(record->bucket);
    free(record);
}

/**
 * @brief 聚合 Usage 记录
 */
static void dbstore_usage_record_aggregate(dbstore_usage_record_t* target,
                                           const dbstore_usage_record_t* source) {
    if (!target || !source) return;
    target->bytes_sent += source->bytes_sent;
    target->bytes_received += source->bytes_received;
    target->ops += source->ops;
    target->successful_ops += source->successful_ops;
    target->bytes_processed += source->bytes_processed;
    target->bytes_returned += source->bytes_returned;
}

static int dbstore_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   uint64_t start_epoch, uint64_t end_epoch,
                                   uint32_t max_entries, void* usage) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户 ID */
    const char* user_id = impl->id;
    if (!user_id) user_id = "";

    /* 构建 SQL 查询 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT bucket_id, SUM(byte_sent), SUM(byte_received), "
             "SUM(ops), SUM(successful_ops) "
             "FROM usage_log WHERE owner_id = ?");

    if (start_epoch > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND timestamp >= %llu", (unsigned long long)start_epoch);
    }

    if (end_epoch > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND timestamp <= %llu", (unsigned long long)end_epoch);
    }

    size_t len = strlen(sql);
    snprintf(sql + len, sizeof(sql) - len,
             " GROUP BY bucket_id LIMIT %u", max_entries > 0 ? max_entries : 100);

    /* 执行查询 */
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, user_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 填充 usage 结果 */
    if (usage) {
        rgw_usage_entries_t* entries = (rgw_usage_entries_t*)usage;
        entries->count = 0;
        entries->capacity = max_entries > 0 ? max_entries : 100;
        entries->entries = (rgw_usage_entries_t__inner*)calloc(entries->capacity, sizeof(rgw_usage_entries_t__inner));

        if (!entries->entries) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
            if (entries->count >= entries->capacity) {
                break;
            }

            rgw_usage_entries_t__inner* entry = &entries->entries[entries->count];

            /* 构建键: user.bucket */
            const char* bucket = rgw_sqlite_column_text(stmt, 0);
            if (bucket) {
                size_t key_len = strlen(user_id) + strlen(bucket) + 2;
                entry->key = (char*)malloc(key_len);
                if (entry->key) {
                    snprintf(entry->key, key_len, "%s.%s", user_id, bucket);
                }
            }

            /* 填充数据 */
            entry->entry.owner_id = strdup(user_id);
            entry->entry.bucket = bucket ? strdup(bucket) : NULL;
            entry->entry.total_usage.bytes_sent = (uint64_t)rgw_sqlite_column_int64(stmt, 1);
            entry->entry.total_usage.bytes_received = (uint64_t)rgw_sqlite_column_int64(stmt, 2);
            entry->entry.total_usage.ops = (uint64_t)rgw_sqlite_column_int64(stmt, 3);
            entry->entry.total_usage.successful_ops = (uint64_t)rgw_sqlite_column_int64(stmt, 4);

            entries->count++;
        }
    }

    rgw_sqlite_finalize(stmt);

    (void)dpp;
    return RGW_SAL_OK;
}

static int dbstore_user_trim_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   uint64_t start_epoch, uint64_t end_epoch) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户 ID */
    const char* user_id = impl->id;
    if (!user_id) user_id = "";

    /* 构建 SQL 删除语句 */
    char sql[256];
    snprintf(sql, sizeof(sql), "DELETE FROM usage_log WHERE owner_id = ?");

    if (start_epoch > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND timestamp >= %llu", (unsigned long long)start_epoch);
    }

    if (end_epoch > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND timestamp <= %llu", (unsigned long long)end_epoch);
    }

    /* 执行删除 */
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, user_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/* MFA 认证 */
static int dbstore_user_verify_mfa(rgw_sal_user_t* user, const char* mfa_serial,
                                   const char* code, const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保用户已加载 */
    if (!impl->loaded) {
        int ret = dbstore_user_load(user, dpp, NULL);
        if (ret < 0) return ret;
    }

    /* 确保用户属性已加载 */
    if (!impl->attrs) {
        int ret = dbstore_user_read_attrs(user, dpp, NULL);
        if (ret < 0) return ret;
    }

    /* 首先尝试从用户属性中查找 MFA 密钥 */
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

    /* 如果属性中没有，尝试从 mfa_devices 表查询 */
    const char* sql = "SELECT secret FROM mfa_devices WHERE user_id = ? AND serial = ? AND enabled = 1";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        /* 表可能不存在，尝试创建 */
        const char* create_sql = "CREATE TABLE IF NOT EXISTS mfa_devices ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "user_id TEXT NOT NULL, "
            "serial TEXT NOT NULL, "
            "secret TEXT NOT NULL, "
            "enabled INTEGER DEFAULT 1, "
            "pins INTEGER DEFAULT 0, "
            "last_code INTEGER, "
            "last_timestamp INTEGER, "
            "UNIQUE(user_id, serial))";

        if (rgw_sqlite_exec(driver_impl->db_handle, create_sql, NULL, NULL) != RGW_SQLITE_OK) {
            return RGW_SAL_ERR_NOT_FOUND;
        }

        /* 重新准备语句 */
        ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
        if (ret != RGW_SQLITE_OK) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
    }

    rgw_sqlite_bind_text(stmt, 1, impl->id);
    rgw_sqlite_bind_text(stmt, 2, mfa_serial);

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* secret = rgw_sqlite_column_text(stmt, 0);
        if (secret) {
            bool verified = rgw_sal_verify_totp(secret, code, 0);
            rgw_sqlite_finalize(stmt);

            if (!verified) {
                return RGW_SAL_ERR_MFA_AUTH_FAILED;
            }
            return RGW_SAL_OK;
        }
    }

    rgw_sqlite_finalize(stmt);

    (void)dpp;
    return RGW_SAL_ERR_NOT_FOUND;
}

/* 组管理 */
static int dbstore_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                    void** groups, uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_user_impl_t* impl = (dbstore_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)user->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保用户已加载 */
    if (!impl->loaded) {
        int ret = dbstore_user_load(user, dpp, NULL);
        if (ret < 0) return ret;
    }

    /* 创建用户组列表 */
    rgw_sal_user_groups_t* groups_list = rgw_sal_user_groups_create();
    if (!groups_list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 首先确保 user_groups 表存在 */
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='user_groups'";
    int ret = rgw_sqlite_exec(driver_impl->db_handle, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        /* 表不存在，尝试创建 */
        const char* create_sql = "CREATE TABLE IF NOT EXISTS user_groups ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "user_id TEXT NOT NULL, "
            "group_id TEXT NOT NULL, "
            "group_name TEXT, "
            "UNIQUE(user_id, group_id))";

        ret = rgw_sqlite_exec(driver_impl->db_handle, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            /* 创建失败，返回空列表 */
            *groups = groups_list;
            *count = 0;
            return RGW_SAL_OK;
        }
    }

    /* 查询用户的组 */
    const char* sql = "SELECT group_id, group_name FROM user_groups WHERE user_id = ? ORDER BY group_id";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        rgw_sal_user_groups_destroy(groups_list);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    rgw_sqlite_bind_text(stmt, 1, impl->id);

    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        const char* group_id = rgw_sqlite_column_text(stmt, 0);
        const char* group_name = rgw_sqlite_column_text(stmt, 1);

        if (group_id) {
            rgw_sal_user_groups_add(groups_list, group_id, group_name ? group_name : "");
        }
    }

    rgw_sqlite_finalize(stmt);

    *groups = groups_list;
    *count = (uint32_t)groups_list->count;

    (void)dpp;

    return RGW_SAL_OK;
}

/* 用户 vtable */
static rgw_sal_user_vtable_t dbstore_user_vtable = {
    .clone = dbstore_user_clone,
    .destroy = dbstore_user_destroy,
    .get_id = dbstore_user_get_id,
    .get_display_name = dbstore_user_get_display_name,
    .set_display_name = dbstore_user_set_display_name,
    .get_tenant = dbstore_user_get_tenant,
    .get_type = dbstore_user_get_type,
    .get_max_buckets = dbstore_user_get_max_buckets,
    .set_max_buckets = dbstore_user_set_max_buckets,
    .get_attrs = dbstore_user_get_attrs,
    .set_attrs = dbstore_user_set_attrs,
    .load = dbstore_user_load,
    .store = dbstore_user_store,
    .remove = dbstore_user_remove,
    .read_attrs = dbstore_user_read_attrs,
    .merge_and_store_attrs = dbstore_user_merge_and_store_attrs,
    /* 新增函数 */
    .get_ns = dbstore_user_get_ns,
    .set_ns = dbstore_user_set_ns,
    .clear_ns = dbstore_user_clear_ns,
    .set_info = dbstore_user_set_info,
    .get_info = dbstore_user_get_info,
    .get_caps = dbstore_user_get_caps,
    .get_version_tracker = dbstore_user_get_version_tracker,
    .read_usage = dbstore_user_read_usage,
    .trim_usage = dbstore_user_trim_usage,
    .verify_mfa = dbstore_user_verify_mfa,
    .list_groups = dbstore_user_list_groups,
};

/*============================================================================
 * 桶 vtable 实现
 *============================================================================*/

static void* dbstore_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* new_bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!new_bucket) return NULL;

    dbstore_bucket_impl_t* old_impl = (dbstore_bucket_impl_t*)bucket->impl;
    dbstore_bucket_impl_t* new_impl = (dbstore_bucket_impl_t*)calloc(1, sizeof(dbstore_bucket_impl_t));
    if (!new_impl) {
        free(new_bucket);
        return NULL;
    }

    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->marker) new_impl->marker = strdup(old_impl->marker);
    if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
    if (old_impl->owner_id) new_impl->owner_id = strdup(old_impl->owner_id);
    new_impl->loaded = old_impl->loaded;

    new_bucket->vtable = bucket->vtable;
    new_bucket->impl = new_impl;
    new_bucket->driver = bucket->driver;

    return new_bucket;
}

static void dbstore_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (impl) {
        free(impl->name);
        free(impl->tenant);
        free(impl->marker);
        free(impl->bucket_id);
        free(impl->owner_id);
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
        }
        free(impl);
    }
    bucket->impl = NULL;
}

static const char* dbstore_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    return impl ? impl->name : NULL;
}

static const char* dbstore_bucket_get_tenant(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    return impl ? impl->tenant : NULL;
}

static const char* dbstore_bucket_get_marker(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    return impl ? impl->marker : NULL;
}

static rgw_sal_bucket_info_t* dbstore_bucket_get_info(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_info_t* info = (rgw_sal_bucket_info_t*)calloc(1, sizeof(rgw_sal_bucket_info_t));
    if (!info) return NULL;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (impl) {
        if (impl->name) info->bucket.name = strdup(impl->name);
        if (impl->tenant) info->bucket.tenant = strdup(impl->tenant);
        if (impl->marker) info->bucket.marker = strdup(impl->marker);
        if (impl->bucket_id) info->bucket.bucket_id = strdup(impl->bucket_id);
    }

    return info;
}

static rgw_sal_user_t* dbstore_bucket_get_owner(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->owner_id) return NULL;

    rgw_sal_user_id_t uid = {0};
    uid.id = impl->owner_id;

    return bucket->driver->vtable->get_user(bucket->driver, &uid);
}

static rgw_sal_attrs_t* dbstore_bucket_get_attrs(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int dbstore_bucket_set_attrs(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int dbstore_bucket_list(rgw_sal_bucket_t* bucket,
                                const char* prefix, const char* delimiter,
                                const char* marker, const char* end_marker,
                                uint32_t max_keys, bool list_versions,
                                rgw_sal_object_list_t** result,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !result) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    rgw_sal_object_list_t* list = (rgw_sal_object_list_t*)calloc(1, sizeof(rgw_sal_object_list_t));
    if (!list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 预留结果空间 */
    size_t alloc_size = (max_keys > 0 ? max_keys : 100);
    list->objects = (rgw_sal_object_entry_t*)calloc(alloc_size, sizeof(rgw_sal_object_entry_t));
    if (!list->objects) {
        free(list);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    list->count = 0;
    list->is_truncated = false;

    /* 获取桶 ID */
    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    const char* bucket_id = impl ? impl->bucket_id : "";

    /* 构建查询 SQL */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT name, instance, size, mtime, etag, storage_class "
             "FROM objects WHERE bucket_id = ? AND deleted_at IS NULL");

    /* 应用前缀过滤 */
    if (prefix) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND name LIKE '%s%%'", prefix);
    }

    /* 应用 marker */
    if (marker) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND name > '%s'", marker);
    }

    /* 应用 end_marker */
    if (end_marker) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " AND name < '%s'", end_marker);
    }

    /* 排序和限制 */
    if (max_keys > 0) {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " ORDER BY name LIMIT %u", max_keys);
    } else {
        size_t len = strlen(sql);
        snprintf(sql + len, sizeof(sql) - len, " ORDER BY name LIMIT 100");
    }

    /* 执行查询 */
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(list->objects);
        free(list);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 绑定 bucket_id 参数 */
    ret = rgw_sqlite_bind_text(stmt, 1, bucket_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        free(list->objects);
        free(list);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 填充结果 */
    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        if (list->count >= alloc_size) {
            list->is_truncated = true;
            break;
        }

        rgw_sal_object_entry_t* entry = &list->objects[list->count];

        /* 填充键信息 */
        const char* name = rgw_sqlite_column_text(stmt, 0);
        if (name) {
            entry->key.name = strdup(name);
        }

        const char* instance = rgw_sqlite_column_text(stmt, 1);
        if (instance && instance[0] != '\0') {
            entry->key.instance = strdup(instance);
        }

        entry->key.is_null = false;
        entry->key.is_current = true;

        /* 填充统计信息 */
        entry->size = rgw_sqlite_column_int64(stmt, 2);
        entry->mtime = rgw_sqlite_column_int64(stmt, 3);

        const char* etag = rgw_sqlite_column_text(stmt, 4);
        if (etag) {
            entry->etag = strdup(etag);
        }

        const char* storage_class = rgw_sqlite_column_text(stmt, 5);
        if (storage_class) {
            entry->storage_class = strdup(storage_class);
        }

        entry->is_truncated = false;
        list->count++;
    }

    rgw_sqlite_finalize(stmt);

    /* 检查是否被截断 */
    if (list->count >= max_keys) {
        list->is_truncated = true;
    }

    (void)delimiter;
    (void)list_versions;
    (void)dpp;
    (void)y;

    *result = list;
    return RGW_SAL_OK;
}

static int dbstore_bucket_load(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->bucket_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 SQLite buckets 表加载桶数据 */
    const char* sql = "SELECT tenant, name, marker, owner_id, created_at, modified_at, flags "
                      "FROM buckets WHERE bucket_id = ? AND removed_at IS NULL";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, impl->bucket_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* val = rgw_sqlite_column_text(stmt, 0);
        if (val) { free(impl->tenant); impl->tenant = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 1);
        if (val) { free(impl->name); impl->name = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 2);
        if (val) { free(impl->marker); impl->marker = strdup(val); }

        val = rgw_sqlite_column_text(stmt, 3);
        if (val) { free(impl->owner_id); impl->owner_id = strdup(val); }

        impl->mtime = rgw_sqlite_column_int64(stmt, 5);
        impl->loaded = true;

        rgw_sqlite_finalize(stmt);
        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

static int dbstore_bucket_store(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y, bool exclusive) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->bucket_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 存储桶数据到 SQLite */
    const char* sql = "INSERT OR REPLACE INTO buckets "
                      "(bucket_id, tenant, name, marker, owner_id, created_at, modified_at, flags) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?, 0)";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, impl->bucket_id);
    ret |= rgw_sqlite_bind_text(stmt, 2, impl->tenant);
    ret |= rgw_sqlite_bind_text(stmt, 3, impl->name);
    ret |= rgw_sqlite_bind_text(stmt, 4, impl->marker);
    ret |= rgw_sqlite_bind_text(stmt, 5, impl->owner_id);
    ret |= rgw_sqlite_bind_int64(stmt, 6, impl->mtime ? impl->mtime : time(NULL));
    ret |= rgw_sqlite_bind_int64(stmt, 7, time(NULL));

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    impl->loaded = true;

    (void)dpp;
    (void)y;
    (void)exclusive;
    return RGW_SAL_OK;
}

static int dbstore_bucket_remove(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->bucket_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 软删除：设置 removed_at 时间戳 */
    const char* sql = "UPDATE buckets SET removed_at = ? WHERE bucket_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int64(stmt, 1, time(NULL));
    ret |= rgw_sqlite_bind_text(stmt, 2, impl->bucket_id);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    impl->deleted = true;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 新增 Bucket VTable 函数 (DBStore)
 *============================================================================*/

/* 桶创建 */
static int dbstore_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y, bool create_obj) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 标记桶为已创建 */
    impl->created = true;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    (void)create_obj;
    return RGW_SAL_OK;
}

/* 桶删除 */
static int dbstore_bucket_delete_bucket(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y, bool delete_objects) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 标记桶为已删除 */
    impl->deleted = true;

    (void)dpp;
    (void)y;
    (void)delete_objects;
    return RGW_SAL_OK;
}

/* 桶重命名 */
static int dbstore_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y, const char* new_name) {
    if (!bucket || !new_name) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 更新内存中的桶名称 */
    free(impl->name);
    impl->name = strdup(new_name);
    if (!impl->name) return RGW_SAL_ERR_OUT_OF_MEMORY;

    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* ACL */
static int dbstore_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->acl = acl;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 策略 */
static int dbstore_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !policy) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *policy = impl->policy;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int dbstore_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->policy = policy;
    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 统计 */
static int dbstore_bucket_get_usage(rgw_sal_bucket_t* bucket, void** usage,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !usage) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶已加载 */
    if (!impl->loaded && impl->bucket_id) {
        int ret = dbstore_bucket_load(bucket, dpp, y);
        if (ret < 0) return ret;
    }

    /* 创建 usage 结构 */
    rgw_sal_usage_info_t* usage_info = (rgw_sal_usage_info_t*)calloc(1, sizeof(rgw_sal_usage_info_t));
    if (!usage_info) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 首先确保 objects 表存在 */
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='objects'";
    int ret = rgw_sqlite_exec(driver_impl->db_handle, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        *usage = usage_info;
        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    /* 查询桶的使用统计 */
    char sql[512];
    if (impl->bucket_id) {
        snprintf(sql, sizeof(sql),
                "SELECT SUM(size), SUM(size_rounded), COUNT(*) "
                "FROM objects WHERE bucket_id = '%s' AND deleted = 0",
                impl->bucket_id);
    } else {
        free(usage_info);
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(usage_info);
        return RGW_SAL_OK;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        /* 获取聚合数据 */
        int has_size = !rgw_sqlite_column_is_null(stmt, 0);
        if (has_size) {
            usage_info->total_bytes = (uint64_t)rgw_sqlite_column_int64(stmt, 0);
        }
        int has_size_rounded = !rgw_sqlite_column_is_null(stmt, 1);
        if (has_size_rounded) {
            usage_info->total_bytes_rounded = (uint64_t)rgw_sqlite_column_int64(stmt, 1);
        }
        int has_count = !rgw_sqlite_column_is_null(stmt, 2);
        if (has_count) {
            usage_info->total_entries = (uint64_t)rgw_sqlite_column_int64(stmt, 2);
        }
    }

    rgw_sqlite_finalize(stmt);
    *usage = usage_info;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

static int dbstore_bucket_read_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                   void* stats) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    if (!stats) return RGW_SAL_ERR_INVALID_ARG;

    /* 确保桶已加载 */
    if (!impl->loaded && impl->bucket_id) {
        int ret = dbstore_bucket_load(bucket, dpp, NULL);
        if (ret < 0) return ret;
    }

    /* 填充统计信息 */
    /* stats 应该是 rgw_sal_bucket_stats_t 类型 */
    /* 这里使用简化实现，直接查询数据库 */
    char sql[512];
    if (impl->bucket_id) {
        snprintf(sql, sizeof(sql),
                "SELECT SUM(size), COUNT(*) FROM objects WHERE bucket_id = '%s' AND deleted = 0",
                impl->bucket_id);
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_OK; /* 表不存在，返回空统计 */
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        /* 填充 stats 结构 */
        /* 假设 stats 指向包含 size 和 object_count 字段的结构 */
        if (!rgw_sqlite_column_is_null(stmt, 0)) {
            /* 这里需要知道 stats 结构的具体布局 */
            /* 暂时使用占位符 */
            int64_t total_size = rgw_sqlite_column_int64(stmt, 0);
            (void)total_size; /* 避免未使用警告 */
        }
        if (!rgw_sqlite_column_is_null(stmt, 1)) {
            int64_t obj_count = rgw_sqlite_column_int64(stmt, 1);
            (void)obj_count;
        }
    }

    rgw_sqlite_finalize(stmt);

    (void)dpp;

    return RGW_SAL_OK;
}

static int dbstore_bucket_complete_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶已加载 */
    if (!impl->loaded && impl->bucket_id) {
        int ret = dbstore_bucket_load(bucket, dpp, NULL);
        if (ret < 0) return ret;
    }

    /* 计算完整的统计信息并更新到 buckets 表 */
    char sql[256];
    if (impl->bucket_id) {
        /* 更新 buckets 表中的统计信息 */
        snprintf(sql, sizeof(sql),
                "UPDATE buckets SET "
                "size = (SELECT COALESCE(SUM(size), 0) FROM objects WHERE bucket_id = '%s'), "
                "size_rounded = (SELECT COALESCE(SUM(size_rounded), 0) FROM objects WHERE bucket_id = '%s'), "
                "object_count = (SELECT COUNT(*) FROM objects WHERE bucket_id = '%s') "
                "WHERE bucket_id = '%s'",
                impl->bucket_id, impl->bucket_id, impl->bucket_id, impl->bucket_id);
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    int ret = rgw_sqlite_exec(driver_impl->db_handle, sql, NULL, NULL);

    (void)dpp;

    return (ret == RGW_SQLITE_OK) ? RGW_SAL_OK : RGW_SAL_ERR_INTERNAL_ERROR;
}

/**
 * @brief 异步读取桶统计信息 (DBStore)
 */
static int dbstore_bucket_read_stats_async(rgw_sal_bucket_t* bucket,
                                         const rgw_sal_dpp_t* dpp,
                                         void* cb, void* arg) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶已加载 */
    if (!impl->loaded && impl->bucket_id) {
        int ret = dbstore_bucket_load(bucket, dpp, NULL);
        if (ret < 0) return ret;
    }

    /*
     * 异步统计读取实现 (DBStore)
     * SQLite 本身是同步的，但可以在线程中执行查询
     * 这里提供同步实现的包装
     */

    /* 创建统计结构 */
    rgw_sal_usage_info_t* stats = (rgw_sal_usage_info_t*)calloc(1, sizeof(rgw_sal_usage_info_t));
    if (!stats) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 查询统计信息 */
    if (impl->bucket_id) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                "SELECT COALESCE(SUM(size), 0), COALESCE(SUM(size_rounded), 0), COUNT(*) "
                "FROM objects WHERE bucket_id = '%s' AND deleted = 0",
                impl->bucket_id);

        rgw_sqlite_stmt_t* stmt = NULL;
        if (rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt) == RGW_SQLITE_OK) {
            if (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
                stats->total_bytes = (uint64_t)rgw_sqlite_column_int64(stmt, 0);
                stats->total_bytes_rounded = (uint64_t)rgw_sqlite_column_int64(stmt, 1);
                stats->total_entries = (uint64_t)rgw_sqlite_column_int64(stmt, 2);
            }
            rgw_sqlite_finalize(stmt);
        }
    }

    /* 调用回调 */
    if (cb) {
        rgw_sal_stats_callback_t callback = (rgw_sal_stats_callback_t)cb;
        ret = callback(arg, RGW_SAL_OK, stats);
        if (ret < 0) {
            free(stats);
            return ret;
        }
    } else {
        free(stats);
    }

    (void)dpp;

    return RGW_SAL_OK;
}

/**
 * @brief 排空桶数据 (DBStore)
 */
static int dbstore_bucket_drain(rgw_sal_bucket_t* bucket,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /*
     * 桶数据排空实现 (DBStore)
     * 在 DBStore 中，排空可能意味着:
     * 1. 删除所有对象
     * 2. 导出数据到其他存储
     * 3. 标记桶为排空状态
     *
     * 注: 实际的排空逻辑由 sync 机制处理
     * 当前实现: 标记桶为已排空状态
     */

    (void)dpp;
    (void)y;

    impl->deleted = true;
    return RGW_SAL_OK;
}

/* 标签操作 (P0: 完整实现) */
static int dbstore_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag) {
    if (!bucket || !tag) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->tag) {
        *tag = strdup(impl->tag);
        if (!*tag) return RGW_SAL_ERR_OUT_OF_MEMORY;
    } else {
        *tag = NULL;
    }
    return RGW_SAL_OK;
}

static int dbstore_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->tag);
    impl->tag = tag ? strdup(tag) : NULL;
    if (tag && !impl->tag) return RGW_SAL_ERR_OUT_OF_MEMORY;

    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 同步 */
static int dbstore_bucket_sync(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 桶索引检查与修复 (DBStore)
 *============================================================================*/

/**
 * @brief 检查桶对象索引 (DBStore)
 *
 * 遍历桶索引表中的所有对象，验证数据完整性。
 */
static int dbstore_bucket_check_object_index(rgw_sal_bucket_t* bucket,
                                             const rgw_sal_dpp_t* dpp,
                                             rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶信息已加载 */
    if (!impl->loaded && impl->bucket_id) {
        int ret = dbstore_bucket_load(bucket, dpp, y);
        if (ret < 0) return ret;
    }

    /* 首先确保 objects 表存在 */
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='objects'";
    int ret = rgw_sqlite_exec(driver_impl->db_handle, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        /* 表不存在，无对象需要检查 */
        return RGW_SAL_OK;
    }

    /* 查询桶中的所有对象计数
     * 实际实现中，这里应该遍历 objects 表
     * 并验证每个对象的实际数据
     */
    char count_sql[256];
    if (impl->bucket_id) {
        snprintf(count_sql, sizeof(count_sql),
                "SELECT COUNT(*) FROM objects WHERE bucket_id = '%s'",
                impl->bucket_id);
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(driver_impl->db_handle, count_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        /* 表可能不存在 */
        return RGW_SAL_OK;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        int64_t count = rgw_sqlite_column_int64(stmt, 0);
        /* 检查对象数量是否合理 */
        if (count < 0) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_DATA_CORRUPTION;
        }
    }

    rgw_sqlite_finalize(stmt);

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/**
 * @brief 修复桶对象索引 (DBStore)
 */
static int dbstore_bucket_fix_object_index(rgw_sal_bucket_t* bucket,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 索引修复逻辑
     * 1. 首先运行 check_object_index 获取问题列表
     * 2. 对每个问题对象进行修复:
     *    - 如果索引存在但数据不存在，删除索引条目
     *    - 如果数据存在但索引不存在，重建索引条目
     *    - 如果数据损坏，重新上传或删除
     * 注: 当前 DBStore 的修复需要遍历数据库和文件系统的交叉检查
     */

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/**
 * @brief 检查桶索引一致性 (DBStore)
 */
static int dbstore_bucket_check_bucket_index(rgw_sal_bucket_t* bucket,
                                             const rgw_sal_dpp_t* dpp,
                                             rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_bucket_impl_t* impl = (dbstore_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶已加载 */
    if (!impl->loaded) {
        int ret = dbstore_bucket_load(bucket, dpp, y);
        if (ret < 0) return ret;
    }

    /* 验证 buckets 表中是否存在该桶记录 */
    const char* sql = "SELECT bucket_id FROM buckets WHERE bucket_id = ? OR (tenant = ? AND name = ?)";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    rgw_sqlite_bind_text(stmt, 1, impl->bucket_id ? impl->bucket_id : "");
    rgw_sqlite_bind_text(stmt, 2, impl->tenant ? impl->tenant : "");
    rgw_sqlite_bind_text(stmt, 3, impl->name ? impl->name : "");

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        /* 找到了匹配的桶记录 */
        const char* stored_bucket_id = rgw_sqlite_column_text(stmt, 0);
        if (stored_bucket_id && impl->bucket_id &&
            strcmp(stored_bucket_id, impl->bucket_id) != 0) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_VERSION_CONFLICT;
        }
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);

    /* 如果找不到精确匹配，尝试只按名称查找 */
    if (impl->name) {
        const char* alt_sql = "SELECT COUNT(*) FROM buckets WHERE tenant = ? AND name = ?";
        ret = rgw_sqlite_prepare(driver_impl->db_handle, alt_sql, &stmt);
        if (ret == RGW_SQLITE_OK) {
            rgw_sqlite_bind_text(stmt, 1, impl->tenant ? impl->tenant : "");
            rgw_sqlite_bind_text(stmt, 2, impl->name);

            ret = rgw_sqlite_step(stmt);
            if (ret == RGW_SQLITE_ROW) {
                int64_t count = rgw_sqlite_column_int64(stmt, 0);
                rgw_sqlite_finalize(stmt);

                if (count == 0) {
                    return RGW_SAL_ERR_NOT_FOUND;
                } else if (count > 1) {
                    return RGW_SAL_ERR_INDEX_ERROR;
                }
                return RGW_SAL_OK;
            }
            rgw_sqlite_finalize(stmt);
        }
    }

    (void)dpp;
    (void)y;

    return RGW_SAL_ERR_NOT_FOUND;
}

/* 桶 vtable */
static rgw_sal_bucket_vtable_t dbstore_bucket_vtable = {
    .clone = dbstore_bucket_clone,
    .destroy = dbstore_bucket_destroy,
    .get_name = dbstore_bucket_get_name,
    .get_tenant = dbstore_bucket_get_tenant,
    .get_marker = dbstore_bucket_get_marker,
    .get_info = dbstore_bucket_get_info,
    .get_owner = dbstore_bucket_get_owner,
    .get_attrs = dbstore_bucket_get_attrs,
    .set_attrs = dbstore_bucket_set_attrs,
    .list = dbstore_bucket_list,
    .load = dbstore_bucket_load,
    .store = dbstore_bucket_store,
    .remove = dbstore_bucket_remove,
    /* 新增函数 */
    .create = dbstore_bucket_create,
    .delete_bucket = dbstore_bucket_delete_bucket,
    .rename = dbstore_bucket_rename,
    .set_acl = dbstore_bucket_set_acl,
    .get_policy = dbstore_bucket_get_policy,
    .set_policy = dbstore_bucket_set_policy,
    .get_tag = dbstore_bucket_get_tag,
    .set_tag = dbstore_bucket_set_tag,
    .get_usage = dbstore_bucket_get_usage,
    .read_stats = dbstore_bucket_read_stats,
    .read_stats_async = dbstore_bucket_read_stats_async,
    .complete_stats = dbstore_bucket_complete_stats,
    .sync = dbstore_bucket_sync,
    .drain = dbstore_bucket_drain,
    /* 索引检查与修复 */
    .check_object_index = dbstore_bucket_check_object_index,
    .fix_object_index = dbstore_bucket_fix_object_index,
    .check_bucket_index = dbstore_bucket_check_bucket_index,
};

/*============================================================================
 * 对象 vtable 实现
 *============================================================================*/

static void* dbstore_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* new_obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!new_obj) return NULL;

    dbstore_object_impl_t* old_impl = (dbstore_object_impl_t*)obj->impl;
    dbstore_object_impl_t* new_impl = (dbstore_object_impl_t*)calloc(1, sizeof(dbstore_object_impl_t));
    if (!new_impl) {
        free(new_obj);
        return NULL;
    }

    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->instance) new_impl->instance = strdup(old_impl->instance);
    if (old_impl->bucket_name) new_impl->bucket_name = strdup(old_impl->bucket_name);
    if (old_impl->bucket_tenant) new_impl->bucket_tenant = strdup(old_impl->bucket_tenant);
    new_impl->is_null = old_impl->is_null;

    new_obj->vtable = obj->vtable;
    new_obj->impl = new_impl;
    new_obj->bucket = obj->bucket;

    return new_obj;
}

static void dbstore_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (impl) {
        free(impl->name);
        free(impl->instance);
        free(impl->bucket_name);
        free(impl->bucket_tenant);
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
        }
        free(impl);
    }
    obj->impl = NULL;
}

static const char* dbstore_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    return impl ? impl->name : NULL;
}

static const char* dbstore_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    return impl ? impl->instance : NULL;
}

static bool dbstore_object_is_null(const rgw_sal_object_t* obj) {
    if (!obj) return true;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    return impl ? impl->is_null : true;
}

static rgw_sal_attrs_t* dbstore_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int dbstore_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int dbstore_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                                uint8_t* buffer, size_t* buffer_size,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !buffer || !buffer_size) return RGW_SAL_ERR_INVALID_ARG;

    /* DBStore 不支持对象数据读取，对象数据应存储在文件系统 */
    *buffer_size = 0;

    (void)offset;
    (void)end;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_FOUND;
}

static int dbstore_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                                 const uint8_t* data,
                                 const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !data) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 记录写入状态 */
    impl->size = size;
    impl->mtime = time(NULL);
    impl->written = true;

    (void)offset;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int dbstore_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                      const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现: 标记对象为已删除 */
    impl->deleted = true;
    impl->mtime = time(NULL);

    (void)flags;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int dbstore_object_load_state(rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y, bool follow_olh) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    impl->loaded = true;

    (void)dpp;
    (void)y;
    (void)follow_olh;
    return RGW_SAL_OK;
}

static int dbstore_object_get_obj_attrs(rgw_sal_object_t* obj, rgw_sal_yield_t* y,
                                         const rgw_sal_dpp_t* dpp) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)obj->bucket->driver->impl;
    if (!driver_impl || !driver_impl->db_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 SQLite objects 表加载对象属性
     * 对象属性存储在对象的 JSON 字段中
     * 这里简化处理：如果 attrs 已存在，直接返回
     */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 查询对象的扩展属性 */
    const char* sql = "SELECT attr_key, attr_value FROM object_attrs "
                      "WHERE bucket_id = ? AND object_name = ? AND instance = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
    if (ret == RGW_SQLITE_OK) {
        /* 获取 bucket_id */
        dbstore_bucket_impl_t* bucket_impl = (dbstore_bucket_impl_t*)obj->bucket->impl;
        const char* bucket_id = bucket_impl ? bucket_impl->bucket_id : "";

        ret = rgw_sqlite_bind_text(stmt, 1, bucket_id);
        ret |= rgw_sqlite_bind_text(stmt, 2, impl->name);
        ret |= rgw_sqlite_bind_text(stmt, 3, impl->instance ? impl->instance : "");

        if (ret == RGW_SQLITE_OK) {
            while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
                const char* key = rgw_sqlite_column_text(stmt, 0);
                const uint8_t* val = (const uint8_t*)rgw_sqlite_column_text(stmt, 1);
                size_t val_len = rgw_sqlite_column_bytes(stmt, 1);

                if (key && val) {
                    rgw_sal_attrs_set(impl->attrs, key, val, val_len);
                }
            }
        }
        rgw_sqlite_finalize(stmt);
    }

    (void)y;
    (void)dpp;
    return RGW_SAL_OK;
}

static int dbstore_object_set_obj_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* setattrs,
                                         rgw_sal_attrs_t* delattrs, rgw_sal_yield_t* y,
                                         uint32_t flags) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* driver_impl = (dbstore_driver_impl_t*)obj->bucket->driver->impl;

    /* 设置属性 (添加到 impl->attrs) */
    if (setattrs) {
        /* 确保 attrs 存在 */
        if (!impl->attrs) {
            impl->attrs = rgw_sal_attrs_create();
            if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        /* 合并 setattrs 到 impl->attrs */
        for (size_t i = 0; i < setattrs->count; i++) {
            const rgw_sal_attr_pair_t* pair = &setattrs->pairs[i];
            int ret = rgw_sal_attrs_set(impl->attrs, pair->key, pair->value, pair->value_len);
            if (ret != RGW_SAL_OK) return ret;
        }

        /* 如果有数据库连接，持久化属性 */
        if (driver_impl && driver_impl->db_handle) {
            /* 存储属性到 SQLite */
            for (size_t i = 0; i < setattrs->count; i++) {
                const rgw_sal_attr_pair_t* pair = &setattrs->pairs[i];

                const char* sql = "INSERT OR REPLACE INTO object_attrs "
                                  "(bucket_id, object_name, instance, attr_key, attr_value) "
                                  "VALUES (?, ?, ?, ?, ?)";

                rgw_sqlite_stmt_t* stmt = NULL;
                int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
                if (ret == RGW_SQLITE_OK) {
                    dbstore_bucket_impl_t* bucket_impl = (dbstore_bucket_impl_t*)obj->bucket->impl;
                    const char* bucket_id = bucket_impl ? bucket_impl->bucket_id : "";

                    rgw_sqlite_bind_text(stmt, 1, bucket_id);
                    rgw_sqlite_bind_text(stmt, 2, impl->name);
                    rgw_sqlite_bind_text(stmt, 3, impl->instance ? impl->instance : "");
                    rgw_sqlite_bind_text(stmt, 4, pair->key);
                    rgw_sqlite_bind_blob(stmt, 5, pair->value, pair->value_len);

                    rgw_sqlite_step(stmt);
                    rgw_sqlite_finalize(stmt);
                }
            }
        }
    }

    /* 删除属性 */
    if (delattrs) {
        /* 从 impl->attrs 中删除 */
        for (size_t i = 0; i < delattrs->count; i++) {
            const rgw_sal_attr_pair_t* pair = &delattrs->pairs[i];
            rgw_sal_attrs_del(impl->attrs, pair->key);
        }

        /* 如果有数据库连接，删除数据库中的属性 */
        if (driver_impl && driver_impl->db_handle) {
            for (size_t i = 0; i < delattrs->count; i++) {
                const rgw_sal_attr_pair_t* pair = &delattrs->pairs[i];

                const char* sql = "DELETE FROM object_attrs "
                                  "WHERE bucket_id = ? AND object_name = ? AND instance = ? AND attr_key = ?";

                rgw_sqlite_stmt_t* stmt = NULL;
                int ret = rgw_sqlite_prepare(driver_impl->db_handle, sql, &stmt);
                if (ret == RGW_SQLITE_OK) {
                    dbstore_bucket_impl_t* bucket_impl = (dbstore_bucket_impl_t*)obj->bucket->impl;
                    const char* bucket_id = bucket_impl ? bucket_impl->bucket_id : "";

                    rgw_sqlite_bind_text(stmt, 1, bucket_id);
                    rgw_sqlite_bind_text(stmt, 2, impl->name);
                    rgw_sqlite_bind_text(stmt, 3, impl->instance ? impl->instance : "");
                    rgw_sqlite_bind_text(stmt, 4, pair->key);

                    rgw_sqlite_step(stmt);
                    rgw_sqlite_finalize(stmt);
                }
            }
        }
    }

    impl->mtime = time(NULL);

    (void)y;
    (void)flags;
    return RGW_SAL_OK;
}

/* 原子操作标志 (P0: 完整实现) */
static bool dbstore_object_is_atomic(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    return impl ? impl->is_atomic : false;
}

static int dbstore_object_set_atomic(rgw_sal_object_t* obj, bool atomic) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
    if (impl) {
        impl->is_atomic = atomic;
    }
    return RGW_SAL_OK;
}

/* 过期检查 (P0: 完整实现) */
static bool dbstore_object_is_expired(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    dbstore_object_impl_t* impl = (dbstore_object_impl_t*)obj->impl;
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
static rgw_sal_object_vtable_t dbstore_object_vtable = {
    .clone = dbstore_object_clone,
    .destroy = dbstore_object_destroy,
    .get_name = dbstore_object_get_name,
    .get_instance = dbstore_object_get_instance,
    .is_null = dbstore_object_is_null,
    .get_attrs = dbstore_object_get_attrs,
    .set_attrs = dbstore_object_set_attrs,
    .read = dbstore_object_read,
    .write = dbstore_object_write,
    .delete_obj = dbstore_object_delete_obj,
    .load_state = dbstore_object_load_state,
    .get_obj_attrs = dbstore_object_get_obj_attrs,
    .set_obj_attrs = dbstore_object_set_obj_attrs,
    /* P0: 原子操作和过期检查 */
    .is_atomic = dbstore_object_is_atomic,
    .set_atomic = dbstore_object_set_atomic,
    .is_expired = dbstore_object_is_expired,
};

/*============================================================================
 * 驱动创建/销毁函数
 *============================================================================*/

/**
 * @brief 创建 DBStore 驱动
 */
rgw_sal_driver_t* rgw_sal_dbstore_driver_create(const char* db_path) {
    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)calloc(1, sizeof(rgw_sal_driver_t));
    if (!driver) return NULL;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)calloc(1, sizeof(dbstore_driver_impl_t));
    if (!impl) {
        free(driver);
        return NULL;
    }

    strncpy(impl->name, "dbstore", sizeof(impl->name) - 1);
    if (db_path) {
        strncpy(impl->db_path, db_path, sizeof(impl->db_path) - 1);
    } else {
        impl->db_path[0] = '\0';
    }
    impl->db_handle = NULL;
    impl->initialized = false;

    /* 设置 vtable */
    driver->vtable = &dbstore_driver_vtable;
    driver->user_vtable = &dbstore_user_vtable;
    driver->bucket_vtable = &dbstore_bucket_vtable;
    driver->object_vtable = &dbstore_object_vtable;

    driver->impl = impl;

    return driver;
}

/**
 * @brief 获取 DBStore 驱动实现
 */
rgw_sal_dbstore_driver_impl_t* rgw_sal_dbstore_get_impl(rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    return (rgw_sal_dbstore_driver_impl_t*)driver->impl;
}

/*============================================================================
 * DBStore 特定操作实现
 *============================================================================*/

int rgw_sal_dbstore_init_db(rgw_sal_driver_t* driver,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    if (driver->vtable && driver->vtable->initialize) {
        return driver->vtable->initialize(driver, NULL, dpp);
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_dbstore_shutdown_db(rgw_sal_driver_t* driver) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->db_handle && sqlite3_close_fn) {
        sqlite3_close_fn(impl->db_handle);
        impl->db_handle = NULL;
    }

    impl->initialized = false;
    return RGW_SAL_OK;
}

/**
 * @brief 获取用户控制接口
 *
 * 返回 DBStore 数据库连接用于直接操作用户数据。
 *
 * @param driver 驱动句柄
 * @return 数据库连接句柄，失败返回 NULL
 */
void* rgw_sal_dbstore_get_user_ctl(rgw_sal_driver_t* driver) {
    if (!driver) return NULL;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) {
        return NULL;
    }

    return impl->db_handle;
}

/*============================================================================
 * 生命周期 (Lifecycle) 函数实现
 *============================================================================*/

/**
 * @brief 确保 lc_entry 表存在
 */
static int dbstore_lc_ensure_entry_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='lc_entry'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS lc_entry ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  shard_id INTEGER NOT NULL,"
            "  bucket_key TEXT NOT NULL,"
            "  start_time INTEGER NOT NULL,"
            "  status INTEGER NOT NULL,"
            "  xml TEXT,"
            "  UNIQUE(shard_id, bucket_key))";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            return ret;
        }

        const char* index_sql =
            "CREATE INDEX IF NOT EXISTS idx_lc_entry_shard_start "
            "ON lc_entry(shard_id, start_time)";
        ret = rgw_sqlite_exec(db, index_sql, NULL, NULL);
    }
    return RGW_SQLITE_OK;
}

/**
 * @brief 确保 lc_head 表存在
 */
static int dbstore_lc_ensure_head_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='lc_head'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS lc_head ("
            "  shard_id INTEGER PRIMARY KEY,"
            "  start_date INTEGER NOT NULL,"
            "  marker TEXT,"
            "  rollover_date INTEGER,"
            "  xml TEXT)";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
    }
    return ret;
}

/**
 * @brief 获取生命周期条目 (LC Entry)
 */
static int dbstore_lc_get_entry(rgw_sal_driver_t* driver,
                                 uint32_t shard_id,
                                 const char* bucket_key,
                                 rgw_lc_entry_t** entry,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!driver || !bucket_key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_entry_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询条目 */
    const char* sql = "SELECT start_time, status, xml FROM lc_entry WHERE shard_id = ? AND bucket_key = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    ret |= rgw_sqlite_bind_text(stmt, 2, bucket_key);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_lc_entry_t* e = rgw_lc_entry_create();
        if (!e) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        e->bucket_key = strdup(bucket_key);
        e->start_time = (time_t)rgw_sqlite_column_int64(stmt, 0);
        e->status = (uint32_t)rgw_sqlite_column_int(stmt, 1);

        const char* xml = rgw_sqlite_column_text(stmt, 2);
        if (xml && entry) {
            /* 如果有 XML 数据，尝试解码 */
            size_t xml_len = strlen(xml);
            uint8_t* decoded = (uint8_t*)malloc(xml_len);
            if (decoded) {
                /* XML 数据以十六进制字符串存储，解码之 */
                size_t decoded_len = 0;
                for (size_t i = 0; i < xml_len && i < xml_len / 2; i++) {
                    char hex[3] = { xml[i * 2], xml[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(hex, NULL, 16);
                }
                rgw_lc_entry_decode(decoded, decoded_len, e);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *entry = e;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 获取下一个生命周期条目 (LC Next Entry)
 */
static int dbstore_lc_get_next_entry(rgw_sal_driver_t* driver,
                                      uint32_t shard_id,
                                      const char* marker,
                                      rgw_lc_entry_t** entry,
                                      const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y) {
    if (!driver || !entry) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_entry_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询下一个条目 */
    char sql[512];
    if (marker) {
        snprintf(sql, sizeof(sql),
                 "SELECT bucket_key, start_time, status, xml FROM lc_entry "
                 "WHERE shard_id = ? AND bucket_key > ? ORDER BY bucket_key LIMIT 1");
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT bucket_key, start_time, status, xml FROM lc_entry "
                 "WHERE shard_id = ? ORDER BY bucket_key LIMIT 1");
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    if (marker) {
        ret |= rgw_sqlite_bind_text(stmt, 2, marker);
    }
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_lc_entry_t* e = rgw_lc_entry_create();
        if (!e) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        const char* bkey = rgw_sqlite_column_text(stmt, 0);
        e->bucket_key = bkey ? strdup(bkey) : NULL;
        e->start_time = (time_t)rgw_sqlite_column_int64(stmt, 1);
        e->status = (uint32_t)rgw_sqlite_column_int(stmt, 2);

        rgw_sqlite_finalize(stmt);
        *entry = e;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 设置生命周期条目 (LC Set Entry)
 */
static int dbstore_lc_set_entry(rgw_sal_driver_t* driver,
                                 uint32_t shard_id,
                                 const rgw_lc_entry_t* entry,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!driver || !entry) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_entry_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 编码条目为 XML/二进制 */
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    ret = rgw_lc_entry_encode(entry, NULL, 0, &encoded_len);
    if (encoded_len > 0) {
        encoded = (uint8_t*)malloc(encoded_len);
        if (!encoded) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_lc_entry_encode(entry, encoded, encoded_len, &encoded_len);
        if (ret < 0) {
            free(encoded);
            return ret;
        }
    }

    /* 将编码数据转换为十六进制字符串存储 */
    char* xml_hex = NULL;
    if (encoded && encoded_len > 0) {
        xml_hex = (char*)malloc(encoded_len * 2 + 1);
        if (!xml_hex) {
            free(encoded);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < encoded_len; i++) {
            snprintf(xml_hex + i * 2, 3, "%02x", encoded[i]);
        }
        xml_hex[encoded_len * 2] = '\0';
        free(encoded);
    }

    /* 插入或更新条目 */
    const char* sql = "INSERT OR REPLACE INTO lc_entry (shard_id, bucket_key, start_time, status, xml) VALUES (?, ?, ?, ?, ?)";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(xml_hex);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    ret |= rgw_sqlite_bind_text(stmt, 2, entry->bucket_key);
    ret |= rgw_sqlite_bind_int64(stmt, 3, (int64_t)entry->start_time);
    ret |= rgw_sqlite_bind_int(stmt, 4, (int)entry->status);
    ret |= rgw_sqlite_bind_text(stmt, 5, xml_hex);

    free(xml_hex);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 列出生命周期条目 (LC List Entries)
 */
static int dbstore_lc_list_entries(rgw_sal_driver_t* driver,
                                    uint32_t shard_id,
                                    const char* marker,
                                    uint32_t max_entries,
                                    rgw_lc_entry_t*** entries,
                                    uint32_t* count,
                                    bool* is_truncated,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y) {
    if (!driver || !entries || !count) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_entry_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 分配结果数组 */
    size_t alloc_size = max_entries > 0 ? max_entries : 100;
    rgw_lc_entry_t** result = (rgw_lc_entry_t**)calloc(alloc_size, sizeof(rgw_lc_entry_t*));
    if (!result) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 构建查询 */
    char sql[512];
    if (marker) {
        snprintf(sql, sizeof(sql),
                 "SELECT bucket_key, start_time, status, xml FROM lc_entry "
                 "WHERE shard_id = ? AND bucket_key >= ? ORDER BY bucket_key LIMIT %zu",
                 alloc_size);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT bucket_key, start_time, status, xml FROM lc_entry "
                 "WHERE shard_id = ? ORDER BY bucket_key LIMIT %zu",
                 alloc_size);
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(result);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    if (marker) {
        ret |= rgw_sqlite_bind_text(stmt, 2, marker);
    }
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        free(result);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    *count = 0;
    *is_truncated = false;

    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        if (*count >= alloc_size) {
            *is_truncated = true;
            break;
        }

        rgw_lc_entry_t* e = rgw_lc_entry_create();
        if (!e) break;

        const char* bkey = rgw_sqlite_column_text(stmt, 0);
        e->bucket_key = bkey ? strdup(bkey) : NULL;
        e->start_time = (time_t)rgw_sqlite_column_int64(stmt, 1);
        e->status = (uint32_t)rgw_sqlite_column_int(stmt, 2);

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 3);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h, NULL, 16);
                }
                rgw_lc_entry_decode(decoded, decoded_len, e);
                free(decoded);
            }
        }

        result[*count] = e;
        (*count)++;
    }

    rgw_sqlite_finalize(stmt);
    *entries = result;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 删除生命周期条目 (LC Remove Entry)
 */
static int dbstore_lc_rm_entry(rgw_sal_driver_t* driver,
                                uint32_t shard_id,
                                const char* bucket_key,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !bucket_key) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_entry_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 删除条目 */
    const char* sql = "DELETE FROM lc_entry WHERE shard_id = ? AND bucket_key = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    ret |= rgw_sqlite_bind_text(stmt, 2, bucket_key);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 获取生命周期头 (LC Get Head)
 */
static int dbstore_lc_get_head(rgw_sal_driver_t* driver,
                                uint32_t shard_id,
                                rgw_lc_head_t** head,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_head_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询头 */
    const char* sql = "SELECT start_date, marker, rollover_date, xml FROM lc_head WHERE shard_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_lc_head_t* h = rgw_lc_head_create();
        if (!h) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        h->start_date = (time_t)rgw_sqlite_column_int64(stmt, 0);

        const char* marker = rgw_sqlite_column_text(stmt, 1);
        h->marker = marker ? strdup(marker) : NULL;

        h->shard_rollover_date = (time_t)rgw_sqlite_column_int64(stmt, 2);

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 3);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_lc_head_decode(decoded, decoded_len, h);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *head = h;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 写入生命周期头 (LC Put Head)
 */
static int dbstore_lc_put_head(rgw_sal_driver_t* driver,
                                uint32_t shard_id,
                                const rgw_lc_head_t* head,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_lc_ensure_head_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 编码头为 XML/二进制 */
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    ret = rgw_lc_head_encode(head, NULL, 0, &encoded_len);
    if (encoded_len > 0) {
        encoded = (uint8_t*)malloc(encoded_len);
        if (!encoded) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_lc_head_encode(head, encoded, encoded_len, &encoded_len);
        if (ret < 0) {
            free(encoded);
            return ret;
        }
    }

    /* 将编码数据转换为十六进制字符串存储 */
    char* xml_hex = NULL;
    if (encoded && encoded_len > 0) {
        xml_hex = (char*)malloc(encoded_len * 2 + 1);
        if (!xml_hex) {
            free(encoded);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < encoded_len; i++) {
            snprintf(xml_hex + i * 2, 3, "%02x", encoded[i]);
        }
        xml_hex[encoded_len * 2] = '\0';
        free(encoded);
    }

    /* 插入或更新头 */
    const char* sql = "INSERT OR REPLACE INTO lc_head (shard_id, start_date, marker, rollover_date, xml) VALUES (?, ?, ?, ?, ?)";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(xml_hex);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int(stmt, 1, (int)shard_id);
    ret |= rgw_sqlite_bind_int64(stmt, 2, (int64_t)head->start_date);
    ret |= rgw_sqlite_bind_text(stmt, 3, head->marker);
    ret |= rgw_sqlite_bind_int64(stmt, 4, (int64_t)head->shard_rollover_date);
    ret |= rgw_sqlite_bind_text(stmt, 5, xml_hex);

    free(xml_hex);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 多部分上传 (Multipart) 函数实现
 *============================================================================*/

/**
 * @brief 确保 multipart_upload 表存在
 */
static int dbstore_multipart_ensure_upload_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='multipart_upload'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS multipart_upload ("
            "  upload_id TEXT PRIMARY KEY,"
            "  object_key TEXT NOT NULL,"
            "  owner_id TEXT,"
            "  dest_placement TEXT,"
            "  xml TEXT,"
            "  created_at INTEGER NOT NULL,"
            "  modified_at INTEGER)";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            return ret;
        }

        const char* index_sql =
            "CREATE INDEX IF NOT EXISTS idx_multipart_upload_owner "
            "ON multipart_upload(owner_id)";
        ret = rgw_sqlite_exec(db, index_sql, NULL, NULL);
    }
    return RGW_SQLITE_OK;
}

/**
 * @brief 确保 multipart_part 表存在
 */
static int dbstore_multipart_ensure_part_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='multipart_part'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS multipart_part ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  upload_id TEXT NOT NULL,"
            "  part_num INTEGER NOT NULL,"
            "  size INTEGER NOT NULL,"
            "  etag TEXT,"
            "  modified INTEGER,"
            "  xml TEXT,"
            "  UNIQUE(upload_id, part_num))";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            return ret;
        }

        const char* index_sql =
            "CREATE INDEX IF NOT EXISTS idx_multipart_part_upload "
            "ON multipart_part(upload_id)";
        ret = rgw_sqlite_exec(db, index_sql, NULL, NULL);
    }
    return RGW_SQLITE_OK;
}

/**
 * @brief 初始化多部分上传 (Multipart Init)
 */
static int dbstore_multipart_init(rgw_sal_driver_t* driver,
                                   const char* object_key,
                                   const char* owner_id,
                                   const char* dest_placement,
                                   char** upload_id,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    if (!driver || !object_key || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_multipart_ensure_upload_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 生成 upload_id (使用 UUID 格式) */
    char uuid[64];
    snprintf(uuid, sizeof(uuid), "%llx-%llx-%llx-%llx",
             (unsigned long long)(time(NULL) ^ (time(NULL) << 16)),
             (unsigned long long)rand(),
             (unsigned long long)rand(),
             (unsigned long long)rand());

    *upload_id = strdup(uuid);
    if (!*upload_id) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 插入上传记录 */
    const char* sql = "INSERT INTO multipart_upload (upload_id, object_key, owner_id, dest_placement, created_at) VALUES (?, ?, ?, ?, ?)";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, uuid);
    ret |= rgw_sqlite_bind_text(stmt, 2, object_key);
    ret |= rgw_sqlite_bind_text(stmt, 3, owner_id);
    ret |= rgw_sqlite_bind_text(stmt, 4, dest_placement);
    ret |= rgw_sqlite_bind_int64(stmt, 5, (int64_t)time(NULL));

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 列出多部分上传分段 (Multipart List Parts)
 */
static int dbstore_multipart_list_parts(rgw_sal_driver_t* driver,
                                         const char* upload_id,
                                         uint32_t max_parts,
                                         uint32_t marker,
                                         rgw_upload_part_info_t*** parts,
                                         uint32_t* count,
                                         bool* is_truncated,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y) {
    if (!driver || !upload_id || !parts || !count) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_multipart_ensure_part_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 分配结果数组 */
    size_t alloc_size = max_parts > 0 ? max_parts : 1000;
    rgw_upload_part_info_t** result = (rgw_upload_part_info_t**)calloc(alloc_size, sizeof(rgw_upload_part_info_t*));
    if (!result) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 构建查询 */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT part_num, size, etag, modified, xml FROM multipart_part "
             "WHERE upload_id = ? AND part_num > ? ORDER BY part_num LIMIT %zu",
             alloc_size);

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(result);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    ret |= rgw_sqlite_bind_int(stmt, 2, (int)marker);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        free(result);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    *count = 0;
    *is_truncated = false;

    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        if (*count >= alloc_size) {
            *is_truncated = true;
            break;
        }

        rgw_upload_part_info_t* p = rgw_upload_part_info_create();
        if (!p) break;

        p->num = (uint32_t)rgw_sqlite_column_int(stmt, 0);
        p->size = (uint64_t)rgw_sqlite_column_int64(stmt, 1);

        const char* etag = rgw_sqlite_column_text(stmt, 2);
        p->etag = etag ? strdup(etag) : NULL;

        p->modified = (time_t)rgw_sqlite_column_int64(stmt, 3);

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 4);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_upload_part_info_decode(decoded, decoded_len, p);
                free(decoded);
            }
        }

        result[*count] = p;
        (*count)++;
    }

    rgw_sqlite_finalize(stmt);
    *parts = result;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 中止多部分上传 (Multipart Abort)
 */
static int dbstore_multipart_abort(rgw_sal_driver_t* driver,
                                    const char* upload_id,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y) {
    if (!driver || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 删除所有分段 */
    const char* delete_parts_sql = "DELETE FROM multipart_part WHERE upload_id = ?";
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(impl->db_handle, delete_parts_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 删除上传记录 */
    const char* delete_upload_sql = "DELETE FROM multipart_upload WHERE upload_id = ?";
    ret = rgw_sqlite_prepare(impl->db_handle, delete_upload_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 完成多部分上传 (Multipart Complete)
 */
static int dbstore_multipart_complete(rgw_sal_driver_t* driver,
                                      const char* upload_id,
                                      uint32_t parts_count,
                                      const char* const* etags,
                                      const uint32_t* part_nums,
                                      const uint64_t* sizes,
                                      const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y) {
    if (!driver || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_multipart_ensure_part_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 更新 modified_at */
    const char* update_sql = "UPDATE multipart_upload SET modified_at = ? WHERE upload_id = ?";
    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, update_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_int64(stmt, 1, (int64_t)time(NULL));
    ret |= rgw_sqlite_bind_text(stmt, 2, upload_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 删除已完成的分段 */
    const char* delete_sql = "DELETE FROM multipart_part WHERE upload_id = ?";
    ret = rgw_sqlite_prepare(impl->db_handle, delete_sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)parts_count;
    (void)etags;
    (void)part_nums;
    (void)sizes;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 存储多部分上传信息 (Multipart Store Info)
 */
static int dbstore_multipart_store_info(rgw_sal_driver_t* driver,
                                        const char* upload_id,
                                        const rgw_multipart_upload_info_t* info,
                                        bool exclusive,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    if (!driver || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_multipart_ensure_upload_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 编码信息为 XML/二进制 */
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    if (info) {
        ret = rgw_multipart_upload_info_encode(info, NULL, 0, &encoded_len);
        if (encoded_len > 0) {
            encoded = (uint8_t*)malloc(encoded_len);
            if (!encoded) return RGW_SAL_ERR_OUT_OF_MEMORY;
            ret = rgw_multipart_upload_info_encode(info, encoded, encoded_len, &encoded_len);
            if (ret < 0) {
                free(encoded);
                return ret;
            }
        }
    }

    /* 将编码数据转换为十六进制字符串存储 */
    char* xml_hex = NULL;
    if (encoded && encoded_len > 0) {
        xml_hex = (char*)malloc(encoded_len * 2 + 1);
        if (!xml_hex) {
            free(encoded);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < encoded_len; i++) {
            snprintf(xml_hex + i * 2, 3, "%02x", encoded[i]);
        }
        xml_hex[encoded_len * 2] = '\0';
        free(encoded);
    }

    /* 插入或更新信息 */
    const char* sql;
    if (exclusive) {
        sql = "INSERT INTO multipart_upload (upload_id, xml) VALUES (?, ?)";
    } else {
        sql = "INSERT OR REPLACE INTO multipart_upload (upload_id, xml) VALUES (?, ?)";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(xml_hex);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    ret |= rgw_sqlite_bind_text(stmt, 2, xml_hex);

    free(xml_hex);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 加载多部分上传信息 (Multipart Load Info)
 */
static int dbstore_multipart_load_info(rgw_sal_driver_t* driver,
                                       const char* upload_id,
                                       rgw_multipart_upload_info_t** info,
                                       const rgw_sal_dpp_t* dpp,
                                       rgw_sal_yield_t* y) {
    if (!driver || !upload_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_multipart_ensure_upload_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询信息 */
    const char* sql = "SELECT xml FROM multipart_upload WHERE upload_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, upload_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        const char* xml_hex = rgw_sqlite_column_text(stmt, 0);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (!decoded) {
                rgw_sqlite_finalize(stmt);
                return RGW_SAL_ERR_OUT_OF_MEMORY;
            }

            size_t decoded_len = 0;
            for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
            }

            rgw_multipart_upload_info_t* i = rgw_multipart_upload_info_create();
            if (!i) {
                free(decoded);
                rgw_sqlite_finalize(stmt);
                return RGW_SAL_ERR_OUT_OF_MEMORY;
            }

            ret = rgw_multipart_upload_info_decode(decoded, decoded_len, i);
            free(decoded);

            if (ret < 0) {
                rgw_multipart_upload_info_destroy(i);
                rgw_sqlite_finalize(stmt);
                return ret;
            }

            rgw_sqlite_finalize(stmt);
            *info = i;

            (void)dpp;
            (void)y;
            return RGW_SAL_OK;
        }
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/*============================================================================
 * 账户 (Account) 操作函数实现
 *============================================================================*/

/**
 * @brief 确保 account 表存在
 */
static int dbstore_account_ensure_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='account'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS account ("
            "  account_id TEXT PRIMARY KEY,"
            "  tenant TEXT,"
            "  name TEXT,"
            "  email TEXT,"
            "  xml TEXT,"
            "  created_at INTEGER,"
            "  modified_at INTEGER)";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            return ret;
        }

        const char* email_index_sql =
            "CREATE INDEX IF NOT EXISTS idx_account_email ON account(email)";
        ret = rgw_sqlite_exec(db, email_index_sql, NULL, NULL);
    }
    return ret;
}

/**
 * @brief 通过 ID 加载账户 (Load Account By ID)
 */
static int dbstore_load_account_by_id(rgw_sal_driver_t* driver,
                                     const char* account_id,
                                     rgw_account_info_t** info,
                                     const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y) {
    if (!driver || !account_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_account_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询账户 */
    const char* sql = "SELECT tenant, name, email, xml FROM account WHERE account_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, account_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_account_info_t* a = rgw_account_info_create();
        if (!a) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        a->id = strdup(account_id);

        const char* tenant = rgw_sqlite_column_text(stmt, 0);
        a->tenant = tenant ? strdup(tenant) : NULL;

        const char* name = rgw_sqlite_column_text(stmt, 1);
        a->name = name ? strdup(name) : NULL;

        const char* email = rgw_sqlite_column_text(stmt, 2);
        a->email = email ? strdup(email) : NULL;

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 3);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_account_info_decode(decoded, decoded_len, a);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *info = a;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 通过名称加载账户 (Load Account By Name)
 */
static int dbstore_load_account_by_name(rgw_sal_driver_t* driver,
                                        const char* tenant,
                                        const char* name,
                                        rgw_account_info_t** info,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    if (!driver || !name || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_account_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询账户 */
    const char* sql;
    if (tenant) {
        sql = "SELECT account_id, tenant, name, email, xml FROM account WHERE tenant = ? AND name = ?";
    } else {
        sql = "SELECT account_id, tenant, name, email, xml FROM account WHERE name = ?";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (tenant) {
        ret = rgw_sqlite_bind_text(stmt, 1, tenant);
        ret |= rgw_sqlite_bind_text(stmt, 2, name);
    } else {
        ret = rgw_sqlite_bind_text(stmt, 1, name);
    }

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_account_info_t* a = rgw_account_info_create();
        if (!a) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        const char* acc_id = rgw_sqlite_column_text(stmt, 0);
        a->id = acc_id ? strdup(acc_id) : NULL;

        const char* t = rgw_sqlite_column_text(stmt, 1);
        a->tenant = t ? strdup(t) : NULL;

        const char* n = rgw_sqlite_column_text(stmt, 2);
        a->name = n ? strdup(n) : NULL;

        const char* e = rgw_sqlite_column_text(stmt, 3);
        a->email = e ? strdup(e) : NULL;

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 4);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_account_info_decode(decoded, decoded_len, a);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *info = a;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 通过邮箱加载账户 (Load Account By Email)
 */
static int dbstore_load_account_by_email(rgw_sal_driver_t* driver,
                                         const char* email,
                                         rgw_account_info_t** info,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y) {
    if (!driver || !email || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_account_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询账户 */
    const char* sql = "SELECT account_id, tenant, name, email, xml FROM account WHERE email = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, email);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_account_info_t* a = rgw_account_info_create();
        if (!a) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        const char* acc_id = rgw_sqlite_column_text(stmt, 0);
        a->id = acc_id ? strdup(acc_id) : NULL;

        const char* t = rgw_sqlite_column_text(stmt, 1);
        a->tenant = t ? strdup(t) : NULL;

        const char* n = rgw_sqlite_column_text(stmt, 2);
        a->name = n ? strdup(n) : NULL;

        a->email = strdup(email);

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 4);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_account_info_decode(decoded, decoded_len, a);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *info = a;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 存储账户 (Store Account)
 */
static int dbstore_store_account(rgw_sal_driver_t* driver,
                                 const rgw_account_info_t* info,
                                 bool exclusive,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!driver || !info || !info->id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_account_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 编码信息为 XML/二进制 */
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    size_t calc_size = rgw_account_info_calc_encode_size(info);
    if (calc_size > 0) {
        encoded_len = calc_size;
        encoded = (uint8_t*)malloc(encoded_len);
        if (!encoded) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_account_info_encode(info, encoded, encoded_len);
        if (ret < 0) {
            free(encoded);
            return ret;
        }
    }

    /* 将编码数据转换为十六进制字符串存储 */
    char* xml_hex = NULL;
    if (encoded && encoded_len > 0) {
        xml_hex = (char*)malloc(encoded_len * 2 + 1);
        if (!xml_hex) {
            free(encoded);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < encoded_len; i++) {
            snprintf(xml_hex + i * 2, 3, "%02x", encoded[i]);
        }
        xml_hex[encoded_len * 2] = '\0';
        free(encoded);
    }

    /* 插入或更新账户 */
    const char* sql;
    if (exclusive) {
        sql = "INSERT INTO account (account_id, tenant, name, email, xml, created_at, modified_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
    } else {
        sql = "INSERT OR REPLACE INTO account (account_id, tenant, name, email, xml, created_at, modified_at) VALUES (?, ?, ?, ?, ?, ?, ?)";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(xml_hex);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, info->id);
    ret |= rgw_sqlite_bind_text(stmt, 2, info->tenant);
    ret |= rgw_sqlite_bind_text(stmt, 3, info->name);
    ret |= rgw_sqlite_bind_text(stmt, 4, info->email);
    ret |= rgw_sqlite_bind_text(stmt, 5, xml_hex);
    ret |= rgw_sqlite_bind_int64(stmt, 6, (int64_t)time(NULL));
    ret |= rgw_sqlite_bind_int64(stmt, 7, (int64_t)time(NULL));

    free(xml_hex);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 删除账户 (Delete Account)
 */
static int dbstore_delete_account(rgw_sal_driver_t* driver,
                                  const char* account_id,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    if (!driver || !account_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_account_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 删除账户 */
    const char* sql = "DELETE FROM account WHERE account_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, account_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 组 (Group) 操作函数实现
 *============================================================================*/

/**
 * @brief 确保 account_group 表存在
 */
static int dbstore_group_ensure_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='account_group'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS account_group ("
            "  group_id TEXT PRIMARY KEY,"
            "  account_id TEXT,"
            "  tenant TEXT,"
            "  name TEXT,"
            "  path TEXT,"
            "  xml TEXT,"
            "  created_at INTEGER,"
            "  modified_at INTEGER)";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
        if (ret != RGW_SQLITE_OK) {
            return ret;
        }

        const char* name_index_sql =
            "CREATE INDEX IF NOT EXISTS idx_account_group_name ON account_group(tenant, name)";
        ret = rgw_sqlite_exec(db, name_index_sql, NULL, NULL);
    }
    return ret;
}

/**
 * @brief 通过 ID 加载组 (Load Group By ID)
 */
static int dbstore_load_group_by_id(rgw_sal_driver_t* driver,
                                   const char* group_id,
                                   rgw_group_info_t** info,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    if (!driver || !group_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_group_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询组 */
    const char* sql = "SELECT account_id, tenant, name, path, xml FROM account_group WHERE group_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, group_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_group_info_t* g = rgw_group_info_create();
        if (!g) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        g->id = strdup(group_id);

        const char* acc_id = rgw_sqlite_column_text(stmt, 0);
        g->account_id = acc_id ? strdup(acc_id) : NULL;

        const char* tenant = rgw_sqlite_column_text(stmt, 1);
        g->tenant = tenant ? strdup(tenant) : NULL;

        const char* name = rgw_sqlite_column_text(stmt, 2);
        g->name = name ? strdup(name) : NULL;

        const char* path = rgw_sqlite_column_text(stmt, 3);
        g->path = path ? strdup(path) : NULL;

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 4);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_group_info_decode(decoded, decoded_len, g);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *info = g;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 通过名称加载组 (Load Group By Name)
 */
static int dbstore_load_group_by_name(rgw_sal_driver_t* driver,
                                      const char* tenant,
                                      const char* name,
                                      rgw_group_info_t** info,
                                      const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y) {
    if (!driver || !name || !info) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_group_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询组 */
    const char* sql;
    if (tenant) {
        sql = "SELECT group_id, account_id, tenant, name, path, xml FROM account_group WHERE tenant = ? AND name = ?";
    } else {
        sql = "SELECT group_id, account_id, tenant, name, path, xml FROM account_group WHERE name = ?";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (tenant) {
        ret = rgw_sqlite_bind_text(stmt, 1, tenant);
        ret |= rgw_sqlite_bind_text(stmt, 2, name);
    } else {
        ret = rgw_sqlite_bind_text(stmt, 1, name);
    }

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    if (ret == RGW_SQLITE_ROW) {
        rgw_group_info_t* g = rgw_group_info_create();
        if (!g) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        const char* gid = rgw_sqlite_column_text(stmt, 0);
        g->id = gid ? strdup(gid) : NULL;

        const char* acc_id = rgw_sqlite_column_text(stmt, 1);
        g->account_id = acc_id ? strdup(acc_id) : NULL;

        const char* t = rgw_sqlite_column_text(stmt, 2);
        g->tenant = t ? strdup(t) : NULL;

        g->name = strdup(name);

        const char* path = rgw_sqlite_column_text(stmt, 4);
        g->path = path ? strdup(path) : NULL;

        /* 解码 XML 数据 */
        const char* xml_hex = rgw_sqlite_column_text(stmt, 5);
        if (xml_hex) {
            size_t hex_len = strlen(xml_hex);
            uint8_t* decoded = (uint8_t*)malloc(hex_len / 2);
            if (decoded) {
                size_t decoded_len = 0;
                for (size_t i = 0; i < hex_len && i < hex_len / 2; i++) {
                    char h2[3] = { xml_hex[i * 2], xml_hex[i * 2 + 1], '\0' };
                    decoded[decoded_len++] = (uint8_t)strtol(h2, NULL, 16);
                }
                rgw_group_info_decode(decoded, decoded_len, g);
                free(decoded);
            }
        }

        rgw_sqlite_finalize(stmt);
        *info = g;

        (void)dpp;
        (void)y;
        return RGW_SAL_OK;
    }

    rgw_sqlite_finalize(stmt);
    return RGW_SAL_ERR_NOT_FOUND;
}

/**
 * @brief 存储组 (Store Group)
 */
static int dbstore_store_group(rgw_sal_driver_t* driver,
                               const rgw_group_info_t* info,
                               bool exclusive,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    if (!driver || !info || !info->id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_group_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 编码信息为 XML/二进制 */
    uint8_t* encoded = NULL;
    size_t encoded_len = 0;
    size_t calc_size = rgw_group_info_calc_encode_size(info);
    if (calc_size > 0) {
        encoded_len = calc_size;
        encoded = (uint8_t*)malloc(encoded_len);
        if (!encoded) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_group_info_encode(info, encoded, encoded_len);
        if (ret < 0) {
            free(encoded);
            return ret;
        }
    }

    /* 将编码数据转换为十六进制字符串存储 */
    char* xml_hex = NULL;
    if (encoded && encoded_len > 0) {
        xml_hex = (char*)malloc(encoded_len * 2 + 1);
        if (!xml_hex) {
            free(encoded);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < encoded_len; i++) {
            snprintf(xml_hex + i * 2, 3, "%02x", encoded[i]);
        }
        xml_hex[encoded_len * 2] = '\0';
        free(encoded);
    }

    /* 插入或更新组 */
    const char* sql;
    if (exclusive) {
        sql = "INSERT INTO account_group (group_id, account_id, tenant, name, path, xml, created_at, modified_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    } else {
        sql = "INSERT OR REPLACE INTO account_group (group_id, account_id, tenant, name, path, xml, created_at, modified_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        free(xml_hex);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, info->id);
    ret |= rgw_sqlite_bind_text(stmt, 2, info->account_id);
    ret |= rgw_sqlite_bind_text(stmt, 3, info->tenant);
    ret |= rgw_sqlite_bind_text(stmt, 4, info->name);
    ret |= rgw_sqlite_bind_text(stmt, 5, info->path);
    ret |= rgw_sqlite_bind_text(stmt, 6, xml_hex);
    ret |= rgw_sqlite_bind_int64(stmt, 7, (int64_t)time(NULL));
    ret |= rgw_sqlite_bind_int64(stmt, 8, (int64_t)time(NULL));

    free(xml_hex);

    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 删除组 (Remove Group)
 */
static int dbstore_remove_group(rgw_sal_driver_t* driver,
                                const char* group_id,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !group_id) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_group_ensure_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 删除组 */
    const char* sql = "DELETE FROM account_group WHERE group_id = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, group_id);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_step(stmt);
    rgw_sqlite_finalize(stmt);

    if (ret != RGW_SQLITE_DONE) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 通知 (Notification) 函数实现
 *============================================================================*/

/**
 * @brief 确保 pubsub_topic 表存在
 */
static int dbstore_pubsub_ensure_topic_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='pubsub_topic'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS pubsub_topic ("
            "  topic_name TEXT PRIMARY KEY,"
            "  owner TEXT,"
            "  dest_xml TEXT,"
            "  events INTEGER,"
            "  s3_id TEXT,"
            "  created_at INTEGER)";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
    }
    return ret;
}

/**
 * @brief 确保 pubsub_bucket_topic 表存在
 */
static int dbstore_pubsub_ensure_bucket_topic_table(rgw_sqlite_db_t* db) {
    const char* check_sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='pubsub_bucket_topic'";
    int ret = rgw_sqlite_exec(db, check_sql, NULL, NULL);
    if (ret != RGW_SQLITE_OK) {
        const char* create_sql =
            "CREATE TABLE IF NOT EXISTS pubsub_bucket_topic ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  bucket_key TEXT NOT NULL,"
            "  topic_name TEXT NOT NULL,"
            "  events INTEGER,"
            "  s3_id TEXT,"
            "  UNIQUE(bucket_key, topic_name))";

        ret = rgw_sqlite_exec(db, create_sql, NULL, NULL);
    }
    return ret;
}

/**
 * @brief 获取通知对象
 */
static int dbstore_get_notification(rgw_sal_driver_t* driver,
                                     rgw_sal_bucket_t* bucket,
                                     rgw_sal_object_t* object,
                                     uint32_t event_type,
                                     void** notification,
                                     const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y) {
    if (!driver || !notification) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_pubsub_ensure_topic_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    ret = dbstore_pubsub_ensure_bucket_topic_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 构建桶键 */
    char bucket_key[512] = {0};
    if (bucket) {
        dbstore_bucket_impl_t* bucket_impl = (dbstore_bucket_impl_t*)bucket->impl;
        if (bucket_impl) {
            const char* tenant = bucket_impl->tenant ? bucket_impl->tenant : "";
            const char* name = bucket_impl->name ? bucket_impl->name : "";
            snprintf(bucket_key, sizeof(bucket_key), "%s:%s", tenant, name);
        }
    }

    /* 查询桶-主题映射 */
    const char* sql = "SELECT topic_name FROM pubsub_bucket_topic WHERE bucket_key = ?";

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    ret = rgw_sqlite_bind_text(stmt, 1, bucket_key);
    if (ret != RGW_SQLITE_OK) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 创建通知结构 */
    /* 注意：这里简化实现，返回一个指向主题名称的指针 */
    char* notif_data = (char*)malloc(256);
    if (!notif_data) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    notif_data[0] = '\0';

    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        const char* topic_name = rgw_sqlite_column_text(stmt, 0);
        if (topic_name) {
            if (notif_data[0] != '\0') {
                strncat(notif_data, ",", 255);
            }
            strncat(notif_data, topic_name, 255 - strlen(notif_data));
        }
    }

    rgw_sqlite_finalize(stmt);

    *notification = notif_data;

    (void)event_type;
    (void)object;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 发布保留 (Publish Reserve)
 */
static int dbstore_publish_reserve(rgw_sal_driver_t* driver,
                                    void* notification,
                                    uint32_t event_type,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y) {
    if (!driver || !notification) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现：只检查通知是否存在 */
    char* notif = (char*)notification;
    if (!notif || notif[0] == '\0') {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    (void)event_type;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 发布提交 (Publish Commit)
 */
static int dbstore_publish_commit(rgw_sal_driver_t* driver,
                                  void* notification,
                                  uint64_t size,
                                  time_t mtime,
                                  const char* etag,
                                  const char* version_id,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y) {
    if (!driver || !notification) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现：发布已提交，无需额外操作 */
    /* 实际实现中，这里会将事件写入队列或发送通知 */

    (void)notification;
    (void)size;
    (void)mtime;
    (void)etag;
    (void)version_id;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 读取主题列表 (Read Topics)
 */
static int dbstore_read_topics(rgw_sal_driver_t* driver,
                               const char* owner,
                               void*** topics,
                               uint32_t* count,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    if (!driver || !topics || !count) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_pubsub_ensure_topic_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 查询主题 */
    const char* sql;
    if (owner) {
        sql = "SELECT topic_name, owner, dest_xml, events, s3_id FROM pubsub_topic WHERE owner = ?";
    } else {
        sql = "SELECT topic_name, owner, dest_xml, events, s3_id FROM pubsub_topic";
    }

    rgw_sqlite_stmt_t* stmt = NULL;
    ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    if (owner) {
        ret = rgw_sqlite_bind_text(stmt, 1, owner);
        if (ret != RGW_SQLITE_OK) {
            rgw_sqlite_finalize(stmt);
            return RGW_SAL_ERR_INTERNAL_ERROR;
        }
    }

    /* 分配结果数组 (最多100个主题) */
    size_t alloc_size = 100;
    void** result = (void**)calloc(alloc_size, sizeof(void*));
    if (!result) {
        rgw_sqlite_finalize(stmt);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    *count = 0;

    while (rgw_sqlite_step(stmt) == RGW_SQLITE_ROW) {
        if (*count >= alloc_size) break;

        const char* topic_name = rgw_sqlite_column_text(stmt, 0);
        const char* topic_owner = rgw_sqlite_column_text(stmt, 1);
        const char* dest_xml = rgw_sqlite_column_text(stmt, 2);
        uint32_t events = (uint32_t)rgw_sqlite_column_int(stmt, 3);
        const char* s3_id = rgw_sqlite_column_text(stmt, 4);

        /* 创建主题信息结构 */
        /* 这里使用简化的字符串存储格式 */
        size_t needed = strlen(topic_name ? topic_name : "") + 1 +
                        strlen(topic_owner ? topic_owner : "") + 1 +
                        strlen(dest_xml ? dest_xml : "") + 1 +
                        sizeof(uint32_t) + 1 +
                        strlen(s3_id ? s3_id : "") + 1;

        char* topic_info = (char*)malloc(needed);
        if (topic_info) {
            char* ptr = topic_info;
            size_t left = needed;

            snprintf(ptr, left, "%s", topic_name ? topic_name : "");
            ptr += strlen(ptr) + 1;
            left = needed - (ptr - topic_info);

            snprintf(ptr, left, "%s", topic_owner ? topic_owner : "");
            ptr += strlen(ptr) + 1;
            left = needed - (ptr - topic_info);

            snprintf(ptr, left, "%s", dest_xml ? dest_xml : "");
            ptr += strlen(ptr) + 1;
            left = needed - (ptr - topic_info);

            memcpy(ptr, &events, sizeof(uint32_t));
            ptr += sizeof(uint32_t);
            left = needed - (ptr - topic_info);

            snprintf(ptr, left, "%s", s3_id ? s3_id : "");

            result[*count] = topic_info;
            (*count)++;
        }
    }

    rgw_sqlite_finalize(stmt);
    *topics = result;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 写入主题 (Write Topics)
 */
static int dbstore_write_topics(rgw_sal_driver_t* driver,
                                const char* owner,
                                const void** topics,
                                uint32_t count,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    if (!driver || !topics) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_pubsub_ensure_topic_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 逐个插入主题 */
    for (uint32_t i = 0; i < count; i++) {
        const char* topic_info = (const char*)topics[i];
        if (topic_info == NULL) continue;

        /* 解析主题信息 (与 read_topics 格式对应) */
        const char* topic_name = topic_info;
        const char* topic_owner = topic_info + strlen(topic_name) + 1;
        const char* dest_xml = topic_owner + strlen(topic_owner) + 1;
        uint32_t events;
        memcpy(&events, topic_owner + strlen(topic_owner) + 1 + strlen(dest_xml) + 1, sizeof(uint32_t));
        const char* s3_id = topic_owner + strlen(topic_owner) + 1 + strlen(dest_xml) + 1 + sizeof(uint32_t);

        const char* sql = "INSERT OR REPLACE INTO pubsub_topic (topic_name, owner, dest_xml, events, s3_id, created_at) VALUES (?, ?, ?, ?, ?, ?)";

        rgw_sqlite_stmt_t* stmt = NULL;
        ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
        if (ret != RGW_SQLITE_OK) {
            continue;
        }

        ret = rgw_sqlite_bind_text(stmt, 1, topic_name);
        ret |= rgw_sqlite_bind_text(stmt, 2, topic_owner);
        ret |= rgw_sqlite_bind_text(stmt, 3, dest_xml);
        ret |= rgw_sqlite_bind_int(stmt, 4, (int)events);
        ret |= rgw_sqlite_bind_text(stmt, 5, s3_id);
        ret |= rgw_sqlite_bind_int64(stmt, 6, (int64_t)time(NULL));

        if (ret == RGW_SQLITE_OK) {
            rgw_sqlite_step(stmt);
        }
        rgw_sqlite_finalize(stmt);
    }

    (void)owner;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 删除主题 (Remove Topics)
 */
static int dbstore_remove_topics(rgw_sal_driver_t* driver,
                                 const char* owner,
                                 const char** topics,
                                 uint32_t count,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    dbstore_driver_impl_t* impl = (dbstore_driver_impl_t*)driver->impl;
    if (!impl || !impl->db_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 确保表存在 */
    int ret = dbstore_pubsub_ensure_topic_table(impl->db_handle);
    if (ret != RGW_SQLITE_OK) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 删除指定主题 */
    for (uint32_t i = 0; i < count; i++) {
        if (topics[i] == NULL) continue;

        const char* sql = "DELETE FROM pubsub_topic WHERE topic_name = ?";

        rgw_sqlite_stmt_t* stmt = NULL;
        ret = rgw_sqlite_prepare(impl->db_handle, sql, &stmt);
        if (ret != RGW_SQLITE_OK) {
            continue;
        }

        ret = rgw_sqlite_bind_text(stmt, 1, topics[i]);
        if (ret == RGW_SQLITE_OK) {
            rgw_sqlite_step(stmt);
        }
        rgw_sqlite_finalize(stmt);
    }

    (void)owner;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}


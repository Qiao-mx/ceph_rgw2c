/**
 * @file rgw_rados_user.c
 * @brief RADOS 用户存储实现
 *
 * 实现用户存储的 RADOS 后端功能。
 * 使用 librados.h C API 进行存储操作。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "rgw_sal_rados.h"
#include "rgw_rados_ctx.h"
#include "rgw_omap.h"
#include "rgw_user_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** RADOS 用户句柄池名称 */
#define RGW_RADOS_USER_POOL     RGW_RADOS_CTX_POOL_USERS_UID

/** RADOS email 索引池名称 */
#define RGW_RADOS_EMAIL_POOL    RGW_RADOS_CTX_POOL_USERS_EMAIL

/** RADOS access key 索引池名称 */
#define RGW_RADOS_KEYS_POOL     RGW_RADOS_CTX_POOL_USERS_KEYS

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 获取用户池的 IO 上下文
 *
 * @param driver RADOS 驱动
 * @param ioctx 输出参数，返回 IO 上下文
 *
 * @return 执行结果
 */
static int get_user_pool_ioctx(rgw_sal_driver_t* driver, rados_ioctx_t* ioctx) {
    if (!driver || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    return rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_UID,
                                         ioctx);
}

/**
 * @brief 获取 email 索引池的 IO 上下文
 */
static int get_email_pool_ioctx(rgw_sal_driver_t* driver, rados_ioctx_t* ioctx) {
    if (!driver || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    return rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_EMAIL,
                                         ioctx);
}

/**
 * @brief 获取 keys 索引池的 IO 上下文
 */
static int get_keys_pool_ioctx(rgw_sal_driver_t* driver, rados_ioctx_t* ioctx) {
    if (!driver || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    return rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_KEYS,
                                         ioctx);
}

/**
 * @brief 构建用户 OMAP 对象 ID
 *
 * 用户数据存储在 OMAP 中，对象名为用户 ID。
 *
 * @param user 用户对象
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
static int make_user_oid(rgw_sal_user_t* user, char* buf, size_t buf_size) {
    if (!user || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 用户 OMAP 对象 ID 就是用户 ID 字符串 */
    const char* uid = rgw_sal_user_get_id(user);
    if (!uid) {
        return RGW_ERR_INVALID_ARG;
    }

    if (strlen(uid) >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    strcpy(buf, uid);
    return RGW_OK;
}

/**
 * @brief 释放用户索引
 *
 * @param index_keys 索引键数组
 * @param num_keys 键数量
 */
static void free_user_index_keys(char** index_keys, size_t num_keys) {
    if (!index_keys) {
        return;
    }

    for (size_t i = 0; i < num_keys; i++) {
        free(index_keys[i]);
    }
    free(index_keys);
}

/**
 * @brief 构建用户索引键
 *
 * @param info 用户信息
 * @param email 输出参数，email 索引键（需要释放）
 * @param access_key 输出参数，access key 索引键（需要释放）
 *
 * @return 执行结果
 */
static int build_user_index_keys(const rgw_user_info_t* info,
                                  char** email,
                                  char** access_key) {
    if (!info || !email || !access_key) {
        return RGW_ERR_INVALID_ARG;
    }

    *email = NULL;
    *access_key = NULL;

    /* 构建 email 索引键 */
    if (info->email && info->email[0] != '\0') {
        *email = rgw_user_email_index_make_key(info->email);
        if (!*email) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
    }

    /* 构建 access key 索引键 - 这里需要从用户信息中获取 access key */
    /* TODO: 需要从 rgw_user_info 中获取 access key 信息 */

    return RGW_OK;
}

/*============================================================================
 * 用户操作实现
 *============================================================================*/

/**
 * @brief 加载用户
 *
 * 从 RADOS 存储中加载用户信息。
 */
static int rados_user_load(rgw_sal_user_t* user,
                            const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_user_pool_ioctx(user->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建 OMAP 键 */
    char oid[RGW_USER_INFO_MAX_ID_LEN * 2 + 2];
    ret = make_user_oid(user, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 获取所有 OMAP 值 */
    uint8_t* val = NULL;
    size_t val_len = 0;
    ret = rgw_omap_get(ioctx, oid, "", &val, &val_len);
    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解码用户信息 */
    rgw_user_info_t info;
    ret = rgw_user_info_decode(val, val_len, &info);
    free(val);

    if (ret != 0) {
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 填充用户实现结构 */
    free(impl->id);
    free(impl->tenant);
    free(impl->display_name);
    free(impl->email);
    free(impl->ns);

    impl->id = info.user_id.id ? strdup(info.user_id.id) : NULL;
    impl->tenant = info.user_id.tenant ? strdup(info.user_id.tenant) : NULL;
    impl->display_name = info.display_name ? strdup(info.display_name) : NULL;
    impl->email = info.email ? strdup(info.email) : NULL;
    impl->ns = info.user_id.ns ? strdup(info.user_id.ns) : NULL;
    impl->user_type = info.user_type;
    impl->max_buckets = info.max_buckets;

    impl->loaded = true;

    /* 释放解码后的用户信息动态成员（只读副本） */
    rgw_user_info_free_members(&info);

    return RGW_SAL_OK;
}

/**
 * @brief 保存用户
 *
 * 将用户信息保存到 RADOS 存储。
 */
static int rados_user_store(rgw_sal_user_t* user,
                            const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y,
                            bool exclusive) {
    (void)dpp;
    (void)y;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_user_pool_ioctx(user->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建用户信息 */
    rgw_user_info_t info;
    rgw_user_info_init(&info);

    info.user_id.id = impl->id ? strdup(impl->id) : NULL;
    info.user_id.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
    info.user_id.ns = impl->ns ? strdup(impl->ns) : NULL;
    info.user_id.type = impl->user_type;
    info.display_name = impl->display_name ? strdup(impl->display_name) : NULL;
    info.email = impl->email ? strdup(impl->email) : NULL;
    info.max_buckets = impl->max_buckets;
    info.mtime = time(NULL);

    /* 动态编码用户信息 */
    uint8_t* buf = NULL;
    size_t buf_len = 0;
    ret = rgw_user_info_encode_alloc(&info, &buf, &buf_len);
    rgw_user_info_free_members(&info);

    if (ret != 0 || !buf) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建 OMAP 键 */
    char oid[RGW_USER_INFO_MAX_ID_LEN * 2 + 2];
    ret = make_user_oid(user, oid, sizeof(oid));
    if (ret != 0) {
        free(buf);
        return ret;
    }

    /* 写入 OMAP */
    ret = rgw_omap_set(ioctx, oid, "", buf, buf_len, exclusive);
    free(buf);

    if (ret != 0) {
        if (ret == RGW_ERR_ALREADY_EXISTS) {
            return RGW_SAL_ERR_EXISTS;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 删除用户
 *
 * 从 RADOS 存储中删除用户信息。
 */
static int rados_user_remove(rgw_sal_user_t* user,
                             const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_user_pool_ioctx(user->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建 OMAP 键 */
    char oid[RGW_USER_INFO_MAX_ID_LEN * 2 + 2];
    ret = make_user_oid(user, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 删除 OMAP 对象 */
    /* 注意: rgw_omap 不提供直接删除对象的函数，
     * 需要使用 librados API 或清空 OMAP */
    /* 简化实现：清空 OMAP 内容 */
    ret = rgw_omap_clear(ioctx, oid);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 删除索引 */
    /* TODO: 删除 email 索引和 access key 索引 */

    return RGW_SAL_OK;
}

/**
 * @brief 按 access key 获取用户
 *
 * 通过 Access Key 查找用户。
 */
static int rados_user_get_by_access_key(rgw_sal_driver_t* driver,
                                        const char* access_key,
                                        rgw_sal_user_t** user,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!driver || !access_key || !user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *user = NULL;

    /* 获取 keys 池 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_keys_pool_ioctx(driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 从索引池中查找 access key 对应的用户 ID */
    uint8_t* val = NULL;
    size_t val_len = 0;
    ret = rgw_omap_get(ioctx, "", access_key, &val, &val_len);
    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解析索引值获取用户 ID */
    rgw_user_id_t uid;
    ret = rgw_user_id_decode_from_index((const char*)val, &uid);
    free(val);

    if (ret != 0) {
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 创建用户对象 */
    rgw_sal_user_t* new_user = rgw_sal_get_user(driver, (const rgw_sal_user_id_t*)&uid);
    if (!new_user) {
        free(uid.id);
        free(uid.tenant);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 加载用户信息 */
    ret = rados_user_load(new_user, dpp, y);
    if (ret != 0) {
        rgw_sal_user_destroy(new_user);
        free(uid.id);
        free(uid.tenant);
        return ret;
    }

    free(uid.id);
    free(uid.tenant);

    *user = new_user;
    return RGW_SAL_OK;
}

/**
 * @brief 按 email 获取用户
 *
 * 通过 Email 查找用户。
 */
static int rados_user_get_by_email(rgw_sal_driver_t* driver,
                                   const char* email,
                                   rgw_sal_user_t** user,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!driver || !email || !user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *user = NULL;

    /* 获取 email 池 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_email_pool_ioctx(driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 从索引池中查找 email 对应的用户 ID */
    uint8_t* val = NULL;
    size_t val_len = 0;
    ret = rgw_omap_get(ioctx, "", email, &val, &val_len);
    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解析索引值获取用户 ID */
    rgw_user_id_t uid;
    ret = rgw_user_id_decode_from_index((const char*)val, &uid);
    free(val);

    if (ret != 0) {
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 创建用户对象 */
    rgw_sal_user_t* new_user = rgw_sal_get_user(driver, (const rgw_sal_user_id_t*)&uid);
    if (!new_user) {
        free(uid.id);
        free(uid.tenant);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 加载用户信息 */
    ret = rados_user_load(new_user, dpp, y);
    if (ret != 0) {
        rgw_sal_user_destroy(new_user);
        free(uid.id);
        free(uid.tenant);
        return ret;
    }

    free(uid.id);
    free(uid.tenant);

    *user = new_user;
    return RGW_SAL_OK;
}

/*============================================================================
 * 用户 vtable 函数实现
 *============================================================================*/

/**
 * @brief 克隆用户
 *
 * 创建用户的深拷贝。
 */
static void* rados_user_clone(const rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }

    rgw_sal_user_t* new_user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!new_user) {
        return NULL;
    }

    rados_user_impl_t* old_impl = (rados_user_impl_t*)user->impl;
    rados_user_impl_t* new_impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
    if (!new_impl) {
        free(new_user);
        return NULL;
    }

    /* 深拷贝所有字符串字段 */
    if (old_impl->id) {
        new_impl->id = strdup(old_impl->id);
        if (!new_impl->id) goto fail;
    }
    if (old_impl->tenant) {
        new_impl->tenant = strdup(old_impl->tenant);
        if (!new_impl->tenant) goto fail;
    }
    if (old_impl->display_name) {
        new_impl->display_name = strdup(old_impl->display_name);
        if (!new_impl->display_name) goto fail;
    }
    if (old_impl->email) {
        new_impl->email = strdup(old_impl->email);
        if (!new_impl->email) goto fail;
    }
    if (old_impl->ns) {
        new_impl->ns = strdup(old_impl->ns);
        if (!new_impl->ns) goto fail;
    }

    /* 拷贝标量字段 */
    new_impl->user_type = old_impl->user_type;
    new_impl->max_buckets = old_impl->max_buckets;
    new_impl->loaded = old_impl->loaded;
    new_impl->usage_loaded = old_impl->usage_loaded;
    new_impl->user_info_stored = old_impl->user_info_stored;

    /* 拷贝配额信息 */
    new_impl->quota_info = old_impl->quota_info;

    /* 拷贝用户权限 */
    new_impl->user_caps = old_impl->user_caps;

    /* 拷贝版本跟踪器 */
    new_impl->version_tracker = old_impl->version_tracker;

    /* 拷贝使用统计 */
    new_impl->usage = old_impl->usage;

    /* 拷贝属性映射 (引用计数方式) */
    if (old_impl->attrs) {
        new_impl->attrs = rgw_sal_attrs_create();
        if (!new_impl->attrs) goto fail;
        /* 简单引用: 共享同一份数据，由销毁函数处理 */
        new_impl->attrs->count = old_impl->attrs->count;
        new_impl->attrs->capacity = old_impl->attrs->capacity;
        new_impl->attrs->pairs = old_impl->attrs->pairs;
    }

    new_user->vtable = user->vtable;
    new_user->impl = new_impl;
    new_user->driver = user->driver;

    return new_user;

fail:
    free(new_impl->id);
    free(new_impl->tenant);
    free(new_impl->display_name);
    free(new_impl->email);
    free(new_impl->ns);
    free(new_impl);
    free(new_user);
    return NULL;
}

/**
 * @brief 销毁用户
 *
 * 释放用户及其内部结构占用的所有内存。
 */
static void rados_user_destroy(rgw_sal_user_t* user) {
    if (!user) {
        return;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        /* 释放所有字符串字段 */
        free(impl->id);
        free(impl->tenant);
        free(impl->display_name);
        free(impl->email);
        free(impl->ns);

        /* 释放用户信息动态成员 */
        rgw_user_info_free_members(&impl->user_info);

        /* 释放属性映射 */
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
            impl->attrs = NULL;
        }

        free(impl);
    }

    user->impl = NULL;
}

/**
 * @brief 获取用户 ID
 *
 * @param user 用户对象
 *
 * @return 用户 ID 字符串，如果用户为 NULL 则返回 NULL
 */
static const char* rados_user_get_id(const rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->id : NULL;
}

/**
 * @brief 获取用户显示名称
 *
 * @param user 用户对象
 *
 * @return 显示名称字符串，如果用户为 NULL 则返回 NULL
 */
static const char* rados_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->display_name : NULL;
}

/**
 * @brief 设置用户显示名称
 *
 * @param user 用户对象
 * @param name 新的显示名称
 *
 * @return 错误码
 */
static int rados_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user || !name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    char* new_name = strdup(name);
    if (!new_name) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    free(impl->display_name);
    impl->display_name = new_name;

    return RGW_SAL_OK;
}

/**
 * @brief 获取用户租户
 *
 * @param user 用户对象
 *
 * @return 租户字符串，如果用户为 NULL 则返回 NULL
 */
static const char* rados_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->tenant : NULL;
}

/**
 * @brief 获取用户类型
 *
 * @param user 用户对象
 *
 * @return 用户类型 (uint32_t)，如果用户为 NULL 则返回 0
 */
static uint32_t rados_user_get_type(const rgw_sal_user_t* user) {
    if (!user) {
        return 0;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->user_type : 0;
}

/**
 * @brief 获取最大桶数量
 *
 * @param user 用户对象
 *
 * @return 最大桶数量，无限制返回 -1
 */
static int32_t rados_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) {
        return -1;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->max_buckets : -1;
}

/**
 * @brief 设置最大桶数量
 *
 * @param user 用户对象
 * @param max 最大桶数量 (-1 表示无限制)
 */
static void rados_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) {
        return;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        impl->max_buckets = max;
    }
}

/**
 * @brief 获取用户属性映射
 *
 * @param user 用户对象
 *
 * @return 属性映射指针，如果失败则返回 NULL
 */
static rgw_sal_attrs_t* rados_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return NULL;
    }

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }

    return impl->attrs;
}

/**
 * @brief 设置用户属性映射
 *
 * @param user 用户对象
 * @param attrs 新的属性映射
 *
 * @return 错误码
 */
static int rados_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 替换属性映射 */
    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

/**
 * @brief 从存储读取用户属性
 *
 * @param user 用户对象
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 *
 * @return 错误码
 */
static int rados_user_read_attrs(rgw_sal_user_t* user,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_user_pool_ioctx(user->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建 OMAP 键 */
    char oid[RGW_USER_INFO_MAX_ID_LEN * 2 + 2];
    ret = make_user_oid(user, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 获取所有 OMAP 值 */
    uint8_t* val = NULL;
    size_t val_len = 0;
    ret = rgw_omap_get(ioctx, oid, "", &val, &val_len);
    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解码用户信息以获取属性 */
    rgw_user_info_t info;
    ret = rgw_user_info_decode(val, val_len, &info);
    free(val);

    if (ret != 0) {
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 创建属性映射 */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) {
            rgw_user_info_free_members(&info);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
    } else {
        rgw_sal_attrs_destroy(impl->attrs);
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) {
            rgw_user_info_free_members(&info);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
    }

    /* 释放解码后的用户信息动态成员 */
    rgw_user_info_free_members(&info);

    return RGW_SAL_OK;
}

/**
 * @brief 合并并存储用户属性
 *
 * @param user 用户对象
 * @param new_attrs 要合并的新属性
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 *
 * @return 错误码
 */
static int rados_user_merge_and_store_attrs(rgw_sal_user_t* user,
                                           rgw_sal_attrs_t* new_attrs,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!user || !new_attrs) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 确保有属性映射 */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) {
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
    }

    /* 合并新属性到现有属性 */
    for (size_t i = 0; i < new_attrs->count; i++) {
        int ret = rgw_sal_attrs_set(impl->attrs,
                                    new_attrs->pairs[i].key,
                                    new_attrs->pairs[i].value,
                                    new_attrs->pairs[i].value_len);
        if (ret != 0) {
            return ret;
        }
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取用户命名空间
 *
 * @param user 用户对象
 *
 * @return 命名空间字符串，如果用户为 NULL 则返回 NULL
 */
static const char* rados_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) {
        return NULL;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->ns : NULL;
}

/**
 * @brief 设置用户命名空间
 *
 * @param user 用户对象
 * @param ns 命名空间
 *
 * @return 错误码
 */
static int rados_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    free(impl->ns);
    if (ns) {
        impl->ns = strdup(ns);
        if (!impl->ns) {
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
    } else {
        impl->ns = NULL;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 清除用户命名空间
 *
 * @param user 用户对象
 */
static void rados_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) {
        return;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        free(impl->ns);
        impl->ns = NULL;
    }
}

/**
 * @brief 设置用户配额信息
 *
 * @param user 用户对象
 * @param info 配额信息
 *
 * @return 错误码
 */
static int rados_user_set_info(rgw_sal_user_t* user, void* info) {
    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    if (info) {
        memcpy(&impl->quota_info, info, sizeof(rgw_sal_quota_info_t));
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取用户配额信息
 *
 * @param user 用户对象
 * @param info 输出：配额信息指针
 *
 * @return 错误码
 */
static int rados_user_get_info(rgw_sal_user_t* user, void** info) {
    if (!user || !info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *info = &impl->quota_info;
    return RGW_SAL_OK;
}

/**
 * @brief 获取用户权限
 *
 * @param user 用户对象
 * @param caps 输出：权限指针
 *
 * @return 错误码
 */
static int rados_user_get_caps(rgw_sal_user_t* user, void** caps) {
    if (!user || !caps) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *caps = &impl->user_caps;
    return RGW_SAL_OK;
}

/**
 * @brief 获取用户版本跟踪器
 *
 * @param user 用户对象
 * @param tracker 输出：版本跟踪器指针
 *
 * @return 错误码
 */
static int rados_user_get_version_tracker(rgw_sal_user_t* user, void** tracker) {
    if (!user || !tracker) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *tracker = &impl->version_tracker;
    return RGW_SAL_OK;
}

/**
 * @brief 读取用户使用统计
 *
 * @param user 用户对象
 * @param dpp 调试前缀提供者
 * @param start_epoch 开始时间戳
 * @param end_epoch 结束时间戳
 * @param max_entries 最大条目数
 * @param usage 输出：使用统计
 *
 * @return 错误码
 */
static int rados_user_read_usage(rgw_sal_user_t* user,
                                 const rgw_sal_dpp_t* dpp,
                                 uint64_t start_epoch,
                                 uint64_t end_epoch,
                                 uint32_t max_entries,
                                 void* usage) {
    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;
    (void)max_entries;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    if (usage) {
        /* 返回缓存的使用统计 */
        memcpy(usage, &impl->usage, sizeof(rgw_sal_usage_info_t));
    }

    impl->usage_loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 修剪用户使用统计
 *
 * @param user 用户对象
 * @param dpp 调试前缀提供者
 * @param start_epoch 开始时间戳
 * @param end_epoch 结束时间戳
 *
 * @return 错误码
 */
static int rados_user_trim_usage(rgw_sal_user_t* user,
                                 const rgw_sal_dpp_t* dpp,
                                 uint64_t start_epoch,
                                 uint64_t end_epoch) {
    (void)dpp;
    (void)start_epoch;
    (void)end_epoch;

    if (!user) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 简化实现：清零使用统计 */
    memset(&impl->usage, 0, sizeof(rgw_sal_usage_info_t));

    return RGW_SAL_OK;
}

/**
 * @brief 验证 MFA 代码
 *
 * @param user 用户对象
 * @param mfa_serial MFA 设备序列号
 * @param code 验证码
 * @param dpp 调试前缀提供者
 *
 * @return 错误码
 */
static int rados_user_verify_mfa(rgw_sal_user_t* user,
                                const char* mfa_serial,
                                const char* code,
                                const rgw_sal_dpp_t* dpp) {
    (void)user;
    (void)mfa_serial;
    (void)code;
    (void)dpp;

    /* MFA 验证需要访问 MFA 设备存储，当前简化实现返回未实现 */
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

/**
 * @brief 列出用户所属组
 *
 * @param user 用户对象
 * @param dpp 调试前缀提供者
 * @param groups 输出：组列表
 * @param count 输出：组数量
 *
 * @return 错误码
 */
static int rados_user_list_groups(rgw_sal_user_t* user,
                                  const rgw_sal_dpp_t* dpp,
                                  void** groups,
                                  uint32_t* count) {
    (void)dpp;

    if (!user || !groups || !count) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 简化实现：创建空组列表 */
    rgw_sal_user_groups_t* groups_list = rgw_sal_user_groups_create();
    if (!groups_list) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    *groups = groups_list;
    *count = 0;

    return RGW_SAL_OK;
}

/*============================================================================
 * 驱动初始化
 *============================================================================*/

/**
 * @brief 获取用户操作函数表
 *
 * @return 用户操作函数表
 */
rgw_sal_user_vtable_t* rgw_rados_get_user_vtable(void) {
    static rgw_sal_user_vtable_t vtable = {
        .clone              = rados_user_clone,
        .destroy            = rados_user_destroy,
        .get_id             = rados_user_get_id,
        .get_display_name   = rados_user_get_display_name,
        .set_display_name   = rados_user_set_display_name,
        .get_tenant         = rados_user_get_tenant,
        .get_type           = rados_user_get_type,
        .get_max_buckets    = rados_user_get_max_buckets,
        .set_max_buckets    = rados_user_set_max_buckets,
        .get_attrs          = rados_user_get_attrs,
        .set_attrs          = rados_user_set_attrs,
        .load               = rados_user_load,
        .store              = rados_user_store,
        .remove             = rados_user_remove,
        .read_attrs         = rados_user_read_attrs,
        .merge_and_store_attrs = rados_user_merge_and_store_attrs,
        .get_ns             = rados_user_get_ns,
        .set_ns             = rados_user_set_ns,
        .clear_ns           = rados_user_clear_ns,
        .set_info           = rados_user_set_info,
        .get_info           = rados_user_get_info,
        .get_caps           = rados_user_get_caps,
        .get_version_tracker = rados_user_get_version_tracker,
        .read_usage         = rados_user_read_usage,
        .trim_usage         = rados_user_trim_usage,
        .verify_mfa         = rados_user_verify_mfa,
        .list_groups        = rados_user_list_groups,
    };

    return &vtable;
}

/**
 * @brief 获取用户查找函数表
 *
 * @return 用户查找函数表
 */
rgw_sal_user_lookup_vtable_t* rgw_rados_get_user_lookup_vtable(void) {
    static rgw_sal_user_lookup_vtable_t vtable = {
        .get_by_uid       = NULL,  /* TODO */
        .get_by_access_key = rados_user_get_by_access_key,
        .get_by_email     = rados_user_get_by_email,
    };

    return &vtable;
}

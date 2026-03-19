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
 * 驱动初始化
 *============================================================================*/

/**
 * @brief 初始化 RADOS 用户子系统
 *
 * 初始化用户存储所需的资源。
 *
 * @param driver RADOS 驱动
 *
 * @return 执行结果
 */
int rgw_rados_user_init(rgw_sal_driver_t* driver) {
    if (!driver) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 预打开用户池 */
    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                             RGW_RADOS_CTX_POOL_USERS_UID,
                                             &ioctx);
    if (ret != 0) {
        /* 池可能不存在，这是正常的 */
    }

    /* 预打开 email 池 */
    ret = rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_EMAIL,
                                         &ioctx);
    if (ret != 0) {
        /* 池可能不存在，这是正常的 */
    }

    /* 预打开 keys 池 */
    ret = rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_KEYS,
                                         &ioctx);
    if (ret != 0) {
        /* 池可能不存在，这是正常的 */
    }

    return RGW_OK;
}

/**
 * @brief 获取用户操作函数表
 *
 * @return 用户操作函数表
 */
rgw_sal_user_vtable_t* rgw_rados_get_user_vtable(void) {
    static rgw_sal_user_vtable_t vtable = {
        .clone              = NULL,  /* TODO */
        .destroy            = NULL,  /* TODO */
        .get_id             = NULL,  /* TODO */
        .get_display_name   = NULL,  /* TODO */
        .set_display_name   = NULL,  /* TODO */
        .get_tenant         = NULL,  /* TODO */
        .get_type           = NULL,  /* TODO */
        .get_max_buckets    = NULL,  /* TODO */
        .set_max_buckets    = NULL,  /* TODO */
        .get_attrs          = NULL,  /* TODO */
        .set_attrs          = NULL,  /* TODO */
        .load               = rados_user_load,
        .store              = rados_user_store,
        .remove             = rados_user_remove,
        .read_attrs         = NULL,  /* TODO */
        .merge_and_store_attrs = NULL,  /* TODO */
        .get_ns             = NULL,  /* TODO */
        .set_ns             = NULL,  /* TODO */
        .clear_ns           = NULL,  /* TODO */
        .set_info           = NULL,  /* TODO */
        .get_info           = NULL,  /* TODO */
        .get_caps           = NULL,  /* TODO */
        .get_version_tracker = NULL,  /* TODO */
        .read_usage         = NULL,  /* TODO */
        .trim_usage         = NULL,  /* TODO */
        .verify_mfa         = NULL,  /* TODO */
        .list_groups        = NULL,  /* TODO */
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

/**
 * @file rgw_rados_bucket.c
 * @brief RADOS 桶存储实现
 *
 * 实现桶存储的 RADOS 后端功能。
 * 这是 create_bucket 的核心实现。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef __APPLE__
#include <uuid/uuid.h>
#else
#include <uuid.h>
#endif

#include "rgw_sal_rados.h"
#include "rgw_rados_ctx.h"
#include "rgw_omap.h"
#include "rgw_bucket_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 桶实例 OMAP 键前缀 */
#define RGW_BUCKET_INFO_PREFIX  ".bucket.info."

/** 桶入口点 OMAP 键前缀 */
#define RGW_BUCKET_EP_PREFIX   ".bucket."

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 获取桶池的 IO 上下文
 */
static int get_bucket_pool_ioctx(rgw_sal_driver_t* driver, rados_ioctx_t* ioctx) {
    if (!driver || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用用户池作为桶存储池，简化实现 */
    return rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                         RGW_RADOS_CTX_POOL_USERS_UID,
                                         ioctx);
}

/**
 * @brief 构建桶实例 OMAP 键
 *
 * @param bucket_id 桶 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
static int make_bucket_info_oid(const char* bucket_id, char* buf, size_t buf_size) {
    if (!bucket_id || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = snprintf(buf, buf_size, "%s%s", RGW_BUCKET_INFO_PREFIX, bucket_id);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/**
 * @brief 构建桶入口点 OMAP 键
 *
 * @param tenant 租户
 * @param bucket_name 桶名称
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
static int make_bucket_ep_oid(const char* tenant, const char* bucket_name,
                                char* buf, size_t buf_size) {
    if (!bucket_name || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    const char* t = tenant ? tenant : "";
    int ret = snprintf(buf, buf_size, "%s%s:%s", RGW_BUCKET_EP_PREFIX, t, bucket_name);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/**
 * @brief 生成 UUID
 *
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
static int generate_uuid(char* buf, size_t buf_size) {
    if (!buf || buf_size < 37) {
        return RGW_ERR_INVALID_ARG;
    }

    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse(uuid, buf);

    return RGW_OK;
}

/**
 * @brief 获取默认放置规则
 *
 * @param ctx RADOS 上下文
 * @param rule 输出参数，返回放置规则
 *
 * @return 执行结果
 */
static int get_default_placement_rule(rgw_rados_ctx_t* ctx, rgw_placement_rule_t* rule) {
    if (!ctx || !rule) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 简化实现：返回默认规则 */
    rule->name = strdup("default-placement");
    rule->storage_class = NULL;

    return RGW_OK;
}

/**
 * @brief 获取默认桶布局
 *
 * @param ctx RADOS 上下文
 * @param placement_rule 放置规则
 * @param layout 输出参数，返回布局
 *
 * @return 执行结果
 */
static int get_default_bucket_layout(rgw_rados_ctx_t* ctx,
                                     const char* placement_rule,
                                     rgw_bucket_layout_t* layout) {
    (void)placement_rule;

    if (!ctx || !layout) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(layout, 0, sizeof(rgw_bucket_layout_t));

    layout->current_index.type = RGW_BUCKET_INDEX_TYPE_NORMAL;
    layout->current_index.normal.num_shards = RGW_BUCKET_DEFAULT_SHARDS;
    layout->current_index.normal.shard_pool = strdup(RGW_RADOS_CTX_POOL_BUCKETS_INDEX);

    return RGW_OK;
}

/**
 * @brief 创建桶入口点
 *
 * @param info 桶信息
 * @param entry 输出参数，返回入口点
 */
static void create_bucket_entrypoint(const rgw_bucket_info_t* info,
                                     rgw_bucket_entrypoint_t* entry) {
    if (!info || !entry) {
        return;
    }

    memset(entry, 0, sizeof(rgw_bucket_entrypoint_t));

    entry->bucket.name = info->bucket.name ? strdup(info->bucket.name) : NULL;
    entry->bucket.tenant = info->bucket.tenant ? strdup(info->bucket.tenant) : NULL;
    entry->bucket.marker = info->bucket.marker ? strdup(info->bucket.marker) : NULL;
    entry->bucket.bucket_id = info->bucket.bucket_id ? strdup(info->bucket.bucket_id) : NULL;

    entry->owner = info->owner;
    entry->creation_time = info->creation_time;
    entry->linked = true;
    entry->has_bucket_info = true;
}

/*============================================================================
 * 桶操作实现
 *============================================================================*/

/**
 * @brief 创建桶
 *
 * 对标 C++: RadosBucket::create -> RGWRados::create_bucket
 *
 * 完整流程：
 * 1. 生成 bucket_id 和 marker
 * 2. 构建 RGWBucketInfo
 * 3. 初始化桶布局
 * 4. 写入桶实例信息 (OMAP)
 * 5. 写入桶入口点 (OMAP)
 */
static int rados_bucket_create(rgw_sal_bucket_t* bucket,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y,
                               bool create_obj) {
    (void)dpp;
    (void)y;
    (void)create_obj;

    if (!bucket) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)bucket->driver->impl;
    if (!driver_impl || !driver_impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 1. 生成 bucket_id 和 marker */
    char bucket_id[128];
    char marker[128];

    /* 使用 UUID 生成器生成唯一的 marker 和 bucket_id */
    int ret = generate_uuid(marker, sizeof(marker));
    if (ret != 0) {
        return ret;
    }

    strncpy(bucket_id, marker, sizeof(bucket_id) - 1);
    bucket_id[sizeof(bucket_id) - 1] = '\0';

    /* 保存到桶实现结构 */
    free(impl->bucket_id);
    impl->bucket_id = strdup(bucket_id);
    free(impl->marker);
    impl->marker = strdup(marker);

    /* 2. 构建 RGWBucketInfo */
    rgw_bucket_info_t info;
    rgw_bucket_info_init(&info);

    info.bucket.name = impl->name ? strdup(impl->name) : NULL;
    info.bucket.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
    info.bucket.marker = strdup(marker);
    info.bucket.bucket_id = strdup(bucket_id);

    /* 所有者信息从 impl->owner_id 获取 */
    info.owner.type = 0;  /* user */
    info.owner.user_id = impl->owner_id ? strdup(impl->owner_id) : NULL;

    /* 获取 zonegroup_id */
    const char* zonegroup = rgw_rados_ctx_get_zonegroup_id(driver_impl->rados_ctx);
    if (zonegroup) {
        info.zonegroup = strdup(zonegroup);
    }

    /* 获取放置规则 */
    rgw_placement_rule_t placement_rule;
    ret = get_default_placement_rule(driver_impl->rados_ctx, &placement_rule);
    if (ret != 0) {
        rgw_bucket_info_free_members(&info);
        return ret;
    }
    info.placement_rule.name = placement_rule.name;
    info.placement_rule.storage_class = placement_rule.storage_class;

    /* 设置创建时间 */
    info.creation_time = time(NULL);

    /* 3. 初始化桶布局 */
    ret = get_default_bucket_layout(driver_impl->rados_ctx,
                                    placement_rule.name,
                                    &info.layout);
    if (ret != 0) {
        rgw_bucket_info_free_members(&info);
        return ret;
    }

    /* 4. 编码桶信息 */
    uint8_t* info_buf = NULL;
    size_t info_buf_len = 0;
    ret = rgw_bucket_info_encode_alloc(&info, &info_buf, &info_buf_len);
    rgw_bucket_info_free_members(&info);

    if (ret != 0 || !info_buf) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 5. 获取 IO 上下文并写入 */
    rados_ioctx_t ioctx;
    ret = get_bucket_pool_ioctx(bucket->driver, &ioctx);
    if (ret != 0) {
        free(info_buf);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建 OMAP 键 */
    char info_oid[256];
    ret = make_bucket_info_oid(bucket_id, info_oid, sizeof(info_oid));
    if (ret != 0) {
        free(info_buf);
        return ret;
    }

    /* 写入桶实例信息 */
    ret = rgw_omap_set(ioctx, info_oid, "", info_buf, info_buf_len, false);
    free(info_buf);

    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 6. 写入桶入口点 */
    rgw_bucket_entrypoint_t entry;
    create_bucket_entrypoint(&info, &entry);

    /* 重新构建 info 以便创建 entrypoint */
    /* 注意：info 已经被 free_members 释放，这里重新创建 */
    rgw_bucket_info_t info2;
    rgw_bucket_info_init(&info2);
    info2.bucket.name = impl->name ? strdup(impl->name) : NULL;
    info2.bucket.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
    info2.bucket.marker = strdup(marker);
    info2.bucket.bucket_id = strdup(bucket_id);
    info2.owner.type = 0;
    info2.owner.user_id = impl->owner_id ? strdup(impl->owner_id) : NULL;
    info2.creation_time = info.creation_time;

    create_bucket_entrypoint(&info2, &entry);
    rgw_bucket_info_free_members(&info2);

    uint8_t* entry_buf = NULL;
    size_t entry_buf_len = rgw_bucket_entrypoint_encode(&entry, NULL, 0);
    if (entry_buf_len > 0) {
        entry_buf = (uint8_t*)malloc(entry_buf_len);
        if (entry_buf) {
            rgw_bucket_entrypoint_encode(&entry, entry_buf, entry_buf_len);

            char ep_oid[256];
            ret = make_bucket_ep_oid(impl->tenant, impl->name, ep_oid, sizeof(ep_oid));
            if (ret == 0) {
                ret = rgw_omap_set(ioctx, ep_oid, "", entry_buf, entry_buf_len, false);
            }

            free(entry_buf);
        }
    }

    rgw_bucket_entrypoint_free_members(&entry);

    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    impl->created = true;

    return RGW_SAL_OK;
}

/**
 * @brief 加载桶
 *
 * 从 RADOS 存储中加载桶信息。
 */
static int rados_bucket_load(rgw_sal_bucket_t* bucket,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!bucket) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_bucket_pool_ioctx(bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建入口点 OMAP 键 */
    char ep_oid[256];
    ret = make_bucket_ep_oid(impl->tenant, impl->name, ep_oid, sizeof(ep_oid));
    if (ret != 0) {
        return ret;
    }

    /* 读取入口点 */
    uint8_t* val = NULL;
    size_t val_len = 0;
    ret = rgw_omap_get(ioctx, ep_oid, "", &val, &val_len);
    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解码入口点 */
    rgw_bucket_entrypoint_t entry;
    ret = rgw_bucket_entrypoint_decode(val, val_len, &entry);
    free(val);

    if (ret != 0) {
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 构建实例 OMAP 键 */
    char info_oid[256];
    if (entry.bucket.bucket_id) {
        ret = make_bucket_info_oid(entry.bucket.bucket_id, info_oid, sizeof(info_oid));
    } else if (entry.bucket.marker) {
        ret = make_bucket_info_oid(entry.bucket.marker, info_oid, sizeof(info_oid));
    } else {
        rgw_bucket_entrypoint_free_members(&entry);
        return RGW_SAL_ERR_INVALID_ARG;
    }

    if (ret != 0) {
        rgw_bucket_entrypoint_free_members(&entry);
        return ret;
    }

    /* 读取桶信息 */
    val = NULL;
    val_len = 0;
    ret = rgw_omap_get(ioctx, info_oid, "", &val, &val_len);
    if (ret != 0) {
        rgw_bucket_entrypoint_free_members(&entry);
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 解码桶信息 */
    rgw_bucket_info_t info;
    ret = rgw_bucket_info_decode(val, val_len, &info);
    free(val);

    if (ret != 0) {
        rgw_bucket_entrypoint_free_members(&entry);
        return RGW_SAL_ERR_PARSE_ERROR;
    }

    /* 填充桶实现结构 */
    free(impl->name);
    impl->name = info.bucket.name ? strdup(info.bucket.name) : NULL;
    free(impl->tenant);
    impl->tenant = info.bucket.tenant ? strdup(info.bucket.tenant) : NULL;
    free(impl->marker);
    impl->marker = info.bucket.marker ? strdup(info.bucket.marker) : NULL;
    free(impl->bucket_id);
    impl->bucket_id = info.bucket.bucket_id ? strdup(info.bucket.bucket_id) : NULL;

    impl->mtime = info.creation_time;

    impl->loaded = true;

    rgw_bucket_info_free_members(&info);
    rgw_bucket_entrypoint_free_members(&entry);

    return RGW_SAL_OK;
}

/**
 * @brief 删除桶
 *
 * 从 RADOS 存储中删除桶信息。
 */
static int rados_bucket_delete(rgw_sal_bucket_t* bucket,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y,
                                bool delete_objects) {
    (void)dpp;
    (void)y;
    (void)delete_objects;

    if (!bucket) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_bucket_pool_ioctx(bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建入口点 OMAP 键 */
    char ep_oid[256];
    ret = make_bucket_ep_oid(impl->tenant, impl->name, ep_oid, sizeof(ep_oid));
    if (ret != 0) {
        return ret;
    }

    /* 删除入口点 */
    ret = rgw_omap_clear(ioctx, ep_oid);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 如果有 bucket_id，删除实例信息 */
    if (impl->bucket_id) {
        char info_oid[256];
        ret = make_bucket_info_oid(impl->bucket_id, info_oid, sizeof(info_oid));
        if (ret == 0) {
            rgw_omap_clear(ioctx, info_oid);
        }
    }

    impl->deleted = true;

    return RGW_SAL_OK;
}

/**
 * @brief 获取桶操作函数表
 */
rgw_sal_bucket_vtable_t* rgw_rados_get_bucket_vtable(void) {
    static rgw_sal_bucket_vtable_t vtable = {
        .clone            = NULL,  /* TODO */
        .destroy          = NULL,  /* TODO */
        .get_name         = NULL,  /* TODO */
        .get_tenant       = NULL,  /* TODO */
        .get_marker       = NULL,  /* TODO */
        .get_info         = NULL,  /* TODO */
        .get_owner        = NULL,  /* TODO */
        .get_attrs        = NULL,  /* TODO */
        .set_attrs        = NULL,  /* TODO */
        .list             = NULL,  /* TODO */
        .load             = rados_bucket_load,
        .store            = NULL,  /* TODO */
        .remove           = NULL,  /* TODO */
        .create           = rados_bucket_create,
        .delete_bucket    = rados_bucket_delete,
        .rename           = NULL,  /* TODO */
        .set_acl          = NULL,  /* TODO */
        .get_policy       = NULL,  /* TODO */
        .set_policy       = NULL,  /* TODO */
        .get_tag          = NULL,  /* TODO */
        .set_tag          = NULL,  /* TODO */
        .get_usage        = NULL,  /* TODO */
        .read_stats       = NULL,  /* TODO */
        .read_stats_async = NULL,  /* TODO */
        .set_quota        = NULL,  /* TODO */
        .get_instance_info = NULL,  /* TODO */
        .update_instance_info = NULL,  /* TODO */
        .remove_instance = NULL,  /* TODO */
        .get_filepath     = NULL,  /* TODO */
        .get_obj_instance = NULL,  /* TODO */
        .check_empty      = NULL,  /* TODO */
        .check_index      = NULL,  /* TODO */
        .rebuild_index    = NULL,  /* TODO */
        .put_info         = NULL,  /* TODO */
        .try_refresh_info = NULL,  /* TODO */
        .read_placement   = NULL,  /* TODO */
        .update_placement = NULL,  /* TODO */
        .delete_placement = NULL,  /* TODO */
        .get_tags         = NULL,  /* TODO */
        .set_tags         = NULL,  /* TODO */
        .get_config       = NULL,  /* TODO */
        .set_config       = NULL,  /* TODO */
        .get_sync_policy  = NULL,  /* TODO */
        .set_sync_policy  = NULL,  /* TODO */
        .get_lc           = NULL,  /* TODO */
        .set_lc           = NULL,  /* TODO */
        .get_request_lock  = NULL,  /* TODO */
        .put_request_lock  = NULL,  /* TODO */
        .finish_request_lock = NULL,  /* TODO */
    };

    return &vtable;
}

/**
 * @brief 初始化 RADOS 桶子系统
 *
 * @param driver RADOS 驱动
 *
 * @return 执行结果
 */
int rgw_rados_bucket_init(rgw_sal_driver_t* driver) {
    if (!driver) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 预打开桶池 */
    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                           RGW_RADOS_CTX_POOL_USERS_UID,
                                           &ioctx);
    if (ret != 0) {
        /* 池可能不存在，这是正常的 */
    }

    return RGW_OK;
}

/**
 * @file rgw_bucket_serde.c
 * @brief 桶信息序列化/反序列化实现
 *
 * 实现桶信息的二进制序列化，用于存储到 RADOS OMAP。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgw_bucket_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 桶实例 OMAP 键前缀 */
#define RGW_BUCKET_INFO_OMAP_PREFIX  ".bucket.info."

/** 桶入口点 OMAP 键前缀 */
#define RGW_BUCKET_EP_OMAP_PREFIX    ".bucket."

/** 用户桶列表 OMAP 键后缀 */
#define RGW_USER_BUCKETS_SUFFIX      ":buckets"

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 分配并复制字符串
 */
static char* str_dup(const char* str) {
    if (!str) {
        return NULL;
    }
    return strdup(str);
}

/**
 * @brief 释放字符串指针（如果不为 NULL）
 */
static void free_str(char** str) {
    if (str && *str) {
        free(*str);
        *str = NULL;
    }
}

/**
 * @brief 初始化 bucket_id
 */
static void init_bucket_id(rgw_bucket_id_t* id) {
    if (!id) {
        return;
    }
    memset(id, 0, sizeof(rgw_bucket_id_t));
}

/**
 * @brief 释放 bucket_id
 */
static void free_bucket_id(rgw_bucket_id_t* id) {
    if (!id) {
        return;
    }
    free_str(&id->tenant);
    free_str(&id->name);
    free_str(&id->marker);
    free_str(&id->bucket_id);
    free_str(&id->placement_rule);
}

/**
 * @brief 初始化 owner
 */
static void init_owner(rgw_owner_t* owner) {
    if (!owner) {
        return;
    }
    memset(owner, 0, sizeof(rgw_owner_t));
}

/**
 * @brief 释放 owner
 */
static void free_owner(rgw_owner_t* owner) {
    if (!owner) {
        return;
    }
    if (owner->type == 0) {
        free_str(&owner->user_id);
    } else {
        free_str(&owner->account_id);
    }
}

/**
 * @brief 初始化 placement_rule
 */
static void init_placement_rule(rgw_placement_rule_t* rule) {
    if (!rule) {
        return;
    }
    memset(rule, 0, sizeof(rgw_placement_rule_t));
}

/**
 * @brief 释放 placement_rule
 */
static void free_placement_rule(rgw_placement_rule_t* rule) {
    if (!rule) {
        return;
    }
    free_str(&rule->name);
    free_str(&rule->storage_class);
}

/**
 * @brief 初始化 bucket_index_shard_layout
 */
static void init_index_shard_layout(rgw_bucket_index_shard_layout_t* layout) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(rgw_bucket_index_shard_layout_t));
}

/**
 * @brief 释放 bucket_index_shard_layout
 */
static void free_index_shard_layout(rgw_bucket_index_shard_layout_t* layout) {
    if (!layout) {
        return;
    }
    free_str(&layout->shard_pool);
    free_str(&layout->object_prefix);
}

/**
 * @brief 初始化 bucket_index_layout_gen
 */
static void init_index_layout_gen(rgw_bucket_index_layout_gen_t* gen) {
    if (!gen) {
        return;
    }
    memset(gen, 0, sizeof(rgw_bucket_index_layout_gen_t));
}

/**
 * @brief 释放 bucket_index_layout_gen
 */
static void free_index_layout_gen(rgw_bucket_index_layout_gen_t* gen) {
    if (!gen) {
        return;
    }
    free_str(&gen->gen_id);
    free_index_shard_layout(&gen->layout);
}

/**
 * @brief 初始化 bucket_index_layout
 */
static void init_index_layout(rgw_bucket_index_layout_t* layout) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(rgw_bucket_index_layout_t));
    layout->type = RGW_BUCKET_INDEX_TYPE_NORMAL;
}

/**
 * @brief 释放 bucket_index_layout
 */
static void free_index_layout(rgw_bucket_index_layout_t* layout) {
    if (!layout) {
        return;
    }
    free_index_shard_layout(&layout->normal);
    free_index_layout_gen(&layout->log);
    free_index_layout_gen(&layout->ulog);
}

/**
 * @brief 初始化 bucket_layout
 */
static void init_bucket_layout(rgw_bucket_layout_t* layout) {
    if (!layout) {
        return;
    }
    memset(layout, 0, sizeof(rgw_bucket_layout_t));
    init_index_layout(&layout->current_index);
}

/**
 * @brief 释放 bucket_layout
 */
static void free_bucket_layout(rgw_bucket_layout_t* layout) {
    if (!layout) {
        return;
    }
    free_index_layout(&layout->current_index);
    free_index_layout(&layout->target_index);
}

/**
 * @brief 初始化 website_conf
 */
static void init_website_conf(rgw_bucket_website_conf_t* conf) {
    if (!conf) {
        return;
    }
    memset(conf, 0, sizeof(rgw_bucket_website_conf_t));
}

/**
 * @brief 释放 website_conf
 */
static void free_website_conf(rgw_bucket_website_conf_t* conf) {
    if (!conf) {
        return;
    }
    free_str(&conf->index_suffix);
    free_str(&conf->error_suffix);
    free_str(&conf->redirect_url);
}

/*============================================================================
 * 函数实现 - 初始化和销毁
 *============================================================================*/

/**
 * @brief 初始化桶信息
 */
void rgw_bucket_info_init(rgw_bucket_info_t* info) {
    if (!info) {
        return;
    }

    memset(info, 0, sizeof(rgw_bucket_info_t));

    init_bucket_id(&info->bucket);
    init_owner(&info->owner);
    init_placement_rule(&info->placement_rule);
    init_bucket_layout(&info->layout);
    init_website_conf(&info->website_conf);

    /* 设置默认值 */
    info->flags = RGW_BUCKET_FLAG_NONE;
    info->quota.max_size = -1;
    info->quota.max_objects = -1;
    info->quota.enabled = false;
    info->layout.current_index.normal.num_shards = RGW_BUCKET_DEFAULT_SHARDS;
}

/**
 * @brief 释放桶信息动态内存
 */
void rgw_bucket_info_free_members(rgw_bucket_info_t* info) {
    if (!info) {
        return;
    }

    free_bucket_id(&info->bucket);
    free_owner(&info->owner);
    free_str(&info->zonegroup);
    free_placement_rule(&info->placement_rule);
    free_bucket_layout(&info->layout);
    free_str(&info->swift_ver_location);
    free_website_conf(&info->website_conf);
    free_str(&info->new_bucket_instance_id);
}

/**
 * @brief 销毁桶信息
 */
void rgw_bucket_info_destroy(rgw_bucket_info_t* info) {
    if (!info) {
        return;
    }

    rgw_bucket_info_free_members(info);
    free(info);
}

/*============================================================================
 * 函数实现 - 序列化
 *============================================================================*/

/**
 * @brief 计算编码后的大小
 */
size_t rgw_bucket_info_calc_encode_size(const rgw_bucket_info_t* info) {
    if (!info) {
        return 0;
    }

    size_t size = 0;

    /* 版本号 */
    size += sizeof(uint32_t) * 2;

    /* bucket.tenant */
    size += sizeof(uint32_t);
    if (info->bucket.tenant) {
        size += strlen(info->bucket.tenant);
    }

    /* bucket.name */
    size += sizeof(uint32_t);
    if (info->bucket.name) {
        size += strlen(info->bucket.name);
    }

    /* bucket.marker */
    size += sizeof(uint32_t);
    if (info->bucket.marker) {
        size += strlen(info->bucket.marker);
    }

    /* bucket.bucket_id */
    size += sizeof(uint32_t);
    if (info->bucket.bucket_id) {
        size += strlen(info->bucket.bucket_id);
    }

    /* bucket.placement_rule */
    size += sizeof(uint32_t);
    if (info->bucket.placement_rule) {
        size += strlen(info->bucket.placement_rule);
    }

    /* bucket.proj_ver */
    size += sizeof(uint32_t);

    /* owner */
    size += sizeof(uint32_t);  /* type */
    size += sizeof(uint32_t);  /* id len */
    if (info->owner.user_id) {
        size += strlen(info->owner.user_id);
    }

    /* 基本字段 */
    size += sizeof(uint32_t);  /* flags */
    size += sizeof(uint32_t);  /* zonegroup len */
    if (info->zonegroup) {
        size += strlen(info->zonegroup);
    }
    size += sizeof(int64_t);  /* creation_time */

    /* placement_rule */
    size += sizeof(uint32_t);
    if (info->placement_rule.name) {
        size += strlen(info->placement_rule.name);
    }
    size += sizeof(uint32_t);
    if (info->placement_rule.storage_class) {
        size += strlen(info->placement_rule.storage_class);
    }

    /* has_instance_obj */
    size += sizeof(bool);

    /* quota */
    size += sizeof(int64_t);  /* max_size */
    size += sizeof(int64_t);  /* max_objects */
    size += sizeof(bool);  /* enabled */

    /* layout */
    size += sizeof(rgw_bucket_index_type_t);
    size += sizeof(uint32_t);  /* num_shards */
    size += sizeof(uint32_t);  /* shard_pool_id */
    size += sizeof(uint32_t);
    if (info->layout.current_index.normal.shard_pool) {
        size += strlen(info->layout.current_index.normal.shard_pool);
    }
    size += sizeof(uint32_t);
    if (info->layout.current_index.normal.object_prefix) {
        size += strlen(info->layout.current_index.normal.object_prefix);
    }

    /* requester_pays */
    size += sizeof(bool);

    /* has_website */
    size += sizeof(bool);

    /* website_conf */
    size += sizeof(uint32_t);
    if (info->website_conf.index_suffix) {
        size += strlen(info->website_conf.index_suffix);
    }
    size += sizeof(uint32_t);
    if (info->website_conf.error_suffix) {
        size += strlen(info->website_conf.error_suffix);
    }
    size += sizeof(uint32_t);
    if (info->website_conf.redirect_url) {
        size += strlen(info->website_conf.redirect_url);
    }

    /* swift_versioning */
    size += sizeof(bool);
    size += sizeof(uint32_t);
    if (info->swift_ver_location) {
        size += strlen(info->swift_ver_location);
    }

    /* reshard_info */
    size += sizeof(rgw_bucket_reshard_status_t);
    size += sizeof(uint32_t);
    size += sizeof(uint64_t);

    /* new_bucket_instance_id */
    size += sizeof(uint32_t);
    if (info->new_bucket_instance_id) {
        size += strlen(info->new_bucket_instance_id);
    }

    /* obj_lock */
    size += sizeof(bool);
    size += sizeof(int32_t);
    size += sizeof(int32_t);

    return size;
}

/**
 * @brief 编码桶信息到缓冲区
 */
int rgw_bucket_info_encode(const rgw_bucket_info_t* info,
                           uint8_t* buf,
                           size_t buf_size) {
    if (!info || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t required_size = rgw_bucket_info_calc_encode_size(info);
    if (buf_size < required_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    uint8_t* p = buf;

    /* 写入版本号 */
    uint32_t ver_start = RGW_BUCKET_INFO_ENCODE_VERSION_START;
    uint32_t ver_end = RGW_BUCKET_INFO_ENCODE_VERSION_END;

    memcpy(p, &ver_start, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &ver_end, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* bucket.tenant */
    uint32_t len = info->bucket.tenant ? strlen(info->bucket.tenant) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->bucket.tenant, len);
        p += len;
    }

    /* bucket.name */
    len = info->bucket.name ? strlen(info->bucket.name) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->bucket.name, len);
        p += len;
    }

    /* bucket.marker */
    len = info->bucket.marker ? strlen(info->bucket.marker) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->bucket.marker, len);
        p += len;
    }

    /* bucket.bucket_id */
    len = info->bucket.bucket_id ? strlen(info->bucket.bucket_id) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->bucket.bucket_id, len);
        p += len;
    }

    /* bucket.placement_rule */
    len = info->bucket.placement_rule ? strlen(info->bucket.placement_rule) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->bucket.placement_rule, len);
        p += len;
    }

    /* bucket.proj_ver */
    memcpy(p, &info->bucket.proj_ver, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* owner */
    memcpy(p, &info->owner.type, sizeof(uint32_t));
    p += sizeof(uint32_t);
    len = info->owner.user_id ? strlen(info->owner.user_id) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->owner.user_id, len);
        p += len;
    }

    /* flags */
    memcpy(p, &info->flags, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* zonegroup */
    len = info->zonegroup ? strlen(info->zonegroup) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->zonegroup, len);
        p += len;
    }

    /* creation_time */
    memcpy(p, &info->creation_time, sizeof(int64_t));
    p += sizeof(int64_t);

    /* placement_rule */
    len = info->placement_rule.name ? strlen(info->placement_rule.name) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->placement_rule.name, len);
        p += len;
    }
    len = info->placement_rule.storage_class ? strlen(info->placement_rule.storage_class) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->placement_rule.storage_class, len);
        p += len;
    }

    /* has_instance_obj */
    memcpy(p, &info->has_instance_obj, sizeof(bool));
    p += sizeof(bool);

    /* quota */
    memcpy(p, &info->quota.max_size, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(p, &info->quota.max_objects, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(p, &info->quota.enabled, sizeof(bool));
    p += sizeof(bool);

    /* layout */
    memcpy(p, &info->layout.current_index.type, sizeof(rgw_bucket_index_type_t));
    p += sizeof(rgw_bucket_index_type_t);
    memcpy(p, &info->layout.current_index.normal.num_shards, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &info->layout.current_index.normal.shard_pool_id, sizeof(uint32_t));
    p += sizeof(uint32_t);
    len = info->layout.current_index.normal.shard_pool ?
          strlen(info->layout.current_index.normal.shard_pool) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->layout.current_index.normal.shard_pool, len);
        p += len;
    }
    len = info->layout.current_index.normal.object_prefix ?
          strlen(info->layout.current_index.normal.object_prefix) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->layout.current_index.normal.object_prefix, len);
        p += len;
    }

    /* requester_pays */
    memcpy(p, &info->requester_pays, sizeof(bool));
    p += sizeof(bool);

    /* has_website */
    memcpy(p, &info->has_website, sizeof(bool));
    p += sizeof(bool);

    /* website_conf */
    len = info->website_conf.index_suffix ? strlen(info->website_conf.index_suffix) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->website_conf.index_suffix, len);
        p += len;
    }
    len = info->website_conf.error_suffix ? strlen(info->website_conf.error_suffix) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->website_conf.error_suffix, len);
        p += len;
    }
    len = info->website_conf.redirect_url ? strlen(info->website_conf.redirect_url) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->website_conf.redirect_url, len);
        p += len;
    }

    /* swift_versioning */
    memcpy(p, &info->swift_versioning, sizeof(bool));
    p += sizeof(bool);
    len = info->swift_ver_location ? strlen(info->swift_ver_location) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->swift_ver_location, len);
        p += len;
    }

    /* reshard_info */
    memcpy(p, &info->reshard_info.status, sizeof(rgw_bucket_reshard_status_t));
    p += sizeof(rgw_bucket_reshard_status_t);
    memcpy(p, &info->reshard_info.num_shards, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &info->reshard_info.new_bucket_instance_id, sizeof(uint64_t));
    p += sizeof(uint64_t);

    /* new_bucket_instance_id */
    len = info->new_bucket_instance_id ? strlen(info->new_bucket_instance_id) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, info->new_bucket_instance_id, len);
        p += len;
    }

    /* obj_lock */
    memcpy(p, &info->obj_lock.enabled, sizeof(bool));
    p += sizeof(bool);
    memcpy(p, &info->obj_lock.mode, sizeof(int32_t));
    p += sizeof(int32_t);
    memcpy(p, &info->obj_lock.retain_days, sizeof(int32_t));
    p += sizeof(int32_t);

    return (int)(p - buf);
}

/**
 * @brief 动态编码桶信息
 */
int rgw_bucket_info_encode_alloc(const rgw_bucket_info_t* info,
                                  uint8_t** out_buf,
                                  size_t* out_len) {
    if (!info || !out_buf || !out_len) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t buf_size = rgw_bucket_info_calc_encode_size(info);
    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    int ret = rgw_bucket_info_encode(info, buf, buf_size);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_len = (size_t)ret;

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 反序列化
 *============================================================================*/

/**
 * @brief 从缓冲区解码桶信息
 */
int rgw_bucket_info_decode(const uint8_t* buf,
                            size_t buf_len,
                            rgw_bucket_info_t* info) {
    if (!buf || !info) {
        return RGW_ERR_INVALID_ARG;
    }

    rgw_bucket_info_init(info);

    const uint8_t* p = buf;
    size_t remaining = buf_len;

    /* 读取版本号 */
    if (remaining < sizeof(uint32_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t ver_start, ver_end;
    memcpy(&ver_start, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&ver_end, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t) * 2;

    /* bucket.tenant */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    uint32_t len;
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->bucket.tenant = (char*)malloc(len + 1);
        if (!info->bucket.tenant) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->bucket.tenant, p, len);
        info->bucket.tenant[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.name */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->bucket.name = (char*)malloc(len + 1);
        if (!info->bucket.name) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->bucket.name, p, len);
        info->bucket.name[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.marker */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->bucket.marker = (char*)malloc(len + 1);
        if (!info->bucket.marker) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->bucket.marker, p, len);
        info->bucket.marker[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.bucket_id */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->bucket.bucket_id = (char*)malloc(len + 1);
        if (!info->bucket.bucket_id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->bucket.bucket_id, p, len);
        info->bucket.bucket_id[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.placement_rule */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->bucket.placement_rule = (char*)malloc(len + 1);
        if (!info->bucket.placement_rule) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->bucket.placement_rule, p, len);
        info->bucket.placement_rule[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.proj_ver */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->bucket.proj_ver, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* owner */
    if (remaining < sizeof(uint32_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->owner.type, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t) * 2;
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        if (info->owner.type == 0) {
            info->owner.user_id = (char*)malloc(len + 1);
            if (!info->owner.user_id) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
            memcpy(info->owner.user_id, p, len);
            info->owner.user_id[len] = '\0';
        }
        p += len;
        remaining -= len;
    }

    /* flags */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->flags, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* zonegroup */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_ZONEGROUP_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->zonegroup = (char*)malloc(len + 1);
        if (!info->zonegroup) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->zonegroup, p, len);
        info->zonegroup[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* creation_time */
    if (remaining < sizeof(int64_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->creation_time, p, sizeof(int64_t));
    p += sizeof(int64_t);
    remaining -= sizeof(int64_t);

    /* placement_rule */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->placement_rule.name = (char*)malloc(len + 1);
        if (!info->placement_rule.name) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->placement_rule.name, p, len);
        info->placement_rule.name[len] = '\0';
        p += len;
        remaining -= len;
    }
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->placement_rule.storage_class = (char*)malloc(len + 1);
        if (!info->placement_rule.storage_class) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->placement_rule.storage_class, p, len);
        info->placement_rule.storage_class[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* has_instance_obj */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->has_instance_obj, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* quota */
    if (remaining < sizeof(int64_t) * 2 + sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->quota.max_size, p, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(&info->quota.max_objects, p, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(&info->quota.enabled, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(int64_t) * 2 + sizeof(bool);

    /* layout */
    if (remaining < sizeof(rgw_bucket_index_type_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->layout.current_index.type, p, sizeof(rgw_bucket_index_type_t));
    p += sizeof(rgw_bucket_index_type_t);
    remaining -= sizeof(rgw_bucket_index_type_t);

    if (remaining < sizeof(uint32_t) * 3) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->layout.current_index.normal.num_shards, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&info->layout.current_index.normal.shard_pool_id, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t) * 3;
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->layout.current_index.normal.shard_pool = (char*)malloc(len + 1);
        if (!info->layout.current_index.normal.shard_pool) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->layout.current_index.normal.shard_pool, p, len);
        info->layout.current_index.normal.shard_pool[len] = '\0';
        p += len;
        remaining -= len;
    }
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->layout.current_index.normal.object_prefix = (char*)malloc(len + 1);
        if (!info->layout.current_index.normal.object_prefix) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->layout.current_index.normal.object_prefix, p, len);
        info->layout.current_index.normal.object_prefix[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* requester_pays */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->requester_pays, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* has_website */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->has_website, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* website_conf */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->website_conf.index_suffix = (char*)malloc(len + 1);
        if (!info->website_conf.index_suffix) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->website_conf.index_suffix, p, len);
        info->website_conf.index_suffix[len] = '\0';
        p += len;
        remaining -= len;
    }
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->website_conf.error_suffix = (char*)malloc(len + 1);
        if (!info->website_conf.error_suffix) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->website_conf.error_suffix, p, len);
        info->website_conf.error_suffix[len] = '\0';
        p += len;
        remaining -= len;
    }
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->website_conf.redirect_url = (char*)malloc(len + 1);
        if (!info->website_conf.redirect_url) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->website_conf.redirect_url, p, len);
        info->website_conf.redirect_url[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* swift_versioning */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->swift_versioning, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->swift_ver_location = (char*)malloc(len + 1);
        if (!info->swift_ver_location) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->swift_ver_location, p, len);
        info->swift_ver_location[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* reshard_info */
    if (remaining < sizeof(rgw_bucket_reshard_status_t) + sizeof(uint32_t) + sizeof(uint64_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->reshard_info.status, p, sizeof(rgw_bucket_reshard_status_t));
    p += sizeof(rgw_bucket_reshard_status_t);
    memcpy(&info->reshard_info.num_shards, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&info->reshard_info.new_bucket_instance_id, p, sizeof(uint64_t));
    p += sizeof(uint64_t);
    remaining -= sizeof(rgw_bucket_reshard_status_t) + sizeof(uint32_t) + sizeof(uint64_t);

    /* new_bucket_instance_id */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > remaining || len >= RGW_BUCKET_MAX_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }
    if (len > 0) {
        info->new_bucket_instance_id = (char*)malloc(len + 1);
        if (!info->new_bucket_instance_id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->new_bucket_instance_id, p, len);
        info->new_bucket_instance_id[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* obj_lock */
    if (remaining < sizeof(bool) + sizeof(int32_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->obj_lock.enabled, p, sizeof(bool));
    p += sizeof(bool);
    memcpy(&info->obj_lock.mode, p, sizeof(int32_t));
    p += sizeof(int32_t);
    memcpy(&info->obj_lock.retain_days, p, sizeof(int32_t));
    p += sizeof(int32_t);

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 桶入口点
 *============================================================================*/

/**
 * @brief 初始化桶入口点
 */
void rgw_bucket_entrypoint_init(rgw_bucket_entrypoint_t* entry) {
    if (!entry) {
        return;
    }

    memset(entry, 0, sizeof(rgw_bucket_entrypoint_t));
    init_bucket_id(&entry->bucket);
    init_owner(&entry->owner);
}

/**
 * @brief 释放桶入口点动态内存
 */
void rgw_bucket_entrypoint_free_members(rgw_bucket_entrypoint_t* entry) {
    if (!entry) {
        return;
    }

    free_bucket_id(&entry->bucket);
    free_owner(&entry->owner);
    free_str(&entry->inline_info);
}

/**
 * @brief 销毁桶入口点
 */
void rgw_bucket_entrypoint_destroy(rgw_bucket_entrypoint_t* entry) {
    if (!entry) {
        return;
    }

    rgw_bucket_entrypoint_free_members(entry);
    free(entry);
}

/**
 * @brief 编码桶入口点到缓冲区
 */
int rgw_bucket_entrypoint_encode(const rgw_bucket_entrypoint_t* entry,
                                  uint8_t* buf,
                                  size_t buf_size) {
    if (!entry || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    uint8_t* p = buf;

    /* version */
    uint32_t version = RGW_BUCKET_ENTRYPOINT_ENCODE_VERSION;
    memcpy(p, &version, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* bucket.tenant */
    uint32_t len = entry->bucket.tenant ? strlen(entry->bucket.tenant) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, entry->bucket.tenant, len);
        p += len;
    }

    /* bucket.name */
    len = entry->bucket.name ? strlen(entry->bucket.name) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, entry->bucket.name, len);
        p += len;
    }

    /* bucket.marker */
    len = entry->bucket.marker ? strlen(entry->bucket.marker) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, entry->bucket.marker, len);
        p += len;
    }

    /* bucket.bucket_id */
    len = entry->bucket.bucket_id ? strlen(entry->bucket.bucket_id) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, entry->bucket.bucket_id, len);
        p += len;
    }

    /* owner */
    memcpy(p, &entry->owner.type, sizeof(uint32_t));
    p += sizeof(uint32_t);
    len = entry->owner.user_id ? strlen(entry->owner.user_id) : 0;
    memcpy(p, &len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (len > 0) {
        memcpy(p, entry->owner.user_id, len);
        p += len;
    }

    /* creation_time */
    memcpy(p, &entry->creation_time, sizeof(int64_t));
    p += sizeof(int64_t);

    /* linked */
    memcpy(p, &entry->linked, sizeof(bool));
    p += sizeof(bool);

    /* has_bucket_info */
    memcpy(p, &entry->has_bucket_info, sizeof(bool));
    p += sizeof(bool);

    return (int)(p - buf);
}

/**
 * @brief 从缓冲区解码桶入口点
 */
int rgw_bucket_entrypoint_decode(const uint8_t* buf,
                                  size_t buf_len,
                                  rgw_bucket_entrypoint_t* entry) {
    if (!buf || !entry) {
        return RGW_ERR_INVALID_ARG;
    }

    rgw_bucket_entrypoint_init(entry);

    const uint8_t* p = buf;
    size_t remaining = buf_len;

    /* version */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    uint32_t version;
    memcpy(&version, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* bucket.tenant */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    uint32_t len;
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > 0 && len < remaining) {
        entry->bucket.tenant = (char*)malloc(len + 1);
        if (!entry->bucket.tenant) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(entry->bucket.tenant, p, len);
        entry->bucket.tenant[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.name */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > 0 && len < remaining) {
        entry->bucket.name = (char*)malloc(len + 1);
        if (!entry->bucket.name) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(entry->bucket.name, p, len);
        entry->bucket.name[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.marker */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > 0 && len < remaining) {
        entry->bucket.marker = (char*)malloc(len + 1);
        if (!entry->bucket.marker) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(entry->bucket.marker, p, len);
        entry->bucket.marker[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* bucket.bucket_id */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);
    if (len > 0 && len < remaining) {
        entry->bucket.bucket_id = (char*)malloc(len + 1);
        if (!entry->bucket.bucket_id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(entry->bucket.bucket_id, p, len);
        entry->bucket.bucket_id[len] = '\0';
        p += len;
        remaining -= len;
    }

    /* owner */
    if (remaining < sizeof(uint32_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&entry->owner.type, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t) * 2;
    if (len > 0 && len < remaining) {
        if (entry->owner.type == 0) {
            entry->owner.user_id = (char*)malloc(len + 1);
            if (!entry->owner.user_id) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
            memcpy(entry->owner.user_id, p, len);
            entry->owner.user_id[len] = '\0';
        }
        p += len;
        remaining -= len;
    }

    /* creation_time */
    if (remaining < sizeof(int64_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&entry->creation_time, p, sizeof(int64_t));
    p += sizeof(int64_t);
    remaining -= sizeof(int64_t);

    /* linked */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&entry->linked, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* has_bucket_info */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&entry->has_bucket_info, p, sizeof(bool));
    p += sizeof(bool);

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - OMAP 键构建
 *============================================================================*/

/**
 * @brief 构建桶实例 OMAP 键
 */
int rgw_bucket_info_make_omap_key(const char* bucket_id,
                                    char* buf,
                                    size_t buf_size) {
    if (!bucket_id || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = snprintf(buf, buf_size, "%s%s",
                       RGW_BUCKET_INFO_OMAP_PREFIX, bucket_id);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/**
 * @brief 构建桶入口点 OMAP 键
 */
int rgw_bucket_entrypoint_make_omap_key(const char* tenant,
                                          const char* bucket_name,
                                          char* buf,
                                          size_t buf_size) {
    if (!bucket_name || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    const char* t = tenant ? tenant : "";

    int ret = snprintf(buf, buf_size, "%s%s:%s",
                       RGW_BUCKET_EP_OMAP_PREFIX, t, bucket_name);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/**
 * @brief 构建用户桶列表 OMAP 键前缀
 */
int rgw_user_buckets_make_omap_key(const char* tenant,
                                     const char* uid,
                                     char* buf,
                                     size_t buf_size) {
    if (!uid || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    const char* t = tenant ? tenant : "";

    int ret = snprintf(buf, buf_size, "%s:%s%s",
                       t, uid, RGW_USER_BUCKETS_SUFFIX);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 工具函数
 *============================================================================*/

/**
 * @brief 创建桶信息
 */
rgw_bucket_info_t* rgw_bucket_info_create(const char* bucket_name,
                                            const char* tenant,
                                            const char* owner_id) {
    rgw_bucket_info_t* info = (rgw_bucket_info_t*)malloc(sizeof(rgw_bucket_info_t));
    if (!info) {
        return NULL;
    }

    rgw_bucket_info_init(info);

    if (bucket_name) {
        info->bucket.name = strdup(bucket_name);
        if (!info->bucket.name) {
            rgw_bucket_info_destroy(info);
            return NULL;
        }
    }

    if (tenant) {
        info->bucket.tenant = strdup(tenant);
        if (!info->bucket.tenant) {
            rgw_bucket_info_destroy(info);
            return NULL;
        }
    }

    if (owner_id) {
        info->owner.type = 0;  /* user */
        info->owner.user_id = strdup(owner_id);
        if (!info->owner.user_id) {
            rgw_bucket_info_destroy(info);
            return NULL;
        }
    }

    return info;
}

/**
 * @brief 深拷贝桶信息
 */
int rgw_bucket_info_deep_copy(const rgw_bucket_info_t* src,
                               rgw_bucket_info_t* dst) {
    if (!src || !dst) {
        return RGW_ERR_INVALID_ARG;
    }

    rgw_bucket_info_free_members(dst);

    /* 复制简单类型 */
    *dst = *src;

    /* 深拷贝字符串成员 */
    dst->bucket.tenant = str_dup(src->bucket.tenant);
    dst->bucket.name = str_dup(src->bucket.name);
    dst->bucket.marker = str_dup(src->bucket.marker);
    dst->bucket.bucket_id = str_dup(src->bucket.bucket_id);
    dst->bucket.placement_rule = str_dup(src->bucket.placement_rule);

    if (src->owner.type == 0) {
        dst->owner.user_id = str_dup(src->owner.user_id);
    } else {
        dst->owner.account_id = str_dup(src->owner.account_id);
    }

    dst->zonegroup = str_dup(src->zonegroup);
    dst->placement_rule.name = str_dup(src->placement_rule.name);
    dst->placement_rule.storage_class = str_dup(src->placement_rule.storage_class);

    dst->layout.current_index.normal.shard_pool =
        str_dup(src->layout.current_index.normal.shard_pool);
    dst->layout.current_index.normal.object_prefix =
        str_dup(src->layout.current_index.normal.object_prefix);

    dst->swift_ver_location = str_dup(src->swift_ver_location);
    dst->new_bucket_instance_id = str_dup(src->new_bucket_instance_id);

    dst->website_conf.index_suffix = str_dup(src->website_conf.index_suffix);
    dst->website_conf.error_suffix = str_dup(src->website_conf.error_suffix);
    dst->website_conf.redirect_url = str_dup(src->website_conf.redirect_url);

    return RGW_OK;
}

/**
 * @brief 比较两个桶 ID
 */
bool rgw_bucket_id_equal(const rgw_bucket_id_t* a, const rgw_bucket_id_t* b) {
    if (!a || !b) {
        return false;
    }

    const char* a_name = a->name ? a->name : "";
    const char* b_name = b->name ? b->name : "";
    const char* a_tenant = a->tenant ? a->tenant : "";
    const char* b_tenant = b->tenant ? b->tenant : "";

    return (strcmp(a_name, b_name) == 0) && (strcmp(a_tenant, b_tenant) == 0);
}

/**
 * @brief 获取桶 ID 字符串表示
 */
char* rgw_bucket_id_to_string(const rgw_bucket_id_t* bucket) {
    if (!bucket) {
        return NULL;
    }

    const char* tenant = bucket->tenant ? bucket->tenant : "";
    const char* name = bucket->name ? bucket->name : "";

    char* buf = (char*)malloc(RGW_BUCKET_MAX_NAME_LEN * 2 + 2);
    if (!buf) {
        return NULL;
    }

    snprintf(buf, RGW_BUCKET_MAX_NAME_LEN * 2 + 2, "%s:%s", tenant, name);
    return buf;
}

/**
 * @brief 检查桶是否已标记删除
 */
bool rgw_bucket_info_is_deleted(const rgw_bucket_info_t* info) {
    if (!info) {
        return false;
    }
    return (info->flags & RGW_BUCKET_FLAG_DELETED) != 0;
}

/**
 * @brief 检查桶是否启用版本控制
 */
bool rgw_bucket_info_is_versioned(const rgw_bucket_info_t* info) {
    if (!info) {
        return false;
    }
    return (info->flags & RGW_BUCKET_FLAG_VERSIONED) != 0;
}

/**
 * @brief 检查桶是否启用对象锁定
 */
bool rgw_bucket_info_is_obj_lock_enabled(const rgw_bucket_info_t* info) {
    if (!info) {
        return false;
    }
    return (info->flags & RGW_BUCKET_FLAG_OBJ_LOCK_ENABLED) != 0;
}

/**
 * @brief 获取默认布局
 */
rgw_bucket_index_layout_t rgw_bucket_get_default_layout(uint32_t num_shards) {
    rgw_bucket_index_layout_t layout;
    memset(&layout, 0, sizeof(layout));

    layout.type = RGW_BUCKET_INDEX_TYPE_NORMAL;
    layout.normal.num_shards = num_shards > 0 ? num_shards : RGW_BUCKET_DEFAULT_SHARDS;

    return layout;
}

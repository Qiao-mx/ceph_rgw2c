/**
 * @file rgw_rados_ctx.c
 * @brief RADOS 上下文管理实现
 *
 * 实现 RADOS 连接和上下文管理的功能。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#include "rgw_rados_ctx.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 预定义池名称数组大小 */
#define RGW_RADOS_CTX_POOL_COUNT  10

/** 池名称数组 */
static const char* g_pool_names[RGW_RADOS_CTX_POOL_COUNT] = {
    RGW_RADOS_CTX_POOL_ZONE,
    RGW_RADOS_CTX_POOL_USERS_UID,
    RGW_RADOS_CTX_POOL_USERS_EMAIL,
    RGW_RADOS_CTX_POOL_USERS_KEYS,
    RGW_RADOS_CTX_POOL_USERS_SWIFT,
    RGW_RADOS_CTX_POOL_BUCKETS_INDEX,
    RGW_RADOS_CTX_POOL_BUCKETS_DATA,
    RGW_RADOS_CTX_POOL_BUCKETS_LOG,
    RGW_RADOS_CTX_POOL_META_LOG,
    RGW_RADOS_CTX_POOL_GC
};

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 初始化池数组
 *
 * 初始化上下文中的池数组。
 *
 * @param ctx RADOS 上下文
 */
static void rgw_rados_ctx_init_pools(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    for (int i = 0; i < RGW_RADOS_CTX_POOL_COUNT; i++) {
        ctx->pools[i].name = g_pool_names[i];
        ctx->pools[i].ioctx = NULL;
        ctx->pools[i].opened = false;
    }
}

/**
 * @brief 查找池索引
 *
 * 根据池名称查找池在数组中的索引。
 *
 * @param pool_name 池名称
 *
 * @return 池索引，未找到返回 -1
 */
static int rgw_rados_ctx_find_pool_index(const char* pool_name) {
    if (!pool_name) {
        return -1;
    }

    for (int i = 0; i < RGW_RADOS_CTX_POOL_COUNT; i++) {
        if (strcmp(pool_name, g_pool_names[i]) == 0) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief 释放计数器锁
 *
 * @param ctx RADOS 上下文
 */
static void rgw_rados_ctx_destroy_mutex(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->counter_mutex) {
        pthread_mutex_destroy((pthread_mutex_t*)ctx->counter_mutex);
        free(ctx->counter_mutex);
        ctx->counter_mutex = NULL;
    }
}

/**
 * @brief 初始化计数器锁
 *
 * @param ctx RADOS 上下文
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOMEM 内存分配失败
 */
static int rgw_rados_ctx_init_mutex(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    ctx->counter_mutex = malloc(sizeof(pthread_mutex_t));
    if (!ctx->counter_mutex) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    int ret = pthread_mutex_init((pthread_mutex_t*)ctx->counter_mutex, NULL);
    if (ret != 0) {
        free(ctx->counter_mutex);
        ctx->counter_mutex = NULL;
        return RGW_ERR_SYSTEM;
    }

    return 0;
}

/*============================================================================
 * 函数实现 - 生命周期
 *============================================================================*/

/**
 * @brief 创建 RADOS 上下文
 */
rgw_rados_ctx_t* rgw_rados_ctx_create(const char* cluster_name) {
    if (!cluster_name) {
        cluster_name = RGW_RADOS_CTX_DEFAULT_CLUSTER_NAME;
    }

    rgw_rados_ctx_t* ctx = (rgw_rados_ctx_t*)malloc(sizeof(rgw_rados_ctx_t));
    if (!ctx) {
        return NULL;
    }

    memset(ctx, 0, sizeof(rgw_rados_ctx_t));

    /* 复制集群名称 */
    strncpy(ctx->cluster_name, cluster_name,
            RGW_RADOS_CTX_MAX_CLUSTER_NAME_LEN - 1);
    ctx->cluster_name[RGW_RADOS_CTX_MAX_CLUSTER_NAME_LEN - 1] = '\0';

    /* 初始化默认配置文件路径 */
    strncpy(ctx->conf_path, RGW_RADOS_CTX_DEFAULT_CONF_PATH,
            RGW_RADOS_CTX_MAX_CONF_PATH_LEN - 1);
    ctx->conf_path[RGW_RADOS_CTX_MAX_CONF_PATH_LEN - 1] = '\0';

    /* 初始化池数组 */
    rgw_rados_ctx_init_pools(ctx);

    /* 初始化计数器锁 */
    int ret = rgw_rados_ctx_init_mutex(ctx);
    if (ret != 0) {
        free(ctx);
        return NULL;
    }

    ctx->state = RGW_RADOS_CTX_STATE_CLOSED;

    return ctx;
}

/**
 * @brief 连接 RADOS 集群
 */
int rgw_rados_ctx_connect(rgw_rados_ctx_t* ctx, const char* conf_path) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (ctx->state == RGW_RADOS_CTX_STATE_OPENED) {
        return RGW_OK;
    }

    int ret = rados_create(&ctx->cluster, ctx->cluster_name);
    if (ret < 0) {
        ctx->state = RGW_RADOS_CTX_STATE_ERROR;
        return RGW_ERR_SYSTEM;
    }

    /* 设置配置文件路径 */
    const char* path = conf_path ? conf_path : ctx->conf_path;
    ret = rados_conf_read_file(ctx->cluster, path);
    if (ret < 0) {
        rados_shutdown(ctx->cluster);
        ctx->cluster = NULL;
        ctx->state = RGW_RADOS_CTX_STATE_ERROR;
        return RGW_ERR_IO_ERROR;
    }

    /* 连接到集群 */
    ret = rados_connect(ctx->cluster);
    if (ret < 0) {
        rados_shutdown(ctx->cluster);
        ctx->cluster = NULL;
        ctx->state = RGW_RADOS_CTX_STATE_ERROR;
        return RGW_ERR_CONNECTION_FAILED;
    }

    /* 获取实例 ID */
    ctx->instance_id = rados_get_instance_id(ctx->cluster);

    ctx->state = RGW_RADOS_CTX_STATE_OPENED;

    return RGW_OK;
}

/**
 * @brief 断开 RADOS 集群连接
 */
int rgw_rados_ctx_disconnect(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return RGW_OK;
    }

    /* 关闭所有已打开的池 IO 上下文 */
    for (int i = 0; i < RGW_RADOS_CTX_POOL_COUNT; i++) {
        if (ctx->pools[i].opened && ctx->pools[i].ioctx) {
            rados_ioctx_destroy(ctx->pools[i].ioctx);
            ctx->pools[i].ioctx = NULL;
            ctx->pools[i].opened = false;
        }
    }

    /* 关闭集群连接 */
    if (ctx->cluster) {
        rados_shutdown(ctx->cluster);
        ctx->cluster = NULL;
    }

    /* 销毁计数器锁 */
    rgw_rados_ctx_destroy_mutex(ctx);

    ctx->state = RGW_RADOS_CTX_STATE_CLOSED;

    return RGW_OK;
}

/**
 * @brief 销毁 RADOS 上下文
 */
void rgw_rados_ctx_destroy(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    rgw_rados_ctx_disconnect(ctx);
    free(ctx);
}

/*============================================================================
 * 函数实现 - 池操作
 *============================================================================*/

/**
 * @brief 打开池的 IO 上下文
 */
int rgw_rados_ctx_open_pool(rgw_rados_ctx_t* ctx,
                             const char* pool_name,
                             rados_ioctx_t* ioctx) {
    if (!ctx || !pool_name || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (ctx->state != RGW_RADOS_CTX_STATE_OPENED) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 检查是否已打开 */
    int pool_idx = rgw_rados_ctx_find_pool_index(pool_name);
    if (pool_idx >= 0 && ctx->pools[pool_idx].opened) {
        *ioctx = ctx->pools[pool_idx].ioctx;
        return RGW_OK;
    }

    /* 创建新的 IO 上下文 */
    rados_ioctx_t new_ioctx;
    int ret = rados_ioctx_create(ctx->cluster, pool_name, &new_ioctx);
    if (ret < 0) {
        return RGW_ERR_NOT_FOUND;
    }

    /* 如果是预定义池，保存到数组 */
    if (pool_idx >= 0) {
        ctx->pools[pool_idx].ioctx = new_ioctx;
        ctx->pools[pool_idx].opened = true;
    }

    *ioctx = new_ioctx;
    return RGW_OK;
}

/**
 * @brief 打开预定义的元数据池
 */
int rgw_rados_ctx_open_meta_pool(rgw_rados_ctx_t* ctx,
                                   int pool_type,
                                   rados_ioctx_t* ioctx) {
    if (!ctx || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (pool_type < 0 || pool_type >= RGW_RADOS_CTX_POOL_COUNT) {
        return RGW_ERR_INVALID_ARG;
    }

    const char* pool_name = g_pool_names[pool_type];
    return rgw_rados_ctx_open_pool(ctx, pool_name, ioctx);
}

/**
 * @brief 获取池的 IO 上下文
 */
rados_ioctx_t rgw_rados_ctx_get_pool(rgw_rados_ctx_t* ctx,
                                        const char* pool_name) {
    if (!ctx || !pool_name) {
        return NULL;
    }

    /* 检查是否已打开 */
    int pool_idx = rgw_rados_ctx_find_pool_index(pool_name);
    if (pool_idx >= 0 && ctx->pools[pool_idx].opened) {
        return ctx->pools[pool_idx].ioctx;
    }

    /* 尝试打开 */
    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_pool(ctx, pool_name, &ioctx);
    if (ret != 0) {
        return NULL;
    }

    return ioctx;
}

/*============================================================================
 * 函数实现 - 配置获取
 *============================================================================*/

/**
 * @brief 获取实例 ID
 */
int rgw_rados_ctx_get_instance_id(rgw_rados_ctx_t* ctx,
                                    uint64_t* instance_id) {
    if (!ctx || !instance_id) {
        return RGW_ERR_INVALID_ARG;
    }

    if (ctx->state != RGW_RADOS_CTX_STATE_OPENED) {
        return RGW_ERR_INVALID_ARG;
    }

    *instance_id = ctx->instance_id;
    return RGW_OK;
}

/**
 * @brief 获取区域 ID
 */
const char* rgw_rados_ctx_get_zone_id(const rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return NULL;
    }
    return ctx->zone_id;
}

/**
 * @brief 设置区域 ID
 */
int rgw_rados_ctx_set_zone_id(rgw_rados_ctx_t* ctx, const char* zone_id) {
    if (!ctx || !zone_id) {
        return RGW_ERR_INVALID_ARG;
    }

    strncpy(ctx->zone_id, zone_id, RGW_RADOS_CTX_MAX_OMAP_KEY_LEN - 1);
    ctx->zone_id[RGW_RADOS_CTX_MAX_OMAP_KEY_LEN - 1] = '\0';

    return RGW_OK;
}

/**
 * @brief 获取区域组 ID
 */
const char* rgw_rados_ctx_get_zonegroup_id(const rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return NULL;
    }
    return ctx->zonegroup_id;
}

/**
 * @brief 设置区域组 ID
 */
int rgw_rados_ctx_set_zonegroup_id(rgw_rados_ctx_t* ctx,
                                     const char* zonegroup_id) {
    if (!ctx || !zonegroup_id) {
        return RGW_ERR_INVALID_ARG;
    }

    strncpy(ctx->zonegroup_id, zonegroup_id, RGW_RADOS_CTX_MAX_OMAP_KEY_LEN - 1);
    ctx->zonegroup_id[RGW_RADOS_CTX_MAX_OMAP_KEY_LEN - 1] = '\0';

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 版本控制
 *============================================================================*/

/**
 * @brief 获取最后操作版本号
 */
uint64_t rgw_rados_ctx_get_last_version(const rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return 0;
    }
    return ctx->last_version;
}

/**
 * @brief 设置最后操作版本号
 */
void rgw_rados_ctx_set_last_version(rgw_rados_ctx_t* ctx, uint64_t version) {
    if (ctx) {
        ctx->last_version = version;
    }
}

/*============================================================================
 * 函数实现 - bucket_id 生成
 *============================================================================*/

/**
 * @brief 生成唯一的 bucket_id
 */
int rgw_rados_ctx_generate_bucket_id(rgw_rados_ctx_t* ctx,
                                       char* bucket_id,
                                       size_t buf_size) {
    if (!ctx || !bucket_id) {
        return RGW_ERR_INVALID_ARG;
    }

    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 计算所需缓冲区大小 */
    const char* zone_id = ctx->zone_id;
    if (!zone_id || zone_id[0] == '\0') {
        zone_id = "default";
    }

    size_t required_size = strlen(zone_id) + 1 + 16 + 1 + 12 + 1;
    if (buf_size < required_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    /* 原子递增计数器 */
    pthread_mutex_lock((pthread_mutex_t*)ctx->counter_mutex);
    uint64_t counter = ++ctx->bucket_id_counter;
    pthread_mutex_unlock((pthread_mutex_t*)ctx->counter_mutex);

    /* 生成 bucket_id */
    int ret = snprintf(bucket_id, buf_size, "%s.%llx.%012llx",
                       zone_id,
                       (unsigned long long)ctx->instance_id,
                       (unsigned long long)counter);

    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 工具函数
 *============================================================================*/

/**
 * @brief 检查上下文是否已连接
 */
bool rgw_rados_ctx_is_connected(const rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return false;
    }
    return ctx->state == RGW_RADOS_CTX_STATE_OPENED;
}

/**
 * @brief 获取集群句柄
 */
rados_t rgw_rados_ctx_get_cluster(rgw_rados_ctx_t* ctx) {
    if (!ctx) {
        return NULL;
    }
    return ctx->cluster;
}

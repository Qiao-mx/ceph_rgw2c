/**
 * @file rgw_rados_ctx.h
 * @brief RADOS 上下文管理接口
 *
 * 定义 RADOS 连接和上下文管理的 C 接口，
 * 提供与 Ceph librados C API 的交互封装。
 *
 * 支持功能：
 * - 集群连接管理
 * - 多个池的 IO 上下文管理
 * - 实例 ID 获取（用于 bucket_id 生成）
 * - 事务支持
 */

#pragma once

#include <rados/librados.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 最大配置路径长度 */
#define RGW_RADOS_CTX_MAX_CONF_PATH_LEN     256

/** 最大集群名称长度 */
#define RGW_RADOS_CTX_MAX_CLUSTER_NAME_LEN  64

/** 最大池名称长度 */
#define RGW_RADOS_CTX_MAX_POOL_NAME_LEN     128

/** OMAP 键最大长度 */
#define RGW_RADOS_CTX_MAX_OMAP_KEY_LEN      256

/** 默认集群名称 */
#define RGW_RADOS_CTX_DEFAULT_CLUSTER_NAME  "ceph"

/** 默认配置文件路径 */
#define RGW_RADOS_CTX_DEFAULT_CONF_PATH     "/etc/ceph/ceph.conf"

/** 区域配置池名称 */
#define RGW_RADOS_CTX_POOL_ZONE             ".rgw.root"

/** 用户 UID 池名称 */
#define RGW_RADOS_CTX_POOL_USERS_UID        ".rgw.meta.users.uid"

/** 用户 email 池名称 */
#define RGW_RADOS_CTX_POOL_USERS_EMAIL       ".rgw.meta.users.email"

/** 用户 keys 池名称 */
#define RGW_RADOS_CTX_POOL_USERS_KEYS        ".rgw.meta.users.keys"

/** 用户 swift 池名称 */
#define RGW_RADOS_CTX_POOL_USERS_SWIFT      ".rgw.meta.users.swift"

/** 桶索引池名称 */
#define RGW_RADOS_CTX_POOL_BUCKETS_INDEX     ".rgw.buckets.index"

/** 桶数据池名称 */
#define RGW_RADOS_CTX_POOL_BUCKETS_DATA      ".rgw.buckets.data"

/** 桶日志池名称 */
#define RGW_RADOS_CTX_POOL_BUCKETS_LOG       ".rgw.buckets.log"

/** 元数据日志池名称 */
#define RGW_RADOS_CTX_POOL_META_LOG          ".rgw.log"

/** 追加日志池名称 */
#define RGW_RADOS_CTX_POOL_GC                ".rgw.gc"

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief RADOS 上下文状态
 */
typedef enum {
    RGW_RADOS_CTX_STATE_CLOSED = 0,   /**< 未连接 */
    RGW_RADOS_CTX_STATE_OPENED,       /**< 已连接 */
    RGW_RADOS_CTX_STATE_ERROR         /**< 连接错误 */
} rgw_rados_ctx_state_t;

/**
 * @brief 池上下文映射
 */
typedef struct rgw_rados_ctx_pool {
    const char* name;        /**< 池名称 */
    rados_ioctx_t ioctx;     /**< IO 上下文 */
    bool opened;             /**< 是否已打开 */
} rgw_rados_ctx_pool_t;

/**
 * @brief RADOS 上下文
 *
 * 封装 librados 连接所需的所有资源。
 *
 * 管理多个池的 IO 上下文：
 * - zone: 区域配置池
 * - users_uid: 用户 UID 池
 * - users_email: 用户 email 池
 * - users_keys: 用户 access key 池
 * - users_swift: 用户 swift key 池
 * - buckets_index: 桶索引池
 * - buckets_data: 桶数据池
 * - buckets_log: 桶日志池
 * - meta_log: 元数据日志池
 * - gc: 垃圾回收池
 */
typedef struct rgw_rados_ctx {
    /** 集群句柄 */
    rados_t cluster;

    /** 状态 */
    rgw_rados_ctx_state_t state;

    /** 集群名称 */
    char cluster_name[RGW_RADOS_CTX_MAX_CLUSTER_NAME_LEN];

    /** 配置文件路径 */
    char conf_path[RGW_RADOS_CTX_MAX_CONF_PATH_LEN];

    /** 实例 ID (用于 bucket_id 生成) */
    uint64_t instance_id;

    /** 最后操作版本号 */
    uint64_t last_version;

    /** 区域 ID */
    char zone_id[RGW_RADOS_CTX_MAX_OMAP_KEY_LEN];

    /** 区域组 ID */
    char zonegroup_id[RGW_RADOS_CTX_MAX_OMAP_KEY_LEN];

    /** 池上下文数组 */
    rgw_rados_ctx_pool_t pools[10];

    /** 桶 ID 计数器 (用于 bucket_id 生成) */
    uint64_t bucket_id_counter;

    /** 计数器锁 */
    void* counter_mutex;
} rgw_rados_ctx_t;

/*============================================================================
 * 函数声明 - 生命周期
 *============================================================================*/

/**
 * @brief 创建 RADOS 上下文
 *
 * 分配并初始化 RADOS 上下文结构体。
 *
 * @param cluster_name 集群名称，传入 NULL 使用默认 "ceph"
 *
 * @return 新创建的上下文，失败返回 NULL
 *
 * @retval NULL 内存分配失败
 *
 * @note 调用者需要使用 rgw_rados_ctx_destroy() 释放
 * @see rgw_rados_ctx_destroy()
 */
rgw_rados_ctx_t* rgw_rados_ctx_create(const char* cluster_name);

/**
 * @brief 连接 RADOS 集群
 *
 * 根据配置文件连接到 RADOS 集群。
 *
 * @param ctx RADOS 上下文
 * @param conf_path 配置文件路径，传入 NULL 使用默认路径
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 * @retval -ECONNREFUSED 无法连接集群
 *
 * @note 连接后需要调用 rgw_rados_ctx_disconnect() 断开
 * @see rgw_rados_ctx_disconnect()
 */
int rgw_rados_ctx_connect(rgw_rados_ctx_t* ctx, const char* conf_path);

/**
 * @brief 断开 RADOS 集群连接
 *
 * 释放所有与集群相关的资源。
 *
 * @param ctx RADOS 上下文
 *
 * @return 执行结果
 * @retval 0 成功
 *
 * @see rgw_rados_ctx_connect()
 */
int rgw_rados_ctx_disconnect(rgw_rados_ctx_t* ctx);

/**
 * @brief 销毁 RADOS 上下文
 *
 * 释放上下文占用的所有资源，包括内存。
 *
 * @param ctx RADOS 上下文，传入 NULL 无操作
 *
 * @see rgw_rados_ctx_create()
 */
void rgw_rados_ctx_destroy(rgw_rados_ctx_t* ctx);

/*============================================================================
 * 函数声明 - 池操作
 *============================================================================*/

/**
 * @brief 打开池的 IO 上下文
 *
 * 获取指定池的 IO 上下文，用于后续的对象操作。
 * 池上下文由 rgw_rados_ctx_disconnect() 统一释放。
 *
 * @param ctx RADOS 上下文
 * @param pool_name 池名称
 * @param ioctx 输出参数，返回 IO 上下文
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效或上下文未连接
 * @retval -ENOENT 池不存在
 *
 * @see rgw_rados_ctx_disconnect()
 */
int rgw_rados_ctx_open_pool(rgw_rados_ctx_t* ctx,
                             const char* pool_name,
                             rados_ioctx_t* ioctx);

/**
 * @brief 打开预定义的元数据池
 *
 * 便利函数，用于打开常用的元数据池。
 *
 * @param ctx RADOS 上下文
 * @param pool_type 池类型
 * @param ioctx 输出参数，返回 IO 上下文
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOENT 池不存在
 *
 * @see rgw_rados_ctx_open_pool()
 */
int rgw_rados_ctx_open_meta_pool(rgw_rados_ctx_t* ctx,
                                   int pool_type,
                                   rados_ioctx_t* ioctx);

/**
 * @brief 获取池的 IO 上下文
 *
 * 获取已打开池的 IO 上下文，如果尚未打开则先打开。
 *
 * @param ctx RADOS 上下文
 * @param pool_name 池名称
 *
 * @return IO 上下文，失败返回 NULL
 */
rados_ioctx_t rgw_rados_ctx_get_pool(rgw_rados_ctx_t* ctx,
                                        const char* pool_name);

/*============================================================================
 * 函数声明 - 配置获取
 *============================================================================*/

/**
 * @brief 获取实例 ID
 *
 * 获取用于生成 bucket_id 的实例 ID。
 *
 * @param ctx RADOS 上下文
 * @param instance_id 输出参数，返回实例 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 *
 * @see rgw_generate_bucket_id()
 */
int rgw_rados_ctx_get_instance_id(rgw_rados_ctx_t* ctx,
                                    uint64_t* instance_id);

/**
 * @brief 获取区域 ID
 *
 * @param ctx RADOS 上下文
 *
 * @return 区域 ID 字符串
 */
const char* rgw_rados_ctx_get_zone_id(const rgw_rados_ctx_t* ctx);

/**
 * @brief 设置区域 ID
 *
 * @param ctx RADOS 上下文
 * @param zone_id 区域 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_rados_ctx_set_zone_id(rgw_rados_ctx_t* ctx, const char* zone_id);

/**
 * @brief 获取区域组 ID
 *
 * @param ctx RADOS 上下文
 *
 * @return 区域组 ID 字符串
 */
const char* rgw_rados_ctx_get_zonegroup_id(const rgw_rados_ctx_t* ctx);

/**
 * @brief 设置区域组 ID
 *
 * @param ctx RADOS 上下文
 * @param zonegroup_id 区域组 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_rados_ctx_set_zonegroup_id(rgw_rados_ctx_t* ctx,
                                     const char* zonegroup_id);

/*============================================================================
 * 函数声明 - 版本控制
 *============================================================================*/

/**
 * @brief 获取最后操作版本号
 *
 * 用于跟踪 OMAP 操作的版本。
 *
 * @param ctx RADOS 上下文
 *
 * @return 最后操作版本号
 */
uint64_t rgw_rados_ctx_get_last_version(const rgw_rados_ctx_t* ctx);

/**
 * @brief 设置最后操作版本号
 *
 * @param ctx RADOS 上下文
 * @param version 版本号
 */
void rgw_rados_ctx_set_last_version(rgw_rados_ctx_t* ctx,
                                      uint64_t version);

/*============================================================================
 * 函数声明 - bucket_id 生成
 *============================================================================*/

/**
 * @brief 生成唯一的 bucket_id
 *
 * 格式: {zone_id}.{instance_id}.{bucket_counter:012llx}
 *
 * @param ctx RADOS 上下文
 * @param bucket_id 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ERANGE 缓冲区太小
 *
 * @note 此函数线程安全
 */
int rgw_rados_ctx_generate_bucket_id(rgw_rados_ctx_t* ctx,
                                       char* bucket_id,
                                       size_t buf_size);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 检查上下文是否已连接
 *
 * @param ctx RADOS 上下文
 *
 * @return 是否已连接
 * @retval true 已连接
 * @retval false 未连接或无效
 */
bool rgw_rados_ctx_is_connected(const rgw_rados_ctx_t* ctx);

/**
 * @brief 获取集群句柄
 *
 * 获取底层 librados 集群句柄，用于高级操作。
 *
 * @param ctx RADOS 上下文
 *
 * @return 集群句柄
 */
rados_t rgw_rados_ctx_get_cluster(rgw_rados_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

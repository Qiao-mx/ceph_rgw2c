/**
 * @file rgw_bucket_serde.h
 * @brief 桶信息序列化接口
 *
 * 定义桶信息的序列化/反序列化接口，
 * 用于将桶信息存储到 RADOS OMAP 或从 OMAP 加载。
 *
 * 对应原 C++ 结构：
 * - RGWBucketInfo: 桶实例信息
 * - RGWBucketEntryPoint: 桶入口点信息
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 桶信息编码版本号 */
#define RGW_BUCKET_INFO_ENCODE_VERSION_START   20
#define RGW_BUCKET_INFO_ENCODE_VERSION_END    7

/** 桶入口点编码版本号 */
#define RGW_BUCKET_ENTRYPOINT_ENCODE_VERSION  10

/** 缓冲区初始大小 */
#define RGW_BUCKET_BUF_INIT_SIZE              4096

/** 缓冲区最大大小 (防止恶意数据，10MB) */
#define RGW_BUCKET_BUF_MAX_SIZE               (10 * 1024 * 1024)

/** 桶名称最大长度 */
#define RGW_BUCKET_MAX_NAME_LEN              255

/** 区域组 ID 最大长度 */
#define RGW_BUCKET_MAX_ZONEGROUP_LEN         64

/** 默认分片数 */
#define RGW_BUCKET_DEFAULT_SHARDS             11

/** 最大分片数 */
#define RGW_BUCKET_MAX_SHARDS                 1000

/*============================================================================
 * 标志位定义
 *============================================================================*/

/** 桶标志位 */
typedef enum {
    RGW_BUCKET_FLAG_NONE               = 0,
    RGW_BUCKET_FLAG_VERSIONED          = (1 << 0),     /**< 版本控制 */
    RGW_BUCKET_FLAG_VERSIONS_SUSPENDED = (1 << 1),     /**< 版本控制暂停 */
    RGW_BUCKET_FLAG_MFA_ENABLED        = (1 << 2),     /**< MFA 启用 */
    RGW_BUCKET_FLAG_DELETED            = (1 << 3),     /**< 已删除 */
    RGW_BUCKET_FLAG_DATASYNC_DISABLED  = (1 << 4),     /**< 数据同步禁用 */
    RGW_BUCKET_FLAG_OBJ_LOCK_ENABLED   = (1 << 5),    /**< 对象锁定启用 */
    RGW_BUCKET_FLAG_REQUESTER_PAYS     = (1 << 6),     /**< 请求者付费 */
    RGW_BUCKET_FLAG_HAS_WEBSITE        = (1 << 7),     /**< 有网站配置 */
    RGW_BUCKET_FLAG_SWIFT_VERSIONING   = (1 << 8),     /**< Swift 版本控制 */
    RGW_BUCKET_FLAG_HASHED_LAYOUT      = (1 << 9),     /**< 哈希布局 */
} rgw_bucket_flag_t;

/** 桶索引类型 */
typedef enum {
    RGW_BUCKET_INDEX_TYPE_NORMAL = 0,    /**< 正常索引 */
    RGW_BUCKET_INDEX_TYPE_INDEXLESS = 1, /**< 无索引 */
    RGW_BUCKET_INDEX_TYPE_NONE = 2        /**< 无索引类型 */
} rgw_bucket_index_type_t;

/** resharding 状态 */
typedef enum {
    RGW_BUCKET_RESHARD_NOT_RESHARDING = 0,  /**< 未 resharding */
    RGW_BUCKET_RESHARD_IN_PROGRESS = 1,      /**< resharding 进行中 */
    RGW_BUCKET_RESHARD_DONE = 2              /**< resharding 完成 */
} rgw_bucket_reshard_status_t;

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 桶标识
 *
 * 桶的唯一标识。
 */
typedef struct {
    char* tenant;              /**< 租户 */
    char* name;               /**< 桶名称 */
    char* marker;             /**< 桶标记 (UUID) */
    char* bucket_id;          /**< 桶 ID */
    char* placement_rule;      /**< 放置规则名称 */
    uint32_t proj_ver;       /**< 项目版本 */
} rgw_bucket_id_t;

/**
 * @brief 所有者
 *
 * 桶的所有者，可以是用户或账户。
 */
typedef struct {
    uint32_t type;            /**< 类型: 0=user, 1=account */
    union {
        char* user_id;        /**< 用户 ID */
        char* account_id;     /**< 账户 ID */
    };
} rgw_owner_t;

/**
 * @brief 放置规则
 *
 * 数据放置的规则。
 */
typedef struct {
    char* name;              /**< 规则名称 */
    char* storage_class;      /**< 存储类别 */
} rgw_placement_rule_t;

/**
 * @brief 桶索引分片布局
 *
 * 定义桶索引的分片方式。
 */
typedef struct {
    uint32_t num_shards;     /**< 分片数量 */
    uint32_t shard_pool_id;   /**< 分片池 ID */
    char* shard_pool;        /**< 分片池名称 */
    char* object_prefix;      /**< 对象前缀 */
} rgw_bucket_index_shard_layout_t;

/**
 * @brief 桶索引布局生成
 */
typedef struct {
    char* gen_id;            /**< 生成 ID */
    rgw_bucket_index_shard_layout_t layout;  /**< 分片布局 */
} rgw_bucket_index_layout_gen_t;

/**
 * @brief 桶索引布局
 */
typedef struct {
    rgw_bucket_index_type_t type;  /**< 索引类型 */
    rgw_bucket_index_shard_layout_t normal; /**< 正常布局 */
    rgw_bucket_index_layout_gen_t log;     /**< 日志布局 */
    rgw_bucket_index_layout_gen_t ulog;    /**< 更新日志布局 */
} rgw_bucket_index_layout_t;

/**
 * @brief 桶布局
 */
typedef struct {
    rgw_bucket_index_layout_t current_index;  /**< 当前索引 */
    rgw_bucket_index_layout_t target_index;    /**< 目标索引 */
} rgw_bucket_layout_t;

/**
 * @brief 对象锁定配置
 */
typedef struct {
    bool enabled;             /**< 是否启用 */
    int32_t mode;            /**< 模式: 0=COMPLIANCE, 1=GOVERNANCE */
    int32_t retain_days;     /**< 保留天数 */
} rgw_object_lock_t;

/**
 * @brief 桶配额信息
 */
typedef struct {
    int64_t max_size;            /**< 最大存储大小 */
    int64_t max_objects;         /**< 最大对象数 */
    bool enabled;                 /**< 是否启用 */
} rgw_bucket_quota_info_t;

/**
 * @brief 桶网站配置
 */
typedef struct {
    char* index_suffix;           /**< 索引后缀 */
    char* error_suffix;           /**< 错误页面后缀 */
    char* redirect_url;           /**< 重定向 URL */
} rgw_bucket_website_conf_t;

/**
 * @brief resharding 状态
 */
typedef struct {
    rgw_bucket_reshard_status_t status;  /**< 状态 */
    uint32_t num_shards;                 /**< 分片数 */
    uint64_t new_bucket_instance_id;    /**< 新桶实例 ID */
} rgw_bucket_reshard_info_t;

/**
 * @brief 桶信息
 *
 * 完整的桶实例信息。
 */
typedef struct {
    /** 桶标识 */
    rgw_bucket_id_t bucket;

    /** 所有者 */
    rgw_owner_t owner;

    /** 标志位 */
    uint32_t flags;

    /** 区域组 */
    char* zonegroup;

    /** 创建时间 */
    int64_t creation_time;

    /** 放置规则 */
    rgw_placement_rule_t placement_rule;

    /** 是否有实例对象 */
    bool has_instance_obj;

    /** 配额 */
    rgw_bucket_quota_info_t quota;

    /** 桶布局 */
    rgw_bucket_layout_t layout;

    /** 请求者付费 */
    bool requester_pays;

    /** 网站配置 */
    bool has_website;
    rgw_bucket_website_conf_t website_conf;

    /** Swift 版本控制 */
    bool swift_versioning;
    char* swift_ver_location;

    /** resharding */
    rgw_bucket_reshard_info_t reshard_info;
    char* new_bucket_instance_id;

    /** 对象锁定 */
    rgw_object_lock_t obj_lock;
} rgw_bucket_info_t;

/**
 * @brief 桶入口点
 *
 * 用于索引桶名称到桶实例。
 */
typedef struct {
    rgw_bucket_id_t bucket;          /**< 桶标识 */
    rgw_owner_t owner;              /**< 所有者 */
    int64_t creation_time;          /**< 创建时间 */
    bool linked;                    /**< 是否已关联到用户 */
    bool has_bucket_info;           /**< 是否有桶信息 */
    /** 内联桶信息（简化版） */
    char* inline_info;              /**< JSON 格式的内联信息 */
} rgw_bucket_entrypoint_t;

/*============================================================================
 * 函数声明 - 序列化/反序列化
 *============================================================================*/

/**
 * @brief 计算桶信息编码后的大小
 *
 * @param info 桶信息
 *
 * @return 所需缓冲区大小，失败返回 0
 */
size_t rgw_bucket_info_calc_encode_size(const rgw_bucket_info_t* info);

/**
 * @brief 编码桶信息到缓冲区
 *
 * @param info 桶信息
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 编码后的数据长度，失败返回负值
 */
int rgw_bucket_info_encode(const rgw_bucket_info_t* info,
                           uint8_t* buf,
                           size_t buf_size);

/**
 * @brief 动态编码桶信息
 *
 * @param info 桶信息
 * @param out_buf 输出参数，返回分配的缓冲区
 * @param out_len 输出参数，返回编码后的长度
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 free() 释放 *out_buf
 */
int rgw_bucket_info_encode_alloc(const rgw_bucket_info_t* info,
                                  uint8_t** out_buf,
                                  size_t* out_len);

/**
 * @brief 从缓冲区解码桶信息
 *
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param info 输出桶信息
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 rgw_bucket_info_free() 释放 info 中的动态成员
 */
int rgw_bucket_info_decode(const uint8_t* buf,
                            size_t buf_len,
                            rgw_bucket_info_t* info);

/**
 * @brief 释放桶信息动态内存
 *
 * @param info 桶信息
 */
void rgw_bucket_info_free_members(rgw_bucket_info_t* info);

/**
 * @brief 初始化桶信息
 *
 * @param info 桶信息
 */
void rgw_bucket_info_init(rgw_bucket_info_t* info);

/**
 * @brief 销毁桶信息
 *
 * @param info 桶信息
 */
void rgw_bucket_info_destroy(rgw_bucket_info_t* info);

/*============================================================================
 * 函数声明 - 桶入口点
 *============================================================================*/

/**
 * @brief 编码桶入口点到缓冲区
 *
 * @param entry 桶入口点
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 编码后的数据长度，失败返回负值
 */
int rgw_bucket_entrypoint_encode(const rgw_bucket_entrypoint_t* entry,
                                  uint8_t* buf,
                                  size_t buf_size);

/**
 * @brief 从缓冲区解码桶入口点
 *
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param entry 输出桶入口点
 *
 * @return 执行结果
 */
int rgw_bucket_entrypoint_decode(const uint8_t* buf,
                                  size_t buf_len,
                                  rgw_bucket_entrypoint_t* entry);

/**
 * @brief 释放桶入口点动态内存
 *
 * @param entry 桶入口点
 */
void rgw_bucket_entrypoint_free_members(rgw_bucket_entrypoint_t* entry);

/**
 * @brief 初始化桶入口点
 *
 * @param entry 桶入口点
 */
void rgw_bucket_entrypoint_init(rgw_bucket_entrypoint_t* entry);

/**
 * @brief 销毁桶入口点
 *
 * @param entry 桶入口点
 */
void rgw_bucket_entrypoint_destroy(rgw_bucket_entrypoint_t* entry);

/*============================================================================
 * 函数声明 - OMAP 键构建
 *============================================================================*/

/**
 * @brief 构建桶实例 OMAP 键
 *
 * 格式: {bucket_id}
 *
 * @param bucket_id 桶 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_bucket_info_make_omap_key(const char* bucket_id,
                                    char* buf,
                                    size_t buf_size);

/**
 * @brief 构建桶入口点 OMAP 键
 *
 * 格式: {tenant}:{name}
 *
 * @param tenant 租户
 * @param bucket_name 桶名称
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_bucket_entrypoint_make_omap_key(const char* tenant,
                                          const char* bucket_name,
                                          char* buf,
                                          size_t buf_size);

/**
 * @brief 构建用户桶列表 OMAP 键前缀
 *
 * 用户桶列表用于关联用户和桶。
 * 格式: {tenant}:{uid}:buckets
 *
 * @param tenant 租户
 * @param uid 用户 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_user_buckets_make_omap_key(const char* tenant,
                                     const char* uid,
                                     char* buf,
                                     size_t buf_size);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 创建桶信息
 *
 * 分配并初始化一个新的桶信息结构。
 *
 * @param bucket_name 桶名称
 * @param tenant 租户
 * @param owner_id 所有者 ID
 *
 * @return 新创建的桶信息，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_bucket_info_destroy() 释放
 */
rgw_bucket_info_t* rgw_bucket_info_create(const char* bucket_name,
                                            const char* tenant,
                                            const char* owner_id);

/**
 * @brief 深拷贝桶信息
 *
 * @param src 源桶信息
 * @param dst 目标桶信息
 *
 * @return 执行结果
 */
int rgw_bucket_info_deep_copy(const rgw_bucket_info_t* src,
                                rgw_bucket_info_t* dst);

/**
 * @brief 比较两个桶 ID
 *
 * @param a 桶 ID A
 * @param b 桶 ID B
 *
 * @return 是否相等
 */
bool rgw_bucket_id_equal(const rgw_bucket_id_t* a, const rgw_bucket_id_t* b);

/**
 * @brief 获取桶 ID 字符串表示
 *
 * @param bucket 桶 ID
 *
 * @return 字符串表示，失败返回 NULL
 *
 * @note 调用者需要使用 free() 释放返回的字符串
 */
char* rgw_bucket_id_to_string(const rgw_bucket_id_t* bucket);

/**
 * @brief 检查桶是否已标记删除
 *
 * @param info 桶信息
 *
 * @return 是否已删除
 */
bool rgw_bucket_info_is_deleted(const rgw_bucket_info_t* info);

/**
 * @brief 检查桶是否启用版本控制
 *
 * @param info 桶信息
 *
 * @return 是否启用版本控制
 */
bool rgw_bucket_info_is_versioned(const rgw_bucket_info_t* info);

/**
 * @brief 检查桶是否启用对象锁定
 *
 * @param info 桶信息
 *
 * @return 是否启用对象锁定
 */
bool rgw_bucket_info_is_obj_lock_enabled(const rgw_bucket_info_t* info);

/**
 * @brief 获取默认布局
 *
 * 获取给定分片数的默认布局。
 *
 * @param num_shards 分片数，为 0 则使用默认值
 *
 * @return 默认布局
 */
rgw_bucket_index_layout_t rgw_bucket_get_default_layout(uint32_t num_shards);

#ifdef __cplusplus
}
#endif

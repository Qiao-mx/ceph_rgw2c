/**
 * @file rgw_sal_rados.h
 * @brief RADOS 驱动 C 接口适配器
 *
 * 提供 RADOS 存储后端的 C 语言接口适配器。
 * 内部实现仍然使用 C++，但提供 C 接口供 SAL C 层调用。
 */

#pragma once

#include "rgw_sal.h"
#include "rgw_sal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * RADOS 驱动特定宏定义
 *============================================================================*/

/* RADOS 上下文池类型 */
#define RGW_RADOS_CTX_POOL_BUCKETS_DATA 1
#define RGW_RADOS_CTX_POOL_BUCKETS_INDEX 2

/* 删除标志 */
#define RGW_SAL_DELETE_FLAG_EXPIRED 0x0001
#define RGW_SAL_DELETE_FLAG_VERSIONING 0x0002

/* 分片上传最大分片数 */
#define RGW_MAX_PART_NUMBER 10000

/*============================================================================
 * RADOS 驱动特定类型 (前向声明)
 *============================================================================*/

/* 注意: rados_ioctx_t 和 rados_ctx_pool_t 已由 librados.h 定义 */

/* 前向声明 */
typedef struct rados_user_impl rados_user_impl_t;
typedef struct rados_bucket_impl rados_bucket_impl_t;
typedef struct rados_object_impl rados_object_impl_t;

/**
 * @brief RADOS 对象列表迭代器类型
 */
typedef void* rados_nobjects_list_t;

/*============================================================================
 * RADOS 驱动特定结构体 (不与 core/types 冲突)
 *============================================================================*/

/**
 * @brief RADOS 特定的用户扩展信息
 */
typedef struct rgw_sal_rados_user_impl {
    void* rados_user;           /**< 内部的 RadosUser* */
    bool user_info_loaded;      /**< 用户信息是否已加载 */
    char* access_key;           /**< 访问密钥 */
    char* secret_key;           /**< 秘密密钥 */
} rgw_sal_rados_user_impl_t;

/**
 * @brief RADOS 特定的桶扩展信息
 */
typedef struct rgw_sal_rados_bucket_impl {
    void* rados_bucket;         /**< 内部的 RadosBucket* */
    bool bucket_info_loaded;     /**< 桶信息是否已加载 */
    void* ioctx;                /**< RADOS IoCtx */
    char* placement_rule;        /**< 放置规则 */
} rgw_sal_rados_bucket_impl_t;

/**
 * @brief RADOS 特定的对象扩展信息
 */
typedef struct rgw_sal_rados_object_impl {
    void* rados_object;         /**< 内部的 RadosObject* */
    void* rados_ctx;            /**< 对象上下文 */
    char* locator;              /**< 对象定位符 */
    uint64_t obj_size;          /**< 对象大小 */
} rgw_sal_rados_object_impl_t;

/**
 * @brief RADOS 驱动特定扩展
 */
typedef struct rgw_sal_rados_driver_impl {
    void* rados_store;          /**< 内部的 RadosStore* */
    void* neorados;             /**< neorados 句柄 */
    void* rados;                /**< RGWRados* 句柄 */
    void* ctx_pool;             /**< 上下文池 (void* 避免依赖) */
} rgw_sal_rados_driver_impl_t;

/**
 * @brief RADOS 桶统计信息结构
 */
typedef struct rgw_sal_rados_bucket_stats {
    int64_t actual_size;        /**< 实际大小 */
    int64_t size;               /**< 大小 */
    int64_t size_rounded;      /**< 对齐后大小 */
    int64_t num_objects;        /**< 对象数量 */
    int64_t size_bytes;        /**< 大小(字节) */
    int64_t mtime;              /**< 修改时间 */
    int num_shards;             /**< 分片数 */
    char max_marker[256];       /**< 最大 marker */
} rgw_sal_rados_bucket_stats_t;

/**
 * @brief RADOS 读回调函数类型
 *
 * @param buffer 数据缓冲区
 * @param buffer_len 缓冲区大小
 * @param arg 用户参数
 * @return 读取的字节数
 */
typedef int (*rgw_sal_rados_read_callback_t)(char* buffer, size_t buffer_len, void* arg);

/*============================================================================
 * RADOS 驱动工厂函数
 *============================================================================*/

/**
 * @brief 创建 RADOS 驱动
 * @param ctx_pool RADOS 上下文池 (可选)
 * @param user_ctx_pool 用户上下文池 (可选)
 * @return 驱动句柄，失败返回 NULL
 */
rgw_sal_driver_t* rgw_sal_rados_driver_create(void* ctx_pool, void* user_ctx_pool);

/**
 * @brief 销毁 RADOS 驱动
 * @param driver 驱动实例
 */
void rgw_sal_rados_driver_destroy(rgw_sal_driver_t* driver);

/**
 * @brief 获取 RADOS 驱动实现
 * @param driver SAL 驱动句柄
 * @return RADOS 驱动实现
 */
rgw_sal_rados_driver_impl_t* rgw_sal_rados_get_impl(rgw_sal_driver_t* driver);

/*============================================================================
 * RADOS 用户操作接口
 *============================================================================*/

/**
 * @brief 获取用户
 * @param driver 驱动实例
 * @param uid 用户 ID
 * @return 用户实例，或失败时返回 NULL
 */
rgw_sal_user_t* rgw_sal_rados_get_user(rgw_sal_driver_t* driver, const rgw_sal_user_id_t* uid);

/**
 * @brief 释放用户
 * @param user 用户实例
 */
void rgw_sal_rados_user_destroy(rgw_sal_user_t* user);

/**
 * @brief 克隆用户
 * @param user 用户实例
 * @return 克隆的用户，或失败时返回 NULL
 */
rgw_sal_user_t* rgw_sal_rados_user_clone(const rgw_sal_user_t* user);

/* 用户属性访问器 */
const char* rgw_sal_rados_user_get_id(const rgw_sal_user_t* user);
const char* rgw_sal_rados_user_get_display_name(rgw_sal_user_t* user);
int rgw_sal_rados_user_set_display_name(rgw_sal_user_t* user, const char* name);
const char* rgw_sal_rados_user_get_tenant(const rgw_sal_user_t* user);
uint32_t rgw_sal_rados_user_get_type(const rgw_sal_user_t* user);
int32_t rgw_sal_rados_user_get_max_buckets(const rgw_sal_user_t* user);
void rgw_sal_rados_user_set_max_buckets(rgw_sal_user_t* user, int32_t max);

/* 用户属性映射 */
rgw_sal_attrs_t* rgw_sal_rados_user_get_attrs(rgw_sal_user_t* user);

/*============================================================================
 * RADOS 桶操作接口
 *============================================================================*/

/**
 * @brief 获取桶
 * @param driver 驱动实例
 * @param bid 桶 ID
 * @return 桶实例，或失败时返回 NULL
 */
rgw_sal_bucket_t* rgw_sal_rados_get_bucket(rgw_sal_driver_t* driver, const rgw_sal_bucket_id_t* bid);

/**
 * @brief 释放桶
 * @param bucket 桶实例
 */
void rgw_sal_rados_bucket_destroy(rgw_sal_bucket_t* bucket);

/**
 * @brief 克隆桶
 * @param bucket 桶实例
 * @return 克隆的桶，或失败时返回 NULL
 */
rgw_sal_bucket_t* rgw_sal_rados_bucket_clone(const rgw_sal_bucket_t* bucket);

/* 桶属性访问器 */
const char* rgw_sal_rados_bucket_get_name(const rgw_sal_bucket_t* bucket);
const char* rgw_sal_rados_bucket_get_tag(rgw_sal_bucket_t* bucket);
void rgw_sal_rados_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag);
rgw_sal_attrs_t* rgw_sal_rados_bucket_get_attrs(rgw_sal_bucket_t* bucket);

/*============================================================================
 * RADOS 对象操作接口
 *============================================================================*/

/**
 * @brief 获取对象
 * @param driver 驱动实例
 * @param bucket 桶实例
 * @param key 对象键
 * @return 对象实例，或失败时返回 NULL
 */
rgw_sal_object_t* rgw_sal_rados_get_object(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket, const rgw_sal_obj_key_t* key);

/**
 * @brief 释放对象
 * @param obj 对象实例
 */
void rgw_sal_rados_object_destroy(rgw_sal_object_t* obj);

/**
 * @brief 克隆对象
 * @param obj 对象实例
 * @return 克隆的对象，或失败时返回 NULL
 */
rgw_sal_object_t* rgw_sal_rados_object_clone(const rgw_sal_object_t* obj);

/* 对象属性访问器 */
const char* rgw_sal_rados_object_get_name(const rgw_sal_object_t* obj);
rgw_sal_attrs_t* rgw_sal_rados_object_get_attrs(rgw_sal_object_t* obj);
void rgw_sal_rados_object_set_atomic(rgw_sal_object_t* obj, bool atomic);
bool rgw_sal_rados_object_is_atomic(const rgw_sal_object_t* obj);

/*============================================================================
 * RADOS 特定操作
 *============================================================================*/

/**
 * @brief 初始化 RADOS 集群连接
 * @param config_file 配置文件路径
 * @param cluster_name 集群名称
 * @param flags 连接标志
 * @return 0 成功，负值失败
 */
int rgw_rados_connect(const char* config_file, const char* cluster_name, uint32_t flags);

/**
 * @brief 断开 RADOS 集群连接
 */
void rgw_rados_disconnect(void);

/**
 * @brief 获取 RADOS IoCtx
 * @param pool_name 池名称
 * @param flags 标志
 * @return IoCtx 句柄
 */
void* rgw_rados_get_ioctx(const char* pool_name, uint32_t flags);

/**
 * @brief 释放 RADOS IoCtx
 * @param ioctx IoCtx 句柄
 */
void rgw_rados_put_ioctx(void* ioctx);

/**
 * @brief 获取集群 ID
 * @param driver 驱动句柄
 * @param cluster_id 输出：集群 ID
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_rados_get_cluster_id(rgw_sal_driver_t* driver,
                                  char** cluster_id,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y);

/**
 * @brief 获取用户控制接口
 * @param driver 驱动句柄
 * @return 用户控制接口句柄
 */
void* rgw_sal_rados_get_user_ctl(rgw_sal_driver_t* driver);

/**
 * @brief 刷新统计数据
 * @param driver 驱动句柄
 * @param owner 所有者
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_rados_complete_flush_stats(rgw_sal_driver_t* driver,
                                        const rgw_sal_user_id_t* owner,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);

/**
 * @brief 获取内部 RadosUser 指针
 * @param user SAL 用户句柄
 * @return 内部 RadosUser* 指针
 */
void* rgw_sal_rados_user_get_internal(rgw_sal_user_t* user);

/**
 * @brief 从内部用户创建 SAL 用户
 * @param driver 驱动句柄
 * @param rados_user 内部 RadosUser 指针
 * @return SAL 用户句柄
 */
rgw_sal_user_t* rgw_sal_rados_user_from_internal(rgw_sal_driver_t* driver,
                                                  void* rados_user);

/**
 * @brief 获取内部 RadosBucket 指针
 * @param bucket SAL 桶句柄
 * @return 内部 RadosBucket* 指针
 */
void* rgw_sal_rados_bucket_get_internal(rgw_sal_bucket_t* bucket);

/**
 * @brief 从内部桶创建 SAL 桶
 * @param driver 驱动句柄
 * @param rados_bucket 内部 RadosBucket 指针
 * @return SAL 桶句柄
 */
rgw_sal_bucket_t* rgw_sal_rados_bucket_from_internal(rgw_sal_driver_t* driver,
                                                      void* rados_bucket);

/**
 * @brief 获取内部 RadosObject 指针
 * @param obj SAL 对象句柄
 * @return 内部 RadosObject* 指针
 */
void* rgw_sal_rados_object_get_internal(rgw_sal_object_t* obj);

/**
 * @brief 获取驱动名称
 * @param driver 驱动句柄
 * @return 驱动名称字符串
 */
const char* rgw_sal_rados_driver_get_name(rgw_sal_driver_t* driver);

/**
 * @brief 列出桶
 * @param driver 驱动句柄
 * @param owner 所有者
 * @param prefix 前缀过滤
 * @param delimiter 分隔符
 * @param marker 起始标记
 * @param end_marker 结束标记
 * @param max_keys 最大返回数量
 * @param list_all 是否列出所有
 * @param result 输出：桶列表
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_rados_list_buckets(rgw_sal_driver_t* driver,
                                rgw_sal_user_t* owner,
                                const char* prefix, const char* delimiter,
                                const char* marker, const char* end_marker,
                                uint32_t max_keys, bool list_all,
                                rgw_sal_bucket_list_t** result,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

#ifdef __cplusplus
}
#endif

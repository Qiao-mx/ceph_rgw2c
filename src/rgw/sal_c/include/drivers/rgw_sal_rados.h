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
 * RADOS 驱动特定类型
 *============================================================================*/

/* RADOS 上下文池类型 */
#define RGW_RADOS_CTX_POOL_BUCKETS_DATA 1
#define RGW_RADOS_CTX_POOL_BUCKETS_INDEX 2

/* 删除标志 */
#define RGW_SAL_DELETE_FLAG_EXPIRED 0x0001
#define RGW_SAL_DELETE_FLAG_VERSIONING 0x0002

/* 分片上传最大分片数 */
#define RGW_MAX_PART_NUMBER 10000

/* 前向声明 */
typedef struct rados_user_impl rados_user_impl_t;

/**
 * @brief RADOS 对象列表迭代器类型
 */
typedef void* rados_nobjects_list_t;

/**
 * @brief SAL 对象列表类型
 */
typedef struct rgw_sal_object_list {
    void** objects;
    size_t count;
    size_t capacity;
    bool is_truncated;
    char* next_marker;
} rgw_sal_object_list_t;

/**
 * @brief SAL 对象条目类型
 */
typedef struct rgw_sal_object_entry {
    char* name;
    char* instance;
    char* key;
    void* info;
} rgw_sal_object_entry_t;

/**
 * @brief RADOS 特定的用户扩展信息
 */
typedef struct rgw_sal_rados_user_impl {
    void* rados_user;           /**< 内部的 RadosUser* */
    bool user_info_loaded;      /**< 用户信息是否已加载 */
} rgw_sal_rados_user_impl_t;

/**
 * @brief RADOS 特定的桶扩展信息
 */
typedef struct rgw_sal_rados_bucket_impl {
    void* rados_bucket;         /**< 内部的 RadosBucket* */
    bool bucket_info_loaded;    /**< 桶信息是否已加载 */
} rgw_sal_rados_bucket_impl_t;

/**
 * @brief RADOS 特定的对象扩展信息
 */
typedef struct rgw_sal_rados_object_impl {
    void* rados_object;         /**< 内部的 RadosObject* */
    void* rados_ctx;            /**< 对象上下文 */
} rgw_sal_rados_object_impl_t;

/**
 * @brief RADOS 驱动特定扩展
 */
typedef struct rgw_sal_rados_driver_impl {
    void* rados_store;          /**< 内部的 RadosStore* */
    void* neorados;             /**< neorados 句柄 */
    void* rados;                /**< RGWRados* 句柄 */
} rgw_sal_rados_driver_impl_t;

/**
 * @brief 桶统计信息结构
 */
typedef struct rgw_sal_bucket_stats {
    int64_t actual_size;        /**< 实际大小 */
    int64_t size;               /**< 大小 */
    int64_t size_rounded;      /**< 对齐后大小 */
    int64_t num_objects;        /**< 对象数量 */
    int64_t size_bytes;        /**< 大小(字节) */
    int64_t size_kb;            /**< 大小(KB) */
    int64_t size_md;            /**< 大小(MD) */
    int64_t size_gb;            /**< 大小(GB) */
    int64_t size_tb;            /**< 大小(TB) */
    int64_t mtime;              /**< 修改时间 */
    int64_t object_count;      /**< 对象计数 */
    int num_shards;             /**< 分片数 */
    char max_marker[256];       /**< 最大 marker */
} rgw_sal_bucket_stats_t;

/*============================================================================
 * RADOS 驱动工厂函数
 *============================================================================*/

/**
 * @brief 创建 RADOS 驱动
 * @param cct Ceph 上下文
 * @param rados_handle RADOS 句柄 (neorados::RADOS)
 * @return 驱动句柄，失败返回 NULL
 */
rgw_sal_driver_t* rgw_sal_rados_driver_create(void* cct, void* neorados);

/**
 * @brief 获取 RADOS 驱动实现
 * @param driver SAL 驱动句柄
 * @return RADOS 驱动实现
 */
rgw_sal_rados_driver_impl_t* rgw_sal_rados_get_impl(rgw_sal_driver_t* driver);

/*============================================================================
 * RADOS 特定操作
 *============================================================================*/

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

/*============================================================================
 * RADOS 用户操作扩展
 *============================================================================*/

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

/*============================================================================
 * RADOS 桶操作扩展
 *============================================================================*/

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

/*============================================================================
 * RADOS 对象操作扩展
 *============================================================================*/

/**
 * @brief 获取内部 RadosObject 指针
 * @param obj SAL 对象句柄
 * @return 内部 RadosObject* 指针
 */
void* rgw_sal_rados_object_get_internal(rgw_sal_object_t* obj);

/**
 * @brief 从内部对象创建 SAL 对象
 * @param bucket 桶句柄
 * @param rados_object 内部 RadosObject 指针
 * @return SAL 对象句柄
 */
rgw_sal_object_t* rgw_sal_rados_object_from_internal(rgw_sal_bucket_t* bucket,
                                                        void* rados_object);

/**
 * @brief RADOS 读操作准备
 * @param obj 对象句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_rados_object_read_prepare(rgw_sal_object_t* obj,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);

/**
 * @brief RADOS 异步读操作
 * @param obj 对象句柄
 * @param offset 起始偏移
 * @param end 结束偏移
 * @param callback 回调函数
 * @param callback_arg 回调参数
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
typedef int (*rgw_sal_rados_read_callback_t)(void* arg, const uint8_t* data, size_t len);

int rgw_sal_rados_object_read_iterate(rgw_sal_object_t* obj,
                                        int64_t offset, int64_t end,
                                        rgw_sal_rados_read_callback_t callback,
                                        void* callback_arg,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y);

/**
 * @brief RADOS 获取对象属性
 * @param obj 对象句柄
 * @param name 属性名
 * @param value 输出：属性值
 * @param value_len 输出：值长度
 * @param y 协程上下文
 * @param dpp 调试前缀提供者
 * @return 错误码
 */
int rgw_sal_rados_object_get_attr(rgw_sal_object_t* obj,
                                    const char* name,
                                    uint8_t** value, size_t* value_len,
                                    rgw_sal_yield_t* y,
                                    const rgw_sal_dpp_t* dpp);

/*============================================================================
 * 用户序列化函数
 *============================================================================*/

/**
 * @brief 解析用户数据缓冲区
 *
 * @param impl 用户实现
 * @param data 缓冲区数据
 * @param data_len 缓冲区长度
 * @return 错误码
 */
int parse_user_from_buffer(rados_user_impl_t* impl, const uint8_t* data, size_t data_len);

/**
 * @brief 将用户数据序列化为缓冲区
 *
 * @param impl 用户实现
 * @param buf_size 输出：缓冲区大小
 * @return 序列化的缓冲区，失败返回 NULL
 */
uint8_t* serialize_user_to_buffer(rados_user_impl_t* impl, size_t* buf_size);

/**
 * @brief 释放序列化缓冲区
 *
 * @param buffer 缓冲区
 */
void rgw_sal_free_buffer(uint8_t* buffer);

/*============================================================================
 * VTable 类型定义
 *============================================================================*/

/* 前向声明 */
struct rgw_sal_driver;
struct rgw_sal_user;
struct rgw_sal_bucket;
struct rgw_sal_object;

/**
 * @brief 驱动 vtable
 */
typedef struct rgw_sal_driver_vtable {
    void (*destroy)(struct rgw_sal_driver* driver);
    int (*initialize)(struct rgw_sal_driver* driver, void* cct, void* dpp);
    const char* (*get_name)(const struct rgw_sal_driver* driver);
    int (*get_cluster_id)(struct rgw_sal_driver* driver, char** cluster_id, void* dpp, void* y);
    rgw_sal_user_t* (*get_user)(struct rgw_sal_driver* driver, const rgw_sal_user_id_t* uid);
    int (*get_user_by_access_key)(struct rgw_sal_driver* driver, const char* key, rgw_sal_user_t** user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_email)(struct rgw_sal_driver* driver, const char* email, rgw_sal_user_t** user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_swift)(struct rgw_sal_driver* driver, const char* swift_user, rgw_sal_user_t** user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_bucket)(struct rgw_sal_driver* driver, const rgw_user_t* user, const char* bucket_name, rgw_sal_bucket_t** bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*list_buckets)(struct rgw_sal_driver* driver, const rgw_sal_user_t* user, const char* marker, const char* prefix, uint32_t max_keys, bool force, rgw_sal_bucket_list_t** list, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    rgw_sal_object_t* (*get_object)(struct rgw_sal_driver* driver, struct rgw_sal_bucket* bucket, const struct rgw_sal_obj_key* key);
} rgw_sal_driver_vtable_t;

/**
 * @brief 用户 vtable
 */
typedef struct rgw_sal_user_vtable {
    void* (*clone)(const struct rgw_sal_user* user);
    void (*destroy)(struct rgw_sal_user* user);
    const char* (*get_id)(const struct rgw_sal_user* user);
    const char* (*get_display_name)(const struct rgw_sal_user* user);
    int (*set_display_name)(struct rgw_sal_user* user, const char* name);
    const char* (*get_tenant)(const struct rgw_sal_user* user);
    uint32_t (*get_type)(const struct rgw_sal_user* user);
    int32_t (*get_max_buckets)(const struct rgw_sal_user* user);
    int (*set_max_buckets)(struct rgw_sal_user* user, int32_t max_buckets);
    void* (*get_attrs)(struct rgw_sal_user* user);
    int (*set_attrs)(struct rgw_sal_user* user, void* attrs);
    int (*load)(struct rgw_sal_user* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*store)(struct rgw_sal_user* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y, bool exclusive);
    int (*remove)(struct rgw_sal_user* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*read_attrs)(struct rgw_sal_user* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*merge_and_store_attrs)(struct rgw_sal_user* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    const char* (*get_ns)(const struct rgw_sal_user* user);
    int (*set_ns)(struct rgw_sal_user* user, const char* ns);
    void (*clear_ns)(struct rgw_sal_user* user);
    /* 额外函数 */
    int (*set_info)(struct rgw_sal_user* user, const void* info);
    int (*get_info)(struct rgw_sal_user* user, void** info);
    const void* (*get_caps)(const struct rgw_sal_user* user);
    const void* (*get_version_tracker)(const struct rgw_sal_user* user);
    int (*read_usage)(struct rgw_sal_user* user, const char* start_date, const char* end_date, uint32_t max_entries, bool* is_truncated, void* iter, void* y);
    int (*trim_usage)(struct rgw_sal_user* user, const char* start_date, const char* end_date);
    int (*verify_mfa)(struct rgw_sal_user* user, const char* mfa_token, bool* verified, const rgw_sal_dpp_t* dpp);
    int (*list_groups)(struct rgw_sal_user* user, const char* marker, uint32_t max_groups, void** groups);
} rgw_sal_user_vtable_t;

/**
 * @brief 桶 vtable
 */
typedef struct rgw_sal_bucket_vtable {
    void* (*clone)(const struct rgw_sal_bucket* bucket);
    void (*destroy)(struct rgw_sal_bucket* bucket);
    const char* (*get_name)(const struct rgw_sal_bucket* bucket);
    const char* (*get_tenant)(const struct rgw_sal_bucket* bucket);
    const char* (*get_marker)(const struct rgw_sal_bucket* bucket);
    void* (*get_info)(struct rgw_sal_bucket* bucket);
    void* (*get_owner)(struct rgw_sal_bucket* bucket);
    void* (*get_attrs)(struct rgw_sal_bucket* bucket);
    int (*set_attrs)(struct rgw_sal_bucket* bucket, void* attrs);
    int (*list)(struct rgw_sal_bucket* bucket, void* dpp, void* y, const char* prefix, const char* delimiter, const char* marker, uint32_t max_keys, void** list);
    int (*load)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* objv);
    int (*store)(struct rgw_sal_bucket* bucket, void* dpp, void* y, bool exclusive, void* objv);
    int (*remove)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* objv);
    int (*create)(struct rgw_sal_bucket* bucket, void* dpp, void* y, bool exclusive, void* objv);
    int (*delete_bucket)(struct rgw_sal_bucket* bucket, void* dpp, void* y, bool delete_children, void* objv);
    int (*rename)(struct rgw_sal_bucket* bucket, void* dpp, void* y, const char* new_bucket_name, void* objv);
    int (*set_acl)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* acl);
    int (*get_policy)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void** policy);
    int (*set_policy)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* policy);
    /* 额外函数 */
    const char* (*get_tag)(const struct rgw_sal_bucket* bucket);
    int (*set_tag)(struct rgw_sal_bucket* bucket, const char* tag);
    int (*get_usage)(struct rgw_sal_bucket* bucket, uint32_t* rgw_usage_num_entries, void* y);
    int (*read_stats)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* stats);
    int (*read_stats_async)(struct rgw_sal_bucket* bucket, void* cb, void* args);
    int (*complete_stats)(struct rgw_sal_bucket* bucket, void* dpp, void* y);
    int (*sync)(struct rgw_sal_bucket* bucket, void* dpp, void* y);
    int (*drain)(struct rgw_sal_bucket* bucket, void* dpp, void* y);
    int (*check_object_index)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void** list);
    int (*fix_object_index)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* list);
    int (*check_bucket_index)(struct rgw_sal_bucket* bucket, void* dpp, void* y, void* list);
} rgw_sal_bucket_vtable_t;

/**
 * @brief 对象 vtable
 */
typedef struct rgw_sal_object_vtable {
    void* (*clone)(const struct rgw_sal_object* obj);
    void (*destroy)(struct rgw_sal_object* obj);
    const char* (*get_name)(const struct rgw_sal_object* obj);
    const char* (*get_instance)(const struct rgw_sal_object* obj);
    bool (*is_null)(const struct rgw_sal_object* obj);
    void* (*get_attrs)(struct rgw_sal_object* obj);
    int (*set_attrs)(struct rgw_sal_object* obj, void* attrs);
    int (*read)(struct rgw_sal_object* obj, void* dpp, void* y, uint64_t offset, size_t len, uint8_t* buf, size_t* bytes_read);
    int (*write)(struct rgw_sal_object* obj, void* dpp, void* y, uint64_t offset, size_t len, const uint8_t* buf, size_t* bytes_written);
    int (*delete_obj)(struct rgw_sal_object* obj, void* dpp, void* y, uint32_t flags);
    int (*load_state)(struct rgw_sal_object* obj, void* dpp, void* y);
    int (*get_obj_attrs)(struct rgw_sal_object* obj, void* dpp, void* y);
    int (*set_obj_attrs)(struct rgw_sal_object* obj, void* dpp, void* y, void* attrs);
    bool (*is_atomic)(const struct rgw_sal_object* obj);
    void (*set_atomic)(struct rgw_sal_object* obj);
    bool (*is_expired)(const struct rgw_sal_object* obj);
} rgw_sal_object_vtable_t;

#ifdef __cplusplus
}
#endif

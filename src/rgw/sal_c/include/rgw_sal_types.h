/**
 * @file rgw_sal_types.h
 * @brief SAL C 接口核心类型定义
 *
 * 定义存储抽象层 (SAL) C 接口的核心数据类型。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 前向声明
 *============================================================================*/

typedef struct rgw_sal_driver rgw_sal_driver_t;
typedef struct rgw_sal_user rgw_sal_user_t;
typedef struct rgw_sal_bucket rgw_sal_bucket_t;
typedef struct rgw_sal_object rgw_sal_object_t;
typedef struct rgw_sal_attrs rgw_sal_attrs_t;
typedef struct rgw_sal_yield rgw_sal_yield_t;

/*============================================================================
 * 用户标识类型
 *============================================================================*/

/**
 * @brief 用户标识
 */
typedef struct rgw_sal_user_id {
    char* id;           /**< 用户 ID */
    char* tenant;       /**< 租户 */
    char* ns;           /**< 命名空间 */
    uint32_t type;      /**< 用户类型 */
} rgw_sal_user_id_t;

/**
 * @brief 用户信息
 */
typedef struct rgw_sal_user_info {
    rgw_sal_user_id_t user_id;
    char* display_name;
    char* email;
    uint32_t user_type;
    int32_t max_buckets;
    uint32_t permissions;
    // TODO: 添加更多字段
} rgw_sal_user_info_t;

/*============================================================================
 * 桶类型
 *============================================================================*/

/**
 * @brief 桶标识
 */
typedef struct rgw_sal_bucket_id {
    char* name;         /**< 桶名称 */
    char* tenant;       /**< 租户 */
    char* marker;       /**< 桶标记 */
    char* bucket_id;    /**< 桶 ID */
} rgw_sal_bucket_id_t;

/**
 * @brief 桶信息
 */
typedef struct rgw_sal_bucket_info {
    rgw_sal_bucket_id_t bucket;
    rgw_sal_user_id_t owner;
    char* zone_group;
    uint32_t placement_rule;
    uint64_t size;
    uint64_t size_rounded;
    uint32_t object_count;
    // TODO: 添加更多字段
} rgw_sal_bucket_info_t;

/*============================================================================
 * 对象类型
 *============================================================================*/

/**
 * @brief 对象键
 */
typedef struct rgw_sal_obj_key {
    char* name;         /**< 对象名称 */
    char* instance;     /**< 版本 ID */
    bool is_null;       /**< 是否为空 */
    bool is_current;    /**< 是否为当前版本 */
} rgw_sal_obj_key_t;

/**
 * @brief 对象标识
 */
typedef struct rgw_sal_object_id {
    rgw_sal_bucket_id_t bucket;
    rgw_sal_obj_key_t key;
} rgw_sal_object_id_t;

/*============================================================================
 * 属性映射
 *============================================================================*/

/**
 * @brief 属性键值对
 */
typedef struct rgw_sal_attr_pair {
    char* key;
    uint8_t* value;
    size_t value_len;
} rgw_sal_attr_pair_t;

/**
 * @brief 属性映射
 *
 * 使用动态数组存储键值对
 */
struct rgw_sal_attrs {
    rgw_sal_attr_pair_t* pairs;
    size_t count;
    size_t capacity;
};

/*============================================================================
 * 列表结果
 *============================================================================*/

/**
 * @brief 桶列表结果
 */
typedef struct rgw_sal_bucket_list {
    rgw_sal_bucket_info_t** buckets;
    size_t count;
    char* marker;
    bool is_truncated;
} rgw_sal_bucket_list_t;

/**
 * @brief 对象列表结果
 */
typedef struct rgw_sal_object_list {
    rgw_sal_object_id_t** objects;
    size_t count;
    char* delimiter;
    char* prefix;
    char* marker;
    char* next_marker;
    bool is_truncated;
} rgw_sal_object_list_t;

/*============================================================================
 * 上下文和句柄
 *============================================================================*/

/**
 * @brief 协程上下文 (简化版)
 *
 * 实际实现中需要与 Ceph 的 optional_yield 对应
 */
struct rgw_sal_yield {
    void* opaque;  /**< 底层实现句柄 */
};

/**
 * @brief 调试前缀提供者
 */
typedef struct rgw_sal_dpp {
    const char* subsys;    /**< 子系统名称 */
    int level;             /**< 日志级别 */
    void* ceph_dpp;        /**< Ceph DPP 句柄 */
} rgw_sal_dpp_t;

/*============================================================================
 * 内存管理
 *============================================================================*/

/**
 * @brief 创建用户 ID
 * @return 分配的用户 ID 结构
 */
rgw_sal_user_id_t* rgw_sal_user_id_create(void);

/**
 * @brief 销毁用户 ID
 * @param uid 要销毁的用户 ID
 */
void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid);

/**
 * @brief 创建桶 ID
 * @return 分配的桶 ID 结构
 */
rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void);

/**
 * @brief 销毁桶 ID
 * @param bid 要销毁的桶 ID
 */
void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid);

/**
 * @brief 创建对象键
 * @return 分配的对象键结构
 */
rgw_sal_obj_key_t* rgw_sal_obj_key_create(void);

/**
 * @brief 销毁对象键
 * @param key 要销毁的对象键
 */
void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key);

/**
 * @brief 创建属性映射
 * @return 分配的属性映射结构
 */
rgw_sal_attrs_t* rgw_sal_attrs_create(void);

/**
 * @brief 销毁属性映射
 * @param attrs 要销毁的属性映射
 */
void rgw_sal_attrs_destroy(rgw_sal_attrs_t* attrs);

/**
 * @brief 设置属性
 * @param attrs 属性映射
 * @param key 键
 * @param value 值
 * @param value_len 值长度
 * @return 错误码
 */
int rgw_sal_attrs_set(rgw_sal_attrs_t* attrs, const char* key,
                      const uint8_t* value, size_t value_len);

/**
 * @brief 获取属性
 * @param attrs 属性映射
 * @param key 键
 * @param value 输出：值
 * @param value_len 输出：值长度
 * @return 错误码
 */
int rgw_sal_attrs_get(const rgw_sal_attrs_t* attrs, const char* key,
                     uint8_t** value, size_t* value_len);

#ifdef __cplusplus
}
#endif

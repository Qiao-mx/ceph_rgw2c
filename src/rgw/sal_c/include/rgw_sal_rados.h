/**
 * @file rgw_sal_rados.h
 * @brief RADOS 驱动 C 接口适配器
 *
 * 提供 RADOS 存储后端的 C 语言接口适配器。
 * 内部实现仍然使用 C++，但提供 C 接口供 SAL C 层调用。
 */

#pragma once

#include "rgw_sal_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * RADOS 驱动特定类型
 *============================================================================*/

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

#ifdef __cplusplus
}
#endif

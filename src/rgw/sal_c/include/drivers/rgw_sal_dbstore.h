/**
 * @file rgw_sal_dbstore.h
 * @brief DBStore 驱动 C 接口适配器
 *
 * 提供 DBStore (SQLite) 存储后端的 C 语言接口适配器。
 */

#pragma once

#include "rgw_sal_c.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * DBStore 驱动特定类型
 *============================================================================*/

/**
 * @brief DBStore 特定的用户扩展信息
 */
typedef struct rgw_sal_dbstore_user_impl {
    void* dbstore_user;           /**< 内部的 DBStoreUser* */
    bool user_info_loaded;         /**< 用户信息是否已加载 */
} rgw_sal_dbstore_user_impl_t;

/**
 * @brief DBStore 特定的桶扩展信息
 */
typedef struct rgw_sal_dbstore_bucket_impl {
    void* dbstore_bucket;         /**< 内部的 DBStoreBucket* */
    bool bucket_info_loaded;      /**< 桶信息是否已加载 */
} rgw_sal_dbstore_bucket_impl_t;

/**
 * @brief DBStore 特定的对象扩展信息
 */
typedef struct rgw_sal_dbstore_object_impl {
    void* dbstore_object;         /**< 内部的 DBStoreObject* */
    void* dbstore_ctx;            /**< 对象上下文 */
} rgw_sal_dbstore_object_impl_t;

/**
 * @brief DBStore 驱动特定扩展
 */
typedef struct rgw_sal_dbstore_driver_impl {
    void* dbstore;                /**< 内部的 DBStore* */
    void* db_handle;              /**< SQLite 数据库句柄 */
    char db_path[256];            /**< 数据库文件路径 */
    bool initialized;              /**< 是否已初始化 */
} rgw_sal_dbstore_driver_impl_t;

/*============================================================================
 * DBStore 驱动工厂函数
 *============================================================================*/

/**
 * @brief 创建 DBStore 驱动
 * @param db_path SQLite 数据库文件路径
 * @return 驱动句柄，失败返回 NULL
 */
rgw_sal_driver_t* rgw_sal_dbstore_driver_create(const char* db_path);

/**
 * @brief 获取 DBStore 驱动实现
 * @param driver SAL 驱动句柄
 * @return DBStore 驱动实现
 */
rgw_sal_dbstore_driver_impl_t* rgw_sal_dbstore_get_impl(rgw_sal_driver_t* driver);

/*============================================================================
 * DBStore 特定操作
 *============================================================================*/

/**
 * @brief 初始化 DBStore 数据库
 * @param driver 驱动句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_dbstore_init_db(rgw_sal_driver_t* driver,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y);

/**
 * @brief 关闭 DBStore 数据库
 * @param driver 驱动句柄
 * @return 错误码
 */
int rgw_sal_dbstore_shutdown_db(rgw_sal_driver_t* driver);

/**
 * @brief 获取用户控制接口
 * @param driver 驱动句柄
 * @return 用户控制接口句柄
 */
void* rgw_sal_dbstore_get_user_ctl(rgw_sal_driver_t* driver);

#ifdef __cplusplus
}
#endif

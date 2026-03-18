/**
 * @file rgw_sal.h
 * @brief SAL C 接口 - 存储抽象层 C 语言接口
 *
 * 本文件定义了 RGW 存储抽象层 (SAL) 的 C 语言接口，
 * 用于将原有的 C++ SAL 实现转换为 C 接口。
 */

#pragma once

#include "rgw_sal_errors.h"
#include "rgw_sal_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 版本信息
 *============================================================================*/

#define RGW_SAL_VERSION_MAJOR 1
#define RGW_SAL_VERSION_MINOR 0
#define RGW_SAL_VERSION_PATCH 0

/*============================================================================
 * 虚函数表定义
 *============================================================================*/

/**
 * @brief 用户操作虚函数表
 */
typedef struct rgw_sal_user_vtable {
    /* 生命周期 */
    void* (*clone)(const rgw_sal_user_t* user);
    void (*destroy)(rgw_sal_user_t* user);

    /* 属性访问 */
    const char* (*get_id)(const rgw_sal_user_t* user);
    const char* (*get_display_name)(rgw_sal_user_t* user);
    int (*set_display_name)(rgw_sal_user_t* user, const char* name);
    const char* (*get_tenant)(const rgw_sal_user_t* user);
    uint32_t (*get_type)(const rgw_sal_user_t* user);
    int32_t (*get_max_buckets)(const rgw_sal_user_t* user);
    void (*set_max_buckets)(rgw_sal_user_t* user, int32_t max);

    /* 属性映射 */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_user_t* user);
    int (*set_attrs)(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs);

    /* 持久化操作 */
    int (*load)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*store)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                 rgw_sal_yield_t* y, bool exclusive);
    int (*remove)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 属性读取/写入 */
    int (*read_attrs)(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*merge_and_store_attrs)(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
} rgw_sal_user_vtable_t;

/**
 * @brief 桶操作虚函数表
 */
typedef struct rgw_sal_bucket_vtable {
    /* 生命周期 */
    void* (*clone)(const rgw_sal_bucket_t* bucket);
    void (*destroy)(rgw_sal_bucket_t* bucket);

    /* 属性访问 */
    const char* (*get_name)(const rgw_sal_bucket_t* bucket);
    const char* (*get_tenant)(const rgw_sal_bucket_t* bucket);
    const char* (*get_marker)(const rgw_sal_bucket_t* bucket);
    rgw_sal_bucket_info_t* (*get_info)(rgw_sal_bucket_t* bucket);
    rgw_sal_user_t* (*get_owner)(rgw_sal_bucket_t* bucket);

    /* 属性映射 */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_bucket_t* bucket);
    int (*set_attrs)(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs);

    /* 对象列表 */
    int (*list)(rgw_sal_bucket_t* bucket,
                const char* prefix, const char* delimiter,
                const char* marker, const char* end_marker,
                uint32_t max_keys, bool list_versions,
                rgw_sal_object_list_t** result,
                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 持久化操作 */
    int (*load)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*store)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                 rgw_sal_yield_t* y, bool exclusive);
    int (*remove)(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
} rgw_sal_bucket_vtable_t;

/**
 * @brief 对象操作虚函数表
 */
typedef struct rgw_sal_object_vtable {
    /* 生命周期 */
    void* (*clone)(const rgw_sal_object_t* obj);
    void (*destroy)(rgw_sal_object_t* obj);

    /* 属性访问 */
    const char* (*get_name)(const rgw_sal_object_t* obj);
    const char* (*get_instance)(const rgw_sal_object_t* obj);
    bool (*is_null)(const rgw_sal_object_t* obj);

    /* 属性映射 */
    rgw_sal_attrs_t* (*get_attrs)(rgw_sal_object_t* obj);
    int (*set_attrs)(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs);

    /* 读操作 */
    int (*read)(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                uint8_t* buffer, size_t* buffer_size,
                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 写操作 */
    int (*write)(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                 const uint8_t* data, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 删除操作 */
    int (*delete_obj)(rgw_sal_object_t* obj, uint32_t flags,
                      const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 持久化操作 */
    int (*load_state)(rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp,
                      rgw_sal_yield_t* y, bool follow_olh);
    int (*get_obj_attrs)(rgw_sal_object_t* obj, rgw_sal_yield_t* y,
                          const rgw_sal_dpp_t* dpp);
    int (*set_obj_attrs)(rgw_sal_object_t* obj, rgw_sal_attrs_t* setattrs,
                          rgw_sal_attrs_t* delattrs, rgw_sal_yield_t* y,
                          uint32_t flags);
} rgw_sal_object_vtable_t;

/**
 * @brief 驱动虚函数表
 */
typedef struct rgw_sal_driver_vtable {
    /* 生命周期 */
    void (*destroy)(rgw_sal_driver_t* driver);

    /* 初始化 */
    int (*initialize)(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp);

    /* 元数据 */
    const char* (*get_name)(const rgw_sal_driver_t* driver);
    int (*get_cluster_id)(rgw_sal_driver_t* driver, char** cluster_id,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 用户操作 */
    rgw_sal_user_t* (*get_user)(rgw_sal_driver_t* driver, const rgw_sal_user_id_t* uid);
    int (*get_user_by_access_key)(rgw_sal_driver_t* driver, const char* key,
                                   rgw_sal_user_t** user,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_email)(rgw_sal_driver_t* driver, const char* email,
                              rgw_sal_user_t** user,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);
    int (*get_user_by_swift)(rgw_sal_driver_t* driver, const char* user_str,
                              rgw_sal_user_t** user,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 桶操作 */
    rgw_sal_bucket_t* (*get_bucket)(rgw_sal_driver_t* driver,
                                    const rgw_sal_bucket_info_t* info);
    int (*list_buckets)(rgw_sal_driver_t* driver,
                        rgw_sal_user_t* owner,
                        const char* prefix, const char* delimiter,
                        const char* marker, const char* end_marker,
                        uint32_t max_keys, bool list_all,
                        rgw_sal_bucket_list_t** result,
                        const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

    /* 对象操作 */
    rgw_sal_object_t* (*get_object)(rgw_sal_driver_t* driver,
                                     rgw_sal_bucket_t* bucket,
                                     const rgw_sal_obj_key_t* key);
} rgw_sal_driver_vtable_t;

/*============================================================================
 * 核心结构体定义
 *============================================================================*/

/**
 * @brief 用户实体
 */
struct rgw_sal_user {
    const rgw_sal_user_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_driver_t* driver;             /**< 所属驱动 */
};

/**
 * @brief 桶实体
 */
struct rgw_sal_bucket {
    const rgw_sal_bucket_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_driver_t* driver;             /**< 所属驱动 */
};

/**
 * @brief 对象实体
 */
struct rgw_sal_object {
    const rgw_sal_object_vtable_t* vtable;
    void* impl;                          /**< 驱动特定实现 */
    rgw_sal_bucket_t* bucket;             /**< 所属桶 */
};

/**
 * @brief 驱动实体
 */
struct rgw_sal_driver {
    const rgw_sal_driver_vtable_t* vtable;
    const rgw_sal_user_vtable_t* user_vtable;    /**< 用户操作 vtable */
    const rgw_sal_bucket_vtable_t* bucket_vtable; /**< 桶操作 vtable */
    const rgw_sal_object_vtable_t* object_vtable;  /**< 对象操作 vtable */
    void* impl;                          /**< 驱动特定实现 */
    char name[64];                       /**< 驱动名称 */
};

/*============================================================================
 * 驱动创建/销毁
 *============================================================================*/

/**
 * @brief 创建 SAL 驱动
 * @param driver_name 驱动名称 (如 "rados", "dbstore", "posix")
 * @param cct Ceph 上下文
 * @return 创建的驱动句柄，失败返回 NULL
 */
rgw_sal_driver_t* rgw_sal_create_driver(const char* driver_name, void* cct);

/**
 * @brief 销毁 SAL 驱动
 * @param driver 要销毁的驱动
 */
void rgw_sal_destroy_driver(rgw_sal_driver_t* driver);

/*============================================================================
 * 驱动初始化
 *============================================================================*/

/**
 * @brief 初始化 SAL 驱动
 * @param driver 驱动句柄
 * @param cct Ceph 上下文
 * @param dpp 调试前缀提供者
 * @return 错误码
 */
int rgw_sal_init_driver(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp);

/**
 * @brief 获取驱动名称
 * @param driver 驱动句柄
 * @return 驱动名称
 */
const char* rgw_sal_get_driver_name(const rgw_sal_driver_t* driver);

/*============================================================================
 * 用户操作
 *============================================================================*/

/**
 * @brief 获取用户
 * @param driver 驱动句柄
 * @param uid 用户 ID
 * @return 用户句柄
 */
rgw_sal_user_t* rgw_sal_get_user(rgw_sal_driver_t* driver,
                                   const rgw_sal_user_id_t* uid);

/**
 * @brief 通过 access_key 获取用户
 * @param driver 驱动句柄
 * @param key access_key
 * @param user 输出：用户句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key,
                                    rgw_sal_user_t** user,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 通过 email 获取用户
 * @param driver 驱动句柄
 * @param email 邮箱地址
 * @param user 输出：用户句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_get_user_by_email(rgw_sal_driver_t* driver, const char* email,
                               rgw_sal_user_t** user,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 加载用户
 * @param user 用户句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_user_load(rgw_sal_user_t* user,
                       const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 存储用户
 * @param user 用户句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @param exclusive 是否独占创建
 * @return 错误码
 */
int rgw_sal_user_store(rgw_sal_user_t* user,
                        const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y,
                        bool exclusive);

/**
 * @brief 删除用户
 * @param user 用户句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_user_remove(rgw_sal_user_t* user,
                         const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 销毁用户
 * @param user 用户句柄
 */
void rgw_sal_user_destroy(rgw_sal_user_t* user);

/*============================================================================
 * 桶操作
 *============================================================================*/

/**
 * @brief 获取桶
 * @param driver 驱动句柄
 * @param info 桶信息
 * @return 桶句柄
 */
rgw_sal_bucket_t* rgw_sal_get_bucket(rgw_sal_driver_t* driver,
                                        const rgw_sal_bucket_info_t* info);

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
int rgw_sal_list_buckets(rgw_sal_driver_t* driver,
                          rgw_sal_user_t* owner,
                          const char* prefix, const char* delimiter,
                          const char* marker, const char* end_marker,
                          uint32_t max_keys, bool list_all,
                          rgw_sal_bucket_list_t** result,
                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 创建桶
 * @param driver 驱动句柄
 * @param name 桶名称
 * @param owner 所有者
 * @param bucket 输出：创建的桶
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_create_bucket(rgw_sal_driver_t* driver,
                           const char* name,
                           rgw_sal_user_t* owner,
                           rgw_sal_bucket_t** bucket,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 删除桶
 * @param bucket 桶句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_bucket_remove(rgw_sal_bucket_t* bucket,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 销毁桶
 * @param bucket 桶句柄
 */
void rgw_sal_bucket_destroy(rgw_sal_bucket_t* bucket);

/*============================================================================
 * 对象操作
 *============================================================================*/

/**
 * @brief 获取对象
 * @param driver 驱动句柄
 * @param bucket 所属桶
 * @param key 对象键
 * @return 对象句柄
 */
rgw_sal_object_t* rgw_sal_get_object(rgw_sal_driver_t* driver,
                                      rgw_sal_bucket_t* bucket,
                                      const rgw_sal_obj_key_t* key);

/**
 * @brief 读取对象
 * @param obj 对象句柄
 * @param offset 起始偏移
 * @param end 结束偏移 (-1 表示读取到末尾)
 * @param buffer 输出缓冲区
 * @param buffer_size 输入：缓冲区大小，输出：实际读取字节数
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_object_read(rgw_sal_object_t* obj,
                         int64_t offset, int64_t end,
                         uint8_t* buffer, size_t* buffer_size,
                         const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 写入对象
 * @param obj 对象句柄
 * @param offset 起始偏移
 * @param size 数据大小
 * @param data 数据
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_object_write(rgw_sal_object_t* obj,
                          int64_t offset, int64_t size,
                          const uint8_t* data,
                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 删除对象
 * @param obj 对象句柄
 * @param flags 标志
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
int rgw_sal_object_delete(rgw_sal_object_t* obj, uint32_t flags,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y);

/**
 * @brief 销毁对象
 * @param obj 对象句柄
 */
void rgw_sal_object_destroy(rgw_sal_object_t* obj);

/*============================================================================
 * 列表结果操作
 *============================================================================*/

/**
 * @brief 销毁桶列表
 * @param list 桶列表
 */
void rgw_sal_bucket_list_destroy(rgw_sal_bucket_list_t* list);

/**
 * @brief 销毁对象列表
 * @param list 对象列表
 */
void rgw_sal_object_list_destroy(rgw_sal_object_list_t* list);

#ifdef __cplusplus
}
#endif

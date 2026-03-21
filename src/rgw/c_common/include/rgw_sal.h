/**
 * @file rgw_sal.h
 * @brief RGW 存储抽象层 (SAL) C 接口
 *
 * 本文件提供 SAL 的 C 语言接口，用于替代原 C++ 版本的 rgw_sal。
 *
 * 核心组件：
 * - rgw_sal_driver_t: 存储驱动抽象
 * - rgw_sal_user_t: 用户抽象
 * - rgw_sal_bucket_t: 桶抽象
 * - rgw_sal_object_t: 对象抽象
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-17
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SAL 版本 */
#define RGW_SAL_VERSION 1

/* 错误码 */
typedef enum rgw_sal_error {
    RGW_SAL_OK = 0,
    RGW_SAL_ERR_INVALID = -1,
    RGW_SAL_ERR_NO_MEMORY = -2,
    RGW_SAL_ERR_NOT_FOUND = -3,
    RGW_SAL_ERR_EXISTS = -4,
    RGW_SAL_ERR_PERMISSION_DENIED = -5,
    RGW_SAL_ERR_ABORTED = -6,
    RGW_SAL_ERR_IO = -7
} rgw_sal_error_t;

/* 前向声明 */
typedef struct rgw_sal_driver rgw_sal_driver_t;
typedef struct rgw_sal_user rgw_sal_user_t;
typedef struct rgw_sal_bucket rgw_sal_bucket_t;
typedef struct rgw_sal_object rgw_sal_object_t;
typedef struct rgw_sal_attrs rgw_sal_attrs_t;
typedef struct rgw_sal_bucket_list rgw_sal_bucket_list_t;

/* 用户 ID 结构 */
typedef struct rgw_user {
    char *tenant;
    char *id;
    char *swift_name;
    char *swift_subuser;
} rgw_user_t;

/**
 * @brief 创建 rgw_user
 */
rgw_user_t *rgw_user_create(const char *tenant, const char *id);

/**
 * @brief 释放 rgw_user
 */
void rgw_user_destroy(rgw_user_t *user);

/**
 * @brief 复制 rgw_user
 */
rgw_user_t *rgw_user_copy(const rgw_user_t *user);

/**
 * @brief 比较两个用户是否相等
 */
int rgw_user_equal(const rgw_user_t *a, const rgw_user_t *b);

/**
 * @brief 用户信息结构
 */
typedef struct rgw_user_info {
    rgw_user_t *user_id;
    char *display_name;
    char *user_email;
    char *swift_subuser;
    uint32_t max_buckets;
    int32_t suspended;
    uint32_t op_mask;
    char *admin_token;
    uint32_t default_placement;
    char *default_storage_class;
    char *placement_tags;
    uint32_t bucket_quota;
    uint32_t user_quota;
    uint32_t temp_url_key;
    /* ... 更多字段可扩展 */
} rgw_user_info_t;

/**
 * @brief 创建 rgw_user_info
 */
rgw_user_info_t *rgw_user_info_create(void);

/**
 * @brief 释放 rgw_user_info
 */
void rgw_user_info_destroy(rgw_user_info_t *info);

/* 属性映射 */
struct rgw_sal_attrs {
    void *data;  /* 内部实现: 使用 rgw_cmap */
};

/**
 * @brief 创建属性映射
 */
rgw_sal_attrs_t *rgw_sal_attrs_create(void);

/**
 * @brief 释放属性映射
 */
void rgw_sal_attrs_destroy(rgw_sal_attrs_t *attrs);

/**
 * @brief 设置属性
 */
int rgw_sal_attrs_set(rgw_sal_attrs_t *attrs, const char *key, const uint8_t *value, size_t len);

/**
 * @brief 获取属性
 */
const uint8_t *rgw_sal_attrs_get(const rgw_sal_attrs_t *attrs, const char *key, size_t *len);

/**
 * @brief 删除属性
 */
int rgw_sal_attrs_del(rgw_sal_attrs_t *attrs, const char *key);

/**
 * @brief 克隆属性映射
 *
 * @param attrs 要克隆的属性映射
 * @return 克隆的属性映射，失败返回 NULL
 */
rgw_sal_attrs_t *rgw_sal_attrs_clone(rgw_sal_attrs_t *attrs);

/**
 * @brief 桶列表结果
 */
struct rgw_sal_bucket_list {
    void *buckets;  /* 内部实现: 使用容器 */
    char *next_marker;
    bool truncated;
};

/**
 * @brief 创建桶列表
 */
rgw_sal_bucket_list_t *rgw_sal_bucket_list_create(void);

/**
 * @brief 释放桶列表
 */
void rgw_sal_bucket_list_destroy(rgw_sal_bucket_list_t *list);

/**
 * @brief 获取桶列表大小
 */
size_t rgw_sal_bucket_list_size(const rgw_sal_bucket_list_t *list);

/**
 * @brief 获取下一个 marker
 */
const char *rgw_sal_bucket_list_next_marker(const rgw_sal_bucket_list_t *list);

/**
 * @brief 存储驱动操作接口
 */
typedef struct rgw_sal_driver_ops {
    /* 驱动生命周期 */
    int (*initialize)(rgw_sal_driver_t *driver, void *cct, void *dpp);
    const char *(*get_name)(const rgw_sal_driver_t *driver);
    
    /* 用户操作 */
    int (*get_user)(rgw_sal_driver_t *driver, const rgw_user_t *user_id, rgw_sal_user_t **user);
    int (*get_user_by_access_key)(rgw_sal_driver_t *driver, void *dpp, const char *key, void *y, rgw_sal_user_t **user);
    int (*get_user_by_email)(rgw_sal_driver_t *driver, void *dpp, const char *email, void *y, rgw_sal_user_t **user);
    int (*load_user)(rgw_sal_driver_t *driver, void *dpp, void *y, const rgw_user_t *user_id, rgw_user_info_t *info, rgw_sal_attrs_t *attrs, void *objv);
    int (*store_user)(rgw_sal_driver_t *driver, void *dpp, void *y, bool exclusive, const rgw_user_info_t *info, const rgw_user_info_t *old_info, const rgw_sal_attrs_t *attrs, void *objv);
    int (*delete_user)(rgw_sal_driver_t *driver, void *dpp, void *y, const rgw_user_info_t *info, void *objv);
    
    /* 桶操作 */
    int (*get_bucket)(rgw_sal_driver_t *driver, void *dpp, void *y, const rgw_user_t *user, const char *bucket_name, rgw_sal_bucket_t **bucket);
    int (*list_buckets)(rgw_sal_driver_t *driver, void *dpp, void *y, const rgw_user_t *user, const char *marker, const char *prefix, uint32_t max_keys, rgw_sal_bucket_list_t **list);
    
    /* 对象操作 */
    int (*get_object)(rgw_sal_bucket_t *bucket, const char *key, rgw_sal_object_t **object);
    
    /* 释放驱动 */
    void (*destroy)(rgw_sal_driver_t *driver);
} rgw_sal_driver_ops_t;

/**
 * @brief 存储驱动结构
 */
struct rgw_sal_driver {
    const rgw_sal_driver_ops_t *ops;
    void *context;
};

/**
 * @brief 创建存储驱动
 */
rgw_sal_driver_t *rgw_sal_driver_create(const rgw_sal_driver_ops_t *ops, void *context);

/**
 * @brief 初始化存储驱动
 */
int rgw_sal_driver_initialize(rgw_sal_driver_t *driver, void *cct, void *dpp);

/**
 * @brief 获取驱动名称
 */
const char *rgw_sal_driver_get_name(const rgw_sal_driver_t *driver);

/**
 * @brief 释放存储驱动
 */
void rgw_sal_driver_destroy(rgw_sal_driver_t *driver);

/**
 * @brief 用户对象操作接口
 */
typedef struct rgw_sal_user_ops {
    int (*load)(rgw_sal_user_t *user, void *dpp, void *y, rgw_user_info_t *info, rgw_sal_attrs_t *attrs, void *objv);
    int (*store)(rgw_sal_user_t *user, void *dpp, void *y, bool exclusive, const rgw_user_info_t *info, const rgw_user_info_t *old_info, const rgw_sal_attrs_t *attrs, void *objv);
    int (*remove)(rgw_sal_user_t *user, void *dpp, void *y, const rgw_user_info_t *info, void *objv);
    void (*destroy)(rgw_sal_user_t *user);
} rgw_sal_user_ops_t;

/**
 * @brief 用户结构
 */
struct rgw_sal_user {
    const rgw_sal_user_ops_t *ops;
    rgw_user_t *user_id;
    void *driver;
};

/**
 * @brief 创建用户对象
 */
rgw_sal_user_t *rgw_sal_user_create(const rgw_sal_user_ops_t *ops, rgw_user_t *user_id, void *driver);

/**
 * @brief 加载用户信息
 */
int rgw_sal_user_load(rgw_sal_user_t *user, void *dpp, void *y, rgw_user_info_t *info, rgw_sal_attrs_t *attrs, void *objv);

/**
 * @brief 存储用户信息
 */
int rgw_sal_user_store(rgw_sal_user_t *user, void *dpp, void *y, bool exclusive, const rgw_user_info_t *info, const rgw_user_info_t *old_info, const rgw_sal_attrs_t *attrs, void *objv);

/**
 * @brief 删除用户
 */
int rgw_sal_user_remove(rgw_sal_user_t *user, void *dpp, void *y, const rgw_user_info_t *info, void *objv);

/**
 * @brief 释放用户对象
 */
void rgw_sal_user_destroy(rgw_sal_user_t *user);

/**
 * @brief 桶对象操作接口
 */
typedef struct rgw_sal_bucket_ops {
    int (*load)(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv);
    int (*store)(rgw_sal_bucket_t *bucket, void *dpp, void *y, bool exclusive, void *objv);
    int (*remove)(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv);
    int (*list_objects)(rgw_sal_bucket_t *bucket, void *dpp, void *y, const char *prefix, const char *delimiter, const char *marker, uint32_t max_keys, void **list);
    void (*destroy)(rgw_sal_bucket_t *bucket);
} rgw_sal_bucket_ops_t;

/**
 * @brief 桶结构
 */
struct rgw_sal_bucket {
    const rgw_sal_bucket_ops_t *ops;
    rgw_user_t *owner;
    char *name;
    char *marker;
    char *bucket_id;
    void *driver;
};

/**
 * @brief 创建桶对象
 */
rgw_sal_bucket_t *rgw_sal_bucket_create(const rgw_sal_bucket_ops_t *ops, rgw_user_t *owner, const char *name, void *driver);

/**
 * @brief 加载桶信息
 */
int rgw_sal_bucket_load(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv);

/**
 * @brief 存储桶信息
 */
int rgw_sal_bucket_store(rgw_sal_bucket_t *bucket, void *dpp, void *y, bool exclusive, void *objv);

/**
 * @brief 删除桶
 */
int rgw_sal_bucket_remove(rgw_sal_bucket_t *bucket, void *dpp, void *y, void *objv);

/**
 * @brief 释放桶对象
 */
void rgw_sal_bucket_destroy(rgw_sal_bucket_t *bucket);

/**
 * @brief 对象操作接口
 */
typedef struct rgw_sal_object_ops {
    int (*load)(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags);
    int (*store)(rgw_sal_object_t *obj, void *dpp, void *y, bool exclusive, uint32_t flags);
    int (*remove)(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags);
    int (*read)(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, uint8_t *buf, size_t *bytes_read);
    int (*write)(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, const uint8_t *buf, size_t *bytes_written);
    void (*destroy)(rgw_sal_object_t *obj);
} rgw_sal_object_ops_t;

/**
 * @brief 对象结构
 */
struct rgw_sal_object {
    const rgw_sal_object_ops_t *ops;
    rgw_sal_bucket_t *bucket;
    char *key;
    uint64_t size;
    void *driver;
};

/**
 * @brief 创建对象
 */
rgw_sal_object_t *rgw_sal_object_create(const rgw_sal_object_ops_t *ops, rgw_sal_bucket_t *bucket, const char *key, void *driver);

/**
 * @brief 加载对象
 */
int rgw_sal_object_load(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags);

/**
 * @brief 存储对象
 */
int rgw_sal_object_store(rgw_sal_object_t *obj, void *dpp, void *y, bool exclusive, uint32_t flags);

/**
 * @brief 删除对象
 */
int rgw_sal_object_remove(rgw_sal_object_t *obj, void *dpp, void *y, uint32_t flags);

/**
 * @brief 读取对象数据
 */
int rgw_sal_object_read(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, uint8_t *buf, size_t *bytes_read);

/**
 * @brief 写入对象数据
 */
int rgw_sal_object_write(rgw_sal_object_t *obj, void *dpp, void *y, uint64_t offset, size_t len, const uint8_t *buf, size_t *bytes_written);

/**
 * @brief 释放对象
 */
void rgw_sal_object_destroy(rgw_sal_object_t *obj);

/*============================================================================
 * 简化的创建函数（用于测试）
 *============================================================================*/

/**
 * @brief 创建用户对象（简化版本，用于测试）
 */
rgw_sal_user_t *rgw_sal_user_create_simple(void);

/**
 * @brief 创建桶对象（简化版本，用于测试）
 */
rgw_sal_bucket_t *rgw_sal_bucket_create_simple(void);

/**
 * @brief 创建对象（简化版本，用于测试）
 */
rgw_sal_object_t *rgw_sal_object_create_simple(void);

#ifdef __cplusplus
}
#endif

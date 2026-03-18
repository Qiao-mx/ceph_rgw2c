/**
 * @file rgw_sal.c
 * @brief SAL C 接口实现
 *
 * 实现存储抽象层 (SAL) C 语言接口的基础功能。
 */

#include <stdlib.h>
#include <string.h>
#include "rgw_sal.h"
#include "rgw_sal_rados.h"

/*============================================================================
 * 驱动创建/销毁
 *============================================================================*/

/**
 * @brief 驱动创建函数
 *
 * 根据驱动名称创建相应的驱动实例
 */
rgw_sal_driver_t* rgw_sal_create_driver(const char* driver_name, void* cct) {
    if (!driver_name) return NULL;

    if (strcmp(driver_name, "rados") == 0) {
        return rgw_sal_rados_driver_create(cct, NULL);
    }

    /* TODO: 添加其他驱动支持 */
    return NULL;
}

/**
 * @brief 驱动销毁存根函数
 */
void rgw_sal_destroy_driver(rgw_sal_driver_t* driver) {
    if (!driver) return;
    if (driver->vtable && driver->vtable->destroy) {
        driver->vtable->destroy(driver);
    }
    free(driver);
}

/*============================================================================
 * 驱动操作
 *============================================================================*/

int rgw_sal_init_driver(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver || !driver->vtable) return RGW_SAL_ERR_INVALID_ARG;
    if (!driver->vtable->initialize) return RGW_SAL_ERR_NOT_IMPLEMENTED;
    return driver->vtable->initialize(driver, cct, dpp);
}

const char* rgw_sal_get_driver_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    if (driver->vtable && driver->vtable->get_name) {
        return driver->vtable->get_name(driver);
    }
    return driver->name;
}

/*============================================================================
 * 用户操作
 *============================================================================*/

rgw_sal_user_t* rgw_sal_get_user(rgw_sal_driver_t* driver,
                                    const rgw_sal_user_id_t* uid) {
    if (!driver || !driver->vtable || !driver->vtable->get_user) return NULL;
    return driver->vtable->get_user(driver, uid);
}

int rgw_sal_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key,
                                    rgw_sal_user_t** user,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !driver->vtable || !driver->vtable->get_user_by_access_key) {
        return RGW_SAL_ERR_NOT_IMPLEMENTED;
    }
    return driver->vtable->get_user_by_access_key(driver, key, user, dpp, y);
}

int rgw_sal_get_user_by_email(rgw_sal_driver_t* driver, const char* email,
                               rgw_sal_user_t** user,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !driver->vtable || !driver->vtable->get_user_by_email) {
        return RGW_SAL_ERR_NOT_IMPLEMENTED;
    }
    return driver->vtable->get_user_by_email(driver, email, user, dpp, y);
}

int rgw_sal_user_load(rgw_sal_user_t* user,
                       const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    /* 用户需要在获取后手动调用 load */
    (void)user;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_user_store(rgw_sal_user_t* user,
                        const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y,
                        bool exclusive) {
    (void)user;
    (void)dpp;
    (void)y;
    (void)exclusive;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_user_remove(rgw_sal_user_t* user,
                         const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)user;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

void rgw_sal_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }
    free(user);
}

/*============================================================================
 * 桶操作
 *============================================================================*/

rgw_sal_bucket_t* rgw_sal_get_bucket(rgw_sal_driver_t* driver,
                                        const rgw_sal_bucket_info_t* info) {
    if (!driver || !driver->vtable || !driver->vtable->get_bucket) return NULL;
    return driver->vtable->get_bucket(driver, info);
}

int rgw_sal_list_buckets(rgw_sal_driver_t* driver,
                          rgw_sal_user_t* owner,
                          const char* prefix, const char* delimiter,
                          const char* marker, const char* end_marker,
                          uint32_t max_keys, bool list_all,
                          rgw_sal_bucket_list_t** result,
                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !driver->vtable || !driver->vtable->list_buckets) {
        return RGW_SAL_ERR_NOT_IMPLEMENTED;
    }
    return driver->vtable->list_buckets(driver, owner, prefix, delimiter,
                                         marker, end_marker, max_keys, list_all,
                                         result, dpp, y);
}

int rgw_sal_create_bucket(rgw_sal_driver_t* driver,
                           const char* name,
                           rgw_sal_user_t* owner,
                           rgw_sal_bucket_t** bucket,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)driver;
    (void)name;
    (void)owner;
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_bucket_remove(rgw_sal_bucket_t* bucket,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)bucket;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

void rgw_sal_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }
    free(bucket);
}

/*============================================================================
 * 对象操作
 *============================================================================*/

rgw_sal_object_t* rgw_sal_get_object(rgw_sal_driver_t* driver,
                                      rgw_sal_bucket_t* bucket,
                                      const rgw_sal_obj_key_t* key) {
    if (!driver || !driver->vtable || !driver->vtable->get_object) return NULL;
    return driver->vtable->get_object(driver, bucket, key);
}

int rgw_sal_object_read(rgw_sal_object_t* obj,
                         int64_t offset, int64_t end,
                         uint8_t* buffer, size_t* buffer_size,
                         const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)obj;
    (void)offset;
    (void)end;
    (void)buffer;
    (void)buffer_size;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_object_write(rgw_sal_object_t* obj,
                          int64_t offset, int64_t size,
                          const uint8_t* data,
                          const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)obj;
    (void)offset;
    (void)size;
    (void)data;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

int rgw_sal_object_delete(rgw_sal_object_t* obj, uint32_t flags,
                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    (void)obj;
    (void)flags;
    (void)dpp;
    (void)y;
    return RGW_SAL_ERR_NOT_IMPLEMENTED;
}

void rgw_sal_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;
    if (obj->vtable && obj->vtable->destroy) {
        obj->vtable->destroy(obj);
    }
    free(obj);
}

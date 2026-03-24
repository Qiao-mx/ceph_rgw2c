/**
 * @file rgw_sal.c
 * @brief SAL C 核心实现
 *
 * 提供 SAL C 层的核心函数，包括驱动工厂函数。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgw_sal.h"
#include "rgw_sal_errors.h"

/* 包含驱动程序头文件 */
#include "drivers/rgw_sal_rados.h"

/*============================================================================
 * 驱动类型定义
 *============================================================================*/

typedef enum {
    RGW_SAL_DRIVER_TYPE_RADOS = 0,
    RGW_SAL_DRIVER_TYPE_DBSTORE,
    RGW_SAL_DRIVER_TYPE_DAOS,
    RGW_SAL_DRIVER_TYPE_POSIX,
    RGW_SAL_DRIVER_TYPE_MOTR,
    RGW_SAL_DRIVER_TYPE_D4N,
    RGW_SAL_DRIVER_TYPE_UNKNOWN
} rgw_sal_driver_type_t;

/**
 * @brief 获取驱动类型
 */
static rgw_sal_driver_type_t get_driver_type(const char* type) {
    if (!type) return RGW_SAL_DRIVER_TYPE_UNKNOWN;

    if (strcmp(type, "rados") == 0 || strcmp(type, "RadosDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_RADOS;
    }
    if (strcmp(type, "dbstore") == 0 || strcmp(type, "DBStoreDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_DBSTORE;
    }
    if (strcmp(type, "daos") == 0 || strcmp(type, "DAOSDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_DAOS;
    }
    if (strcmp(type, "posix") == 0 || strcmp(type, "PosixDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_POSIX;
    }
    if (strcmp(type, "motr") == 0 || strcmp(type, "MotrDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_MOTR;
    }
    if (strcmp(type, "d4n") == 0 || strcmp(type, "D4NDriver") == 0) {
        return RGW_SAL_DRIVER_TYPE_D4N;
    }

    return RGW_SAL_DRIVER_TYPE_UNKNOWN;
}

/*============================================================================
 * 驱动工厂函数实现
 *============================================================================*/

/**
 * @brief 创建 SAL 驱动实例
 *
 * 根据指定的驱动类型创建相应的驱动实例。
 *
 * @param type 驱动类型 ("rados", "dbstore", "daos", 等)
 * @param cct Ceph 上下文指针
 * @return 驱动实例，失败返回 NULL
 */
rgw_sal_driver_t* rgw_sal_create_driver(const char* type, void* cct) {
    if (!type) {
        return NULL;
    }

    rgw_sal_driver_type_t driver_type = get_driver_type(type);

    switch (driver_type) {
        case RGW_SAL_DRIVER_TYPE_RADOS:
            return rgw_sal_rados_driver_create(cct, NULL);

#ifdef HAVE_RGW_SAL_DBSTORE
        case RGW_SAL_DRIVER_TYPE_DBSTORE:
            return rgw_sal_dbstore_driver_create(cct);
#endif

#ifdef HAVE_RGW_SAL_DAOS
        case RGW_SAL_DRIVER_TYPE_DAOS:
            return rgw_sal_daos_driver_create(cct);
#endif

#ifdef HAVE_RGW_SAL_POSIX
        case RGW_SAL_DRIVER_TYPE_POSIX:
            return rgw_sal_posix_driver_create(cct);
#endif

#ifdef HAVE_RGW_SAL_MOTR
        case RGW_SAL_DRIVER_TYPE_MOTR:
            return rgw_sal_motr_driver_create(cct);
#endif

#ifdef HAVE_RGW_SAL_D4N
        case RGW_SAL_DRIVER_TYPE_D4N:
            return rgw_sal_d4n_driver_create(cct);
#endif

        default:
            /* 默认为 RADOS 驱动 */
            fprintf(stderr, "Unknown driver type '%s', using default 'rados'\n", type);
            return rgw_sal_rados_driver_create(cct, NULL);
    }
}

/**
 * @brief 销毁 SAL 驱动
 *
 * 释放驱动实例及其相关资源。
 *
 * @param driver 驱动实例
 */
void rgw_sal_destroy_driver(rgw_sal_driver_t* driver) {
    if (!driver) return;

    /* 调用驱动的销毁函数 */
    if (driver->vtable && driver->vtable->destroy) {
        driver->vtable->destroy(driver);
    } else {
        /* 默认释放逻辑 */
        if (driver->impl) {
            free(driver->impl);
            driver->impl = NULL;
        }
        free(driver);
    }
}

/**
 * @brief 获取驱动名称
 *
 * @param driver 驱动实例
 * @return 驱动名称
 */
const char* rgw_sal_get_driver_name(const rgw_sal_driver_t* driver) {
    if (!driver || !driver->vtable) return NULL;
    return driver->vtable->get_name(driver);
}

/**
 * @brief 初始化驱动
 *
 * @param driver 驱动实例
 * @param cct Ceph 上下文
 * @param dpp 调试前缀提供者
 * @return 错误码
 */
int rgw_sal_init_driver(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    if (driver->vtable && driver->vtable->initialize) {
        return driver->vtable->initialize(driver, cct, dpp);
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取 SAL 版本
 *
 * @return SAL 版本字符串
 */
const char* rgw_sal_get_version(void) {
    return "1.0.0";
}

/*============================================================================
 * 错误处理函数实现
 *============================================================================*/

/**
 * @brief 获取错误码对应的描述
 *
 * @param err 错误码
 * @return 错误描述字符串
 */
const char* rgw_sal_strerror(int err) {
    switch (err) {
        case RGW_SAL_OK:
            return "Success";
        case RGW_SAL_ERR_INVALID:
            return "Invalid argument";
        case RGW_SAL_ERR_NO_MEMORY:
            return "Out of memory";
        case RGW_SAL_ERR_NOT_FOUND:
            return "Not found";
        case RGW_SAL_ERR_EXISTS:
            return "Already exists";
        case RGW_SAL_ERR_PERMISSION_DENIED:
            return "Permission denied";
        case RGW_SAL_ERR_ABORTED:
            return "Operation aborted";
        case RGW_SAL_ERR_IO:
            return "I/O error";
        case RGW_SAL_ERR_INVALID_ARG:
            return "Invalid argument";
        case RGW_SAL_ERR_OUT_OF_MEMORY:
            return "Out of memory";
        case RGW_SAL_ERR_NOT_INITIALIZED:
            return "Not initialized";
        case RGW_SAL_ERR_WRITE_ERROR:
            return "Write error";
        case RGW_SAL_ERR_INTERNAL_ERROR:
            return "Internal error";
        case RGW_SAL_ERR_DATA_CORRUPTION:
            return "Data corruption";
        case RGW_SAL_ERR_IO_ERROR:
            return "I/O error";
        case RGW_SAL_ERR_NOT_IMPLEMENTED:
            return "Not implemented";
        case RGW_SAL_ERR_MFA_AUTH_FAILED:
            return "MFA authentication failed";
        case RGW_SAL_ERR_INDEX_ERROR:
            return "Index error";
        case RGW_SAL_ERR_VERSION_CONFLICT:
            return "Version conflict";
        case RGW_SAL_ERR_GENERIC:
            return "Generic error";
        case RGW_SAL_ERR_PARSE_ERROR:
            return "Parse error";
        case RGW_SAL_ERR_READ_ERROR:
            return "Read error";
        default:
            return "Unknown error";
    }
}

/**
 * @brief 检查是否为致命错误
 *
 * @param err 错误码
 * @return 如果是致命错误返回 true
 */
bool rgw_sal_is_fatal_error(int err) {
    switch (err) {
        case RGW_SAL_ERR_NO_MEMORY:
        case RGW_SAL_ERR_OUT_OF_MEMORY:
        case RGW_SAL_ERR_DATA_CORRUPTION:
        case RGW_SAL_ERR_INTERNAL_ERROR:
            return true;
        default:
            return false;
    }
}

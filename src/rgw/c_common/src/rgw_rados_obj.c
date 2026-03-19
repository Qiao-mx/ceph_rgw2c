/**
 * @file rgw_rados_obj.c
 * @brief RADOS 对象 SAL 实现
 *
 * 实现 SAL 对象操作的 RADOS 后端功能。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "rgw_sal_rados.h"
#include "rgw_rados_ctx.h"
#include "rgw_rados_object.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 默认对象池名称 */
#define RGW_RADOS_DEFAULT_OBJ_POOL  RGW_RADOS_CTX_POOL_BUCKETS_DATA

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 获取对象池的 IO 上下文
 */
static int get_object_pool_ioctx(rgw_sal_driver_t* driver,
                                  rados_ioctx_t* ioctx) {
    if (!driver || !ioctx) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用数据池作为对象池 */
    return rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                       RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                       ioctx);
}

/**
 * @brief 构建对象 OID
 */
static int build_object_oid(rgw_sal_object_t* obj, char* buf, size_t buf_size) {
    if (!obj || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 构建桶名/对象名格式的 OID */
    const char* bucket_name = obj_impl->bucket_name ? obj_impl->bucket_name : "";
    const char* obj_name = obj_impl->name ? obj_impl->name : "";

    int ret = snprintf(buf, buf_size, "%s/%s", bucket_name, obj_name);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/*============================================================================
 * 对象操作实现
 *============================================================================*/

/**
 * @brief 对象读取
 */
static int rados_object_read(rgw_sal_object_t* obj,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y,
                              uint64_t offset,
                              uint64_t size,
                              rgw_sal_buffer_t** buffer) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 分配读取缓冲区 */
    size_t read_size = (size > 0 && size < RGW_OBJECT_MAX_BUFFER_SIZE) ?
                       size : RGW_OBJECT_MAX_BUFFER_SIZE;

    uint8_t* buf = (uint8_t*)malloc(read_size);
    if (!buf) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 执行读取 */
    int bytes_read = rados_read(ioctx, oid, (char*)buf, read_size, offset);

    if (bytes_read < 0) {
        free(buf);
        if (bytes_read == -ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 创建缓冲区对象 */
    if (buffer) {
        *buffer = (rgw_sal_buffer_t*)malloc(sizeof(rgw_sal_buffer_t));
        if (*buffer) {
            (*buffer)->data = buf;
            (*buffer)->size = (size_t)bytes_read;
            (*buffer)->ref_count = 1;
        } else {
            free(buf);
        }
    } else {
        free(buf);
    }

    obj_impl->size = bytes_read;
    obj_impl->loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 对象写入
 */
static int rados_object_write(rgw_sal_object_t* obj,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y,
                               uint64_t offset,
                               const uint8_t* data,
                               size_t size) {
    (void)dpp;
    (void)y;

    if (!obj || !data) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 执行写入 */
    ret = rados_write(ioctx, oid, (const char*)data, size, offset);

    if (ret < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    obj_impl->size = size;
    obj_impl->written = true;
    obj_impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 对象删除
 */
static int rados_object_delete(rgw_sal_object_t* obj,
                               const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 执行删除 */
    ret = rados_remove(ioctx, oid);

    if (ret < 0 && ret != -ENOENT) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    obj_impl->deleted = true;

    return RGW_SAL_OK;
}

/**
 * @brief 获取对象大小
 */
static int64_t rados_object_get_size(const rgw_sal_object_t* obj) {
    if (!obj) {
        return -1;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return -1;
    }

    if (!obj_impl->loaded) {
        return -1;
    }

    return obj_impl->size;
}

/**
 * @brief 获取对象修改时间
 */
static time_t rados_object_get_mtime(const rgw_sal_object_t* obj) {
    if (!obj) {
        return 0;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return 0;
    }

    return obj_impl->mtime;
}

/**
 * @brief 获取对象状态
 */
static int rados_object_stat(rgw_sal_object_t* obj,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 执行 stat */
    uint64_t size;
    time_t mtime;

    ret = rados_stat(ioctx, oid, &size, &mtime);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    obj_impl->size = size;
    obj_impl->mtime = mtime;
    obj_impl->loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 设置对象属性
 */
static int rados_object_set_attr(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  const char* name,
                                  const uint8_t* value,
                                  size_t len) {
    (void)dpp;
    (void)y;

    if (!obj || !name || !value) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 设置 xattr */
    ret = rgw_object_set_xattr(ioctx, oid, name, value, len);

    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取对象属性
 */
static int rados_object_get_attr(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  const char* name,
                                  uint8_t** value,
                                  size_t* len) {
    (void)dpp;
    (void)y;

    if (!obj || !name || !value || !len) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 获取 xattr */
    ret = rgw_object_get_xattr(ioctx, oid, name, value, len);

    if (ret != 0) {
        if (ret == RGW_ERR_NOT_FOUND) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 删除对象属性
 */
static int rados_object_del_attr(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  const char* name) {
    (void)dpp;
    (void)y;

    if (!obj || !name) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = get_object_pool_ioctx(obj->bucket->driver, &ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    ret = build_object_oid(obj, oid, sizeof(oid));
    if (ret != 0) {
        return ret;
    }

    /* 删除 xattr */
    ret = rgw_object_del_xattr(ioctx, oid, name);

    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    return RGW_SAL_OK;
}

/*============================================================================
 * 对象 vtable
 *============================================================================*/

/**
 * @brief 获取对象操作函数表
 */
rgw_sal_object_vtable_t* rgw_rados_get_object_vtable(void) {
    static rgw_sal_object_vtable_t vtable = {
        .destroy           = NULL,  /* TODO */
        .get_name          = NULL,  /* TODO */
        .get_instance      = NULL,  /* TODO */
        .get_oid           = NULL,  /* TODO */
        .get_key           = NULL,  /* TODO */
        .check_exists      = NULL,  /* TODO */
        .read              = rados_object_read,
        .write             = rados_object_write,
        .delete            = rados_object_delete,
        .get_size          = NULL,  /* TODO */
        .get_mtime         = NULL,  /* TODO */
        .set_mtime         = NULL,  /* TODO */
        .stat              = rados_object_stat,
        .set_attr          = rados_object_set_attr,
        .get_attr          = rados_object_get_attr,
        .del_attr          = rados_object_del_attr,
        .set_attrs         = NULL,  /* TODO */
        .get_attrs         = NULL,  /* TODO */
        .modify_attr       = NULL,  /* TODO */
        .delete_obj_tags   = NULL,  /* TODO */
        .get_tags          = NULL,  /* TODO */
        .set_tags          = NULL,  /* TODO */
        .get_acl           = NULL,  /* TODO */
        .set_acl           = NULL,  /* TODO */
        .get_owner         = NULL,  /* TODO */
        .set_owner         = NULL,  /* TODO */
        .load_state        = NULL,  /* TODO */
        .refresh_info      = NULL,  /* TODO */
        .transition        = NULL,  /* TODO */
        .get_info          = NULL,  /* TODO */
        .dump_obj          = NULL,  /* TODO */
        .transition_rgw_obj = NULL,  /* TODO */
        .copy_from         = NULL,  /* TODO */
        .rewrite           = NULL,  /* TODO */
        .get_max_chunk_size = NULL,  /* TODO */
        .follow_etag       = NULL,  /* TODO */
        .check_storage_class = NULL,  /* TODO */
        .get_obj_storage_class = NULL,  /* TODO */
        .obj_manifest       = NULL,  /* TODO */
    };

    return &vtable;
}

/*============================================================================
 * 初始化
 *============================================================================*/

/**
 * @brief 初始化 RADOS 对象子系统
 */
int rgw_rados_object_init(rgw_sal_driver_t* driver) {
    if (!driver) {
        return RGW_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 预打开对象池 */
    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_meta_pool(impl->rados_ctx,
                                             RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                             &ioctx);
    if (ret != 0) {
        /* 池可能不存在，这是正常的 */
    }

    return RGW_OK;
}

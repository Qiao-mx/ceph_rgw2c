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

/** 默认最大块大小 (8MB) */
#define RGW_RADOS_DEFAULT_MAX_CHUNK_SIZE (8 * 1024 * 1024)

/** 默认存储类别 */
#define RGW_RADOS_DEFAULT_STORAGE_CLASS "STANDARD"

/** 对象清单最大大小 */
#define RGW_RADOS_MANIFEST_MAX_SIZE (64 * 1024)

/** ETag 最大长度 */
#define RGW_RADOS_ETAG_MAX_LEN 64

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

/**
 * @brief 释放对象实现结构
 */
static void free_object_impl(rados_object_impl_t* impl) {
    if (!impl) {
        return;
    }

    free(impl->name);
    free(impl->instance);
    free(impl->bucket_name);
    free(impl->bucket_tenant);
    free(impl->bucket_id);

    if (impl->attrs) {
        /* 释放属性 */
        for (size_t i = 0; i < impl->attrs->count; i++) {
            free(impl->attrs->pairs[i].key);
            free(impl->attrs->pairs[i].value);
        }
        free(impl->attrs->pairs);
        free(impl->attrs);
    }

    free(impl);
}

/*============================================================================
 * 对象操作实现
 *============================================================================*/

/**
 * @brief 销毁对象
 */
static void rados_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) {
        return;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (impl) {
        free_object_impl(impl);
        obj->impl = NULL;
    }
}

/**
 * @brief 获取对象名称
 */
static const char* rados_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    return impl->name;
}

/**
 * @brief 获取对象实例/版本
 */
static const char* rados_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    return impl->instance;
}

/**
 * @brief 获取对象 OID
 */
static const char* rados_object_get_oid(const rgw_sal_object_t* obj, char* buf, size_t buf_size) {
    if (!obj || !buf) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    /* 构建 OID: bucket_name/object_name */
    const char* bucket_name = impl->bucket_name ? impl->bucket_name : "";
    const char* obj_name = impl->name ? impl->name : "";

    int ret = snprintf(buf, buf_size, "%s/%s", bucket_name, obj_name);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return NULL;
    }

    return buf;
}

/**
 * @brief 获取对象键
 */
static const char* rados_object_get_key(const rgw_sal_object_t* obj) {
    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    /* 对象键通常是 name */
    return impl->name;
}

/**
 * @brief 检查对象是否存在
 */
static bool rados_object_check_exists(const rgw_sal_object_t* obj) {
    if (!obj) {
        return false;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return false;
    }

    /* 获取 IO 上下文 */
    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)obj->bucket->driver->impl;
    if (!driver_impl || !driver_impl->rados_ctx) {
        return false;
    }

    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_meta_pool(driver_impl->rados_ctx,
                                            RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                            &ioctx);
    if (ret != 0) {
        return false;
    }

    /* 构建 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    const char* bucket_name = impl->bucket_name ? impl->bucket_name : "";
    const char* obj_name = impl->name ? impl->name : "";

    ret = snprintf(oid, sizeof(oid), "%s/%s", bucket_name, obj_name);
    if (ret < 0 || (size_t)ret >= sizeof(oid)) {
        return false;
    }

    /* 使用 stat 检查对象是否存在 */
    uint64_t size;
    time_t mtime;
    ret = rados_stat(ioctx, oid, &size, &mtime);

    return (ret == 0);
}

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

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return -1;
    }

    return impl->size;
}

/**
 * @brief 获取对象修改时间
 */
static time_t rados_object_get_mtime(const rgw_sal_object_t* obj) {
    if (!obj) {
        return 0;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return 0;
    }

    return impl->mtime;
}

/**
 * @brief 设置对象修改时间
 */
static int rados_object_set_mtime(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y,
                                   time_t mtime) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    impl->mtime = mtime;
    return RGW_SAL_OK;
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

/**
 * @brief 设置多个对象属性
 */
static int rados_object_set_attrs(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y,
                                   rgw_sal_attrs_t* attrs) {
    (void)dpp;
    (void)y;

    if (!obj || !attrs) {
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

    /* 遍历属性并设置 */
    for (size_t i = 0; i < attrs->count; i++) {
        ret = rgw_object_set_xattr(ioctx, oid,
                                   attrs->pairs[i].key,
                                   attrs->pairs[i].value,
                                   attrs->pairs[i].value_len);
        if (ret != 0) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取所有对象属性
 */
static rgw_sal_attrs_t* rados_object_get_attrs(rgw_sal_object_t* obj,
                                                 const rgw_sal_dpp_t* dpp,
                                                 rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) {
        return NULL;
    }

    /* 如果已有属性缓存，直接返回 */
    if (obj_impl->attrs) {
        return obj_impl->attrs;
    }

    /* 获取 IO 上下文 */
    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)obj->bucket->driver->impl;
    if (!driver_impl || !driver_impl->rados_ctx) {
        return NULL;
    }

    rados_ioctx_t ioctx;
    int ret = rgw_rados_ctx_open_meta_pool(driver_impl->rados_ctx,
                                            RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                            &ioctx);
    if (ret != 0) {
        return NULL;
    }

    /* 构建对象 OID */
    char oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    const char* bucket_name = obj_impl->bucket_name ? obj_impl->bucket_name : "";
    const char* obj_name = obj_impl->name ? obj_impl->name : "";

    ret = snprintf(oid, sizeof(oid), "%s/%s", bucket_name, obj_name);
    if (ret < 0 || (size_t)ret >= sizeof(oid)) {
        return NULL;
    }

    /* 创建属性映射 */
    rgw_sal_attrs_t* attrs = (rgw_sal_attrs_t*)malloc(sizeof(rgw_sal_attrs_t));
    if (!attrs) {
        return NULL;
    }
    memset(attrs, 0, sizeof(rgw_sal_attrs_t));

    /* 获取 xattrs 列表 */
    rados_list_ctx_t list_ctx;
    ret = rados_list_xattrs_start(ioctx, oid, &list_ctx);
    if (ret < 0) {
        free(attrs);
        return NULL;
    }

    /* 分配初始容量 */
    attrs->capacity = 16;
    attrs->pairs = (rgw_sal_attr_pair_t*)malloc(attrs->capacity * sizeof(rgw_sal_attr_pair_t));
    if (!attrs->pairs) {
        rados_list_xattrs_end(list_ctx);
        free(attrs);
        return NULL;
    }
    attrs->count = 0;

    /* 遍历 xattrs */
    char* xattr_key = NULL;
    while (true) {
        ret = rados_list_xattrs_next2(list_ctx, &xattr_key, NULL, NULL);
        if (ret < 0) {
            break;
        }
        if (!xattr_key) {
            break;
        }

        /* 确保容量 */
        if (attrs->count >= attrs->capacity) {
            size_t new_capacity = attrs->capacity * 2;
            rgw_sal_attr_pair_t* new_pairs = (rgw_sal_attr_pair_t*)realloc(
                attrs->pairs, new_capacity * sizeof(rgw_sal_attr_pair_t));
            if (!new_pairs) {
                free(xattr_key);
                break;
            }
            attrs->pairs = new_pairs;
            attrs->capacity = new_capacity;
        }

        /* 获取 xattr 值 */
        uint8_t* value = NULL;
        size_t value_len = 0;
        ret = rgw_object_get_xattr(ioctx, oid, xattr_key, &value, &value_len);

        if (ret == 0 && value) {
            attrs->pairs[attrs->count].key = xattr_key;
            attrs->pairs[attrs->count].value = value;
            attrs->pairs[attrs->count].value_len = value_len;
            attrs->count++;
        } else {
            free(xattr_key);
        }
    }

    rados_list_xattrs_end(list_ctx);

    /* 保存到对象实现 */
    obj_impl->attrs = attrs;

    return attrs;
}

/**
 * @brief 修改对象属性
 */
static int rados_object_modify_attr(rgw_sal_object_t* obj,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y,
                                    const char* name,
                                    const uint8_t* value,
                                    size_t value_len) {
    if (!obj || !name || !value) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 修改属性等同于设置属性 */
    return rados_object_set_attr(obj, dpp, y, name, value, value_len);
}

/**
 * @brief 删除对象标签
 */
static int rados_object_delete_obj_tags(rgw_sal_object_t* obj,
                                         const rgw_sal_dpp_t* dpp,
                                         rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 标签存储在 xattr 中，键名为 "tag" */
    return rados_object_del_attr(obj, dpp, y, "tag");
}

/**
 * @brief 获取对象标签
 */
static int rados_object_get_tags(rgw_sal_object_t* obj,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y,
                                 rgw_sal_attrs_t** tags) {
    (void)dpp;
    (void)y;

    if (!obj || !tags) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *tags = NULL;

    /* 标签存储在 xattr 中，键名为 "tag" */
    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rados_object_get_attr(obj, dpp, y, "tag", &value, &value_len);
    if (ret != RGW_SAL_OK) {
        return ret;
    }

    /* 创建标签属性映射 */
    rgw_sal_attrs_t* attrs = (rgw_sal_attrs_t*)malloc(sizeof(rgw_sal_attrs_t));
    if (!attrs) {
        rgw_object_xattr_free(value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    attrs->count = 1;
    attrs->capacity = 1;
    attrs->pairs = (rgw_sal_attr_pair_t*)malloc(sizeof(rgw_sal_attr_pair_t));
    if (!attrs->pairs) {
        free(attrs);
        rgw_object_xattr_free(value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    attrs->pairs[0].key = strdup("tag");
    attrs->pairs[0].value = value;
    attrs->pairs[0].value_len = value_len;

    *tags = attrs;
    return RGW_SAL_OK;
}

/**
 * @brief 设置对象标签
 */
static int rados_object_set_tags(rgw_sal_object_t* obj,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y,
                                 rgw_sal_attrs_t* tags) {
    (void)dpp;
    (void)y;

    if (!obj || !tags) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 标签存储在 xattr 中，键名为 "tag" */
    for (size_t i = 0; i < tags->count; i++) {
        int ret = rados_object_set_attr(obj, dpp, y,
                                        tags->pairs[i].key,
                                        tags->pairs[i].value,
                                        tags->pairs[i].value_len);
        if (ret != RGW_SAL_OK) {
            return ret;
        }
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取对象 ACL
 */
static void* rados_object_get_acl(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return NULL;
    }

    /* ACL 存储在 xattr 中 */
    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rados_object_get_attr(obj, dpp, y, "acl", &value, &value_len);
    if (ret != RGW_SAL_OK) {
        return NULL;
    }

    /* 返回值指针，调用者需要释放 */
    return value;
}

/**
 * @brief 设置对象 ACL
 */
static int rados_object_set_acl(rgw_sal_object_t* obj,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y,
                                 void* acl) {
    (void)dpp;
    (void)y;

    if (!obj || !acl) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* ACL 存储在 xattr 中 */
    /* acl 参数应该是已序列化的 ACL 数据 */
    return rados_object_set_attr(obj, dpp, y, "acl", (const uint8_t*)acl, 0);
}

/**
 * @brief 获取对象所有者
 */
static int rados_object_get_owner(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y,
                                   rgw_sal_user_t** owner) {
    (void)dpp;
    (void)y;

    if (!obj || !owner) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *owner = NULL;

    /* 所有者信息存储在 xattr 中 */
    uint8_t* owner_id_value = NULL;
    size_t value_len = 0;

    int ret = rados_object_get_attr(obj, dpp, y, "owner", &owner_id_value, &value_len);
    if (ret != RGW_SAL_OK) {
        return ret;
    }

    /* 创建用户对象 */
    rgw_sal_user_t* user = rgw_sal_get_user(obj->bucket->driver, NULL);
    if (!user) {
        rgw_object_xattr_free(owner_id_value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 设置用户 ID */
    rados_user_impl_t* user_impl = (rados_user_impl_t*)user->impl;
    if (user_impl) {
        if (value_len > 0) {
            user_impl->id = (char*)malloc(value_len + 1);
            if (user_impl->id) {
                memcpy(user_impl->id, owner_id_value, value_len);
                user_impl->id[value_len] = '\0';
            }
        }
    }

    rgw_object_xattr_free(owner_id_value);
    *owner = user;

    return RGW_SAL_OK;
}

/**
 * @brief 设置对象所有者
 */
static int rados_object_set_owner(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y,
                                   rgw_sal_user_t* owner) {
    (void)dpp;
    (void)y;

    if (!obj || !owner) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取用户 ID */
    const char* owner_id = rgw_sal_user_get_id(owner);
    if (!owner_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    size_t owner_id_len = strlen(owner_id);

    /* 存储到 xattr */
    return rados_object_set_attr(obj, dpp, y, "owner",
                                 (const uint8_t*)owner_id, owner_id_len);
}

/**
 * @brief 加载对象状态
 */
static int rados_object_load_state(rgw_sal_object_t* obj,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y,
                                   bool follow_olh) {
    (void)dpp;
    (void)y;
    (void)follow_olh;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 执行 stat 获取对象元数据 */
    int ret = rados_object_stat(obj, dpp, y);
    if (ret != RGW_SAL_OK) {
        return ret;
    }

    impl->loaded = true;

    return RGW_SAL_OK;
}

/**
 * @brief 刷新对象信息
 */
static int rados_object_refresh_info(rgw_sal_object_t* obj,
                                      const rgw_sal_dpp_t* dpp,
                                      rgw_sal_yield_t* y) {
    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 重新加载对象状态 */
    return rados_object_load_state(obj, dpp, y, false);
}

/**
 * @brief 对象转换到其他存储类别
 */
static int rados_object_transition(rgw_sal_object_t* obj,
                                    const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y,
                                    const char* storage_class) {
    (void)dpp;
    (void)y;

    if (!obj || !storage_class) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 设置存储类别属性 */
    int ret = rados_object_set_attr(obj, dpp, y, "storage_class",
                                    (const uint8_t*)storage_class,
                                    strlen(storage_class));
    if (ret != RGW_SAL_OK) {
        return ret;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 获取对象信息
 */
static int rados_object_get_info(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  void** info) {
    (void)dpp;
    (void)y;

    if (!obj || !info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    *info = NULL;

    /* 获取对象 stat 信息 */
    int ret = rados_object_stat(obj, dpp, y);
    if (ret != RGW_SAL_OK) {
        return ret;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建信息结构 (简化实现) */
    /* 实际应该创建完整的对象信息结构 */
    *info = NULL; /* TODO: 返回实际对象信息 */

    return RGW_SAL_OK;
}

/**
 * @brief 转储对象信息到字符串
 */
static int rados_object_dump_obj(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  char* buf,
                                  size_t buf_size) {
    (void)dpp;
    (void)y;

    if (!obj || !buf) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 构建对象信息字符串 */
    int ret = snprintf(buf, buf_size,
                       "Object: name=%s, instance=%s, bucket=%s, "
                       "size=%lld, mtime=%ld, loaded=%s, deleted=%s",
                       impl->name ? impl->name : "(null)",
                       impl->instance ? impl->instance : "(null)",
                       impl->bucket_name ? impl->bucket_name : "(null)",
                       (long long)impl->size,
                       (long)impl->mtime,
                       impl->loaded ? "true" : "false",
                       impl->deleted ? "true" : "false");

    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_SAL_OK;
}

/**
 * @brief RGW 对象转换 (简化实现)
 */
static int rados_object_transition_rgw_obj(rgw_sal_object_t* obj,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y,
                                           const char* storage_class,
                                           void* src_obj) {
    (void)src_obj;

    if (!obj || !storage_class) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 使用通用的 transition 函数 */
    return rados_object_transition(obj, dpp, y, storage_class);
}

/**
 * @brief 从其他对象复制
 */
static int rados_object_copy_from(rgw_sal_object_t* obj,
                                  const rgw_sal_dpp_t* dpp,
                                  rgw_sal_yield_t* y,
                                  rgw_sal_object_t* src_obj) {
    (void)dpp;
    (void)y;

    if (!obj || !src_obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 获取源对象大小 */
    int64_t src_size = rados_object_get_size(src_obj);
    if (src_size < 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 获取 IO 上下文 */
    rados_ioctx_t src_ioctx, dst_ioctx;

    rados_driver_impl_t* src_driver_impl = (rados_driver_impl_t*)src_obj->bucket->driver->impl;
    rados_driver_impl_t* dst_driver_impl = (rados_driver_impl_t*)obj->bucket->driver->impl;

    if (!src_driver_impl || !dst_driver_impl ||
        !src_driver_impl->rados_ctx || !dst_driver_impl->rados_ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = rgw_rados_ctx_open_meta_pool(src_driver_impl->rados_ctx,
                                            RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                            &src_ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    ret = rgw_rados_ctx_open_meta_pool(dst_driver_impl->rados_ctx,
                                        RGW_RADOS_CTX_POOL_BUCKETS_DATA,
                                        &dst_ioctx);
    if (ret != 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建源和目标 OID */
    rados_object_impl_t* src_impl = (rados_object_impl_t*)src_obj->impl;
    rados_object_impl_t* dst_impl = (rados_object_impl_t*)obj->impl;

    char src_oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];
    char dst_oid[RGW_OBJECT_MAX_NAME_LEN * 2 + 2];

    const char* src_bucket = src_impl->bucket_name ? src_impl->bucket_name : "";
    const char* src_name = src_impl->name ? src_impl->name : "";
    const char* dst_bucket = dst_impl->bucket_name ? dst_impl->bucket_name : "";
    const char* dst_name = dst_impl->name ? dst_impl->name : "";

    ret = snprintf(src_oid, sizeof(src_oid), "%s/%s", src_bucket, src_name);
    if (ret < 0 || (size_t)ret >= sizeof(src_oid)) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    ret = snprintf(dst_oid, sizeof(dst_oid), "%s/%s", dst_bucket, dst_name);
    if (ret < 0 || (size_t)ret >= sizeof(dst_oid)) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    /* 使用 librados 复制对象 */
    ret = rados_copy_from(src_ioctx, src_oid, dst_ioctx, dst_oid);

    if (ret < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 更新目标对象状态 */
    dst_impl->size = src_size;
    dst_impl->written = true;
    dst_impl->loaded = true;
    dst_impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 重写对象
 */
static int rados_object_rewrite(rgw_sal_object_t* obj,
                                const rgw_sal_dpp_t* dpp,
                                rgw_sal_yield_t* y) {
    (void)dpp;
    (void)y;

    if (!obj) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 重写等同于重新执行一次读-写操作 */
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 如果对象未加载，先加载 */
    if (!impl->loaded) {
        int ret = rados_object_stat(obj, dpp, y);
        if (ret != RGW_SAL_OK) {
            return ret;
        }
    }

    /* 对象重写 - 简单实现：重新设置 mtime */
    impl->mtime = time(NULL);

    return RGW_SAL_OK;
}

/**
 * @brief 获取最大块大小
 */
static uint64_t rados_object_get_max_chunk_size(const rgw_sal_object_t* obj,
                                                const rgw_sal_dpp_t* dpp) {
    (void)obj;
    (void)dpp;

    /* 返回默认最大块大小 */
    return RGW_RADOS_DEFAULT_MAX_CHUNK_SIZE;
}

/**
 * @brief 跟踪 ETag
 */
static const char* rados_object_follow_etag(rgw_sal_object_t* obj,
                                              const rgw_sal_dpp_t* dpp,
                                              rgw_sal_yield_t* y,
                                              const char* etag) {
    (void)dpp;
    (void)y;

    if (!obj || !etag) {
        return NULL;
    }

    /* 设置 ETag 属性 */
    int ret = rados_object_set_attr(obj, dpp, y, "etag",
                                    (const uint8_t*)etag, strlen(etag));
    if (ret != RGW_SAL_OK) {
        return NULL;
    }

    /* 返回设置的 ETag */
    return etag;
}

/**
 * @brief 检查存储类别
 */
static bool rados_object_check_storage_class(const rgw_sal_object_t* obj,
                                              const rgw_sal_dpp_t* dpp,
                                              rgw_sal_yield_t* y,
                                              const char* desired_class) {
    if (!obj || !desired_class) {
        return false;
    }

    /* 获取当前存储类别 */
    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rados_object_get_attr((rgw_sal_object_t*)obj, dpp, y,
                                   "storage_class", &value, &value_len);
    if (ret != RGW_SAL_OK || !value) {
        /* 没有存储类别属性，默认是 STANDARD */
        return (strcmp(desired_class, RGW_RADOS_DEFAULT_STORAGE_CLASS) == 0);
    }

    /* 比较存储类别 */
    bool match = (strncmp((const char*)value, desired_class, value_len) == 0);

    rgw_object_xattr_free(value);

    return match;
}

/**
 * @brief 获取对象存储类别
 */
static const char* rados_object_get_obj_storage_class(const rgw_sal_object_t* obj) {
    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    /* 存储类别应该从属性中获取，这里简化处理 */
    /* TODO: 实际实现应该查询 xattr */
    return RGW_RADOS_DEFAULT_STORAGE_CLASS;
}

/**
 * @brief 获取对象清单
 */
static void* rados_object_get_manifest(const rgw_sal_object_t* obj) {
    if (!obj) {
        return NULL;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) {
        return NULL;
    }

    /* 获取清单 xattr */
    /* 简化实现返回 NULL */
    return NULL; /* TODO: 实现清单获取 */
}

/**
 * @brief 设置对象清单
 */
static int rados_object_set_manifest(rgw_sal_object_t* obj,
                                     const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y,
                                     void* manifest) {
    (void)dpp;
    (void)y;

    if (!obj || !manifest) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 设置清单 xattr */
    /* 简化实现 */
    return RGW_SAL_OK; /* TODO: 实现清单设置 */
}

/*============================================================================
 * 对象 vtable
 *============================================================================*/

/**
 * @brief 获取对象操作函数表
 */
rgw_sal_object_vtable_t* rgw_rados_get_object_vtable(void) {
    static rgw_sal_object_vtable_t vtable = {
        .destroy           = rados_object_destroy,
        .get_name          = rados_object_get_name,
        .get_instance      = rados_object_get_instance,
        .get_oid           = rados_object_get_oid,
        .get_key           = rados_object_get_key,
        .check_exists      = rados_object_check_exists,
        .read              = rados_object_read,
        .write             = rados_object_write,
        .delete            = rados_object_delete,
        .get_size          = rados_object_get_size,
        .get_mtime         = rados_object_get_mtime,
        .set_mtime         = rados_object_set_mtime,
        .stat              = rados_object_stat,
        .set_attr          = rados_object_set_attr,
        .get_attr          = rados_object_get_attr,
        .del_attr          = rados_object_del_attr,
        .set_attrs         = rados_object_set_attrs,
        .get_attrs         = rados_object_get_attrs,
        .modify_attr       = rados_object_modify_attr,
        .delete_obj_tags   = rados_object_delete_obj_tags,
        .get_tags          = rados_object_get_tags,
        .set_tags          = rados_object_set_tags,
        .get_acl           = rados_object_get_acl,
        .set_acl           = rados_object_set_acl,
        .get_owner         = rados_object_get_owner,
        .set_owner         = rados_object_set_owner,
        .load_state        = rados_object_load_state,
        .refresh_info      = rados_object_refresh_info,
        .transition        = rados_object_transition,
        .get_info          = rados_object_get_info,
        .dump_obj          = rados_object_dump_obj,
        .transition_rgw_obj = rados_object_transition_rgw_obj,
        .copy_from         = rados_object_copy_from,
        .rewrite           = rados_object_rewrite,
        .get_max_chunk_size = rados_object_get_max_chunk_size,
        .follow_etag       = rados_object_follow_etag,
        .check_storage_class = rados_object_check_storage_class,
        .get_obj_storage_class = rados_object_get_obj_storage_class,
        .obj_manifest      = rados_object_get_manifest,
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

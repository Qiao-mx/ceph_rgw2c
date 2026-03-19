/**
 * @file rgw_rados_object.c
 * @brief RADOS 对象操作实现
 *
 * 实现对象存储的 RADOS 后端功能。
 * 使用 librados.h C API 进行对象读写操作。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgw_rados_object.h"
#include "rgw_errors.h"

/*============================================================================
 * 函数实现 - 基础读写
 *============================================================================*/

/**
 * @brief 创建对象读取上下文
 */
rgw_object_read_ctx_t* rgw_object_read_ctx_create(rados_ioctx_t ioctx,
                                                    const char* oid,
                                                    size_t buffer_size) {
    if (!ioctx || !oid) {
        return NULL;
    }

    /* 默认缓冲区大小 */
    if (buffer_size == 0) {
        buffer_size = RGW_OBJECT_DEFAULT_READ_SIZE;
    }

    /* 限制缓冲区大小 */
    if (buffer_size > RGW_OBJECT_MAX_BUFFER_SIZE) {
        buffer_size = RGW_OBJECT_MAX_BUFFER_SIZE;
    }

    rgw_object_read_ctx_t* ctx = (rgw_object_read_ctx_t*)malloc(
        sizeof(rgw_object_read_ctx_t));
    if (!ctx) {
        return NULL;
    }

    ctx->buffer = (uint8_t*)malloc(buffer_size);
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }

    ctx->ioctx = ioctx;
    ctx->oid = oid;
    ctx->buffer_size = buffer_size;
    ctx->bytes_read = 0;
    ctx->offset = 0;
    ctx->object_size = 0;
    ctx->last_ret = 0;

    return ctx;
}

/**
 * @brief 从对象读取数据
 */
rgw_object_read_status_t rgw_object_read(rgw_object_read_ctx_t* ctx,
                                          uint64_t offset,
                                          size_t size) {
    if (!ctx) {
        return RGW_OBJECT_READ_ERROR;
    }

    /* 限制读取大小 */
    if (size > ctx->buffer_size) {
        size = ctx->buffer_size;
    }

    /* 执行读取 */
    int ret = rados_read(ctx->ioctx, ctx->oid, (char*)ctx->buffer, size, offset);

    if (ret < 0) {
        ctx->last_ret = ret;
        if (ret == -ENOENT) {
            return RGW_OBJECT_READ_NOT_FOUND;
        }
        return RGW_OBJECT_READ_ERROR;
    }

    ctx->bytes_read = (size_t)ret;
    ctx->offset = offset + ctx->bytes_read;

    if (ret == 0) {
        return RGW_OBJECT_READ_EOF;
    }

    return RGW_OBJECT_READ_OK;
}

/**
 * @brief 读取对象全部数据
 */
int rgw_object_read_full(rados_ioctx_t ioctx,
                           const char* oid,
                           uint8_t* buffer,
                           size_t buffer_size,
                           size_t* bytes_read) {
    if (!ioctx || !oid || !buffer || !bytes_read) {
        return RGW_ERR_INVALID_ARG;
    }

    *bytes_read = 0;

    /* 首先获取对象大小 */
    uint64_t size;
    time_t mtime;
    int ret = rados_stat(ioctx, oid, &size, &mtime);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    /* 检查对象是否为空 */
    if (size == 0) {
        *bytes_read = 0;
        return RGW_OK;
    }

    /* 限制读取大小 */
    if ((uint64_t)buffer_size < size) {
        size = buffer_size;
    }

    /* 执行读取 */
    ret = rados_read(ioctx, oid, (char*)buffer, size, 0);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    *bytes_read = (size_t)ret;

    return RGW_OK;
}

/**
 * @brief 销毁对象读取上下文
 */
void rgw_object_read_ctx_destroy(rgw_object_read_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    free(ctx->buffer);
    free(ctx);
}

/*============================================================================
 * 函数实现 - 写入操作
 *============================================================================*/

/**
 * @brief 创建对象写入上下文
 */
rgw_object_write_ctx_t* rgw_object_write_ctx_create(rados_ioctx_t ioctx,
                                                       const char* oid,
                                                       size_t buffer_size) {
    if (!ioctx || !oid) {
        return NULL;
    }

    /* 默认缓冲区大小 */
    if (buffer_size == 0) {
        buffer_size = RGW_OBJECT_DEFAULT_WRITE_SIZE;
    }

    /* 限制缓冲区大小 */
    if (buffer_size > RGW_OBJECT_MAX_BUFFER_SIZE) {
        buffer_size = RGW_OBJECT_MAX_BUFFER_SIZE;
    }

    rgw_object_write_ctx_t* ctx = (rgw_object_write_ctx_t*)malloc(
        sizeof(rgw_object_write_ctx_t));
    if (!ctx) {
        return NULL;
    }

    ctx->buffer = (uint8_t*)malloc(buffer_size);
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }

    ctx->ioctx = ioctx;
    ctx->oid = oid;
    ctx->buffer_size = buffer_size;
    ctx->bytes_written = 0;
    ctx->offset = 0;
    ctx->write_flags = 0;
    ctx->last_ret = 0;

    return ctx;
}

/**
 * @brief 写入数据到对象
 */
int rgw_object_write(rgw_object_write_ctx_t* ctx,
                       uint64_t offset,
                       const uint8_t* data,
                       size_t size) {
    if (!ctx || !data) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 执行写入 */
    int ret = rados_write(ctx->ioctx, ctx->oid, (const char*)data, size, offset);

    if (ret < 0) {
        ctx->last_ret = ret;
        return RGW_ERR_IO_ERROR;
    }

    ctx->bytes_written = size;
    ctx->offset = offset + size;

    return RGW_OK;
}

/**
 * @brief 刷新写入缓冲区
 */
int rgw_object_write_flush(rgw_object_write_ctx_t* ctx) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (ctx->bytes_written == 0) {
        return RGW_OK;
    }

    int ret = rados_write(ctx->ioctx, ctx->oid,
                          (const char*)ctx->buffer,
                          ctx->bytes_written,
                          ctx->offset - ctx->bytes_written);

    if (ret < 0) {
        ctx->last_ret = ret;
        return RGW_ERR_IO_ERROR;
    }

    ctx->bytes_written = 0;

    return RGW_OK;
}

/**
 * @brief 写入完整对象
 */
int rgw_object_write_full(rados_ioctx_t ioctx,
                           const char* oid,
                           const uint8_t* data,
                           size_t size,
                           bool exclusive) {
    if (!ioctx || !oid || !data) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用 rados_write_full 原子写入完整对象 */
    int ret;
    if (exclusive) {
        /* 独占创建：先尝试创建 */
        rados_write_op_t op = rados_create_write_op();
        if (!op) {
            return RGW_ERR_OUT_OF_MEMORY;
        }

        rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
        rados_write_op_write(op, (const char*)data, size, 0);

        ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
        rados_release_write_op(op);
    } else {
        /* 普通写入 */
        ret = rados_write_full(ioctx, oid, (const char*)data, size);
    }

    if (ret < 0) {
        if (ret == -EEXIST) {
            return RGW_ERR_ALREADY_EXISTS;
        }
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 追加数据到对象
 */
int rgw_object_append(rgw_ioctx_t ioctx,
                        const char* oid,
                        const uint8_t* data,
                        size_t size) {
    if (!ioctx || !oid || !data) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 检查追加大小限制 */
    if ((int64_t)size > RGW_OBJECT_MAX_APPEND_SIZE) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    int ret = rados_append(ioctx, oid, (const char*)data, size);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 销毁对象写入上下文
 */
void rgw_object_write_ctx_destroy(rgw_object_write_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    /* 刷新未写入的数据 */
    if (ctx->bytes_written > 0) {
        rgw_object_write_flush(ctx);
    }

    free(ctx->buffer);
    free(ctx);
}

/*============================================================================
 * 函数实现 - 删除操作
 *============================================================================*/

/**
 * @brief 删除对象
 */
int rgw_object_delete(rados_ioctx_t ioctx, const char* oid) {
    if (!ioctx || !oid) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = rados_remove(ioctx, oid);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 异步删除对象
 */
int rgw_object_delete_async(rados_ioctx_t ioctx,
                              const char* oid,
                              rados_completion_t completion) {
    if (!ioctx || !oid || !completion) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = rados_aio_remove(ioctx, oid, completion);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 对象属性
 *============================================================================*/

/**
 * @brief 获取对象元数据
 */
int rgw_object_stat(rados_ioctx_t ioctx,
                       const char* oid,
                       rgw_object_meta_t* meta) {
    if (!ioctx || !oid || !meta) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(meta, 0, sizeof(rgw_object_meta_t));

    uint64_t size;
    time_t mtime;

    int ret = rados_stat(ioctx, oid, &size, &mtime);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    meta->size = size;
    meta->mtime = (int64_t)mtime;
    meta->version = 0;
    meta->etag = NULL;

    return RGW_OK;
}

/**
 * @brief 释放对象元数据
 */
void rgw_object_meta_free(rgw_object_meta_t* meta) {
    if (!meta) {
        return;
    }

    free(meta->etag);
    meta->etag = NULL;
}

/**
 * @brief 检查对象是否存在
 */
bool rgw_object_exists(rados_ioctx_t ioctx, const char* oid) {
    if (!ioctx || !oid) {
        return false;
    }

    uint64_t size;
    time_t mtime;

    int ret = rados_stat(ioctx, oid, &size, &mtime);

    return ret == 0;
}

/**
 * @brief 设置对象 xattr
 */
int rgw_object_set_xattr(rados_ioctx_t ioctx,
                            const char* oid,
                            const char* key,
                            const uint8_t* val,
                            size_t val_len) {
    if (!ioctx || !oid || !key || !val) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用写入操作设置 xattr */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    rados_write_op_setxattr(op, key, (const char*)val, val_len);

    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 获取对象 xattr
 */
int rgw_object_get_xattr(rados_ioctx_t ioctx,
                           const char* oid,
                           const char* key,
                           uint8_t** val,
                           size_t* val_len) {
    if (!ioctx || !oid || !key || !val || !val_len) {
        return RGW_ERR_INVALID_ARG;
    }

    *val = NULL;
    *val_len = 0;

    /* 使用读取操作获取 xattr */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    char* get_val;
    size_t get_len;
    int prval = 0;

    /* 获取单个 xattr */
    rados_read_op_getxattr(op, key, &get_val, &prval);

    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0 || prval < 0) {
        if (prval == -ENOENT || ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    /* 获取实际长度 */
    get_len = strlen(get_val);

    /* 复制返回值 */
    *val = (uint8_t*)malloc(get_len + 1);
    if (!*val) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    memcpy(*val, get_val, get_len + 1);
    *val_len = get_len;

    return RGW_OK;
}

/**
 * @brief 删除对象 xattr
 */
int rgw_object_del_xattr(rados_ioctx_t ioctx,
                            const char* oid,
                            const char* key) {
    if (!ioctx || !oid || !key) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用写入操作删除 xattr */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    rados_write_op_rmxattr(op, key);

    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 释放 xattr 值
 */
void rgw_object_xattr_free(uint8_t* val) {
    free(val);
}

/*============================================================================
 * 函数实现 - 对象操作封装
 *============================================================================*/

/**
 * @brief 截断对象
 */
int rgw_object_truncate(rados_ioctx_t ioctx,
                          const char* oid,
                          uint64_t size) {
    if (!ioctx || !oid) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用写入操作截断对象 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    rados_write_op_truncate(op, size);

    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 复制对象
 */
int rgw_object_copy(rados_ioctx_t src_ioctx,
                      const char* src_oid,
                      rados_ioctx_t dst_ioctx,
                      const char* dst_oid) {
    if (!src_ioctx || !src_oid || !dst_ioctx || !dst_oid) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 获取源对象大小 */
    uint64_t size;
    time_t mtime;
    int ret = rados_stat(src_ioctx, src_oid, &size, &mtime);

    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_ERR_NOT_FOUND;
        }
        return RGW_ERR_IO_ERROR;
    }

    /* 分配读取缓冲区 */
    if (size > RGW_OBJECT_MAX_BUFFER_SIZE) {
        size = RGW_OBJECT_MAX_BUFFER_SIZE;
    }

    uint8_t* buffer = (uint8_t*)malloc((size_t)size);
    if (!buffer && size > 0) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 读取源对象 */
    if (size > 0) {
        ret = rados_read(src_ioctx, src_oid, (char*)buffer, (size_t)size, 0);
        if (ret < 0) {
            free(buffer);
            return RGW_ERR_IO_ERROR;
        }
        size = (uint64_t)ret;
    }

    /* 写入目标对象 */
    if (size > 0) {
        ret = rados_write_full(dst_ioctx, dst_oid, (const char*)buffer, (size_t)size);
        if (ret < 0) {
            free(buffer);
            return RGW_ERR_IO_ERROR;
        }
    } else {
        /* 创建空对象 */
        ret = rados_write_full(dst_ioctx, dst_oid, "", 0);
        if (ret < 0 && ret != -ENOENT) {
            free(buffer);
            return RGW_ERR_IO_ERROR;
        }
    }

    free(buffer);

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 对象键构建
 *============================================================================*/

/**
 * @brief 构建对象 OID
 */
int rgw_object_build_oid(const rgw_bucket_layout_t* layout,
                           const char* object_name,
                           char* buf,
                           size_t buf_size) {
    if (!object_name || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 对于普通布局，对象 OID 就是对象名称 */
    size_t name_len = strlen(object_name);
    if (name_len >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    strcpy(buf, object_name);

    return RGW_OK;
}

/**
 * @brief 构建对象实例 OID
 */
int rgw_object_build_instance_oid(const rgw_bucket_layout_t* layout,
                                    const char* object_name,
                                    const char* instance,
                                    char* buf,
                                    size_t buf_size) {
    if (!object_name || !instance || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 格式: {object_name}/{instance} */
    size_t name_len = strlen(object_name);
    size_t inst_len = strlen(instance);
    size_t total_len = name_len + 1 + inst_len;

    if (total_len >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    snprintf(buf, buf_size, "%s/%s", object_name, instance);

    return RGW_OK;
}

/**
 * @brief 构建桶索引对象 OID
 */
int rgw_bucket_index_oid(const char* bucket_id,
                           uint32_t shard_id,
                           char* buf,
                           size_t buf_size) {
    if (!bucket_id || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 格式: .bucket.index.{bucket_id}.{shard_id:05d} */
    int ret = snprintf(buf, buf_size, ".bucket.index.%s.%05u",
                       bucket_id, shard_id);

    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

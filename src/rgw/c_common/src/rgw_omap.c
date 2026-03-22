/**
 * @file rgw_omap.c
 * @brief OMAP 操作封装实现
 *
 * 实现 OMAP 操作的 C 接口封装。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "rgw_omap.h"
#include "rgw_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** librados 比较操作映射到 librados.h 定义 */
#define LIBRADOS_CMP_OP_EQ LIBRADOS_CMPXATTR_OP_EQ
#define LIBRADOS_CMP_OP_NE LIBRADOS_CMPXATTR_OP_NE
#define LIBRADOS_CMP_OP_GT LIBRADOS_CMPXATTR_OP_GT
#define LIBRADOS_CMP_OP_GTE LIBRADOS_CMPXATTR_OP_GTE
#define LIBRADOS_CMP_OP_LT LIBRADOS_CMPXATTR_OP_LT
#define LIBRADOS_CMP_OP_LTE LIBRADOS_CMPXATTR_OP_LTE

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 将比较操作转换为 librados 格式
 */
static uint8_t rgw_omap_cmp_op_to_librados(rgw_omap_cmp_op_t op) {
    switch (op) {
        case RGW_OMAP_CMP_OP_EQ:
            return LIBRADOS_CMPXATTR_OP_EQ;
        case RGW_OMAP_CMP_OP_NE:
            return LIBRADOS_CMPXATTR_OP_NE;
        case RGW_OMAP_CMP_OP_GT:
            return LIBRADOS_CMPXATTR_OP_GT;
        case RGW_OMAP_CMP_OP_GTE:
            return LIBRADOS_CMPXATTR_OP_GTE;
        case RGW_OMAP_CMP_OP_LT:
            return LIBRADOS_CMPXATTR_OP_LT;
        case RGW_OMAP_CMP_OP_LTE:
            return LIBRADOS_CMPXATTR_OP_LTE;
        default:
            return LIBRADOS_CMPXATTR_OP_EQ;
    }
}

/**
 * @brief 释放键数组内存
 */
static void rgw_omap_free_keys_array(char** keys, size_t count) {
    if (!keys) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(keys[i]);
    }
    free(keys);
}

/*============================================================================
 * 函数实现 - 基础操作
 *============================================================================*/

/**
 * @brief 获取单个 OMAP 值
 */
int rgw_omap_get(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key,
                  uint8_t** val,
                  size_t* val_len) {
    if (!ioctx || !oid || !key || !val || !val_len) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 获取指定键的值 */
    rados_omap_iter_t iter;
    int prval = 0;
    rados_read_op_omap_get_vals_by_keys(op, &key, 1, &iter, &prval);

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    if (prval < 0) {
        return prval == -ENOENT ? RGW_ERR_NOT_FOUND : RGW_ERR_IO_ERROR;
    }

    /* 获取值 */
    char* got_key = NULL;
    char* got_val = NULL;
    size_t got_val_len = 0;

    ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_val_len, &prval);
    rados_omap_get_end(iter);

    if (ret < 0 || !got_key) {
        return RGW_ERR_NOT_FOUND;
    }

    /* 复制值 */
    uint8_t* buf = (uint8_t*)malloc(got_val_len);
    if (!buf) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    memcpy(buf, got_val, got_val_len);
    *val = buf;
    *val_len = got_val_len;

    return RGW_OK;
}

/**
 * @brief 设置单个 OMAP 键值对
 */
int rgw_omap_set(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key,
                  const uint8_t* val,
                  size_t val_len,
                  bool exclusive) {
    if (!ioctx || !oid || !key || !val) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 设置值 */
    rados_write_op_omap_set2(op, &key, &val, &val_len, &val_len, 1);

    /* 如果需要独占创建 */
    if (exclusive) {
        rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
    }

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return ret == -EEXIST ? RGW_ERR_ALREADY_EXISTS : RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 删除单个 OMAP 键
 */
int rgw_omap_del(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key) {
    if (!ioctx || !oid || !key) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 删除键 */
    rados_write_op_omap_rm_keys2(op, &key, &key, 1);

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 批量操作
 *============================================================================*/

/**
 * @brief 设置多个 OMAP 键值对
 */
int rgw_omap_set_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const rgw_omap_kv_t* kvs,
                        size_t num_kvs,
                        bool exclusive) {
    if (!ioctx || !oid || !kvs || num_kvs == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    if (num_kvs > RGW_OMAP_MAX_KEYS_PER_OP) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 准备键值对数组 */
    const char** keys = (const char**)malloc(sizeof(char*) * num_kvs);
    const uint8_t** vals = (const uint8_t**)malloc(sizeof(uint8_t*) * num_kvs);
    size_t* key_lens = (size_t*)malloc(sizeof(size_t) * num_kvs);
    size_t* val_lens = (size_t*)malloc(sizeof(size_t) * num_kvs);

    if (!keys || !vals || !key_lens || !val_lens) {
        free(keys);
        free(vals);
        free(key_lens);
        free(val_lens);
        rados_release_write_op(op);
        return RGW_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < num_kvs; i++) {
        keys[i] = kvs[i].key;
        vals[i] = kvs[i].val;
        key_lens[i] = strlen(kvs[i].key);
        val_lens[i] = kvs[i].val_len;
    }

    /* 设置值 */
    rados_write_op_omap_set2(op, keys, vals, key_lens, val_lens, num_kvs);

    /* 如果需要独占创建 */
    if (exclusive) {
        rados_write_op_create(op, LIBRADOS_CREATE_EXCLUSIVE, NULL);
    }

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    free(keys);
    free(vals);
    free(key_lens);
    free(val_lens);

    if (ret < 0) {
        return ret == -EEXIST ? RGW_ERR_ALREADY_EXISTS : RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 删除多个 OMAP 键
 */
int rgw_omap_del_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const char** keys,
                        size_t num_keys) {
    if (!ioctx || !oid || !keys || num_keys == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    if (num_keys > RGW_OMAP_MAX_KEYS_PER_OP) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 准备键长度数组 */
    size_t* key_lens = (size_t*)malloc(sizeof(size_t) * num_keys);
    if (!key_lens) {
        rados_release_write_op(op);
        return RGW_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < num_keys; i++) {
        key_lens[i] = strlen(keys[i]);
    }

    /* 删除键 */
    rados_write_op_omap_rm_keys2(op, keys, key_lens, num_keys);

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    free(key_lens);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 获取多个 OMAP 键值对
 */
int rgw_omap_get_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const char** keys,
                        size_t num_keys,
                        rgw_omap_kv_array_t* result) {
    if (!ioctx || !oid || !result) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(rgw_omap_kv_array_t));

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 获取迭代器 */
    rados_omap_iter_t iter;
    int prval = 0;

    if (keys && num_keys > 0) {
        rados_read_op_omap_get_vals_by_keys2(op, keys, num_keys, NULL, &iter, &prval);
    } else {
        rados_read_op_omap_get_vals2(op, NULL, NULL, RGW_OMAP_DEFAULT_PAGE_SIZE,
                                     &iter, NULL, &prval);
    }

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    /* 遍历结果 */
    size_t capacity = num_keys > 0 ? num_keys : RGW_OMAP_DEFAULT_PAGE_SIZE;
    result->kvs = (rgw_omap_kv_t*)malloc(sizeof(rgw_omap_kv_t) * capacity);
    if (!result->kvs) {
        rados_omap_get_end(iter);
        return RGW_ERR_OUT_OF_MEMORY;
    }

    result->capacity = capacity;
    result->count = 0;

    while (true) {
        char* got_key = NULL;
        char* got_val = NULL;
        size_t got_key_len = 0;
        size_t got_val_len = 0;

        ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_key_len, &got_val_len);
        if (ret < 0 || !got_key) {
            break;
        }

        /* 扩容检查 */
        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity * 2;
            rgw_omap_kv_t* new_kvs = (rgw_omap_kv_t*)realloc(
                result->kvs, sizeof(rgw_omap_kv_t) * new_capacity);
            if (!new_kvs) {
                break;
            }
            result->kvs = new_kvs;
            result->capacity = new_capacity;
        }

        /* 复制键值对 */
        result->kvs[result->count].key = (char*)malloc(got_key_len + 1);
        if (!result->kvs[result->count].key) {
            break;
        }
        memcpy(result->kvs[result->count].key, got_key, got_key_len);
        result->kvs[result->count].key[got_key_len] = '\0';

        result->kvs[result->count].val = (uint8_t*)malloc(got_val_len);
        if (!result->kvs[result->count].val) {
            free(result->kvs[result->count].key);
            break;
        }
        memcpy(result->kvs[result->count].val, got_val, got_val_len);
        result->kvs[result->count].val_len = got_val_len;

        result->count++;
    }

    rados_omap_get_end(iter);

    return RGW_OK;
}

/**
 * @brief 获取所有 OMAP 键值对
 */
int rgw_omap_get_all(rados_ioctx_t ioctx,
                      const char* oid,
                      const char* start_after,
                      uint64_t max_return,
                      rgw_omap_kv_array_t* result) {
    if (!ioctx || !oid || !result) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(rgw_omap_kv_array_t));

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 获取迭代器 */
    rados_omap_iter_t iter;
    int prval = 0;
    rados_read_op_omap_get_vals2(op, start_after, NULL, max_return,
                                  &iter, NULL, &prval);

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    /* 遍历结果 */
    size_t capacity = max_return > 0 ? max_return : RGW_OMAP_DEFAULT_PAGE_SIZE;
    result->kvs = (rgw_omap_kv_t*)malloc(sizeof(rgw_omap_kv_t) * capacity);
    if (!result->kvs) {
        rados_omap_get_end(iter);
        return RGW_ERR_OUT_OF_MEMORY;
    }

    result->capacity = capacity;
    result->count = 0;

    while (true) {
        char* got_key = NULL;
        char* got_val = NULL;
        size_t got_key_len = 0;
        size_t got_val_len = 0;

        ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_key_len, &got_val_len);
        if (ret < 0 || !got_key) {
            break;
        }

        /* 扩容检查 */
        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity * 2;
            rgw_omap_kv_t* new_kvs = (rgw_omap_kv_t*)realloc(
                result->kvs, sizeof(rgw_omap_kv_t) * new_capacity);
            if (!new_kvs) {
                break;
            }
            result->kvs = new_kvs;
            result->capacity = new_capacity;
        }

        /* 复制键值对 */
        result->kvs[result->count].key = (char*)malloc(got_key_len + 1);
        if (!result->kvs[result->count].key) {
            break;
        }
        memcpy(result->kvs[result->count].key, got_key, got_key_len);
        result->kvs[result->count].key[got_key_len] = '\0';

        result->kvs[result->count].val = (uint8_t*)malloc(got_val_len);
        if (!result->kvs[result->count].val) {
            free(result->kvs[result->count].key);
            break;
        }
        memcpy(result->kvs[result->count].val, got_val, got_val_len);
        result->kvs[result->count].val_len = got_val_len;

        result->count++;
    }

    rados_omap_get_end(iter);

    return RGW_OK;
}

/**
 * @brief 获取所有 OMAP 键
 */
int rgw_omap_get_keys(rados_ioctx_t ioctx,
                      const char* oid,
                      const char* start_after,
                      uint64_t max_return,
                      char*** keys,
                      size_t* keys_count) {
    if (!ioctx || !oid || !keys || !keys_count) {
        return RGW_ERR_INVALID_ARG;
    }

    *keys = NULL;
    *keys_count = 0;

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 获取迭代器 */
    rados_omap_iter_t iter;
    int prval = 0;
    rados_read_op_omap_get_keys2(op, start_after, max_return,
                                  &iter, NULL, &prval);

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    /* 遍历结果 */
    size_t capacity = max_return > 0 ? max_return : RGW_OMAP_DEFAULT_PAGE_SIZE;
    char** result_keys = (char**)malloc(sizeof(char*) * capacity);
    if (!result_keys) {
        rados_omap_get_end(iter);
        return RGW_ERR_OUT_OF_MEMORY;
    }

    size_t count = 0;

    while (true) {
        char* got_key = NULL;
        char* got_val = NULL;
        size_t got_key_len = 0;
        size_t got_val_len = 0;

        ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_key_len, &got_val_len);
        if (ret < 0 || !got_key) {
            break;
        }

        /* 扩容检查 */
        if (count >= capacity) {
            size_t new_capacity = capacity * 2;
            char** new_keys = (char**)realloc(result_keys, sizeof(char*) * new_capacity);
            if (!new_keys) {
                break;
            }
            result_keys = new_keys;
            capacity = new_capacity;
        }

        /* 复制键 */
        result_keys[count] = (char*)malloc(got_key_len + 1);
        if (!result_keys[count]) {
            break;
        }
        memcpy(result_keys[count], got_key, got_key_len);
        result_keys[count][got_key_len] = '\0';

        count++;
    }

    rados_omap_get_end(iter);

    *keys = result_keys;
    *keys_count = count;

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - CAS 操作
 *============================================================================*/

/**
 * @brief 比较并交换 OMAP 值
 */
int rgw_omap_cmp_and_set(rados_ioctx_t ioctx,
                           const char* oid,
                           const char* key,
                           const uint8_t* expected_val,
                           size_t expected_len,
                           const uint8_t* new_val,
                           size_t new_len) {
    if (!ioctx || !oid || !key) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 添加比较操作 */
    int prval = 0;
    rados_write_op_omap_cmp2(op, key, LIBRADOS_CMPXATTR_OP_EQ,
                             expected_val, strlen(key), expected_len, &prval);

    /* 如果比较成功则设置新值 */
    rados_write_op_omap_set2(op, &key, &new_val, &new_len, &new_len, 1);

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        if (prval < 0) {
            return prval;
        }
        return RGW_ERR_IO_ERROR;
    }

    if (prval < 0) {
        return prval == -ECANCELED ? RGW_ERR_ALREADY_EXISTS : prval;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 写入操作上下文
 *============================================================================*/

/**
 * @brief 创建 OMAP 写入上下文
 */
rgw_omap_write_ctx_t* rgw_omap_write_ctx_create(rados_ioctx_t ioctx,
                                                  const char* oid) {
    if (!ioctx || !oid) {
        return NULL;
    }

    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return NULL;
    }

    rgw_omap_write_ctx_t* ctx = (rgw_omap_write_ctx_t*)malloc(
        sizeof(rgw_omap_write_ctx_t));
    if (!ctx) {
        rados_release_write_op(op);
        return NULL;
    }

    ctx->op = op;
    ctx->ioctx = ioctx;
    ctx->oid = oid;
    ctx->result = 0;

    return ctx;
}

/**
 * @brief 添加 OMAP 键值对到写入上下文
 */
int rgw_omap_write_ctx_add(rgw_omap_write_ctx_t* ctx,
                             const char* key,
                             const uint8_t* val,
                             size_t val_len) {
    if (!ctx || !key || !val) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 使用简化版本的 omap_set2 */
    rados_write_op_omap_set2(ctx->op, &key, &val, &val_len, &val_len, 1);

    return RGW_OK;
}

/**
 * @brief 添加删除键操作到写入上下文
 */
int rgw_omap_write_ctx_del(rgw_omap_write_ctx_t* ctx,
                              const char* key) {
    if (!ctx || !key) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t key_len = strlen(key);
    rados_write_op_omap_rm_keys2(ctx->op, &key, &key_len, 1);

    return RGW_OK;
}

/**
 * @brief 添加断言操作到写入上下文
 */
int rgw_omap_write_ctx_assert(rgw_omap_write_ctx_t* ctx,
                                 bool check_exists,
                                 uint64_t expected_version) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (check_exists) {
        rados_write_op_assert_exists(ctx->op);
    }

    if (expected_version > 0) {
        rados_write_op_assert_version(ctx->op, expected_version);
    }

    return RGW_OK;
}

/**
 * @brief 添加对象创建操作到写入上下文
 */
int rgw_omap_write_ctx_set_create_flags(rgw_omap_write_ctx_t* ctx,
                                        rgw_omap_create_flags_t flags) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    int lib_flags = (flags == RGW_OMAP_CREATE_EXCLUSIVE) ?
        LIBRADOS_CREATE_EXCLUSIVE : LIBRADOS_CREATE_IDEMPOTENT;

    rados_write_op_create(ctx->op, lib_flags, NULL);

    return RGW_OK;
}

/**
 * @brief 执行 OMAP 写入操作
 */
int rgw_omap_write_ctx_execute(rgw_omap_write_ctx_t* ctx,
                                  time_t* mtime,
                                  int flags) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = rados_write_op_operate(ctx->op, ctx->ioctx, ctx->oid, mtime, flags);
    ctx->result = ret;

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 销毁 OMAP 写入上下文
 */
void rgw_omap_write_ctx_destroy(rgw_omap_write_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->op) {
        rados_release_write_op(ctx->op);
    }

    free(ctx);
}

/*============================================================================
 * 函数实现 - 读取操作上下文
 *============================================================================*/

/**
 * @brief 创建 OMAP 读取上下文
 */
rgw_omap_read_ctx_t* rgw_omap_read_ctx_create(rados_ioctx_t ioctx,
                                                const char* oid) {
    if (!ioctx || !oid) {
        return NULL;
    }

    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return NULL;
    }

    rgw_omap_read_ctx_t* ctx = (rgw_omap_read_ctx_t*)malloc(
        sizeof(rgw_omap_read_ctx_t));
    if (!ctx) {
        rados_release_read_op(op);
        return NULL;
    }

    ctx->op = op;
    ctx->ioctx = ioctx;
    ctx->oid = oid;
    ctx->result = 0;

    return ctx;
}

/**
 * @brief 添加断言操作到读取上下文
 */
int rgw_omap_read_ctx_assert(rgw_omap_read_ctx_t* ctx,
                                bool check_exists) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    if (check_exists) {
        rados_read_op_assert_exists(ctx->op);
    }

    return RGW_OK;
}

/**
 * @brief 添加获取值操作到读取上下文
 */
int rgw_omap_read_ctx_get_vals(rgw_omap_read_ctx_t* ctx,
                                  const char** keys,
                                  size_t num_keys,
                                  rgw_omap_kv_array_t* result) {
    if (!ctx || !result) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(result, 0, sizeof(rgw_omap_kv_array_t));

    /* 获取迭代器 */
    rados_omap_iter_t iter;
    int prval = 0;

    if (keys && num_keys > 0) {
        rados_read_op_omap_get_vals_by_keys2(ctx->op, keys, num_keys, NULL, &iter, &prval);
    } else {
        rados_read_op_omap_get_vals2(ctx->op, NULL, NULL, RGW_OMAP_DEFAULT_PAGE_SIZE,
                                     &iter, NULL, &prval);
    }

    /* 遍历结果 */
    size_t capacity = num_keys > 0 ? num_keys : RGW_OMAP_DEFAULT_PAGE_SIZE;
    result->kvs = (rgw_omap_kv_t*)malloc(sizeof(rgw_omap_kv_t) * capacity);
    if (!result->kvs) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    result->capacity = capacity;
    result->count = 0;

    while (true) {
        char* got_key = NULL;
        char* got_val = NULL;
        size_t got_key_len = 0;
        size_t got_val_len = 0;

        int ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_key_len, &got_val_len);
        if (ret < 0 || !got_key) {
            break;
        }

        if (result->count >= result->capacity) {
            size_t new_capacity = result->capacity * 2;
            rgw_omap_kv_t* new_kvs = (rgw_omap_kv_t*)realloc(
                result->kvs, sizeof(rgw_omap_kv_t) * new_capacity);
            if (!new_kvs) {
                break;
            }
            result->kvs = new_kvs;
            result->capacity = new_capacity;
        }

        result->kvs[result->count].key = (char*)malloc(got_key_len + 1);
        if (!result->kvs[result->count].key) {
            break;
        }
        memcpy(result->kvs[result->count].key, got_key, got_key_len);
        result->kvs[result->count].key[got_key_len] = '\0';

        result->kvs[result->count].val = (uint8_t*)malloc(got_val_len);
        if (!result->kvs[result->count].val) {
            free(result->kvs[result->count].key);
            break;
        }
        memcpy(result->kvs[result->count].val, got_val, got_val_len);
        result->kvs[result->count].val_len = got_val_len;

        result->count++;
    }

    rados_omap_get_end(iter);

    return RGW_OK;
}

/**
 * @brief 执行 OMAP 读取操作
 */
int rgw_omap_read_ctx_execute(rgw_omap_read_ctx_t* ctx,
                                 int flags) {
    if (!ctx) {
        return RGW_ERR_INVALID_ARG;
    }

    int ret = rados_read_op_operate(ctx->op, ctx->ioctx, ctx->oid, flags);
    ctx->result = ret;

    if (ret < 0) {
        return ret == -ENOENT ? RGW_ERR_NOT_FOUND : RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/**
 * @brief 销毁 OMAP 读取上下文
 */
void rgw_omap_read_ctx_destroy(rgw_omap_read_ctx_t* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->op) {
        rados_release_read_op(ctx->op);
    }

    free(ctx);
}

/*============================================================================
 * 函数实现 - 迭代器
 *============================================================================*/

/**
 * @brief 创建 OMAP 迭代器
 */
rgw_omap_iter_t* rgw_omap_iter_create(rados_ioctx_t ioctx,
                                         const char* oid,
                                         const char* start_after,
                                         const char* filter_prefix,
                                         uint64_t max_return) {
    if (!ioctx || !oid) {
        return NULL;
    }

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return NULL;
    }

    /* 获取迭代器 */
    rados_omap_iter_t iter;
    int prval = 0;
    rados_read_op_omap_get_vals2(op, start_after, filter_prefix, max_return,
                                  &iter, NULL, &prval);

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        return NULL;
    }

    rgw_omap_iter_t* ctx = (rgw_omap_iter_t*)malloc(sizeof(rgw_omap_iter_t));
    if (!ctx) {
        rados_omap_get_end(iter);
        return NULL;
    }

    ctx->iter = iter;
    ctx->cur_key = NULL;
    ctx->cur_val = NULL;
    ctx->cur_val_len = 0;
    ctx->ended = false;

    return ctx;
}

/**
 * @brief 获取迭代器的下一个键值对
 */
int rgw_omap_iter_next(rgw_omap_iter_t* iter,
                         const char** key,
                         const uint8_t** val,
                         size_t* val_len) {
    if (!iter || !key || !val || !val_len) {
        return RGW_ERR_INVALID_ARG;
    }

    if (iter->ended) {
        return 0;
    }

    /* 释放上一次的键 */
    free(iter->cur_key);
    free(iter->cur_val);
    iter->cur_key = NULL;
    iter->cur_val = NULL;

    /* 获取下一个值 */
    char* got_key = NULL;
    char* got_val = NULL;
    size_t got_key_len = 0;
    size_t got_val_len = 0;
    int prval = 0;

    int ret = rados_omap_get_next2(iter->iter, &got_key, &got_val,
                                     &got_key_len, &got_val_len);

    if (ret < 0) {
        iter->ended = true;
        return RGW_ERR_IO_ERROR;
    }

    if (!got_key) {
        iter->ended = true;
        return 0;
    }

    /* 复制键 */
    iter->cur_key = (char*)malloc(got_key_len + 1);
    if (iter->cur_key) {
        memcpy(iter->cur_key, got_key, got_key_len);
        iter->cur_key[got_key_len] = '\0';
    }

    /* 复制值 */
    iter->cur_val = (uint8_t*)malloc(got_val_len);
    if (iter->cur_val) {
        memcpy(iter->cur_val, got_val, got_val_len);
        iter->cur_val_len = got_val_len;
    }

    *key = iter->cur_key;
    *val = iter->cur_val;
    *val_len = iter->cur_val_len;

    return 1;
}

/**
 * @brief 检查迭代器是否结束
 */
bool rgw_omap_iter_ended(const rgw_omap_iter_t* iter) {
    if (!iter) {
        return true;
    }
    return iter->ended;
}

/**
 * @brief 销毁 OMAP 迭代器
 */
void rgw_omap_iter_destroy(rgw_omap_iter_t* iter) {
    if (!iter) {
        return;
    }

    if (iter->iter) {
        rados_omap_get_end(iter->iter);
    }

    free(iter->cur_key);
    free(iter->cur_val);
    free(iter);
}

/*============================================================================
 * 函数实现 - 内存管理
 *============================================================================*/

/**
 * @brief 释放 OMAP 值内存
 */
void rgw_omap_free_value(uint8_t* val) {
    free(val);
}

/**
 * @brief 释放 OMAP 键值对数组
 */
void rgw_omap_kv_array_free(rgw_omap_kv_array_t* array) {
    if (!array || !array->kvs) {
        return;
    }

    for (size_t i = 0; i < array->count; i++) {
        free(array->kvs[i].key);
        free(array->kvs[i].val);
    }

    free(array->kvs);
    array->kvs = NULL;
    array->count = 0;
    array->capacity = 0;
}

/**
 * @brief 创建 OMAP 键值对
 */
rgw_omap_kv_t* rgw_omap_kv_create(const char* key,
                                     const uint8_t* val,
                                     size_t val_len) {
    if (!key || !val) {
        return NULL;
    }

    rgw_omap_kv_t* kv = (rgw_omap_kv_t*)malloc(sizeof(rgw_omap_kv_t));
    if (!kv) {
        return NULL;
    }

    kv->key = (char*)malloc(strlen(key) + 1);
    if (!kv->key) {
        free(kv);
        return NULL;
    }
    strcpy(kv->key, key);

    kv->val = (uint8_t*)malloc(val_len);
    if (!kv->val) {
        free(kv->key);
        free(kv);
        return NULL;
    }
    memcpy(kv->val, val, val_len);
    kv->val_len = val_len;

    return kv;
}

/**
 * @brief 释放单个 OMAP 键值对
 */
void rgw_omap_kv_free(rgw_omap_kv_t* kv) {
    if (!kv) {
        return;
    }

    free(kv->key);
    free(kv->val);
    free(kv);
}

/*============================================================================
 * 函数实现 - 工具函数
 *============================================================================*/

/**
 * @brief 检查对象是否存在
 */
bool rgw_omap_exists(rados_ioctx_t ioctx, const char* oid) {
    if (!ioctx || !oid) {
        return false;
    }

    /* 创建读取操作检查对象存在性 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return false;
    }

    rados_read_op_assert_exists(op);

    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    return ret == 0;
}

/**
 * @brief 获取 OMAP 键数量
 */
int64_t rgw_omap_count(rados_ioctx_t ioctx, const char* oid) {
    if (!ioctx || !oid) {
        return -1;
    }

    /* 创建读取操作 */
    rados_read_op_t op = rados_create_read_op();
    if (!op) {
        return -1;
    }

    /* 获取键迭代器 */
    rados_omap_iter_t iter;
    unsigned char pmore = 0;
    int prval = 0;
    rados_read_op_omap_get_keys2(op, NULL, UINT64_MAX, &iter, &pmore, &prval);

    /* 执行操作 */
    int ret = rados_read_op_operate(op, ioctx, oid, 0);
    rados_release_read_op(op);

    if (ret < 0) {
        rados_omap_get_end(iter);
        return -1;
    }

    /* 计算键数量 */
    int64_t count = 0;
    while (true) {
        char* got_key = NULL;
        char* got_val = NULL;
        size_t got_key_len = 0;
        size_t got_val_len = 0;

        ret = rados_omap_get_next2(iter, &got_key, &got_val, &got_key_len, &got_val_len);
        if (ret < 0 || !got_key) {
            break;
        }
        count++;
    }

    rados_omap_get_end(iter);

    return count;
}

/**
 * @brief 清空对象的 OMAP
 */
int rgw_omap_clear(rados_ioctx_t ioctx, const char* oid) {
    if (!ioctx || !oid) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 创建写入操作 */
    rados_write_op_t op = rados_create_write_op();
    if (!op) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    /* 清空所有 OMAP 键值对 */
    rados_write_op_omap_clear(op);

    /* 执行操作 */
    int ret = rados_write_op_operate(op, ioctx, oid, NULL, 0);
    rados_release_write_op(op);

    if (ret < 0) {
        return RGW_ERR_IO_ERROR;
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 原子事务支持
 *============================================================================*/

/**
 * @brief 开始 OMAP 事务
 */
void* rgw_omap_txn_begin(rados_ioctx_t ioctx, const char* oid) {
    /* 简化实现：返回 NULL 表示不支持跨对象事务 */
    (void)ioctx;
    (void)oid;
    return NULL;
}

/**
 * @brief 提交 OMAP 事务
 */
int rgw_omap_txn_commit(rados_ioctx_t ioctx, void* txn) {
    (void)ioctx;
    (void)txn;
    if (txn) {
        return RGW_ERR_INVALID_ARG;
    }
    return RGW_OK;
}

/**
 * @brief 中止 OMAP 事务
 */
int rgw_omap_txn_abort(rados_ioctx_t ioctx, void* txn) {
    (void)ioctx;
    (void)txn;
    if (txn) {
        return RGW_ERR_INVALID_ARG;
    }
    return RGW_OK;
}

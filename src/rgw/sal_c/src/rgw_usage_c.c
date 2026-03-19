/**
 * @file rgw_usage_c.c
 * @brief SAL C 接口 - Usage 访问模块实现
 *
 * 实现 rgw_usage_c.h 中定义的 Usage 统计访问函数。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <rados/librados.h>

#include "rgw_usage_c.h"
#include "rgw_sal_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

#define USAGE_ENTRIES_INITIAL_CAPACITY 16
#define DYNAMIC_ARRAY_GROWTH_FACTOR 2
#define SERIALIZATION_VERSION 4

/*============================================================================
 * 辅助函数
 *============================================================================*/

/**
 * @brief 简单的字符串哈希函数
 *
 * 这是简化实现。
 */
static uint32_t simple_string_hash(const char* str, size_t len) {
    if (!str || len == 0) return 0;

    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = hash * 33 + (uint8_t)str[i];
    }
    return hash;
}

/*============================================================================
 * Usage 迭代器函数实现
 *============================================================================*/

rgw_usage_iter_t* rgw_usage_iter_create(void) {
    rgw_usage_iter_t* iter = calloc(1, sizeof(rgw_usage_iter_t));
    return iter;
}

void rgw_usage_iter_reset(rgw_usage_iter_t* iter) {
    if (!iter) return;
    free(iter->read_iter);
    iter->read_iter = NULL;
    iter->index = 0;
}

void rgw_usage_iter_destroy(rgw_usage_iter_t* iter) {
    if (!iter) return;
    free(iter->read_iter);
    free(iter);
}

/*============================================================================
 * Usage 哈希计算函数实现
 *============================================================================*/

void rgw_usage_log_hash(const char* name, uint32_t index, char* hash) {
    uint32_t max_shards = RGW_USAGE_DEFAULT_MAX_SHARDS;
    uint32_t max_user_shards = RGW_USAGE_DEFAULT_MAX_USER_SHARDS;

    /* 计算哈希值 */
    uint32_t val = index;

    if (name && name[0] != '\0') {
        uint32_t name_hash = simple_string_hash(name, strlen(name));
        val %= max_user_shards;
        val += name_hash;
    }

    /* 生成对象 ID (与 C++ 版本兼容) */
    snprintf(hash, RGW_USAGE_HASH_LEN, "%010u", val % max_shards);
}

int rgw_usage_generate_oid(uint32_t index, char* buf, size_t buf_size) {
    if (!buf || buf_size < 32) return RGW_SAL_ERR_INVALID_ARG;

    snprintf(buf, buf_size, "%s%u", RGW_USAGE_OBJ_PREFIX, index);
    return RGW_SAL_OK;
}

/*============================================================================
 * Usage 条目函数实现
 *============================================================================*/

rgw_usage_log_entry_t* rgw_usage_log_entry_create(void) {
    rgw_usage_log_entry_t* entry = calloc(1, sizeof(rgw_usage_log_entry_t));
    return entry;
}

void rgw_usage_log_entry_destroy(rgw_usage_log_entry_t* entry) {
    if (!entry) return;

    free(entry->owner_id);
    free(entry->payer_id);
    free(entry->bucket);
    free(entry);
}

void rgw_usage_log_entry_aggregate(rgw_usage_log_entry_t* target,
                                    const rgw_usage_log_entry_t* source) {
    if (!target || !source) return;

    /* 如果 target 为空，从 source 复制基础信息 */
    if (!target->owner_id && source->owner_id) {
        target->owner_id = strdup(source->owner_id);
        target->bucket = source->bucket ? strdup(source->bucket) : NULL;
        target->epoch = source->epoch;
        if (source->payer_id) {
            target->payer_id = strdup(source->payer_id);
        }
    }

    /* 聚合 total_usage */
    target->total_usage.bytes_sent += source->total_usage.bytes_sent;
    target->total_usage.bytes_received += source->total_usage.bytes_received;
    target->total_usage.ops += source->total_usage.ops;
    target->total_usage.successful_ops += source->total_usage.successful_ops;

    /* 聚合 category_usage */
    target->category_usage.bytes_sent += source->category_usage.bytes_sent;
    target->category_usage.bytes_received += source->category_usage.bytes_received;
    target->category_usage.ops += source->category_usage.ops;
    target->category_usage.successful_ops += source->category_usage.successful_ops;

    /* 聚合 s3select_usage */
    target->s3select_usage.bytes_processed += source->s3select_usage.bytes_processed;
    target->s3select_usage.bytes_returned += source->s3select_usage.bytes_returned;
}

/*============================================================================
 * Usage 集合函数实现
 *============================================================================*/

static int rgw_usage_entries_resize(rgw_usage_entries_t* entries, size_t new_capacity) {
    if (!entries) return RGW_SAL_ERR_INVALID_ARG;

    if (new_capacity <= entries->capacity) {
        return RGW_SAL_OK;
    }

    rgw_usage_log_entry_t* new_entries = realloc(entries->entries,
                                                  new_capacity * sizeof(rgw_usage_log_entry_t));
    if (!new_entries) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    entries->entries = new_entries;
    entries->capacity = new_capacity;
    return RGW_SAL_OK;
}

rgw_usage_entries_t* rgw_usage_entries_create(void) {
    rgw_usage_entries_t* entries = calloc(1, sizeof(rgw_usage_entries_t));
    if (entries) {
        entries->entries = calloc(USAGE_ENTRIES_INITIAL_CAPACITY, sizeof(rgw_usage_log_entry_t));
        if (entries->entries) {
            entries->capacity = USAGE_ENTRIES_INITIAL_CAPACITY;
        } else {
            free(entries);
            return NULL;
        }
    }
    return entries;
}

void rgw_usage_entries_destroy(rgw_usage_entries_t* entries) {
    if (!entries) return;

    if (entries->entries) {
        for (size_t i = 0; i < entries->count; i++) {
            free(entries->entries[i].owner_id);
            free(entries->entries[i].payer_id);
            free(entries->entries[i].bucket);
        }
        free(entries->entries);
    }
    free(entries);
}

int rgw_usage_entries_add(rgw_usage_entries_t* entries,
                            const rgw_usage_log_entry_t* entry) {
    if (!entries || !entry) return RGW_SAL_ERR_INVALID_ARG;

    if (entries->count >= entries->capacity) {
        int ret = rgw_usage_entries_resize(entries, entries->capacity * DYNAMIC_ARRAY_GROWTH_FACTOR);
        if (ret != RGW_SAL_OK) return ret;
    }

    /* 复制 entry */
    rgw_usage_log_entry_t* dest = &entries->entries[entries->count];
    memset(dest, 0, sizeof(*dest));

    if (entry->owner_id) dest->owner_id = strdup(entry->owner_id);
    if (entry->payer_id) dest->payer_id = strdup(entry->payer_id);
    if (entry->bucket) dest->bucket = strdup(entry->bucket);
    dest->epoch = entry->epoch;
    dest->total_usage = entry->total_usage;
    dest->category_usage = entry->category_usage;
    dest->s3select_usage = entry->s3select_usage;

    entries->count++;
    return RGW_SAL_OK;
}

int rgw_usage_entries_aggregate(rgw_usage_entries_t* entries,
                                   const char* bucket_name,
                                   const rgw_usage_log_entry_t* entry) {
    if (!entries || !bucket_name || !entry) return RGW_SAL_ERR_INVALID_ARG;

    /* 查找是否已存在 */
    for (size_t i = 0; i < entries->count; i++) {
        if (entries->entries[i].bucket &&
            strcmp(entries->entries[i].bucket, bucket_name) == 0) {
            rgw_usage_log_entry_aggregate(&entries->entries[i], entry);
            return RGW_SAL_OK;
        }
    }

    /* 不存在，添加新的 */
    return rgw_usage_entries_add(entries, entry);
}

/*============================================================================
 * Usage 序列化函数实现
 *============================================================================*/

int rgw_usage_log_entry_encode(const rgw_usage_log_entry_t* entry,
                                  uint8_t* buf, size_t buf_size, size_t* out_size) {
    if (!entry || !buf || !out_size) return RGW_SAL_ERR_INVALID_ARG;

    /*
     * 二进制格式 (version 4):
     * - version: 1 byte
     * - epoch: 8 bytes
     * - owner_len: 4 bytes + owner string
     * - bucket_len: 4 bytes + bucket string
     * - bytes_sent: 8 bytes
     * - bytes_received: 8 bytes
     * - ops: 8 bytes
     * - successful_ops: 8 bytes
     * - s3select_processed: 8 bytes
     * - s3select_returned: 8 bytes
     */

    size_t offset = 0;

    /* 写入 version */
    buf[offset++] = SERIALIZATION_VERSION;

    /* 写入 epoch */
    memcpy(&buf[offset], &entry->epoch, sizeof(entry->epoch));
    offset += sizeof(entry->epoch);

    /* 计算 owner 和 bucket 长度 */
    size_t owner_len = entry->owner_id ? strlen(entry->owner_id) + 1 : 1;
    size_t bucket_len = entry->bucket ? strlen(entry->bucket) + 1 : 1;

    /* 检查空间 */
    size_t required_size = offset + sizeof(owner_len) + owner_len +
                            sizeof(bucket_len) + bucket_len +
                            sizeof(entry->total_usage) +
                            sizeof(entry->category_usage) +
                            sizeof(entry->s3select_usage);
    if (buf_size < required_size) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 写入 owner */
    memcpy(&buf[offset], &owner_len, sizeof(owner_len));
    offset += sizeof(owner_len);
    if (entry->owner_id) {
        memcpy(&buf[offset], entry->owner_id, owner_len - 1);
    }
    offset += owner_len;

    /* 写入 bucket */
    memcpy(&buf[offset], &bucket_len, sizeof(bucket_len));
    offset += sizeof(bucket_len);
    if (entry->bucket) {
        memcpy(&buf[offset], entry->bucket, bucket_len - 1);
    }
    offset += bucket_len;

    /* 写入 total_usage */
    memcpy(&buf[offset], &entry->total_usage, sizeof(entry->total_usage));
    offset += sizeof(entry->total_usage);

    /* 写入 category_usage */
    memcpy(&buf[offset], &entry->category_usage, sizeof(entry->category_usage));
    offset += sizeof(entry->category_usage);

    /* 写入 s3select_usage */
    memcpy(&buf[offset], &entry->s3select_usage, sizeof(entry->s3select_usage));
    offset += sizeof(entry->s3select_usage);

    *out_size = offset;
    return RGW_SAL_OK;
}

int rgw_usage_log_entry_decode(const uint8_t* buf, size_t buf_size,
                                  rgw_usage_log_entry_t* entry) {
    if (!buf || !entry) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 读取 version */
    if (offset + 1 > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    uint8_t version = buf[offset++];

    /* 读取 epoch */
    if (offset + sizeof(entry->epoch) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&entry->epoch, &buf[offset], sizeof(entry->epoch));
    offset += sizeof(entry->epoch);

    /* 读取 owner */
    size_t owner_len;
    if (offset + sizeof(owner_len) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&owner_len, &buf[offset], sizeof(owner_len));
    offset += sizeof(owner_len);

    if (owner_len > 0 && offset + owner_len <= buf_size) {
        entry->owner_id = malloc(owner_len);
        if (entry->owner_id) {
            memcpy(entry->owner_id, &buf[offset], owner_len - 1);
            entry->owner_id[owner_len - 1] = '\0';
        }
        offset += owner_len;
    }

    /* 读取 bucket */
    size_t bucket_len;
    if (offset + sizeof(bucket_len) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&bucket_len, &buf[offset], sizeof(bucket_len));
    offset += sizeof(bucket_len);

    if (bucket_len > 0 && offset + bucket_len <= buf_size) {
        entry->bucket = malloc(bucket_len);
        if (entry->bucket) {
            memcpy(entry->bucket, &buf[offset], bucket_len - 1);
            entry->bucket[bucket_len - 1] = '\0';
        }
        offset += bucket_len;
    }

    /* 读取 total_usage */
    if (offset + sizeof(entry->total_usage) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&entry->total_usage, &buf[offset], sizeof(entry->total_usage));
    offset += sizeof(entry->total_usage);

    /* 读取 category_usage */
    if (offset + sizeof(entry->category_usage) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&entry->category_usage, &buf[offset], sizeof(entry->category_usage));
    offset += sizeof(entry->category_usage);

    /* 读取 s3select_usage */
    if (offset + sizeof(entry->s3select_usage) <= buf_size) {
        memcpy(&entry->s3select_usage, &buf[offset], sizeof(entry->s3select_usage));
    }

    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS Usage 操作函数实现
 *============================================================================*/

/*
 * 完整的 RADOS Usage 实现需要调用 Ceph 的 cls_obj_usage_log_* 类方法。
 * 这些类方法通过 RADOS exec 操作执行，通常需要在 C++ 代码中实现。
 *
 * 以下是实现骨架，实际的 OMAP 操作需要与 Ceph 的类方法交互。
 */

/**
 * @brief 从 OMAP 键值对中提取 Usage 数据
 *
 * 解析 OMAP 键值对的键和值，提取 user.bucket 和 usage 数据。
 *
 * @param key OMAP 键 (格式: "user:bucket:epoch")
 * @param val OMAP 值 (二进制序列化数据)
 * @param val_len 值长度
 * @param entry 输出: usage 条目
 * @param start_epoch 起始 epoch 过滤
 * @param end_epoch 结束 epoch 过滤
 * @return 0=成功添加到结果, 1=跳过 (不匹配过滤条件), <0=错误
 */
static int parse_omap_entry(const char* key, const uint8_t* val, size_t val_len,
                           rgw_usage_log_entry_t* entry,
                           uint64_t start_epoch, uint64_t end_epoch) {
    if (!key || !val || !entry) return -EINVAL;

    memset(entry, 0, sizeof(*entry));

    /* 解析键格式: "user:bucket:epoch" */
    const char* bucket_colon = strchr(key, ':');
    if (!bucket_colon) return -EINVAL;

    const char* epoch_colon = strchr(bucket_colon + 1, ':');
    if (!epoch_colon) return -EINVAL;

    /* 提取 user */
    size_t user_len = bucket_colon - key;
    entry->owner_id = malloc(user_len + 1);
    if (!entry->owner_id) return -ENOMEM;
    memcpy(entry->owner_id, key, user_len);
    entry->owner_id[user_len] = '\0';

    /* 提取 bucket */
    size_t bucket_len = epoch_colon - bucket_colon - 1;
    entry->bucket = malloc(bucket_len + 1);
    if (!entry->bucket) {
        free(entry->owner_id);
        return -ENOMEM;
    }
    memcpy(entry->bucket, bucket_colon + 1, bucket_len);
    entry->bucket[bucket_len] = '\0';

    /* 提取 epoch */
    entry->epoch = (uint64_t)strtoull(epoch_colon + 1, NULL, 10);

    /* 检查 epoch 范围 */
    if (start_epoch > 0 && entry->epoch < start_epoch) {
        free(entry->owner_id);
        free(entry->bucket);
        return 1;
    }
    if (end_epoch > 0 && entry->epoch > end_epoch) {
        free(entry->owner_id);
        free(entry->bucket);
        return 1;
    }

    /* 解析值 */
    int ret = rgw_usage_log_entry_decode(val, val_len, entry);
    if (ret < 0) {
        free(entry->owner_id);
        free(entry->bucket);
        return ret;
    }

    return 0;
}

int rgw_usage_read_omap(rados_ioctx_t ioctx,
                          const char* user_id,
                          const char* bucket_name,
                          uint64_t start_epoch,
                          uint64_t end_epoch,
                          uint32_t max_entries,
                          rgw_usage_iter_t* iter,
                          rgw_usage_entries_t* entries,
                          bool* is_truncated) {
    if (!ioctx || !entries || !iter || !is_truncated) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /*
     * 完整的 RADOS 实现需要:
     * 1. 计算 usage 对象的 OID
     * 2. 使用 rados_read_op 和 omap_get_vals 读取 OMAP 数据
     * 3. 使用 iter->read_iter 作为迭代器位置
     *
     * 由于 Ceph 的 usage 日志使用类方法 (cls_obj_usage_log_read)
     * 而不是直接 OMAP 操作，完整实现需要调用 librados::exec()
     *
     * 以下是简化的 OMAP 读取实现
     */

    /* 计算哈希值 */
    char hash[RGW_USAGE_HASH_LEN];
    rgw_usage_log_hash(user_id, iter->index, hash);

    /* 生成 OID */
    char oid[64];
    snprintf(oid, sizeof(oid), "%s%s", RGW_USAGE_OBJ_PREFIX, hash);

    /* 初始化输出 */
    entries->count = 0;
    *is_truncated = false;

    /*
     * 简化实现: 使用 omap_get_keys 和 omap_get 获取所有键值
     *
     * 完整实现应该使用:
     * - cls_obj_usage_log_read 类方法
     * - 支持按 epoch 过滤
     * - 支持分页
     */

    /* 获取所有 OMAP 键 */
    char** keys = NULL;
    size_t keys_count = 0;
    int ret = rados_omap_get_keys(ioctx, oid, iter->read_iter, max_entries,
                                  &keys, &keys_count);
    if (ret < 0 && ret != -ENOENT) {
        return ret;
    }

    if (keys_count == 0) {
        /* 尝试下一个分片 */
        if (iter->index == 0) {
            iter->index++;
            return rgw_usage_read_omap(ioctx, user_id, bucket_name,
                                       start_epoch, end_epoch, max_entries,
                                       iter, entries, is_truncated);
        }
        return 0;
    }

    /* 读取每个键的值 */
    for (size_t i = 0; i < keys_count && entries->count < max_entries; i++) {
        const char* key = keys[i];

        /* 过滤: 如果指定了 bucket_name，只保留匹配的键 */
        if (bucket_name && bucket_name[0] != '\0') {
            const char* bucket_colon = strchr(key, ':');
            if (bucket_colon) {
                size_t key_bucket_len = bucket_colon - key;
                if (key_bucket_len != strlen(user_id) ||
                    strncmp(key, user_id, key_bucket_len) != 0) {
                    free(keys[i]);
                    continue;
                }
            }
        }

        /* 过滤: 如果指定了 user_id，只保留匹配的键 */
        if (user_id && user_id[0] != '\0') {
            if (strncmp(key, user_id, strlen(user_id)) != 0 ||
                key[strlen(user_id)] != ':') {
                free(keys[i]);
                continue;
            }
        }

        /* 获取值 */
        uint8_t* val = NULL;
        size_t val_len = 0;
        ret = rados_omap_get_val(ioctx, oid, key, &val, &val_len);
        if (ret < 0) {
            free(keys[i]);
            continue;
        }

        /* 解析 entry */
        rgw_usage_log_entry_t entry;
        ret = parse_omap_entry(key, val, val_len, &entry, start_epoch, end_epoch);
        free(val);

        if (ret == 0) {
            /* 添加到结果集合 */
            rgw_usage_entries_aggregate(entries, entry.bucket, &entry);
            free(entry.owner_id);
            free(entry.bucket);
            free(entry.payer_id);
        }

        free(keys[i]);
    }

    free(keys);

    /* 更新迭代器 */
    if (iter->read_iter) {
        free(iter->read_iter);
        iter->read_iter = NULL;
    }
    iter->read_iter = strdup(keys[keys_count - 1]);

    /* 检查是否还有更多数据 */
    *is_truncated = (keys_count >= max_entries);

    return 0;
}

int rgw_usage_trim_omap(rados_ioctx_t ioctx,
                           const char* user_id,
                           const char* bucket_name,
                           uint64_t start_epoch,
                           uint64_t end_epoch) {
    if (!ioctx) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /*
     * 完整的 RADOS 实现需要:
     * 1. 遍历所有 usage 分片
     * 2. 使用 cls_obj_usage_log_trim 类方法删除指定 epoch 范围的日志
     *
     * 简化实现:
     * - 遍历指定用户的 OMAP
     * - 删除匹配 epoch 范围的键
     */

    uint32_t index = 0;
    char hash[RGW_USAGE_HASH_LEN];
    char first_hash[RGW_USAGE_HASH_LEN];
    char oid[64];

    /* 计算第一个哈希 */
    rgw_usage_log_hash(user_id, index, first_hash);

    do {
        /* 生成 OID */
        snprintf(oid, sizeof(oid), "%s%s", RGW_USAGE_OBJ_PREFIX, first_hash);

        /* 获取所有键 */
        char** keys = NULL;
        size_t keys_count = 0;
        int ret = rados_omap_get_keys(ioctx, oid, NULL, 0, &keys, &keys_count);
        if (ret < 0 && ret != -ENOENT) {
            /* 继续尝试下一个分片 */
        } else if (keys_count > 0) {
            /* 遍历键并删除符合条件的 */
            for (size_t i = 0; i < keys_count; i++) {
                const char* key = keys[i];

                /* 检查用户匹配 */
                if (user_id && user_id[0] != '\0') {
                    if (strncmp(key, user_id, strlen(user_id)) != 0 ||
                        key[strlen(user_id)] != ':') {
                        free(keys[i]);
                        continue;
                    }
                }

                /* 提取 epoch */
                const char* last_colon = strrchr(key, ':');
                if (last_colon) {
                    uint64_t epoch = (uint64_t)strtoull(last_colon + 1, NULL, 10);

                    /* 检查 epoch 范围 */
                    bool should_delete = true;
                    if (start_epoch > 0 && epoch < start_epoch) {
                        should_delete = false;
                    }
                    if (end_epoch > 0 && epoch > end_epoch) {
                        should_delete = false;
                    }

                    if (should_delete) {
                        rados_omap_rm_key(ioctx, oid, key);
                    }
                }

                free(keys[i]);
            }
            free(keys);
        }

        /* 移动到下一个分片 */
        rgw_usage_log_hash(user_id, ++index, hash);

    } while (strcmp(hash, first_hash) != 0);

    return 0;
}

int rgw_usage_clear_omap(rados_ioctx_t ioctx) {
    if (!ioctx) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /*
     * 清空所有 usage 日志
     */

    /* 遍历所有分片并删除 */
    for (uint32_t i = 0; i < RGW_USAGE_DEFAULT_MAX_SHARDS; i++) {
        char oid[64];
        snprintf(oid, sizeof(oid), "%s%u", RGW_USAGE_OBJ_PREFIX, i);

        /* 获取所有键 */
        char** keys = NULL;
        size_t keys_count = 0;
        int ret = rados_omap_get_keys(ioctx, oid, NULL, 0, &keys, &keys_count);
        if (ret < 0 && ret != -ENOENT) {
            continue;
        }

        if (keys_count > 0) {
            /* 删除所有键 */
            for (size_t j = 0; j < keys_count; j++) {
                free(keys[j]);
            }
            free(keys);

            /* 清空 OMAP */
            rados_omap_clear(ioctx, oid);
        }
    }

    return 0;
}

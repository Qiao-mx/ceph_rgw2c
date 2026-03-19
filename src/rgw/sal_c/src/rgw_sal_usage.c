/**
 * @file rgw_sal_usage.c
 * @brief SAL C 接口 - 使用统计类型实现
 *
 * 实现 rgw_sal_usage.h 中定义的使用统计相关函数。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>

#include "rgw_sal_usage.h"
#include "rgw_sal_errors.h"

/*============================================================================
 * 常量定义
 *============================================================================*/

/** 初始容量 */
#define USAGE_MAP_INITIAL_CAPACITY 4

/** 扩容增量 */
#define USAGE_ENTRIES_INITIAL_CAPACITY 16

/** 动态数组扩容因子 */
#define DYNAMIC_ARRAY_GROWTH_FACTOR 2

/*============================================================================
 * 辅助函数
 *============================================================================*/

/**
 * @brief 释放 usage map
 */
static void rgw_sal_usage_map_destroy_internal(rgw_sal_usage_map_t* map) {
    if (!map) return;

    if (map->entries) {
        for (size_t i = 0; i < map->count; i++) {
            free(map->entries[i].category);
        }
        free(map->entries);
    }
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

/**
 * @brief 初始化 usage map
 */
static int rgw_sal_usage_map_init_internal(rgw_sal_usage_map_t* map) {
    if (!map) return RGW_SAL_ERR_INVALID_ARG;

    map->entries = calloc(USAGE_MAP_INITIAL_CAPACITY, sizeof(rgw_sal_usage_map_entry_t));
    if (!map->entries) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    map->count = 0;
    map->capacity = USAGE_MAP_INITIAL_CAPACITY;
    return RGW_SAL_OK;
}

/**
 * @brief 在 usage map 中查找类别
 */
static rgw_sal_usage_map_entry_t* rgw_sal_usage_map_find_internal(rgw_sal_usage_map_t* map, const char* category) {
    if (!map || !category) return NULL;

    for (size_t i = 0; i < map->count; i++) {
        if (map->entries[i].category && strcmp(map->entries[i].category, category) == 0) {
            return &map->entries[i];
        }
    }
    return NULL;
}

/**
 * @brief 在 usage map 中添加或获取类别
 */
static int rgw_sal_usage_map_get_or_create_internal(rgw_sal_usage_map_t* map, const char* category,
                                                  rgw_sal_usage_map_entry_t** out_entry) {
    if (!map || !category || !out_entry) return RGW_SAL_ERR_INVALID_ARG;

    rgw_sal_usage_map_entry_t* entry = rgw_sal_usage_map_find_internal(map, category);
    if (entry) {
        *out_entry = entry;
        return RGW_SAL_OK;
    }

    /* 需要添加新的条目 */
    if (map->count >= map->capacity) {
        size_t new_capacity = map->capacity * DYNAMIC_ARRAY_GROWTH_FACTOR;
        rgw_sal_usage_map_entry_t* new_entries = realloc(map->entries,
                                                          new_capacity * sizeof(rgw_sal_usage_map_entry_t));
        if (!new_entries) {
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        map->entries = new_entries;
        map->capacity = new_capacity;
    }

    entry = &map->entries[map->count];
    memset(entry, 0, sizeof(*entry));
    entry->category = strdup(category);
    if (!entry->category) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    map->count++;
    *out_entry = entry;
    return RGW_SAL_OK;
}

/*============================================================================
 * Usage 数据函数实现
 *============================================================================*/

rgw_sal_usage_data_t* rgw_sal_usage_data_create(void) {
    rgw_sal_usage_data_t* data = calloc(1, sizeof(rgw_sal_usage_data_t));
    return data;
}

void rgw_sal_usage_data_destroy(rgw_sal_usage_data_t* data) {
    free(data);
}

void rgw_sal_usage_data_aggregate(rgw_sal_usage_data_t* target, const rgw_sal_usage_data_t* source) {
    if (!target || !source) return;

    target->bytes_sent += source->bytes_sent;
    target->bytes_received += source->bytes_received;
    target->ops += source->ops;
    target->successful_ops += source->successful_ops;
}

/*============================================================================
 * Usage 迭代器函数实现
 *============================================================================*/

rgw_sal_usage_iter_t* rgw_sal_usage_iter_create(void) {
    rgw_sal_usage_iter_t* iter = calloc(1, sizeof(rgw_sal_usage_iter_t));
    return iter;
}

void rgw_sal_usage_iter_reset(rgw_sal_usage_iter_t* iter) {
    if (!iter) return;
    free(iter->read_iter);
    iter->read_iter = NULL;
    iter->index = 0;
}

void rgw_sal_usage_iter_destroy(rgw_sal_usage_iter_t* iter) {
    if (!iter) return;
    free(iter->read_iter);
    free(iter);
}

/*============================================================================
 * Usage 日志条目函数实现
 *============================================================================*/

rgw_sal_usage_log_entry_t* rgw_sal_usage_log_entry_create(void) {
    rgw_sal_usage_log_entry_t* entry = calloc(1, sizeof(rgw_sal_usage_log_entry_t));
    if (entry) {
        rgw_sal_usage_map_init_internal(&entry->usage_map);
    }
    return entry;
}

void rgw_sal_usage_log_entry_destroy(rgw_sal_usage_log_entry_t* entry) {
    if (!entry) return;

    free(entry->owner_id);
    free(entry->payer_id);
    free(entry->bucket);
    rgw_sal_usage_map_destroy_internal(&entry->usage_map);
    free(entry);
}

int rgw_sal_usage_log_entry_add_usage(rgw_sal_usage_log_entry_t* entry,
                                      const char* category,
                                      const rgw_sal_usage_data_t* data) {
    if (!entry || !category || !data) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取或创建类别条目 */
    rgw_sal_usage_map_entry_t* map_entry = NULL;
    int ret = rgw_sal_usage_map_get_or_create_internal(&entry->usage_map, category, &map_entry);
    if (ret != RGW_SAL_OK) return ret;

    /* 聚合到类别数据 */
    rgw_sal_usage_data_aggregate(&map_entry->data, data);

    /* 聚合到总使用量 */
    rgw_sal_usage_data_aggregate(&entry->total_usage, data);

    return RGW_SAL_OK;
}

void rgw_sal_usage_log_entry_aggregate(rgw_sal_usage_log_entry_t* target,
                                       const rgw_sal_usage_log_entry_t* source) {
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

    /* 聚合每个类别 */
    for (size_t i = 0; i < source->usage_map.count; i++) {
        rgw_sal_usage_data_aggregate(&target->total_usage, &source->usage_map.entries[i].data);

        /* 获取或创建 target 中的对应类别 */
        rgw_sal_usage_map_entry_t* map_entry = NULL;
        if (rgw_sal_usage_map_get_or_create_internal(&target->usage_map,
                                                     source->usage_map.entries[i].category,
                                                     &map_entry) == RGW_SAL_OK) {
            rgw_sal_usage_data_aggregate(&map_entry->data, &source->usage_map.entries[i].data);
        }
    }

    /* 聚合 S3Select 使用量 */
    target->s3select_usage.bytes_processed += source->s3select_usage.bytes_processed;
    target->s3select_usage.bytes_returned += source->s3select_usage.bytes_returned;
}

void rgw_sal_usage_log_entry_sum(const rgw_sal_usage_log_entry_t* entry,
                                  rgw_sal_usage_data_t* result) {
    if (!entry || !result) return;

    memset(result, 0, sizeof(*result));

    for (size_t i = 0; i < entry->usage_map.count; i++) {
        rgw_sal_usage_data_aggregate(result, &entry->usage_map.entries[i].data);
    }
}

/*============================================================================
 * Usage 集合函数实现
 *============================================================================*/

rgw_sal_usage_entries_t* rgw_sal_usage_entries_create(void) {
    rgw_sal_usage_entries_t* entries = calloc(1, sizeof(rgw_sal_usage_entries_t));
    if (entries) {
        entries->entries = calloc(USAGE_ENTRIES_INITIAL_CAPACITY, sizeof(entries->entries[0]));
        if (entries->entries) {
            entries->capacity = USAGE_ENTRIES_INITIAL_CAPACITY;
        } else {
            free(entries);
            return NULL;
        }
    }
    return entries;
}

void rgw_sal_usage_entries_destroy(rgw_sal_usage_entries_t* entries) {
    if (!entries) return;

    if (entries->entries) {
        for (size_t i = 0; i < entries->count; i++) {
            free(entries->entries[i].key);
            rgw_sal_usage_log_entry_destroy(&entries->entries[i].entry);
        }
        free(entries->entries);
    }
    free(entries);
}

int rgw_sal_usage_entries_add(rgw_sal_usage_entries_t* entries,
                                const char* key,
                                const rgw_sal_usage_log_entry_t* entry) {
    if (!entries || !key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    if (entries->count >= entries->capacity) {
        size_t new_capacity = entries->capacity * DYNAMIC_ARRAY_GROWTH_FACTOR;
        void* new_entries = realloc(entries->entries, new_capacity * sizeof(entries->entries[0]));
        if (!new_entries) {
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
        entries->entries = new_entries;
        entries->capacity = new_capacity;
    }

    entries->entries[entries->count].key = strdup(key);
    if (!entries->entries[entries->count].key) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 复制 entry */
    rgw_sal_usage_log_entry_t* dest = &entries->entries[entries->count].entry;
    memset(dest, 0, sizeof(*dest));

    if (entry->owner_id) dest->owner_id = strdup(entry->owner_id);
    if (entry->payer_id) dest->payer_id = strdup(entry->payer_id);
    if (entry->bucket) dest->bucket = strdup(entry->bucket);
    dest->epoch = entry->epoch;
    dest->total_usage = entry->total_usage;
    dest->s3select_usage = entry->s3select_usage;

    /* 复制 usage_map */
    if (rgw_sal_usage_map_init_internal(&dest->usage_map) != RGW_SAL_OK) {
        free(dest->owner_id);
        free(dest->payer_id);
        free(dest->bucket);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    for (size_t i = 0; i < entry->usage_map.count; i++) {
        rgw_sal_usage_map_entry_t* map_entry = NULL;
        if (rgw_sal_usage_map_get_or_create_internal(&dest->usage_map,
                                                      entry->usage_map.entries[i].category,
                                                      &map_entry) == RGW_SAL_OK) {
            map_entry->data = entry->usage_map.entries[i].data;
        }
    }

    entries->count++;
    return RGW_SAL_OK;
}

int rgw_sal_usage_entries_aggregate(rgw_sal_usage_entries_t* entries,
                                      const char* key,
                                      const rgw_sal_usage_log_entry_t* entry) {
    if (!entries || !key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    /* 查找是否已存在 */
    for (size_t i = 0; i < entries->count; i++) {
        if (entries->entries[i].key && strcmp(entries->entries[i].key, key) == 0) {
            rgw_sal_usage_log_entry_aggregate(&entries->entries[i].entry, entry);
            return RGW_SAL_OK;
        }
    }

    /* 不存在，添加新的 */
    return rgw_sal_usage_entries_add(entries, key, entry);
}

/*============================================================================
 * User-Bucket 键函数实现
 *============================================================================*/

rgw_sal_user_bucket_t* rgw_sal_user_bucket_create(const char* user, const char* bucket) {
    rgw_sal_user_bucket_t* ub = calloc(1, sizeof(rgw_sal_user_bucket_t));
    if (!ub) return NULL;

    if (user) ub->user = strdup(user);
    if (bucket) ub->bucket = strdup(bucket);

    return ub;
}

void rgw_sal_user_bucket_destroy(rgw_sal_user_bucket_t* ub) {
    if (!ub) return;
    free(ub->user);
    free(ub->bucket);
    free(ub);
}

rgw_sal_user_bucket_t* rgw_sal_user_bucket_parse(const char* str) {
    if (!str) return NULL;

    /* 查找分隔符 ':' 或 '.' */
    const char* sep = strchr(str, ':');
    if (!sep) sep = strchr(str, '.');
    if (!sep) {
        /* 只有 user，没有 bucket */
        return rgw_sal_user_bucket_create(str, NULL);
    }

    size_t user_len = sep - str;
    char* user = malloc(user_len + 1);
    if (!user) return NULL;
    memcpy(user, str, user_len);
    user[user_len] = '\0';

    const char* bucket = sep + 1;
    rgw_sal_user_bucket_t* ub = rgw_sal_user_bucket_create(user, bucket);
    free(user);
    return ub;
}

const char* rgw_sal_user_bucket_to_string(const rgw_sal_user_bucket_t* ub, char* buf, size_t buf_size) {
    if (!ub || !buf || buf_size == 0) return NULL;

    if (ub->user && ub->bucket) {
        snprintf(buf, buf_size, "%s:%s", ub->user, ub->bucket);
    } else if (ub->user) {
        snprintf(buf, buf_size, "%s", ub->user);
    } else {
        buf[0] = '\0';
    }

    return buf;
}

/*============================================================================
 * Usage 序列化函数实现
 *============================================================================*/

int rgw_sal_usage_log_entry_encode(const rgw_sal_usage_log_entry_t* entry,
                                    uint8_t* buf, size_t buf_size, size_t* out_size) {
    if (!entry || !buf || !out_size) return RGW_SAL_ERR_INVALID_ARG;

    /* 简化实现：使用二进制格式 */
    /* 格式: version(1) + epoch(8) + owner_len(4) + owner + bucket_len(4) + bucket +
             bytes_sent(8) + bytes_received(8) + ops(8) + successful_ops(8) +
             usage_map_count(4) + [category_len(4) + category + data...] +
             s3select_processed(8) + s3select_returned(8) */

    size_t offset = 0;
    uint8_t version = 4;
    size_t required_size = 1 + 8 + 4 + 4 + 8 + 8 + 8 + 8 + 4;  /* 基本字段 */

    /* 计算 owner 和 bucket 长度 */
    size_t owner_len = entry->owner_id ? strlen(entry->owner_id) + 1 : 1;
    size_t bucket_len = entry->bucket ? strlen(entry->bucket) + 1 : 1;
    required_size += owner_len + bucket_len;

    /* 计算 usage_map 大小 */
    for (size_t i = 0; i < entry->usage_map.count; i++) {
        size_t cat_len = entry->usage_map.entries[i].category ?
                         strlen(entry->usage_map.entries[i].category) + 1 : 1;
        required_size += 4 + cat_len + sizeof(rgw_sal_usage_data_t);
    }

    required_size += sizeof(rgw_sal_s3select_usage_t);

    if (buf_size < required_size) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 写入 version */
    buf[offset++] = version;

    /* 写入 epoch */
    memcpy(&buf[offset], &entry->epoch, sizeof(entry->epoch));
    offset += sizeof(entry->epoch);

    /* 写入 owner */
    memcpy(&buf[offset], &owner_len, sizeof(owner_len));
    offset += sizeof(owner_len);
    if (entry->owner_id) {
        memcpy(&buf[offset], entry->owner_id, owner_len - 1);
        offset += owner_len - 1;
    }
    buf[offset++] = '\0';

    /* 写入 bucket */
    memcpy(&buf[offset], &bucket_len, sizeof(bucket_len));
    offset += sizeof(bucket_len);
    if (entry->bucket) {
        memcpy(&buf[offset], entry->bucket, bucket_len - 1);
        offset += bucket_len - 1;
    }
    buf[offset++] = '\0';

    /* 写入 total_usage */
    memcpy(&buf[offset], &entry->total_usage.bytes_sent, sizeof(entry->total_usage.bytes_sent));
    offset += sizeof(entry->total_usage.bytes_sent);
    memcpy(&buf[offset], &entry->total_usage.bytes_received, sizeof(entry->total_usage.bytes_received));
    offset += sizeof(entry->total_usage.bytes_received);
    memcpy(&buf[offset], &entry->total_usage.ops, sizeof(entry->total_usage.ops));
    offset += sizeof(entry->total_usage.ops);
    memcpy(&buf[offset], &entry->total_usage.successful_ops, sizeof(entry->total_usage.successful_ops));
    offset += sizeof(entry->total_usage.successful_ops);

    /* 写入 usage_map_count */
    uint32_t map_count = (uint32_t)entry->usage_map.count;
    memcpy(&buf[offset], &map_count, sizeof(map_count));
    offset += sizeof(map_count);

    /* 写入每个类别 */
    for (size_t i = 0; i < entry->usage_map.count; i++) {
        size_t cat_len = entry->usage_map.entries[i].category ?
                         strlen(entry->usage_map.entries[i].category) + 1 : 1;

        memcpy(&buf[offset], &cat_len, sizeof(cat_len));
        offset += sizeof(cat_len);

        if (entry->usage_map.entries[i].category) {
            memcpy(&buf[offset], entry->usage_map.entries[i].category, cat_len - 1);
            offset += cat_len - 1;
        }
        buf[offset++] = '\0';

        memcpy(&buf[offset], &entry->usage_map.entries[i].data,
               sizeof(entry->usage_map.entries[i].data));
        offset += sizeof(entry->usage_map.entries[i].data);
    }

    /* 写入 s3select_usage */
    memcpy(&buf[offset], &entry->s3select_usage.bytes_processed,
           sizeof(entry->s3select_usage.bytes_processed));
    offset += sizeof(entry->s3select_usage.bytes_processed);
    memcpy(&buf[offset], &entry->s3select_usage.bytes_returned,
           sizeof(entry->s3select_usage.bytes_returned));
    offset += sizeof(entry->s3select_usage.bytes_returned);

    *out_size = offset;
    return RGW_SAL_OK;
}

int rgw_sal_usage_log_entry_decode(const uint8_t* buf, size_t buf_size,
                                    rgw_sal_usage_log_entry_t* entry) {
    if (!buf || !entry) return RGW_SAL_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 读取 version */
    uint8_t version;
    if (offset + sizeof(version) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    version = buf[offset++];

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

    /* 读取 usage_map_count */
    uint32_t map_count = 0;
    if (offset + sizeof(map_count) > buf_size) return RGW_SAL_ERR_INVALID_ARG;
    memcpy(&map_count, &buf[offset], sizeof(map_count));
    offset += sizeof(map_count);

    /* 初始化 usage_map */
    if (rgw_sal_usage_map_init_internal(&entry->usage_map) != RGW_SAL_OK) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 读取每个类别 */
    for (uint32_t i = 0; i < map_count; i++) {
        size_t cat_len;
        if (offset + sizeof(cat_len) > buf_size) break;
        memcpy(&cat_len, &buf[offset], sizeof(cat_len));
        offset += sizeof(cat_len);

        char* category = NULL;
        if (cat_len > 0 && offset + cat_len <= buf_size) {
            category = malloc(cat_len);
            if (category) {
                memcpy(category, &buf[offset], cat_len - 1);
                category[cat_len - 1] = '\0';
            }
            offset += cat_len;
        }

        rgw_sal_usage_map_entry_t* map_entry = NULL;
        if (rgw_sal_usage_map_get_or_create_internal(&entry->usage_map, category ? category : "",
                                                     &map_entry) == RGW_SAL_OK) {
            if (offset + sizeof(map_entry->data) <= buf_size) {
                memcpy(&map_entry->data, &buf[offset], sizeof(map_entry->data));
            }
            offset += sizeof(map_entry->data);
        }

        free(category);
    }

    /* 读取 s3select_usage */
    if (offset + sizeof(entry->s3select_usage) <= buf_size) {
        memcpy(&entry->s3select_usage, &buf[offset], sizeof(entry->s3select_usage));
    }

    return RGW_SAL_OK;
}

/*============================================================================
 * Usage 辅助函数实现
 *============================================================================*/

/**
 * @brief 简单的字符串哈希函数
 *
 * 这是简化实现，不使用 ceph_str_hash_linux。
 */
static uint32_t simple_string_hash(const char* str, size_t len) {
    if (!str || len == 0) return 0;

    uint32_t hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = hash * 33 + (uint8_t)str[i];
    }
    return hash;
}

void rgw_sal_usage_log_hash(void* cct, const char* name, uint32_t index, char* hash) {
    (void)cct;  /* 未使用，保留兼容性 */

    /* 获取配置参数 */
    uint32_t max_shards = RGW_SAL_USAGE_DEFAULT_MAX_SHARDS;
    uint32_t max_user_shards = RGW_SAL_USAGE_DEFAULT_MAX_USER_SHARDS;

    /* 计算哈希值 */
    uint32_t val = index;

    if (name && name[0] != '\0') {
        uint32_t name_hash = simple_string_hash(name, strlen(name));
        val %= max_user_shards;
        val += name_hash;
    }

    /* 生成对象 ID */
    snprintf(hash, RGW_SAL_USAGE_HASH_LEN, "%010u", val % max_shards);
}

int rgw_sal_usage_generate_oid(uint32_t index, char* buf, size_t buf_size) {
    if (!buf || buf_size < 32) return RGW_SAL_ERR_INVALID_ARG;

    snprintf(buf, buf_size, "%s%u", RGW_SAL_USAGE_OBJ_PREFIX, index);
    return RGW_SAL_OK;
}

/*============================================================================
 * 配置函数实现
 *============================================================================*/

static rgw_sal_usage_config_t default_usage_config = {
    .max_shards = RGW_SAL_USAGE_DEFAULT_MAX_SHARDS,
    .max_user_shards = RGW_SAL_USAGE_DEFAULT_MAX_USER_SHARDS,
    .pool_name = ".rgw.log"
};

const rgw_sal_usage_config_t* rgw_sal_usage_get_default_config(void) {
    return &default_usage_config;
}

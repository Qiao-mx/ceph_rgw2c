/**
 * @file rgw_acl_serde.c
 * @brief ACL 序列化实现
 *
 * 实现 ACL 信息的二进制序列化和 JSON 序列化功能，
 * 用于 RADOS OMAP 存储。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#include "rgw_acl_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 计算字符串编码后的大小
 */
static size_t calc_string_size(const char* str) {
    if (!str) return sizeof(uint32_t);  /* 长度字段 */
    return sizeof(uint32_t) + strlen(str) + 1;  /* 长度 + 字符串 + 终止符 */
}

/**
 * @brief 编码字符串到缓冲区
 */
static int encode_string(const char* str, uint8_t* buf, size_t buf_size, size_t* offset) {
    if (!buf || !offset) return -EINVAL;

    uint32_t len = str ? (uint32_t)strlen(str) + 1 : 0;
    size_t total_len = sizeof(uint32_t) + len;

    if (*offset + total_len > buf_size) {
        return -ENOBUFS;
    }

    /* 写入长度 */
    memcpy(buf + *offset, &len, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* 写入字符串 */
    if (len > 0) {
        memcpy(buf + *offset, str, len);
        *offset += len;
    }

    return 0;
}

/**
 * @brief 解码字符串从缓冲区
 */
static int decode_string(const uint8_t* buf, size_t buf_len, size_t* offset, char** out_str) {
    if (!buf || !offset || !out_str) return -EINVAL;

    if (*offset + sizeof(uint32_t) > buf_len) {
        return -ENOBUFS;
    }

    /* 读取长度 */
    uint32_t len;
    memcpy(&len, buf + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    if (len == 0) {
        *out_str = NULL;
        return 0;
    }

    if (*offset + len > buf_len) {
        return -ENOBUFS;
    }

    /* 复制字符串 */
    *out_str = (char*)malloc(len);
    if (!*out_str) return -ENOMEM;

    memcpy(*out_str, buf + *offset, len);
    *offset += len;

    return 0;
}

/*============================================================================
 * 生命周期管理
 *============================================================================*/

rgw_acl_info_t* rgw_acl_info_create(void) {
    rgw_acl_info_t* acl = (rgw_acl_info_t*)calloc(1, sizeof(rgw_acl_info_t));
    if (!acl) return NULL;

    acl->grants_capacity = 16;
    acl->grants = (rgw_acl_grant_t*)calloc(acl->grants_capacity, sizeof(rgw_acl_grant_t));
    if (!acl->grants) {
        free(acl);
        return NULL;
    }

    return acl;
}

void rgw_acl_info_destroy(rgw_acl_info_t* acl) {
    if (!acl) return;

    /* 释放 owner */
    free(acl->owner.id);
    free(acl->owner.display_name);

    /* 释放 grants */
    if (acl->grants) {
        for (size_t i = 0; i < acl->num_grants; i++) {
            free(acl->grants[i].id);
            free(acl->grants[i].display_name);
            free(acl->grants[i].url_group);
        }
        free(acl->grants);
    }

    free(acl);
}

int rgw_acl_info_deep_copy(const rgw_acl_info_t* src, rgw_acl_info_t* dst) {
    if (!src || !dst) return -EINVAL;

    /* 复制 owner */
    free(dst->owner.id);
    free(dst->owner.display_name);
    dst->owner.id = NULL;
    dst->owner.display_name = NULL;

    if (src->owner.id) {
        dst->owner.id = strdup(src->owner.id);
        if (!dst->owner.id) return -ENOMEM;
    }
    if (src->owner.display_name) {
        dst->owner.display_name = strdup(src->owner.display_name);
        if (!dst->owner.display_name) {
            free(dst->owner.id);
            dst->owner.id = NULL;
            return -ENOMEM;
        }
    }

    /* 复制 grants */
    if (src->num_grants > dst->grants_capacity) {
        rgw_acl_grant_t* new_grants = (rgw_acl_grant_t*)realloc(
            dst->grants, src->num_grants * sizeof(rgw_acl_grant_t));
        if (!new_grants) return -ENOMEM;
        dst->grants = new_grants;
        dst->grants_capacity = src->num_grants;
    }

    /* 先清理旧的 grants 数据 */
    for (size_t i = 0; i < dst->num_grants; i++) {
        free(dst->grants[i].id);
        free(dst->grants[i].display_name);
        free(dst->grants[i].url_group);
    }

    dst->num_grants = src->num_grants;
    for (size_t i = 0; i < src->num_grants; i++) {
        dst->grants[i].type = src->grants[i].type;
        dst->grants[i].perm = src->grants[i].perm;
        dst->grants[i].group_type = src->grants[i].group_type;
        dst->grants[i].id = NULL;
        dst->grants[i].display_name = NULL;
        dst->grants[i].url_group = NULL;

        if (src->grants[i].id) {
            dst->grants[i].id = strdup(src->grants[i].id);
            if (!dst->grants[i].id) return -ENOMEM;
        }
        if (src->grants[i].display_name) {
            dst->grants[i].display_name = strdup(src->grants[i].display_name);
            if (!dst->grants[i].display_name) return -ENOMEM;
        }
        if (src->grants[i].url_group) {
            dst->grants[i].url_group = strdup(src->grants[i].url_group);
            if (!dst->grants[i].url_group) return -ENOMEM;
        }
    }

    return 0;
}

/*============================================================================
 * Grant 管理
 *============================================================================*/

int rgw_acl_info_add_grant(rgw_acl_info_t* acl, const rgw_acl_grant_t* grant) {
    if (!acl || !grant) return -EINVAL;

    /* 检查容量并扩容 */
    if (acl->num_grants >= acl->grants_capacity) {
        size_t new_capacity = acl->grants_capacity * 2;
        rgw_acl_grant_t* new_grants = (rgw_acl_grant_t*)realloc(
            acl->grants, new_capacity * sizeof(rgw_acl_grant_t));
        if (!new_grants) return -ENOMEM;
        acl->grants = new_grants;
        acl->grants_capacity = new_capacity;
    }

    /* 复制 grant */
    rgw_acl_grant_t* dst = &acl->grants[acl->num_grants];
    dst->type = grant->type;
    dst->perm = grant->perm;
    dst->group_type = grant->group_type;
    dst->id = grant->id ? strdup(grant->id) : NULL;
    dst->display_name = grant->display_name ? strdup(grant->display_name) : NULL;
    dst->url_group = grant->url_group ? strdup(grant->url_group) : NULL;

    acl->num_grants++;
    return 0;
}

int rgw_acl_info_create_default(rgw_acl_info_t* acl,
                                 const char* owner_id,
                                 const char* owner_name) {
    if (!acl) return -EINVAL;

    /* 清理现有 owner */
    free(acl->owner.id);
    free(acl->owner.display_name);

    acl->owner.id = owner_id ? strdup(owner_id) : NULL;
    acl->owner.display_name = owner_name ? strdup(owner_name) : NULL;

    return 0;
}

/*============================================================================
 * 序列化/反序列化
 *============================================================================*/

size_t rgw_acl_calc_encode_size(const rgw_acl_info_t* acl) {
    if (!acl) return 0;

    size_t size = 0;

    /* 版本号 */
    size += sizeof(uint8_t);

    /* Owner */
    size += calc_string_size(acl->owner.id);
    size += calc_string_size(acl->owner.display_name);

    /* Grants 数量 */
    size += sizeof(uint32_t);

    /* Grants */
    for (size_t i = 0; i < acl->num_grants; i++) {
        /* type */
        size += sizeof(rgw_acl_grant_type_t);
        /* perm */
        size += sizeof(rgw_acl_perm_t);
        /* group_type */
        size += sizeof(int32_t);
        /* id */
        size += calc_string_size(acl->grants[i].id);
        /* display_name */
        size += calc_string_size(acl->grants[i].display_name);
        /* url_group */
        size += calc_string_size(acl->grants[i].url_group);
    }

    return size;
}

int rgw_acl_encode(const rgw_acl_info_t* acl, uint8_t* buf, size_t buf_size) {
    if (!acl || !buf) return -EINVAL;

    size_t offset = 0;
    int ret;

    /* 写入版本号 */
    if (offset + sizeof(uint8_t) > buf_size) return -ENOBUFS;
    uint8_t version = RGW_ACL_ENCODE_VERSION;
    memcpy(buf + offset, &version, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    /* 写入 Owner */
    ret = encode_string(acl->owner.id, buf, buf_size, &offset);
    if (ret < 0) return ret;

    ret = encode_string(acl->owner.display_name, buf, buf_size, &offset);
    if (ret < 0) return ret;

    /* 写入 Grants 数量 */
    if (offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
    uint32_t num_grants = (uint32_t)acl->num_grants;
    memcpy(buf + offset, &num_grants, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    /* 写入每个 Grant */
    for (size_t i = 0; i < acl->num_grants; i++) {
        /* type */
        if (offset + sizeof(rgw_acl_grant_type_t) > buf_size) return -ENOBUFS;
        memcpy(buf + offset, &acl->grants[i].type, sizeof(rgw_acl_grant_type_t));
        offset += sizeof(rgw_acl_grant_type_t);

        /* perm */
        if (offset + sizeof(rgw_acl_perm_t) > buf_size) return -ENOBUFS;
        memcpy(buf + offset, &acl->grants[i].perm, sizeof(rgw_acl_perm_t));
        offset += sizeof(rgw_acl_perm_t);

        /* group_type */
        if (offset + sizeof(int32_t) > buf_size) return -ENOBUFS;
        memcpy(buf + offset, &acl->grants[i].group_type, sizeof(int32_t));
        offset += sizeof(int32_t);

        /* id */
        ret = encode_string(acl->grants[i].id, buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* display_name */
        ret = encode_string(acl->grants[i].display_name, buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* url_group */
        ret = encode_string(acl->grants[i].url_group, buf, buf_size, &offset);
        if (ret < 0) return ret;
    }

    return (int)offset;
}

int rgw_acl_encode_alloc(const rgw_acl_info_t* acl, uint8_t** out_buf, size_t* out_len) {
    if (!acl || !out_buf || !out_len) return -EINVAL;

    size_t buf_size = rgw_acl_calc_encode_size(acl);
    if (buf_size == 0) return -EINVAL;

    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) return -ENOMEM;

    int ret = rgw_acl_encode(acl, buf, buf_size);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_len = (size_t)ret;
    return 0;
}

int rgw_acl_decode(const uint8_t* buf, size_t buf_len, rgw_acl_info_t* acl) {
    if (!buf || !acl) return -EINVAL;

    size_t offset = 0;
    int ret;

    /* 读取版本号 */
    if (offset + sizeof(uint8_t) > buf_len) return -ENOBUFS;
    uint8_t version;
    memcpy(&version, buf + offset, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    if (version != RGW_ACL_ENCODE_VERSION) {
        return -EINVAL;  /* 不支持的版本 */
    }

    /* 读取 Owner */
    ret = decode_string(buf, buf_len, &offset, &acl->owner.id);
    if (ret < 0) goto cleanup;

    ret = decode_string(buf, buf_len, &offset, &acl->owner.display_name);
    if (ret < 0) goto cleanup;

    /* 读取 Grants 数量 */
    if (offset + sizeof(uint32_t) > buf_len) {
        ret = -ENOBUFS;
        goto cleanup;
    }
    uint32_t num_grants;
    memcpy(&num_grants, buf + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    /* 分配 grants 数组 */
    if (num_grants > 0) {
        if (acl->grants) {
            free(acl->grants);
        }
        acl->grants = (rgw_acl_grant_t*)calloc(num_grants, sizeof(rgw_acl_grant_t));
        if (!acl->grants) {
            ret = -ENOMEM;
            goto cleanup;
        }
        acl->grants_capacity = num_grants;
    }
    acl->num_grants = num_grants;

    /* 读取每个 Grant */
    for (uint32_t i = 0; i < num_grants; i++) {
        /* type */
        if (offset + sizeof(rgw_acl_grant_type_t) > buf_len) {
            ret = -ENOBUFS;
            goto cleanup;
        }
        memcpy(&acl->grants[i].type, buf + offset, sizeof(rgw_acl_grant_type_t));
        offset += sizeof(rgw_acl_grant_type_t);

        /* perm */
        if (offset + sizeof(rgw_acl_perm_t) > buf_len) {
            ret = -ENOBUFS;
            goto cleanup;
        }
        memcpy(&acl->grants[i].perm, buf + offset, sizeof(rgw_acl_perm_t));
        offset += sizeof(rgw_acl_perm_t);

        /* group_type */
        if (offset + sizeof(int32_t) > buf_len) {
            ret = -ENOBUFS;
            goto cleanup;
        }
        memcpy(&acl->grants[i].group_type, buf + offset, sizeof(int32_t));
        offset += sizeof(int32_t);

        /* id */
        ret = decode_string(buf, buf_len, &offset, &acl->grants[i].id);
        if (ret < 0) goto cleanup;

        /* display_name */
        ret = decode_string(buf, buf_len, &offset, &acl->grants[i].display_name);
        if (ret < 0) goto cleanup;

        /* url_group */
        ret = decode_string(buf, buf_len, &offset, &acl->grants[i].url_group);
        if (ret < 0) goto cleanup;
    }

    return 0;

cleanup:
    /* 清理部分解析的数据 */
    rgw_acl_info_destroy(acl);
    return ret;
}

/*============================================================================
 * JSON 序列化
 *============================================================================*/

int rgw_acl_to_json(const rgw_acl_info_t* acl, char** json_str, size_t* json_len) {
    if (!acl || !json_str) return -EINVAL;

    /* 计算所需缓冲区大小 */
    size_t buf_size = 4096;
    for (size_t i = 0; i < acl->num_grants; i++) {
        buf_size += 512;  /* 每个 grant 预留空间 */
        if (acl->grants[i].id) buf_size += strlen(acl->grants[i].id);
        if (acl->grants[i].display_name) buf_size += strlen(acl->grants[i].display_name);
        if (acl->grants[i].url_group) buf_size += strlen(acl->grants[i].url_group);
    }

    char* buf = (char*)malloc(buf_size);
    if (!buf) return -ENOMEM;

    size_t offset = 0;
    int written;

    written = snprintf(buf + offset, buf_size - offset, "{\"owner\":{\"id\":\"%s\",\"display_name\":\"%s\"},\"grants\":[",
                       acl->owner.id ? acl->owner.id : "",
                       acl->owner.display_name ? acl->owner.display_name : "");
    if (written < 0 || (size_t)written >= buf_size - offset) {
        free(buf);
        return -ENOBUFS;
    }
    offset += written;

    for (size_t i = 0; i < acl->num_grants; i++) {
        if (i > 0) {
            written = snprintf(buf + offset, buf_size - offset, ",");
            if (written < 0 || (size_t)written >= buf_size - offset) {
                free(buf);
                return -ENOBUFS;
            }
            offset += written;
        }

        written = snprintf(buf + offset, buf_size - offset,
                          "{\"type\":%d,\"perm\":%d,\"id\":\"%s\",\"display_name\":\"%s\",\"group_type\":%d,\"url_group\":\"%s\"}",
                          acl->grants[i].type,
                          acl->grants[i].perm,
                          acl->grants[i].id ? acl->grants[i].id : "",
                          acl->grants[i].display_name ? acl->grants[i].display_name : "",
                          acl->grants[i].group_type,
                          acl->grants[i].url_group ? acl->grants[i].url_group : "");
        if (written < 0 || (size_t)written >= buf_size - offset) {
            free(buf);
            return -ENOBUFS;
        }
        offset += written;
    }

    written = snprintf(buf + offset, buf_size - offset, "]}");
    if (written < 0 || (size_t)written >= buf_size - offset) {
        free(buf);
        return -ENOBUFS;
    }
    offset += written;

    *json_str = buf;
    if (json_len) *json_len = offset;
    return 0;
}

/**
 * @brief 跳过空白字符
 */
static const char* skip_whitespace(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

/**
 * @brief 提取 JSON 字符串值
 */
static int extract_json_string(const char* json, const char* key, char** out_value) {
    if (!json || !key || !out_value) return -EINVAL;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char* p = strstr(json, search_key);
    if (!p) {
        *out_value = NULL;
        return 0;
    }

    p += strlen(search_key);
    p = skip_whitespace(p);
    if (*p != ':') return -EINVAL;
    p++;
    p = skip_whitespace(p);

    if (*p != '"') return -EINVAL;
    p++;

    const char* end = p;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1]) {
            end += 2;
        } else {
            end++;
        }
    }

    if (*end != '"') return -EINVAL;

    size_t len = end - p;
    *out_value = (char*)malloc(len + 1);
    if (!*out_value) return -ENOMEM;

    memcpy(*out_value, p, len);
    (*out_value)[len] = '\0';

    return 0;
}

/**
 * @brief 提取 JSON 整数值
 */
static int extract_json_int(const char* json, const char* key, int* out_value) {
    if (!json || !key || !out_value) return -EINVAL;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char* p = strstr(json, search_key);
    if (!p) return -EINVAL;

    p += strlen(search_key);
    p = skip_whitespace(p);
    if (*p != ':') return -EINVAL;
    p++;
    p = skip_whitespace(p);

    *out_value = atoi(p);
    return 0;
}

int rgw_acl_from_json(const char* json_str, size_t json_len, rgw_acl_info_t* acl) {
    if (!json_str || !acl) return -EINVAL;

    /* 使用固定大小缓冲区复制字符串，添加终止符 */
    char* json = (char*)malloc(json_len + 1);
    if (!json) return -ENOMEM;

    memcpy(json, json_str, json_len);
    json[json_len] = '\0';

    int ret = 0;

    /* 提取 owner.id */
    char* owner_id = NULL;
    ret = extract_json_string(json, "owner_id", &owner_id);
    if (ret < 0) goto cleanup;
    acl->owner.id = owner_id;

    /* 提取 owner.display_name */
    char* owner_name = NULL;
    ret = extract_json_string(json, "owner_display_name", &owner_name);
    if (ret < 0) goto cleanup;
    acl->owner.display_name = owner_name;

    /* 提取 grants 数组 - 简化处理，查找 grants 数组中的元素 */
    const char* grants_start = strstr(json, "\"grants\"");
    if (!grants_start) {
        acl->num_grants = 0;
        ret = 0;
        goto cleanup;
    }

    /* 简化实现：统计逗号来估计 grant 数量（不精确但可用） */
    const char* p = grants_start;
    size_t grant_count = 0;
    int brace_depth = 0;
    bool in_string = false;

    /* 找到 '[' 开始 */
    while (*p && *p != '[') p++;
    if (*p != '[') {
        acl->num_grants = 0;
        ret = 0;
        goto cleanup;
    }
    p++;

    /* 简单计数：每个 { } 对应一个 grant */
    while (*p) {
        if (*p == '"' && (p == json || p[-1] != '\\')) {
            in_string = !in_string;
        }
        if (!in_string) {
            if (*p == '{') {
                if (brace_depth == 0) grant_count++;
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
            } else if (*p == ']' && brace_depth == 0) {
                break;
            }
        }
        p++;
    }

    acl->num_grants = grant_count;
    if (grant_count > 0) {
        acl->grants = (rgw_acl_grant_t*)calloc(grant_count, sizeof(rgw_acl_grant_t));
        if (!acl->grants) {
            ret = -ENOMEM;
            goto cleanup;
        }
        acl->grants_capacity = grant_count;

        /* 解析每个 grant */
        p = grants_start;
        while (*p && *p != '[') p++;
        if (*p == '[') p++;

        for (size_t i = 0; i < grant_count; i++) {
            /* 找到下一个 { */
            while (*p && *p != '{') p++;
            if (*p != '{') break;

            const char* grant_start = p;
            brace_depth = 0;
            in_string = false;

            /* 找到对应的 } */
            while (*p) {
                if (*p == '"' && (p == json || p[-1] != '\\')) {
                    in_string = !in_string;
                }
                if (!in_string) {
                    if (*p == '{') brace_depth++;
                    else if (*p == '}') {
                        brace_depth--;
                        if (brace_depth == 0) {
                            p++;
                            break;
                        }
                    }
                }
                p++;
            }

            size_t grant_len = p - grant_start;
            char* grant_json = (char*)malloc(grant_len + 1);
            if (!grant_json) continue;
            memcpy(grant_json, grant_start, grant_len);
            grant_json[grant_len] = '\0';

            /* 提取 grant 字段 */
            extract_json_int(grant_json, "type", (int*)&acl->grants[i].type);
            extract_json_int(grant_json, "perm", (int*)&acl->grants[i].perm);
            extract_json_int(grant_json, "group_type", &acl->grants[i].group_type);
            extract_json_string(grant_json, "id", &acl->grants[i].id);
            extract_json_string(grant_json, "display_name", &acl->grants[i].display_name);
            extract_json_string(grant_json, "url_group", &acl->grants[i].url_group);

            free(grant_json);
        }
    }

    ret = 0;

cleanup:
    free(json);
    return ret;
}

/*============================================================================
 * OMAP 键
 *============================================================================*/

int rgw_acl_make_bucket_omap_key(const char* bucket_name, char* buf, size_t buf_size) {
    if (!bucket_name || !buf) return -EINVAL;

    int written = snprintf(buf, buf_size, ".bucket.acl.%s", bucket_name);
    if (written < 0 || (size_t)written >= buf_size) {
        return -ENOBUFS;
    }

    return 0;
}

/*============================================================================
 * 工具函数
 *============================================================================*/

bool rgw_acl_is_empty(const rgw_acl_info_t* acl) {
    if (!acl) return true;
    return acl->num_grants == 0 && acl->owner.id == NULL;
}

bool rgw_acl_equal(const rgw_acl_info_t* a, const rgw_acl_info_t* b) {
    if (!a || !b) return false;

    /* 比较 owner */
    if ((a->owner.id == NULL) != (b->owner.id == NULL)) return false;
    if (a->owner.id && strcmp(a->owner.id, b->owner.id) != 0) return false;

    if ((a->owner.display_name == NULL) != (b->owner.display_name == NULL)) return false;
    if (a->owner.display_name && strcmp(a->owner.display_name, b->owner.display_name) != 0) return false;

    /* 比较 grants */
    if (a->num_grants != b->num_grants) return false;

    for (size_t i = 0; i < a->num_grants; i++) {
        if (a->grants[i].type != b->grants[i].type) return false;
        if (a->grants[i].perm != b->grants[i].perm) return false;
        if (a->grants[i].group_type != b->grants[i].group_type) return false;

        if ((a->grants[i].id == NULL) != (b->grants[i].id == NULL)) return false;
        if (a->grants[i].id && strcmp(a->grants[i].id, b->grants[i].id) != 0) return false;

        if ((a->grants[i].display_name == NULL) != (b->grants[i].display_name == NULL)) return false;
        if (a->grants[i].display_name && strcmp(a->grants[i].display_name, b->grants[i].display_name) != 0) return false;
    }

    return true;
}

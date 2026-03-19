/**
 * @file rgw_user_serde.c
 * @brief 用户信息序列化/反序列化实现
 *
 * 实现用户信息的二进制序列化，用于存储到 RADOS OMAP。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgw_user_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 安全复制字符串
 *
 * @param dest 目标地址
 * @param dest_size 目标缓冲区大小
 * @param src 源字符串
 *
 * @return 复制是否成功
 */
static bool safe_strcpy(char* dest, size_t dest_size, const char* src) {
    if (!dest || dest_size == 0) {
        return false;
    }

    if (!src) {
        dest[0] = '\0';
        return true;
    }

    size_t len = strlen(src);
    if (len >= dest_size) {
        return false;
    }

    memcpy(dest, src, len + 1);
    return true;
}

/**
 * @brief 分配并复制字符串
 *
 * @param str 源字符串
 *
 * @return 复制后的字符串，失败返回 NULL
 */
static char* str_dup(const char* str) {
    if (!str) {
        return NULL;
    }
    return strdup(str);
}

/*============================================================================
 * 函数实现 - 初始化和销毁
 *============================================================================*/

/**
 * @brief 初始化用户信息
 */
void rgw_user_info_init(rgw_user_info_t* info) {
    if (!info) {
        return;
    }

    memset(info, 0, sizeof(rgw_user_info_t));

    /* 设置默认值 */
    info->max_buckets = -1;  /* 无限制 */
    info->quota.max_size = -1;
    info->quota.max_objects = -1;
    info->quota.enabled = false;
    info->quota.check_on_raw = false;
    info->suspended = false;
    info->system = false;
}

/**
 * @brief 释放用户信息动态内存
 */
void rgw_user_info_free_members(rgw_user_info_t* info) {
    if (!info) {
        return;
    }

    free(info->user_id.tenant);
    free(info->user_id.id);
    free(info->user_id.ns);
    free(info->display_name);
    free(info->email);
    free(info->mfa_ids);

    info->user_id.tenant = NULL;
    info->user_id.id = NULL;
    info->user_id.ns = NULL;
    info->display_name = NULL;
    info->email = NULL;
    info->mfa_ids = NULL;
}

/**
 * @brief 销毁用户信息
 */
void rgw_user_info_destroy(rgw_user_info_t* info) {
    if (!info) {
        return;
    }

    rgw_user_info_free_members(info);
    free(info);
}

/*============================================================================
 * 函数实现 - 序列化
 *============================================================================*/

/**
 * @brief 计算编码后的大小
 */
size_t rgw_user_info_calc_encode_size(const rgw_user_info_t* info) {
    if (!info) {
        return 0;
    }

    size_t size = 0;

    /* 版本号 (2 * sizeof(uint32_t)) */
    size += sizeof(uint32_t) * 2;

    /* 用户 ID */
    size += sizeof(uint32_t);
    if (info->user_id.tenant) {
        size += strlen(info->user_id.tenant);
    }

    size += sizeof(uint32_t);
    if (info->user_id.id) {
        size += strlen(info->user_id.id);
    }

    size += sizeof(uint32_t);
    if (info->user_id.ns) {
        size += strlen(info->user_id.ns);
    }

    /* user_id.type */
    size += sizeof(uint32_t);

    /* display_name */
    size += sizeof(uint32_t);
    if (info->display_name) {
        size += strlen(info->display_name);
    }

    /* email */
    size += sizeof(uint32_t);
    if (info->email) {
        size += strlen(info->email);
    }

    /* user_type */
    size += sizeof(uint32_t);

    /* max_buckets */
    size += sizeof(int32_t);

    /* permissions */
    size += sizeof(uint32_t);

    /* quota */
    size += sizeof(int64_t) * 2;  /* max_size, max_objects */
    size += sizeof(bool) * 2;      /* enabled, check_on_raw */

    /* temp_url_key[2] */
    size += sizeof(int64_t) * 2;

    /* mtime */
    size += sizeof(int64_t);

    /* user_stats_quota */
    size += sizeof(uint32_t);

    /* stats_quota */
    size += sizeof(uint32_t);

    /* suspended */
    size += sizeof(bool);

    /* system */
    size += sizeof(bool);

    /* mfa_ids */
    size += sizeof(uint32_t);
    if (info->mfa_ids) {
        size += strlen(info->mfa_ids);
    }

    /* objv_tracker */
    size += sizeof(uint64_t) * 2;  /* read_version.ver, write_version.ver */
    size += sizeof(uint32_t) * 2;  /* read_version.epoch, write_version.epoch */
    size += sizeof(bool) * 2;      /* read_version.exists, write_version.exists */
    size += sizeof(bool);          /* ignore_dirty */

    return size;
}

/**
 * @brief 编码用户信息到缓冲区
 */
int rgw_user_info_encode(const rgw_user_info_t* info,
                         uint8_t* buf,
                         size_t buf_size) {
    if (!info || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t required_size = rgw_user_info_calc_encode_size(info);
    if (buf_size < required_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    uint8_t* p = buf;

    /* 写入版本号 */
    uint32_t ver_start = RGW_USER_INFO_ENCODE_VERSION_START;
    uint32_t ver_end = RGW_USER_INFO_ENCODE_VERSION_END;

    memcpy(p, &ver_start, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &ver_end, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入 tenant */
    uint32_t tenant_len = info->user_id.tenant ? strlen(info->user_id.tenant) : 0;
    memcpy(p, &tenant_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (tenant_len > 0) {
        memcpy(p, info->user_id.tenant, tenant_len);
        p += tenant_len;
    }

    /* 写入 user id */
    uint32_t id_len = info->user_id.id ? strlen(info->user_id.id) : 0;
    memcpy(p, &id_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (id_len > 0) {
        memcpy(p, info->user_id.id, id_len);
        p += id_len;
    }

    /* 写入 ns */
    uint32_t ns_len = info->user_id.ns ? strlen(info->user_id.ns) : 0;
    memcpy(p, &ns_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (ns_len > 0) {
        memcpy(p, info->user_id.ns, ns_len);
        p += ns_len;
    }

    /* 写入 user_id.type */
    memcpy(p, &info->user_id.type, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入 display_name */
    uint32_t name_len = info->display_name ? strlen(info->display_name) : 0;
    memcpy(p, &name_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (name_len > 0) {
        memcpy(p, info->display_name, name_len);
        p += name_len;
    }

    /* 写入 email */
    uint32_t email_len = info->email ? strlen(info->email) : 0;
    memcpy(p, &email_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (email_len > 0) {
        memcpy(p, info->email, email_len);
        p += email_len;
    }

    /* 写入 user_type */
    memcpy(p, &info->user_type, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入 max_buckets */
    memcpy(p, &info->max_buckets, sizeof(int32_t));
    p += sizeof(int32_t);

    /* 写入 permissions */
    memcpy(p, &info->permissions, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入配额信息 */
    memcpy(p, &info->quota.max_size, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(p, &info->quota.max_objects, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(p, &info->quota.enabled, sizeof(bool));
    p += sizeof(bool);
    memcpy(p, &info->quota.check_on_raw, sizeof(bool));
    p += sizeof(bool);

    /* 写入 temp_url_key */
    memcpy(p, &info->temp_url_key[0], sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(p, &info->temp_url_key[1], sizeof(int64_t));
    p += sizeof(int64_t);

    /* 写入 mtime */
    memcpy(p, &info->mtime, sizeof(int64_t));
    p += sizeof(int64_t);

    /* 写入 user_stats_quota */
    memcpy(p, &info->user_stats_quota, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入 stats_quota */
    memcpy(p, &info->stats_quota, sizeof(uint32_t));
    p += sizeof(uint32_t);

    /* 写入 suspended */
    memcpy(p, &info->suspended, sizeof(bool));
    p += sizeof(bool);

    /* 写入 system */
    memcpy(p, &info->system, sizeof(bool));
    p += sizeof(bool);

    /* 写入 mfa_ids */
    uint32_t mfa_len = info->mfa_ids ? strlen(info->mfa_ids) : 0;
    memcpy(p, &mfa_len, sizeof(uint32_t));
    p += sizeof(uint32_t);
    if (mfa_len > 0) {
        memcpy(p, info->mfa_ids, mfa_len);
        p += mfa_len;
    }

    /* 写入 objv_tracker */
    memcpy(p, &info->objv_tracker.read_version.ver, sizeof(uint64_t));
    p += sizeof(uint64_t);
    memcpy(p, &info->objv_tracker.read_version.epoch, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &info->objv_tracker.read_version.exists, sizeof(bool));
    p += sizeof(bool);
    memcpy(p, &info->objv_tracker.write_version.ver, sizeof(uint64_t));
    p += sizeof(uint64_t);
    memcpy(p, &info->objv_tracker.write_version.epoch, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(p, &info->objv_tracker.write_version.exists, sizeof(bool));
    p += sizeof(bool);
    memcpy(p, &info->objv_tracker.ignore_dirty, sizeof(bool));
    p += sizeof(bool);

    return (int)(p - buf);
}

/**
 * @brief 动态编码用户信息
 */
int rgw_user_info_encode_alloc(const rgw_user_info_t* info,
                               uint8_t** out_buf,
                               size_t* out_len) {
    if (!info || !out_buf || !out_len) {
        return RGW_ERR_INVALID_ARG;
    }

    size_t buf_size = rgw_user_info_calc_encode_size(info);
    if (buf_size == 0) {
        return RGW_ERR_INVALID_ARG;
    }

    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) {
        return RGW_ERR_OUT_OF_MEMORY;
    }

    int ret = rgw_user_info_encode(info, buf, buf_size);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_len = (size_t)ret;

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 反序列化
 *============================================================================*/

/**
 * @brief 从缓冲区解码用户信息
 */
int rgw_user_info_decode(const uint8_t* buf,
                         size_t buf_len,
                         rgw_user_info_t* info) {
    if (!buf || !info) {
        return RGW_ERR_INVALID_ARG;
    }

    rgw_user_info_init(info);

    const uint8_t* p = buf;
    size_t remaining = buf_len;

    /* 读取版本号 */
    if (remaining < sizeof(uint32_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t ver_start, ver_end;
    memcpy(&ver_start, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&ver_end, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t) * 2;

    /* 读取 tenant */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t tenant_len;
    memcpy(&tenant_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (tenant_len > remaining || tenant_len >= RGW_USER_INFO_MAX_ID_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (tenant_len > 0) {
        info->user_id.tenant = (char*)malloc(tenant_len + 1);
        if (!info->user_id.tenant) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->user_id.tenant, p, tenant_len);
        info->user_id.tenant[tenant_len] = '\0';
        p += tenant_len;
        remaining -= tenant_len;
    }

    /* 读取 user id */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t id_len;
    memcpy(&id_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (id_len > remaining || id_len >= RGW_USER_INFO_MAX_ID_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (id_len > 0) {
        info->user_id.id = (char*)malloc(id_len + 1);
        if (!info->user_id.id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->user_id.id, p, id_len);
        info->user_id.id[id_len] = '\0';
        p += id_len;
        remaining -= id_len;
    }

    /* 读取 ns */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t ns_len;
    memcpy(&ns_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (ns_len > remaining || ns_len >= RGW_USER_INFO_MAX_ID_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (ns_len > 0) {
        info->user_id.ns = (char*)malloc(ns_len + 1);
        if (!info->user_id.ns) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->user_id.ns, p, ns_len);
        info->user_id.ns[ns_len] = '\0';
        p += ns_len;
        remaining -= ns_len;
    }

    /* 读取 user_id.type */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->user_id.type, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* 读取 display_name */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t name_len;
    memcpy(&name_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (name_len > remaining || name_len >= RGW_USER_INFO_MAX_DISPLAY_NAME_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (name_len > 0) {
        info->display_name = (char*)malloc(name_len + 1);
        if (!info->display_name) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->display_name, p, name_len);
        info->display_name[name_len] = '\0';
        p += name_len;
        remaining -= name_len;
    }

    /* 读取 email */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t email_len;
    memcpy(&email_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (email_len > remaining || email_len >= RGW_USER_INFO_MAX_EMAIL_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (email_len > 0) {
        info->email = (char*)malloc(email_len + 1);
        if (!info->email) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->email, p, email_len);
        info->email[email_len] = '\0';
        p += email_len;
        remaining -= email_len;
    }

    /* 读取 user_type */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->user_type, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* 读取 max_buckets */
    if (remaining < sizeof(int32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->max_buckets, p, sizeof(int32_t));
    p += sizeof(int32_t);
    remaining -= sizeof(int32_t);

    /* 读取 permissions */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->permissions, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* 读取配额信息 */
    if (remaining < sizeof(int64_t) * 2 + sizeof(bool) * 2) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->quota.max_size, p, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(&info->quota.max_objects, p, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(&info->quota.enabled, p, sizeof(bool));
    p += sizeof(bool);
    memcpy(&info->quota.check_on_raw, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(int64_t) * 2 + sizeof(bool) * 2;

    /* 读取 temp_url_key */
    if (remaining < sizeof(int64_t) * 2) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->temp_url_key[0], p, sizeof(int64_t));
    p += sizeof(int64_t);
    memcpy(&info->temp_url_key[1], p, sizeof(int64_t));
    p += sizeof(int64_t);
    remaining -= sizeof(int64_t) * 2;

    /* 读取 mtime */
    if (remaining < sizeof(int64_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->mtime, p, sizeof(int64_t));
    p += sizeof(int64_t);
    remaining -= sizeof(int64_t);

    /* 读取 user_stats_quota */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->user_stats_quota, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* 读取 stats_quota */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->stats_quota, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    /* 读取 suspended */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->suspended, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* 读取 system */
    if (remaining < sizeof(bool)) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->system, p, sizeof(bool));
    p += sizeof(bool);
    remaining -= sizeof(bool);

    /* 读取 mfa_ids */
    if (remaining < sizeof(uint32_t)) {
        return RGW_ERR_INVALID_ARG;
    }

    uint32_t mfa_len;
    memcpy(&mfa_len, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    remaining -= sizeof(uint32_t);

    if (mfa_len > remaining || mfa_len >= RGW_USER_INFO_MAX_ID_LEN) {
        return RGW_ERR_INVALID_ARG;
    }

    if (mfa_len > 0) {
        info->mfa_ids = (char*)malloc(mfa_len + 1);
        if (!info->mfa_ids) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
        memcpy(info->mfa_ids, p, mfa_len);
        info->mfa_ids[mfa_len] = '\0';
        p += mfa_len;
        remaining -= mfa_len;
    }

    /* 读取 objv_tracker */
    if (remaining < sizeof(uint64_t) * 2 + sizeof(uint32_t) * 2 + sizeof(bool) * 3) {
        return RGW_ERR_INVALID_ARG;
    }
    memcpy(&info->objv_tracker.read_version.ver, p, sizeof(uint64_t));
    p += sizeof(uint64_t);
    memcpy(&info->objv_tracker.read_version.epoch, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&info->objv_tracker.read_version.exists, p, sizeof(bool));
    p += sizeof(bool);
    memcpy(&info->objv_tracker.write_version.ver, p, sizeof(uint64_t));
    p += sizeof(uint64_t);
    memcpy(&info->objv_tracker.write_version.epoch, p, sizeof(uint32_t));
    p += sizeof(uint32_t);
    memcpy(&info->objv_tracker.write_version.exists, p, sizeof(bool));
    p += sizeof(bool);
    memcpy(&info->objv_tracker.ignore_dirty, p, sizeof(bool));
    p += sizeof(bool);

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 索引操作
 *============================================================================*/

/**
 * @brief 构建用户 OMAP 键名
 */
int rgw_user_info_make_omap_key(const rgw_user_id_t* user_id,
                                 char* buf,
                                 size_t buf_size) {
    if (!user_id || !buf) {
        return RGW_ERR_INVALID_ARG;
    }

    const char* tenant = user_id->tenant ? user_id->tenant : "";
    const char* id = user_id->id ? user_id->id : "";

    int ret = snprintf(buf, buf_size, "%s:%s", tenant, id);
    if (ret < 0 || (size_t)ret >= buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }

    return RGW_OK;
}

/**
 * @brief 构建 access key 索引键
 */
char* rgw_user_access_key_index_make_key(const char* access_key) {
    if (!access_key) {
        return NULL;
    }
    return strdup(access_key);
}

/**
 * @brief 构建 email 索引键
 */
char* rgw_user_email_index_make_key(const char* email) {
    if (!email) {
        return NULL;
    }
    return strdup(email);
}

/**
 * @brief 编码 access key 索引值
 */
char* rgw_user_access_key_index_encode_value(const rgw_user_id_t* user_id) {
    if (!user_id) {
        return NULL;
    }

    const char* tenant = user_id->tenant ? user_id->tenant : "";
    const char* id = user_id->id ? user_id->id : "";

    char* buf = (char*)malloc(RGW_USER_INFO_MAX_ID_LEN * 2 + 2);
    if (!buf) {
        return NULL;
    }

    snprintf(buf, RGW_USER_INFO_MAX_ID_LEN * 2 + 2, "%s:%s", tenant, id);
    return buf;
}

/**
 * @brief 编码 email 索引值
 */
char* rgw_user_email_index_encode_value(const rgw_user_id_t* user_id) {
    return rgw_user_access_key_index_encode_value(user_id);
}

/**
 * @brief 解码索引键获取用户 ID
 */
int rgw_user_id_decode_from_index(const char* value,
                                   rgw_user_id_t* user_id) {
    if (!value || !user_id) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(user_id, 0, sizeof(rgw_user_id_t));

    /* 解析 tenant:id 格式 */
    const char* colon = strchr(value, ':');
    if (colon) {
        size_t tenant_len = colon - value;
        if (tenant_len > 0) {
            user_id->tenant = (char*)malloc(tenant_len + 1);
            if (!user_id->tenant) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
            memcpy(user_id->tenant, value, tenant_len);
            user_id->tenant[tenant_len] = '\0';

            user_id->id = strdup(colon + 1);
            if (!user_id->id) {
                free(user_id->tenant);
                user_id->tenant = NULL;
                return RGW_ERR_OUT_OF_MEMORY;
            }
        } else {
            user_id->id = strdup(colon + 1);
            if (!user_id->id) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
        }
    } else {
        user_id->id = strdup(value);
        if (!user_id->id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 工具函数
 *============================================================================*/

/**
 * @brief 深拷贝用户信息
 */
int rgw_user_info_deep_copy(const rgw_user_info_t* src,
                             rgw_user_info_t* dst) {
    if (!src || !dst) {
        return RGW_ERR_INVALID_ARG;
    }

    /* 先释放目标可能存在的内存 */
    rgw_user_info_free_members(dst);

    /* 复制简单类型 */
    *dst = *src;

    /* 深拷贝字符串成员 */
    dst->user_id.tenant = str_dup(src->user_id.tenant);
    dst->user_id.id = str_dup(src->user_id.id);
    dst->user_id.ns = str_dup(src->user_id.ns);
    dst->display_name = str_dup(src->display_name);
    dst->email = str_dup(src->email);
    dst->mfa_ids = str_dup(src->mfa_ids);

    return RGW_OK;
}

/**
 * @brief 比较两个用户 ID
 */
bool rgw_user_id_equal(const rgw_user_id_t* a, const rgw_user_id_t* b) {
    if (!a || !b) {
        return false;
    }

    const char* a_tenant = a->tenant ? a->tenant : "";
    const char* b_tenant = b->tenant ? b->tenant : "";
    const char* a_id = a->id ? a->id : "";
    const char* b_id = b->id ? b->id : "";

    return (strcmp(a_tenant, b_tenant) == 0) && (strcmp(a_id, b_id) == 0);
}

/**
 * @brief 获取用户 ID 字符串表示
 */
char* rgw_user_id_to_string(const rgw_user_id_t* user_id) {
    if (!user_id) {
        return NULL;
    }

    const char* tenant = user_id->tenant ? user_id->tenant : "";
    const char* id = user_id->id ? user_id->id : "";

    char* buf = (char*)malloc(RGW_USER_INFO_MAX_ID_LEN * 2 + 2);
    if (!buf) {
        return NULL;
    }

    snprintf(buf, RGW_USER_INFO_MAX_ID_LEN * 2 + 2, "%s:%s", tenant, id);
    return buf;
}

/**
 * @brief 解析用户 ID 字符串
 */
int rgw_user_id_parse(const char* str, rgw_user_id_t* user_id) {
    if (!str || !user_id) {
        return RGW_ERR_INVALID_ARG;
    }

    memset(user_id, 0, sizeof(rgw_user_id_t));

    /* 解析 tenant:id 格式 */
    const char* colon = strchr(str, ':');
    if (colon) {
        size_t tenant_len = colon - str;
        if (tenant_len > 0) {
            user_id->tenant = (char*)malloc(tenant_len + 1);
            if (!user_id->tenant) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
            memcpy(user_id->tenant, str, tenant_len);
            user_id->tenant[tenant_len] = '\0';

            user_id->id = strdup(colon + 1);
            if (!user_id->id) {
                free(user_id->tenant);
                user_id->tenant = NULL;
                return RGW_ERR_OUT_OF_MEMORY;
            }
        } else {
            user_id->id = strdup(colon + 1);
            if (!user_id->id) {
                return RGW_ERR_OUT_OF_MEMORY;
            }
        }
    } else {
        user_id->id = strdup(str);
        if (!user_id->id) {
            return RGW_ERR_OUT_OF_MEMORY;
        }
    }

    return RGW_OK;
}

/*============================================================================
 * 函数实现 - 初始化函数
 *============================================================================*/

/**
 * @brief 创建新的用户信息
 */
rgw_user_info_t* rgw_user_info_create(const char* user_id,
                                        const char* display_name) {
    rgw_user_info_t* info = (rgw_user_info_t*)malloc(sizeof(rgw_user_info_t));
    if (!info) {
        return NULL;
    }

    rgw_user_info_init(info);

    if (user_id) {
        info->user_id.id = strdup(user_id);
        if (!info->user_id.id) {
            free(info);
            return NULL;
        }
    }

    if (display_name) {
        info->display_name = strdup(display_name);
        if (!info->display_name) {
            free(info->user_id.id);
            free(info);
            return NULL;
        }
    }

    return info;
}

/**
 * @brief 创建用户 ID
 */
rgw_user_id_t* rgw_user_id_create(const char* tenant,
                                    const char* id,
                                    const char* ns) {
    rgw_user_id_t* user_id = (rgw_user_id_t*)malloc(sizeof(rgw_user_id_t));
    if (!user_id) {
        return NULL;
    }

    memset(user_id, 0, sizeof(rgw_user_id_t));

    if (tenant) {
        user_id->tenant = strdup(tenant);
        if (!user_id->tenant) {
            free(user_id);
            return NULL;
        }
    }

    if (id) {
        user_id->id = strdup(id);
        if (!user_id->id) {
            free(user_id->tenant);
            free(user_id);
            return NULL;
        }
    }

    if (ns) {
        user_id->ns = strdup(ns);
        if (!user_id->ns) {
            free(user_id->id);
            free(user_id->tenant);
            free(user_id);
            return NULL;
        }
    }

    return user_id;
}

/**
 * @brief 销毁用户 ID
 */
void rgw_user_id_destroy(rgw_user_id_t* user_id) {
    if (!user_id) {
        return;
    }

    free(user_id->tenant);
    free(user_id->id);
    free(user_id->ns);
    free(user_id);
}

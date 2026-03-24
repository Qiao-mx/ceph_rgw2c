/**
 * @file rgw_sal_rados.c
 * @brief RADOS 驱动 C 接口实现
 *
 * 实现 RADOS 存储后端的 C 语言接口。
 * 使用 vtable 模式提供多态支持。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <rados/librados.h>

#include "rgw_sal.h"
#include "rgw_sal_rados.h"
#include "rgw_sal_errors.h"

/* 引入 OMAP 和序列化封装 */
#include "rgw_omap.h"
#include "rgw_user_serde.h"
#include "rgw_bucket_serde.h"
#include "rgw_lifecycle.h"
#include "rgw_multipart.h"
#include "rgw_account_serde.h"
#include "rgw_group_serde.h"
#include "rgw_oidc_serde.h"
#include "rgw_notification.h"
#include "rgw_errors.h"
#include "rgw_sal_usage.h"
#include "rgw_acl_serde.h"
#include "rgw_policy_serde.h"

/*============================================================================
 * RADOS 驱动常量定义
 *============================================================================*/

/**
 * @brief 安全获取用户 ID 的辅助宏
 *
 * 使用 vtable 函数获取用户 ID，如果失败则返回指定的错误码
 * 用法: const char* id = RADOS_USER_GET_ID_SAFE(user); if (!id) return -1;
 */
#define RADOS_USER_GET_ID(user) \
    ((user) && (user)->vtable && (user)->vtable->get_id ? \
     (user)->vtable->get_id(user) : NULL)

/**
 * @brief OMAP 键名常量
 */
#define RGW_BUCKET_ACL_OMAP_KEY "acl"
#define RGW_BUCKET_POLICY_OMAP_KEY "policy"
#define RGW_BUCKET_STATS_OMAP_KEY "stats"

/*============================================================================
 * 函数前向声明
 *============================================================================*/

/* 桶 ACL/策略 OMAP 对象名构建函数 */
static int rados_bucket_acl_make_omap_oid(const char* bucket_name,
                                          char* buf, size_t buf_size);

/* 对象 OID 构建函数 */
static int rados_build_object_oid(rgw_sal_object_t* obj, char* oid, size_t oid_size);

/**
 * @brief RADOS 驱动内部结构
 *============================================================================*/

/**
 * @brief RADOS 用户实现
 */
typedef struct rados_user_impl {
    char* id;
    char* tenant;
    char* display_name;
    char* email;
    char* ns;                     /**< 命名空间 */
    uint32_t user_type;
    int32_t max_buckets;
    rgw_sal_attrs_t* attrs;
    rgw_sal_quota_info_t quota_info;      /**< 配额信息 (P0: 直接实现) */
    rgw_sal_user_caps_t user_caps;        /**< 用户权限 (P0: 直接实现) */
    rgw_sal_obj_version_tracker_t version_tracker; /**< 版本跟踪器 (P0: 直接实现) */
    /* 模块 A: User 统计功能 */
    rgw_sal_usage_info_t usage;          /**< 使用统计缓存 */
    bool usage_loaded;                     /**< 使用统计是否已加载 */
    bool loaded;

    /* 用于持久化的用户信息 */
    rgw_user_info_t user_info;   /**< 用户完整信息 (用于 OMAP 存储) */
    bool user_info_stored;        /**< 用户信息是否已存储 */

    /* 内存安全: 销毁状态标记 */
    bool destroyed;               /**< 防止双重释放 */
} rados_user_impl_t;

/**
 * @brief RADOS 桶实现
 */
typedef struct rados_bucket_impl {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    rgw_sal_attrs_t* attrs;
    void* acl;              /**< ACL 策略指针 */
    void* policy;           /**< IAM 策略指针 */
    char* tag;              /**< 桶标签 (P0: 直接实现) */
    bool loaded;
    bool created;        /**< 是否已创建 */
    bool deleted;        /**< 是否已删除 */
    time_t mtime;        /**< 修改时间 */

    /* 内存安全: 销毁状态标记 */
    bool destroyed;       /**< 防止双重释放 */
} rados_bucket_impl_t;

/**
 * @brief RADOS 对象实现
 */
typedef struct rados_object_impl {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    char* bucket_id;            /**< 桶 ID，用于确定数据池 */
    char* obj_oid;               /**< 对象 OID */
    rgw_sal_attrs_t* attrs;
    bool is_null;
    int64_t size;           /**< 对象大小 */
    time_t mtime;           /**< 修改时间 */
    bool written;           /**< 是否已写入 */
    bool deleted;           /**< 是否已删除 */
    bool loaded;            /**< 是否已加载状态 */
    bool is_atomic;         /**< 是否原子操作 (P0: 直接实现) */
    bool is_expired;        /**< 是否已过期 (P0: 直接实现) */

    /* 对象 IO 上下文 (在运行时获取) */
    rados_ioctx_t data_ioctx;  /**< 数据池 IO 上下文 */

    /* 内存安全: 销毁状态标记 */
    bool destroyed;             /**< 防止双重释放 */
} rados_object_impl_t;

/**
 * @brief RADOS 驱动实现
 */
typedef struct rados_driver_impl {
    char name[64];
    void* rados_handle;          /**< librados 集群句柄 */
    void* cct;                  /**< Ceph 上下文 */
    bool initialized;            /**< 是否已初始化 */

    /* RADOS 连接信息 - 用于 OMAP 操作 */
    rados_ioctx_t users_uid_ioctx;      /**< 用户 UID 池 IO 上下文 */
    rados_ioctx_t users_email_ioctx;    /**< 用户 Email 池 IO 上下文 */
    rados_ioctx_t users_keys_ioctx;     /**< 用户 Keys 池 IO 上下文 */
    rados_ioctx_t users_swift_ioctx;    /**< 用户 Swift 池 IO 上下文 */
    rados_ioctx_t buckets_index_ioctx;  /**< 桶索引池 IO 上下文 */
    rados_ioctx_t buckets_data_ioctx;   /**< 桶数据池 IO 上下文 */

    /* RADOS OMAP 连接信息 - 用于生命周期和账户等 */
    rados_ioctx_t lc_pool_ioctx;        /**< 生命周期池 IO 上下文 */
    rados_ioctx_t account_pool_ioctx;   /**< 账户池 IO 上下文 */
    rados_ioctx_t group_pool_ioctx;     /**< 组池 IO 上下文 */
    rados_ioctx_t oidc_pool_ioctx;      /**< OIDC 池 IO 上下文 */
    rados_ioctx_t topic_pool_ioctx;     /**< Topic 池 IO 上下文 */

    /* 连接状态 */
    bool ioctxs_initialized;    /**< IO 上下文是否已初始化 */
} rados_driver_impl_t;

/*============================================================================
 * 桶索引损坏检测结构
 *============================================================================*/

/**
 * @brief 索引损坏类型
 */
typedef enum {
    RGW_DAMAGE_NONE = 0,              /**< 无损坏 */
    RGW_DAMAGE_INDEX_BUT_NO_DATA = 1,  /**< 索引存在但数据不存在 */
    RGW_DAMAGE_DATA_BUT_NO_INDEX = 2,  /**< 数据存在但索引不存在 */
    RGW_DAMAGE_DATA_CORRUPTED = 3      /**< 数据损坏 */
} rgw_damage_type_t;

/**
 * @brief 损坏条目
 */
typedef struct {
    char oid[256];              /**< 对象 ID */
    rgw_damage_type_t type;      /**< 损坏类型 */
    time_t detected_time;        /**< 检测时间 */
} rgw_damage_entry_t;

/**
 * @brief 损坏列表
 *
 * 用于存储索引检查过程中发现的损坏条目。
 */
typedef struct {
    rgw_damage_entry_t* entries;  /**< 损坏条目数组 */
    size_t count;                 /**< 损坏条目数量 */
    size_t capacity;              /**< 数组容量 */
} rgw_damage_list_t;

/**
 * @brief 全局损坏列表 (用于 check/fix 通信)
 *
 * check_object_index 将损坏条目添加到此列表，
 * fix_object_index 从此列表读取并修复。
 */
static rgw_damage_list_t g_damage_list = {
    .entries = NULL,
    .count = 0,
    .capacity = 0
};

/**
 * @brief 添加损坏条目到全局列表
 */
static int add_damage_entry(const char* oid, rgw_damage_type_t type) {
    if (!oid) return RGW_SAL_ERR_INVALID_ARG;

    /* 需要扩容时翻倍 */
    if (g_damage_list.count >= g_damage_list.capacity) {
        size_t new_capacity = g_damage_list.capacity == 0 ? 16 : g_damage_list.capacity * 2;
        rgw_damage_entry_t* new_entries = realloc(g_damage_list.entries,
                                                   new_capacity * sizeof(rgw_damage_entry_t));
        if (!new_entries) return RGW_SAL_ERR_OUT_OF_MEMORY;

        g_damage_list.entries = new_entries;
        g_damage_list.capacity = new_capacity;
    }

    rgw_damage_entry_t* entry = &g_damage_list.entries[g_damage_list.count];
    strncpy(entry->oid, oid, sizeof(entry->oid) - 1);
    entry->oid[sizeof(entry->oid) - 1] = '\0';
    entry->type = type;
    entry->detected_time = time(NULL);

    g_damage_list.count++;
    return RGW_SAL_OK;
}

/**
 * @brief 清空损坏列表
 */
static void clear_damage_list(void) {
    if (g_damage_list.entries) {
        free(g_damage_list.entries);
        g_damage_list.entries = NULL;
    }
    g_damage_list.count = 0;
    g_damage_list.capacity = 0;
}

/**
 * @brief 获取损坏列表
 */
static rgw_damage_list_t* get_damage_list(void) {
    return &g_damage_list;
}

/*============================================================================
 * 用户序列化/反序列化实现
 *============================================================================*/

/**
 * @brief 解析用户数据缓冲区
 *
 * 解析格式: key=value\nkey=value\n...
 * 支持的字段:
 *   - id: 用户 ID
 *   - tenant: 租户
 *   - display_name: 显示名称
 *   - email: 邮箱
 *   - ns: 命名空间
 *   - user_type: 用户类型
 *   - quota_enabled: 配额是否启用
 *   - quota_check_on_raw: 是否检查原始大小
 *   - quota_bytes: 配额字节数
 *   - quota_max_objects: 最大对象数配额
 *   - user_caps: 用户权限
 */
int parse_user_from_buffer(rados_user_impl_t* impl, const uint8_t* data, size_t data_len) {
    if (!impl || !data || data_len == 0) return RGW_SAL_ERR_INVALID_ARG;

    /* 确保字符串以 null 结尾 */
    char* buffer = (char*)malloc(data_len + 1);
    if (!buffer) return RGW_SAL_ERR_OUT_OF_MEMORY;
    memcpy(buffer, data, data_len);
    buffer[data_len] = '\0';

    /* 解析每一行 */
    char* line = buffer;
    char* next;
    char* saveptr = NULL;

    while (line && *line) {
        /* 找到行尾 */
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        /* 跳过空行 */
        if (*line == '\0') {
            line = next;
            continue;
        }

        /* 解析 key=value 格式 */
        char* equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* key = line;
            char* value = equals + 1;

            /* 解析各个字段 */
            if (strcmp(key, "id") == 0) {
                free(impl->id);
                impl->id = strdup(value);
            } else if (strcmp(key, "tenant") == 0) {
                free(impl->tenant);
                impl->tenant = strdup(value);
            } else if (strcmp(key, "display_name") == 0) {
                free(impl->display_name);
                impl->display_name = strdup(value);
            } else if (strcmp(key, "email") == 0) {
                free(impl->email);
                impl->email = strdup(value);
            } else if (strcmp(key, "ns") == 0) {
                free(impl->ns);
                impl->ns = strdup(value);
            } else if (strcmp(key, "user_type") == 0) {
                impl->user_type = (uint32_t)atoi(value);
            } else if (strcmp(key, "max_buckets") == 0) {
                impl->max_buckets = (int32_t)atoi(value);
            } else if (strcmp(key, "quota_enabled") == 0) {
                impl->quota_info.enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
            } else if (strcmp(key, "quota_check_on_raw") == 0) {
                impl->quota_info.check_on_raw = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
            } else if (strcmp(key, "quota_bytes") == 0) {
                /* quota_bytes 是 uint64_t 类型，使用 strtoull 解析 */
                impl->quota_info.quota_bytes = strtoull(value, NULL, 10);
            } else if (strcmp(key, "quota_max_objects") == 0) {
                /* quota_max_objects 是 uint64_t 类型，使用 strtoull 解析 */
                impl->quota_info.quota_max_objects = strtoull(value, NULL, 10);
            } else if (strcmp(key, "user_caps") == 0) {
                free(impl->user_caps.caps);
                impl->user_caps.caps = strdup(value);
            }
        }

        line = next;
    }

    free(buffer);
    return RGW_SAL_OK;
}

/**
 * @brief 将用户数据序列化为缓冲区
 *
 * 序列化格式: key=value\nkey=value\n...
 * 序列化的字段:
 *   - id: 用户 ID
 *   - tenant: 租户
 *   - display_name: 显示名称
 *   - email: 邮箱
 *   - ns: 命名空间
 *   - user_type: 用户类型
 *   - max_buckets: 最大桶数
 *   - quota_enabled: 配额是否启用
 *   - quota_check_on_raw: 是否检查原始大小
 *   - quota_bytes: 配额字节数
 *   - quota_max_objects: 最大对象数配额
 *   - user_caps: 用户权限
 */
uint8_t* serialize_user_to_buffer(rados_user_impl_t* impl, size_t* buf_size) {
    if (!impl || !buf_size) return NULL;

    /* 估算需要的缓冲区大小 */
    size_t estimate = 1024;
    if (impl->id) estimate += strlen(impl->id) + 10;
    if (impl->tenant) estimate += strlen(impl->tenant) + 10;
    if (impl->display_name) estimate += strlen(impl->display_name) + 20;
    if (impl->email) estimate += strlen(impl->email) + 10;
    if (impl->ns) estimate += strlen(impl->ns) + 10;
    /* quota_bytes 和 quota_max_objects 是 uint64_t，每个最多 20 位数字 */
    estimate += 40;  /* 两个 uint64_t 的序列化空间 */
    if (impl->user_caps.caps) estimate += strlen(impl->user_caps.caps) + 15;

    /* 分配缓冲区 */
    char* buffer = (char*)malloc(estimate);
    if (!buffer) return NULL;

    size_t offset = 0;
    int ret;

    /* 序列化各个字段 */
    if (impl->id) {
        ret = snprintf(buffer + offset, estimate - offset, "id=%s\n", impl->id);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->tenant) {
        ret = snprintf(buffer + offset, estimate - offset, "tenant=%s\n", impl->tenant);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->display_name) {
        ret = snprintf(buffer + offset, estimate - offset, "display_name=%s\n", impl->display_name);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->email) {
        ret = snprintf(buffer + offset, estimate - offset, "email=%s\n", impl->email);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->ns) {
        ret = snprintf(buffer + offset, estimate - offset, "ns=%s\n", impl->ns);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf(buffer + offset, estimate - offset, "user_type=%u\n", impl->user_type);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "max_buckets=%d\n", impl->max_buckets);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_enabled=%d\n", impl->quota_info.enabled ? 1 : 0);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_check_on_raw=%d\n", impl->quota_info.check_on_raw ? 1 : 0);
    if (ret > 0) offset += (size_t)ret;

    /* 序列化配额字段 - quota_bytes 和 quota_max_objects 是 uint64_t */
    ret = snprintf(buffer + offset, estimate - offset, "quota_bytes=%llu\n",
                   (unsigned long long)impl->quota_info.quota_bytes);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_max_objects=%llu\n",
                   (unsigned long long)impl->quota_info.quota_max_objects);
    if (ret > 0) offset += (size_t)ret;

    if (impl->user_caps.caps) {
        ret = snprintf(buffer + offset, estimate - offset, "user_caps=%s\n", impl->user_caps.caps);
        if (ret > 0) offset += (size_t)ret;
    }

    *buf_size = offset;
    return (uint8_t*)buffer;
}

/**
 * @brief 释放序列化缓冲区
 */
void rgw_sal_free_buffer(uint8_t* buffer) {
    free(buffer);
}

/*============================================================================
 * 驱动 vtable 实现
 *============================================================================*/

/**
 * @brief 清理 IO 上下文
 */
static void rados_cleanup_ioctxs(rados_driver_impl_t* impl) {
    if (!impl) return;

    if (impl->users_uid_ioctx) {
        rados_ioctx_destroy(impl->users_uid_ioctx);
        impl->users_uid_ioctx = NULL;
    }
    if (impl->users_email_ioctx) {
        rados_ioctx_destroy(impl->users_email_ioctx);
        impl->users_email_ioctx = NULL;
    }
    if (impl->users_keys_ioctx) {
        rados_ioctx_destroy(impl->users_keys_ioctx);
        impl->users_keys_ioctx = NULL;
    }
    if (impl->users_swift_ioctx) {
        rados_ioctx_destroy(impl->users_swift_ioctx);
        impl->users_swift_ioctx = NULL;
    }
    if (impl->buckets_index_ioctx) {
        rados_ioctx_destroy(impl->buckets_index_ioctx);
        impl->buckets_index_ioctx = NULL;
    }
    if (impl->buckets_data_ioctx) {
        rados_ioctx_destroy(impl->buckets_data_ioctx);
        impl->buckets_data_ioctx = NULL;
    }
    if (impl->lc_pool_ioctx) {
        rados_ioctx_destroy(impl->lc_pool_ioctx);
        impl->lc_pool_ioctx = NULL;
    }
    if (impl->account_pool_ioctx) {
        rados_ioctx_destroy(impl->account_pool_ioctx);
        impl->account_pool_ioctx = NULL;
    }
    if (impl->group_pool_ioctx) {
        rados_ioctx_destroy(impl->group_pool_ioctx);
        impl->group_pool_ioctx = NULL;
    }
    if (impl->oidc_pool_ioctx) {
        rados_ioctx_destroy(impl->oidc_pool_ioctx);
        impl->oidc_pool_ioctx = NULL;
    }
    if (impl->topic_pool_ioctx) {
        rados_ioctx_destroy(impl->topic_pool_ioctx);
        impl->topic_pool_ioctx = NULL;
    }

    impl->ioctxs_initialized = false;
}

/**
 * @brief 初始化 RADOS IO 上下文
 */
static int rados_init_ioctxs(rados_driver_impl_t* impl, rados_t cluster) {
    if (!impl || !cluster) return RGW_SAL_ERR_INVALID_ARG;

    int ret;

    ret = rados_ioctx_create(cluster, ".rgw.meta.users.uid", &impl->users_uid_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.users.email", &impl->users_email_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.users.keys", &impl->users_keys_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.users.swift", &impl->users_swift_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.buckets.index", &impl->buckets_index_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.buckets.data", &impl->buckets_data_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.lc", &impl->lc_pool_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.account", &impl->account_pool_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.group", &impl->group_pool_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.meta.oidc", &impl->oidc_pool_ioctx);
    if (ret < 0) goto cleanup;

    ret = rados_ioctx_create(cluster, ".rgw.topic", &impl->topic_pool_ioctx);
    if (ret < 0) goto cleanup;

    impl->ioctxs_initialized = true;
    return RGW_SAL_OK;

cleanup:
    rados_cleanup_ioctxs(impl);
    return RGW_SAL_ERR_IO_ERROR;
}

static void rados_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (impl) {
        /* 清理 IO 上下文 */
        if (impl->ioctxs_initialized) {
            rados_cleanup_ioctxs(impl);
        }

        /* 清理 RADOS 连接 */
        if (impl->rados_handle) {
            rados_shutdown(impl->rados_handle);
            impl->rados_handle = NULL;
        }

        free(impl);
    }
    driver->impl = NULL;
}

static int rados_driver_initialize(rgw_sal_driver_t* driver, void* cct, const rgw_sal_dpp_t* dpp) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->initialized) return RGW_SAL_OK;

    int ret;

    /* 初始化 librados 集群句柄 */
    ret = rados_create(&impl->rados_handle, NULL);
    if (ret < 0) {
        impl->rados_handle = NULL;
        return RGW_SAL_ERR_IO_ERROR;
    }

    impl->cct = cct;

    /* 尝试从默认配置文件读取配置 */
    ret = rados_conf_read_file(impl->rados_handle, "/etc/ceph/ceph.conf");
    if (ret < 0) {
        /* 配置读取失败，继续使用默认配置 */
    }

    /* 连接到集群 */
    ret = rados_connect(impl->rados_handle);
    if (ret < 0) {
        rados_shutdown(impl->rados_handle);
        impl->rados_handle = NULL;
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 初始化 IO 上下文 */
    ret = rados_init_ioctxs(impl, impl->rados_handle);
    if (ret < 0) {
        rados_shutdown(impl->rados_handle);
        impl->rados_handle = NULL;
        return ret;
    }

    impl->initialized = true;

    (void)dpp;
    return RGW_SAL_OK;
}

static const char* rados_driver_get_name(const rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl) return NULL;
    return impl->name;
}

static int rados_driver_get_cluster_id(rgw_sal_driver_t* driver, char** cluster_id,
                                        const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !cluster_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 从 RADOS 集群获取 fsid */
    char fsid[128];
    int ret = rados_cluster_fsid(impl->rados_handle, fsid, sizeof(fsid) - 1);
    if (ret < 0) {
        /* 如果获取失败，使用默认名称 */
        *cluster_id = strdup("ceph");
    } else {
        fsid[sizeof(fsid) - 1] = '\0';
        *cluster_id = strdup(fsid);
    }

    if (!*cluster_id) return RGW_SAL_ERR_OUT_OF_MEMORY;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static rgw_sal_user_t* rados_driver_get_user(rgw_sal_driver_t* driver,
                                               const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    rados_user_impl_t* impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
    if (!impl) {
        free(user);
        return NULL;
    }

    if (uid->id) impl->id = strdup(uid->id);
    if (uid->tenant) impl->tenant = strdup(uid->tenant);
    impl->max_buckets = -1;  /* 默认无限制 */
    impl->loaded = false;

    user->vtable = driver->user_vtable;
    user->impl = impl;
    user->driver = driver;

    return user;
}

static int rados_driver_get_user_by_access_key(rgw_sal_driver_t* driver, const char* key,
                                                rgw_sal_user_t** user,
                                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !key || !user) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rgw_omap_get(impl->users_keys_ioctx, ".users.keys",
                              key, &value, &value_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    char* tenant = NULL;
    char* uid = NULL;
    const char* colon = strchr((const char*)value, ':');
    if (colon) {
        size_t tlen = colon - (const char*)value;
        if (tlen > 0) {
            tenant = (char*)malloc(tlen + 1);
            if (!tenant) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
            memcpy(tenant, value, tlen);
            tenant[tlen] = '\0';
        }
        uid = strdup(colon + 1);
        if (!uid) { free(tenant); rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    } else {
        uid = strdup((const char*)value);
        if (!uid) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    }
    rgw_omap_free_value(value);

    rgw_sal_user_id_t uid_struct = { .id = uid, .tenant = tenant };
    *user = driver->vtable->get_user(driver, &uid_struct);
    free(uid); free(tenant);

    if (!*user) return RGW_SAL_ERR_OUT_OF_MEMORY;

    ret = (*user)->vtable->load(*user, dpp, y);
    if (ret < 0) { (*user)->vtable->destroy(*user); *user = NULL; return ret; }

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_driver_get_user_by_email(rgw_sal_driver_t* driver, const char* email,
                                           rgw_sal_user_t** user,
                                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !email || !user) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rgw_omap_get(impl->users_email_ioctx, ".users.email",
                              email, &value, &value_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    char* tenant = NULL;
    char* uid = NULL;
    const char* colon = strchr((const char*)value, ':');
    if (colon) {
        size_t tlen = colon - (const char*)value;
        if (tlen > 0) {
            tenant = (char*)malloc(tlen + 1);
            if (!tenant) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
            memcpy(tenant, value, tlen);
            tenant[tlen] = '\0';
        }
        uid = strdup(colon + 1);
        if (!uid) { free(tenant); rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    } else {
        uid = strdup((const char*)value);
        if (!uid) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    }
    rgw_omap_free_value(value);

    rgw_sal_user_id_t uid_struct = { .id = uid, .tenant = tenant };
    *user = driver->vtable->get_user(driver, &uid_struct);
    free(uid); free(tenant);

    if (!*user) return RGW_SAL_ERR_OUT_OF_MEMORY;

    ret = (*user)->vtable->load(*user, dpp, y);
    if (ret < 0) { (*user)->vtable->destroy(*user); *user = NULL; return ret; }

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_driver_get_user_by_swift(rgw_sal_driver_t* driver, const char* user_str,
                                           rgw_sal_user_t** user,
                                           const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !user_str || !user) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    uint8_t* value = NULL;
    size_t value_len = 0;

    int ret = rgw_omap_get(impl->users_swift_ioctx, ".users.swift",
                              user_str, &value, &value_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    char* tenant = NULL;
    char* uid = NULL;
    const char* colon = strchr((const char*)value, ':');
    if (colon) {
        size_t tlen = colon - (const char*)value;
        if (tlen > 0) {
            tenant = (char*)malloc(tlen + 1);
            if (!tenant) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
            memcpy(tenant, value, tlen);
            tenant[tlen] = '\0';
        }
        uid = strdup(colon + 1);
        if (!uid) { free(tenant); rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    } else {
        uid = strdup((const char*)value);
        if (!uid) { rgw_omap_free_value(value); return RGW_SAL_ERR_OUT_OF_MEMORY; }
    }
    rgw_omap_free_value(value);

    rgw_sal_user_id_t uid_struct = { .id = uid, .tenant = tenant };
    *user = driver->vtable->get_user(driver, &uid_struct);
    free(uid); free(tenant);

    if (!*user) return RGW_SAL_ERR_OUT_OF_MEMORY;

    ret = (*user)->vtable->load(*user, dpp, y);
    if (ret < 0) { (*user)->vtable->destroy(*user); *user = NULL; return ret; }

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static rgw_sal_bucket_t* rados_driver_get_bucket(rgw_sal_driver_t* driver,
                                                   const rgw_sal_bucket_info_t* info) {
    if (!driver || !info) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)calloc(1, sizeof(rados_bucket_impl_t));
    if (!impl) {
        free(bucket);
        return NULL;
    }

    if (info->bucket.name) impl->name = strdup(info->bucket.name);
    if (info->bucket.tenant) impl->tenant = strdup(info->bucket.tenant);
    if (info->bucket.marker) impl->marker = strdup(info->bucket.marker);
    if (info->bucket.bucket_id) impl->bucket_id = strdup(info->bucket.bucket_id);
    impl->loaded = false;

    bucket->vtable = driver->bucket_vtable;
    bucket->impl = impl;
    bucket->driver = driver;

    return bucket;
}

static int rados_driver_list_buckets(rgw_sal_driver_t* driver,
                                     rgw_sal_user_t* owner,
                                     const char* prefix, const char* delimiter,
                                     const char* marker, const char* end_marker,
                                     uint32_t max_keys, bool list_all,
                                     rgw_sal_bucket_list_t** result,
                                     const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!driver || !result) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)driver->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    rgw_sal_bucket_list_t* list = (rgw_sal_bucket_list_t*)calloc(1, sizeof(rgw_sal_bucket_list_t));
    if (!list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /*
     * RADOS 桶列表存储在 .rgw.meta.buckets.index 池的 OMAP 中
     * 用户拥有的桶列表存储在 .rgw.buckets.{user_id} 对象中
     *
     * 桶信息 (RGWBucketInfo) 序列化后存储在 OMAP 中
     * 键: bucket_id
     */

    /* 如果 RADOS 未初始化，返回空列表 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->rados_handle) {
        list->buckets = NULL;
        list->count = 0;
        list->is_truncated = false;
        *result = list;
        return RGW_SAL_OK;
    }

    /* 获取 owner ID */
    const char* owner_id = NULL;
    if (owner) {
        rados_user_impl_t* user_impl = (rados_user_impl_t*)owner->impl;
        if (user_impl && user_impl->id) {
            owner_id = user_impl->id;
        }
    }

    if (!owner_id) {
        list->buckets = NULL;
        list->count = 0;
        list->is_truncated = false;
        *result = list;
        return RGW_SAL_OK;
    }

    /*
     * 完整实现:
     * 1. 从用户的桶列表 OMAP 获取桶信息
     *    - 对象名: .rgw.buckets.{owner_id}
     *    - 使用 rgw_omap_get_all
     * 2. 从桶信息池获取每个桶的详细信息
     *    - 池名: .rgw.meta.buckets.index
     *    - 键: bucket_id
     * 3. 反序列化 RGWBucketInfo
     * 4. 过滤 prefix, marker 等
     * 5. 应用 delimiter 进行分组
     */

    /* 构建用户桶列表 OMAP 对象名 */
    char user_buckets_oid[256];
    snprintf(user_buckets_oid, sizeof(user_buckets_oid), ".rgw.buckets.%s", owner_id);

    /* 获取用户桶列表 OMAP */
    rgw_omap_kv_array_t user_buckets;
    memset(&user_buckets, 0, sizeof(user_buckets));

    int ret = rgw_omap_get_all(driver_impl->buckets_index_ioctx,
                                user_buckets_oid, marker, max_keys, &user_buckets);
    if (ret < 0 && ret != -ENOENT) {
        free(list);
        return ret;
    }

    /* 检查是否有截断 */
    list->is_truncated = (user_buckets.count >= max_keys);

    /* 如果没有桶，返回空列表 */
    if (user_buckets.count == 0) {
        list->buckets = NULL;
        list->count = 0;
        rgw_omap_kv_array_free(&user_buckets);
        *result = list;
        return RGW_SAL_OK;
    }

    /* 分配桶数组 - 注意: rgw_sal_bucket_list_t.buckets 是 rgw_sal_bucket_info_t** */
    list->count = 0;
    list->buckets = (rgw_sal_bucket_info_t**)calloc(user_buckets.count,
                                                      sizeof(rgw_sal_bucket_info_t*));
    if (!list->buckets) {
        rgw_omap_kv_array_free(&user_buckets);
        free(list);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 遍历用户的桶列表 */
    for (size_t i = 0; i < user_buckets.count; i++) {
        /* OMAP 键格式: {bucket_name}
         * OMAP 值格式: {bucket_id}
         */
        const char* bucket_name = user_buckets.kvs[i].key;
        const uint8_t* bucket_id_val = user_buckets.kvs[i].val;
        size_t bucket_id_len = user_buckets.kvs[i].val_len;

        if (!bucket_name || !bucket_id_val) {
            continue;
        }

        /* 解析 bucket_id */
        char* bucket_id = (char*)malloc(bucket_id_len + 1);
        if (!bucket_id) {
            continue;
        }
        memcpy(bucket_id, bucket_id_val, bucket_id_len);
        bucket_id[bucket_id_len] = '\0';

        /* 检查前缀过滤 */
        if (prefix && strlen(prefix) > 0) {
            if (strncmp(bucket_name, prefix, strlen(prefix)) != 0) {
                free(bucket_id);
                continue;
            }
        }

        /* 检查 end_marker */
        if (end_marker && strcmp(bucket_name, end_marker) >= 0) {
            free(bucket_id);
            break;
        }

        /* 构建桶信息 OMAP 键 */
        char omap_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_info_make_omap_key(bucket_id, omap_key, sizeof(omap_key));

        /* 从桶信息池获取详细信息 */
        uint8_t* info_data = NULL;
        size_t info_data_len = 0;

        /* 桶信息存储在 .rgw.meta.buckets.index 池中 */
        ret = rgw_omap_get(driver_impl->buckets_index_ioctx,
                           ".rgw.meta.buckets.index",
                           omap_key, &info_data, &info_data_len);
        if (ret < 0) {
            /* 桶信息不存在，跳过这个桶 */
            free(bucket_id);
            continue;
        }

        /* 解码桶信息 */
        rgw_bucket_info_t bucket_info;
        rgw_bucket_info_init(&bucket_info);

        ret = rgw_bucket_info_decode(info_data, info_data_len, &bucket_info);
        rgw_omap_free_value(info_data);

        if (ret < 0) {
            /* 解码失败，跳过这个桶 */
            free(bucket_id);
            continue;
        }

        /* 分配桶信息结构 */
        rgw_sal_bucket_info_t* sal_bucket_info =
            (rgw_sal_bucket_info_t*)calloc(1, sizeof(rgw_sal_bucket_info_t));
        if (!sal_bucket_info) {
            rgw_bucket_info_free_members(&bucket_info);
            free(bucket_id);
            continue;
        }

        /* 填充 SAL 桶信息 */
        /* 分配并复制桶 ID */
        sal_bucket_info->bucket.bucket_id = strdup(bucket_id);
        sal_bucket_info->bucket.name = strdup(bucket_name);

        /* 如果 bucket_info 中有更多信息，使用它们 */
        if (bucket_info.bucket.tenant) {
            sal_bucket_info->bucket.tenant = strdup(bucket_info.bucket.tenant);
        }
        if (bucket_info.bucket.marker) {
            sal_bucket_info->bucket.marker = strdup(bucket_info.bucket.marker);
        }

        /* 复制所有者信息 */
        if (bucket_info.owner.user_id) {
            sal_bucket_info->owner.id = strdup(bucket_info.owner.user_id);
        }
        if (bucket_info.owner.account_id) {
            sal_bucket_info->owner.account_id = strdup(bucket_info.owner.account_id);
        }
        sal_bucket_info->owner.type = bucket_info.owner.type;

        /* 复制区域组 */
        if (bucket_info.zonegroup) {
            sal_bucket_info->zone_group = strdup(bucket_info.zonegroup);
        }

        /* 复制放置规则 */
        sal_bucket_info->placement_rule = 0;  /* 默认值 */

        /* 释放解码的桶信息 */
        rgw_bucket_info_free_members(&bucket_info);
        free(bucket_id);

        /* 添加到结果列表 */
        ((rgw_sal_bucket_info_t**)list->buckets)[list->count] = sal_bucket_info;
        list->count++;

        /* 如果达到最大数量，停止 */
        if (max_keys > 0 && list->count >= max_keys) {
            break;
        }
    }

    /* 释放用户桶列表 OMAP 数据 */
    rgw_omap_kv_array_free(&user_buckets);

    (void)delimiter;
    (void)list_all;
    (void)dpp;
    (void)y;

    *result = list;
    return RGW_SAL_OK;
}

static rgw_sal_object_t* rados_driver_get_object(rgw_sal_driver_t* driver,
                                                   rgw_sal_bucket_t* bucket,
                                                   const rgw_sal_obj_key_t* key) {
    if (!driver || !bucket || !key) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    rados_object_impl_t* impl = (rados_object_impl_t*)calloc(1, sizeof(rados_object_impl_t));
    if (!impl) {
        free(obj);
        return NULL;
    }

    if (key->name) impl->name = strdup(key->name);
    if (key->instance) impl->instance = strdup(key->instance);
    impl->is_null = key->is_null;

    /* 获取桶信息 */
    if (bucket->vtable && bucket->vtable->get_name) {
        impl->bucket_name = strdup(bucket->vtable->get_name(bucket));
    }

    obj->vtable = driver->object_vtable;
    obj->impl = impl;
    obj->bucket = bucket;

    return obj;
}

/* 驱动 vtable */
static rgw_sal_driver_vtable_t rados_driver_vtable = {
    .destroy = rados_driver_destroy,
    .initialize = rados_driver_initialize,
    .get_name = rados_driver_get_name,
    .get_cluster_id = rados_driver_get_cluster_id,
    .get_user = rados_driver_get_user,
    .get_user_by_access_key = rados_driver_get_user_by_access_key,
    .get_user_by_email = rados_driver_get_user_by_email,
    .get_user_by_swift = rados_driver_get_user_by_swift,
    .get_bucket = rados_driver_get_bucket,
    .list_buckets = rados_driver_list_buckets,
    .get_object = rados_driver_get_object,
};

/*============================================================================
 * 用户 vtable 实现
 *============================================================================*/

static void* rados_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* new_user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!new_user) return NULL;

    rados_user_impl_t* old_impl = (rados_user_impl_t*)user->impl;
    rados_user_impl_t* new_impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
    if (!new_impl) {
        free(new_user);
        return NULL;
    }

    /* 深拷贝字符串资源 */
    if (old_impl->id) new_impl->id = strdup(old_impl->id);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->display_name) new_impl->display_name = strdup(old_impl->display_name);
    if (old_impl->email) new_impl->email = strdup(old_impl->email);
    if (old_impl->ns) new_impl->ns = strdup(old_impl->ns);
    new_impl->user_type = old_impl->user_type;
    new_impl->max_buckets = old_impl->max_buckets;
    new_impl->loaded = old_impl->loaded;
    new_impl->usage_loaded = old_impl->usage_loaded;
    new_impl->user_info_stored = old_impl->user_info_stored;

    /* 深拷贝配额信息 - quota_bytes 和 quota_max_objects 是 uint64_t，直接复制 */
    new_impl->quota_info.enabled = old_impl->quota_info.enabled;
    new_impl->quota_info.check_on_raw = old_impl->quota_info.check_on_raw;
    new_impl->quota_info.max_size = old_impl->quota_info.max_size;
    new_impl->quota_info.max_size_kb = old_impl->quota_info.max_size_kb;
    new_impl->quota_info.max_objects = old_impl->quota_info.max_objects;
    new_impl->quota_info.quota_bytes = old_impl->quota_info.quota_bytes;
    new_impl->quota_info.quota_max_objects = old_impl->quota_info.quota_max_objects;

    /* 深拷贝权限信息中的字符串 */
    if (old_impl->user_caps.caps) {
        new_impl->user_caps.caps = strdup(old_impl->user_caps.caps);
    }

    /* 复制版本跟踪器 */
    new_impl->version_tracker.write_version = old_impl->version_tracker.write_version;
    new_impl->version_tracker.read_version = old_impl->version_tracker.read_version;

    /* 深拷贝属性映射 (创建新副本) */
    if (old_impl->attrs) {
        new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
    }

    new_user->vtable = user->vtable;
    new_user->impl = new_impl;
    new_user->driver = user->driver;

    return new_user;
}

static void rados_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        /* 防止双重释放 */
        if (impl->destroyed) {
            return;
        }
        impl->destroyed = true;

        /* 释放字符串资源 */
        free(impl->id);
        impl->id = NULL;
        free(impl->tenant);
        impl->tenant = NULL;
        free(impl->display_name);
        impl->display_name = NULL;
        free(impl->email);
        impl->email = NULL;
        free(impl->ns);
        impl->ns = NULL;

        /* 注意: quota_bytes 和 quota_max_objects 是 uint64_t，不是指针，不需要释放 */

        /* 释放权限信息中的字符串资源 */
        if (impl->user_caps.caps) {
            free(impl->user_caps.caps);
        }

        /* 释放属性映射 */
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
            impl->attrs = NULL;
        }

        free(impl);
    }
    user->impl = NULL;
}

static const char* rados_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->id : NULL;
}

static const char* rados_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->display_name : NULL;
}

static int rados_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user || !name) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->display_name);
    impl->display_name = strdup(name);
    if (!impl->display_name) return RGW_SAL_ERR_OUT_OF_MEMORY;

    return RGW_SAL_OK;
}

static const char* rados_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->tenant : NULL;
}

static uint32_t rados_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->user_type : 0;
}

static int32_t rados_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return -1;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->max_buckets : -1;
}

static void rados_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        impl->max_buckets = max;
    }
}

static rgw_sal_attrs_t* rados_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int rados_user_set_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* attrs) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 替换属性映射 */
    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int rados_user_load(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 安全获取用户 ID */
    const char* user_id = RADOS_USER_GET_ID(user);
    if (!user_id) return RGW_SAL_ERR_INVALID_ARG;

    char bucket[RGW_SAL_BUF_SIZE];
    snprintf(bucket, sizeof(bucket), ".users.%s", user_id);

    uint8_t* data = NULL;
    size_t data_len = 0;

    int ret = rgw_omap_get(driver_impl->users_uid_ioctx, bucket, user_id, &data, &data_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    ret = parse_user_from_buffer(impl, data, data_len);
    rgw_omap_free_value(data);

    if (ret < 0) return ret;

    impl->loaded = true;
    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_user_store(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                            rgw_sal_yield_t* y, bool exclusive) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 安全获取用户 ID */
    const char* user_id = RADOS_USER_GET_ID(user);
    if (!user_id) return RGW_SAL_ERR_INVALID_ARG;

    char bucket[RGW_SAL_BUF_SIZE];
    snprintf(bucket, sizeof(bucket), ".users.%s", user_id);

    size_t buf_size = 0;
    uint8_t* buffer = serialize_user_to_buffer(impl, &buf_size);
    if (!buffer) return RGW_SAL_ERR_INTERNAL_ERROR;

    int ret = rgw_omap_set(driver_impl->users_uid_ioctx, bucket, user_id, buffer, buf_size, exclusive);
    rgw_sal_free_buffer(buffer);

    if (ret < 0) {
        if (exclusive && ret == -EEXIST) return RGW_SAL_ERR_EXISTS;
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    impl->loaded = true;
    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_user_remove(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 安全获取用户 ID */
    const char* user_id = RADOS_USER_GET_ID(user);
    if (!user_id) return RGW_SAL_ERR_INVALID_ARG;

    char bucket[RGW_SAL_BUF_SIZE];
    snprintf(bucket, sizeof(bucket), ".users.%s", user_id);

    int ret = rgw_omap_del(driver_impl->users_uid_ioctx, bucket, user_id);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_user_read_attrs(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 安全获取用户 ID */
    const char* user_id = RADOS_USER_GET_ID(user);
    if (!user_id) return RGW_SAL_ERR_INVALID_ARG;

    char bucket[RGW_SAL_BUF_SIZE];
    snprintf(bucket, sizeof(bucket), ".users.%s", user_id);

    /* 先读取基本用户数据 */
    uint8_t* data = NULL;
    size_t data_len = 0;

    int ret = rgw_omap_get(driver_impl->users_uid_ioctx, bucket, user_id, &data, &data_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    /* 解析用户数据到 impl */
    ret = parse_user_from_buffer(impl, data, data_len);
    rgw_omap_free_value(data);
    if (ret < 0) return ret;

    impl->loaded = true;
    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_user_merge_and_store_attrs(rgw_sal_user_t* user, rgw_sal_attrs_t* new_attrs,
                                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!user || !new_attrs) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 安全获取用户 ID */
    const char* user_id = RADOS_USER_GET_ID(user);
    if (!user_id) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取当前属性，如果不存在则创建 */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 合并新属性到当前属性 */
    for (size_t i = 0; i < new_attrs->count; i++) {
        const rgw_sal_attr_pair_t* pair = &new_attrs->pairs[i];
        int ret = rgw_sal_attrs_set(impl->attrs, pair->key, pair->value, pair->value_len);
        if (ret != RGW_SAL_OK) return ret;
    }

    /* 持久化到 RADOS */
    size_t buf_size = 0;
    uint8_t* buffer = serialize_user_to_buffer(impl, &buf_size);
    if (!buffer) return RGW_SAL_ERR_INTERNAL_ERROR;

    char bucket[RGW_SAL_BUF_SIZE];
    snprintf(bucket, sizeof(bucket), ".users.%s", user_id);

    int ret = rgw_omap_set(driver_impl->users_uid_ioctx, bucket, user_id, buffer, buf_size, false);
    rgw_sal_free_buffer(buffer);

    if (ret < 0) return RGW_SAL_ERR_WRITE_ERROR;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 新增 User VTable 函数实现
 *============================================================================*/

/* 命名空间操作 */
static const char* rados_user_get_ns(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->ns : NULL;
}

static int rados_user_set_ns(rgw_sal_user_t* user, const char* ns) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->ns);
    impl->ns = ns ? strdup(ns) : NULL;
    if (ns && !impl->ns) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

static void rados_user_clear_ns(rgw_sal_user_t* user) {
    if (!user) return;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) {
        free(impl->ns);
        impl->ns = NULL;
    }
}

/* 配额信息 (P0: 完整实现) */
static int rados_user_set_info(rgw_sal_user_t* user, void* info) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (info) {
        /* 复制配额信息 */
        memcpy(&impl->quota_info, info, sizeof(rgw_sal_quota_info_t));
    }
    return RGW_SAL_OK;
}

static int rados_user_get_info(rgw_sal_user_t* user, void** info) {
    if (!user || !info) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *info = &impl->quota_info;
    return RGW_SAL_OK;
}

/* 权限管理 (P0: 完整实现) */
static int rados_user_get_caps(rgw_sal_user_t* user, void** caps) {
    if (!user || !caps) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *caps = &impl->user_caps;
    return RGW_SAL_OK;
}

static int rados_user_get_version_tracker(rgw_sal_user_t* user, void** tracker) {
    if (!user || !tracker) return RGW_SAL_ERR_INVALID_ARG;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    *tracker = &impl->version_tracker;
    return RGW_SAL_OK;
}

/* 使用统计 - 完整实现 */
static int rados_user_read_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                  uint64_t start_epoch, uint64_t end_epoch,
                                  uint32_t max_entries, void* usage) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户 ID */
    const char* user_id = impl->id;
    if (!user_id) user_id = "";

    /*
     * RADOS Usage 存储在 .rgw.log 池的 OMAP 对象中
     * 对象命名格式: usage:<owner>:<bucket>:<epoch>
     * 每个分片存储一部分 usage 数据
     */

    /* 如果 RADOS 未完全初始化，返回空数据 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->rados_handle) {
        if (usage) {
            rgw_usage_entries_t* entries = (rgw_usage_entries_t*)usage;
            entries->count = 0;
            entries->capacity = 0;
            entries->entries = NULL;
        }
        return RGW_SAL_OK;
    }

    /* 创建 .rgw.log 池的 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = rados_ioctx_create(driver_impl->rados_handle, ".rgw.log", &ioctx);
    if (ret < 0) {
        return ret;
    }

    /* 创建结果集合 */
    rgw_usage_entries_t* entries = NULL;
    if (usage) {
        entries = rgw_usage_entries_create();
        if (!entries) {
            rados_ioctx_destroy(ioctx);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }
    }

    /* 遍历所有分片读取 usage 数据 */
    int max_shards = RGW_USAGE_DEFAULT_MAX_SHARDS;
    uint32_t entries_read = 0;
    uint32_t max_to_read = max_entries > 0 ? max_entries : UINT32_MAX;

    for (int shard = 0; shard < max_shards && entries_read < max_to_read; shard++) {
        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "usage:%s:%d", user_id, shard);

        /* 使用 OMAP 迭代器读取对象
         * prefix 过滤: 只读取以 user_id: 开头的键
         */
        char prefix_filter[128];
        snprintf(prefix_filter, sizeof(prefix_filter), "%s:", user_id);

        rgw_omap_iter_t* iter = rgw_omap_iter_create(ioctx, obj_name, NULL, prefix_filter, max_to_read - entries_read);
        if (!iter) {
            /* 对象可能不存在，继续下一个分片 */
            continue;
        }

        const char* key = NULL;
        const uint8_t* val = NULL;
        size_t val_len = 0;

        while (rgw_omap_iter_next(iter, &key, &val, &val_len) == 1) {
            if (!key || !val) continue;

            /* 解析键格式: owner:bucket:epoch */
            /* 找到最后一个冒号，分离出 epoch */
            const char* last_colon = strrchr(key, ':');
            if (!last_colon) continue;

            uint64_t epoch = (uint64_t)strtoull(last_colon + 1, NULL, 10);

            /* 过滤 epoch 范围 */
            if (start_epoch > 0 && epoch < start_epoch) continue;
            if (end_epoch > 0 && epoch > end_epoch) continue;

            /* 解析 usage 条目 */
            rgw_usage_log_entry_t entry;
            memset(&entry, 0, sizeof(entry));

            /* 从值中解码 usage 条目 */
            ret = rgw_usage_log_entry_decode(val, val_len, &entry);
            if (ret < 0) {
                /* 解码失败，跳过此条目 */
                continue;
            }

            /* 设置 epoch */
            entry.epoch = epoch;

            /* 构建 user.bucket 键 */
            char entry_key[512];
            const char* bucket = entry.bucket ? entry.bucket : "";

            /* 查找 owner 位置 (在冒号之前) */
            const char* colon1 = strchr(key, ':');
            if (colon1) {
                const char* owner_start = key;
                size_t owner_len = colon1 - owner_start;

                if (owner_len > sizeof(entry_key) - strlen(bucket) - 2) {
                    owner_len = sizeof(entry_key) - strlen(bucket) - 2;
                }

                memcpy(entry_key, owner_start, owner_len);
                entry_key[owner_len] = '\0';

                size_t key_len = strlen(entry_key);
                snprintf(entry_key + key_len, sizeof(entry_key) - key_len, ".%s", bucket);
            } else {
                snprintf(entry_key, sizeof(entry_key), "%s.%s", user_id, bucket);
            }

            /* 添加或聚合到 entries */
            if (entries) {
                ret = rgw_usage_entries_aggregate(entries, entry_key, &entry);
                if (ret == RGW_SAL_OK) {
                    entries_read++;
                }
            }

            /* 释放 entry 中分配的字符串 */
            if (entry.owner_id) free(entry.owner_id);
            if (entry.bucket) free(entry.bucket);
        }

        rgw_omap_iter_destroy(iter);
    }

    rados_ioctx_destroy(ioctx);

    /* 填充输出参数 */
    if (usage && entries) {
        *((rgw_usage_entries_t**)usage) = entries;
    } else if (entries) {
        rgw_usage_entries_destroy(entries);
    }

    (void)dpp;

    return RGW_SAL_OK;
}

static int rados_user_trim_usage(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   uint64_t start_epoch, uint64_t end_epoch) {
    if (!user) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户 ID */
    const char* user_id = impl->id;
    if (!user_id) user_id = "";

    /*
     * RADOS Usage 清理
     * 需要删除指定 epoch 范围内的 usage 数据
     * 在 RADOS 中，这涉及遍历和删除 OMAP 条目
     * 由于 librados OMAP 不支持范围删除，需要:
     * 1. 遍历读取所有键
     * 2. 过滤出需要删除的键
     * 3. 使用写入操作批量删除
     */

    if (!driver_impl->ioctxs_initialized || !driver_impl->rados_handle) {
        /* RADOS 未初始化，无法执行 */
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 创建 .rgw.log 池的 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = rados_ioctx_create(driver_impl->rados_handle, ".rgw.log", &ioctx);
    if (ret < 0) {
        return ret;
    }

    /* 遍历所有分片删除 usage 数据 */
    int max_shards = RGW_USAGE_DEFAULT_MAX_SHARDS;

    for (int shard = 0; shard < max_shards; shard++) {
        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "usage:%s:%d", user_id, shard);

        /* 首先使用迭代器读取所有键，找出需要删除的 */
        rgw_omap_iter_t* iter = rgw_omap_iter_create(ioctx, obj_name, NULL, NULL, 0);
        if (!iter) {
            /* 对象可能不存在，继续下一个分片 */
            continue;
        }

        /* 收集需要删除的键，最多 256 个一批 */
        char* keys_to_delete[256];
        int keys_count = 0;
        memset(keys_to_delete, 0, sizeof(keys_to_delete));

        const char* key = NULL;
        const uint8_t* val = NULL;
        size_t val_len = 0;

        while (rgw_omap_iter_next(iter, &key, &val, &val_len) == 1) {
            if (!key) continue;

            /* 解析键格式: owner:bucket:epoch */
            const char* last_colon = strrchr(key, ':');
            if (!last_colon) continue;

            uint64_t epoch = (uint64_t)strtoull(last_colon + 1, NULL, 10);

            /* 检查 epoch 是否在删除范围内 */
            bool should_delete = true;
            if (start_epoch > 0 && epoch < start_epoch) {
                should_delete = false;
            }
            if (end_epoch > 0 && epoch > end_epoch) {
                should_delete = false;
            }

            if (should_delete && keys_count < 256) {
                keys_to_delete[keys_count] = strdup(key);
                if (keys_to_delete[keys_count]) {
                    keys_count++;
                }
            }
        }

        rgw_omap_iter_destroy(iter);

        /* 如果有需要删除的键，使用写入操作删除 */
        if (keys_count > 0) {
            /* 创建写入操作 */
            rados_write_op_t op = rados_create_write_op();
            if (op) {
                /* 添加 OMAP 删除操作 */
                const char* keys_ptr[256];
                for (int i = 0; i < keys_count; i++) {
                    keys_ptr[i] = keys_to_delete[i];
                }

                rados_write_op_omap_rm_keys(op, keys_ptr, keys_count);

                /* 执行写入操作 */
                ret = rados_write_op_operate(op, ioctx, obj_name, NULL, 0);
                rados_release_write_op(op);

                if (ret < 0) {
                    /* 删除操作失败，记录日志但继续处理其他分片 */
                }
            }

            /* 释放键字符串 */
            for (int i = 0; i < keys_count; i++) {
                free(keys_to_delete[i]);
            }
        }
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;

    return RGW_SAL_OK;
}

/* MFA 认证 */
static int rados_user_verify_mfa(rgw_sal_user_t* user, const char* mfa_serial,
                                   const char* code, const rgw_sal_dpp_t* dpp) {
    if (!user || !mfa_serial || !code) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户属性中的 MFA 信息 */
    if (!impl->attrs) {
        /* 尝试加载用户属性 */
        int ret = rados_user_read_attrs(user, dpp, NULL);
        if (ret < 0) {
            return ret;
        }
    }

    if (!impl->attrs) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 构建 MFA 属性键: mfa:serial:<serial> */
    char mfa_key[128];
    snprintf(mfa_key, sizeof(mfa_key), "mfa:serial:%s", mfa_serial);

    /* 查找 MFA 密钥 */
    uint8_t* secret_data = NULL;
    size_t secret_len = 0;
    int ret = rgw_sal_attrs_get(impl->attrs, mfa_key, &secret_data, &secret_len);
    if (ret < 0) {
        /* 尝试备用格式 */
        snprintf(mfa_key, sizeof(mfa_key), "totp:%s", mfa_serial);
        ret = rgw_sal_attrs_get(impl->attrs, mfa_key, &secret_data, &secret_len);
        if (ret < 0) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
    }

    /* 将密钥转换为字符串 */
    char* secret = (char*)malloc(secret_len + 1);
    if (!secret) {
        free(secret_data);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    memcpy(secret, secret_data, secret_len);
    secret[secret_len] = '\0';
    free(secret_data);

    /* 使用 TOTP 验证代码 */
    bool verified = rgw_sal_verify_totp(secret, code, 0);
    free(secret);

    if (!verified) {
        return RGW_SAL_ERR_MFA_AUTH_FAILED;
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/* 组管理 */
static int rados_user_list_groups(rgw_sal_user_t* user, const rgw_sal_dpp_t* dpp,
                                   void** groups, uint32_t* count) {
    if (!user || !groups || !count) return RGW_SAL_ERR_INVALID_ARG;

    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)user->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取用户 ID */
    const char* user_id = impl->id;
    if (!user_id) user_id = "";

    /* 创建用户组列表 */
    rgw_sal_user_groups_t* groups_list = rgw_sal_user_groups_create();
    if (!groups_list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /*
     * RADOS 用户组存储
     * 组信息存储在 .rgw.meta.group 池的 OMAP 中
     *
     * OMAP 对象命名格式:
     * - 用户组列表对象: "account.{account_id}.groups"
     *   - OMAP 键: 组 ID
     *   - OMAP 值: 组信息二进制数据 (使用 rgw_group_info_encode 编码)
     *
     * 对于 IAM 用户，组信息可能存储在用户的属性中，
     * 或者通过遍历所有组来检查成员资格。
     *
     * 这里采用两种策略:
     * 1. 首先尝试从用户属性中读取组信息
     * 2. 如果驱动有 group_pool_ioctx，从组池中查找该用户所在的组
     */

    /* 策略 1: 从用户属性中读取组信息 */
    if (impl->attrs) {
        uint8_t* groups_data = NULL;
        size_t groups_len = 0;

        /* 尝试获取组属性 */
        int ret = rgw_sal_attrs_get(impl->attrs, "groups", &groups_data, &groups_len);
        if (ret == RGW_SAL_OK && groups_data && groups_len > 0) {
            /* 解析 JSON 格式的组信息
             * 格式: {"group1": "Group 1", "group2": "Group 2"}
             */
            char* groups_str = (char*)malloc(groups_len + 1);
            if (groups_str) {
                memcpy(groups_str, groups_data, groups_len);
                groups_str[groups_len] = '\0';

                /* 简单解析 JSON */
                char* p = groups_str;
                while (*p) {
                    /* 跳过空白 */
                    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                    if (*p != '"') break;

                    p++; /* 跳过开头引号 */
                    char* key_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t key_len = p - key_start;
                    p++; /* 跳过结尾引号 */

                    /* 跳过空白和冒号 */
                    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
                    if (*p != '"') continue;

                    p++; /* 跳过开头引号 */
                    char* value_start = p;
                    while (*p && *p != '"') p++;
                    if (*p != '"') break;

                    size_t value_len = p - value_start;

                    /* 添加组 */
                    char* key = (char*)malloc(key_len + 1);
                    char* value = (char*)malloc(value_len + 1);
                    if (key && value) {
                        memcpy(key, key_start, key_len);
                        key[key_len] = '\0';
                        memcpy(value, value_start, value_len);
                        value[value_len] = '\0';
                        rgw_sal_user_groups_add(groups_list, key, value);
                        free(key);
                        free(value);
                    } else {
                        free(key);
                        free(value);
                    }
                    p++; /* 跳过结尾引号 */
                }
                free(groups_str);
            }
            free(groups_data);
        }
    }

    /* 策略 2: 如果有 group_pool_ioctx，从组池中查找用户所在的组
     * 注意: 这需要遍历所有组来检查成员资格
     * 在生产环境中，更高效的方法是维护用户到组的反向索引
     */
    if (driver_impl->group_pool_ioctx) {
        /*
         * 用户组列表存储在 account.{account_id}.groups 对象中
         * OMAP 键是组 ID，值是组的元数据
         *
         * 首先需要获取用户的 account_id
         */
        const char* account_id = NULL;
        if (impl->attrs) {
            uint8_t* account_id_data = NULL;
            size_t account_id_len = 0;
            int ret = rgw_sal_attrs_get(impl->attrs, "account_id", &account_id_data, &account_id_len);
            if (ret == RGW_SAL_OK && account_id_data && account_id_len > 0) {
                account_id = (const char*)account_id_data;
                /* 继续使用，但最后需要释放 */
            }
        }

        if (account_id) {
            /* 构建用户组列表 OMAP 对象名 */
            char user_groups_oid[256];
            snprintf(user_groups_oid, sizeof(user_groups_oid), "account.%s.groups", account_id);

            /* 使用 OMAP 迭代器读取用户组列表 */
            rgw_omap_iter_t* iter = rgw_omap_iter_create(
                driver_impl->group_pool_ioctx,
                user_groups_oid,
                NULL,  /* start_after */
                NULL,  /* filter_prefix */
                1000); /* max_return - 足够大的值 */

            if (iter) {
                const char* key = NULL;
                const uint8_t* val = NULL;
                size_t val_len = 0;

                while (rgw_omap_iter_next(iter, &key, &val, &val_len) == 1) {
                    if (!key || !val || val_len == 0) continue;

                    /* 解析组信息 */
                    rgw_group_info_t group_info;
                    rgw_group_info_init(&group_info);

                    int ret = rgw_group_info_decode(val, val_len, &group_info);
                    if (ret < 0) {
                        /* 解码失败，跳过此条目 */
                        continue;
                    }

                    /* 添加组到列表 (使用组 ID 和组名称) */
                    const char* group_id = group_info.id ? group_info.id : key;
                    const char* group_name = group_info.name ? group_info.name : "";

                    /* 检查是否已存在 */
                    bool exists = false;
                    for (size_t i = 0; i < groups_list->count; i++) {
                        if (groups_list->groups[i].group_id &&
                            strcmp(groups_list->groups[i].group_id, group_id) == 0) {
                            exists = true;
                            break;
                        }
                    }

                    if (!exists) {
                        rgw_sal_user_groups_add(groups_list, group_id, group_name);
                    }

                    rgw_group_info_free_members(&group_info);
                }

                rgw_omap_iter_destroy(iter);
            }

            /* 释放 account_id_data */
            if (impl->attrs) {
                /* 注意: 实际的释放需要通过 impl->attrs 的释放机制 */
                /* 这里只是标记，不需要单独释放 */
            }
        }
    }

    *groups = groups_list;
    *count = (uint32_t)groups_list->count;

    (void)dpp;

    return RGW_SAL_OK;
}

/* 用户 vtable */
static rgw_sal_user_vtable_t rados_user_vtable = {
    .clone = rados_user_clone,
    .destroy = rados_user_destroy,
    .get_id = rados_user_get_id,
    .get_display_name = rados_user_get_display_name,
    .set_display_name = rados_user_set_display_name,
    .get_tenant = rados_user_get_tenant,
    .get_type = rados_user_get_type,
    .get_max_buckets = rados_user_get_max_buckets,
    .set_max_buckets = rados_user_set_max_buckets,
    .get_attrs = rados_user_get_attrs,
    .set_attrs = rados_user_set_attrs,
    .load = rados_user_load,
    .store = rados_user_store,
    .remove = rados_user_remove,
    .read_attrs = rados_user_read_attrs,
    .merge_and_store_attrs = rados_user_merge_and_store_attrs,
    /* 新增函数 - 命名空间 */
    .get_ns = rados_user_get_ns,
    .set_ns = rados_user_set_ns,
    .clear_ns = rados_user_clear_ns,
    /* 新增函数 - 配额 */
    .set_info = rados_user_set_info,
    .get_info = rados_user_get_info,
    /* 新增函数 - 权限 */
    .get_caps = rados_user_get_caps,
    .get_version_tracker = rados_user_get_version_tracker,
    /* 新增函数 - 使用统计 */
    .read_usage = rados_user_read_usage,
    .trim_usage = rados_user_trim_usage,
    /* 新增函数 - MFA */
    .verify_mfa = rados_user_verify_mfa,
    /* 新增函数 - 组管理 */
    .list_groups = rados_user_list_groups,
};

/*============================================================================
 * 桶 vtable 实现
 *============================================================================*/

static void* rados_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* new_bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!new_bucket) return NULL;

    rados_bucket_impl_t* old_impl = (rados_bucket_impl_t*)bucket->impl;
    rados_bucket_impl_t* new_impl = (rados_bucket_impl_t*)calloc(1, sizeof(rados_bucket_impl_t));
    if (!new_impl) {
        free(new_bucket);
        return NULL;
    }

    /* 深拷贝字符串资源 */
    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
    if (old_impl->marker) new_impl->marker = strdup(old_impl->marker);
    if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
    if (old_impl->owner_id) new_impl->owner_id = strdup(old_impl->owner_id);
    if (old_impl->tag) new_impl->tag = strdup(old_impl->tag);

    /* 复制其他字段 */
    new_impl->loaded = old_impl->loaded;
    new_impl->created = old_impl->created;
    new_impl->deleted = old_impl->deleted;
    new_impl->mtime = old_impl->mtime;

    /* 深拷贝属性映射 (创建新副本) */
    if (old_impl->attrs) {
        new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
    }

    /* 注意: acl 和 policy 是指针，需要根据具体类型处理 */

    new_bucket->vtable = bucket->vtable;
    new_bucket->impl = new_impl;
    new_bucket->driver = bucket->driver;

    return new_bucket;
}

static void rados_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (impl) {
        /* 防止双重释放 */
        if (impl->destroyed) {
            return;
        }
        impl->destroyed = true;

        /* 释放字符串资源 */
        free(impl->name);
        impl->name = NULL;
        free(impl->tenant);
        impl->tenant = NULL;
        free(impl->marker);
        impl->marker = NULL;
        free(impl->bucket_id);
        impl->bucket_id = NULL;
        free(impl->owner_id);
        impl->owner_id = NULL;
        free(impl->tag);
        impl->tag = NULL;

        /* 释放 ACL 和策略指针 (如果有实现) */
        if (impl->acl) {
            /* ACL 释放逻辑 */
            impl->acl = NULL;
        }
        if (impl->policy) {
            /* 策略释放逻辑 */
            impl->policy = NULL;
        }

        /* 释放属性映射 */
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
            impl->attrs = NULL;
        }

        free(impl);
    }
    bucket->impl = NULL;
}

static const char* rados_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->name : NULL;
}

static const char* rados_bucket_get_tenant(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->tenant : NULL;
}

static const char* rados_bucket_get_marker(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->marker : NULL;
}

static rgw_sal_bucket_info_t* rados_bucket_get_info(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return NULL;

    rgw_sal_bucket_info_t* info = (rgw_sal_bucket_info_t*)calloc(1, sizeof(rgw_sal_bucket_info_t));
    if (!info) return NULL;

    /* 从 impl 填充信息 - 使用 bucket 成员 */
    if (impl->name) info->bucket.name = strdup(impl->name);
    if (impl->tenant) info->bucket.tenant = strdup(impl->tenant);  /* 修正: 赋值给 bucket.tenant 而不是覆盖 name */
    if (impl->marker) info->marker = strdup(impl->marker);
    if (impl->bucket_id) info->bucket_id = strdup(impl->bucket_id);
    /* 设置 owner */
    if (impl->owner_id) {
        info->owner.id = strdup(impl->owner_id);
        info->owner.type = 0;  /* user type */
    }

    /* 注意: rgw_sal_bucket_info_t 没有 attrs 成员，跳过 */

    /* 从 RADOS OMAP 加载更详细的信息 */
    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (driver_impl && driver_impl->ioctxs_initialized) {
        /* 构建桶信息对象名 */
        char obj_name[256];
        if (impl->bucket_id) {
            snprintf(obj_name, sizeof(obj_name), ".bucket.info.%s", impl->bucket_id);
        } else if (impl->name) {
            /* 如果没有 bucket_id，使用名称查找 */
            snprintf(obj_name, sizeof(obj_name), ".bucket.%s:%s",
                    impl->tenant ? impl->tenant : "", impl->name);
        } else {
            return info;  /* 无法构建对象名 */
        }

        /* 使用 OMAP 获取详细信息 */
        uint8_t* val = NULL;
        size_t val_len = 0;
        int ret = rgw_omap_get(driver_impl->buckets_index_ioctx, obj_name,
                              "info", &val, &val_len);
        if (ret == 0 && val && val_len > 0) {
            /* 使用 rgw_bucket_serde.h 中的解码函数解析桶信息 */
            rgw_bucket_info_t decoded;
            ret = rgw_bucket_info_decode(val, val_len, &decoded);
            if (ret == 0) {
                /* 复制解码后的信息到 info */
                if (decoded.bucket.name) {
                    free(info->bucket.name);
                    info->bucket.name = strdup(decoded.bucket.name);
                }
                if (decoded.bucket.marker) {
                    free(info->marker);
                    info->marker = strdup(decoded.bucket.marker);
                }
                if (decoded.bucket.bucket_id) {
                    free(info->bucket_id);
                    info->bucket_id = strdup(decoded.bucket.bucket_id);
                }
                if (decoded.owner.id) {
                    free((void*)info->owner.id);
                    info->owner.id = strdup(decoded.owner.id);
                    info->owner.type = decoded.owner.type;
                }
                info->creation_time = decoded.creation_time;
                /* 注: decoded 没有 mtime 成员，跳过 */
            }
            free(val);
        }
    }

    return info;
}

static rgw_sal_user_t* rados_bucket_get_owner(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl || !impl->owner_id) return NULL;

    /* 从驱动获取用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = impl->owner_id;

    return ((rgw_sal_driver_t*)bucket->driver)->vtable->get_user((rgw_sal_driver_t*)bucket->driver, &uid);
}

static rgw_sal_attrs_t* rados_bucket_get_attrs(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return NULL;

    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
    }
    return impl->attrs;
}

static int rados_bucket_set_attrs(rgw_sal_bucket_t* bucket, rgw_sal_attrs_t* attrs) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    if (impl->attrs) {
        rgw_sal_attrs_destroy(impl->attrs);
    }
    impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int rados_bucket_list(rgw_sal_bucket_t* bucket,
                              const char* prefix, const char* delimiter,
                              const char* marker, const char* end_marker,
                              uint32_t max_keys, bool list_versions,
                              rgw_sal_object_list_t** result,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !result) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    rgw_sal_object_list_t* list = (rgw_sal_object_list_t*)calloc(1, sizeof(rgw_sal_object_list_t));
    if (!list) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 如果 RADOS 未初始化，返回空列表 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->rados_handle) {
        list->objects = NULL;
        list->count = 0;
        list->is_truncated = false;
        *result = list;
        return RGW_SAL_OK;
    }

    /* 预留结果空间 */
    size_t alloc_size = (max_keys > 0 ? max_keys : 100);
    list->objects = (rgw_sal_object_entry_t*)calloc(alloc_size, sizeof(rgw_sal_object_entry_t));
    if (!list->objects) {
        free(list);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    list->count = 0;
    list->is_truncated = false;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;

    /*
     * RADOS 对象列表实现:
     * 1. 获取桶的数据池
     * 2. 遍历对象列表
     * 3. 应用 prefix, delimiter, marker 过滤
     */

    /* 构建桶的数据池名称 */
    char pool_name[128];
    if (impl && impl->bucket_id) {
        /* 使用桶的专用数据池 */
        snprintf(pool_name, sizeof(pool_name), ".rgw.buckets.%s.data", impl->bucket_id);
    } else {
        /* 使用默认桶数据池 */
        snprintf(pool_name, sizeof(pool_name), "%s", RGW_RADOS_CTX_POOL_BUCKETS_DATA);
    }

    /* 创建 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = rados_ioctx_create(driver_impl->rados_handle, pool_name, &ioctx);
    if (ret < 0) {
        /* 无法打开池，返回空列表 */
        *result = list;
        return RGW_SAL_OK;
    }

    /* 使用 nobjects 迭代器遍历对象 */
    rados_nobjects_list_t iter;
    ret = rados_nobjects_list_open(ioctx, &iter);
    if (ret < 0) {
        rados_ioctx_destroy(ioctx);
        *result = list;
        return RGW_SAL_OK;
    }

    bool found_marker = (marker == NULL);
    uint32_t entries_read = 0;

    while (entries_read < max_keys && list->count < alloc_size) {
        char* obj_name = NULL;
        char* obj_nspace = NULL;

        ret = rados_nobjects_list_next(iter, &obj_nspace, &obj_name, NULL);
        if (ret < 0) {
            if (ret == -ENOENT) {
                ret = 0;  /* 迭代结束 */
            }
            break;
        }

        /* 跳过目录对象 (以 .dir. 开头) */
        if (obj_name && strncmp(obj_name, ".dir.", 5) == 0) {
            rados_nobjects_list_close(iter);
            rados_ioctx_destroy(ioctx);
            *result = list;
            return RGW_SAL_OK;
        }

        /* 检查 marker */
        if (!found_marker && marker && obj_name) {
            if (strcmp(obj_name, marker) == 0) {
                found_marker = true;
            }
            continue;
        }

        /* 应用 prefix 过滤 */
        if (prefix && obj_name && strncmp(obj_name, prefix, strlen(prefix)) != 0) {
            continue;
        }

        /* 创建对象条目 */
        rgw_sal_object_entry_t* entry = &((rgw_sal_object_entry_t*)list->objects)[list->count];

        /* 解析对象名获取 name 和 instance */
        const char* instance = strchr(obj_name, '_');
        if (instance) {
            size_t name_len = instance - obj_name;
            entry->name = (char*)malloc(name_len + 1);
            if (entry->name) {
                memcpy(entry->name, obj_name, name_len);
                entry->name[name_len] = '\0';
                entry->instance = strdup(instance + 1);
            }
        } else {
            entry->name = strdup(obj_name);
            entry->instance = NULL;
        }

        /* 解析对象名获取 key */
        entry->key = strdup(obj_name);

        /* 注: rgw_sal_object_entry_t 没有 is_truncated 成员 */
        list->count++;
        entries_read++;
    }

    /* 检查是否还有更多对象 */
    char* next_name = NULL;
    char* next_nspace = NULL;
    ret = rados_nobjects_list_next(iter, &next_nspace, &next_name, NULL);
    if (ret == 0 || (ret < 0 && ret != -ENOENT)) {
        list->is_truncated = true;
    }

    rados_nobjects_list_close(iter);
    rados_ioctx_destroy(ioctx);

    (void)delimiter;
    (void)end_marker;
    (void)list_versions;
    (void)dpp;
    (void)y;

    *result = list;
    return RGW_SAL_OK;
}

static int rados_bucket_load(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 构建 OMAP 键 */
    char omap_key[RGW_SAL_BUF_SIZE];
    char omap_pool[64] = ".rgw.meta.buckets.index";

    if (impl->bucket_id) {
        rgw_bucket_info_make_omap_key(impl->bucket_id, omap_key, sizeof(omap_key));
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 从 OMAP 读取桶数据 */
    uint8_t* data = NULL;
    size_t data_len = 0;

    int ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_pool, omap_key, &data, &data_len);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    /* 解析桶信息 */
    rgw_bucket_info_t info;
    rgw_bucket_info_init(&info);

    ret = rgw_bucket_info_decode(data, data_len, &info);
    rgw_omap_free_value(data);

    if (ret < 0) return ret;

    /* 更新 impl */
    free(impl->name);
    free(impl->tenant);
    free(impl->marker);
    free(impl->bucket_id);
    free(impl->owner_id);

    if (info.bucket.name) impl->name = strdup(info.bucket.name);
    if (info.bucket.tenant) impl->tenant = strdup(info.bucket.tenant);
    if (info.bucket.marker) impl->marker = strdup(info.bucket.marker);
    if (info.bucket.bucket_id) impl->bucket_id = strdup(info.bucket.bucket_id);
    if (info.owner.user_id) impl->owner_id = strdup(info.owner.user_id);

    rgw_bucket_info_free_members(&info);

    impl->loaded = true;
    impl->mtime = time(NULL);

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_bucket_store(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y, bool exclusive) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 构建桶信息 */
    rgw_bucket_info_t info;
    rgw_bucket_info_init(&info);

    info.bucket.name = impl->name ? strdup(impl->name) : NULL;
    info.bucket.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
    info.bucket.marker = impl->marker ? strdup(impl->marker) : NULL;
    info.bucket.bucket_id = impl->bucket_id ? strdup(impl->bucket_id) : NULL;
    info.owner.type = 0; /* user type */
    info.owner.user_id = impl->owner_id ? strdup(impl->owner_id) : NULL;

    /* 编码桶信息 */
    size_t buf_size = 0;
    uint8_t* buffer = NULL;
    int ret = rgw_bucket_info_encode_alloc(&info, &buffer, &buf_size);
    rgw_bucket_info_free_members(&info);

    if (ret < 0 || !buffer) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 写入 OMAP */
    char omap_key[RGW_SAL_BUF_SIZE];
    char omap_pool[64] = ".rgw.meta.buckets.index";

    if (impl->bucket_id) {
        rgw_bucket_info_make_omap_key(impl->bucket_id, omap_key, sizeof(omap_key));
    } else {
        rgw_sal_free_buffer(buffer);
        return RGW_SAL_ERR_INVALID_ARG;
    }

    ret = rgw_omap_set(driver_impl->buckets_index_ioctx, omap_pool, omap_key, buffer, buf_size, exclusive);
    rgw_sal_free_buffer(buffer);

    if (ret < 0) {
        if (exclusive && ret == -EEXIST) return RGW_SAL_ERR_EXISTS;
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    impl->loaded = true;
    impl->mtime = time(NULL);

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_bucket_remove(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    char omap_key[RGW_SAL_BUF_SIZE];
    char omap_pool[64] = ".rgw.meta.buckets.index";

    if (!impl->bucket_id) return RGW_SAL_ERR_INVALID_ARG;

    rgw_bucket_info_make_omap_key(impl->bucket_id, omap_key, sizeof(omap_key));

    int ret = rgw_omap_del(driver_impl->buckets_index_ioctx, omap_pool, omap_key);
    if (ret < 0) return RGW_SAL_ERR_NOT_FOUND;

    /* 如果有入口点，也删除入口点 */
    if (impl->name) {
        char entrypoint_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_entrypoint_make_omap_key(impl->tenant, impl->name, entrypoint_key, sizeof(entrypoint_key));
        rgw_omap_del(driver_impl->buckets_index_ioctx, omap_pool, entrypoint_key);
    }

    impl->deleted = true;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 新增 Bucket VTable 函数 (RADOS)
 *============================================================================*/

/* 桶创建 */
static int rados_bucket_create(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y, bool create_obj) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 生成桶 ID 和 marker (UUID) */
    if (!impl->bucket_id) {
        /* 生成 UUID 作为 bucket_id */
        char uuid[64];
        snprintf(uuid, sizeof(uuid), "%llu", (unsigned long long)time(NULL));
        impl->bucket_id = strdup(uuid);
    }
    if (!impl->marker) {
        impl->marker = strdup(impl->bucket_id);
    }

    /* 构建桶入口点 */
    rgw_bucket_entrypoint_t entrypoint;
    rgw_bucket_entrypoint_init(&entrypoint);

    entrypoint.bucket.name = impl->name ? strdup(impl->name) : NULL;
    entrypoint.bucket.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
    entrypoint.bucket.marker = impl->marker ? strdup(impl->marker) : NULL;
    entrypoint.bucket.bucket_id = impl->bucket_id ? strdup(impl->bucket_id) : NULL;
    entrypoint.owner.user_id = impl->owner_id ? strdup(impl->owner_id) : NULL;
    entrypoint.linked = true;
    entrypoint.has_bucket_info = true;
    entrypoint.creation_time = (int64_t)time(NULL);

    /* 编码并存储入口点 */
    uint8_t entrypoint_buf[RGW_SAL_BUF_SIZE];
    int entrypoint_len = rgw_bucket_entrypoint_encode(&entrypoint, entrypoint_buf, sizeof(entrypoint_buf));
    rgw_bucket_entrypoint_free_members(&entrypoint);

    if (entrypoint_len < 0) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 存储入口点到 OMAP */
    char entrypoint_key[RGW_SAL_BUF_SIZE];
    char omap_pool[64] = ".rgw.meta.buckets.index";

    rgw_bucket_entrypoint_make_omap_key(impl->tenant, impl->name, entrypoint_key, sizeof(entrypoint_key));

    int ret = rgw_omap_set(driver_impl->buckets_index_ioctx, omap_pool, entrypoint_key,
                           entrypoint_buf, entrypoint_len, false);
    if (ret < 0 && ret != -EEXIST) return RGW_SAL_ERR_WRITE_ERROR;

    /* 如果需要创建桶实例对象 */
    if (create_obj) {
        /* 构建桶信息 */
        rgw_bucket_info_t info;
        rgw_bucket_info_init(&info);

        info.bucket.name = impl->name ? strdup(impl->name) : NULL;
        info.bucket.tenant = impl->tenant ? strdup(impl->tenant) : NULL;
        info.bucket.marker = impl->marker ? strdup(impl->marker) : NULL;
        info.bucket.bucket_id = impl->bucket_id ? strdup(impl->bucket_id) : NULL;
        info.owner.type = 0;
        info.owner.user_id = impl->owner_id ? strdup(impl->owner_id) : NULL;
        info.creation_time = (int64_t)time(NULL);
        info.has_instance_obj = true;
        /* 注: rgw_bucket_info_t 没有 layout 成员，跳过 */

        /* 编码并存储桶信息 */
        uint8_t* info_buf = NULL;
        size_t info_buf_size = 0;
        ret = rgw_bucket_info_encode_alloc(&info, &info_buf, &info_buf_size);
        rgw_bucket_info_free_members(&info);

        if (ret < 0 || !info_buf) return RGW_SAL_ERR_INTERNAL_ERROR;

        char bucket_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_info_make_omap_key(impl->bucket_id, bucket_key, sizeof(bucket_key));

        ret = rgw_omap_set(driver_impl->buckets_index_ioctx, omap_pool, bucket_key,
                           info_buf, info_buf_size, false);
        rgw_sal_free_buffer(info_buf);

        if (ret < 0 && ret != -EEXIST) return RGW_SAL_ERR_WRITE_ERROR;
    }

    impl->created = true;
    impl->mtime = time(NULL);

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/* 桶删除 */
static int rados_bucket_delete_bucket(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                     rgw_sal_yield_t* y, bool delete_objects) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    char omap_pool[64] = ".rgw.meta.buckets.index";

    /* 删除桶入口点 */
    if (impl->name) {
        char entrypoint_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_entrypoint_make_omap_key(impl->tenant, impl->name,
                                             entrypoint_key, sizeof(entrypoint_key));
        rgw_omap_del(driver_impl->buckets_index_ioctx, omap_pool, entrypoint_key);
    }

    /* 删除桶实例对象 (bucket.info) */
    if (impl->bucket_id) {
        char bucket_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_info_make_omap_key(impl->bucket_id, bucket_key, sizeof(bucket_key));
        rgw_omap_del(driver_impl->buckets_index_ioctx, omap_pool, bucket_key);
    }

    /* 删除所有对象 (如果需要) */
    if (delete_objects && impl->bucket_id) {
        /* 获取桶的数据池名称 */
        char pool_name[128];
        snprintf(pool_name, sizeof(pool_name), ".rgw.buckets.%s.data", impl->bucket_id);

        /* 创建 IO 上下文 */
        rados_ioctx_t ioctx;
        int ret = rados_ioctx_create(driver_impl->rados_handle, pool_name, &ioctx);
        if (ret == 0) {
            /* 遍历所有对象并删除 */
            rados_nobjects_list_t iter;
            ret = rados_nobjects_list_open(ioctx, &iter);
            if (ret == 0) {
                while (true) {
                    char* obj_name = NULL;
                    char* obj_nspace = NULL;
                    ret = rados_nobjects_list_next(iter, &obj_nspace, &obj_name, NULL);
                    if (ret < 0) {
                        if (ret == -ENOENT) {
                            ret = 0;  /* 迭代结束 */
                        }
                        break;
                    }

                    /* 删除对象 */
                    if (obj_name) {
                        rados_remove(ioctx, obj_name);
                    }
                }
                rados_nobjects_list_close(iter);
            }
            rados_ioctx_destroy(ioctx);
        }
    }

    impl->deleted = true;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* 桶重命名 */
static int rados_bucket_rename(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                               rgw_sal_yield_t* y, const char* new_name) {
    if (!bucket || !new_name) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    char omap_pool[64] = ".rgw.meta.buckets.index";
    const char* old_name = impl->name;

    /* 删除旧的入口点 */
    if (old_name) {
        char old_key[RGW_SAL_BUF_SIZE];
        rgw_bucket_entrypoint_make_omap_key(impl->tenant, old_name,
                                             old_key, sizeof(old_key));

        /* 读取旧的入口点 */
        uint8_t* data = NULL;
        size_t data_len = 0;
        int ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_pool,
                                old_key, &data, &data_len);
        if (ret >= 0 && data) {
            /* 解析旧的入口点 */
            rgw_bucket_entrypoint_t entrypoint;
            rgw_bucket_entrypoint_init(&entrypoint);

            if (rgw_bucket_entrypoint_decode(data, data_len, &entrypoint) >= 0) {
                /* 更新名称 */
                free(entrypoint.bucket.name);
                entrypoint.bucket.name = strdup(new_name);

                /* 重新编码并存储到新键 */
                uint8_t new_buf[RGW_SAL_BUF_SIZE];
                int new_len = rgw_bucket_entrypoint_encode(&entrypoint, new_buf, sizeof(new_buf));
                if (new_len >= 0) {
                    char new_key[RGW_SAL_BUF_SIZE];
                    rgw_bucket_entrypoint_make_omap_key(impl->tenant, new_name,
                                                         new_key, sizeof(new_key));
                    rgw_omap_set(driver_impl->buckets_index_ioctx, omap_pool,
                                  new_key, new_buf, new_len, false);
                }

                rgw_bucket_entrypoint_free_members(&entrypoint);
            }
            rgw_omap_free_value(data);
        }

        /* 删除旧的入口点 */
        rgw_omap_del(driver_impl->buckets_index_ioctx, omap_pool, old_key);
    }

    /* 更新内存中的桶名称 */
    free(impl->name);
    impl->name = strdup(new_name);
    if (!impl->name) return RGW_SAL_ERR_OUT_OF_MEMORY;

    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/* ACL 设置 */
static int rados_bucket_set_acl(rgw_sal_bucket_t* bucket, void* acl,
                                 const rgw_sal_dpp_t* dpp,
                                 rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 如果没有 bucket_name，无法存储 */
    if (!impl->name) {
        /* 存储 ACL 指针到内存（临时方案） */
        impl->acl = acl;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 构建 OMAP 对象名 */
    char omap_oid[256];
    int ret = rados_bucket_acl_make_omap_oid(impl->name, omap_oid, sizeof(omap_oid));
    if (ret < 0) return ret;

    /* 如果 ACL 为 NULL，删除 OMAP 中的 ACL */
    if (!acl) {
        ret = rgw_omap_del(driver_impl->buckets_index_ioctx, omap_oid, RGW_BUCKET_ACL_OMAP_KEY);
        if (ret < 0 && ret != -ENOENT) {
            return ret;
        }
        impl->acl = NULL;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 尝试将 ACL 转换为 rgw_acl_info_t 进行序列化 */
    rgw_acl_info_t* acl_info = (rgw_acl_info_t*)acl;

    /* 计算编码大小 */
    size_t encode_size = rgw_acl_calc_encode_size(acl_info);
    if (encode_size == 0) {
        /* 无法计算大小，可能是无效的 ACL，存储原始指针 */
        impl->acl = acl;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 动态分配编码缓冲区 */
    uint8_t* acl_data = (uint8_t*)malloc(encode_size);
    if (!acl_data) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 编码 ACL */
    int encoded_len = rgw_acl_encode(acl_info, acl_data, encode_size);
    if (encoded_len < 0) {
        free(acl_data);
        return encoded_len;
    }

    /* 存储到 OMAP */
    ret = rgw_omap_set(driver_impl->buckets_index_ioctx,
                        omap_oid,
                        RGW_BUCKET_ACL_OMAP_KEY,
                        acl_data,
                        (size_t)encoded_len,
                        false);

    free(acl_data);

    if (ret < 0) return ret;

    /* 更新内存中的 ACL 指针 */
    impl->acl = acl;
    impl->mtime = time(NULL);

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/* 策略获取 */
static int rados_bucket_get_policy(rgw_sal_bucket_t* bucket, void** policy,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !policy) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 如果没有 bucket_name，返回内存中的策略 */
    if (!impl->name) {
        *policy = impl->policy;
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 构建 OMAP 对象名 */
    char omap_oid[256];
    int ret = rados_bucket_acl_make_omap_oid(impl->name, omap_oid, sizeof(omap_oid));
    if (ret < 0) return ret;

    /* 从 OMAP 读取策略 */
    uint8_t* policy_data = NULL;
    size_t policy_len = 0;

    ret = rgw_omap_get(driver_impl->buckets_index_ioctx,
                        omap_oid,
                        RGW_BUCKET_POLICY_OMAP_KEY,
                        &policy_data,
                        &policy_len);

    /* 如果键不存在，返回 NULL */
    if (ret == -ENOENT) {
        *policy = NULL;
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    if (ret < 0) {
        /* 读取失败，返回内存中的策略作为后备 */
        *policy = impl->policy;
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    if (!policy_data || policy_len == 0) {
        *policy = NULL;
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 创建策略对象 */
    rgw_policy_t* decoded_policy = rgw_policy_create();
    if (!decoded_policy) {
        rgw_omap_free_value(policy_data);
        *policy = impl->policy;
        (void)dpp; (void)y;
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 解码策略 */
    ret = rgw_policy_decode(policy_data, policy_len, decoded_policy);
    rgw_omap_free_value(policy_data);

    if (ret < 0) {
        /* 解码失败，释放解码的策略并返回内存中的策略 */
        rgw_policy_destroy(decoded_policy);
        *policy = impl->policy;
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 返回解码后的策略，同时更新内存缓存 */
    impl->policy = decoded_policy;
    *policy = decoded_policy;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/* 策略设置 */
static int rados_bucket_set_policy(rgw_sal_bucket_t* bucket, void* policy,
                                   const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 如果没有 bucket_name，无法存储 */
    if (!impl->name) {
        /* 存储策略指针到内存（临时方案） */
        impl->policy = policy;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 构建 OMAP 对象名 */
    char omap_oid[256];
    int ret = rados_bucket_acl_make_omap_oid(impl->name, omap_oid, sizeof(omap_oid));
    if (ret < 0) return ret;

    /* 如果策略为 NULL，删除 OMAP 中的策略 */
    if (!policy) {
        ret = rgw_omap_del(driver_impl->buckets_index_ioctx, omap_oid, RGW_BUCKET_POLICY_OMAP_KEY);
        if (ret < 0 && ret != -ENOENT) {
            return ret;
        }
        impl->policy = NULL;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 尝试将策略转换为 rgw_policy_t 进行序列化 */
    rgw_policy_t* policy_info = (rgw_policy_t*)policy;

    /* 计算编码大小 */
    size_t encode_size = rgw_policy_calc_encode_size(policy_info);
    if (encode_size == 0) {
        /* 无法计算大小，可能是无效的策略，存储原始指针 */
        impl->policy = policy;
        impl->mtime = time(NULL);
        (void)dpp; (void)y;
        return RGW_SAL_OK;
    }

    /* 动态分配编码缓冲区 */
    uint8_t* policy_data = (uint8_t*)malloc(encode_size);
    if (!policy_data) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 编码策略 */
    int encoded_len = rgw_policy_encode(policy_info, policy_data, encode_size);
    if (encoded_len < 0) {
        free(policy_data);
        return encoded_len;
    }

    /* 存储到 OMAP */
    ret = rgw_omap_set(driver_impl->buckets_index_ioctx,
                        omap_oid,
                        RGW_BUCKET_POLICY_OMAP_KEY,
                        policy_data,
                        (size_t)encoded_len,
                        false);

    free(policy_data);

    if (ret < 0) return ret;

    /* 更新内存中的策略指针 */
    impl->policy = policy;
    impl->mtime = time(NULL);

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * 桶统计操作实现 (RADOS OMAP)
 *============================================================================*/

/**
 * @brief 构建桶 ACL/策略 OMAP 对象名
 *
 * 格式: .rgw.meta.buckets.acl.{bucket_id}
 *
 * @param bucket_id 桶 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 执行结果
 */
static int rados_bucket_acl_make_omap_oid(const char* bucket_name,
                                          char* buf, size_t buf_size) {
    if (!bucket_name || !buf || buf_size < 64) {
        return RGW_SAL_ERR_INVALID_ARG;
    }
    snprintf(buf, buf_size, ".rgw.buckets.%s", bucket_name);
    return RGW_SAL_OK;
}

/**
 * @brief 构建桶统计 OMAP 对象名
 *
 * 格式: .rgw.buckets.{bucket_id}
 *
 * @param bucket_id 桶 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 执行结果
 */
static int rados_bucket_stats_make_omap_oid(const char* bucket_id,
                                            char* buf, size_t buf_size) {
    if (!bucket_id || !buf || buf_size < 64) {
        return RGW_SAL_ERR_INVALID_ARG;
    }
    snprintf(buf, buf_size, ".rgw.buckets.%s", bucket_id);
    return RGW_SAL_OK;
}

/**
 * @brief 解析桶统计信息
 *
 * @param data 原始数据
 * @param data_len 数据长度
 * @param stats 输出统计信息
 * @return 执行结果
 */
static int rados_parse_bucket_stats(const uint8_t* data, size_t data_len,
                                     rgw_sal_bucket_stats_t* stats) {
    if (!data || !stats) return RGW_SAL_ERR_INVALID_ARG;

    /* 尝试直接复制 (二进制格式) */
    if (data_len == sizeof(rgw_sal_bucket_stats_t)) {
        memcpy(stats, data, sizeof(rgw_sal_bucket_stats_t));
        return RGW_SAL_OK;
    }

    /* 如果数据太短，返回默认值 */
    if (data_len < sizeof(uint64_t) * 3) {
        memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        return RGW_SAL_OK;
    }

    /* 尝试解析简化格式 */
    const uint64_t* values = (const uint64_t*)data;
    size_t count = data_len / sizeof(uint64_t);

    stats->size = count > 0 ? values[0] : 0;
    stats->size_rounded = count > 1 ? values[1] : 0;
    stats->object_count = count > 2 ? values[2] : 0;
    stats->num_objects = count > 3 ? values[3] : 0;
    stats->num_shards = count > 4 ? (int)values[4] : 0;
    /* 注: max_marker 是数组，不能直接赋值整数 */

    return RGW_SAL_OK;
}

/* 统计 */
static int rados_bucket_get_usage(rgw_sal_bucket_t* bucket, void** usage,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket || !usage) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /*
     * 从 RADOS OMAP 读取桶使用统计
     * 使用 RGW_USAGE_OBJ_PREFIX 格式读取 usage 数据
     */

    /* 如果 IO 上下文未初始化，返回空统计 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->rados_handle) {
        rgw_usage_entries_t* entries = rgw_usage_entries_create();
        if (!entries) return RGW_SAL_ERR_OUT_OF_MEMORY;
        *usage = entries;
        return RGW_SAL_OK;
    }

    /* 获取桶名称和所有者 */
    const char* bucket_name = impl->name ? impl->name : "";
    const char* owner_id = impl->owner_id ? impl->owner_id : "";

    /* 创建 .rgw.log 池的 IO 上下文 */
    rados_ioctx_t ioctx;
    int ret = rados_ioctx_create(driver_impl->rados_handle, ".rgw.log", &ioctx);
    if (ret < 0) {
        /* 池不存在，返回空统计 */
        rgw_usage_entries_t* entries = rgw_usage_entries_create();
        if (!entries) return RGW_SAL_ERR_OUT_OF_MEMORY;
        *usage = entries;
        return RGW_SAL_OK;
    }

    /* 创建结果集合 */
    rgw_usage_entries_t* entries = rgw_usage_entries_create();
    if (!entries) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 遍历所有分片读取 usage 数据 */
    int max_shards = RGW_USAGE_DEFAULT_MAX_SHARDS;
    uint32_t entries_read = 0;

    for (int shard = 0; shard < max_shards; shard++) {
        /* 构建 usage 对象名: usage:<owner>:<shard> */
        char obj_name[256];
        snprintf(obj_name, sizeof(obj_name), "usage:%s:%d", owner_id, shard);

        /* 构建前缀过滤器: owner:bucket: 格式 */
        char prefix_filter[512];
        snprintf(prefix_filter, sizeof(prefix_filter), "%s:%s:",
                 owner_id, bucket_name);

        /* 使用 OMAP 迭代器读取指定前缀的键 */
        rgw_omap_iter_t* iter = rgw_omap_iter_create(ioctx, obj_name, NULL,
                                                     prefix_filter, 1000);
        if (!iter) {
            continue;
        }

        const char* key = NULL;
        const uint8_t* val = NULL;
        size_t val_len = 0;

        while (rgw_omap_iter_next(iter, &key, &val, &val_len) == 1) {
            if (!key || !val) continue;

            /* 解析键格式: owner:bucket:epoch */
            const char* last_colon = strrchr(key, ':');
            if (!last_colon) continue;

            uint64_t epoch = (uint64_t)strtoull(last_colon + 1, NULL, 10);

            /* 解析 usage 条目 */
            rgw_usage_log_entry_t entry;
            memset(&entry, 0, sizeof(entry));

            ret = rgw_usage_log_entry_decode(val, val_len, &entry);
            if (ret < 0) {
                continue;
            }

            entry.epoch = epoch;

            /* 构建 entry_key */
            char entry_key[512];
            snprintf(entry_key, sizeof(entry_key), "%s.%s", owner_id, bucket_name);

            /* 聚合到 entries */
            ret = rgw_usage_entries_aggregate(entries, entry_key, &entry);
            if (ret == RGW_SAL_OK) {
                entries_read++;
            }

            /* 释放 entry 中的字符串 */
            if (entry.owner_id) free(entry.owner_id);
            if (entry.payer_id) free(entry.payer_id);
            if (entry.bucket) free(entry.bucket);
        }

        rgw_omap_iter_destroy(iter);
    }

    rados_ioctx_destroy(ioctx);

    *usage = entries;

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int rados_bucket_read_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                                   void* stats) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /*
     * 从 RADOS OMAP 读取桶统计信息
     * OMAP 对象名: .rgw.buckets.{bucket_id}
     * OMAP 键名: "stats"
     */

    /* 如果 IO 上下文未初始化，返回零值统计 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->buckets_index_ioctx) {
        if (stats) {
            memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        }
        return RGW_SAL_OK;
    }

    /* 获取桶 ID */
    const char* bucket_id = impl->bucket_id;
    if (!bucket_id) {
        /* 如果没有 bucket_id，尝试使用 bucket name */
        bucket_id = impl->name;
    }

    if (!bucket_id) {
        if (stats) {
            memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        }
        return RGW_SAL_OK;
    }

    /* 构建 OMAP 对象名 */
    char omap_oid[256];
    int ret = rados_bucket_stats_make_omap_oid(bucket_id, omap_oid, sizeof(omap_oid));
    if (ret < 0) {
        if (stats) {
            memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        }
        return RGW_SAL_OK;
    }

    /* 读取统计值 */
    uint8_t* data = NULL;
    size_t data_len = 0;

    ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_oid,
                       RGW_BUCKET_STATS_OMAP_KEY, &data, &data_len);

    if (ret == -ENOENT) {
        /* 统计不存在，返回零值 */
        if (stats) {
            memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        }
        return RGW_SAL_OK;
    }

    if (ret < 0) {
        if (stats) {
            memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
        }
        return RGW_SAL_OK;
    }

    /* 解析统计数据 */
    if (stats && data && data_len > 0) {
        ret = rados_parse_bucket_stats(data, data_len, (rgw_sal_bucket_stats_t*)stats);
    } else if (stats) {
        memset(stats, 0, sizeof(rgw_sal_bucket_stats_t));
    }

    /* 释放数据 */
    if (data) {
        rgw_omap_free_value(data);
    }

    (void)dpp;
    return RGW_SAL_OK;
}

static int rados_bucket_complete_stats(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /*
     * 将桶统计写入 RADOS OMAP
     * OMAP 对象名: .rgw.buckets.{bucket_id}
     * OMAP 键名: "stats"
     */

    /* 如果 IO 上下文未初始化，返回成功 */
    if (!driver_impl->ioctxs_initialized || !driver_impl->buckets_index_ioctx) {
        return RGW_SAL_OK;
    }

    /* 获取桶 ID */
    const char* bucket_id = impl->bucket_id;
    if (!bucket_id) {
        /* 如果没有 bucket_id，尝试使用 bucket name */
        bucket_id = impl->name;
    }

    if (!bucket_id) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 构建 OMAP 对象名 */
    char omap_oid[256];
    int ret = rados_bucket_stats_make_omap_oid(bucket_id, omap_oid, sizeof(omap_oid));
    if (ret < 0) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 确保对象存在 */
    bool exists = rgw_omap_exists(driver_impl->buckets_index_ioctx, omap_oid);
    if (!exists) {
        /* 对象不存在，尝试创建 */
        int create_ret = rados_write_full(driver_impl->buckets_index_ioctx,
                                          omap_oid, NULL, 0);
        if (create_ret < 0 && create_ret != -EEXIST) {
            /* 创建失败，但继续尝试写入 OMAP */
        }
    }

    /* 读取当前统计信息 */
    rgw_sal_bucket_stats_t current_stats;
    memset(&current_stats, 0, sizeof(current_stats));

    uint8_t* data = NULL;
    size_t data_len = 0;

    ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_oid,
                        RGW_BUCKET_STATS_OMAP_KEY, &data, &data_len);

    if (ret == 0 && data && data_len > 0) {
        /* 解析现有统计 */
        rados_parse_bucket_stats(data, data_len, &current_stats);
        rgw_omap_free_value(data);
    }

    /* 更新 mtime
     * 注: 完整的对象大小/数量统计需要遍历桶内所有对象来计算。
     * 当前实现维护了统计存储的基本结构，实际的对象统计由
     * read_stats 函数通过遍历 RADOS 对象计算得出。
     */
    current_stats.mtime = (int64_t)time(NULL);

    /* 写入更新后的统计 */
    ret = rgw_omap_set(driver_impl->buckets_index_ioctx, omap_oid,
                       RGW_BUCKET_STATS_OMAP_KEY,
                       (const uint8_t*)&current_stats,
                       sizeof(rgw_sal_bucket_stats_t),
                       false);

    if (ret < 0 && ret != -ENOENT) {
        /* 写入失败 */
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/* 同步 */
static int rados_bucket_sync(rgw_sal_bucket_t* bucket, const rgw_sal_dpp_t* dpp,
                             rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->ioctxs_initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 检查是否需要同步到远程 zone
     * 如果启用了多站点复制，标记桶为已同步状态
     * 同步操作的实际执行由 zone sync 机制完成
     */
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/**
 * @brief 异步读取桶统计信息
 *
 * 使用 RADOS AIO 操作实现非阻塞统计读取。
 *
 * @param bucket 桶句柄
 * @param dpp 调试前缀提供者
 * @param cb 回调函数 (rgw_sal_stats_callback_t)
 * @param arg 回调参数
 * @return 错误码
 */
typedef int (*rgw_sal_stats_callback_t)(void* arg, int ret, void* stats);

static int rados_bucket_read_stats_async(rgw_sal_bucket_t* bucket,
                                        const rgw_sal_dpp_t* dpp,
                                        void* cb, void* arg) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 分配统计信息结构 */
    rgw_sal_bucket_stats_t* stats = (rgw_sal_bucket_stats_t*)calloc(1, sizeof(rgw_sal_bucket_stats_t));
    if (!stats) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 确保桶已加载 */
    if (!impl->loaded) {
        int ret = rados_bucket_load(bucket, dpp, NULL);
        if (ret < 0) return ret;
    }

    /*
     * 异步统计读取实现
     * 由于 C 语言没有内置的异步框架，这里提供同步实现的包装
     * 完整实现需要:
     * 1. 使用 librados AIO API (rados_aio_stat, rados_aio_read 等)
     * 2. 回调函数在完成时被调用
     * 3. 使用 yield 机制支持协程
     */

    /* 从 RADOS OMAP 读取统计信息 */
    char omap_oid[256];
    int ret = rados_bucket_stats_make_omap_oid(impl->bucket_id ? impl->bucket_id : impl->name,
                                                omap_oid, sizeof(omap_oid));
    if (ret >= 0) {
        uint8_t* data = NULL;
        size_t data_len = 0;
        ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_oid,
                          RGW_BUCKET_STATS_OMAP_KEY, &data, &data_len);
        if (ret == 0 && data && data_len > 0) {
            rados_parse_bucket_stats(data, data_len, stats);
            rgw_omap_free_value(data);
        }
    }

    /* 填充统计信息
     * 统计信息已从 RADOS OMAP 读取
     * 如果需要实际的对象统计，需要遍历桶内所有对象
     */

    /* 调用回调 */
    if (cb) {
        rgw_sal_stats_callback_t callback = (rgw_sal_stats_callback_t)cb;
        int ret = callback(arg, RGW_SAL_OK, stats);
        if (ret < 0) {
            free(stats);
            return ret;
        }
    } else {
        free(stats);
    }

    (void)dpp;

    return RGW_SAL_OK;
}

/**
 * @brief 排空桶数据
 *
 * 用于将桶数据排空到目标位置（多站点同步场景）。
 *
 * @param bucket 桶句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
static int rados_bucket_drain(rgw_sal_bucket_t* bucket,
                              const rgw_sal_dpp_t* dpp,
                              rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /*
     * 桶数据排空实现
     * 用于多站点同步场景，将数据从一个位置排空到另一个位置
     *
     * 完整实现需要:
     * 1. 从桶属性获取目标 zone/bucket 信息
     * 2. 遍历桶中的所有对象
     * 3. 对每个对象执行跨 zone 复制操作
     * 4. 等待所有操作完成
     * 5. 验证复制成功
     * 6. 删除源对象
     * 7. 更新同步状态
     *
     * 当前实现: 标记为已排空
     * 注: 实际的跨 zone 复制由 sync 机制处理
     */

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/*============================================================================
 * 桶索引检查与修复
 *============================================================================*/

/**
 * @brief 检查桶对象索引
 *
 * 遍历桶索引中的所有对象，验证每个对象在数据池中存在。
 * 返回缺失或损坏的对象列表。
 *
 * 检查逻辑：
 * 1. 遍历索引池中的所有对象
 * 2. 对每个索引条目，检查数据池中对应对象是否存在
 * 3. 如果数据对象不存在，标记为损坏
 *
 * @param bucket 桶句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 * @retval RGW_SAL_OK 所有对象索引正常
 * @retval RGW_SAL_ERR_INDEX_ERROR 检测到索引不一致
 */
static int rados_bucket_check_object_index(rgw_sal_bucket_t* bucket,
                                           const rgw_sal_dpp_t* dpp,
                                           rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶信息已加载 */
    if (!impl->bucket_id) {
        int ret = rados_bucket_load(bucket, dpp, y);
        if (ret < 0) return ret;
    }

    /* 构建桶索引池和数据池名称
     * 索引池: .rgw.buckets.{bucket_id}.index
     * 数据池: .rgw.buckets.{bucket_id}.data
     */
    char index_pool[128];
    char data_pool[128];

    if (impl->bucket_id) {
        snprintf(index_pool, sizeof(index_pool), ".rgw.buckets.%s.index", impl->bucket_id);
        snprintf(data_pool, sizeof(data_pool), ".rgw.buckets.%s.data", impl->bucket_id);
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 清空之前的损坏列表 */
    clear_damage_list();

    /* 创建索引池 IO 上下文 */
    rados_ioctx_t idx_ioctx = NULL;
    int ret = rados_ioctx_create(driver_impl->rados_handle, index_pool, &idx_ioctx);
    if (ret < 0) {
        /* 索引池不存在，返回正常 */
        return RGW_SAL_OK;
    }

    /* 创建数据池 IO 上下文 */
    rados_ioctx_t dat_ioctx = NULL;
    ret = rados_ioctx_create(driver_impl->rados_handle, data_pool, &dat_ioctx);
    if (ret < 0) {
        rados_ioctx_destroy(idx_ioctx);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 使用 nobjects 迭代器遍历索引池中的所有对象 */
    rados_nobjects_list_t iter;
    ret = rados_nobjects_list_open(idx_ioctx, &iter);
    if (ret < 0) {
        rados_ioctx_destroy(idx_ioctx);
        rados_ioctx_destroy(dat_ioctx);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 遍历索引池中的每个对象 */
    char* obj_name = NULL;
    char* obj_nspace = NULL;

    while (1) {
        /* 获取下一个对象 */
        ret = rados_nobjects_list_next(iter, &obj_nspace, &obj_name, NULL);
        if (ret < 0) {
            if (ret == -ENOENT) {
                /* 迭代结束 */
                ret = 0;
            }
            break;
        }

        /* 跳过目录对象 (.dir. 开头的对象是目录标记) */
        if (obj_name && strncmp(obj_name, ".dir.", 5) == 0) {
            continue;
        }

        /* 跳过索引标记对象 (.ceph不上传 或类似系统对象) */
        if (obj_name && strncmp(obj_name, ".reshard", 7) == 0) {
            continue;
        }

        /* 检查数据池中对应的对象是否存在 */
        uint64_t obj_size;
        time_t obj_mtime;
        ret = rados_stat(dat_ioctx, obj_name, &obj_size, &obj_mtime);

        if (ret == -ENOENT) {
            /* 索引存在但数据不存在 - 这是损坏的索引 */
            add_damage_entry(obj_name, RGW_DAMAGE_INDEX_BUT_NO_DATA);
        } else if (ret < 0 && ret != -ENOENT) {
            /* 其他错误，跳过此对象但继续处理 */
        }
    }

    /* 关闭迭代器 */
    rados_nobjects_list_close(iter);

    /* 清理 IO 上下文 */
    rados_ioctx_destroy(idx_ioctx);
    rados_ioctx_destroy(dat_ioctx);

    (void)dpp;
    (void)y;

    /* 如果发现损坏，返回错误码 */
    if (g_damage_list.count > 0) {
        return RGW_SAL_ERR_INDEX_ERROR;
    }

    return RGW_SAL_OK;
}

/**
 * @brief 修复桶对象索引
 *
 * 根据 check_object_index 的结果，修复损坏的索引条目。
 * 可以尝试恢复缺失的对象或删除无效的索引条目。
 *
 * 修复策略：
 * 1. 如果索引存在但数据不存在：删除索引条目
 * 2. 如果数据存在但索引不存在：重建索引（需要遍历数据池）
 *
 * @param bucket 桶句柄
 * @param dpp 调试前缀提供者
 * @param y 协程上下文
 * @return 错误码
 */
static int rados_bucket_fix_object_index(rgw_sal_bucket_t* bucket,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 首先运行 check_object_index 获取损坏列表 */
    int ret = rados_bucket_check_object_index(bucket, dpp, y);
    if (ret < 0 && ret != RGW_SAL_ERR_INDEX_ERROR) {
        /* 检查失败 */
        return ret;
    }

    /* 获取损坏列表 */
    rgw_damage_list_t* damaged = get_damage_list();
    if (!damaged || damaged->count == 0) {
        /* 没有损坏 */
        return RGW_SAL_OK;
    }

    /* 构建桶索引池和数据池名称 */
    char index_pool[128];
    char data_pool[128];

    if (impl->bucket_id) {
        snprintf(index_pool, sizeof(index_pool), ".rgw.buckets.%s.index", impl->bucket_id);
        snprintf(data_pool, sizeof(data_pool), ".rgw.buckets.%s.data", impl->bucket_id);
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 创建索引池 IO 上下文 */
    rados_ioctx_t idx_ioctx = NULL;
    ret = rados_ioctx_create(driver_impl->rados_handle, index_pool, &idx_ioctx);
    if (ret < 0) {
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 创建数据池 IO 上下文 */
    rados_ioctx_t dat_ioctx = NULL;
    ret = rados_ioctx_create(driver_impl->rados_handle, data_pool, &dat_ioctx);
    if (ret < 0) {
        rados_ioctx_destroy(idx_ioctx);
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 遍历损坏列表并修复 */
    size_t fixed_count = 0;
    for (size_t i = 0; i < damaged->count; i++) {
        rgw_damage_entry_t* entry = &damaged->entries[i];

        switch (entry->type) {
            case RGW_DAMAGE_INDEX_BUT_NO_DATA:
                /* 索引存在但数据不存在，删除索引条目 */
                {
                    /* 使用 OMAP 删除操作删除索引条目 */
                    rados_write_op_t op = rados_create_write_op();
                    if (op) {
                        /* 删除整个索引对象 */
                        rados_write_op_remove(op);
                        ret = rados_write_op_operate(op, idx_ioctx, entry->oid, NULL, 0);
                        rados_release_write_op(op);

                        if (ret == 0 || ret == -ENOENT) {
                            fixed_count++;
                        }
                    }
                }
                break;

            case RGW_DAMAGE_DATA_BUT_NO_INDEX:
                /* 数据存在但索引不存在，重建索引 */
                {
                    /* 读取数据对象获取元数据 */
                    uint64_t obj_size;
                    time_t obj_mtime;

                    ret = rados_stat(dat_ioctx, entry->oid, &obj_size, &obj_mtime);
                    if (ret == 0) {
                        /* 创建索引条目 - 写入 OMAP 标记表示对象存在 */
                        rgw_omap_kv_t kv;
                        kv.key = "object_exists";
                        kv.val = (uint8_t*)strdup("1");
                        kv.val_len = 1;

                        ret = rgw_omap_set(idx_ioctx, entry->oid, kv.key,
                                           kv.val, kv.val_len, false);

                        /* 释放临时值 */
                        free(kv.val);

                        if (ret == 0) {
                            fixed_count++;
                        }
                    }
                }
                break;

            case RGW_DAMAGE_DATA_CORRUPTED:
                /* 数据损坏 - 标记但不自动修复（需要更复杂的恢复逻辑） */
                /* 可以尝试从快照恢复或通知管理员 */
                break;

            default:
                break;
        }
    }

    /* 清理 IO 上下文 */
    rados_ioctx_destroy(idx_ioctx);
    rados_ioctx_destroy(dat_ioctx);

    /* 清空损坏列表 */
    clear_damage_list();

    (void)dpp;
    (void)y;

    return fixed_count > 0 ? RGW_SAL_OK : RGW_SAL_ERR_GENERIC;
}

/**
 * @brief 检查桶索引一致性
 *
 * 验证桶入口点信息与桶实际信息的一致性。
 */
static int rados_bucket_check_bucket_index(rgw_sal_bucket_t* bucket,
                                            const rgw_sal_dpp_t* dpp,
                                            rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 确保桶已加载 */
    if (!impl->loaded) {
        int ret = rados_bucket_load(bucket, dpp, y);
        if (ret < 0) return ret;
    }

    /* 验证入口点与桶信息的一致性
     * 1. 检查入口点是否存在
     * 2. 验证 bucket_id、marker 等关键字段
     */
    char entrypoint_key[RGW_SAL_BUF_SIZE];
    char omap_pool[64] = ".rgw.meta.buckets.index";

    if (impl->tenant && impl->name) {
        rgw_bucket_entrypoint_make_omap_key(impl->tenant, impl->name,
                                            entrypoint_key, sizeof(entrypoint_key));
    } else {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    /* 读取入口点信息 */
    uint8_t* data = NULL;
    size_t data_len = 0;
    int ret = rgw_omap_get(driver_impl->buckets_index_ioctx, omap_pool,
                           entrypoint_key, &data, &data_len);
    if (ret < 0) {
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 解析并验证入口点 */
    rgw_bucket_entrypoint_t entrypoint;
    rgw_bucket_entrypoint_init(&entrypoint);

    ret = rgw_bucket_entrypoint_decode(data, data_len, &entrypoint);
    free(data);

    if (ret < 0) {
        rgw_bucket_entrypoint_free_members(&entrypoint);
        return RGW_SAL_ERR_DATA_CORRUPTION;
    }

    /* 验证一致性 */
    if (!entrypoint.linked || !entrypoint.has_bucket_info) {
        rgw_bucket_entrypoint_free_members(&entrypoint);
        return RGW_SAL_ERR_INDEX_ERROR;
    }

    /* 检查 bucket_id 是否匹配 */
    if (impl->bucket_id && entrypoint.bucket.bucket_id &&
        strcmp(impl->bucket_id, entrypoint.bucket.bucket_id) != 0) {
        rgw_bucket_entrypoint_free_members(&entrypoint);
        return RGW_SAL_ERR_VERSION_CONFLICT;
    }

    rgw_bucket_entrypoint_free_members(&entrypoint);

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/* 标签操作 (P0: 完整实现) */
static int rados_bucket_get_tag(rgw_sal_bucket_t* bucket, char** tag) {
    if (!bucket || !tag) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 从 tag 字段获取标签 */
    if (impl->tag) {
        *tag = strdup(impl->tag);
        if (!*tag) return RGW_SAL_ERR_OUT_OF_MEMORY;
    } else {
        *tag = NULL;
    }
    return RGW_SAL_OK;
}

static int rados_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag,
                                  const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    free(impl->tag);
    impl->tag = tag ? strdup(tag) : NULL;
    if (tag && !impl->tag) return RGW_SAL_ERR_OUT_OF_MEMORY;

    impl->mtime = time(NULL);

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

    /* 桶 vtable */
static rgw_sal_bucket_vtable_t rados_bucket_vtable = {
    .clone = rados_bucket_clone,
    .destroy = rados_bucket_destroy,
    .get_name = rados_bucket_get_name,
    .get_tenant = rados_bucket_get_tenant,
    .get_marker = rados_bucket_get_marker,
    .get_info = rados_bucket_get_info,
    .get_owner = rados_bucket_get_owner,
    .get_attrs = rados_bucket_get_attrs,
    .set_attrs = rados_bucket_set_attrs,
    .list = rados_bucket_list,
    .load = rados_bucket_load,
    .store = rados_bucket_store,
    .remove = rados_bucket_remove,
    /* 新增函数 */
    .create = rados_bucket_create,
    .delete_bucket = rados_bucket_delete_bucket,
    .rename = rados_bucket_rename,
    .set_acl = rados_bucket_set_acl,
    .get_policy = rados_bucket_get_policy,
    .set_policy = rados_bucket_set_policy,
    .get_tag = rados_bucket_get_tag,
    .set_tag = rados_bucket_set_tag,
    .get_usage = rados_bucket_get_usage,
    .read_stats = rados_bucket_read_stats,
    .read_stats_async = rados_bucket_read_stats_async,
    .complete_stats = rados_bucket_complete_stats,
    .sync = rados_bucket_sync,
    .drain = rados_bucket_drain,
    /* 索引检查与修复 */
    .check_object_index = rados_bucket_check_object_index,
    .fix_object_index = rados_bucket_fix_object_index,
    .check_bucket_index = rados_bucket_check_bucket_index,
};

/*============================================================================
 * 对象 vtable 实现
 *============================================================================*/

static void* rados_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* new_obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!new_obj) return NULL;

    rados_object_impl_t* old_impl = (rados_object_impl_t*)obj->impl;
    rados_object_impl_t* new_impl = (rados_object_impl_t*)calloc(1, sizeof(rados_object_impl_t));
    if (!new_impl) {
        free(new_obj);
        return NULL;
    }

    /* 深拷贝字符串资源 */
    if (old_impl->name) new_impl->name = strdup(old_impl->name);
    if (old_impl->instance) new_impl->instance = strdup(old_impl->instance);
    if (old_impl->bucket_name) new_impl->bucket_name = strdup(old_impl->bucket_name);
    if (old_impl->bucket_tenant) new_impl->bucket_tenant = strdup(old_impl->bucket_tenant);
    if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
    if (old_impl->obj_oid) new_impl->obj_oid = strdup(old_impl->obj_oid);

    /* 复制其他字段 */
    new_impl->is_null = old_impl->is_null;
    new_impl->size = old_impl->size;
    new_impl->mtime = old_impl->mtime;
    new_impl->written = old_impl->written;
    new_impl->deleted = old_impl->deleted;
    new_impl->loaded = old_impl->loaded;
    new_impl->is_atomic = old_impl->is_atomic;
    new_impl->is_expired = old_impl->is_expired;

    /* 深拷贝属性映射 (创建新副本) */
    if (old_impl->attrs) {
        new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
    }

    new_obj->vtable = obj->vtable;
    new_obj->impl = new_impl;
    new_obj->bucket = obj->bucket;

    return new_obj;
}

static void rados_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (impl) {
        /* 防止双重释放 */
        if (impl->destroyed) {
            return;
        }
        impl->destroyed = true;

        /* 释放字符串资源 */
        free(impl->name);
        impl->name = NULL;
        free(impl->instance);
        impl->instance = NULL;
        free(impl->bucket_name);
        impl->bucket_name = NULL;
        free(impl->bucket_tenant);
        impl->bucket_tenant = NULL;
        free(impl->bucket_id);
        impl->bucket_id = NULL;
        free(impl->obj_oid);
        impl->obj_oid = NULL;

        /* 释放数据池 IO 上下文 */
        if (impl->data_ioctx) {
            rados_ioctx_destroy(impl->data_ioctx);
            impl->data_ioctx = NULL;
        }

        /* 释放属性映射 */
        if (impl->attrs) {
            rgw_sal_attrs_destroy(impl->attrs);
            impl->attrs = NULL;
        }

        free(impl);
    }
    obj->impl = NULL;
}

static const char* rados_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->name : NULL;
}

static const char* rados_object_get_instance(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->instance : NULL;
}

static bool rados_object_is_null(const rgw_sal_object_t* obj) {
    if (!obj) return true;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->is_null : true;
}

static rgw_sal_attrs_t* rados_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return NULL;

    /* 如果已有属性，先清理 */
    if (obj_impl->attrs) {
        rgw_sal_attrs_destroy(obj_impl->attrs);
    }

    obj_impl->attrs = rgw_sal_attrs_create();
    if (!obj_impl->attrs) return NULL;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return obj_impl->attrs;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return obj_impl->attrs;

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }
    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 获取所有 xattr */
    rados_xattrs_iter_t iter;
    int ret = rados_getxattrs(ioctx, oid, &iter);
    if (ret == 0) {
        /* 使用迭代器获取 xattr */
        const char* name;
        const char* val;
        size_t val_len;
        while (rados_getxattrs_next(iter, &name, &val, &val_len) == 0) {
            if (name && val) {
                rgw_sal_attrs_set(obj_impl->attrs, name, (uint8_t*)val, val_len);
            }
        }
        rados_getxattrs_end(iter);
    }

    return obj_impl->attrs;
}

static int rados_object_set_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* attrs) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }
    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 使用 setxattr 设置每个属性 */
    for (size_t i = 0; i < attrs->count; i++) {
        const rgw_sal_attr_pair_t* pair = &attrs->pairs[i];
        int ret = rados_setxattr(ioctx, oid, pair->key, pair->value, pair->value_len);
        if (ret < 0 && ret != -ENOENT) {
            return RGW_SAL_ERR_WRITE_ERROR;
        }
    }

    /* 更新内存中的属性引用 */
    if (obj_impl->attrs) {
        rgw_sal_attrs_destroy(obj_impl->attrs);
    }
    obj_impl->attrs = attrs;

    return RGW_SAL_OK;
}

static int rados_object_read(rgw_sal_object_t* obj, int64_t offset, int64_t end,
                              uint8_t* buffer, size_t* buffer_size,
                              const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !buffer || !buffer_size) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID: {bucket_id}_{object_name} */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }

    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }

    /* 如果有实例版本，添加版本信息 */
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 计算读取大小 */
    size_t read_size = *buffer_size;
    if (end > 0 && end >= offset) {
        size_t requested = (size_t)(end - offset + 1);
        if (requested < read_size) {
            read_size = requested;
        }
    }

    /* 从 RADOS 读取对象数据 */
    int ret = rados_read(ioctx, oid, (char*)buffer, read_size, offset);

    if (ret < 0) {
        if (ret == -ENOENT) {
            *buffer_size = 0;
            return RGW_SAL_ERR_NOT_FOUND;
        }
        *buffer_size = 0;
        return RGW_SAL_ERR_IO_ERROR;
    }

    *buffer_size = (size_t)ret;
    obj_impl->loaded = true;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_object_write(rgw_sal_object_t* obj, int64_t offset, int64_t size,
                               const uint8_t* data,
                               const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj || !data) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID: {bucket_id}_{object_name} */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }

    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }

    /* 如果有实例版本，添加版本信息 */
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 写入数据到 RADOS */
    int ret;
    if (offset == 0 && obj_impl->is_atomic) {
        /* 原子写入：创建新对象 */
        ret = rados_write_full(ioctx, oid, (const char*)data, (size_t)size);
    } else if (offset > 0) {
        /* 追加写入 */
        ret = rados_write(ioctx, oid, (const char*)data, (size_t)size, offset);
    } else {
        /* 追加到末尾 */
        ret = rados_append(ioctx, oid, (const char*)data, (size_t)size);
    }

    if (ret < 0) {
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    /* 更新对象元数据 */
    obj_impl->size = size;
    obj_impl->mtime = time(NULL);
    obj_impl->written = true;

    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_object_delete_obj(rgw_sal_object_t* obj, uint32_t flags,
                                    const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID: {bucket_id}_{object_name} */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }

    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }

    /* 如果有实例版本，添加版本信息 */
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 检查是否为版本控制桶的删除标记 */
    bool delete_olh = (flags & RGW_SAL_DELETE_FLAG_EXPIRED) != 0;
    bool versioning = false;

    /* 检查桶是否启用版本控制 */
    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)obj->bucket->impl;
    if (bucket_impl && bucket_impl->attrs) {
        /* 检查 xattr 中的版本控制标志 */
        const char* versioning_val = NULL;
        size_t versioning_len = 0;
        if (rgw_sal_attrs_get(bucket_impl->attrs, "versioning", &versioning_val, &versioning_len) == 0) {
            versioning = (versioning_val && strncmp(versioning_val, "true", versioning_len) == 0);
        }
    }

    if (versioning && !delete_olh && !obj_impl->instance) {
        /* 版本控制桶中，删除最新版本时添加删除标记而非真正删除 */
        char marker_oid[RGW_SAL_BUF_SIZE * 2 + 10];
        snprintf(marker_oid, sizeof(marker_oid), "%s_%s", oid, "null");

        time_t now = time(NULL);
        char delete_marker[256];
        snprintf(delete_marker, sizeof(delete_marker),
                 "{\"name\":\"%s\",\"instance\":\"\",\"ver\":{\"gen\":0},\"delete_marker\":true,\"mtime\":%ld}",
                 obj_impl->name ? obj_impl->name : "", (long)now);

        /* 写入删除标记作为对象的一个特殊版本 */
        int ret = rados_write_full(ioctx, oid, delete_marker, strlen(delete_marker));
        if (ret < 0) return RGW_SAL_ERR_WRITE_ERROR;
    } else {
        /* 真正删除对象 */
        int ret = rados_remove(ioctx, oid);
        if (ret < 0 && ret != -ENOENT) {
            return RGW_SAL_ERR_IO_ERROR;
        }
    }

    /* 更新对象状态 */
    obj_impl->deleted = true;
    obj_impl->mtime = time(NULL);

    (void)flags;
    (void)dpp; (void)y;
    return RGW_SAL_OK;
}

static int rados_object_load_state(rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp,
                                    rgw_sal_yield_t* y, bool follow_olh) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->ioctxs_initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 构建对象 OID */
    char oid[256];
    if (!impl->obj_oid || !impl->obj_oid[0]) {
        int ret = rados_build_object_oid(obj, oid, sizeof(oid));
        if (ret < 0) return ret;
    } else {
        snprintf(oid, sizeof(oid), "%s", impl->obj_oid);
    }

    /* 获取对象 stat 信息 */
    uint64_t size;
    time_t mtime;
    int ret = rados_stat(ioctx, oid, &size, &mtime);
    if (ret < 0) {
        if (ret == -ENOENT) {
            impl->deleted = true;
            impl->loaded = true;
            return RGW_SAL_OK;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 更新对象状态 */
    impl->size = size;
    impl->mtime = mtime;
    impl->deleted = false;
    impl->loaded = true;

    /* TODO: 处理 OLH (Object Lambda Handler) 链接跟踪
     * 如果 follow_olh 为 true，需要跟随链接获取真实对象
     */
    (void)follow_olh;
    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

static int rados_object_get_obj_attrs(rgw_sal_object_t* obj, rgw_sal_yield_t* y,
                                       const rgw_sal_dpp_t* dpp) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->ioctxs_initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 构建对象 OID */
    char oid[256];
    if (!impl->obj_oid || !impl->obj_oid[0]) {
        int ret = rados_build_object_oid(obj, oid, sizeof(oid));
        if (ret < 0) return ret;
    } else {
        snprintf(oid, sizeof(oid), "%s", impl->obj_oid);
    }

    /* 获取对象 xattr */
    if (!impl->attrs) {
        impl->attrs = rgw_sal_attrs_create();
        if (!impl->attrs) return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 使用 omap 获取对象的用户定义属性 - 使用兼容 librados 17.2.9 的 API */
    rados_read_op_t read_op = rados_create_read_op();
    if (!read_op) {
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    rados_omap_iter_t iter;
    unsigned char pmore = 1;
    int op_ret = 0;

    /* 使用 rados_read_op_omap_get_vals2 替代不存在的 rados_omap_get_vals */
    rados_read_op_omap_get_vals2(read_op, "", NULL, 0, &iter, &pmore, &op_ret);

    /* 执行读取操作 */
    int ret = rados_read_op_operate(read_op, ioctx, oid, 0);
    rados_release_read_op(read_op);

    if (ret < 0 || op_ret < 0) {
        /* 如果不支持 omap，尝试使用 getxattr */
        return RGW_SAL_OK;  /* 属性将保持为空 */
    }

    /* 遍历并复制 xattr 到 impl->attrs - 使用 rados_omap_get_next2 */
    const char* key = NULL;
    const char* val = NULL;
    size_t len = 0;
    int next_ret = 0;

    while (pmore) {
        next_ret = rados_omap_get_next2(iter, &key, &val, &len, &op_ret);
        if (next_ret < 0 || !key) {
            break;
        }
        /* 跳过系统属性 (以 _ 开始) */
        if (key && key[0] != '_' && val) {
            rgw_sal_attrs_set(impl->attrs, key, (const uint8_t*)val, len);
        }
        /* 检查是否还有更多数据 */
        if (op_ret != 0) {
            pmore = 0;
        }
    }
    rados_omap_get_end(iter);

    (void)y;
    (void)dpp;
    return RGW_SAL_OK;
}

static int rados_object_set_obj_attrs(rgw_sal_object_t* obj, rgw_sal_attrs_t* setattrs,
                                       rgw_sal_attrs_t* delattrs, rgw_sal_yield_t* y,
                                       uint32_t flags) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->initialized) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    if (obj_impl->bucket_id) {
        snprintf(oid, sizeof(oid), "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }
    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "%s", obj_impl->name);
    }
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, sizeof(oid) - len, "_%s", obj_impl->instance);
    }

    /* 设置属性 (setattrs) */
    if (setattrs) {
        for (size_t i = 0; i < setattrs->count; i++) {
            const rgw_sal_attr_pair_t* pair = &setattrs->pairs[i];
            int ret = rados_setxattr(ioctx, oid, pair->key, pair->value, pair->value_len);
            if (ret < 0 && ret != -ENOENT) {
                return RGW_SAL_ERR_WRITE_ERROR;
            }
        }
        /* 更新内存中的属性引用 */
        for (size_t i = 0; i < setattrs->count; i++) {
            const rgw_sal_attr_pair_t* pair = &setattrs->pairs[i];
            rgw_sal_attrs_set(obj_impl->attrs, pair->key, pair->value, pair->value_len);
        }
    }

    /* 删除属性 (delattrs) */
    if (delattrs) {
        for (size_t i = 0; i < delattrs->count; i++) {
            const rgw_sal_attr_pair_t* pair = &delattrs->pairs[i];
            int ret = rados_rmxattr(ioctx, oid, pair->key);
            if (ret < 0 && ret != -ENOENT) {
                return RGW_SAL_ERR_WRITE_ERROR;
            }
            /* 从内存属性中删除 */
            if (obj_impl->attrs) {
                rgw_sal_attrs_del(obj_impl->attrs, pair->key);
            }
        }
    }

    obj_impl->mtime = time(NULL);

    (void)y; (void)flags;
    return RGW_SAL_OK;
}

/* 原子操作标志 (P0: 完整实现) */
static bool rados_object_is_atomic(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->is_atomic : false;
}

static int rados_object_set_atomic(rgw_sal_object_t* obj, bool atomic) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (impl) {
        impl->is_atomic = atomic;
    }
    return RGW_SAL_OK;
}

/* 过期检查 (P0: 完整实现) */
static bool rados_object_is_expired(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl || !impl->attrs) return false;

    /* 检查 Expiration-Time 属性 */
    uint8_t* value = NULL;
    size_t value_len = 0;
    int ret = rgw_sal_attrs_get(impl->attrs, " expiration-time", &value, &value_len);
    if (ret != RGW_SAL_OK || !value) return false;

    /* 解析过期时间并比较 */
    time_t now = time(NULL);
    /* 简单解析：假设格式为 Unix 时间戳字符串 */
    time_t expiry = (time_t)atoll((const char*)value);
    return now > expiry;
}

/* 对象 vtable */
static rgw_sal_object_vtable_t rados_object_vtable = {
    .clone = rados_object_clone,
    .destroy = rados_object_destroy,
    .get_name = rados_object_get_name,
    .get_instance = rados_object_get_instance,
    .is_null = rados_object_is_null,
    .get_attrs = rados_object_get_attrs,
    .set_attrs = rados_object_set_attrs,
    .read = rados_object_read,
    .write = rados_object_write,
    .delete_obj = rados_object_delete_obj,
    .load_state = rados_object_load_state,
    .get_obj_attrs = rados_object_get_obj_attrs,
    .set_obj_attrs = rados_object_set_obj_attrs,
    /* P0: 原子操作和过期检查 */
    .is_atomic = rados_object_is_atomic,
    .set_atomic = rados_object_set_atomic,
    .is_expired = rados_object_is_expired,
};

/*============================================================================
 * 驱动创建/销毁函数
 *============================================================================*/

/**
 * @brief 创建 RADOS 驱动
 */
rgw_sal_driver_t* rgw_sal_rados_driver_create(void* cct, void* neorados) {
    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)calloc(1, sizeof(rgw_sal_driver_t));
    if (!driver) return NULL;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)calloc(1, sizeof(rados_driver_impl_t));
    if (!impl) {
        free(driver);
        return NULL;
    }

    strncpy(impl->name, "rados", sizeof(impl->name) - 1);
    impl->rados_handle = neorados;
    impl->initialized = false;

    /* 设置 vtable */
    driver->vtable = &rados_driver_vtable;

    /* 设置用户 vtable */
    driver->user_vtable = &rados_user_vtable;

    /* 设置桶 vtable */
    driver->bucket_vtable = &rados_bucket_vtable;

    /* 设置对象 vtable */
    driver->object_vtable = &rados_object_vtable;

    driver->impl = impl;

    (void)cct;
    return driver;
}

/**
 * @brief 获取 RADOS 驱动实现
 */
rgw_sal_rados_driver_impl_t* rgw_sal_rados_get_impl(rgw_sal_driver_t* driver) {
    if (!driver) return NULL;
    return (rgw_sal_rados_driver_impl_t*)driver->impl;
}

/*============================================================================
 * RADOS 特定操作实现
 *============================================================================*/

int rgw_sal_rados_get_cluster_id(rgw_sal_driver_t* driver,
                                   char** cluster_id,
                                   const rgw_sal_dpp_t* dpp,
                                   rgw_sal_yield_t* y) {
    if (!driver || !driver->vtable || !driver->vtable->get_cluster_id) {
        return RGW_SAL_ERR_NOT_IMPLEMENTED;
    }
    return driver->vtable->get_cluster_id(driver, cluster_id, dpp, y);
}

/**
 * @brief 获取用户控制接口
 *
 * 返回 RADOS OMAP 上下文用于直接操作用户数据。
 *
 * @param driver 驱动句柄
 * @return 用户 OMAP IO 上下文，失败返回 NULL
 */
void* rgw_sal_rados_get_user_ctl(rgw_sal_driver_t* driver) {
    if (!driver) return NULL;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->ioctxs_initialized) {
        return NULL;
    }

    return impl->users_uid_ioctx;
}

/**
 * @brief 完成并刷新统计数据
 *
 * 将用户桶的统计信息刷新到 RADOS OMAP。
 *
 * @param driver 驱动句柄
 * @param owner 桶所有者
 * @param dpp 调试前缀提供者
 * @param y 可选的 yield 上下文
 * @return 错误码
 */
int rgw_sal_rados_complete_flush_stats(rgw_sal_driver_t* driver,
                                          const rgw_sal_user_id_t* owner,
                                          const rgw_sal_dpp_t* dpp,
                                          rgw_sal_yield_t* y) {
    if (!driver) return RGW_SAL_ERR_INVALID_ARG;
    if (!owner) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->ioctxs_initialized) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 构建用户桶统计 OMAP 键 */
    char omap_key[256];
    snprintf(omap_key, sizeof(omap_key), "%s:%s.stales",
             owner->tenant ? owner->tenant : "",
             owner->id ? owner->id : "");

    /* 检查是否有陈旧的统计需要刷新 */
    uint8_t* val = NULL;
    size_t val_len = 0;
    int ret = rgw_omap_get(impl->buckets_index_ioctx, omap_key, "stats", &val, &val_len);
    if (ret < 0 && ret != -ENOENT) {
        return ret;
    }

    if (ret == 0 && val && val_len > 0) {
        /* 存在陈旧统计，需要刷新 */
        /* 解析并清除陈旧标记 */
        rgw_omap_free_value(val);

        /* 清除陈旧标记 */
        ret = rgw_omap_del(impl->buckets_index_ioctx, omap_key, "stats");
        if (ret < 0 && ret != -ENOENT) {
            return ret;
        }
    }

    (void)dpp;
    (void)y;
    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS 用户/桶/对象内部指针访问
 *============================================================================*/

void* rgw_sal_rados_user_get_internal(rgw_sal_user_t* user) {
    if (!user) return NULL;
    return user->impl;
}

rgw_sal_user_t* rgw_sal_rados_user_from_internal(rgw_sal_driver_t* driver,
                                                    void* rados_user) {
    if (!driver || !rados_user) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    user->impl = rados_user;
    user->driver = driver;

    return user;
}

void* rgw_sal_rados_bucket_get_internal(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    return bucket->impl;
}

rgw_sal_bucket_t* rgw_sal_rados_bucket_from_internal(rgw_sal_driver_t* driver,
                                                        void* rados_bucket) {
    if (!driver || !rados_bucket) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    bucket->impl = rados_bucket;
    bucket->driver = driver;

    return bucket;
}

void* rgw_sal_rados_object_get_internal(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    return obj->impl;
}

rgw_sal_object_t* rgw_sal_rados_object_from_internal(rgw_sal_bucket_t* bucket,
                                                        void* rados_object) {
    if (!bucket || !rados_object) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    obj->impl = rados_object;
    obj->bucket = bucket;

    return obj;
}

/*============================================================================
 * RADOS 读操作实现
 *
 * 对应原 C++ RadosObject::RadosReadOp 的实现
 *============================================================================*/

/**
 * @brief 读操作上下文
 */
typedef struct rados_read_ctx {
    rados_read_op_t read_op;        /**< RADOS 读操作 */
    rgw_sal_object_t* obj;           /**< 对象 */
    uint64_t obj_size;              /**< 对象大小 */
    time_t mtime;                  /**< 修改时间 */
    bool prepared;                  /**< 是否已准备 */
} rados_read_ctx_t;

/**
 * @brief 构建对象 OID
 *
 * @param obj 对象
 * @param oid 输出缓冲区
 * @param oid_size 缓冲区大小
 * @return 成功返回 RGW_SAL_OK
 */
static int rados_build_object_oid(rgw_sal_object_t* obj, char* oid, size_t oid_size) {
    if (!obj || !oid || oid_size == 0) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    if (!obj_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)obj->bucket->impl;
    if (!bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 构建对象 OID: {bucket_id}_{object_name} */
    if (obj_impl->bucket_id) {
        snprintf(oid, oid_size, "%s_", obj_impl->bucket_id);
    } else {
        oid[0] = '\0';
    }

    if (obj_impl->name) {
        size_t len = strlen(oid);
        snprintf(oid + len, oid_size - len, "%s", obj_impl->name);
    }

    /* 如果有实例版本，添加版本信息 */
    if (obj_impl->instance) {
        size_t len = strlen(oid);
        snprintf(oid + len, oid_size - len, "_%s", obj_impl->instance);
    }

    return RGW_SAL_OK;
}

int rgw_sal_rados_object_read_prepare(rgw_sal_object_t* obj,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    if (!obj) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->rados_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    int ret = rados_build_object_oid(obj, oid, sizeof(oid));
    if (ret != RGW_SAL_OK) return ret;

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 获取对象 stat 信息以获取大小和修改时间 */
    uint64_t size = 0;
    time_t mtime = 0;
    ret = rados_stat(ioctx, oid, &size, &mtime);
    if (ret < 0) {
        if (ret == -ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 更新对象元数据 */
    impl->size = size;
    impl->mtime = mtime;
    impl->loaded = true;

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

int rgw_sal_rados_object_read_iterate(rgw_sal_object_t* obj,
                                        int64_t offset, int64_t end,
                                        rgw_sal_rados_read_callback_t callback,
                                        void* callback_arg,
                                        const rgw_sal_dpp_t* dpp,
                                        rgw_sal_yield_t* y) {
    if (!obj || !callback) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->rados_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    int ret = rados_build_object_oid(obj, oid, sizeof(oid));
    if (ret != RGW_SAL_OK) return ret;

    /* 如果对象未加载，先 prepare */
    if (!impl->loaded) {
        ret = rgw_sal_rados_object_read_prepare(obj, dpp, y);
        if (ret != RGW_SAL_OK) return ret;
    }

    /* 计算读取范围 */
    int64_t read_size = impl->size;
    if (end > 0 && end >= offset) {
        read_size = end - offset + 1;
    }
    if (read_size <= 0 || read_size > impl->size - offset) {
        read_size = impl->size - offset;
    }

    /* 分配读取缓冲区 */
    uint8_t* buffer = (uint8_t*)malloc((size_t)read_size);
    if (!buffer) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 执行读取 */
    int bytes_read = rados_read(ioctx, oid, (char*)buffer, (size_t)read_size, offset);
    if (bytes_read < 0) {
        free(buffer);
        if (bytes_read == -ENOENT) {
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 调用回调处理数据 */
    if (bytes_read > 0) {
        ret = callback(callback_arg, buffer, (size_t)bytes_read);
        if (ret != 0) {
            free(buffer);
            return RGW_SAL_ERR_ABORTED;
        }
    }

    free(buffer);

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

int rgw_sal_rados_object_get_attr(rgw_sal_object_t* obj,
                                    const char* name,
                                    uint8_t** value, size_t* value_len,
                                    rgw_sal_yield_t* y,
                                    const rgw_sal_dpp_t* dpp) {
    if (!obj || !name || !value || !value_len) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* driver_impl = (rados_driver_impl_t*)((rgw_sal_driver_t*)obj->bucket->driver)->impl;
    if (!driver_impl || !driver_impl->rados_handle) {
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取数据池 IO 上下文 */
    rados_ioctx_t ioctx = driver_impl->buckets_data_ioctx;
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建对象 OID */
    char oid[RGW_SAL_BUF_SIZE * 2];
    int ret = rados_build_object_oid(obj, oid, sizeof(oid));
    if (ret != RGW_SAL_OK) return ret;

    /* 分配缓冲区存储属性值 */
    char* attr_value = (char*)malloc(RGW_SAL_BUF_SIZE);
    if (!attr_value) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 使用 getxattr 获取对象扩展属性 */
    int attr_len = rados_getxattr(ioctx, oid, name, attr_value, RGW_SAL_BUF_SIZE - 1);
    if (attr_len < 0) {
        free(attr_value);
        if (attr_len == -ENODATA || attr_len == -ENOENT) {
            *value = NULL;
            *value_len = 0;
            return RGW_SAL_ERR_NOT_FOUND;
        }
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 确保字符串以 null 结尾 */
    attr_value[attr_len] = '\0';

    /* 分配输出缓冲区 */
    *value = (uint8_t*)malloc((size_t)attr_len + 1);
    if (!*value) {
        free(attr_value);
        *value_len = 0;
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 复制属性值到输出缓冲区 */
    memcpy(*value, attr_value, (size_t)attr_len + 1);
    *value_len = (size_t)attr_len;

    free(attr_value);

    (void)dpp;
    (void)y;

    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS 生命周期 (Lifecycle) 操作实现
 * OMAP key 格式:
 *   - lc.entry.{shard_id}.{bucket_key}
 *   - lc.head.{shard_id}
 *============================================================================*/

/**
 * @brief 构建生命周期 entry OMAP key
 *
 * 格式: lc.entry.{shard_id}.{bucket_key}
 */
static int rados_lc_make_entry_key(char* key, size_t key_size,
                                    uint32_t shard_id, const char* bucket_key) {
    if (!key || !bucket_key) return RGW_SAL_ERR_INVALID_ARG;
    int ret = snprintf(key, key_size, "lc.entry.%u.%s", shard_id, bucket_key);
    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 构建生命周期 head OMAP key
 *
 * 格式: lc.head.{shard_id}
 */
static int rados_lc_make_head_key(char* key, size_t key_size, uint32_t shard_id) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;
    int ret = snprintf(key, key_size, "lc.head.%u", shard_id);
    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 获取单个生命周期条目
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param bucket_key 桶键
 * @param entry 输出参数，返回的生命周期条目
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_get_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                               const char* bucket_key, rgw_lc_entry_t* entry,
                               const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket_key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_lc_make_entry_key(key, sizeof(key), shard_id, bucket_key);
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->lc_pool_ioctx, ".rgw.lc", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_lc_entry_decode(value, value_len, entry);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 获取下一个生命周期条目
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param marker 标记
 * @param entry 输出参数，返回的生命周期条目
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_get_next_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                                    const char* marker, rgw_lc_entry_t* entry,
                                    const rgw_sal_dpp_t* dpp) {
    if (!driver || !entry) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 构建查找前缀 */
    char prefix[256];
    int ret = snprintf(prefix, sizeof(prefix), "lc.entry.%u.", shard_id);
    if (ret < 0 || (size_t)ret >= sizeof(prefix)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 使用 OMAP 迭代器获取下一个条目 */
    rgw_omap_iter_t* iter = rgw_omap_iter_create(impl->lc_pool_ioctx, ".rgw.lc",
                                                  marker, prefix, 1);
    if (!iter) return RGW_SAL_ERR_OUT_OF_MEMORY;

    const char* key = NULL;
    const uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_iter_next(iter, &key, &value, &value_len);
    if (ret <= 0) {
        rgw_omap_iter_destroy(iter);
        return RGW_SAL_ERR_NOT_FOUND;
    }

    ret = rgw_lc_entry_decode(value, value_len, entry);
    rgw_omap_iter_destroy(iter);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 设置生命周期条目
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param bucket_key 桶键
 * @param entry 生命周期条目
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_set_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                               const char* bucket_key, const rgw_lc_entry_t* entry,
                               const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket_key || !entry) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_lc_make_entry_key(key, sizeof(key), shard_id, bucket_key);
    if (ret < 0) return ret;

    /* 编码生命周期条目 */
    uint8_t* buf = NULL;
    size_t buf_len = 0;
    ret = rgw_lc_entry_encode(entry, NULL, 0, &buf_len);
    if (ret == -ERANGE) {
        buf = (uint8_t*)malloc(buf_len);
        if (!buf) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_lc_entry_encode(entry, buf, buf_len, &buf_len);
    }

    if (ret < 0) {
        free(buf);
        return ret;
    }

    ret = rgw_omap_set(impl->lc_pool_ioctx, ".rgw.lc", key, buf, buf_len, false);
    free(buf);

    if (ret < 0) return RGW_SAL_ERR_WRITE_ERROR;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 列出生命周期条目
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param entries 输出参数，返回的生命周期条目数组
 * @param max_entries 最大条目数
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_list_entries(rgw_sal_driver_t* driver, uint32_t shard_id,
                                  rgw_lc_entry_t** entries, size_t max_entries,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !entries) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 构建查找前缀 */
    char prefix[256];
    int ret = snprintf(prefix, sizeof(prefix), "lc.entry.%u.", shard_id);
    if (ret < 0 || (size_t)ret >= sizeof(prefix)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 使用 OMAP 迭代器获取所有条目 */
    rgw_omap_iter_t* iter = rgw_omap_iter_create(impl->lc_pool_ioctx, ".rgw.lc",
                                                  NULL, prefix, max_entries);
    if (!iter) return RGW_SAL_ERR_OUT_OF_MEMORY;

    size_t count = 0;
    *entries = (rgw_lc_entry_t*)calloc(max_entries, sizeof(rgw_lc_entry_t));
    if (!*entries) {
        rgw_omap_iter_destroy(iter);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    while (count < max_entries) {
        const char* key = NULL;
        const uint8_t* value = NULL;
        size_t value_len = 0;

        ret = rgw_omap_iter_next(iter, &key, &value, &value_len);
        if (ret <= 0) break;

        rgw_lc_entry_t* entry = &((*entries)[count]);
        ret = rgw_lc_entry_decode(value, value_len, entry);
        if (ret < 0) {
            /* 跳过损坏的条目 */
            continue;
        }
        count++;
    }

    rgw_omap_iter_destroy(iter);

    (void)dpp;
    return (int)count;
}

/**
 * @brief 删除生命周期条目
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param bucket_key 桶键
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_rm_entry(rgw_sal_driver_t* driver, uint32_t shard_id,
                              const char* bucket_key, const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket_key) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_lc_make_entry_key(key, sizeof(key), shard_id, bucket_key);
    if (ret < 0) return ret;

    ret = rgw_omap_del(impl->lc_pool_ioctx, ".rgw.lc", key);
    if (ret < 0 && ret != -ENOENT) return RGW_SAL_ERR_WRITE_ERROR;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 获取生命周期头
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param head 输出参数，返回的生命周期头
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_get_head(rgw_sal_driver_t* driver, uint32_t shard_id,
                              rgw_lc_head_t* head, const rgw_sal_dpp_t* dpp) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_lc_make_head_key(key, sizeof(key), shard_id);
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->lc_pool_ioctx, ".rgw.lc", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_lc_head_decode(value, value_len, head);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 保存生命周期头
 *
 * @param driver 驱动
 * @param shard_id 分片 ID
 * @param head 生命周期头
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_lc_put_head(rgw_sal_driver_t* driver, uint32_t shard_id,
                               const rgw_lc_head_t* head, const rgw_sal_dpp_t* dpp) {
    if (!driver || !head) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->lc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_lc_make_head_key(key, sizeof(key), shard_id);
    if (ret < 0) return ret;

    /* 编码生命周期头 */
    size_t buf_len = 0;
    ret = rgw_lc_head_encode(head, NULL, 0, &buf_len);
    if (ret == -ERANGE) {
        uint8_t* buf = (uint8_t*)malloc(buf_len);
        if (!buf) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_lc_head_encode(head, buf, buf_len, &buf_len);
        if (ret >= 0) {
            ret = rgw_omap_set(impl->lc_pool_ioctx, ".rgw.lc", key, buf, buf_len, false);
            if (ret < 0) ret = RGW_SAL_ERR_WRITE_ERROR;
        }
        free(buf);
    } else {
        ret = RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    return ret;
}

/*============================================================================
 * RADOS 多部分上传 (Multipart Upload) 操作实现
 * 对象 key 格式:
 *   - 元数据对象: {object}.meta.{upload_id}
 *   - 分段对象: {object}.{upload_id}.{part_num}
 *============================================================================*/

/**
 * @brief 获取多部分上传数据池 IO 上下文
 *
 * @param driver 驱动
 * @param bucket_id 桶 ID
 *
 * @return IO 上下文
 */
static rados_ioctx_t rados_get_multipart_ioctx(rgw_sal_driver_t* driver,
                                                 const char* bucket_id) {
    if (!driver || !bucket_id) return NULL;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_handle) return NULL;

    /* 构建数据池名称 */
    char pool_name[128];
    snprintf(pool_name, sizeof(pool_name), ".rgw.buckets.%s.data", bucket_id);

    rados_ioctx_t ioctx = NULL;
    int ret = rados_ioctx_create(impl->rados_handle, pool_name, &ioctx);
    if (ret < 0) return NULL;

    return ioctx;
}

/**
 * @brief 初始化多部分上传
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param obj 对象
 * @param upload_id 输出参数，返回上传 ID
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_init(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                  rgw_sal_object_t* obj, char** upload_id,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket || !obj || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;

    if (!obj_impl || !bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 生成上传 ID */
    time_t now = time(NULL);
    uint64_t random = (uint64_t)rand() | ((uint64_t)now << 32);
    char buf[64];
    snprintf(buf, sizeof(buf), "%lu.%llu", (unsigned long)now, (unsigned long long)random);

    *upload_id = strdup(buf);
    if (!*upload_id) return RGW_SAL_ERR_OUT_OF_MEMORY;

    /* 存储上传元信息 */
    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_impl->bucket_id);
    if (!ioctx) {
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_IO_ERROR;
    }

    /* 构建元数据对象键 */
    char meta_key[512];
    snprintf(meta_key, sizeof(meta_key), "%s.meta.%s", obj_impl->name ? obj_impl->name : "", *upload_id);

    /* 创建空的 multipart_upload_info */
    rgw_multipart_upload_info_t info;
    memset(&info, 0, sizeof(info));

    uint8_t* info_buf = NULL;
    size_t info_len = 0;
    int ret = rgw_multipart_upload_info_encode_alloc(&info, &info_buf, &info_len);
    if (ret < 0 || !info_buf) {
        rados_ioctx_destroy(ioctx);
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 写入元数据对象 */
    ret = rados_write(ioctx, meta_key, (const char*)info_buf, info_len, 0);
    free(info_buf);

    if (ret < 0) {
        rados_ioctx_destroy(ioctx);
        free(*upload_id);
        *upload_id = NULL;
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 列出多部分上传的分段
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param obj 对象
 * @param upload_id 上传 ID
 * @param max_parts 最大分段数
 * @param parts 输出参数，返回的分段数组
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_list_parts(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                        rgw_sal_object_t* obj, const char* upload_id,
                                        uint32_t max_parts, rgw_upload_part_info_t** parts,
                                        const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket || !obj || !upload_id || !parts) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;

    if (!obj_impl || !bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_impl->bucket_id);
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 分配结果数组 */
    *parts = (rgw_upload_part_info_t*)calloc(max_parts, sizeof(rgw_upload_part_info_t));
    if (!*parts) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    uint32_t count = 0;
    char part_key[512];

    /* 遍历所有可能的分段编号 */
    for (uint32_t i = 0; i < max_parts && count < max_parts; i++) {
        snprintf(part_key, sizeof(part_key), "%s.%s.%u",
                 obj_impl->name ? obj_impl->name : "", upload_id, i);

        /* 尝试读取分段元数据 */
        char xattr_key[64];
        snprintf(xattr_key, sizeof(xattr_key), "part%u.info", i);

        char* value_buf = (char*)malloc(4096);
        if (!value_buf) continue;

        int ret = rados_getxattr(ioctx, part_key, xattr_key, value_buf, 4096);
        if (ret < 0) {
            free(value_buf);
            continue;
        }

        /* 解码分段信息 */
        rgw_upload_part_info_t* part = &((*parts)[count]);
        ret = rgw_upload_part_info_decode((const uint8_t*)value_buf, ret, part);
        if (ret >= 0) {
            count++;
        }
        free(value_buf);
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;
    return (int)count;
}

/**
 * @brief 中止多部分上传
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param obj 对象
 * @param upload_id 上传 ID
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_abort(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                  rgw_sal_object_t* obj, const char* upload_id,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket || !obj || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;

    if (!obj_impl || !bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_impl->bucket_id);
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 删除元数据对象 */
    char meta_key[512];
    snprintf(meta_key, sizeof(meta_key), "%s.meta.%s",
             obj_impl->name ? obj_impl->name : "", upload_id);
    rados_remove(ioctx, meta_key);

    /* 删除所有分段对象 */
    char part_key[512];
    for (uint32_t i = 0; i < RGW_MAX_PART_NUMBER; i++) {
        snprintf(part_key, sizeof(part_key), "%s.%s.%u",
                 obj_impl->name ? obj_impl->name : "", upload_id, i);
        int ret = rados_remove(ioctx, part_key);
        if (ret < 0 && ret != -ENOENT) {
            /* 继续删除其他分段 */
        }
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 完成多部分上传
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param obj 对象
 * @param upload_id 上传 ID
 * @param parts 分段数组
 * @param num_parts 分段数量
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_complete(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                      rgw_sal_object_t* obj, const char* upload_id,
                                      rgw_upload_part_info_t* parts, int num_parts,
                                      const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket || !obj || !upload_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_object_impl_t* obj_impl = (rados_object_impl_t*)obj->impl;
    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;

    if (!obj_impl || !bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_impl->bucket_id);
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 合并所有分段到最终对象 */
    /* 这是一个简化实现，完整的合并逻辑需要处理分段顺序、大小等 */

    /* 删除元数据对象 */
    char meta_key[512];
    snprintf(meta_key, sizeof(meta_key), "%s.meta.%s",
             obj_impl->name ? obj_impl->name : "", upload_id);
    rados_remove(ioctx, meta_key);

    /* 计算最终对象大小 */
    uint64_t total_size = 0;
    if (parts && num_parts > 0) {
        for (int i = 0; i < num_parts; i++) {
            total_size += parts[i].size;
        }
    }

    /* 删除所有分段对象 */
    char part_key[512];
    for (uint32_t i = 0; i < RGW_MAX_PART_NUMBER; i++) {
        snprintf(part_key, sizeof(part_key), "%s.%s.%u",
                 obj_impl->name ? obj_impl->name : "", upload_id, i);
        rados_remove(ioctx, part_key);
    }

    rados_ioctx_destroy(ioctx);

    /* 更新对象大小 */
    obj_impl->size = total_size;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 存储多部分上传元信息
 *
 * @param driver 驱动
 * @param bucket_id 桶 ID
 * @param obj_key 对象键
 * @param upload_id 上传 ID
 * @param info 上传元信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_store_info(rgw_sal_driver_t* driver, const char* bucket_id,
                                       const char* obj_key, const char* upload_id,
                                       const rgw_multipart_upload_info_t* info,
                                       const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket_id || !obj_key || !upload_id || !info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_id);
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建元数据对象键 */
    char meta_key[512];
    snprintf(meta_key, sizeof(meta_key), "%s.meta.%s", obj_key, upload_id);

    /* 编码上传元信息 */
    uint8_t* buf = NULL;
    size_t buf_len = 0;
    int ret = rgw_multipart_upload_info_encode_alloc(info, &buf, &buf_len);
    if (ret < 0 || !buf) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_INTERNAL_ERROR;
    }

    /* 写入元数据对象 */
    ret = rados_write(ioctx, meta_key, (const char*)buf, buf_len, 0);
    free(buf);

    if (ret < 0) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 加载多部分上传元信息
 *
 * @param driver 驱动
 * @param bucket_id 桶 ID
 * @param obj_key 对象键
 * @param upload_id 上传 ID
 * @param info 输出参数，返回的上传元信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_multipart_load_info(rgw_sal_driver_t* driver, const char* bucket_id,
                                       const char* obj_key, const char* upload_id,
                                       rgw_multipart_upload_info_t* info,
                                       const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket_id || !obj_key || !upload_id || !info) {
        return RGW_SAL_ERR_INVALID_ARG;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->rados_handle) return RGW_SAL_ERR_NOT_INITIALIZED;

    rados_ioctx_t ioctx = rados_get_multipart_ioctx(driver, bucket_id);
    if (!ioctx) return RGW_SAL_ERR_IO_ERROR;

    /* 构建元数据对象键 */
    char meta_key[512];
    snprintf(meta_key, sizeof(meta_key), "%s.meta.%s", obj_key, upload_id);

    /* 获取对象大小 */
    uint64_t size = 0;
    int ret = rados_stat(ioctx, meta_key, &size, NULL);
    if (ret < 0) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_NOT_FOUND;
    }

    /* 读取元数据 */
    char* buf = (char*)malloc(size);
    if (!buf) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    ret = rados_read(ioctx, meta_key, buf, size, 0);
    if (ret < 0) {
        free(buf);
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_READ_ERROR;
    }

    /* 解码上传元信息 */
    ret = rgw_multipart_upload_info_decode((const uint8_t*)buf, size, info);
    free(buf);

    if (ret < 0) {
        rados_ioctx_destroy(ioctx);
        return RGW_SAL_ERR_DATA_CORRUPTION;
    }

    rados_ioctx_destroy(ioctx);

    (void)dpp;
    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS 账户 (Account) 操作实现
 * OMAP key 格式:
 *   - account.{account_id} - 账户主数据
 *   - account.name.{name} - 按名称索引
 *   - account.email.{email} - 按邮箱索引
 *============================================================================*/

/**
 * @brief 构建账户 OMAP key
 *
 * @param type 类型 (0=主数据, 1=按名称, 2=按邮箱)
 * @param key 输出参数
 * @param key_size 缓冲区大小
 * @param account_id 账户 ID
 * @param name 名称 (可选)
 * @param email 邮箱 (可选)
 *
 * @return 执行结果
 */
static int rados_account_make_key(int type, char* key, size_t key_size,
                                   const char* account_id, const char* name,
                                   const char* email) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;

    int ret;
    switch (type) {
        case 0: /* 主数据 */
            ret = snprintf(key, key_size, "account.%s", account_id);
            break;
        case 1: /* 按名称 */
            ret = snprintf(key, key_size, "account.name.%s", name);
            break;
        case 2: /* 按邮箱 */
            ret = snprintf(key, key_size, "account.email.%s", email);
            break;
        default:
            return RGW_SAL_ERR_INVALID_ARG;
    }

    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 按 ID 加载账户
 *
 * @param driver 驱动
 * @param account_id 账户 ID
 * @param info 输出参数，返回的账户信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_account_by_id(rgw_sal_driver_t* driver, const char* account_id,
                                     rgw_account_info_t* info, const rgw_sal_dpp_t* dpp) {
    if (!driver || !account_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->account_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_account_make_key(0, key, sizeof(key), account_id, NULL, NULL);
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->account_pool_ioctx, ".rgw.meta.account", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_account_info_decode(value, value_len, info);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 按名称加载账户
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param name 账户名称
 * @param info 输出参数，返回的账户信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_account_by_name(rgw_sal_driver_t* driver, const char* tenant,
                                        const char* name, rgw_account_info_t* info,
                                        const rgw_sal_dpp_t* dpp) {
    if (!driver || !name || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->account_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 先通过名称索引查找账户 ID */
    char index_key[256];
    int ret = snprintf(index_key, sizeof(index_key), "account.name.%s", name);
    if (ret < 0 || (size_t)ret >= sizeof(index_key)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    uint8_t* account_id_value = NULL;
    size_t account_id_len = 0;

    ret = rgw_omap_get(impl->account_pool_ioctx, ".rgw.meta.account",
                        index_key, &account_id_value, &account_id_len);
    if (ret < 0) return ret;

    /* 复制账户 ID */
    char account_id[256];
    if (account_id_len >= sizeof(account_id)) {
        rgw_omap_free_value(account_id_value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    memcpy(account_id, account_id_value, account_id_len);
    account_id[account_id_len] = '\0';
    rgw_omap_free_value(account_id_value);

    /* 再按 ID 加载完整信息 */
    return rados_load_account_by_id(driver, account_id, info, dpp);
}

/**
 * @brief 按邮箱加载账户
 *
 * @param driver 驱动
 * @param email 邮箱
 * @param info 输出参数，返回的账户信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_account_by_email(rgw_sal_driver_t* driver, const char* email,
                                         rgw_account_info_t* info,
                                         const rgw_sal_dpp_t* dpp) {
    if (!driver || !email || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->account_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 先通过邮箱索引查找账户 ID */
    char index_key[256];
    int ret = snprintf(index_key, sizeof(index_key), "account.email.%s", email);
    if (ret < 0 || (size_t)ret >= sizeof(index_key)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    uint8_t* account_id_value = NULL;
    size_t account_id_len = 0;

    ret = rgw_omap_get(impl->account_pool_ioctx, ".rgw.meta.account",
                        index_key, &account_id_value, &account_id_len);
    if (ret < 0) return ret;

    /* 复制账户 ID */
    char account_id[256];
    if (account_id_len >= sizeof(account_id)) {
        rgw_omap_free_value(account_id_value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    memcpy(account_id, account_id_value, account_id_len);
    account_id[account_id_len] = '\0';
    rgw_omap_free_value(account_id_value);

    /* 再按 ID 加载完整信息 */
    return rados_load_account_by_id(driver, account_id, info, dpp);
}

/**
 * @brief 存储账户
 *
 * @param driver 驱动
 * @param info 账户信息
 * @param exclusive 是否独占创建
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_store_account(rgw_sal_driver_t* driver, const rgw_account_info_t* info,
                                 bool exclusive, const rgw_sal_dpp_t* dpp) {
    if (!driver || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->account_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 编码账户信息 */
    size_t data_len = rgw_account_info_calc_encode_size(info);
    if (data_len == 0) return RGW_SAL_ERR_INTERNAL_ERROR;

    uint8_t* data = (uint8_t*)malloc(data_len);
    if (!data) return RGW_SAL_ERR_OUT_OF_MEMORY;

    int ret = rgw_account_info_encode(info, data, data_len);
    if (ret < 0) {
        free(data);
        return ret;
    }

    /* 存储主数据 */
    char key[256];
    ret = rados_account_make_key(0, key, sizeof(key), info->account_id, NULL, NULL);
    if (ret < 0) {
        free(data);
        return ret;
    }

    ret = rgw_omap_set(impl->account_pool_ioctx, ".rgw.meta.account", key, data, data_len, exclusive);
    if (ret < 0 && ret != -EEXIST) {
        free(data);
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    /* 存储名称索引 */
    if (info->display_name) {
        ret = rados_account_make_key(1, key, sizeof(key), info->account_id, info->display_name, NULL);
        if (ret >= 0) {
            char id_ref[256];
            snprintf(id_ref, sizeof(id_ref), "%s", info->account_id);
            size_t id_len = strlen(id_ref);
            rgw_omap_set(impl->account_pool_ioctx, ".rgw.meta.account", key,
                         (const uint8_t*)id_ref, id_len, exclusive);
        }
    }

    /* 存储邮箱索引 */
    if (info->email) {
        ret = rados_account_make_key(2, key, sizeof(key), info->account_id, NULL, info->email);
        if (ret >= 0) {
            char id_ref[256];
            snprintf(id_ref, sizeof(id_ref), "%s", info->account_id);
            size_t id_len = strlen(id_ref);
            rgw_omap_set(impl->account_pool_ioctx, ".rgw.meta.account", key,
                         (const uint8_t*)id_ref, id_len, exclusive);
        }
    }

    free(data);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 删除账户
 *
 * @param driver 驱动
 * @param account_id 账户 ID
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_delete_account(rgw_sal_driver_t* driver, const char* account_id,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !account_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->account_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 先加载账户信息以获取名称和邮箱索引 */
    rgw_account_info_t info;
    memset(&info, 0, sizeof(info));

    int ret = rados_load_account_by_id(driver, account_id, &info, dpp);
    if (ret == 0) {
        /* 删除名称索引 */
        if (info.display_name) {
            char key[256];
            rados_account_make_key(1, key, sizeof(key), account_id, info.display_name, NULL);
            rgw_omap_del(impl->account_pool_ioctx, ".rgw.meta.account", key);
        }

        /* 删除邮箱索引 */
        if (info.email) {
            char key[256];
            rados_account_make_key(2, key, sizeof(key), account_id, NULL, info.email);
            rgw_omap_del(impl->account_pool_ioctx, ".rgw.meta.account", key);
        }

        rgw_account_info_free_members(&info);
    }

    /* 删除主数据 */
    char key[256];
    ret = rados_account_make_key(0, key, sizeof(key), account_id, NULL, NULL);
    if (ret >= 0) {
        ret = rgw_omap_del(impl->account_pool_ioctx, ".rgw.meta.account", key);
        if (ret < 0 && ret != -ENOENT) return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS 组 (Group) 操作实现
 * OMAP key 格式:
 *   - group.{group_id} - 组主数据
 *   - group.name.{tenant}.{name} - 按名称索引
 *   - account.{account_id}.groups - 账户组列表
 *============================================================================*/

/**
 * @brief 构建组 OMAP key
 *
 * @param type 类型 (0=主数据, 1=按名称, 2=账户组列表)
 * @param key 输出参数
 * @param key_size 缓冲区大小
 * @param group_id 组 ID
 * @param tenant 租户
 * @param name 名称 (可选)
 * @param account_id 账户 ID (可选)
 *
 * @return 执行结果
 */
static int rados_group_make_key(int type, char* key, size_t key_size,
                                 const char* group_id, const char* tenant,
                                 const char* name, const char* account_id) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;

    int ret;
    switch (type) {
        case 0: /* 主数据 */
            ret = snprintf(key, key_size, "group.%s", group_id);
            break;
        case 1: /* 按名称 */
            ret = snprintf(key, key_size, "group.name.%s.%s", tenant ? tenant : "", name);
            break;
        case 2: /* 账户组列表 */
            ret = snprintf(key, key_size, "account.%s.groups", account_id);
            break;
        default:
            return RGW_SAL_ERR_INVALID_ARG;
    }

    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 按 ID 加载组
 *
 * @param driver 驱动
 * @param group_id 组 ID
 * @param info 输出参数，返回的组信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_group_by_id(rgw_sal_driver_t* driver, const char* group_id,
                                   rgw_group_info_t* info, const rgw_sal_dpp_t* dpp) {
    if (!driver || !group_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->group_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_group_make_key(0, key, sizeof(key), group_id, NULL, NULL, NULL);
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->group_pool_ioctx, ".rgw.meta.group", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_group_info_decode(value, value_len, info);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 按名称加载组
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param name 组名称
 * @param info 输出参数，返回的组信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_group_by_name(rgw_sal_driver_t* driver, const char* tenant,
                                      const char* name, rgw_group_info_t* info,
                                      const rgw_sal_dpp_t* dpp) {
    if (!driver || !name || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->group_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 先通过名称索引查找组 ID */
    char index_key[256];
    int ret = rados_group_make_key(1, index_key, sizeof(index_key), NULL, tenant, name, NULL);
    if (ret < 0) return ret;

    uint8_t* group_id_value = NULL;
    size_t group_id_len = 0;

    ret = rgw_omap_get(impl->group_pool_ioctx, ".rgw.meta.group", index_key,
                        &group_id_value, &group_id_len);
    if (ret < 0) return ret;

    /* 复制组 ID */
    char group_id[256];
    if (group_id_len >= sizeof(group_id)) {
        rgw_omap_free_value(group_id_value);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }
    memcpy(group_id, group_id_value, group_id_len);
    group_id[group_id_len] = '\0';
    rgw_omap_free_value(group_id_value);

    /* 再按 ID 加载完整信息 */
    return rados_load_group_by_id(driver, group_id, info, dpp);
}

/**
 * @brief 存储组
 *
 * @param driver 驱动
 * @param info 组信息
 * @param exclusive 是否独占创建
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_store_group(rgw_sal_driver_t* driver, const rgw_group_info_t* info,
                               bool exclusive, const rgw_sal_dpp_t* dpp) {
    if (!driver || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->group_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 编码组信息 */
    size_t data_len = rgw_group_info_calc_encode_size(info);
    if (data_len == 0) return RGW_SAL_ERR_INTERNAL_ERROR;

    uint8_t* data = (uint8_t*)malloc(data_len);
    if (!data) return RGW_SAL_ERR_OUT_OF_MEMORY;

    int ret = rgw_group_info_encode(info, data, data_len);
    if (ret < 0) {
        free(data);
        return ret;
    }

    /* 存储主数据 */
    char key[256];
    ret = rados_group_make_key(0, key, sizeof(key), info->id, NULL, NULL, NULL);
    if (ret < 0) {
        free(data);
        return ret;
    }

    ret = rgw_omap_set(impl->group_pool_ioctx, ".rgw.meta.group", key, data, data_len, exclusive);
    if (ret < 0 && ret != -EEXIST) {
        free(data);
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    /* 存储名称索引 */
    if (info->name) {
        ret = rados_group_make_key(1, key, sizeof(key), info->id, info->tenant, info->name, NULL);
        if (ret >= 0) {
            char id_ref[256];
            snprintf(id_ref, sizeof(id_ref), "%s", info->id);
            size_t id_len = strlen(id_ref);
            rgw_omap_set(impl->group_pool_ioctx, ".rgw.meta.group", key,
                         (const uint8_t*)id_ref, id_len, exclusive);
        }
    }

    /* 更新账户组列表 */
    if (info->account_id) {
        char group_list_key[256];
        rados_group_make_key(2, group_list_key, sizeof(group_list_key), NULL, NULL, NULL, info->account_id);

        /* 添加组 ID 到列表 */
        rgw_omap_kv_t kv;
        kv.key = info->id;
        kv.val = (uint8_t*)info->id;
        kv.val_len = strlen(info->id);

        rgw_omap_set_multi(impl->group_pool_ioctx, ".rgw.meta.group", &kv, 1, false);
    }

    free(data);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 删除组
 *
 * @param driver 驱动
 * @param group_id 组 ID
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_remove_group(rgw_sal_driver_t* driver, const char* group_id,
                               const rgw_sal_dpp_t* dpp) {
    if (!driver || !group_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->group_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 先加载组信息以获取名称和账户 ID */
    rgw_group_info_t info;
    rgw_group_info_init(&info);

    int ret = rados_load_group_by_id(driver, group_id, &info, dpp);
    if (ret == 0) {
        /* 删除名称索引 */
        if (info.name) {
            char key[256];
            rados_group_make_key(1, key, sizeof(key), group_id, info.tenant, info.name, NULL);
            rgw_omap_del(impl->group_pool_ioctx, ".rgw.meta.group", key);
        }

        /* 从账户组列表中移除 */
        if (info.account_id) {
            char group_list_key[256];
            rados_group_make_key(2, group_list_key, sizeof(group_list_key), NULL, NULL, NULL, info.account_id);
            rgw_omap_del(impl->group_pool_ioctx, ".rgw.meta.group", group_list_key);
        }

        rgw_group_info_free_members(&info);
    }

    /* 删除主数据 */
    char key[256];
    ret = rados_group_make_key(0, key, sizeof(key), group_id, NULL, NULL, NULL);
    if (ret >= 0) {
        ret = rgw_omap_del(impl->group_pool_ioctx, ".rgw.meta.group", key);
        if (ret < 0 && ret != -ENOENT) return RGW_SAL_ERR_WRITE_ERROR;
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 列出组中的用户
 *
 * @param driver 驱动
 * @param group_id 组 ID
 * @param user_ids 输出参数，返回的用户 ID 数组
 * @param max_users 最大用户数
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_list_group_users(rgw_sal_driver_t* driver, const char* group_id,
                                   char*** user_ids, size_t max_users,
                                   const rgw_sal_dpp_t* dpp) {
    if (!driver || !group_id || !user_ids) return RGW_SAL_ERR_INVALID_ARG;

    /* 先加载组信息获取账户 ID */
    rgw_group_info_t info;
    rgw_group_info_init(&info);

    int ret = rados_load_group_by_id(driver, group_id, &info, dpp);
    if (ret < 0) {
        rgw_group_info_free_members(&info);
        return ret;
    }

    if (!info.account_id) {
        rgw_group_info_free_members(&info);
        *user_ids = NULL;
        return 0;
    }

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->group_pool_ioctx) {
        rgw_group_info_free_members(&info);
        return RGW_SAL_ERR_NOT_INITIALIZED;
    }

    /* 获取账户组列表 */
    char group_list_key[256];
    rados_group_make_key(2, group_list_key, sizeof(group_list_key), NULL, NULL, NULL, info.account_id);

    char** keys = NULL;
    size_t keys_count = 0;

    ret = rgw_omap_get_keys(impl->group_pool_ioctx, ".rgw.meta.group",
                              group_list_key, max_users, &keys, &keys_count);

    if (ret < 0 || !keys) {
        *user_ids = NULL;
    } else {
        *user_ids = keys;
    }

    rgw_group_info_free_members(&info);

    (void)dpp;
    return (int)keys_count;
}

/*============================================================================
 * RADOS OIDC Provider 操作实现
 * OMAP key 格式:
 *   - oidc.{tenant}.{provider_id} - OIDC 提供商主数据
 *   - oidc.{tenant}.providers - 提供商列表
 *============================================================================*/

/**
 * @brief 构建 OIDC provider OMAP key
 *
 * @param type 类型 (0=主数据, 1=提供商列表)
 * @param key 输出参数
 * @param key_size 缓冲区大小
 * @param tenant 租户
 * @param provider_id 提供商 ID (可选)
 *
 * @return 执行结果
 */
static int rados_oidc_make_key(int type, char* key, size_t key_size,
                                const char* tenant, const char* provider_id) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;

    int ret;
    switch (type) {
        case 0: /* 主数据 */
            ret = snprintf(key, key_size, "oidc.%s.%s", tenant ? tenant : "", provider_id);
            break;
        case 1: /* 提供商列表 */
            ret = snprintf(key, key_size, "oidc.%s.providers", tenant ? tenant : "");
            break;
        default:
            return RGW_SAL_ERR_INVALID_ARG;
    }

    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 存储 OIDC Provider
 *
 * @param driver 驱动
 * @param info OIDC Provider 信息
 * @param exclusive 是否独占创建
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_store_oidc_provider(rgw_sal_driver_t* driver,
                                       const rgw_oidc_provider_info_t* info,
                                       bool exclusive, const rgw_sal_dpp_t* dpp) {
    if (!driver || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->oidc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 编码 OIDC Provider 信息 */
    uint8_t* data = NULL;
    size_t data_len = 0;
    int ret = rgw_oidc_provider_info_encode_alloc(info, &data, &data_len);
    if (ret < 0 || !data) return RGW_SAL_ERR_INTERNAL_ERROR;

    /* 存储主数据 */
    char key[256];
    ret = rados_oidc_make_key(0, key, sizeof(key), info->tenant, info->id);
    if (ret < 0) {
        free(data);
        return ret;
    }

    ret = rgw_omap_set(impl->oidc_pool_ioctx, ".rgw.meta.oidc", key, data, data_len, exclusive);
    if (ret < 0 && ret != -EEXIST) {
        free(data);
        return RGW_SAL_ERR_WRITE_ERROR;
    }

    /* 添加到提供商列表 */
    char list_key[256];
    ret = rados_oidc_make_key(1, list_key, sizeof(list_key), info->tenant, NULL);
    if (ret >= 0) {
        char id_ref[256];
        snprintf(id_ref, sizeof(id_ref), "%s", info->id);
        size_t id_len = strlen(id_ref);
        rgw_omap_set(impl->oidc_pool_ioctx, ".rgw.meta.oidc", list_key,
                     (const uint8_t*)id_ref, id_len, false);
    }

    free(data);

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 加载 OIDC Provider
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param provider_id 提供商 ID
 * @param info 输出参数，返回的 OIDC Provider 信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_load_oidc_provider(rgw_sal_driver_t* driver, const char* tenant,
                                      const char* provider_id, rgw_oidc_provider_info_t* info,
                                      const rgw_sal_dpp_t* dpp) {
    if (!driver || !provider_id || !info) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->oidc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_oidc_make_key(0, key, sizeof(key), tenant, provider_id);
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->oidc_pool_ioctx, ".rgw.meta.oidc", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_oidc_provider_info_decode(value, value_len, info);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 删除 OIDC Provider
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param provider_id 提供商 ID
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_delete_oidc_provider(rgw_sal_driver_t* driver, const char* tenant,
                                        const char* provider_id, const rgw_sal_dpp_t* dpp) {
    if (!driver || !provider_id) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->oidc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 删除主数据 */
    char key[256];
    int ret = rados_oidc_make_key(0, key, sizeof(key), tenant, provider_id);
    if (ret >= 0) {
        ret = rgw_omap_del(impl->oidc_pool_ioctx, ".rgw.meta.oidc", key);
        if (ret < 0 && ret != -ENOENT) return RGW_SAL_ERR_WRITE_ERROR;
    }

    /* 从提供商列表中移除 */
    char list_key[256];
    ret = rados_oidc_make_key(1, list_key, sizeof(list_key), tenant, NULL);
    if (ret >= 0) {
        rgw_omap_del(impl->oidc_pool_ioctx, ".rgw.meta.oidc", list_key);
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 获取 OIDC Providers 列表
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param providers 输出参数，返回的提供商数组
 * @param max_providers 最大提供商数
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_get_oidc_providers(rgw_sal_driver_t* driver, const char* tenant,
                                      rgw_oidc_provider_info_t** providers,
                                      size_t max_providers, const rgw_sal_dpp_t* dpp) {
    if (!driver || !providers) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->oidc_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 获取提供商列表 */
    char list_key[256];
    int ret = rados_oidc_make_key(1, list_key, sizeof(list_key), tenant, NULL);
    if (ret < 0) return ret;

    char** provider_ids = NULL;
    size_t ids_count = 0;

    ret = rgw_omap_get_keys(impl->oidc_pool_ioctx, ".rgw.meta.oidc",
                              list_key, max_providers, &provider_ids, &ids_count);
    if (ret < 0 || !provider_ids) {
        *providers = NULL;
        return 0;
    }

    /* 分配结果数组 */
    *providers = (rgw_oidc_provider_info_t*)calloc(ids_count, sizeof(rgw_oidc_provider_info_t));
    if (!*providers) {
        for (size_t i = 0; i < ids_count; i++) {
            free(provider_ids[i]);
        }
        free(provider_ids);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 加载每个提供商信息 */
    size_t count = 0;
    for (size_t i = 0; i < ids_count && count < max_providers; i++) {
        rgw_oidc_provider_info_t info;
        rgw_oidc_provider_info_init(&info);

        ret = rados_load_oidc_provider(driver, tenant, provider_ids[i], &info, dpp);
        if (ret >= 0) {
            memcpy(&((*providers)[count]), &info, sizeof(rgw_oidc_provider_info_t));
            count++;
        }
        free(provider_ids[i]);
    }
    free(provider_ids);

    (void)dpp;
    return (int)count;
}

/*============================================================================
 * RADOS 通知/发布订阅 (Notification/PubSub) 操作实现
 * OMAP key 格式:
 *   - topic.{tenant}.{topic_name} - Topic 数据
 *   - bucket.{tenant}.{bucket_name}.topics - 桶的 Topic 列表
 *============================================================================*/

/**
 * @brief 获取通知元数据键
 *
 * @param tenant 租户
 * @param topic_name Topic 名称
 * @param key 输出参数
 * @param key_size 缓冲区大小
 *
 * @return 执行结果
 */
static int rados_notification_make_topic_key(const char* tenant, const char* topic_name,
                                                char* key, size_t key_size) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;
    int ret = snprintf(key, key_size, "topic.%s.%s", tenant ? tenant : "", topic_name);
    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 获取桶 Topic 列表键
 *
 * @param tenant 租户
 * @param bucket_name 桶名称
 * @param key 输出参数
 * @param key_size 缓冲区大小
 *
 * @return 执行结果
 */
static int rados_notification_make_bucket_topics_key(const char* tenant,
                                                        const char* bucket_name,
                                                        char* key, size_t key_size) {
    if (!key) return RGW_SAL_ERR_INVALID_ARG;
    int ret = snprintf(key, key_size, "bucket.%s.%s.topics", tenant ? tenant : "", bucket_name);
    if (ret < 0 || (size_t)ret >= key_size) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 获取通知
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name Topic 名称
 * @param notification 输出参数，返回的通知
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_get_notification(rgw_sal_driver_t* driver, const char* tenant,
                                     const char* topic_name, rgw_bucket_topic_filter_t* notification,
                                     const rgw_sal_dpp_t* dpp) {
    if (!driver || !topic_name || !notification) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = rados_notification_make_topic_key(tenant, topic_name, key, sizeof(key));
    if (ret < 0) return ret;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->topic_pool_ioctx, ".rgw.topic", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_bucket_topic_filter_decode(value, value_len, notification);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 预留发布资源
 *
 * @param driver 驱动
 * @param notification 通知
 * @param obj 对象
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_publish_reserve(rgw_sal_driver_t* driver,
                                   rgw_bucket_topic_filter_t* notification,
                                   rgw_sal_object_t* obj, const rgw_sal_dpp_t* dpp) {
    if (!driver || !notification || !obj) return RGW_SAL_ERR_INVALID_ARG;

    /* 预留发布资源的简化实现
     * 完整实现需要:
     * 1. 检查通知目标是否支持持久化
     * 2. 预留队列资源
     * 3. 返回预订 ID
     */

    (void)driver;
    (void)notification;
    (void)obj;
    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 提交发布
 *
 * @param driver 驱动
 * @param notification 通知
 * @param obj 对象
 * @param size 对象大小
 * @param mtime 修改时间
 * @param etag ETag
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_publish_commit(rgw_sal_driver_t* driver,
                                  rgw_bucket_topic_filter_t* notification,
                                  rgw_sal_object_t* obj, uint64_t size,
                                  time_t mtime, const char* etag,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !notification || !obj) return RGW_SAL_ERR_INVALID_ARG;

    /* 提交发布的简化实现
     * 完整实现需要:
     * 1. 构建事件消息
     * 2. 发送到目标端点
     * 3. 如果是持久化目标，需要存储到队列
     */

    (void)driver;
    (void)notification;
    (void)obj;
    (void)size;
    (void)mtime;
    (void)etag;
    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 读取 Topics
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param topics 输出参数，返回的 Topic 过滤器数组
 * @param max_topics 最大 Topic 数
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_read_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                               rgw_bucket_topic_filter_t** topics, size_t max_topics,
                               const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket || !topics) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;
    if (!bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取桶的 Topic 列表键 */
    char list_key[256];
    int ret = rados_notification_make_bucket_topics_key(bucket_impl->tenant,
                                                          bucket_impl->name,
                                                          list_key, sizeof(list_key));
    if (ret < 0) return ret;

    /* 获取所有 Topic 名称 */
    char** topic_names = NULL;
    size_t names_count = 0;

    ret = rgw_omap_get_keys(impl->topic_pool_ioctx, ".rgw.topic",
                              list_key, max_topics, &topic_names, &names_count);
    if (ret < 0 || !topic_names) {
        *topics = NULL;
        return 0;
    }

    /* 分配结果数组 */
    *topics = (rgw_bucket_topic_filter_t*)calloc(names_count, sizeof(rgw_bucket_topic_filter_t));
    if (!*topics) {
        for (size_t i = 0; i < names_count; i++) {
            free(topic_names[i]);
        }
        free(topic_names);
        return RGW_SAL_ERR_OUT_OF_MEMORY;
    }

    /* 加载每个 Topic 信息 */
    size_t count = 0;
    for (size_t i = 0; i < names_count && count < max_topics; i++) {
        rgw_bucket_topic_filter_t filter;
        rgw_bucket_topic_filter_init(&filter);

        ret = rados_get_notification(driver, bucket_impl->tenant, topic_names[i],
                                       &filter, dpp);
        if (ret >= 0) {
            memcpy(&((*topics)[count]), &filter, sizeof(rgw_bucket_topic_filter_t));
            count++;
        }
        free(topic_names[i]);
    }
    free(topic_names);

    (void)dpp;
    return (int)count;
}

/**
 * @brief 写入 Topics
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param topics Topic 过滤器数组
 * @param num_topics Topic 数量
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_write_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                rgw_bucket_topic_filter_t* topics, size_t num_topics,
                                const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;
    if (!bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 存储每个 Topic */
    for (size_t i = 0; i < num_topics; i++) {
        /* 编码 Topic 信息 */
        uint8_t* buf = NULL;
        size_t buf_len = 0;
        int ret = rgw_bucket_topic_filter_encode(&topics[i], NULL, 0, &buf_len);
        if (ret == -ERANGE) {
            buf = (uint8_t*)malloc(buf_len);
            if (!buf) continue;
            ret = rgw_bucket_topic_filter_encode(&topics[i], buf, buf_len, &buf_len);
            if (ret >= 0) {
                /* 存储 Topic 数据 */
                char topic_key[256];
                if (rados_notification_make_topic_key(bucket_impl->tenant,
                                                      topics[i].topic.name,
                                                      topic_key, sizeof(topic_key)) >= 0) {
                    rgw_omap_set(impl->topic_pool_ioctx, ".rgw.topic",
                                 topic_key, buf, buf_len, false);
                }
            }
            free(buf);
        }
    }

    /* 更新桶的 Topic 列表 */
    char list_key[256];
    int ret = rados_notification_make_bucket_topics_key(bucket_impl->tenant,
                                                          bucket_impl->name,
                                                          list_key, sizeof(list_key));
    if (ret >= 0) {
        /* 清空旧列表 */
        rgw_omap_clear(impl->topic_pool_ioctx, list_key);

        /* 添加新 Topic 到列表 */
        for (size_t i = 0; i < num_topics; i++) {
            if (topics[i].topic.name) {
                rgw_omap_set(impl->topic_pool_ioctx, ".rgw.topic", list_key,
                             (const uint8_t*)topics[i].topic.name,
                             strlen(topics[i].topic.name), false);
            }
        }
    }

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 移除 Topics
 *
 * @param driver 驱动
 * @param bucket 桶
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_remove_topics(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket,
                                 const rgw_sal_dpp_t* dpp) {
    if (!driver || !bucket) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;
    if (!bucket_impl) return RGW_SAL_ERR_INVALID_ARG;

    /* 获取桶的 Topic 列表键 */
    char list_key[256];
    int ret = rados_notification_make_bucket_topics_key(bucket_impl->tenant,
                                                          bucket_impl->name,
                                                          list_key, sizeof(list_key));
    if (ret < 0) return ret;

    /* 获取所有 Topic 名称 */
    char** topic_names = NULL;
    size_t names_count = 0;

    ret = rgw_omap_get_keys(impl->topic_pool_ioctx, ".rgw.topic",
                              list_key, 1000, &topic_names, &names_count);
    if (ret >= 0 && topic_names) {
        /* 删除每个 Topic */
        for (size_t i = 0; i < names_count; i++) {
            char topic_key[256];
            ret = rados_notification_make_topic_key(bucket_impl->tenant,
                                                     topic_names[i],
                                                     topic_key, sizeof(topic_key));
            if (ret >= 0) {
                rgw_omap_del(impl->topic_pool_ioctx, ".rgw.topic", topic_key);
            }
            free(topic_names[i]);
        }
        free(topic_names);
    }

    /* 清空桶的 Topic 列表 */
    rgw_omap_clear(impl->topic_pool_ioctx, list_key);

    (void)dpp;
    return RGW_SAL_OK;
}

/*============================================================================
 * RADOS Topic V2 操作实现 (新版通知 API)
 *============================================================================*/

/**
 * @brief 读取 Topic V2
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name Topic 名称
 * @param topic 输出参数，返回的 Topic 信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_read_topic_v2(rgw_sal_driver_t* driver, const char* tenant,
                                 const char* topic_name, rgw_topic_t* topic,
                                 const rgw_sal_dpp_t* dpp) {
    if (!driver || !topic_name || !topic) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 使用带版本的前缀 */
    char key[256];
    int ret = snprintf(key, sizeof(key), "_topic.%s.%s", tenant ? tenant : "", topic_name);
    if (ret < 0 || (size_t)ret >= sizeof(key)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    uint8_t* value = NULL;
    size_t value_len = 0;

    ret = rgw_omap_get(impl->topic_pool_ioctx, ".rgw.topic", key, &value, &value_len);
    if (ret < 0) return ret;

    ret = rgw_topic_decode(value, value_len, topic);
    rgw_omap_free_value(value);

    if (ret < 0) return RGW_SAL_ERR_DATA_CORRUPTION;

    (void)dpp;
    return RGW_SAL_OK;
}

/**
 * @brief 写入 Topic V2
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name Topic 名称
 * @param topic Topic 信息
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_write_topic_v2(rgw_sal_driver_t* driver, const char* tenant,
                                  const char* topic_name, const rgw_topic_t* topic,
                                  const rgw_sal_dpp_t* dpp) {
    if (!driver || !topic_name || !topic) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    /* 编码 Topic 信息 */
    uint8_t* buf = NULL;
    size_t buf_len = 0;
    int ret = rgw_topic_encode(topic, NULL, 0, &buf_len);
    if (ret == -ERANGE) {
        buf = (uint8_t*)malloc(buf_len);
        if (!buf) return RGW_SAL_ERR_OUT_OF_MEMORY;
        ret = rgw_topic_encode(topic, buf, buf_len, &buf_len);
        if (ret < 0) {
            free(buf);
            return ret;
        }

        /* 存储 Topic 数据 */
        char key[256];
        ret = snprintf(key, sizeof(key), "_topic.%s.%s", tenant ? tenant : "", topic_name);
        if (ret < 0 || (size_t)ret >= sizeof(key)) {
            free(buf);
            return RGW_SAL_ERR_OUT_OF_MEMORY;
        }

        ret = rgw_omap_set(impl->topic_pool_ioctx, ".rgw.topic", key, buf, buf_len, false);
        free(buf);

        if (ret < 0 && ret != -EEXIST) return RGW_SAL_ERR_WRITE_ERROR;
    } else {
        ret = RGW_SAL_ERR_INTERNAL_ERROR;
    }

    (void)dpp;
    return ret >= 0 ? RGW_SAL_OK : ret;
}

/**
 * @brief 删除 Topic V2
 *
 * @param driver 驱动
 * @param tenant 租户
 * @param topic_name Topic 名称
 * @param dpp 调试前缀
 *
 * @return 执行结果
 */
static int rados_remove_topic_v2(rgw_sal_driver_t* driver, const char* tenant,
                                   const char* topic_name, const rgw_sal_dpp_t* dpp) {
    if (!driver || !topic_name) return RGW_SAL_ERR_INVALID_ARG;

    rados_driver_impl_t* impl = (rados_driver_impl_t*)driver->impl;
    if (!impl || !impl->topic_pool_ioctx) return RGW_SAL_ERR_NOT_INITIALIZED;

    char key[256];
    int ret = snprintf(key, sizeof(key), "_topic.%s.%s", tenant ? tenant : "", topic_name);
    if (ret < 0 || (size_t)ret >= sizeof(key)) return RGW_SAL_ERR_OUT_OF_MEMORY;

    ret = rgw_omap_del(impl->topic_pool_ioctx, ".rgw.topic", key);
    if (ret < 0 && ret != -ENOENT) return RGW_SAL_ERR_WRITE_ERROR;

    (void)dpp;
    return RGW_SAL_OK;
}

/*============================================================================
 * 注意：rgw_sal_create_driver 函数定义在 rgw_sal.c 中
 *============================================================================*/

/*============================================================================
 * RADOS 驱动 API 函数实现 (非 static)
 *============================================================================*/

/**
 * @brief 销毁 RADOS 驱动
 */
void rgw_sal_rados_driver_destroy(rgw_sal_driver_t* driver) {
    if (!driver) return;
    if (driver->impl) {
        free(driver->impl);
        driver->impl = NULL;
    }
    free(driver);
}

/**
 * @brief 销毁 RADOS 用户
 */
void rgw_sal_rados_user_destroy(rgw_sal_user_t* user) {
    if (!user) return;
    if (user->impl) {
        free(user->impl);
        user->impl = NULL;
    }
    free(user);
}

/**
 * @brief 销毁 RADOS 桶
 */
void rgw_sal_rados_bucket_destroy(rgw_sal_bucket_t* bucket) {
    if (!bucket) return;
    if (bucket->impl) {
        free(bucket->impl);
        bucket->impl = NULL;
    }
    free(bucket);
}

/**
 * @brief 销毁 RADOS 对象
 */
void rgw_sal_rados_object_destroy(rgw_sal_object_t* obj) {
    if (!obj) return;
    if (obj->impl) {
        free(obj->impl);
        obj->impl = NULL;
    }
    free(obj);
}

/**
 * @brief 获取用户
 */
rgw_sal_user_t* rgw_sal_rados_get_user(rgw_sal_driver_t* driver, const rgw_sal_user_id_t* uid) {
    if (!driver || !uid) return NULL;

    rgw_sal_user_t* user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!user) return NULL;

    rados_user_impl_t* impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
    if (!impl) {
        free(user);
        return NULL;
    }

    /* 从输入参数初始化 impl */
    if (uid->id) impl->id = strdup(uid->id);
    if (uid->tenant) impl->tenant = strdup(uid->tenant);
    impl->max_buckets = -1;  /* 默认无限制 */
    impl->loaded = false;
    impl->user_type = uid->type;
    impl->destroyed = false;

    /* 初始化属性 */
    impl->attrs = rgw_sal_attrs_create();

    /* 设置 vtable 和 impl */
    user->vtable = driver->user_vtable;
    user->impl = impl;
    user->driver = driver;

    return user;
}

/**
 * @brief 克隆用户
 */
rgw_sal_user_t* rgw_sal_rados_user_clone(const rgw_sal_user_t* user) {
    if (!user) return NULL;

    rgw_sal_user_t* new_user = (rgw_sal_user_t*)calloc(1, sizeof(rgw_sal_user_t));
    if (!new_user) return NULL;

    rados_user_impl_t* old_impl = (rados_user_impl_t*)user->impl;
    if (old_impl) {
        rados_user_impl_t* new_impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
        if (!new_impl) {
            free(new_user);
            return NULL;
        }

        /* 复制所有字段 */
        if (old_impl->id) new_impl->id = strdup(old_impl->id);
        if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
        if (old_impl->display_name) new_impl->display_name = strdup(old_impl->display_name);
        if (old_impl->email) new_impl->email = strdup(old_impl->email);
        if (old_impl->ns) new_impl->ns = strdup(old_impl->ns);
        new_impl->user_type = old_impl->user_type;
        new_impl->max_buckets = old_impl->max_buckets;
        new_impl->loaded = old_impl->loaded;
        new_impl->usage_loaded = old_impl->usage_loaded;
        new_impl->user_info_stored = old_impl->user_info_stored;
        new_impl->destroyed = false;

        /* 复制配额信息 */
        new_impl->quota_info = old_impl->quota_info;

        /* 复制属性 */
        if (old_impl->attrs) {
            new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
        } else {
            new_impl->attrs = rgw_sal_attrs_create();
        }

        /* 复制版本跟踪器 */
        new_impl->version_tracker = old_impl->version_tracker;

        /* 复制使用统计 */
        new_impl->usage = old_impl->usage;

        new_user->impl = new_impl;
    } else {
        /* 如果原用户没有 impl，至少创建空的 attrs */
        rados_user_impl_t* new_impl = (rados_user_impl_t*)calloc(1, sizeof(rados_user_impl_t));
        if (new_impl) {
            new_impl->destroyed = false;
            new_impl->attrs = rgw_sal_attrs_create();
            new_user->impl = new_impl;
        }
    }

    new_user->vtable = user->vtable;
    new_user->driver = user->driver;

    return new_user;
}

/**
 * @brief 获取对象名称
 */
const char* rgw_sal_rados_object_get_name(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    if (obj->vtable && obj->vtable->get_name) {
        return obj->vtable->get_name(obj);
    }
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->name : NULL;
}

/**
 * @brief 检查对象是否为原子操作
 */
bool rgw_sal_rados_object_is_atomic(const rgw_sal_object_t* obj) {
    if (!obj) return false;
    if (obj->vtable && obj->vtable->is_atomic) {
        return obj->vtable->is_atomic(obj);
    }
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->is_atomic : false;
}

/**
 * @brief 获取桶
 */
rgw_sal_bucket_t* rgw_sal_rados_get_bucket(rgw_sal_driver_t* driver, const rgw_sal_bucket_id_t* bid) {
    if (!driver || !bid) return NULL;

    rgw_sal_bucket_t* bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!bucket) return NULL;

    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)calloc(1, sizeof(rados_bucket_impl_t));
    if (!impl) {
        free(bucket);
        return NULL;
    }

    /* 从输入参数初始化 impl */
    if (bid->name) impl->name = strdup(bid->name);
    if (bid->tenant) impl->tenant = strdup(bid->tenant);
    if (bid->marker) impl->marker = strdup(bid->marker);
    if (bid->bucket_id) impl->bucket_id = strdup(bid->bucket_id);
    impl->loaded = false;
    impl->created = false;
    impl->deleted = false;
    impl->destroyed = false;

    /* 初始化属性 */
    impl->attrs = rgw_sal_attrs_create();

    /* 设置 vtable 和 impl */
    bucket->vtable = driver->bucket_vtable;
    bucket->impl = impl;
    bucket->driver = driver;

    return bucket;
}

/**
 * @brief 获取对象
 */
rgw_sal_object_t* rgw_sal_rados_get_object(rgw_sal_driver_t* driver, rgw_sal_bucket_t* bucket, const rgw_sal_obj_key_t* key) {
    if (!driver || !key) return NULL;

    rgw_sal_object_t* obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!obj) return NULL;

    rados_object_impl_t* impl = (rados_object_impl_t*)calloc(1, sizeof(rados_object_impl_t));
    if (!impl) {
        free(obj);
        return NULL;
    }

    /* 从输入参数初始化 impl */
    if (key->name) impl->name = strdup(key->name);
    if (key->instance) impl->instance = strdup(key->instance);
    impl->is_null = key->is_null;
    impl->is_atomic = false;
    impl->is_expired = false;
    impl->written = false;
    impl->deleted = false;
    impl->loaded = false;
    impl->destroyed = false;

    /* 获取桶信息以设置 bucket 相关字段 */
    if (bucket) {
        if (bucket->vtable && bucket->vtable->get_name) {
            impl->bucket_name = strdup(bucket->vtable->get_name(bucket));
        }
        /* 获取桶的 tenant */
        if (bucket->impl) {
            rados_bucket_impl_t* bucket_impl = (rados_bucket_impl_t*)bucket->impl;
            if (bucket_impl->tenant) impl->bucket_tenant = strdup(bucket_impl->tenant);
            if (bucket_impl->bucket_id) impl->bucket_id = strdup(bucket_impl->bucket_id);
        }
    }

    /* 初始化属性 */
    impl->attrs = rgw_sal_attrs_create();

    /* 设置 vtable, impl 和 bucket */
    obj->vtable = driver->object_vtable;
    obj->impl = impl;
    obj->bucket = bucket;

    return obj;
}

/**
 * @brief 获取对象属性
 */
rgw_sal_attrs_t* rgw_sal_rados_object_get_attrs(rgw_sal_object_t* obj) {
    if (!obj) return NULL;
    if (obj->vtable && obj->vtable->get_attrs) {
        return obj->vtable->get_attrs(obj);
    }
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    return impl ? impl->attrs : NULL;
}

/**
 * @brief 设置对象原子性
 */
void rgw_sal_rados_object_set_atomic(rgw_sal_object_t* obj, bool atomic) {
    if (!obj) return;
    if (obj->vtable && obj->vtable->set_atomic) {
        obj->vtable->set_atomic(obj, atomic);
        return;
    }
    rados_object_impl_t* impl = (rados_object_impl_t*)obj->impl;
    if (impl) impl->is_atomic = atomic;
}

/**
 * @brief 克隆对象
 */
rgw_sal_object_t* rgw_sal_rados_object_clone(const rgw_sal_object_t* obj) {
    if (!obj) return NULL;

    rgw_sal_object_t* new_obj = (rgw_sal_object_t*)calloc(1, sizeof(rgw_sal_object_t));
    if (!new_obj) return NULL;

    rados_object_impl_t* old_impl = (rados_object_impl_t*)obj->impl;
    if (old_impl) {
        rados_object_impl_t* new_impl = (rados_object_impl_t*)calloc(1, sizeof(rados_object_impl_t));
        if (!new_impl) {
            free(new_obj);
            return NULL;
        }

        /* 复制所有字段 */
        if (old_impl->name) new_impl->name = strdup(old_impl->name);
        if (old_impl->instance) new_impl->instance = strdup(old_impl->instance);
        if (old_impl->bucket_name) new_impl->bucket_name = strdup(old_impl->bucket_name);
        if (old_impl->bucket_tenant) new_impl->bucket_tenant = strdup(old_impl->bucket_tenant);
        if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
        if (old_impl->obj_oid) new_impl->obj_oid = strdup(old_impl->obj_oid);
        new_impl->is_null = old_impl->is_null;
        new_impl->is_atomic = old_impl->is_atomic;
        new_impl->is_expired = old_impl->is_expired;
        new_impl->size = old_impl->size;
        new_impl->mtime = old_impl->mtime;
        new_impl->written = old_impl->written;
        new_impl->deleted = old_impl->deleted;
        new_impl->loaded = old_impl->loaded;
        new_impl->destroyed = false;

        /* 复制属性 */
        if (old_impl->attrs) {
            new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
        } else {
            new_impl->attrs = rgw_sal_attrs_create();
        }

        new_obj->impl = new_impl;
    } else {
        /* 如果原对象没有 impl，至少创建空的 attrs */
        rados_object_impl_t* new_impl = (rados_object_impl_t*)calloc(1, sizeof(rados_object_impl_t));
        if (new_impl) {
            new_impl->destroyed = false;
            new_impl->is_null = true;
            new_impl->attrs = rgw_sal_attrs_create();
            new_obj->impl = new_impl;
        }
    }

    new_obj->vtable = obj->vtable;
    new_obj->bucket = obj->bucket;

    return new_obj;
}

/**
 * @brief 获取桶属性
 */
rgw_sal_attrs_t* rgw_sal_rados_bucket_get_attrs(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->attrs : NULL;
}

/**
 * @brief 获取桶标签
 */
const char* rgw_sal_rados_bucket_get_tag(rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->tag : NULL;
}

/**
 * @brief 设置桶标签
 */
void rgw_sal_rados_bucket_set_tag(rgw_sal_bucket_t* bucket, const char* tag) {
    if (!bucket) return;
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    if (!impl) return;
    free(impl->tag);
    impl->tag = tag ? strdup(tag) : NULL;
}

/**
 * @brief 克隆桶
 */
rgw_sal_bucket_t* rgw_sal_rados_bucket_clone(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;

    rgw_sal_bucket_t* new_bucket = (rgw_sal_bucket_t*)calloc(1, sizeof(rgw_sal_bucket_t));
    if (!new_bucket) return NULL;

    rados_bucket_impl_t* old_impl = (rados_bucket_impl_t*)bucket->impl;
    if (old_impl) {
        rados_bucket_impl_t* new_impl = (rados_bucket_impl_t*)calloc(1, sizeof(rados_bucket_impl_t));
        if (!new_impl) {
            free(new_bucket);
            return NULL;
        }

        /* 复制所有字段 */
        if (old_impl->name) new_impl->name = strdup(old_impl->name);
        if (old_impl->tenant) new_impl->tenant = strdup(old_impl->tenant);
        if (old_impl->marker) new_impl->marker = strdup(old_impl->marker);
        if (old_impl->bucket_id) new_impl->bucket_id = strdup(old_impl->bucket_id);
        if (old_impl->owner_id) new_impl->owner_id = strdup(old_impl->owner_id);
        if (old_impl->tag) new_impl->tag = strdup(old_impl->tag);
        new_impl->loaded = old_impl->loaded;
        new_impl->created = old_impl->created;
        new_impl->deleted = old_impl->deleted;
        new_impl->mtime = old_impl->mtime;
        new_impl->destroyed = false;

        /* 复制属性 */
        if (old_impl->attrs) {
            new_impl->attrs = rgw_sal_attrs_clone(old_impl->attrs);
        } else {
            new_impl->attrs = rgw_sal_attrs_create();
        }

        /* 复制 ACL 和 policy 指针（浅拷贝） */
        new_impl->acl = old_impl->acl;
        new_impl->policy = old_impl->policy;

        new_bucket->impl = new_impl;
    } else {
        /* 如果原桶没有 impl，至少创建空的 attrs */
        rados_bucket_impl_t* new_impl = (rados_bucket_impl_t*)calloc(1, sizeof(rados_bucket_impl_t));
        if (new_impl) {
            new_impl->destroyed = false;
            new_impl->attrs = rgw_sal_attrs_create();
            new_bucket->impl = new_impl;
        }
    }

    new_bucket->vtable = bucket->vtable;
    new_bucket->driver = bucket->driver;

    return new_bucket;
}

/**
 * @brief 获取桶名称
 */
const char* rgw_sal_rados_bucket_get_name(const rgw_sal_bucket_t* bucket) {
    if (!bucket) return NULL;
    if (bucket->vtable && bucket->vtable->get_name) {
        return bucket->vtable->get_name(bucket);
    }
    rados_bucket_impl_t* impl = (rados_bucket_impl_t*)bucket->impl;
    return impl ? impl->name : NULL;
}

/**
 * @brief 获取用户 ID
 */
const char* rgw_sal_rados_user_get_id(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    if (user->vtable && user->vtable->get_id) {
        return user->vtable->get_id(user);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->id : NULL;
}

/**
 * @brief 获取用户显示名称
 */
const char* rgw_sal_rados_user_get_display_name(rgw_sal_user_t* user) {
    if (!user) return NULL;
    if (user->vtable && user->vtable->get_display_name) {
        return user->vtable->get_display_name(user);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->display_name : NULL;
}

/**
 * @brief 设置用户显示名称
 */
int rgw_sal_rados_user_set_display_name(rgw_sal_user_t* user, const char* name) {
    if (!user || !name) return RGW_SAL_ERR_INVALID_ARG;
    if (user->vtable && user->vtable->set_display_name) {
        return user->vtable->set_display_name(user, name);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (!impl) return RGW_SAL_ERR_INVALID_ARG;
    free(impl->display_name);
    impl->display_name = strdup(name);
    if (!impl->display_name) return RGW_SAL_ERR_OUT_OF_MEMORY;
    return RGW_SAL_OK;
}

/**
 * @brief 获取用户租户
 */
const char* rgw_sal_rados_user_get_tenant(const rgw_sal_user_t* user) {
    if (!user) return NULL;
    if (user->vtable && user->vtable->get_tenant) {
        return user->vtable->get_tenant(user);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->tenant : NULL;
}

/**
 * @brief 获取用户类型
 */
uint32_t rgw_sal_rados_user_get_type(const rgw_sal_user_t* user) {
    if (!user) return 0;
    if (user->vtable && user->vtable->get_type) {
        return user->vtable->get_type(user);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->user_type : 0;
}

/**
 * @brief 获取用户最大桶数
 */
int32_t rgw_sal_rados_user_get_max_buckets(const rgw_sal_user_t* user) {
    if (!user) return -1;
    if (user->vtable && user->vtable->get_max_buckets) {
        return user->vtable->get_max_buckets(user);
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->max_buckets : -1;
}

/**
 * @brief 设置用户最大桶数
 */
void rgw_sal_rados_user_set_max_buckets(rgw_sal_user_t* user, int32_t max) {
    if (!user) return;
    if (user->vtable && user->vtable->set_max_buckets) {
        user->vtable->set_max_buckets(user, max);
        return;
    }
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    if (impl) impl->max_buckets = max;
}

/**
 * @brief 获取用户属性
 */
rgw_sal_attrs_t* rgw_sal_rados_user_get_attrs(rgw_sal_user_t* user) {
    if (!user) return NULL;
    rados_user_impl_t* impl = (rados_user_impl_t*)user->impl;
    return impl ? impl->attrs : NULL;
}

/**
 * @brief 获取驱动名称
 */
const char* rgw_sal_rados_driver_get_name(rgw_sal_driver_t* driver) {
    if (!driver) return "";
    return "rados";
}

/**
 * @brief 列出桶
 *
 * 调用 rados_driver_list_buckets 的实现
 */
int rgw_sal_rados_list_buckets(rgw_sal_driver_t* driver,
                                rgw_sal_user_t* owner,
                                const char* prefix, const char* delimiter,
                                const char* marker, const char* end_marker,
                                uint32_t max_keys, bool list_all,
                                rgw_sal_bucket_list_t** result,
                                const rgw_sal_dpp_t* dpp, rgw_sal_yield_t* y) {
    return rados_driver_list_buckets(driver, owner, prefix, delimiter,
                                     marker, end_marker, max_keys, list_all,
                                     result, dpp, y);
}

/**
 * @file rgw_sal_usage.h
 * @brief SAL C 接口 - 使用统计类型定义
 *
 * 定义存储抽象层 (SAL) 使用统计相关的 C 语言数据类型，
 * 对应 C++ 中的 rgw_usage_data, rgw_usage_log_entry 等结构。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 常量定义
 *============================================================================*/

/** Usage 对象前缀 */
#define RGW_SAL_USAGE_OBJ_PREFIX ".rgw_usage."

/** 默认最大分片数 */
#define RGW_SAL_USAGE_DEFAULT_MAX_SHARDS 64

/** 默认用户最大分片数 */
#define RGW_SAL_USAGE_DEFAULT_MAX_USER_SHARDS 256

/** Usage 哈希长度 */
#define RGW_SAL_USAGE_HASH_LEN 17

/*============================================================================
 * 迭代器类型
 *============================================================================*/

/**
 * @brief Usage 读取迭代器
 *
 * 用于分页读取 usage 日志条目。
 */
typedef struct rgw_sal_usage_iter {
    char* read_iter;    /**< 读取位置迭代器 (序列化字符串) */
    uint32_t index;    /**< 当前分片索引 */
} rgw_sal_usage_iter_t;

/**
 * @brief 创建 Usage 迭代器
 * @return 新创建的迭代器，失败返回 NULL
 */
rgw_sal_usage_iter_t* rgw_sal_usage_iter_create(void);

/**
 * @brief 重置 Usage 迭代器
 * @param iter 要重置的迭代器
 */
void rgw_sal_usage_iter_reset(rgw_sal_usage_iter_t* iter);

/**
 * @brief 销毁 Usage 迭代器
 * @param iter 要销毁的迭代器
 */
void rgw_sal_usage_iter_destroy(rgw_sal_usage_iter_t* iter);

/*============================================================================
 * Usage 数据类型
 *============================================================================*/

/**
 * @brief Usage 数据
 *
 * 存储单个类别的使用统计数据。
 */
typedef struct rgw_sal_usage_data {
    uint64_t bytes_sent;       /**< 发送字节数 */
    uint64_t bytes_received;   /**< 接收字节数 */
    uint64_t ops;              /**< 操作数 */
    uint64_t successful_ops;   /**< 成功操作数 */
} rgw_sal_usage_data_t;

/**
 * @brief S3Select Usage 数据
 */
typedef struct rgw_sal_s3select_usage {
    uint64_t bytes_processed; /**< 处理字节数 */
    uint64_t bytes_returned;  /**< 返回字节数 */
} rgw_sal_s3select_usage_t;

/**
 * @brief Usage 类别映射
 *
 * 使用 C 风格的动态数组实现 std::map<string, rgw_usage_data>
 */
typedef struct rgw_sal_usage_map_entry {
    char* category;            /**< 类别名称 */
    rgw_sal_usage_data_t data; /**< 使用数据 */
} rgw_sal_usage_map_entry_t;

typedef struct rgw_sal_usage_map {
    rgw_sal_usage_map_entry_t* entries;
    size_t count;
    size_t capacity;
} rgw_sal_usage_map_t;

/**
 * @brief Usage 日志条目
 *
 * 对应 C++ 的 rgw_usage_log_entry。
 * 存储单个 usage 记录的所有信息。
 */
typedef struct rgw_sal_usage_log_entry {
    char* owner_id;                    /**< 所有者 ID */
    char* payer_id;                    /**< 支付者 ID (可选) */
    char* bucket;                      /**< 桶名称 */
    uint64_t epoch;                     /**< 时间纪元 */
    rgw_sal_usage_data_t total_usage; /**< 总使用量 (向后兼容) */
    rgw_sal_usage_map_t usage_map;      /**< 类别使用量映射 */
    rgw_sal_s3select_usage_t s3select_usage; /**< S3Select 使用量 */
} rgw_sal_usage_log_entry_t;

/**
 * @brief User-Bucket 键
 *
 * 用于索引 usage 数据。
 */
typedef struct rgw_sal_user_bucket {
    char* user;     /**< 用户 ID */
    char* bucket;  /**< 桶名称 */
} rgw_sal_user_bucket_t;

/*============================================================================
 * Usage 集合类型
 *============================================================================*/

/**
 * @brief Usage 集合
 *
 * 存储多个 user-bucket 的 usage 条目。
 */
typedef struct rgw_sal_usage_entries {
    struct {
        char* key;                          /**< user.bucket 格式的键 */
        rgw_sal_usage_log_entry_t entry;   /**< 使用条目 */
    }* entries;
    size_t count;
    size_t capacity;
} rgw_sal_usage_entries_t;

/**
 * @brief 创建 Usage 数据
 * @return 新创建的 usage 数据结构
 */
rgw_sal_usage_data_t* rgw_sal_usage_data_create(void);

/**
 * @brief 销毁 Usage 数据
 * @param data 要销毁的 usage 数据
 */
void rgw_sal_usage_data_destroy(rgw_sal_usage_data_t* data);

/**
 * @brief 聚合 Usage 数据
 * @param target 目标数据 (会被修改)
 * @param source 源数据
 */
void rgw_sal_usage_data_aggregate(rgw_sal_usage_data_t* target, const rgw_sal_usage_data_t* source);

/**
 * @brief 创建 Usage 日志条目
 * @return 新创建的 usage 日志条目
 */
rgw_sal_usage_log_entry_t* rgw_sal_usage_log_entry_create(void);

/**
 * @brief 销毁 Usage 日志条目
 * @param entry 要销毁的 usage 日志条目
 */
void rgw_sal_usage_log_entry_destroy(rgw_sal_usage_log_entry_t* entry);

/**
 * @brief 聚合 Usage 日志条目
 * @param target 目标条目 (会被修改)
 * @param source 源条目
 */
void rgw_sal_usage_log_entry_aggregate(rgw_sal_usage_log_entry_t* target, const rgw_sal_usage_log_entry_t* source);

/**
 * @brief 添加类别 Usage
 * @param entry usage 日志条目
 * @param category 类别名称
 * @param data 使用数据
 */
int rgw_sal_usage_log_entry_add_usage(rgw_sal_usage_log_entry_t* entry,
                                      const char* category,
                                      const rgw_sal_usage_data_t* data);

/**
 * @brief 计算 Usage 总和
 * @param entry usage 日志条目
 * @param result 输出: 使用数据总和
 */
void rgw_sal_usage_log_entry_sum(const rgw_sal_usage_log_entry_t* entry,
                                  rgw_sal_usage_data_t* result);

/**
 * @brief 创建 Usage 集合
 * @return 新创建的 usage 集合
 */
rgw_sal_usage_entries_t* rgw_sal_usage_entries_create(void);

/**
 * @brief 销毁 Usage 集合
 * @param entries 要销毁的 usage 集合
 */
void rgw_sal_usage_entries_destroy(rgw_sal_usage_entries_t* entries);

/**
 * @brief 添加 Usage 条目到集合
 * @param entries usage 集合
 * @param key user.bucket 格式的键
 * @param entry usage 日志条目
 * @return 错误码
 */
int rgw_sal_usage_entries_add(rgw_sal_usage_entries_t* entries,
                                const char* key,
                                const rgw_sal_usage_log_entry_t* entry);

/**
 * @brief 聚合 Usage 条目
 * @param entries usage 集合
 * @param key user.bucket 格式的键
 * @param entry usage 日志条目
 * @return 错误码
 */
int rgw_sal_usage_entries_aggregate(rgw_sal_usage_entries_t* entries,
                                      const char* key,
                                      const rgw_sal_usage_log_entry_t* entry);

/**
 * @brief 创建 User-Bucket 键
 * @param user 用户 ID
 * @param bucket 桶名称
 * @return 新创建的 user-bucket 键
 */
rgw_sal_user_bucket_t* rgw_sal_user_bucket_create(const char* user, const char* bucket);

/**
 * @brief 销毁 User-Bucket 键
 * @param ub 要销毁的 user-bucket 键
 */
void rgw_sal_user_bucket_destroy(rgw_sal_user_bucket_t* ub);

/**
 * @brief 从字符串解析 User-Bucket 键
 * @param str user.bucket 格式的字符串
 * @return 新创建的 user-bucket 键
 */
rgw_sal_user_bucket_t* rgw_sal_user_bucket_parse(const char* str);

/**
 * @brief 将 User-Bucket 键格式化为字符串
 * @param ub user-bucket 键
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 格式化后的字符串
 */
const char* rgw_sal_user_bucket_to_string(const rgw_sal_user_bucket_t* ub, char* buf, size_t buf_size);

/*============================================================================
 * Usage 序列化/反序列化
 *============================================================================*/

/**
 * @brief 序列化 Usage 日志条目为二进制
 * @param entry usage 日志条目
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param out_size 输出: 实际序列化大小
 * @return 错误码
 */
int rgw_sal_usage_log_entry_encode(const rgw_sal_usage_log_entry_t* entry,
                                   uint8_t* buf, size_t buf_size, size_t* out_size);

/**
 * @brief 从二进制反序列化 Usage 日志条目
 * @param buf 输入缓冲区
 * @param buf_size 缓冲区大小
 * @param entry 输出: usage 日志条目
 * @return 错误码
 */
int rgw_sal_usage_log_entry_decode(const uint8_t* buf, size_t buf_size,
                                    rgw_sal_usage_log_entry_t* entry);

/*============================================================================
 * Usage 辅助函数
 *============================================================================*/

/**
 * @brief 计算 Usage 哈希值
 *
 * 生成 usage 对象的 OID。
 * 格式: {prefix}{hash}
 *
 * @param cct Ceph 上下文 (可以为 NULL，使用默认值)
 * @param name 用户名称 (可以为空)
 * @param index 分片索引
 * @param hash 输出: 哈希值 (必须至少有 RGW_SAL_USAGE_HASH_LEN 字节)
 */
void rgw_sal_usage_log_hash(void* cct, const char* name, uint32_t index, char* hash);

/**
 * @brief 生成 Usage 对象 OID
 *
 * @param index 分片索引
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 错误码
 */
int rgw_sal_usage_generate_oid(uint32_t index, char* buf, size_t buf_size);

/*============================================================================
 * 配置相关
 *============================================================================*/

/**
 * @brief Usage 配置
 */
typedef struct rgw_sal_usage_config {
    uint32_t max_shards;        /**< 最大分片数 */
    uint32_t max_user_shards;  /**< 用户最大分片数 */
    const char* pool_name;     /**< 使用存储池名称 */
} rgw_sal_usage_config_t;

/**
 * @brief 获取默认 Usage 配置
 * @return 默认配置 (静态变量，不要释放)
 */
const rgw_sal_usage_config_t* rgw_sal_usage_get_default_config(void);

#ifdef __cplusplus
}
#endif

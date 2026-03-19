/**
 * @file rgw_usage_c.h
 * @brief SAL C 接口 - Usage 访问模块
 *
 * 提供与 Ceph RADOS 存储交互的 Usage 统计访问接口。
 * 用于读取和清理用户使用统计信息。
 *
 * 对应 C++ 中的 RGWRados::read_usage, RGWRados::trim_usage 等函数。
 */

#pragma once

#include <rados/librados.h>
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
#define RGW_USAGE_OBJ_PREFIX ".rgw_usage."

/** 默认最大分片数 */
#define RGW_USAGE_DEFAULT_MAX_SHARDS 64

/** 默认用户最大分片数 */
#define RGW_USAGE_DEFAULT_MAX_USER_SHARDS 256

/** Usage 哈希长度 */
#define RGW_USAGE_HASH_LEN 17

/** Usage OMAP 值最大大小 */
#define RGW_USAGE_MAX_VALUE_SIZE (64 * 1024)

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief Usage 数据
 *
 * 存储单个类别的使用统计数据。
 */
typedef struct rgw_usage_data {
    uint64_t bytes_sent;       /**< 发送字节数 */
    uint64_t bytes_received;   /**< 接收字节数 */
    uint64_t ops;              /**< 操作数 */
    uint64_t successful_ops;   /**< 成功操作数 */
} rgw_usage_data_t;

/**
 * @brief S3Select Usage 数据
 */
typedef struct rgw_s3select_usage {
    uint64_t bytes_processed; /**< 处理字节数 */
    uint64_t bytes_returned;   /**< 返回字节数 */
} rgw_s3select_usage_t;

/**
 * @brief Usage 日志条目
 *
 * 对应 C++ 的 rgw_usage_log_entry。
 */
typedef struct rgw_usage_log_entry {
    char* owner_id;                     /**< 所有者 ID */
    char* payer_id;                     /**< 支付者 ID (可选) */
    char* bucket;                       /**< 桶名称 */
    uint64_t epoch;                     /**< 时间纪元 */
    rgw_usage_data_t total_usage;       /**< 总使用量 (向后兼容) */
    /* 简化实现: 使用固定大小的类别映射 */
    rgw_usage_data_t category_usage;   /**< 类别使用量 (简化实现) */
    rgw_s3select_usage_t s3select_usage; /**< S3Select 使用量 */
} rgw_usage_log_entry_t;

/**
 * @brief Usage 读取迭代器
 */
typedef struct rgw_usage_iter {
    char* read_iter;      /**< 读取位置迭代器 */
    uint32_t index;       /**< 当前分片索引 */
} rgw_usage_iter_t;

/**
 * @brief Usage 读取结果
 */
typedef struct rgw_usage_entries {
    rgw_usage_log_entry_t* entries;
    size_t count;
    size_t capacity;
} rgw_usage_entries_t;

/*============================================================================
 * 函数声明 - Usage 迭代器
 *============================================================================*/

/**
 * @brief 创建 Usage 迭代器
 * @return 新创建的迭代器，失败返回 NULL
 */
rgw_usage_iter_t* rgw_usage_iter_create(void);

/**
 * @brief 重置 Usage 迭代器
 * @param iter 要重置的迭代器
 */
void rgw_usage_iter_reset(rgw_usage_iter_t* iter);

/**
 * @brief 销毁 Usage 迭代器
 * @param iter 要销毁的迭代器
 */
void rgw_usage_iter_destroy(rgw_usage_iter_t* iter);

/*============================================================================
 * 函数声明 - Usage 哈希计算
 *============================================================================*/

/**
 * @brief 计算 Usage 哈希值
 *
 * 生成 usage 对象的 OID。
 *
 * @param name 用户名称 (可以为空，为空则返回所有用户)
 * @param index 分片索引
 * @param hash 输出: 哈希值 (必须至少有 RGW_USAGE_HASH_LEN 字节)
 */
void rgw_usage_log_hash(const char* name, uint32_t index, char* hash);

/**
 * @brief 生成 Usage 对象 OID
 *
 * @param index 分片索引
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 错误码
 */
int rgw_usage_generate_oid(uint32_t index, char* buf, size_t buf_size);

/*============================================================================
 * 函数声明 - Usage 条目操作
 *============================================================================*/

/**
 * @brief 创建 Usage 日志条目
 * @return 新创建的 usage 日志条目
 */
rgw_usage_log_entry_t* rgw_usage_log_entry_create(void);

/**
 * @brief 销毁 Usage 日志条目
 * @param entry 要销毁的 usage 日志条目
 */
void rgw_usage_log_entry_destroy(rgw_usage_log_entry_t* entry);

/**
 * @brief 聚合 Usage 数据到条目
 * @param target 目标条目
 * @param source 源数据
 */
void rgw_usage_log_entry_aggregate(rgw_usage_log_entry_t* target,
                                     const rgw_usage_log_entry_t* source);

/**
 * @brief 创建 Usage 集合
 * @return 新创建的 usage 集合
 */
rgw_usage_entries_t* rgw_usage_entries_create(void);

/**
 * @brief 销毁 Usage 集合
 * @param entries 要销毁的 usage 集合
 */
void rgw_usage_entries_destroy(rgw_usage_entries_t* entries);

/**
 * @brief 添加 Usage 条目到集合
 * @param entries usage 集合
 * @param entry usage 日志条目
 * @return 错误码
 */
int rgw_usage_entries_add(rgw_usage_entries_t* entries,
                           const rgw_usage_log_entry_t* entry);

/**
 * @brief 聚合 Usage 条目到集合
 * @param entries usage 集合
 * @param bucket_name 桶名称 (用于 key)
 * @param entry usage 日志条目
 * @return 错误码
 */
int rgw_usage_entries_aggregate(rgw_usage_entries_t* entries,
                                  const char* bucket_name,
                                  const rgw_usage_log_entry_t* entry);

/*============================================================================
 * 函数声明 - Usage 序列化/反序列化
 *============================================================================*/

/**
 * @brief 序列化 Usage 日志条目为二进制
 *
 * 格式 (version 4):
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
 *
 * @param entry usage 日志条目
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param out_size 输出: 实际序列化大小
 * @return 错误码
 */
int rgw_usage_log_entry_encode(const rgw_usage_log_entry_t* entry,
                                  uint8_t* buf, size_t buf_size, size_t* out_size);

/**
 * @brief 从二进制反序列化 Usage 日志条目
 * @param buf 输入缓冲区
 * @param buf_size 缓冲区大小
 * @param entry 输出: usage 日志条目
 * @return 错误码
 */
int rgw_usage_log_entry_decode(const uint8_t* buf, size_t buf_size,
                                  rgw_usage_log_entry_t* entry);

/*============================================================================
 * 函数声明 - RADOS Usage 操作
 *============================================================================*/

/**
 * @brief 读取 Usage 日志
 *
 * 从 RADOS 读取指定用户的 usage 日志。
 * 使用迭代器支持分页读取。
 *
 * @param ioctx IO 上下文
 * @param user_id 用户 ID
 * @param bucket_name 桶名称 (空字符串表示所有桶)
 * @param start_epoch 起始 epoch (0 表示不限)
 * @param end_epoch 结束 epoch (0 表示不限)
 * @param max_entries 最大返回条目数
 * @param iter 输入/输出迭代器
 * @param entries 输出: usage 条目集合
 * @param is_truncated 输出: 是否还有更多数据
 * @return 错误码
 * @retval 0 成功
 * @retval -ENOENT 没有找到数据
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 */
int rgw_usage_read_omap(rados_ioctx_t ioctx,
                          const char* user_id,
                          const char* bucket_name,
                          uint64_t start_epoch,
                          uint64_t end_epoch,
                          uint32_t max_entries,
                          rgw_usage_iter_t* iter,
                          rgw_usage_entries_t* entries,
                          bool* is_truncated);

/**
 * @brief 清理 Usage 日志
 *
 * 删除指定时间范围内的 usage 日志。
 *
 * @param ioctx IO 上下文
 * @param user_id 用户 ID
 * @param bucket_name 桶名称 (空字符串表示所有桶)
 * @param start_epoch 起始 epoch
 * @param end_epoch 结束 epoch
 * @return 错误码
 * @retval 0 成功
 * @retval -ENOENT 没有找到数据
 * @retval -EINVAL 参数无效
 */
int rgw_usage_trim_omap(rados_ioctx_t ioctx,
                          const char* user_id,
                          const char* bucket_name,
                          uint64_t start_epoch,
                          uint64_t end_epoch);

/**
 * @brief 清空 Usage 日志
 *
 * 清空所有 usage 日志。
 *
 * @param ioctx IO 上下文
 * @return 错误码
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_usage_clear_omap(rados_ioctx_t ioctx);

#ifdef __cplusplus
}
#endif

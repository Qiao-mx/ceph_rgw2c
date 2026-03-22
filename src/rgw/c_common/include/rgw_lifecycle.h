/**
 * @file rgw_lifecycle.h
 * @brief 生命周期管理序列化接口 (STUB)
 *
 * 生命周期规则的序列化/反序列化接口。
 * 此为占位符实现，完整功能需要原始 C++ 代码。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 生命周期规则类型
 *============================================================================*/

/**
 * @brief 生命周期规则动作
 */
typedef struct {
    uint32_t expiration_days;       /**< 过期天数 */
    uint32_t expiration_size;       /**< 过期大小阈值 */
    bool noncurrent_expiration;     /**< 非当前版本过期 */
} rgw_lc_rule_action_t;

/**
 * @brief 生命周期规则前缀过滤器
 */
typedef struct {
    char* prefix;                   /**< 对象名前缀 */
    char* tag_key;                  /**< 标签键 */
    char* tag_value;                /**< 标签值 */
} rgw_lc_rule_filter_t;

/**
 * @brief 生命周期规则
 */
typedef struct {
    char* id;                       /**< 规则 ID */
    char* prefix;                   /**< 前缀 */
    bool enabled;                   /**< 是否启用 */
    uint32_t expiration_days;       /**< 过期天数 */
    uint32_t noncurrent_expiration_days; /**< 非当前版本过期天数 */
} rgw_lc_rule_t;

/**
 * @brief 生命周期配置
 */
typedef struct {
    rgw_lc_rule_t* rules;          /**< 规则数组 */
    size_t rules_count;             /**< 规则数量 */
} rgw_lc_config_t;

/**
 * @brief 生命周期条目
 */
typedef struct {
    char* bucket;                  /**< 桶名 */
    char* marker;                  /**< 标记 */
    uint32_t opstatus;            /**< 操作状态 */
    time_t time;                   /**< 时间 */
    uint32_t days;                 /**< 天数 */
} rgw_lc_entry_t;

/**
 * @brief 生命周期头部
 */
typedef struct {
    char* start_date;              /**< 开始日期 */
    char* marker;                  /**< 标记 */
    uint32_t num_shards;          /**< 分片数 */
    time_t now;                    /**< 当前时间 */
} rgw_lc_head_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 创建生命周期配置
 */
rgw_lc_config_t* rgw_lc_config_create(void);

/**
 * @brief 销毁生命周期配置
 */
void rgw_lc_config_destroy(rgw_lc_config_t* config);

/**
 * @brief 添加生命周期规则
 */
int rgw_lc_config_add_rule(rgw_lc_config_t* config, const rgw_lc_rule_t* rule);

/**
 * @brief 计算编码大小
 */
size_t rgw_lc_config_calc_encode_size(const rgw_lc_config_t* config);

/**
 * @brief 编码生命周期配置
 */
int rgw_lc_config_encode(const rgw_lc_config_t* config,
                         uint8_t* buf,
                         size_t buf_size);

/**
 * @brief 解码生命周期配置
 */
int rgw_lc_config_decode(const uint8_t* buf,
                        size_t buf_size,
                        rgw_lc_config_t* config);

/*============================================================================
 * 生命周期条目和头部序列化
 *============================================================================*/

/**
 * @brief 计算生命周期条目编码大小
 */
size_t rgw_lc_entry_calc_encode_size(const rgw_lc_entry_t* entry);

/**
 * @brief 编码生命周期条目
 *
 * @param entry 条目
 * @param buf 输出缓冲区（可以为 NULL，用于计算大小）
 * @param buf_size 缓冲区大小
 * @param actual_size 实际编码大小（输出）
 * @return 成功返回 0，失败返回负数错误码
 */
int rgw_lc_entry_encode(const rgw_lc_entry_t* entry,
                        uint8_t* buf,
                        size_t buf_size,
                        size_t* actual_size);

/**
 * @brief 解码生命周期条目
 *
 * @param buf 输入缓冲区
 * @param buf_size 缓冲区大小
 * @param entry 输出条目
 * @return 成功返回 0，失败返回负数错误码
 */
int rgw_lc_entry_decode(const uint8_t* buf,
                        size_t buf_size,
                        rgw_lc_entry_t* entry);

/**
 * @brief 计算生命周期头部编码大小
 */
size_t rgw_lc_head_calc_encode_size(const rgw_lc_head_t* head);

/**
 * @brief 编码生命周期头部
 *
 * @param head 头部
 * @param buf 输出缓冲区（可以为 NULL，用于计算大小）
 * @param buf_size 缓冲区大小
 * @param actual_size 实际编码大小（输出）
 * @return 成功返回 0，失败返回负数错误码
 */
int rgw_lc_head_encode(const rgw_lc_head_t* head,
                        uint8_t* buf,
                        size_t buf_size,
                        size_t* actual_size);

/**
 * @brief 解码生命周期头部
 *
 * @param buf 输入缓冲区
 * @param buf_size 缓冲区大小
 * @param head 输出头部
 * @return 成功返回 0，失败返回负数错误码
 */
int rgw_lc_head_decode(const uint8_t* buf,
                       size_t buf_size,
                       rgw_lc_head_t* head);

#ifdef __cplusplus
}
#endif

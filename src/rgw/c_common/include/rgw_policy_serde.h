/**
 * @file rgw_policy_serde.h
 * @brief IAM 策略序列化接口
 *
 * 定义 IAM 策略的序列化/反序列化接口，
 * 用于将策略存储到 RADOS OMAP 或从 OMAP 加载。
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

/** 策略缓冲区初始大小 */
#define RGW_POLICY_BUF_INIT_SIZE              8192

/** 策略缓冲区最大大小 (防止恶意数据，64KB) */
#define RGW_POLICY_BUF_MAX_SIZE               (64 * 1024)

/** 策略版本号 */
#define RGW_POLICY_ENCODE_VERSION              1

/** 最大语句数量 */
#define RGW_POLICY_MAX_STATEMENTS              100

/** 最大条件键长度 */
#define RGW_POLICY_MAX_CONDITION_KEY_LEN      128

/** 最大资源数量 */
#define RGW_POLICY_MAX_RESOURCES               100

/** 最大主体数量 */
#define RGW_POLICY_MAX_PRINCIPALS              100

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 策略效果
 */
typedef enum {
    RGW_POLICY_EFFECT_ALLOW = 0,  /**< 允许 */
    RGW_POLICY_EFFECT_DENY = 1   /**< 拒绝 */
} rgw_policy_effect_t;

/**
 * @brief 策略操作类型
 */
typedef enum {
    RGW_POLICY_ACTION_GET = 0,
    RGW_POLICY_ACTION_PUT = 1,
    RGW_POLICY_ACTION_DELETE = 2,
    RGW_POLICY_ACTION_LIST = 3,
    RGW_POLICY_ACTION_ALL = 4
} rgw_policy_action_t;

/**
 * @brief 条件运算符
 */
typedef enum {
    RGW_POLICY_COND_STRING_EQUALS = 0,
    RGW_POLICY_COND_STRING_NOT_EQUALS = 1,
    RGW_POLICY_COND_STRING_LIKE = 2,
    RGW_POLICY_COND_STRING_NOT_LIKE = 3,
    RGW_POLICY_COND_BOOL = 4,
    RGW_POLICY_COND_NULL = 5,
    RGW_POLICY_COND_NUMERIC_EQUALS = 6,
    RGW_POLICY_COND_NUMERIC_NOT_EQUALS = 7,
    RGW_POLICY_COND_NUMERIC_LESS_THAN = 8,
    RGW_POLICY_COND_NUMERIC_LESS_THAN_EQUALS = 9,
    RGW_POLICY_COND_NUMERIC_GREATER_THAN = 10,
    RGW_POLICY_COND_NUMERIC_GREATER_THAN_EQUALS = 11,
    RGW_POLICY_COND_DATE_EQUALS = 12,
    RGW_POLICY_COND_DATE_NOT_EQUALS = 13,
    RGW_POLICY_COND_DATE_LESS_THAN = 14,
    RGW_POLICY_COND_DATE_LESS_THAN_EQUALS = 15,
    RGW_POLICY_COND_DATE_GREATER_THAN = 16,
    RGW_POLICY_COND_DATE_GREATER_THAN_EQUALS = 17,
    RGW_POLICY_COND_IP_ADDRESS = 18,
    RGW_POLICY_COND_NOT_IP_ADDRESS = 19
} rgw_policy_condition_op_t;

/**
 * @brief 条件键类型
 */
typedef enum {
    RGW_POLICY_COND_KEY_STRING = 0,
    RGW_POLICY_COND_KEY_BOOL = 1,
    RGW_POLICY_COND_KEY_NUMBER = 2,
    RGW_POLICY_COND_KEY_DATE = 3,
    RGW_POLICY_COND_KEY_IP = 4
} rgw_policy_condition_key_type_t;

/**
 * @brief 条件
 *
 * 表示策略中的一个条件。
 */
typedef struct {
    rgw_policy_condition_op_t op;        /**< 运算符 */
    char* key;                          /**< 条件键 */
    char** values;                      /**< 值数组 */
    size_t num_values;                  /**< 值数量 */
    size_t values_capacity;              /**< 值数组容量 */
} rgw_policy_condition_t;

/**
 * @brief 条件块
 *
 * 一组条件的集合 (AND 关系)。
 */
typedef struct {
    rgw_policy_condition_t* conditions; /**< 条件数组 */
    size_t num_conditions;               /**< 条件数量 */
    size_t conditions_capacity;         /**< 条件数组容量 */
} rgw_policy_condition_block_t;

/**
 * @brief 语句
 *
 * 表示策略中的一个语句。
 */
typedef struct {
    char* sid;                          /**< 语句 ID */
    rgw_policy_effect_t effect;         /**< 效果 */
    char** actions;                     /**< 操作数组 */
    size_t num_actions;                 /**< 操作数量 */
    size_t actions_capacity;             /**< 操作数组容量 */
    char** resources;                   /**< 资源数组 */
    size_t num_resources;               /**< 资源数量 */
    size_t resources_capacity;           /**< 资源数组容量 */
    char** not_actions;                 /**< 排除的操作数组 */
    size_t num_not_actions;             /**< 排除的操作数量 */
    size_t not_actions_capacity;         /**< 排除的操作数组容量 */
    char** not_resources;               /**< 排除的资源数组 */
    size_t num_not_resources;           /**< 排除的资源数量 */
    size_t not_resources_capacity;       /**< 排除的资源数组容量 */
    rgw_policy_condition_block_t* conditions;  /**< 条件块数组 */
    size_t num_conditions;              /**< 条件块数量 */
    size_t conditions_capacity;         /**< 条件块数组容量 */
} rgw_policy_statement_t;

/**
 * @brief IAM 策略
 *
 * 完整的 IAM 策略结构。
 */
typedef struct {
    char* version;                      /**< 策略版本 */
    char* id;                           /**< 策略 ID */
    rgw_policy_statement_t* statements; /**< 语句数组 */
    size_t num_statements;              /**< 语句数量 */
    size_t statements_capacity;         /**< 语句数组容量 */
} rgw_policy_t;

/*============================================================================
 * 函数声明 - 生命周期管理
 *============================================================================*/

/**
 * @brief 创建策略
 *
 * @return 新创建的策略，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_policy_destroy() 释放
 */
rgw_policy_t* rgw_policy_create(void);

/**
 * @brief 销毁策略
 *
 * @param policy 策略
 */
void rgw_policy_destroy(rgw_policy_t* policy);

/**
 * @brief 深拷贝策略
 *
 * @param src 源策略
 * @param dst 目标策略
 *
 * @return 执行结果
 */
int rgw_policy_deep_copy(const rgw_policy_t* src, rgw_policy_t* dst);

/*============================================================================
 * 函数声明 - 语句管理
 *============================================================================*/

/**
 * @brief 添加语句到策略
 *
 * @param policy 策略
 * @param statement 语句
 *
 * @return 执行结果
 */
int rgw_policy_add_statement(rgw_policy_t* policy, const rgw_policy_statement_t* statement);

/**
 * @brief 创建允许语句
 *
 * @param policy 策略
 * @param action 操作
 * @param resource 资源
 *
 * @return 执行结果
 */
int rgw_policy_add_allow_statement(rgw_policy_t* policy,
                                    const char* action,
                                    const char* resource);

/**
 * @brief 创建拒绝语句
 *
 * @param policy 策略
 * @param action 操作
 * @param resource 资源
 *
 * @return 执行结果
 */
int rgw_policy_add_deny_statement(rgw_policy_t* policy,
                                  const char* action,
                                  const char* resource);

/*============================================================================
 * 函数声明 - 序列化/反序列化
 *============================================================================*/

/**
 * @brief 计算策略编码后的大小
 *
 * @param policy 策略
 *
 * @return 所需缓冲区大小，失败返回 0
 */
size_t rgw_policy_calc_encode_size(const rgw_policy_t* policy);

/**
 * @brief 编码策略到缓冲区
 *
 * @param policy 策略
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 编码后的数据长度，失败返回负值
 */
int rgw_policy_encode(const rgw_policy_t* policy, uint8_t* buf, size_t buf_size);

/**
 * @brief 动态编码策略
 *
 * @param policy 策略
 * @param out_buf 输出参数，返回分配的缓冲区
 * @param out_len 输出参数，返回编码后的长度
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 free() 释放 *out_buf
 */
int rgw_policy_encode_alloc(const rgw_policy_t* policy, uint8_t** out_buf, size_t* out_len);

/**
 * @brief 从缓冲区解码策略
 *
 * @param buf 输入缓冲区
 * @param buf_len 缓冲区长度
 * @param policy 输出策略
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 rgw_policy_destroy() 释放 policy
 */
int rgw_policy_decode(const uint8_t* buf, size_t buf_len, rgw_policy_t* policy);

/*============================================================================
 * 函数声明 - JSON 序列化
 *============================================================================*/

/**
 * @brief 将策略编码为 JSON 字符串
 *
 * @param policy 策略
 * @param json_str 输出参数，返回 JSON 字符串
 * @param json_len 输出参数，返回 JSON 字符串长度
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 free() 释放 *json_str
 */
int rgw_policy_to_json(const rgw_policy_t* policy, char** json_str, size_t* json_len);

/**
 * @brief 从 JSON 字符串解码策略
 *
 * @param json_str JSON 字符串
 * @param json_len JSON 字符串长度
 * @param policy 输出策略
 *
 * @return 执行结果
 *
 * @note 调用者需要使用 rgw_policy_destroy() 释放 policy
 */
int rgw_policy_from_json(const char* json_str, size_t json_len, rgw_policy_t* policy);

/*============================================================================
 * 函数声明 - OMAP 键
 *============================================================================*/

/**
 * @brief 构建桶策略 OMAP 键
 *
 * @param bucket_name 桶名称
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_policy_make_bucket_omap_key(const char* bucket_name, char* buf, size_t buf_size);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 检查策略是否为空
 *
 * @param policy 策略
 *
 * @return 是否为空
 */
bool rgw_policy_is_empty(const rgw_policy_t* policy);

/**
 * @brief 检查策略是否有效
 *
 * @param policy 策略
 *
 * @return 是否有效
 */
bool rgw_policy_is_valid(const rgw_policy_t* policy);

/**
 * @brief 比较两个策略
 *
 * @param a 策略 A
 * @param b 策略 B
 *
 * @return 是否相等
 */
bool rgw_policy_equal(const rgw_policy_t* a, const rgw_policy_t* b);

#ifdef __cplusplus
}
#endif

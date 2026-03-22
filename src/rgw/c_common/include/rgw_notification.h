/**
 * @file rgw_notification.h
 * @brief 通知系统序列化接口 (STUB)
 *
 * 通知事件的序列化/反序列化接口。
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
 * 通知类型
 *============================================================================*/

/**
 * @brief 通知事件类型
 */
typedef enum {
    RGW_NOTIFICATION_EVENT_OBJECT_CREATED = 0,
    RGW_NOTIFICATION_EVENT_OBJECT_DELETED = 1,
    RGW_NOTIFICATION_EVENT_OBJECT_GET = 2,
    RGW_NOTIFICATION_EVENT_OBJECT_HEAD = 3,
    RGW_NOTIFICATION_EVENT_BUCKET_CREATED = 4,
    RGW_NOTIFICATION_EVENT_BUCKET_DELETED = 5
} rgw_notification_event_type_t;

/**
 * @brief 通知事件信息
 */
typedef struct {
    char* event_id;               /**< 事件 ID */
    rgw_notification_event_type_t event_type; /**< 事件类型 */
    char* bucket_name;            /**< 桶名 */
    char* object_key;             /**< 对象键 */
    uint64_t object_size;         /**< 对象大小 */
    char* etag;                   /**< ETag */
    int64_t timestamp;            /**< 时间戳 */
} rgw_notification_event_t;

/**
 * @brief 通知配置
 */
typedef struct {
    char* topic_arn;             /**< Topic ARN */
    char* bucket;                /**< 桶名 */
    char* object_prefix;        /**< 对象前缀 */
    uint32_t events_mask;        /**< 事件掩码 */
} rgw_notification_config_t;

/**
 * @brief Topic 信息
 */
typedef struct {
    char* name;                 /**< Topic 名称 */
    char* arn;                 /**< Topic ARN */
    char* bucket;              /**< 关联的桶 */
    char* object_prefix;       /**< 对象前缀 */
    uint32_t events_mask;       /**< 事件掩码 */
    bool persistent;            /**< 是否持久化 */
    int64_t create_time;        /**< 创建时间 */
} rgw_topic_t;

/**
 * @brief 桶 Topic 过滤器
 */
typedef struct {
    char* bucket;               /**< 桶名 */
    rgw_topic_t topic;          /**< Topic 信息 */
    uint32_t events_mask;        /**< 事件掩码 */
    bool persistent;             /**< 是否持久化 */
} rgw_bucket_topic_filter_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief 创建通知事件
 */
rgw_notification_event_t* rgw_notification_event_create(void);

/**
 * @brief 销毁通知事件
 */
void rgw_notification_event_destroy(rgw_notification_event_t* event);

/**
 * @brief 创建通知配置
 */
rgw_notification_config_t* rgw_notification_config_create(void);

/**
 * @brief 销毁通知配置
 */
void rgw_notification_config_destroy(rgw_notification_config_t* config);

/**
 * @brief 编码通知事件
 */
int rgw_notification_event_encode(const rgw_notification_event_t* event,
                                 uint8_t* buf,
                                 size_t buf_size);

/**
 * @brief 解码通知事件
 */
int rgw_notification_event_decode(const uint8_t* buf,
                                 size_t buf_size,
                                 rgw_notification_event_t* event);

/*============================================================================
 * Topic 函数
 *============================================================================*/

/**
 * @brief 初始化 Topic
 */
int rgw_topic_init(rgw_topic_t* topic);

/**
 * @brief 释放 Topic 成员
 */
void rgw_topic_free_members(rgw_topic_t* topic);

/**
 * @brief 编码 Topic
 */
int rgw_topic_encode(const rgw_topic_t* topic,
                     uint8_t* buf,
                     size_t buf_size,
                     size_t* out_len);

/**
 * @brief 解码 Topic
 */
int rgw_topic_decode(const uint8_t* buf,
                     size_t buf_size,
                     rgw_topic_t* topic);

/*============================================================================
 * 桶 Topic 过滤器函数
 *============================================================================*/

/**
 * @brief 初始化桶 Topic 过滤器
 */
int rgw_bucket_topic_filter_init(rgw_bucket_topic_filter_t* filter);

/**
 * @brief 释放桶 Topic 过滤器成员
 */
void rgw_bucket_topic_filter_free_members(rgw_bucket_topic_filter_t* filter);

/**
 * @brief 编码桶 Topic 过滤器
 */
int rgw_bucket_topic_filter_encode(const rgw_bucket_topic_filter_t* filter,
                                  uint8_t* buf,
                                  size_t buf_size,
                                  size_t* out_len);

/**
 * @brief 解码桶 Topic 过滤器
 */
int rgw_bucket_topic_filter_decode(const uint8_t* buf,
                                  size_t buf_size,
                                  rgw_bucket_topic_filter_t* filter);

#ifdef __cplusplus
}
#endif

/**
 * @file rgw_notification.c
 * @brief 通知系统序列化接口实现
 *
 * 通知事件的序列化/反序列化接口。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "rgw_notification.h"
#include "rgw_errors.h"

/*============================================================================
 * Topic 函数实现
 *============================================================================*/

int rgw_topic_init(rgw_topic_t* topic) {
    if (!topic) return RGW_ERR_INVALID_ARG;

    memset(topic, 0, sizeof(rgw_topic_t));
    return 0;
}

void rgw_topic_free_members(rgw_topic_t* topic) {
    if (!topic) return;

    if (topic->name) { free(topic->name); topic->name = NULL; }
    if (topic->arn) { free(topic->arn); topic->arn = NULL; }
    if (topic->bucket) { free(topic->bucket); topic->bucket = NULL; }
    if (topic->object_prefix) { free(topic->object_prefix); topic->object_prefix = NULL; }
}

int rgw_topic_encode(const rgw_topic_t* topic,
                     uint8_t* buf,
                     size_t buf_size,
                     size_t* out_len) {
    if (!topic || !buf || !out_len) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 编码 name */
    size_t name_len = topic->name ? strlen(topic->name) + 1 : 0;
    if (offset + sizeof(size_t) + name_len > buf_size) return RGW_ERR_BUFFER_OVERFLOW;
    memcpy(buf + offset, &name_len, sizeof(size_t));
    offset += sizeof(size_t);
    if (name_len > 0) {
        memcpy(buf + offset, topic->name, name_len);
        offset += name_len;
    }

    /* 编码 arn */
    size_t arn_len = topic->arn ? strlen(topic->arn) + 1 : 0;
    if (offset + sizeof(size_t) + arn_len > buf_size) return RGW_ERR_BUFFER_OVERFLOW;
    memcpy(buf + offset, &arn_len, sizeof(size_t));
    offset += sizeof(size_t);
    if (arn_len > 0) {
        memcpy(buf + offset, topic->arn, arn_len);
        offset += arn_len;
    }

    /* 编码 bucket */
    size_t bucket_len = topic->bucket ? strlen(topic->bucket) + 1 : 0;
    if (offset + sizeof(size_t) + bucket_len > buf_size) return RGW_ERR_BUFFER_OVERFLOW;
    memcpy(buf + offset, &bucket_len, sizeof(size_t));
    offset += sizeof(size_t);
    if (bucket_len > 0) {
        memcpy(buf + offset, topic->bucket, bucket_len);
        offset += bucket_len;
    }

    /* 编码 object_prefix */
    size_t prefix_len = topic->object_prefix ? strlen(topic->object_prefix) + 1 : 0;
    if (offset + sizeof(size_t) + prefix_len > buf_size) return RGW_ERR_BUFFER_OVERFLOW;
    memcpy(buf + offset, &prefix_len, sizeof(size_t));
    offset += sizeof(size_t);
    if (prefix_len > 0) {
        memcpy(buf + offset, topic->object_prefix, prefix_len);
        offset += prefix_len;
    }

    /* 编码 events_mask, persistent, create_time */
    if (offset + sizeof(uint32_t) + sizeof(bool) + sizeof(int64_t) > buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }
    memcpy(buf + offset, &topic->events_mask, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(buf + offset, &topic->persistent, sizeof(bool));
    offset += sizeof(bool);
    memcpy(buf + offset, &topic->create_time, sizeof(int64_t));
    offset += sizeof(int64_t);

    *out_len = offset;
    return 0;
}

int rgw_topic_decode(const uint8_t* buf,
                     size_t buf_size,
                     rgw_topic_t* topic) {
    if (!buf || !topic) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 解码 name */
    size_t name_len;
    memcpy(&name_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (name_len > 0) {
        if (offset + name_len > buf_size) return RGW_ERR_PARSE_ERROR;
        topic->name = (char*)malloc(name_len);
        if (topic->name) {
            memcpy(topic->name, buf + offset, name_len);
        }
        offset += name_len;
    }

    /* 解码 arn */
    size_t arn_len;
    memcpy(&arn_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (arn_len > 0) {
        if (offset + arn_len > buf_size) return RGW_ERR_PARSE_ERROR;
        topic->arn = (char*)malloc(arn_len);
        if (topic->arn) {
            memcpy(topic->arn, buf + offset, arn_len);
        }
        offset += arn_len;
    }

    /* 解码 bucket */
    size_t bucket_len;
    memcpy(&bucket_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (bucket_len > 0) {
        if (offset + bucket_len > buf_size) return RGW_ERR_PARSE_ERROR;
        topic->bucket = (char*)malloc(bucket_len);
        if (topic->bucket) {
            memcpy(topic->bucket, buf + offset, bucket_len);
        }
        offset += bucket_len;
    }

    /* 解码 object_prefix */
    size_t prefix_len;
    memcpy(&prefix_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (prefix_len > 0) {
        if (offset + prefix_len > buf_size) return RGW_ERR_PARSE_ERROR;
        topic->object_prefix = (char*)malloc(prefix_len);
        if (topic->object_prefix) {
            memcpy(topic->object_prefix, buf + offset, prefix_len);
        }
        offset += prefix_len;
    }

    /* 解码 events_mask, persistent, create_time */
    if (offset + sizeof(uint32_t) + sizeof(bool) + sizeof(int64_t) > buf_size) {
        return RGW_ERR_PARSE_ERROR;
    }
    memcpy(&topic->events_mask, buf + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(&topic->persistent, buf + offset, sizeof(bool));
    offset += sizeof(bool);
    memcpy(&topic->create_time, buf + offset, sizeof(int64_t));

    return 0;
}

/*============================================================================
 * 桶 Topic 过滤器函数实现
 *============================================================================*/

int rgw_bucket_topic_filter_init(rgw_bucket_topic_filter_t* filter) {
    if (!filter) return RGW_ERR_INVALID_ARG;

    memset(filter, 0, sizeof(rgw_bucket_topic_filter_t));
    return 0;
}

void rgw_bucket_topic_filter_free_members(rgw_bucket_topic_filter_t* filter) {
    if (!filter) return;

    if (filter->bucket) { free(filter->bucket); filter->bucket = NULL; }
    rgw_topic_free_members(&filter->topic);
}

int rgw_bucket_topic_filter_encode(const rgw_bucket_topic_filter_t* filter,
                                  uint8_t* buf,
                                  size_t buf_size,
                                  size_t* out_len) {
    if (!filter || !buf || !out_len) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 编码 bucket */
    size_t bucket_len = filter->bucket ? strlen(filter->bucket) + 1 : 0;
    if (offset + sizeof(size_t) + bucket_len > buf_size) return RGW_ERR_BUFFER_OVERFLOW;
    memcpy(buf + offset, &bucket_len, sizeof(size_t));
    offset += sizeof(size_t);
    if (bucket_len > 0) {
        memcpy(buf + offset, filter->bucket, bucket_len);
        offset += bucket_len;
    }

    /* 编码 topic */
    size_t topic_len;
    int ret = rgw_topic_encode(&filter->topic, buf + offset + sizeof(size_t),
                               buf_size - offset - sizeof(size_t), &topic_len);
    if (ret != 0) return ret;
    memcpy(buf + offset, &topic_len, sizeof(size_t));
    offset += sizeof(size_t) + topic_len;

    /* 编码 events_mask, persistent */
    if (offset + sizeof(uint32_t) + sizeof(bool) > buf_size) {
        return RGW_ERR_BUFFER_OVERFLOW;
    }
    memcpy(buf + offset, &filter->events_mask, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(buf + offset, &filter->persistent, sizeof(bool));
    offset += sizeof(bool);

    *out_len = offset;
    return 0;
}

int rgw_bucket_topic_filter_decode(const uint8_t* buf,
                                  size_t buf_size,
                                  rgw_bucket_topic_filter_t* filter) {
    if (!buf || !filter) return RGW_ERR_INVALID_ARG;

    size_t offset = 0;

    /* 解码 bucket */
    size_t bucket_len;
    memcpy(&bucket_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (bucket_len > 0) {
        if (offset + bucket_len > buf_size) return RGW_ERR_PARSE_ERROR;
        filter->bucket = (char*)malloc(bucket_len);
        if (filter->bucket) {
            memcpy(filter->bucket, buf + offset, bucket_len);
        }
        offset += bucket_len;
    }

    /* 解码 topic */
    size_t topic_len;
    memcpy(&topic_len, buf + offset, sizeof(size_t));
    offset += sizeof(size_t);
    if (offset + topic_len > buf_size) return RGW_ERR_PARSE_ERROR;
    int ret = rgw_topic_decode(buf + offset, topic_len, &filter->topic);
    if (ret != 0) return ret;
    offset += topic_len;

    /* 解码 events_mask, persistent */
    if (offset + sizeof(uint32_t) + sizeof(bool) > buf_size) {
        return RGW_ERR_PARSE_ERROR;
    }
    memcpy(&filter->events_mask, buf + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);
    memcpy(&filter->persistent, buf + offset, sizeof(bool));

    return 0;
}

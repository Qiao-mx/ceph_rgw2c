// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab ft=c

/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2024 Red Hat, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

/**
 * @file rgw_buffer.c
 * @brief Buffer implementation for binary data storage
 */

#include "rgw_buffer.h"
#include "containers/rgw_carray.h"
#include "containers/rgw_cmemory.h"
#include <string.h>
#include <stdlib.h>

// buffer_element_free 函数已移除，因为 rgw_array 不支持自定义 dtor

rgw_buffer_t* rgw_buffer_create(size_t capacity) {
    rgw_buffer_t* buf = rgw_c_alloc(sizeof(rgw_buffer_t));
    if (!buf) {
        return NULL;
    }

    buf->array = rgw_array_create(capacity > 0 ? capacity : 16);
    if (!buf->array) {
        rgw_c_free(buf);
        return NULL;
    }

    // 设置元素释放回调
    // 注意：rgw_array 不直接支持自定义 dtor，我们需要手动管理

    buf->total_len = 0;
    return buf;
}

void rgw_buffer_destroy(rgw_buffer_t* buf) {
    if (!buf) {
        return;
    }

    if (buf->array) {
        rgw_array_destroy(buf->array);
    }

    rgw_c_free(buf);
}

int rgw_buffer_append(rgw_buffer_t* buf, const void* data, size_t len) {
    if (!buf || !data || len == 0) {
        return -1;
    }

    // 直接添加数据到数组（rgw_array 会自动复制数据）
    int rc = rgw_array_append(buf->array, data, len);
    if (rc != 0) {
        return rc;
    }

    buf->total_len += len;
    return 0;
}

int rgw_buffer_append_str(rgw_buffer_t* buf, const char* str) {
    if (!buf || !str) {
        return -1;
    }
    return rgw_buffer_append(buf, str, strlen(str));
}

int rgw_buffer_append_buffer(rgw_buffer_t* buf, const rgw_buffer_t* src) {
    if (!buf || !src) {
        return -1;
    }

    size_t count = rgw_array_size(src->array);
    for (size_t i = 0; i < count; i++) {
        uint32_t len = 0;
        const void* data = rgw_array_get(src->array, i, &len);
        if (data && len > 0) {
            int rc = rgw_buffer_append(buf, data, len);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

void* rgw_buffer_data(const rgw_buffer_t* buf) {
    if (!buf || buf->total_len == 0) {
        return NULL;
    }

    // 如果只有一个数据块，直接返回
    if (rgw_array_size(buf->array) == 1) {
        uint32_t len = 0;
        return (void*)rgw_array_get(buf->array, 0, &len);
    }

    // 多个数据块需要合并（延迟处理）
    // 对于性能敏感场景，用户应该使用 rgw_buffer_flatten
    return NULL;
}

int rgw_buffer_flatten(rgw_buffer_t* buf) {
    if (!buf || buf->total_len == 0) {
        return 0;
    }

    // 如果只有一个数据块，不需要合并
    if (rgw_array_size(buf->array) == 1) {
        return 0;
    }

    // 分配连续内存
    void* flat_data = rgw_c_alloc(buf->total_len);
    if (!flat_data) {
        return -1;
    }

    // 复制所有数据块到连续内存
    size_t offset = 0;
    size_t count = rgw_array_size(buf->array);
    for (size_t i = 0; i < count; i++) {
        uint32_t len = 0;
        const void* data = rgw_array_get(buf->array, i, &len);
        if (data && len > 0) {
            memcpy((char*)flat_data + offset, data, len);
            offset += len;
        }
    }

    // 清空原有数据并添加合并后的数据
    rgw_array_clear(buf->array);
    int rc = rgw_array_append(buf->array, flat_data, buf->total_len);
    rgw_c_free(flat_data);

    return rc;
}

size_t rgw_buffer_length(const rgw_buffer_t* buf) {
    return buf ? buf->total_len : 0;
}

size_t rgw_buffer_capacity(const rgw_buffer_t* buf) {
    return buf && buf->array ? rgw_array_capacity(buf->array) : 0;
}

void rgw_buffer_clear(rgw_buffer_t* buf) {
    if (!buf) {
        return;
    }

    rgw_array_clear(buf->array);
    buf->total_len = 0;
}

int rgw_buffer_reserve(rgw_buffer_t* buf, size_t capacity) {
    if (!buf || !buf->array) {
        return -1;
    }
    return rgw_array_reserve(buf->array, capacity);
}

int rgw_buffer_truncate(rgw_buffer_t* buf, size_t new_len) {
    if (!buf || new_len > buf->total_len) {
        return -1;
    }

    if (new_len == 0) {
        rgw_buffer_clear(buf);
        return 0;
    }

    // 从后向前删除数据块直到达到目标长度
    size_t remaining = new_len;
    size_t count = rgw_array_size(buf->array);
    
    while (count > 0 && remaining > 0) {
        uint32_t len = 0;
        rgw_array_get(buf->array, count - 1, &len);
        
        if (len <= remaining) {
            // 整个数据块保留
            remaining -= len;
            count--;
        } else {
            // 需要截断当前数据块
            void* data = rgw_array_get_mut(buf->array, count - 1, &len);
            if (data) {
                // 创建新的截断数据
                void* new_data = rgw_c_alloc(remaining);
                if (new_data) {
                    memcpy(new_data, data, remaining);
                    // 替换数据
                    rgw_array_erase(buf->array, count - 1);
                    rgw_array_append(buf->array, new_data, remaining);
                    rgw_c_free(new_data);
                }
            }
            remaining = 0;
        }
    }
    
    // 删除前面多余的数据块
    for (size_t i = 0; i < count - 1; i++) {
        rgw_array_erase(buf->array, 0);
    }
    
    buf->total_len = new_len;
    return 0;
}

rgw_buffer_t* rgw_buffer_copy(const rgw_buffer_t* buf) {
    if (!buf) {
        return NULL;
    }

    rgw_buffer_t* new_buf = rgw_buffer_create(buf->total_len > 0 ? buf->total_len : 16);
    if (!new_buf) {
        return NULL;
    }

    int rc = rgw_buffer_copy_to(buf, new_buf);
    if (rc != 0) {
        rgw_buffer_destroy(new_buf);
        return NULL;
    }

    return new_buf;
}

int rgw_buffer_copy_to(const rgw_buffer_t* src, rgw_buffer_t* dst) {
    if (!src || !dst) {
        return -1;
    }

    if (src == dst) {
        return -2;
    }

    rgw_buffer_clear(dst);

    size_t count = rgw_array_size(src->array);
    for (size_t i = 0; i < count; i++) {
        uint32_t len = 0;
        const void* data = rgw_array_get(src->array, i, &len);
        if (data && len > 0) {
            int rc = rgw_buffer_append(dst, data, len);
            if (rc != 0) {
                return rc;
            }
        }
    }

    return 0;
}

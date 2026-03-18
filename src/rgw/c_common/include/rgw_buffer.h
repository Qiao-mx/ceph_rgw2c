#ifndef RGW_BUFFER_H
#define RGW_BUFFER_H

#include <stddef.h>
#include <stdbool.h>
#include "containers/rgw_carray.h"

typedef struct rgw_buffer {
    rgw_array_t* array;  // 使用动态数组存储多个数据块
    size_t total_len;    // 总数据长度
} rgw_buffer_t;

// 创建和销毁
rgw_buffer_t* rgw_buffer_create(size_t capacity);
void rgw_buffer_destroy(rgw_buffer_t* buf);

// 写入操作
int rgw_buffer_append(rgw_buffer_t* buf, const void* data, size_t len);
int rgw_buffer_append_str(rgw_buffer_t* buf, const char* str);
int rgw_buffer_append_buffer(rgw_buffer_t* buf, const rgw_buffer_t* src);

// 读取操作
void* rgw_buffer_data(const rgw_buffer_t* buf);
size_t rgw_buffer_length(const rgw_buffer_t* buf);
size_t rgw_buffer_capacity(const rgw_buffer_t* buf);

// 修改操作
void rgw_buffer_clear(rgw_buffer_t* buf);
int rgw_buffer_reserve(rgw_buffer_t* buf, size_t capacity);
int rgw_buffer_truncate(rgw_buffer_t* buf, size_t new_len);
int rgw_buffer_flatten(rgw_buffer_t* buf);

// 复制操作
rgw_buffer_t* rgw_buffer_copy(const rgw_buffer_t* buf);
int rgw_buffer_copy_to(const rgw_buffer_t* src, rgw_buffer_t* dst);

#endif // RGW_BUFFER_H

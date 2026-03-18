// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct rgw_memory_ops {
    void* (*allocate)(size_t size);
    void (*deallocate)(void *ptr);
    void* (*reallocate)(void *ptr, size_t new_size);
    void (*copy)(void *dest, const void *src, size_t size);
    int (*compare)(const void *a, const void *b, size_t size);
} rgw_memory_ops_t;
void rgw_set_memory_ops(const rgw_memory_ops_t *ops);
const rgw_memory_ops_t* rgw_get_memory_ops(void);
void* rgw_c_alloc(size_t size);
void rgw_c_free(void *ptr);
void* rgw_c_realloc(void *ptr, size_t new_size);
void rgw_c_memcpy(void *dest, const void *src, size_t size);
void rgw_c_memmove(void *dest, const void *src, size_t size);
int rgw_c_memcmp(const void *a, const void *b, size_t size);
char* rgw_c_strdup(const char *s);
#ifdef __cplusplus
}
#endif

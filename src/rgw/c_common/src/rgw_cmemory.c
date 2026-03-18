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
 * @file rgw_cmemory.c
 * @brief Memory management implementation for RGW C data structures
 * 
 * Provides customizable memory operations for all RGW C data structures.
 * Supports standard library allocation by default, with ability to override
 * with custom allocators (e.g., memory pools, slab allocators).
 */

#include "../include/containers/rgw_cmemory.h"
#include <stdlib.h>
#include <string.h>

static void wrap_memcpy(void *dest, const void *src, size_t size) { memcpy(dest, src, size); }

/**
 * Default memory operations using standard library
 */
static rgw_memory_ops_t default_memory_ops = {
    .allocate = malloc,
    .deallocate = free,
    .reallocate = realloc,
    .copy = wrap_memcpy,
    .compare = memcmp,
};

/**
 * Current memory operations (can be overridden)
 */
static rgw_memory_ops_t current_memory_ops = {
    .allocate = malloc,
    .deallocate = free,
    .reallocate = realloc,
    .copy = wrap_memcpy,
    .compare = memcmp,
};

/**
 * Set custom memory operations for all RGW C data structures
 */
void rgw_set_memory_ops(const rgw_memory_ops_t *ops)
{
    if (ops) {
        current_memory_ops = *ops;
    } else {
        current_memory_ops = default_memory_ops;
    }
}

/**
 * Get current memory operations
 */
const rgw_memory_ops_t* rgw_get_memory_ops(void)
{
    return &current_memory_ops;
}

/**
 * Allocate memory using current memory ops
 */
void* rgw_c_alloc(size_t size)
{
    return current_memory_ops.allocate(size);
}

/**
 * Deallocate memory using current memory ops
 */
void rgw_c_free(void *ptr)
{
    if (ptr && current_memory_ops.deallocate) {
        current_memory_ops.deallocate(ptr);
    }
}

/**
 * Reallocate memory using current memory ops
 */
void* rgw_c_realloc(void *ptr, size_t new_size)
{
    return current_memory_ops.reallocate(ptr, new_size);
}

/**
 * Copy memory using current memory ops
 */
void rgw_c_memcpy(void *dest, const void *src, size_t size)
{
    if (dest && src && size > 0) {
        current_memory_ops.copy(dest, src, size);
    }
}

/**
 * Move memory using current memory ops (handles overlapping regions)
 */
void rgw_c_memmove(void *dest, const void *src, size_t size)
{
    if (dest && src && size > 0) {
        memmove(dest, src, size);
    }
}

/**
 * Compare memory using current memory ops
 */
int rgw_c_memcmp(const void *a, const void *b, size_t size)
{
    if (a && b) {
        return current_memory_ops.compare(a, b, size);
    }
    return 0;
}

/**
 * String duplicate using current memory ops
 */
char* rgw_c_strdup(const char *s)
{
    if (!s) {
        return NULL;
    }
    
    size_t len = strlen(s) + 1;
    char *dup = (char*)current_memory_ops.allocate(len);
    if (dup) {
        current_memory_ops.copy(dup, s, len);
    }
    return dup;
}

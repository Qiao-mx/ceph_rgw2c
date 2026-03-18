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
 * @file rgw_carray.c
 * @brief Dynamic array implementation using utarray (替代 std::vector)
 */

#include "containers/rgw_carray.h"
#include "containers/rgw_cmemory.h"
#include "internal/utarray.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* rgw_array_element_t is defined in header file */

static void element_copy(void *dst, const void *src)
{
    const rgw_array_element_t *src_elem = (const rgw_array_element_t *)src;
    rgw_array_element_t *dst_elem = (rgw_array_element_t *)dst;
    
    dst_elem->data = NULL;
    dst_elem->len = 0;
    
    if (src_elem->data && src_elem->len > 0) {
        dst_elem->data = rgw_c_alloc(src_elem->len);
        if (dst_elem->data) {
            rgw_c_memcpy(dst_elem->data, src_elem->data, src_elem->len);
            dst_elem->len = src_elem->len;
        }
    }
}

static void element_dtor(void *elt)
{
    rgw_array_element_t *elem = (rgw_array_element_t *)elt;
    if (elem->data) {
        rgw_c_free(elem->data);
    }
    elem->data = NULL;
    elem->len = 0;
}

static void element_init(void *elt)
{
    rgw_array_element_t *elem = (rgw_array_element_t *)elt;
    elem->data = NULL;
    elem->len = 0;
}

static const UT_icd element_icd = {
    .sz = sizeof(rgw_array_element_t),
    .init = element_init,
    .copy = element_copy,
    .dtor = element_dtor
};

struct rgw_array {
    UT_array *utarray;
};

rgw_array_t* rgw_array_create(size_t initial_capacity)
{
    rgw_array_t *array = rgw_c_alloc(sizeof(rgw_array_t));
    if (!array) {
        return NULL;
    }
    
    utarray_new(array->utarray, &element_icd);
    
    if (initial_capacity > 0) {
        utarray_reserve(array->utarray, initial_capacity);
    }
    
    return array;
}

void rgw_array_destroy(rgw_array_t *array)
{
    if (!array) {
        return;
    }
    
    if (array->utarray) {
        utarray_free(array->utarray);
    }
    
    rgw_c_free(array);
}

int rgw_array_append(rgw_array_t *array, const void *data, uint32_t len)
{
    if (!array || !data || len == 0) {
        return -EINVAL;
    }
    
    rgw_array_element_t elem;
    elem.data = rgw_c_alloc(len);
    if (!elem.data) {
        return -ENOMEM;
    }
    rgw_c_memcpy(elem.data, data, len);
    elem.len = len;
    
    utarray_push_back(array->utarray, &elem);
    
    return 0;
}

int rgw_array_insert(rgw_array_t *array, size_t index, const void *data, uint32_t len)
{
    if (!array || !data || len == 0) {
        return -EINVAL;
    }
    
    if (index > utarray_len(array->utarray)) {
        return -EINVAL;
    }
    
    rgw_array_element_t elem;
    elem.data = rgw_c_alloc(len);
    if (!elem.data) {
        return -ENOMEM;
    }
    rgw_c_memcpy(elem.data, data, len);
    elem.len = len;
    
    utarray_insert(array->utarray, &elem, index);
    
    return 0;
}

int rgw_array_erase(rgw_array_t *array, size_t index)
{
    if (!array) {
        return -EINVAL;
    }
    
    if (index >= utarray_len(array->utarray)) {
        return -EINVAL;
    }
    
    utarray_erase(array->utarray, index, 1);
    
    return 0;
}

const void* rgw_array_get(const rgw_array_t *array, size_t index, uint32_t *len)
{
    if (!array || index >= utarray_len(array->utarray)) {
        if (len) *len = 0;
        return NULL;
    }
    
    rgw_array_element_t *elem = (rgw_array_element_t *)utarray_eltptr(array->utarray, index);
    if (len) {
        *len = elem->len;
    }
    
    return elem->data;
}

void* rgw_array_get_mut(rgw_array_t *array, size_t index, uint32_t *len)
{
    if (!array || index >= utarray_len(array->utarray)) {
        if (len) *len = 0;
        return NULL;
    }
    
    rgw_array_element_t *elem = (rgw_array_element_t *)utarray_eltptr(array->utarray, index);
    if (len) {
        *len = elem->len;
    }
    
    return elem->data;
}

size_t rgw_array_size(const rgw_array_t *array)
{
    return array ? utarray_len(array->utarray) : 0;
}

bool rgw_array_empty(const rgw_array_t *array)
{
    return rgw_array_size(array) == 0;
}

size_t rgw_array_capacity(const rgw_array_t *array)
{
    return array ? array->utarray->n : 0;
}

void rgw_array_clear(rgw_array_t *array)
{
    if (!array) {
        return;
    }
    
    utarray_clear(array->utarray);
}

int rgw_array_reserve(rgw_array_t *array, size_t capacity)
{
    if (!array) {
        return -EINVAL;
    }
    
    utarray_reserve(array->utarray, capacity);
    
    return 0;
}

int rgw_array_resize(rgw_array_t *array, size_t n, const void *default_val, uint32_t default_len)
{
    if (!array) {
        return -EINVAL;
    }
    
    size_t old_size = utarray_len(array->utarray);
    
    if (n < old_size) {
        utarray_resize(array->utarray, n);
        return 0;
    }
    
    if (n > old_size) {
        utarray_resize(array->utarray, n);
        
        if (default_val && default_len > 0) {
            for (size_t i = old_size; i < n; i++) {
                rgw_array_element_t *elem = (rgw_array_element_t *)utarray_eltptr(array->utarray, i);
                elem->data = rgw_c_alloc(default_len);
                if (elem->data) {
                    rgw_c_memcpy(elem->data, default_val, default_len);
                    elem->len = default_len;
                }
            }
        }
    }
    
    return 0;
}

int rgw_array_swap(rgw_array_t *array, size_t i, size_t j)
{
    if (!array || i >= utarray_len(array->utarray) || j >= utarray_len(array->utarray)) {
        return -EINVAL;
    }
    
    if (i == j) {
        return 0;
    }
    
    rgw_array_element_t *elem_i = (rgw_array_element_t *)utarray_eltptr(array->utarray, i);
    rgw_array_element_t *elem_j = (rgw_array_element_t *)utarray_eltptr(array->utarray, j);
    
    rgw_array_element_t temp;
    element_copy(&temp, elem_i);
    element_dtor(elem_i);
    element_copy(elem_i, elem_j);
    element_dtor(elem_j);
    element_copy(elem_j, &temp);
    element_dtor(&temp);
    
    return 0;
}

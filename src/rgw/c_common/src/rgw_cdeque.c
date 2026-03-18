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
 * @file rgw_cdeque.c
 * @brief Double-ended queue implementation using dynamic array (替代 std::deque)
 */

#include "containers/rgw_cdeque.h"
#include "containers/rgw_cmemory.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct rgw_deque_element {
    void *data;
    uint32_t len;
} rgw_deque_element_t;

#define DEFAULT_DEQUE_CAPACITY 16

struct rgw_deque_impl {
    rgw_deque_element_t *elements;
    size_t size;
    size_t capacity;
    void (*value_free)(void*);
};

struct rgw_deque_iterator_impl {
    const rgw_deque_t *deque;
    size_t current_index;
};

static int rgw_deque_reserve(rgw_deque_t *deque, size_t new_capacity)
{
    if (!deque || !deque->impl) {
        return -EINVAL;
    }
    
    if (new_capacity <= deque->impl->capacity) {
        return 0;
    }
    
    rgw_deque_element_t *new_elements = rgw_c_alloc(new_capacity * sizeof(rgw_deque_element_t));
    if (!new_elements) {
        return -ENOMEM;
    }
    
    // Copy existing elements
    for (size_t i = 0; i < deque->impl->size; i++) {
        new_elements[i].data = deque->impl->elements[i].data;
        new_elements[i].len = deque->impl->elements[i].len;
    }
    
    rgw_c_free(deque->impl->elements);
    deque->impl->elements = new_elements;
    deque->impl->capacity = new_capacity;
    
    return 0;
}

rgw_deque_t* rgw_deque_create(void (*value_free)(void*))
{
    rgw_deque_t *deque = rgw_c_alloc(sizeof(rgw_deque_t));
    if (!deque) {
        return NULL;
    }
    
    deque->impl = rgw_c_alloc(sizeof(struct rgw_deque_impl));
    if (!deque->impl) {
        rgw_c_free(deque);
        return NULL;
    }
    
    deque->impl->elements = rgw_c_alloc(DEFAULT_DEQUE_CAPACITY * sizeof(rgw_deque_element_t));
    if (!deque->impl->elements) {
        rgw_c_free(deque->impl);
        rgw_c_free(deque);
        return NULL;
    }
    
    deque->impl->size = 0;
    deque->impl->capacity = DEFAULT_DEQUE_CAPACITY;
    deque->impl->value_free = value_free;
    
    return deque;
}

void rgw_deque_destroy(rgw_deque_t *deque)
{
    if (!deque || !deque->impl) {
        return;
    }
    
    // Free all elements
    for (size_t i = 0; i < deque->impl->size; i++) {
        if (deque->impl->elements[i].data) {
            if (deque->impl->value_free) {
                deque->impl->value_free(deque->impl->elements[i].data);
            } else {
                rgw_c_free(deque->impl->elements[i].data);
            }
        }
    }
    
    rgw_c_free(deque->impl->elements);
    rgw_c_free(deque->impl);
    rgw_c_free(deque);
}

int rgw_deque_push_back(rgw_deque_t *deque, const void *value, uint32_t value_len)
{
    if (!deque || !deque->impl || !value || value_len == 0) {
        return -EINVAL;
    }
    
    // Expand if needed
    if (deque->impl->size >= deque->impl->capacity) {
        size_t new_capacity = deque->impl->capacity * 2;
        if (rgw_deque_reserve(deque, new_capacity) != 0) {
            return -ENOMEM;
        }
    }
    
    // Allocate and copy data
    void *data = rgw_c_alloc(value_len);
    if (!data) {
        return -ENOMEM;
    }
    rgw_c_memcpy(data, value, value_len);
    
    deque->impl->elements[deque->impl->size].data = data;
    deque->impl->elements[deque->impl->size].len = value_len;
    deque->impl->size++;
    
    return 0;
}

int rgw_deque_push_front(rgw_deque_t *deque, const void *value, uint32_t value_len)
{
    if (!deque || !deque->impl || !value || value_len == 0) {
        return -EINVAL;
    }
    
    // Expand if needed
    if (deque->impl->size >= deque->impl->capacity) {
        size_t new_capacity = deque->impl->capacity * 2;
        if (rgw_deque_reserve(deque, new_capacity) != 0) {
            return -ENOMEM;
        }
    }
    
    // Allocate and copy data
    void *data = rgw_c_alloc(value_len);
    if (!data) {
        return -ENOMEM;
    }
    rgw_c_memcpy(data, value, value_len);
    
    // Shift all elements to the right
    for (size_t i = deque->impl->size; i > 0; i--) {
        deque->impl->elements[i].data = deque->impl->elements[i - 1].data;
        deque->impl->elements[i].len = deque->impl->elements[i - 1].len;
    }
    
    // Add new element at front
    deque->impl->elements[0].data = data;
    deque->impl->elements[0].len = value_len;
    deque->impl->size++;
    
    return 0;
}

int rgw_deque_pop_back(rgw_deque_t *deque)
{
    if (!deque || !deque->impl) {
        return -EINVAL;
    }
    
    if (deque->impl->size == 0) {
        return -EINVAL;
    }
    
    // Free the last element
    size_t idx = deque->impl->size - 1;
    if (deque->impl->elements[idx].data) {
        if (deque->impl->value_free) {
            deque->impl->value_free(deque->impl->elements[idx].data);
        } else {
            rgw_c_free(deque->impl->elements[idx].data);
        }
        deque->impl->elements[idx].data = NULL;
        deque->impl->elements[idx].len = 0;
    }
    
    deque->impl->size--;
    
    return 0;
}

int rgw_deque_pop_front(rgw_deque_t *deque)
{
    if (!deque || !deque->impl) {
        return -EINVAL;
    }
    
    if (deque->impl->size == 0) {
        return -EINVAL;
    }
    
    // Free the first element
    if (deque->impl->elements[0].data) {
        if (deque->impl->value_free) {
            deque->impl->value_free(deque->impl->elements[0].data);
        } else {
            rgw_c_free(deque->impl->elements[0].data);
        }
    }
    
    // Shift all elements to the left
    for (size_t i = 0; i < deque->impl->size - 1; i++) {
        deque->impl->elements[i].data = deque->impl->elements[i + 1].data;
        deque->impl->elements[i].len = deque->impl->elements[i + 1].len;
    }
    
    deque->impl->size--;
    
    return 0;
}

const void* rgw_deque_get(const rgw_deque_t *deque, size_t index, uint32_t *value_len)
{
    if (!deque || !deque->impl || index >= deque->impl->size) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    if (value_len) {
        *value_len = deque->impl->elements[index].len;
    }
    
    return deque->impl->elements[index].data;
}

void* rgw_deque_get_mut(rgw_deque_t *deque, size_t index, uint32_t *value_len)
{
    if (!deque || !deque->impl || index >= deque->impl->size) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    if (value_len) {
        *value_len = deque->impl->elements[index].len;
    }
    
    return deque->impl->elements[index].data;
}

const void* rgw_deque_front(const rgw_deque_t *deque, uint32_t *value_len)
{
    return rgw_deque_get(deque, 0, value_len);
}

const void* rgw_deque_back(const rgw_deque_t *deque, uint32_t *value_len)
{
    if (!deque || !deque->impl || deque->impl->size == 0) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    return rgw_deque_get(deque, deque->impl->size - 1, value_len);
}

bool rgw_deque_empty(const rgw_deque_t *deque)
{
    return deque && deque->impl && deque->impl->size == 0;
}

size_t rgw_deque_size(const rgw_deque_t *deque)
{
    return deque && deque->impl ? deque->impl->size : 0;
}

void rgw_deque_clear(rgw_deque_t *deque)
{
    if (!deque || !deque->impl) {
        return;
    }
    
    // Free all elements
    for (size_t i = 0; i < deque->impl->size; i++) {
        if (deque->impl->elements[i].data) {
            if (deque->impl->value_free) {
                deque->impl->value_free(deque->impl->elements[i].data);
            } else {
                rgw_c_free(deque->impl->elements[i].data);
            }
            deque->impl->elements[i].data = NULL;
            deque->impl->elements[i].len = 0;
        }
    }
    
    deque->impl->size = 0;
}

rgw_deque_iterator_t rgw_deque_begin(const rgw_deque_t *deque)
{
    rgw_deque_iterator_t iter = {0};
    
    if (!deque || !deque->impl || deque->impl->size == 0) {
        return iter;
    }
    
    iter.impl = rgw_c_alloc(sizeof(struct rgw_deque_iterator_impl));
    if (!iter.impl) {
        return iter;
    }
    
    iter.impl->deque = deque;
    iter.impl->current_index = 0;
    iter.valid = true;
    
    return iter;
}

rgw_deque_iterator_t rgw_deque_end(const rgw_deque_t *deque)
{
    rgw_deque_iterator_t iter = {0};
    (void)deque;
    return iter;
}

void rgw_deque_iterator_next(rgw_deque_iterator_t *iter)
{
    if (!iter || !iter->impl || !iter->valid) {
        return;
    }
    
    iter->impl->current_index++;
    
    if (iter->impl->current_index >= iter->impl->deque->impl->size) {
        iter->valid = false;
    }
}

bool rgw_deque_iterator_valid(const rgw_deque_iterator_t *iter)
{
    return iter && iter->impl && iter->valid;
}

const void* rgw_deque_iterator_value(const rgw_deque_iterator_t *iter, uint32_t *value_len)
{
    if (!iter || !iter->impl || !iter->valid) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    return rgw_deque_get(iter->impl->deque, iter->impl->current_index, value_len);
}

void rgw_deque_iterator_destroy(rgw_deque_iterator_t *iter)
{
    if (!iter || !iter->impl) {
        return;
    }
    
    rgw_c_free(iter->impl);
    iter->impl = NULL;
    iter->valid = false;
}

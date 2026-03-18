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
 * @file rgw_cdeque.h
 * @brief Double-ended queue implementation (替代 std::deque)
 * 
 * Features:
 * - Dynamic array-based implementation with automatic expansion
 * - O(1) insert/erase at both ends
 * - O(1) random access by index
 * - Supports custom value types via void* with free callback
 * 
 * Usage example:
 * @code
 *   rgw_deque_t *deque = rgw_deque_create(free_callback);
 *   rgw_deque_push_back(deque, data1, size1);
 *   rgw_deque_push_front(deque, data2, size2);
 *   const void *elem = rgw_deque_get(deque, 0, NULL);
 *   rgw_deque_destroy(deque);
 * @endcode
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward declarations
 */
typedef struct rgw_deque_impl rgw_deque_impl_t;
typedef struct rgw_deque_iterator_impl rgw_deque_iterator_impl_t;

/**
 * Deque container (opaque type)
 */
typedef struct rgw_deque {
    rgw_deque_impl_t *impl;
} rgw_deque_t;

/**
 * Deque iterator (opaque type)
 */
typedef struct rgw_deque_iterator {
    rgw_deque_iterator_impl_t *impl;
    bool valid;
} rgw_deque_iterator_t;

/**
 * @brief Create a new deque
 * @param value_free Optional callback to free values, or NULL to use free()
 * @return New deque, or NULL on failure
 */
rgw_deque_t* rgw_deque_create(void (*value_free)(void*));

/**
 * @brief Destroy a deque and all its elements
 * @param deque Deque to destroy
 */
void rgw_deque_destroy(rgw_deque_t *deque);

/**
 * @brief Add element to the back of the deque
 * @param deque Deque to add to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_deque_push_back(rgw_deque_t *deque, const void *value, uint32_t value_len);

/**
 * @brief Add element to the front of the deque
 * @param deque Deque to add to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_deque_push_front(rgw_deque_t *deque, const void *value, uint32_t value_len);

/**
 * @brief Remove element from the back of the deque
 * @param deque Deque to remove from
 * @return 0 on success, -EINVAL if deque is empty
 */
int rgw_deque_pop_back(rgw_deque_t *deque);

/**
 * @brief Remove element from the front of the deque
 * @param deque Deque to remove from
 * @return 0 on success, -EINVAL if deque is empty
 */
int rgw_deque_pop_front(rgw_deque_t *deque);

/**
 * @brief Get element at specific position
 * @param deque Deque to get from
 * @param index Position to get (0-based)
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if index out of bounds
 */
const void* rgw_deque_get(const rgw_deque_t *deque, size_t index, uint32_t *value_len);

/**
 * @brief Get mutable element at specific position
 * @param deque Deque to get from
 * @param index Position to get
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if index out of bounds
 */
void* rgw_deque_get_mut(rgw_deque_t *deque, size_t index, uint32_t *value_len);

/**
 * @brief Get element at the front
 * @param deque Deque to query
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if deque is empty
 */
const void* rgw_deque_front(const rgw_deque_t *deque, uint32_t *value_len);

/**
 * @brief Get element at the back
 * @param deque Deque to query
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if deque is empty
 */
const void* rgw_deque_back(const rgw_deque_t *deque, uint32_t *value_len);

/**
 * @brief Check if deque is empty
 * @param deque Deque to check
 * @return true if empty, false otherwise
 */
bool rgw_deque_empty(const rgw_deque_t *deque);

/**
 * @brief Get number of elements in deque
 * @param deque Deque to query
 * @return Number of elements, or 0 if deque is NULL
 */
size_t rgw_deque_size(const rgw_deque_t *deque);

/**
 * @brief Remove all elements from deque
 * @param deque Deque to clear
 */
void rgw_deque_clear(rgw_deque_t *deque);

/**
 * @brief Get iterator to first element
 * @param deque Deque to iterate
 * @return Iterator to first element
 */
rgw_deque_iterator_t rgw_deque_begin(const rgw_deque_t *deque);

/**
 * @brief Get iterator to end (past-last element)
 * @param deque Deque to iterate
 * @return End iterator
 */
rgw_deque_iterator_t rgw_deque_end(const rgw_deque_t *deque);

/**
 * @brief Advance iterator to next element
 * @param iter Iterator to advance
 */
void rgw_deque_iterator_next(rgw_deque_iterator_t *iter);

/**
 * @brief Check if iterator is valid
 * @param iter Iterator to check
 * @return true if valid, false if at end
 */
bool rgw_deque_iterator_valid(const rgw_deque_iterator_t *iter);

/**
 * @brief Get value at current iterator position
 * @param iter Iterator
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if iterator is invalid
 */
const void* rgw_deque_iterator_value(const rgw_deque_iterator_t *iter, uint32_t *value_len);

/**
 * @brief Destroy iterator
 * @param iter Iterator to destroy
 */
void rgw_deque_iterator_destroy(rgw_deque_iterator_t *iter);

#ifdef __cplusplus
}
#endif

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
 * @file rgw_carray.h
 * @brief Dynamic array implementation (替代 std::vector)
 * 
 * Features:
 * - O(1) amortized append operations
 * - O(1) random access by index
 * - Automatic capacity growth (1.5x or 2x)
 * - Support for element copy/free callbacks
 * - Value type: buffer (void* with length)
 * 
 * Usage example:
 * @code
 *   rgw_array_t *arr = rgw_array_create(0);
 *   rgw_array_append(arr, data, data_len);
 *   const void *elem = rgw_array_get(arr, 0, &elem_len);
 *   rgw_array_destroy(arr);
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
 * Dynamic array element structure
 */
typedef struct rgw_array_element {
    void *data;
    uint32_t len;
} rgw_array_element_t;

/**
 * Dynamic array implementation
 */
typedef struct rgw_array rgw_array_t;

/**
 * Create a new dynamic array
 * @param initial_capacity Initial capacity (0 for default: 16)
 * @return Pointer to new array, or NULL on failure
 */
rgw_array_t* rgw_array_create(size_t initial_capacity);

/**
 * Destroy an array and free all memory
 * @param array Array to destroy
 */
void rgw_array_destroy(rgw_array_t *array);

/**
 * Append an element to the end of the array
 * @param array Array to append to
 * @param data Data to append (will be copied)
 * @param len Length of data
 * @return 0 on success, negative error code on failure
 */
int rgw_array_append(rgw_array_t *array, const void *data, uint32_t len);

/**
 * Insert an element at a specific position
 * @param array Array to insert into
 * @param index Position to insert at (0 <= index <= size)
 * @param data Data to insert (will be copied)
 * @param len Length of data
 * @return 0 on success, negative error code on failure
 */
int rgw_array_insert(rgw_array_t *array, size_t index, const void *data, uint32_t len);

/**
 * Remove an element at a specific position
 * @param array Array to remove from
 * @param index Position to remove (0 <= index < size)
 * @return 0 on success, negative error code on failure
 */
int rgw_array_erase(rgw_array_t *array, size_t index);

/**
 * Get an element by index
 * @param array Array to query
 * @param index Element index (0 <= index < size)
 * @param[out] len Optional output for element length
 * @return Pointer to element data, or NULL if index out of bounds
 */
const void* rgw_array_get(const rgw_array_t *array, size_t index, uint32_t *len);

/**
 * Get mutable element by index
 * @param array Array to query
 * @param index Element index
 * @param[out] len Optional output for element length
 * @return Pointer to element data, or NULL if index out of bounds
 */
void* rgw_array_get_mut(rgw_array_t *array, size_t index, uint32_t *len);

/**
 * Get the number of elements in the array
 * @param array Array to query
 * @return Number of elements
 */
size_t rgw_array_size(const rgw_array_t *array);

/**
 * Check if the array is empty
 * @param array Array to check
 * @return true if empty, false otherwise
 */
bool rgw_array_empty(const rgw_array_t *array);

/**
 * Get the current capacity of the array
 * @param array Array to query
 * @return Current capacity
 */
size_t rgw_array_capacity(const rgw_array_t *array);

/**
 * Clear all elements from the array
 * @param array Array to clear
 */
void rgw_array_clear(rgw_array_t *array);

/**
 * Reserve capacity for future elements
 * @param array Array to reserve for
 * @param capacity Minimum capacity to reserve
 * @return 0 on success, negative error code on failure
 */
int rgw_array_reserve(rgw_array_t *array, size_t capacity);

/**
 * Resize the array to contain n elements
 * @param array Array to resize
 * @param n New size
 * @param default_val Default value for new elements (can be NULL)
 * @param default_len Length of default value
 * @return 0 on success, negative error code on failure
 */
int rgw_array_resize(rgw_array_t *array, size_t n, const void *default_val, uint32_t default_len);

/**
 * Swap two elements in the array
 * @param array Array to modify
 * @param i First element index
 * @param j Second element index
 * @return 0 on success, negative error code on failure
 */
int rgw_array_swap(rgw_array_t *array, size_t i, size_t j);

#ifdef __cplusplus
}
#endif

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
 * @file rgw_clist.h
 * @brief Doubly linked list implementation (alternative to std::list)
 *
 * Features:
 * - O(1) insert at head/tail, remove from any position
 * - Supports iteration in both directions
 * - Based on rgw_list.h (proven doubly-linked list implementation)
 * - Supports custom value types via void* with free callback
 *
 * Usage example:
 * @code
 *   rgw_list_t *list = rgw_list_create(free_callback);
 *   rgw_list_add_tail(list, value1, size1);
 *   rgw_list_add_head(list, value2, size2);
 *   void *val = rgw_list_get(list, 0, NULL);  // Get first element
 *   rgw_list_destroy(list);
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
typedef struct rgw_clist_impl rgw_clist_impl_t;
typedef struct rgw_clist_iterator_impl rgw_clist_iterator_impl_t;

/**
 * List container (opaque type)
 */
typedef struct rgw_clist {
    rgw_clist_impl_t *impl;
} rgw_clist_t;

/**
 * List iterator (opaque type)
 */
typedef struct rgw_clist_iterator {
    rgw_clist_iterator_impl_t *impl;
} rgw_clist_iterator_t;

/**
 * @brief Create a new list
 * @param value_free Optional callback to free values, or NULL to use free()
 * @return New list, or NULL on failure
 */
rgw_clist_t* rgw_clist_create(void (*value_free)(void*));

/**
 * @brief Destroy a list and all its elements
 * @param list List to destroy
 */
void rgw_clist_destroy(rgw_clist_t *list);

/**
 * @brief Add element to the tail of the list
 * @param list List to add to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_clist_add_tail(rgw_clist_t *list, const void *value, uint32_t value_len);

/**
 * @brief Add element to the head of the list
 * @param list List to add to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_clist_add_head(rgw_clist_t *list, const void *value, uint32_t value_len);

/**
 * @brief Add element at specific position
 * @param list List to add to
 * @param index Position to insert at (0-based)
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_clist_insert(rgw_clist_t *list, size_t index, const void *value, uint32_t value_len);

/**
 * @brief Get element at specific position
 * @param list List to get from
 * @param index Position to get (0-based)
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if index out of bounds
 */
const void* rgw_clist_get(const rgw_clist_t *list, size_t index, uint32_t *value_len);

/**
 * @brief Remove element at specific position
 * @param list List to remove from
 * @param index Position to remove (0-based)
 * @return 0 on success, -EINVAL on invalid parameters, -ENOENT if index out of bounds
 */
int rgw_clist_remove(rgw_clist_t *list, size_t index);

/**
 * @brief Check if list contains a specific value
 * @param list List to search
 * @param value Value to search for
 * @param value_len Length of value
 * @return true if found, false otherwise
 */
bool rgw_clist_contains(const rgw_clist_t *list, const void *value, uint32_t value_len);

/**
 * @brief Get number of elements in list
 * @param list List to query
 * @return Number of elements, or 0 if list is NULL
 */
size_t rgw_clist_size(const rgw_clist_t *list);

/**
 * @brief Check if list is empty
 * @param list List to query
 * @return true if empty, false otherwise
 */
bool rgw_clist_empty(const rgw_clist_t *list);

/**
 * @brief Remove all elements from list
 * @param list List to clear
 */
void rgw_clist_clear(rgw_clist_t *list);

/**
 * @brief Get iterator to first element
 * @param list List to iterate
 * @return Iterator to first element, or invalid iterator if list is empty
 */
rgw_clist_iterator_t rgw_clist_begin(const rgw_clist_t *list);

/**
 * @brief Get iterator to end (past-last element)
 * @param list List to iterate
 * @return End iterator
 */
rgw_clist_iterator_t rgw_clist_end(const rgw_clist_t *list);

/**
 * @brief Advance iterator to next element
 * @param iter Iterator to advance
 */
void rgw_clist_iterator_next(rgw_clist_iterator_t *iter);

/**
 * @brief Check if iterator is valid
 * @param iter Iterator to check
 * @return true if valid, false if at end
 */
bool rgw_clist_iterator_valid(const rgw_clist_iterator_t *iter);

/**
 * @brief Get value at current iterator position
 * @param iter Iterator
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if iterator is invalid
 */
const void* rgw_clist_iterator_value(const rgw_clist_iterator_t *iter, uint32_t *value_len);

/**
 * @brief Destroy iterator
 * @param iter Iterator to destroy
 */
void rgw_clist_iterator_destroy(rgw_clist_iterator_t *iter);

#ifdef __cplusplus
}
#endif

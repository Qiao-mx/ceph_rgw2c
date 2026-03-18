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
 * @file rgw_cpriority_queue.h
 * @brief Priority Queue implementation (替代 std::priority_queue)
 *
 * Features:
 * - Binary heap based priority queue
 * - O(log n) push and pop operations
 * - O(1) top operation
 * - Configurable compare function (min-heap or max-heap)
 * - Supports custom value types via void* with free callback
 *
 * Usage example:
 * @code
 *   // For min-heap: compare returns negative if a < b
 *   // For max-heap: compare returns positive if a < b
 *   int compare_int(const void *a, const void *b) {
 *       return *(int*)a - *(int*)b;  // min-heap
 *   }
 *
 *   rgw_priority_queue_t *pq = rgw_priority_queue_create(compare_int, free);
 *   rgw_priority_queue_push(pq, &val1, sizeof(val1));
 *   rgw_priority_queue_push(pq, &val2, sizeof(val2));
 *   int *top = (int*)rgw_priority_queue_top(pq, NULL);
 *   rgw_priority_queue_pop(pq);
 *   rgw_priority_queue_destroy(pq);
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
 * Priority Queue container (opaque type)
 */
typedef struct rgw_priority_queue rgw_priority_queue_t;

/**
 * @brief Create a new priority queue
 * @param compare Compare function: returns negative if a < b, 0 if a == b, positive if a > b
 *               For min-heap: return (*a) - (*b)
 *               For max-heap: return (*b) - (*a)
 * @param value_free Optional callback to free values, or NULL to not auto-free
 * @return New priority queue, or NULL on failure
 */
rgw_priority_queue_t* rgw_priority_queue_create(int (*compare)(const void*, const void*),
                                                 void (*value_free)(void*));

/**
 * @brief Destroy a priority queue and all its elements
 * @param pq Priority queue to destroy
 */
void rgw_priority_queue_destroy(rgw_priority_queue_t *pq);

/**
 * @brief Push element into priority queue
 * @param pq Priority queue
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_priority_queue_push(rgw_priority_queue_t *pq, const void *value, uint32_t value_len);

/**
 * @brief Pop element from priority queue (removes and returns the top element)
 * @param pq Priority queue
 * @param value Pointer to store the popped value (can be NULL if value_len is NULL)
 * @param value_len If not NULL, will be set to the length of popped value
 * @return 0 on success, -EINVAL if priority queue is empty
 */
int rgw_priority_queue_pop(rgw_priority_queue_t *pq, void *value, uint32_t *value_len);

/**
 * @brief Get top element without removing it
 * @param pq Priority queue to query
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if priority queue is empty
 */
const void* rgw_priority_queue_top(const rgw_priority_queue_t *pq, uint32_t *value_len);

/**
 * @brief Check if priority queue is empty
 * @param pq Priority queue to check
 * @return true if empty, false otherwise
 */
bool rgw_priority_queue_empty(const rgw_priority_queue_t *pq);

/**
 * @brief Get number of elements in priority queue
 * @param pq Priority queue to query
 * @return Number of elements, or 0 if pq is NULL
 */
size_t rgw_priority_queue_size(const rgw_priority_queue_t *pq);

/**
 * @brief Remove all elements from priority queue
 * @param pq Priority queue to clear
 */
void rgw_priority_queue_clear(rgw_priority_queue_t *pq);

#ifdef __cplusplus
}
#endif

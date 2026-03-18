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
 * @file rgw_cqueue.h
 * @brief Queue implementation (替代 std::queue)
 * 
 * Features:
 * - FIFO (First In First Out) data structure
 * - O(1) push_back and pop_front operations
 * - Based on doubly-linked list
 * - Supports custom value types via void* with free callback
 * 
 * Usage example:
 * @code
 *   rgw_queue_t *queue = rgw_queue_create(free_callback);
 *   rgw_queue_push(queue, data1, size1);
 *   rgw_queue_push(queue, data2, size2);
 *   const void *front = rgw_queue_front(queue, NULL);
 *   rgw_queue_pop(queue);
 *   rgw_queue_destroy(queue);
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
 * Queue container (opaque type)
 */
typedef struct rgw_queue rgw_queue_t;

/**
 * @brief Create a new queue
 * @param value_free Optional callback to free values, or NULL to use free()
 * @return New queue, or NULL on failure
 */
rgw_queue_t* rgw_queue_create(void (*value_free)(void*));

/**
 * @brief Destroy a queue and all its elements
 * @param queue Queue to destroy
 */
void rgw_queue_destroy(rgw_queue_t *queue);

/**
 * @brief Push element to the back of queue
 * @param queue Queue to push to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_queue_push(rgw_queue_t *queue, const void *value, uint32_t value_len);

/**
 * @brief Pop element from the front of queue
 * @param queue Queue to pop from
 * @return 0 on success, -EINVAL if queue is empty
 */
int rgw_queue_pop(rgw_queue_t *queue);

/**
 * @brief Get front element without removing it
 * @param queue Queue to query
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if queue is empty
 */
const void* rgw_queue_front(const rgw_queue_t *queue, uint32_t *value_len);

/**
 * @brief Get back element without removing it
 * @param queue Queue to query
 * @param value_len If not NULL, will be
 * @return Pointer to value, set to value length or NULL if queue is empty
 */
const void* rgw_queue_back(const rgw_queue_t *queue, uint32_t *value_len);

/**
 * @brief Check if queue is empty
 * @param queue Queue to check
 * @return true if empty, false otherwise
 */
bool rgw_queue_empty(const rgw_queue_t *queue);

/**
 * @brief Get number of elements in queue
 * @param queue Queue to query
 * @return Number of elements, or 0 if queue is NULL
 */
size_t rgw_queue_size(const rgw_queue_t *queue);

/**
 * @brief Remove all elements from queue
 * @param queue Queue to clear
 */
void rgw_queue_clear(rgw_queue_t *queue);

#ifdef __cplusplus
}
#endif

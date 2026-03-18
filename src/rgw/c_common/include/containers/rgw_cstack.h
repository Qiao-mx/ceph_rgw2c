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
 * @file rgw_cstack.h
 * @brief Stack implementation (替代 std::stack)
 * 
 * Features:
 * - LIFO (Last In First Out) data structure
 * - O(1) push, pop, top operations
 * - Based on utstack.h (singly-linked list)
 * - Supports custom value types via void* with free callback
 * 
 * Usage example:
 * @code
 *   rgw_stack_t *stack = rgw_stack_create(free_callback);
 *   rgw_stack_push(stack, data1, size1);
 *   rgw_stack_push(stack, data2, size2);
 *   const void *top = rgw_stack_top(stack, NULL);
 *   rgw_stack_pop(stack);
 *   rgw_stack_destroy(stack);
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
 * Stack container (opaque type)
 */
typedef struct rgw_stack rgw_stack_t;

/**
 * @brief Create a new stack
 * @param value_free Optional callback to free values, or NULL to use free()
 * @return New stack, or NULL on failure
 */
rgw_stack_t* rgw_stack_create(void (*value_free)(void*));

/**
 * @brief Destroy a stack and all its elements
 * @param stack Stack to destroy
 */
void rgw_stack_destroy(rgw_stack_t *stack);

/**
 * @brief Push element onto stack
 * @param stack Stack to push to
 * @param value Pointer to value data
 * @param value_len Length of value data
 * @return 0 on success, -EINVAL on invalid parameters, -ENOMEM on memory allocation failure
 */
int rgw_stack_push(rgw_stack_t *stack, const void *value, uint32_t value_len);

/**
 * @brief Pop element from stack
 * @param stack Stack to pop from
 * @return 0 on success, -EINVAL if stack is empty
 */
int rgw_stack_pop(rgw_stack_t *stack);

/**
 * @brief Get top element without removing it
 * @param stack Stack to query
 * @param value_len If not NULL, will be set to value length
 * @return Pointer to value, or NULL if stack is empty
 */
const void* rgw_stack_top(const rgw_stack_t *stack, uint32_t *value_len);

/**
 * @brief Check if stack is empty
 * @param stack Stack to check
 * @return true if empty, false otherwise
 */
bool rgw_stack_empty(const rgw_stack_t *stack);

/**
 * @brief Get number of elements in stack
 * @param stack Stack to query
 * @return Number of elements, or 0 if stack is NULL
 */
size_t rgw_stack_size(const rgw_stack_t *stack);

/**
 * @brief Remove all elements from stack
 * @param stack Stack to clear
 */
void rgw_stack_clear(rgw_stack_t *stack);

#ifdef __cplusplus
}
#endif

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
 * @file rgw_coptional.h
 * @brief Optional value implementation (替代 std::optional)
 * 
 * Features:
 * - Type-safe container for optional values
 * - Can hold a value or be empty
 * - Support for any data type (void* with length)
 * - Value semantics (copy on assignment)
 * 
 * Usage example:
 * @code
 *   rgw_optional_t opt = {0};
 *   rgw_optional_set(&opt, value, value_len);
 *   if (rgw_optional_has_value(&opt)) {
 *       const void *val = rgw_optional_get(&opt, &val_len);
 *   }
 *   rgw_optional_destroy(&opt);
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
 * Optional value structure
 * 
 * Use as:
 *   rgw_optional_t opt = {0};  // Initialize to empty
 */
typedef struct rgw_optional {
    void *value;
    uint32_t len;
    bool has_value;
} rgw_optional_t;

/**
 * Initialize an optional value (to empty state)
 * @param opt Optional to initialize
 */
void rgw_optional_init(rgw_optional_t *opt);

/**
 * Set a value in the optional
 * @param opt Optional to set
 * @param value Value to store (will be copied)
 * @param len Length of value
 * @return 0 on success, negative error code on failure
 */
int rgw_optional_set(rgw_optional_t *opt, const void *value, uint32_t len);

/**
 * Set a value from a C-string
 * @param opt Optional to set
 * @param cstr C-string to store
 * @return 0 on success, negative error code on failure
 */
int rgw_optional_set_string(rgw_optional_t *opt, const char *cstr);

/**
 * Check if optional has a value
 * @param opt Optional to check
 * @return true if has value, false otherwise
 */
bool rgw_optional_has_value(const rgw_optional_t *opt);

/**
 * Get the value (const)
 * @param opt Optional to query
 * @param[out] len Optional output for value length
 * @return Pointer to value, or NULL if empty
 */
const void* rgw_optional_get(const rgw_optional_t *opt, uint32_t *len);

/**
 * Get the value (mutable)
 * @param opt Optional to query
 * @param[out] len Optional output for value length
 * @return Pointer to value, or NULL if empty
 */
void* rgw_optional_get_mut(rgw_optional_t *opt, uint32_t *len);

/**
 * Get value as C-string (only valid if value is a string)
 * @param opt Optional to query
 * @return C-string pointer, or NULL if empty
 */
const char* rgw_optional_get_string(const rgw_optional_t *opt);

/**
 * Clear the optional (set to empty)
 * @param opt Optional to clear
 */
void rgw_optional_clear(rgw_optional_t *opt);

/**
 * Destroy the optional and free memory
 * @param opt Optional to destroy
 */
void rgw_optional_destroy(rgw_optional_t *opt);

/**
 * Copy an optional value
 * @param dest Destination optional
 * @param src Source optional
 * @return 0 on success, negative error code on failure
 */
int rgw_optional_copy(rgw_optional_t *dest, const rgw_optional_t *src);

/**
 * Move an optional value (source becomes empty)
 * @param dest Destination optional
 * @param src Source optional (will be cleared)
 */
void rgw_optional_move(rgw_optional_t *dest, rgw_optional_t *src);

/**
 * Swap two optional values
 * @param a First optional
 * @param b Second optional
 */
void rgw_optional_swap(rgw_optional_t *a, rgw_optional_t *b);

/**
 * Compare two optional values
 * @param a First optional
 * @param b Second optional
 * @return true if both empty or both have equal values
 */
bool rgw_optional_equals(const rgw_optional_t *a, const rgw_optional_t *b);

#ifdef __cplusplus
}
#endif

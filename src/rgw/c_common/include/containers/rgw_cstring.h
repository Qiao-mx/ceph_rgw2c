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
 * @file rgw_cstring.h
 * @brief String implementation (替代 std::string)
 * 
 * Features:
 * - Automatic memory management
 * - O(1) length query
 * - O(n) copy, append, compare
 * - C-string compatibility
 * - Substring operations
 * 
 * Usage example:
 * @code
 *   rgw_string_t *str = rgw_string_create("hello");
 *   rgw_string_append(str, " world");
 *   const char *cstr = rgw_string_c_str(str);
 *   rgw_string_destroy(str);
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
 * String implementation (替代 std::string)
 */
typedef struct rgw_string rgw_string_t;

/**
 * Create a new string from C-string
 * @param cstr C-string to copy (can be NULL for empty string)
 * @return Pointer to new string, or NULL on failure
 */
rgw_string_t* rgw_string_create(const char *cstr);

/**
 * Create a new string with initial capacity
 * @param capacity Initial capacity
 * @return Pointer to new string, or NULL on failure
 */
rgw_string_t* rgw_string_create_with_capacity(size_t capacity);

/**
 * Create a string from binary data
 * @param data Data to copy
 * @param len Length of data
 * @return Pointer to new string, or NULL on failure
 */
rgw_string_t* rgw_string_create_from_data(const void *data, size_t len);

/**
 * Destroy a string and free all memory
 * @param str String to destroy
 */
void rgw_string_destroy(rgw_string_t *str);

/**
 * Assign a new value to the string
 * @param str String to assign to
 * @param cstr C-string to copy
 * @return 0 on success, negative error code on failure
 */
int rgw_string_assign(rgw_string_t *str, const char *cstr);

/**
 * Append a C-string to the end of the string
 * @param str String to append to
 * @param cstr C-string to append
 * @return 0 on success, negative error code on failure
 */
int rgw_string_append(rgw_string_t *str, const char *cstr);

/**
 * Append a formatted string (like sprintf)
 * @param str String to append to
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return 0 on success, negative error code on failure
 */
int rgw_string_append_format(rgw_string_t *str, const char *format, ...);

/**
 * Append binary data to the string
 * @param str String to append to
 * @param data Data to append
 * @param len Length of data
 * @return 0 on success, negative error code on failure
 */
int rgw_string_append_data(rgw_string_t *str, const void *data, size_t len);

/**
 * Insert a C-string at a position
 * @param str String to insert into
 * @param pos Position to insert at
 * @param cstr C-string to insert
 * @return 0 on success, negative error code on failure
 */
int rgw_string_insert(rgw_string_t *str, size_t pos, const char *cstr);

/**
 * Erase characters from the string
 * @param str String to erase from
 * @param pos Starting position
 * @param len Number of characters to erase
 * @return 0 on success, negative error code on failure
 */
int rgw_string_erase(rgw_string_t *str, size_t pos, size_t len);

/**
 * Replace a portion of the string
 * @param str String to modify
 * @param pos Starting position of replace
 * @param len Length of portion to replace
 * @param cstr Replacement string
 * @return 0 on success, negative error code on failure
 */
int rgw_string_replace(rgw_string_t *str, size_t pos, size_t len, const char *cstr);

/**
 * Get the C-string (null-terminated)
 * @param str String to query
 * @return C-string pointer (do not free)
 */
const char* rgw_string_c_str(const rgw_string_t *str);

/**
 * Get string data (may contain null characters)
 * @param str String to query
 * @return Pointer to data (do not free)
 */
const void* rgw_string_data(const rgw_string_t *str);

/**
 * Get the length of the string
 * @param str String to query
 * @return Length in bytes
 */
size_t rgw_string_length(const rgw_string_t *str);

/**
 * Get the capacity of the string
 * @param str String to query
 * @return Current capacity
 */
size_t rgw_string_capacity(const rgw_string_t *str);

/**
 * Check if the string is empty
 * @param str String to check
 * @return true if empty, false otherwise
 */
bool rgw_string_empty(const rgw_string_t *str);

/**
 * Clear the string contents
 * @param str String to clear
 */
void rgw_string_clear(rgw_string_t *str);

/**
 * Compare two strings
 * @param str1 First string
 * @param str2 Second string
 * @return 0 if equal, negative if str1 < str2, positive if str1 > str2
 */
int rgw_string_compare(const rgw_string_t *str1, const rgw_string_t *str2);

/**
 * Compare string with C-string
 * @param str String
 * @param cstr C-string
 * @return 0 if equal, negative if str < cstr, positive if str > cstr
 */
int rgw_string_compare_cstr(const rgw_string_t *str, const char *cstr);

/**
 * Find a substring
 * @param str String to search in
 * @param substr Substring to find
 * @return Position of first occurrence, or (size_t)-1 if not found
 */
size_t rgw_string_find(const rgw_string_t *str, const char *substr);

/**
 * Find a character
 * @param str String to search in
 * @param c Character to find
 * @return Position of first occurrence, or (size_t)-1 if not found
 */
size_t rgw_string_find_char(const rgw_string_t *str, char c);

/**
 * Get a substring
 * @param str String to get substring from
 * @param pos Starting position
 * @param len Length of substring
 * @return New string (caller must free with rgw_string_destroy), or NULL on failure
 */
rgw_string_t* rgw_string_substring(const rgw_string_t *str, size_t pos, size_t len);

/**
 * Trim whitespace from both ends
 * @param str String to trim
 */
void rgw_string_trim(rgw_string_t *str);

/**
 * Convert to uppercase
 * @param str String to convert
 */
void rgw_string_to_upper(rgw_string_t *str);

/**
 * Convert to lowercase
 * @param str String to convert
 */
void rgw_string_to_lower(rgw_string_t *str);

/**
 * Duplicate a string
 * @param str String to duplicate
 * @return New string (caller must free with rgw_string_destroy), or NULL on failure
 */
rgw_string_t* rgw_string_dup(const rgw_string_t *str);

#ifdef __cplusplus
}
#endif

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
 * @file rgw_cmap.h
 * @brief Ordered map implementation using rbt_tree (替代 std::map)
 * 
 * Features:
 * - O(log n) insert, find, erase operations
 * - Maintains keys in sorted order (lexicographical for strings)
 * - Based on rbt_tree.h (proven red-black tree implementation)
 * - Supports custom value types via void* with free callback
 * 
 * Usage example:
 * @code
 *   rgw_map_t *mymap = rgw_map_create(free_callback);
 *   rgw_map_insert(mymap, "key", value, value_size);
 *   void *found = rgw_map_find(mymap, "key", NULL);
 *   rgw_map_destroy(mymap);
 * @endcode
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "internal/rgw_rbtree.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward declarations
 */
typedef struct rgw_map_impl rgw_map_impl_t;
typedef struct rgw_map_iterator_impl rgw_map_iterator_impl_t;

/**
 * Map iterator structure
 */
typedef struct rgw_map_iterator {
    rgw_map_iterator_impl_t *impl;
    bool valid;
} rgw_map_iterator_t;

/**
 * Ordered map data structure (替代 std::map)
 * 
 * Usage scenarios:
 * - Object extended attributes: std::map<std::string, bufferlist> xattrs
 * - Authentication key-value mapping: std::map<std::string, std::string> val_map
 * - Storage statistics: std::map<RGWObjCategory, RGWStorageStats> stats
 * - Configuration management
 */
typedef struct rgw_map {
    rgw_map_impl_t *impl;
} rgw_map_t;

/**
 * Create a new map
 * @param value_free Callback function to free values (can be NULL)
 * @return Pointer to new map, or NULL on failure
 */
rgw_map_t* rgw_map_create(void (*value_free)(void*));

/**
 * Destroy a map and free all memory
 * @param map Map to destroy
 */
void rgw_map_destroy(rgw_map_t *map);

/**
 * Insert or update a key-value pair
 * @param map Map to insert into
 * @param key Null-terminated string key
 * @param value Value to insert (will be copied)
 * @param value_len Length of value in bytes
 * @return 0 on success, negative error code on failure
 */
int rgw_map_insert(rgw_map_t *map, const char *key, const void *value, uint32_t value_len);

/**
 * Find a value by key
 * @param map Map to search
 * @param key Null-terminated string key
 * @param[out] value_len Optional output parameter for value length
 * @return Pointer to value (do not free), or NULL if not found
 */
const void* rgw_map_find(const rgw_map_t *map, const char *key, uint32_t *value_len);

/**
 * Erase a key from the map
 * @param map Map to erase from
 * @param key Null-terminated string key
 * @return 0 on success, -ENOENT if not found
 */
int rgw_map_erase(rgw_map_t *map, const char *key);

/**
 * Check if a key exists in the map
 * @param map Map to search
 * @param key Null-terminated string key
 * @return true if key exists, false otherwise
 */
bool rgw_map_contains(const rgw_map_t *map, const char *key);

/**
 * Get the number of elements in the map
 * @param map Map to query
 * @return Number of elements
 */
size_t rgw_map_size(const rgw_map_t *map);

/**
 * Check if the map is empty
 * @param map Map to check
 * @return true if empty, false otherwise
 */
bool rgw_map_empty(const rgw_map_t *map);

/**
 * Clear all elements from the map
 * @param map Map to clear
 */
void rgw_map_clear(rgw_map_t *map);

/**
 * Get an iterator to the beginning of the map
 * @param map Map to iterate over
 * @return Iterator positioned at first element
 */
rgw_map_iterator_t rgw_map_begin(const rgw_map_t *map);

/**
 * Get an iterator to the end of the map
 * @param map Map to iterate over
 * @return Iterator positioned past last element
 */
rgw_map_iterator_t rgw_map_end(const rgw_map_t *map);

/**
 * Advance an iterator to the next element
 * @param iter Iterator to advance
 */
void rgw_map_iterator_next(rgw_map_iterator_t *iter);

/**
 * Check if iterator is valid
 * @param iter Iterator to check
 * @return true if iterator points to valid element
 */
bool rgw_map_iterator_valid(const rgw_map_iterator_t *iter);

/**
 * Get current key from iterator
 * @param iter Iterator to query
 * @return Current key (null-terminated string, do not free)
 */
const char* rgw_map_iterator_key(const rgw_map_iterator_t *iter);

/**
 * Get current value from iterator
 * @param iter Iterator to query
 * @param[out] value_len Optional output parameter for value length
 * @return Current value (do not free), or NULL if invalid
 */
const void* rgw_map_iterator_value(const rgw_map_iterator_t *iter, uint32_t *value_len);

/**
 * Destroy an iterator
 * @param iter Iterator to destroy
 */
void rgw_map_iterator_destroy(rgw_map_iterator_t *iter);

/**
 * Convenience macro for iterating over a map
 * Usage:
 * @code
 *   rgw_map_foreach(map, key, value, value_len) {
 *       // process key, value, value_len
 *   }
 * @endcode
 */
#define rgw_map_foreach(map, key_var, value_var, value_len_var) \
    do { \
        rgw_map_iterator_t _iter = rgw_map_begin(map); \
        while (rgw_map_iterator_valid(&_iter)) { \
            const char *key_var = rgw_map_iterator_key(&_iter); \
            const void *value_var = rgw_map_iterator_value(&_iter, &value_len_var); \
            if (value_var)

#define rgw_map_foreach_end() \
            rgw_map_iterator_next(&_iter); \
        } \
        rgw_map_iterator_destroy(&_iter); \
    } while(0)

#ifdef __cplusplus
}
#endif

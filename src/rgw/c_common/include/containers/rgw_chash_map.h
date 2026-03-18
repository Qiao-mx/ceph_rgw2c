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
 * @file rgw_chash_map.h
 * @brief Hash map implementation using uthash (替代 std::unordered_map)
 * 
 * Features:
 * - O(1) average case insert, find, erase
 * - Based on uthash library
 * - String keys with generic void* values
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rgw_hash_map_impl rgw_hash_map_impl_t;
typedef struct rgw_hash_map_iterator_impl rgw_hash_map_iterator_impl_t;

typedef struct rgw_hash_map_iterator {
    rgw_hash_map_iterator_impl_t *impl;
    bool valid;
} rgw_hash_map_iterator_t;

typedef struct rgw_hash_map {
    rgw_hash_map_impl_t *impl;
} rgw_hash_map_t;

rgw_hash_map_t* rgw_hash_map_create(void (*value_free)(void*));
void rgw_hash_map_destroy(rgw_hash_map_t *map);
int rgw_hash_map_insert(rgw_hash_map_t *map, const char *key, const void *value, uint32_t value_len);
const void* rgw_hash_map_find(const rgw_hash_map_t *map, const char *key, uint32_t *value_len);
int rgw_hash_map_erase(rgw_hash_map_t *map, const char *key);
bool rgw_hash_map_contains(const rgw_hash_map_t *map, const char *key);
size_t rgw_hash_map_size(const rgw_hash_map_t *map);
bool rgw_hash_map_empty(const rgw_hash_map_t *map);
void rgw_hash_map_clear(rgw_hash_map_t *map);

rgw_hash_map_iterator_t rgw_hash_map_begin(const rgw_hash_map_t *map);
rgw_hash_map_iterator_t rgw_hash_map_end(const rgw_hash_map_t *map);
void rgw_hash_map_iterator_next(rgw_hash_map_iterator_t *iter);
bool rgw_hash_map_iterator_valid(const rgw_hash_map_iterator_t *iter);
const char* rgw_hash_map_iterator_key(const rgw_hash_map_iterator_t *iter);
const void* rgw_hash_map_iterator_value(const rgw_hash_map_iterator_t *iter, uint32_t *value_len);
void rgw_hash_map_iterator_destroy(rgw_hash_map_iterator_t *iter);

#define rgw_hash_map_foreach(map, key_var, value_var, value_len_var) \
    do { \
        rgw_hash_map_iterator_t _iter = rgw_hash_map_begin(map); \
        while (rgw_hash_map_iterator_valid(&_iter)) { \
            const char *key_var = rgw_hash_map_iterator_key(&_iter); \
            const void *value_var = rgw_hash_map_iterator_value(&_iter, &value_len_var); \
            if (value_var)

#define rgw_hash_map_foreach_end() \
            rgw_hash_map_iterator_next(&_iter); \
        } \
        rgw_hash_map_iterator_destroy(&_iter); \
    } while(0)

#ifdef __cplusplus
}
#endif

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
 * @file rgw_cset.h
 * @brief Ordered set implementation using rbt_tree (替代 std::set)
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "internal/rgw_rbtree.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rgw_set_impl rgw_set_impl_t;
typedef struct rgw_set_iterator_impl rgw_set_iterator_impl_t;

typedef struct rgw_set_iterator {
    rgw_set_iterator_impl_t *impl;
    bool valid;
} rgw_set_iterator_t;

typedef struct rgw_set {
    rgw_set_impl_t *impl;
} rgw_set_t;

rgw_set_t* rgw_set_create_string(void);
void rgw_set_destroy(rgw_set_t *set);
int rgw_set_insert_string(rgw_set_t *set, const char *key);
bool rgw_set_contains_string(const rgw_set_t *set, const char *key);
int rgw_set_erase_string(rgw_set_t *set, const char *key);
size_t rgw_set_size(const rgw_set_t *set);
bool rgw_set_empty(const rgw_set_t *set);
void rgw_set_clear(rgw_set_t *set);

rgw_set_iterator_t rgw_set_begin(const rgw_set_t *set);
rgw_set_iterator_t rgw_set_end(const rgw_set_t *set);
void rgw_set_iterator_next(rgw_set_iterator_t *iter);
bool rgw_set_iterator_valid(const rgw_set_iterator_t *iter);
const void* rgw_set_iterator_key(const rgw_set_iterator_t *iter, uint32_t *key_len);
void rgw_set_iterator_destroy(rgw_set_iterator_t *iter);

#define rgw_set_foreach(set, key_var, key_len_var) \
    do { \
        rgw_set_iterator_t _iter = rgw_set_begin(set); \
        while (rgw_set_iterator_valid(&_iter)) { \
            const void *key_var = rgw_set_iterator_key(&_iter, &key_len_var); \
            if (key_var)

#define rgw_set_foreach_end() \
            rgw_set_iterator_next(&_iter); \
        } \
        rgw_set_iterator_destroy(&_iter); \
    } while(0)

#ifdef __cplusplus
}
#endif

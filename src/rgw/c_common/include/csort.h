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
 * @file csort.h
 * @brief Sorting algorithms for C containers
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Comparison function type for sorting
 * @param a Pointer to first element
 * @param b Pointer to second element
 * @return negative if a < b, 0 if a == b, positive if a > b
 */
typedef int (*csort_compare_fn)(const void *a, const void *b);

/**
 * @brief Quick sort implementation
 * @param base Pointer to array to sort
 * @param nmemb Number of elements
 * @param size Size of each element in bytes
 * @param cmp Comparison function
 */
void csort_qsort(void *base, size_t nmemb, size_t size, csort_compare_fn cmp);

/**
 * @brief Binary search in sorted array
 * @param key Key to search for
 * @param base Pointer to sorted array
 * @param nmemb Number of elements
 * @param size Size of each element in bytes
 * @param cmp Comparison function
 * @return Pointer to found element, or NULL if not found
 */
void* csort_bsearch(const void *key, const void *base, size_t nmemb, size_t size, csort_compare_fn cmp);

/**
 * @brief Find first element matching predicate in array
 * @param predicate Function that returns true for matching elements
 * @param base Pointer to array
 * @param nmemb Number of elements
 * @param size Size of each element in bytes
 * @return Pointer to first matching element, or NULL if not found
 */
typedef int (*csort_predicate_fn)(const void *element, void *user_data);

void* csort_find_if(csort_predicate_fn predicate, void *user_data,
                    void *base, size_t nmemb, size_t size);

#ifdef __cplusplus
}
#endif

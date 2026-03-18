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

#include "csort.h"
#include <stdlib.h>
#include <string.h>

static void swap_bytes(char *a, char *b, size_t size)
{
    char tmp;
    for (size_t i = 0; i < size; i++) {
        tmp = a[i];
        a[i] = b[i];
        b[i] = tmp;
    }
}

static char* median_of_three(char *a, char *b, char *c, size_t size, csort_compare_fn cmp)
{
    if (cmp(a, b) < 0) {
        if (cmp(b, c) < 0) return b;
        if (cmp(a, c) < 0) return c;
        return a;
    }
    if (cmp(a, c) < 0) return a;
    if (cmp(b, c) < 0) return c;
    return b;
}

static char* partition(char *base, size_t nmemb, size_t size, csort_compare_fn cmp)
{
    char *pivot = base + (nmemb - 1) * size;
    char *i = base - size;
    char *j = base;

    for (; j < pivot; j += size) {
        if (cmp(j, pivot) < 0) {
            i += size;
            if (i != j) {
                swap_bytes(i, j, size);
            }
        }
    }

    if (i + size != pivot) {
        swap_bytes(i + size, pivot, size);
    }
    return i + size;
}

static void quick_sort_impl(char *base, size_t nmemb, size_t size, csort_compare_fn cmp)
{
    if (nmemb <= 1) return;
    if (nmemb <= 7) {
        for (size_t i = 1; i < nmemb; i++) {
            char *key = base + i * size;
            char *j = key - size;
            while (j >= base && cmp(j, key) > 0) {
                swap_bytes(j, j + size, size);
                j -= size;
            }
        }
        return;
    }

    char *p = median_of_three(base, base + (nmemb / 2) * size, base + (nmemb - 1) * size, size, cmp);
    if (p != base + (nmemb - 1) * size) {
        swap_bytes(p, base + (nmemb - 1) * size, size);
    }

    char *pi = partition(base, nmemb, size, cmp);
    size_t left_count = pi - base;
    quick_sort_impl(base, left_count / size, size, cmp);
    quick_sort_impl(pi + size, nmemb - left_count / size - 1, size, cmp);
}

void csort_qsort(void *base, size_t nmemb, size_t size, csort_compare_fn cmp)
{
    if (!base || nmemb <= 1 || size == 0 || !cmp) return;
    quick_sort_impl((char*)base, nmemb, size, cmp);
}

void* csort_bsearch(const void *key, const void *base, size_t nmemb, size_t size, csort_compare_fn cmp)
{
    if (!key || !base || nmemb == 0 || size == 0 || !cmp) return NULL;

    size_t left = 0;
    size_t right = nmemb;

    while (left < right) {
        size_t mid = left + (right - left) / 2;
        const char *mid_elem = (const char*)base + mid * size;
        int comp = cmp(key, mid_elem);

        if (comp == 0) {
            return (void*)mid_elem;
        } else if (comp < 0) {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    return NULL;
}

void* csort_find_if(csort_predicate_fn predicate, void *user_data,
                    void *base, size_t nmemb, size_t size)
{
    if (!predicate || !base || nmemb == 0 || size == 0) return NULL;

    char *elem = (char*)base;
    for (size_t i = 0; i < nmemb; i++, elem += size) {
        if (predicate(elem, user_data)) {
            return elem;
        }
    }
    return NULL;
}

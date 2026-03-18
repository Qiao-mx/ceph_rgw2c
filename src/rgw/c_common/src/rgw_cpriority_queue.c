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
 * @file rgw_cpriority_queue.c
 * @brief Priority Queue implementation using binary heap (替代 std::priority_queue)
 */

#include "containers/rgw_cpriority_queue.h"
#include "containers/rgw_cmemory.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define DEFAULT_PQ_CAPACITY 16

typedef struct rgw_pq_element {
    void *data;
    uint32_t len;
} rgw_pq_element_t;

struct rgw_priority_queue {
    rgw_pq_element_t *elements;
    size_t capacity;
    size_t size;
    int (*compare)(const void*, const void*);
    void (*value_free)(void*);
};

static int expand_capacity(rgw_priority_queue_t *pq)
{
    if (pq == NULL) {
        return -EINVAL;
    }

    size_t new_capacity = pq->capacity * 2;
    rgw_pq_element_t *new_elements = rgw_c_realloc(pq->elements,
                                                     new_capacity * sizeof(rgw_pq_element_t));
    if (new_elements == NULL) {
        return -ENOMEM;
    }

    pq->elements = new_elements;
    pq->capacity = new_capacity;
    return 0;
}

static void swap_elements(rgw_pq_element_t *a, rgw_pq_element_t *b)
{
    rgw_pq_element_t tmp = *a;
    *a = *b;
    *b = tmp;
}

static void heapify_up(rgw_priority_queue_t *pq, size_t index)
{
    if (pq == NULL || index == 0) {
        return;
    }

    size_t parent = (index - 1) / 2;

    while (index > 0 && pq->compare(pq->elements[parent].data,
                                     pq->elements[index].data) > 0) {
        swap_elements(&pq->elements[parent], &pq->elements[index]);
        index = parent;
        parent = (index - 1) / 2;
    }
}

static void heapify_down(rgw_priority_queue_t *pq, size_t index)
{
    if (pq == NULL) {
        return;
    }

    size_t left = 2 * index + 1;
    size_t right = 2 * index + 2;
    size_t smallest = index;

    while (left < pq->size) {
        if (pq->compare(pq->elements[smallest].data,
                        pq->elements[left].data) > 0) {
            smallest = left;
        }

        if (right < pq->size &&
            pq->compare(pq->elements[smallest].data,
                        pq->elements[right].data) > 0) {
            smallest = right;
        }

        if (smallest != index) {
            swap_elements(&pq->elements[index], &pq->elements[smallest]);
            index = smallest;
            left = 2 * index + 1;
            right = 2 * index + 2;
        } else {
            break;
        }
    }
}

rgw_priority_queue_t* rgw_priority_queue_create(int (*compare)(const void*, const void*),
                                                 void (*value_free)(void*))
{
    if (compare == NULL) {
        return NULL;
    }

    rgw_priority_queue_t *pq = rgw_c_alloc(sizeof(rgw_priority_queue_t));
    if (pq == NULL) {
        return NULL;
    }

    pq->elements = rgw_c_alloc(DEFAULT_PQ_CAPACITY * sizeof(rgw_pq_element_t));
    if (pq->elements == NULL) {
        rgw_c_free(pq);
        return NULL;
    }

    pq->capacity = DEFAULT_PQ_CAPACITY;
    pq->size = 0;
    pq->compare = compare;
    pq->value_free = value_free;

    return pq;
}

void rgw_priority_queue_destroy(rgw_priority_queue_t *pq)
{
    if (pq == NULL) {
        return;
    }

    rgw_priority_queue_clear(pq);
    rgw_c_free(pq->elements);
    rgw_c_free(pq);
}

int rgw_priority_queue_push(rgw_priority_queue_t *pq, const void *value, uint32_t value_len)
{
    if (pq == NULL || value == NULL || value_len == 0) {
        return -EINVAL;
    }

    if (pq->size >= pq->capacity) {
        int ret = expand_capacity(pq);
        if (ret != 0) {
            return ret;
        }
    }

    void *data_copy = rgw_c_alloc(value_len);
    if (data_copy == NULL) {
        return -ENOMEM;
    }
    rgw_c_memcpy(data_copy, value, value_len);

    pq->elements[pq->size].data = data_copy;
    pq->elements[pq->size].len = value_len;
    pq->size++;

    heapify_up(pq, pq->size - 1);

    return 0;
}

int rgw_priority_queue_pop(rgw_priority_queue_t *pq, void *value, uint32_t *value_len)
{
    if (pq == NULL || pq->size == 0) {
        return -EINVAL;
    }

    if (value != NULL && value_len != NULL) {
        rgw_c_memcpy(value, pq->elements[0].data, pq->elements[0].len);
        *value_len = pq->elements[0].len;
    }

    if (pq->value_free != NULL && pq->elements[0].data != NULL) {
        pq->value_free(pq->elements[0].data);
    }

    pq->size--;

    if (pq->size > 0) {
        pq->elements[0].data = pq->elements[pq->size].data;
        pq->elements[0].len = pq->elements[pq->size].len;
        pq->elements[pq->size].data = NULL;
        pq->elements[pq->size].len = 0;

        heapify_down(pq, 0);
    }

    return 0;
}

const void* rgw_priority_queue_top(const rgw_priority_queue_t *pq, uint32_t *value_len)
{
    if (pq == NULL || pq->size == 0) {
        if (value_len != NULL) {
            *value_len = 0;
        }
        return NULL;
    }

    if (value_len != NULL) {
        *value_len = pq->elements[0].len;
    }

    return pq->elements[0].data;
}

bool rgw_priority_queue_empty(const rgw_priority_queue_t *pq)
{
    return pq == NULL || pq->size == 0;
}

size_t rgw_priority_queue_size(const rgw_priority_queue_t *pq)
{
    if (pq == NULL) {
        return 0;
    }
    return pq->size;
}

void rgw_priority_queue_clear(rgw_priority_queue_t *pq)
{
    if (pq == NULL) {
        return;
    }

    if (pq->value_free != NULL) {
        for (size_t i = 0; i < pq->size; i++) {
            if (pq->elements[i].data != NULL) {
                pq->value_free(pq->elements[i].data);
            }
        }
    }

    pq->size = 0;
}

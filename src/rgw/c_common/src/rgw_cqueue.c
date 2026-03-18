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
 * @file rgw_cqueue.c
 * @brief Queue implementation using doubly-linked list (替代 std::queue)
 */

#include "containers/rgw_cqueue.h"
#include "containers/rgw_cmemory.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct rgw_queue_element {
    void *data;
    uint32_t len;
    struct rgw_queue_element *prev;
    struct rgw_queue_element *next;
} rgw_queue_element_t;

struct rgw_queue {
    rgw_queue_element_t *head;  // front of queue
    rgw_queue_element_t *tail;  // back of queue
    size_t size;
    void (*value_free)(void*);
};

rgw_queue_t* rgw_queue_create(void (*value_free)(void*))
{
    rgw_queue_t *queue = rgw_c_alloc(sizeof(rgw_queue_t));
    if (!queue) {
        return NULL;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->value_free = value_free;
    
    return queue;
}

void rgw_queue_destroy(rgw_queue_t *queue)
{
    if (!queue) {
        return;
    }
    
    rgw_queue_clear(queue);
    rgw_c_free(queue);
}

int rgw_queue_push(rgw_queue_t *queue, const void *value, uint32_t value_len)
{
    if (!queue || !value || value_len == 0) {
        return -EINVAL;
    }
    
    rgw_queue_element_t *elem = rgw_c_alloc(sizeof(rgw_queue_element_t));
    if (!elem) {
        return -ENOMEM;
    }
    
    elem->data = rgw_c_alloc(value_len);
    if (!elem->data) {
        rgw_c_free(elem);
        return -ENOMEM;
    }
    rgw_c_memcpy(elem->data, value, value_len);
    elem->len = value_len;
    elem->prev = NULL;
    elem->next = NULL;
    
    if (!queue->tail) {
        // Empty queue
        queue->head = elem;
        queue->tail = elem;
    } else {
        // Add to back
        elem->prev = queue->tail;
        queue->tail->next = elem;
        queue->tail = elem;
    }
    
    queue->size++;
    
    return 0;
}

int rgw_queue_pop(rgw_queue_t *queue)
{
    if (!queue || !queue->head) {
        return -EINVAL;
    }
    
    rgw_queue_element_t *elem = queue->head;
    queue->head = elem->next;
    
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        // Queue is now empty
        queue->tail = NULL;
    }
    
    if (elem->data) {
        if (queue->value_free) {
            queue->value_free(elem->data);
        } else {
            rgw_c_free(elem->data);
        }
    }
    
    rgw_c_free(elem);
    queue->size--;
    
    return 0;
}

const void* rgw_queue_front(const rgw_queue_t *queue, uint32_t *value_len)
{
    if (!queue || !queue->head) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    if (value_len) {
        *value_len = queue->head->len;
    }
    
    return queue->head->data;
}

const void* rgw_queue_back(const rgw_queue_t *queue, uint32_t *value_len)
{
    if (!queue || !queue->tail) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    if (value_len) {
        *value_len = queue->tail->len;
    }
    
    return queue->tail->data;
}

bool rgw_queue_empty(const rgw_queue_t *queue)
{
    return !queue || queue->size == 0;
}

size_t rgw_queue_size(const rgw_queue_t *queue)
{
    return queue ? queue->size : 0;
}

void rgw_queue_clear(rgw_queue_t *queue)
{
    if (!queue) {
        return;
    }
    
    rgw_queue_element_t *elem = queue->head;
    while (elem) {
        rgw_queue_element_t *next = elem->next;
        
        if (elem->data) {
            if (queue->value_free) {
                queue->value_free(elem->data);
            } else {
                rgw_c_free(elem->data);
            }
        }
        
        rgw_c_free(elem);
        elem = next;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
}

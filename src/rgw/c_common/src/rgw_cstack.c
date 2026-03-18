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
 * @file rgw_cstack.c
 * @brief Stack implementation using utstack (替代 std::stack)
 */

#include "containers/rgw_cstack.h"
#include "containers/rgw_cmemory.h"
#include "internal/utstack.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef struct rgw_stack_element {
    void *data;
    uint32_t len;
    struct rgw_stack_element *next;
} rgw_stack_element_t;

struct rgw_stack {
    rgw_stack_element_t *head;
    size_t size;
    void (*value_free)(void*);
};

rgw_stack_t* rgw_stack_create(void (*value_free)(void*))
{
    rgw_stack_t *stack = rgw_c_alloc(sizeof(rgw_stack_t));
    if (!stack) {
        return NULL;
    }
    
    stack->head = NULL;
    stack->size = 0;
    stack->value_free = value_free;
    
    return stack;
}

void rgw_stack_destroy(rgw_stack_t *stack)
{
    if (!stack) {
        return;
    }
    
    rgw_stack_clear(stack);
    rgw_c_free(stack);
}

int rgw_stack_push(rgw_stack_t *stack, const void *value, uint32_t value_len)
{
    if (!stack || !value || value_len == 0) {
        return -EINVAL;
    }
    
    rgw_stack_element_t *elem = rgw_c_alloc(sizeof(rgw_stack_element_t));
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
    elem->next = stack->head;
    
    stack->head = elem;
    stack->size++;
    
    return 0;
}

int rgw_stack_pop(rgw_stack_t *stack)
{
    if (!stack || !stack->head) {
        return -EINVAL;
    }
    
    rgw_stack_element_t *elem = stack->head;
    stack->head = elem->next;
    
    if (elem->data) {
        if (stack->value_free) {
            stack->value_free(elem->data);
        } else {
            rgw_c_free(elem->data);
        }
    }
    
    rgw_c_free(elem);
    stack->size--;
    
    return 0;
}

const void* rgw_stack_top(const rgw_stack_t *stack, uint32_t *value_len)
{
    if (!stack || !stack->head) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    
    if (value_len) {
        *value_len = stack->head->len;
    }
    
    return stack->head->data;
}

bool rgw_stack_empty(const rgw_stack_t *stack)
{
    return !stack || stack->size == 0;
}

size_t rgw_stack_size(const rgw_stack_t *stack)
{
    return stack ? stack->size : 0;
}

void rgw_stack_clear(rgw_stack_t *stack)
{
    if (!stack) {
        return;
    }
    
    rgw_stack_element_t *elem = stack->head;
    while (elem) {
        rgw_stack_element_t *next = elem->next;
        
        if (elem->data) {
            if (stack->value_free) {
                stack->value_free(elem->data);
            } else {
                rgw_c_free(elem->data);
            }
        }
        
        rgw_c_free(elem);
        elem = next;
    }
    
    stack->head = NULL;
    stack->size = 0;
}

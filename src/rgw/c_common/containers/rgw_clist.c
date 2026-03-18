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

#include "../include/containers/rgw_clist.h"
#include "../include/internal/rgw_list.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct rgw_clist_node {
    struct rgw_list_head node;
    void *value;
    uint32_t value_len;
};

struct rgw_clist_impl {
    struct rgw_list_head head;
    size_t size;
    void (*value_free)(void*);
};

struct rgw_clist_iterator_impl {
    struct rgw_clist_node *current;
    const struct rgw_list_head *head;
};

static struct rgw_clist_node* create_node(const void *value, uint32_t value_len)
{
    struct rgw_clist_node *node = malloc(sizeof(struct rgw_clist_node));
    if (!node) return NULL;
    node->value = malloc(value_len);
    if (!node->value) { free(node); return NULL; }
    memcpy(node->value, value, value_len);
    node->value_len = value_len;
    return node;
}

static void free_node(struct rgw_clist_node *node, void (*value_free)(void*))
{
    if (!node) return;
    if (value_free && node->value)
        value_free(node->value);
    else
        free(node->value);
    free(node);
}

rgw_clist_t* rgw_clist_create(void (*value_free)(void*))
{
    rgw_clist_t *list = malloc(sizeof(rgw_clist_t));
    if (!list) return NULL;
    list->impl = malloc(sizeof(struct rgw_clist_impl));
    if (!list->impl) { free(list); return NULL; }
    rgw_list_init(&list->impl->head);
    list->impl->size = 0;
    list->impl->value_free = value_free;
    return list;
}

void rgw_clist_destroy(rgw_clist_t *list)
{
    if (!list || !list->impl) return;
    rgw_clist_clear(list);
    free(list->impl);
    free(list);
}

int rgw_clist_add_tail(rgw_clist_t *list, const void *value, uint32_t value_len)
{
    if (!list || !list->impl || !value || value_len == 0) return -EINVAL;

    struct rgw_clist_node *node = create_node(value, value_len);
    if (!node) return -ENOMEM;

    rgw_list_add_tail(&list->impl->head, &node->node);
    list->impl->size++;
    return 0;
}

int rgw_clist_add_head(rgw_clist_t *list, const void *value, uint32_t value_len)
{
    if (!list || !list->impl || !value || value_len == 0) return -EINVAL;

    struct rgw_clist_node *node = create_node(value, value_len);
    if (!node) return -ENOMEM;

    rgw_list_add(&list->impl->head, &node->node);
    list->impl->size++;
    return 0;
}

int rgw_clist_insert(rgw_clist_t *list, size_t index, const void *value, uint32_t value_len)
{
    if (!list || !list->impl || !value || value_len == 0) return -EINVAL;
    if (index > list->impl->size) return -EINVAL;

    if (index == 0) {
        return rgw_clist_add_head(list, value, value_len);
    }
    if (index == list->impl->size) {
        return rgw_clist_add_tail(list, value, value_len);
    }

    struct rgw_clist_node *new_node = create_node(value, value_len);
    if (!new_node) return -ENOMEM;

    struct rgw_list_head *current = list->impl->head.next;
    for (size_t i = 0; i < index; i++) {
        current = current->next;
    }

    new_node->node.prev = current->prev;
    new_node->node.next = current;
    current->prev->next = &new_node->node;
    current->prev = &new_node->node;
    list->impl->size++;
    return 0;
}

const void* rgw_clist_get(const rgw_clist_t *list, size_t index, uint32_t *value_len)
{
    if (!list || !list->impl || index >= list->impl->size) return NULL;

    struct rgw_list_head *current = list->impl->head.next;
    for (size_t i = 0; i < index; i++) {
        current = current->next;
    }

    struct rgw_clist_node *node = rgw_list_entry(current, struct rgw_clist_node, node);
    if (value_len) *value_len = node->value_len;
    return node->value;
}

int rgw_clist_remove(rgw_clist_t *list, size_t index)
{
    if (!list || !list->impl || index >= list->impl->size) return -EINVAL;

    struct rgw_list_head *current = list->impl->head.next;
    for (size_t i = 0; i < index; i++) {
        current = current->next;
    }

    rgw_list_del(current);
    struct rgw_clist_node *node = rgw_list_entry(current, struct rgw_clist_node, node);
    free_node(node, list->impl->value_free);
    list->impl->size--;
    return 0;
}

bool rgw_clist_contains(const rgw_clist_t *list, const void *value, uint32_t value_len)
{
    if (!list || !list->impl || !value) return false;

    struct rgw_list_head *pos;
    rgw_list_for_each(pos, &list->impl->head) {
        struct rgw_clist_node *node = rgw_list_entry(pos, struct rgw_clist_node, node);
        if (node->value_len == value_len && memcmp(node->value, value, value_len) == 0) {
            return true;
        }
    }
    return false;
}

size_t rgw_clist_size(const rgw_clist_t *list)
{
    return (list && list->impl) ? list->impl->size : 0;
}

bool rgw_clist_empty(const rgw_clist_t *list)
{
    return rgw_clist_size(list) == 0;
}

void rgw_clist_clear(rgw_clist_t *list)
{
    if (!list || !list->impl) return;

    struct rgw_list_head *pos, *n;
    rgw_list_for_each_safe(pos, n, &list->impl->head) {
        struct rgw_clist_node *node = rgw_list_entry(pos, struct rgw_clist_node, node);
        rgw_list_del(pos);
        free_node(node, list->impl->value_free);
    }
    list->impl->size = 0;
}

rgw_clist_iterator_t rgw_clist_begin(const rgw_clist_t *list)
{
    rgw_clist_iterator_t iter = {0};
    if (!list || !list->impl) return iter;

    iter.impl = malloc(sizeof(struct rgw_clist_iterator_impl));
    if (!iter.impl) return iter;

    iter.impl->head = &list->impl->head;
    iter.impl->current = (struct rgw_clist_node *)list->impl->head.next;
    return iter;
}

rgw_clist_iterator_t rgw_clist_end(const rgw_clist_t *list)
{
    rgw_clist_iterator_t iter = {0};
    if (!list || !list->impl) return iter;

    iter.impl = malloc(sizeof(struct rgw_clist_iterator_impl));
    if (!iter.impl) return iter;
    iter.impl->head = &list->impl->head;
    iter.impl->current = NULL;
    return iter;
}

void rgw_clist_iterator_next(rgw_clist_iterator_t *iter)
{
    if (!iter || !iter->impl || !iter->impl->current) return;
    iter->impl->current = (struct rgw_clist_node *)iter->impl->current->node.next;
}

bool rgw_clist_iterator_valid(const rgw_clist_iterator_t *iter)
{
    if (!iter || !iter->impl || !iter->impl->head) return false;
    if (!iter->impl->current) return false;
    return &iter->impl->current->node != iter->impl->head;
}

const void* rgw_clist_iterator_value(const rgw_clist_iterator_t *iter, uint32_t *value_len)
{
    if (!iter || !iter->impl || !iter->impl->current) return NULL;

    struct rgw_clist_node *node = iter->impl->current;
    if (value_len) *value_len = node->value_len;
    return node->value;
}

void rgw_clist_iterator_destroy(rgw_clist_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    free(iter->impl);
    iter->impl = NULL;
}

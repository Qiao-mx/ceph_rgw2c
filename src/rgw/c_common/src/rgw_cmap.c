// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "../include/containers/rgw_cmap.h"
#include "../include/containers/rgw_cmemory.h"
#include "../include/internal/rgw_rbt_node.h"
#include "../include/internal/rgw_rbtree.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

struct rgw_map_entry {
    struct rbt_node node;
    char *key;
    void *value;
    uint32_t value_len;
};

struct rgw_map_iterator_impl {
    struct rbt_node *current;
};

struct rgw_map_impl {
    struct rbt_head tree;
    size_t size;
    void (*value_free)(void*);
};

static int compare_keys(const char *a, const char *b)
{
    return strcmp(a, b);
}

static struct rgw_map_entry* create_entry(const char *key, const void *value, uint32_t value_len)
{
    struct rgw_map_entry *entry = malloc(sizeof(struct rgw_map_entry));
    if (!entry) return NULL;
    entry->key = strdup(key);
    if (!entry->key) { free(entry); return NULL; }
    entry->value = malloc(value_len);
    if (!entry->value) { free(entry->key); free(entry); return NULL; }
    memcpy(entry->value, value, value_len);
    entry->value_len = value_len;
    entry->node.rbt_value = (uint64_t)entry->key;
    entry->node.rbt_opaq = entry;
    return entry;
}

static void free_entry(struct rgw_map_entry *entry, void (*value_free)(void*))
{
    if (!entry) return;
    free(entry->key);
    if (value_free && entry->value) value_free(entry->value);
    else free(entry->value);
    free(entry);
}

rgw_map_t* rgw_map_create(void (*value_free)(void*))
{
    rgw_map_t *map = malloc(sizeof(rgw_map_t));
    if (!map) return NULL;
    map->impl = malloc(sizeof(struct rgw_map_impl));
    if (!map->impl) { free(map); return NULL; }
    RBT_HEAD_INIT(&map->impl->tree);
    map->impl->size = 0;
    map->impl->value_free = value_free;
    return map;
}

void rgw_map_destroy(rgw_map_t *map)
{
    if (!map || !map->impl) return;
    rgw_map_clear(map);
    free(map->impl);
    free(map);
}

int rgw_map_insert(rgw_map_t *map, const char *key, const void *value, uint32_t value_len)
{
    if (!map || !map->impl || !key || !value || value_len == 0) return -EINVAL;
    
    // Find insertion point
    struct rbt_node *parent = NULL;
    struct rbt_node **link = &map->impl->tree.root;
    
    while (*link) {
        parent = *link;
        struct rgw_map_entry *parent_entry = (struct rgw_map_entry*)(uintptr_t)parent->rbt_opaq;
        int cmp = compare_keys(key, parent_entry->key);
        
        if (cmp < 0)
            link = &parent->left;
        else if (cmp > 0)
            link = &parent->next;
        else {
            // Key exists, update value
            // Save old value first, then allocate new value before freeing old one
            void *old_value = parent_entry->value;
            void *new_value = malloc(value_len);
            if (!new_value) return -ENOMEM;
            memcpy(new_value, value, value_len);
            // Free old value after successful allocation
            if (map->impl->value_free && old_value)
                map->impl->value_free(old_value);
            else
                free(old_value);
            parent_entry->value = new_value;
            parent_entry->value_len = value_len;
            return 0;
        }
    }
    
    // Create new entry
    struct rgw_map_entry *entry = create_entry(key, value, value_len);
    if (!entry) return -ENOMEM;
    
    // Insert using RBT_INSERT
    RBT_INSERT(&map->impl->tree, &entry->node, parent);
    map->impl->size++;
    return 1;
}

const void* rgw_map_find(const rgw_map_t *map, const char *key, uint32_t *value_len)
{
    if (!map || !map->impl || !key) return NULL;

    // Traverse the tree using string comparison instead of pointer address
    struct rbt_node *node = map->impl->tree.root;
    while (node) {
        struct rgw_map_entry *entry = (struct rgw_map_entry*)(uintptr_t)node->rbt_opaq;
        int cmp = compare_keys(key, entry->key);

        if (cmp < 0) {
            node = node->left;
        } else if (cmp > 0) {
            node = node->next;
        } else {
            // Key found
            if (value_len) *value_len = entry->value_len;
            return entry->value;
        }
    }

    return NULL;
}

int rgw_map_erase(rgw_map_t *map, const char *key)
{
    if (!map || !map->impl || !key) return -EINVAL;

    // Traverse the tree using string comparison instead of pointer address
    struct rbt_node *node = map->impl->tree.root;
    struct rbt_node *parent = NULL;
    while (node) {
        struct rgw_map_entry *entry = (struct rgw_map_entry*)(uintptr_t)node->rbt_opaq;
        int cmp = compare_keys(key, entry->key);

        if (cmp < 0) {
            parent = node;
            node = node->left;
        } else if (cmp > 0) {
            parent = node;
            node = node->next;
        } else {
            // Key found, unlink and free
            RBT_UNLINK(&map->impl->tree, &entry->node);
            free_entry(entry, map->impl->value_free);
            map->impl->size--;
            return 0;
        }
    }

    return -ENOENT;
}

bool rgw_map_contains(const rgw_map_t *map, const char *key)
{
    return rgw_map_find(map, key, NULL) != NULL;
}

size_t rgw_map_size(const rgw_map_t *map) { return (map && map->impl) ? map->impl->size : 0; }
bool rgw_map_empty(const rgw_map_t *map) { return rgw_map_size(map) == 0; }

void rgw_map_clear(rgw_map_t *map)
{
    if (!map || !map->impl) return;

    // Manually traverse and clear to safely handle node deletion during iteration
    struct rbt_node *node = map->impl->tree.leftmost;
    while (node) {
        // Save next node before unlinking, as RBT_UNLINK modifies the tree
        struct rbt_node *next = node;
        RBT_INCREMENT(next);
        struct rgw_map_entry *entry = (struct rgw_map_entry*)(uintptr_t)node->rbt_opaq;
        RBT_UNLINK(&map->impl->tree, &entry->node);
        free_entry(entry, map->impl->value_free);
        node = next;
    }
    map->impl->size = 0;
}

rgw_map_iterator_t rgw_map_begin(const rgw_map_t *map)
{
    rgw_map_iterator_t iter = {0};
    if (!map || !map->impl) return iter;
    iter.impl = malloc(sizeof(struct rgw_map_iterator_impl));
    if (!iter.impl) return iter;
    iter.impl->current = RBT_LEFTMOST(&map->impl->tree);
    return iter;
}

rgw_map_iterator_t rgw_map_end(const rgw_map_t *map)
{
    rgw_map_iterator_t iter = {0};
    iter.impl = NULL;
    (void)map;
    return iter;
}

void rgw_map_iterator_next(rgw_map_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    struct rbt_node *it = iter->impl->current;
    if (it) {
        RBT_INCREMENT(it);
        iter->impl->current = it;
    }
}

bool rgw_map_iterator_valid(const rgw_map_iterator_t *iter)
{
    return iter && iter->impl && iter->impl->current != NULL;
}

const char* rgw_map_iterator_key(const rgw_map_iterator_t *iter)
{
    if (!iter || !iter->impl || !iter->impl->current) return NULL;
    struct rgw_map_entry *entry = (struct rgw_map_entry*)(uintptr_t)iter->impl->current->rbt_opaq;
    return entry->key;
}

const void* rgw_map_iterator_value(const rgw_map_iterator_t *iter, uint32_t *value_len)
{
    if (!iter || !iter->impl || !iter->impl->current) return NULL;
    struct rgw_map_entry *entry = (struct rgw_map_entry*)(uintptr_t)iter->impl->current->rbt_opaq;
    if (value_len) *value_len = entry->value_len;
    return entry->value;
}

void rgw_map_iterator_destroy(rgw_map_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    free(iter->impl);
    iter->impl = NULL;
}

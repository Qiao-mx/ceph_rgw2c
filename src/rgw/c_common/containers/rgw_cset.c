// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "containers/rgw_cset.h"
#include "internal/rgw_rbt_node.h"
#include "internal/rgw_rbtree.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

struct rgw_set_entry {
    struct rbt_node node;
    char *key;
};

struct rgw_set_iterator_impl {
    struct rbt_node *current;
};

struct rgw_set_impl {
    struct rbt_head tree;
    size_t size;
};

static int compare_keys(const char *a, const char *b)
{
    return strcmp(a, b);
}

static struct rgw_set_entry* create_entry(const char *key)
{
    struct rgw_set_entry *entry = malloc(sizeof(struct rgw_set_entry));
    if (!entry) return NULL;
    entry->key = strdup(key);
    if (!entry->key) { free(entry); return NULL; }
    entry->node.rbt_value = (uint64_t)(uintptr_t)entry->key;
    entry->node.rbt_opaq = entry;
    return entry;
}

static void free_entry(struct rgw_set_entry *entry)
{
    if (!entry) return;
    free(entry->key);
    free(entry);
}

rgw_set_t* rgw_set_create_string(void)
{
    rgw_set_t *set = malloc(sizeof(rgw_set_t));
    if (!set) return NULL;
    
    set->impl = malloc(sizeof(struct rgw_set_impl));
    if (!set->impl) { free(set); return NULL; }
    
    RBT_HEAD_INIT(&set->impl->tree);
    set->impl->size = 0;
    return set;
}

void rgw_set_destroy(rgw_set_t *set)
{
    if (!set || !set->impl) return;
    rgw_set_clear(set);
    free(set->impl);
    free(set);
}

int rgw_set_insert_string(rgw_set_t *set, const char *key)
{
    if (!set || !set->impl || !key) return -EINVAL;
    
    // Check if exists
    struct rbt_node *parent = NULL;
    struct rbt_node **link = &set->impl->tree.root;
    
    while (*link) {
        parent = *link;
        struct rgw_set_entry *entry = (struct rgw_set_entry*)parent->rbt_opaq;
        int cmp = compare_keys(key, entry->key);
        
        if (cmp < 0)
            link = &parent->left;
        else if (cmp > 0)
            link = &parent->next;
        else
            return 0;  // Already exists
    }
    
    struct rgw_set_entry *entry = create_entry(key);
    if (!entry) return -ENOMEM;
    
    RBT_INSERT(&set->impl->tree, &entry->node, parent);
    set->impl->size++;
    return 1;
}

bool rgw_set_contains_string(const rgw_set_t *set, const char *key)
{
    if (!set || !set->impl || !key) return false;
    
    struct rbt_node *found;
    uint64_t key_val = (uint64_t)(uintptr_t)key;
    RBT_FIND(&set->impl->tree, found, key_val);
    
    if (!found) return false;
    
    struct rgw_set_entry *entry = (struct rgw_set_entry*)found->rbt_opaq;
    return compare_keys(entry->key, key) == 0;
}

int rgw_set_erase_string(rgw_set_t *set, const char *key)
{
    if (!set || !set->impl || !key) return -EINVAL;
    
    struct rbt_node *found;
    uint64_t key_val = (uint64_t)(uintptr_t)key;
    RBT_FIND(&set->impl->tree, found, key_val);
    
    if (!found || compare_keys((const char*)(uintptr_t)found->rbt_value, key) != 0)
        return -ENOENT;
    
    struct rgw_set_entry *entry = (struct rgw_set_entry*)found->rbt_opaq;
    RBT_UNLINK(&set->impl->tree, &entry->node);
    free_entry(entry);
    set->impl->size--;
    return 0;
}

size_t rgw_set_size(const rgw_set_t *set)
{
    return set ? set->impl->size : 0;
}

bool rgw_set_empty(const rgw_set_t *set)
{
    return rgw_set_size(set) == 0;
}

void rgw_set_clear(rgw_set_t *set)
{
    if (!set || !set->impl) return;
    
    // Manually traverse and clear to safely handle node deletion during iteration
    struct rbt_node *node = set->impl->tree.leftmost;
    while (node) {
        // Save next node before unlinking, as RBT_UNLINK modifies the tree
        struct rbt_node *next = node;
        RBT_INCREMENT(next);
        struct rgw_set_entry *entry = (struct rgw_set_entry*)node->rbt_opaq;
        RBT_UNLINK(&set->impl->tree, &entry->node);
        free_entry(entry);
        node = next;
    }
    set->impl->size = 0;
}

rgw_set_iterator_t rgw_set_begin(const rgw_set_t *set)
{
    rgw_set_iterator_t iter = {0};
    if (!set || !set->impl) return iter;
    
    iter.impl = malloc(sizeof(struct rgw_set_iterator_impl));
    if (!iter.impl) return iter;
    
    iter.impl->current = RBT_LEFTMOST(&set->impl->tree);
    iter.valid = (iter.impl->current != NULL);
    return iter;
}

rgw_set_iterator_t rgw_set_end(const rgw_set_t *set)
{
    rgw_set_iterator_t iter = {0};
    iter.valid = false;
    (void)set;
    return iter;
}

void rgw_set_iterator_next(rgw_set_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    struct rbt_node *it = iter->impl->current;
    if (it) {
        RBT_INCREMENT(it);
        iter->impl->current = it;
    }
}

bool rgw_set_iterator_valid(const rgw_set_iterator_t *iter)
{
    return iter && iter->impl && iter->impl->current != NULL;
}

const void* rgw_set_iterator_key(const rgw_set_iterator_t *iter, uint32_t *key_len)
{
    if (!iter || !iter->impl || !iter->impl->current) {
        if (key_len) *key_len = 0;
        return NULL;
    }
    
    struct rgw_set_entry *entry = (struct rgw_set_entry*)iter->impl->current->rbt_opaq;
    size_t len = strlen(entry->key);
    if (key_len) *key_len = (uint32_t)len;
    return entry->key;
}

void rgw_set_iterator_destroy(rgw_set_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    free(iter->impl);
    iter->impl = NULL;
}

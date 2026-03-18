// -*- mode:C; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
#include "../include/containers/rgw_chash_map.h"
#include "../include/internal/uthash.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct hash_entry {
    char *key;
    void *value;
    uint32_t value_len;
    UT_hash_handle hh;
};

struct rgw_hash_map_impl {
    struct hash_entry *head;
    size_t size;
    void (*value_free)(void*);
};

struct rgw_hash_map_iterator_impl {
    struct hash_entry *current;
};

rgw_hash_map_t* rgw_hash_map_create(void (*value_free)(void*))
{
    rgw_hash_map_t *map = malloc(sizeof(rgw_hash_map_t));
    if (!map) return NULL;
    
    map->impl = malloc(sizeof(struct rgw_hash_map_impl));
    if (!map->impl) { free(map); return NULL; }
    
    map->impl->head = NULL;
    map->impl->size = 0;
    map->impl->value_free = value_free;
    return map;
}

void rgw_hash_map_destroy(rgw_hash_map_t *map)
{
    if (!map || !map->impl) return;
    rgw_hash_map_clear(map);
    free(map->impl);
    free(map);
}

int rgw_hash_map_insert(rgw_hash_map_t *map, const char *key, const void *value, uint32_t value_len)
{
    if (!map || !map->impl || !key || !value || value_len == 0) return -EINVAL;
    
    struct hash_entry *entry;
    HASH_FIND_STR(map->impl->head, key, entry);
    
    if (entry) {
        if (map->impl->value_free && entry->value)
            map->impl->value_free(entry->value);
        else
            free(entry->value);
        entry->value = malloc(value_len);
        if (!entry->value) return -ENOMEM;
        memcpy(entry->value, value, value_len);
        entry->value_len = value_len;
        return 0;
    }
    
    entry = malloc(sizeof(struct hash_entry));
    if (!entry) return -ENOMEM;
    entry->key = strdup(key);
    if (!entry->key) { free(entry); return -ENOMEM; }
    entry->value = malloc(value_len);
    if (!entry->value) { free(entry->key); free(entry); return -ENOMEM; }
    memcpy(entry->value, value, value_len);
    entry->value_len = value_len;
    
    HASH_ADD_KEYPTR(hh, map->impl->head, entry->key, strlen(entry->key), entry);
    map->impl->size++;
    return 1;
}

const void* rgw_hash_map_find(const rgw_hash_map_t *map, const char *key, uint32_t *value_len)
{
    if (!map || !map->impl || !key) return NULL;
    
    struct hash_entry *entry;
    HASH_FIND_STR(map->impl->head, key, entry);
    
    if (!entry) return NULL;
    if (value_len) *value_len = entry->value_len;
    return entry->value;
}

int rgw_hash_map_erase(rgw_hash_map_t *map, const char *key)
{
    if (!map || !map->impl || !key) return -EINVAL;
    
    struct hash_entry *entry;
    HASH_FIND_STR(map->impl->head, key, entry);
    
    if (!entry) return -ENOENT;
    
    HASH_DEL(map->impl->head, entry);
    free(entry->key);
    if (map->impl->value_free && entry->value)
        map->impl->value_free(entry->value);
    else
        free(entry->value);
    free(entry);
    map->impl->size--;
    return 0;
}

bool rgw_hash_map_contains(const rgw_hash_map_t *map, const char *key)
{
    if (!map || !map->impl || !key) return false;
    struct hash_entry *entry;
    HASH_FIND_STR(map->impl->head, key, entry);
    return entry != NULL;
}

size_t rgw_hash_map_size(const rgw_hash_map_t *map)
{
    return map ? map->impl->size : 0;
}

bool rgw_hash_map_empty(const rgw_hash_map_t *map)
{
    return rgw_hash_map_size(map) == 0;
}

void rgw_hash_map_clear(rgw_hash_map_t *map)
{
    if (!map || !map->impl) return;
    
    struct hash_entry *entry, *tmp;
    HASH_ITER(hh, map->impl->head, entry, tmp) {
        HASH_DEL(map->impl->head, entry);
        free(entry->key);
        if (map->impl->value_free && entry->value)
            map->impl->value_free(entry->value);
        else
            free(entry->value);
        free(entry);
    }
    map->impl->size = 0;
}

rgw_hash_map_iterator_t rgw_hash_map_begin(const rgw_hash_map_t *map)
{
    rgw_hash_map_iterator_t iter = {0};
    if (!map || !map->impl) return iter;
    
    iter.impl = malloc(sizeof(struct rgw_hash_map_iterator_impl));
    if (!iter.impl) return iter;
    
    iter.impl->current = map->impl->head;
    iter.valid = (iter.impl->current != NULL);
    return iter;
}

rgw_hash_map_iterator_t rgw_hash_map_end(const rgw_hash_map_t *map)
{
    rgw_hash_map_iterator_t iter = {0};
    iter.valid = false;
    (void)map;
    return iter;
}

void rgw_hash_map_iterator_next(rgw_hash_map_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    if (iter->impl->current)
        iter->impl->current = (struct hash_entry*)iter->impl->current->hh.next;
    iter->valid = (iter->impl->current != NULL);
}

bool rgw_hash_map_iterator_valid(const rgw_hash_map_iterator_t *iter)
{
    return iter && iter->impl && iter->valid;
}

const char* rgw_hash_map_iterator_key(const rgw_hash_map_iterator_t *iter)
{
    if (!iter || !iter->impl || !iter->impl->current) return NULL;
    return iter->impl->current->key;
}

const void* rgw_hash_map_iterator_value(const rgw_hash_map_iterator_t *iter, uint32_t *value_len)
{
    if (!iter || !iter->impl || !iter->impl->current) {
        if (value_len) *value_len = 0;
        return NULL;
    }
    if (value_len) *value_len = iter->impl->current->value_len;
    return iter->impl->current->value;
}

void rgw_hash_map_iterator_destroy(rgw_hash_map_iterator_t *iter)
{
    if (!iter || !iter->impl) return;
    free(iter->impl);
    iter->impl = NULL;
}

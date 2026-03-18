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
 * @file rgw_coptional.c
 * @brief Optional value implementation (替代 std::optional)
 */

#include "containers/rgw_coptional.h"
#include "containers/rgw_cmemory.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void rgw_optional_init(rgw_optional_t *opt)
{
    if (!opt) {
        return;
    }
    
    opt->value = NULL;
    opt->len = 0;
    opt->has_value = false;
}

int rgw_optional_set(rgw_optional_t *opt, const void *value, uint32_t len)
{
    if (!opt || !value || len == 0) {
        return -EINVAL;
    }
    
    rgw_optional_clear(opt);
    
    opt->value = rgw_c_alloc(len);
    if (!opt->value) {
        return -ENOMEM;
    }
    
    rgw_c_memcpy(opt->value, value, len);
    opt->len = len;
    opt->has_value = true;
    
    return 0;
}

int rgw_optional_set_string(rgw_optional_t *opt, const char *cstr)
{
    if (!opt || !cstr) {
        return -EINVAL;
    }
    
    return rgw_optional_set(opt, cstr, (uint32_t)(strlen(cstr) + 1));
}

bool rgw_optional_has_value(const rgw_optional_t *opt)
{
    return opt && opt->has_value && opt->value != NULL;
}

const void* rgw_optional_get(const rgw_optional_t *opt, uint32_t *len)
{
    if (!opt || !opt->has_value || !opt->value) {
        if (len) *len = 0;
        return NULL;
    }
    
    if (len) {
        *len = opt->len;
    }
    
    return opt->value;
}

void* rgw_optional_get_mut(rgw_optional_t *opt, uint32_t *len)
{
    if (!opt || !opt->has_value || !opt->value) {
        if (len) *len = 0;
        return NULL;
    }
    
    if (len) {
        *len = opt->len;
    }
    
    return opt->value;
}

const char* rgw_optional_get_string(const rgw_optional_t *opt)
{
    return (const char*)rgw_optional_get(opt, NULL);
}

void rgw_optional_clear(rgw_optional_t *opt)
{
    if (!opt) {
        return;
    }
    
    if (opt->value) {
        rgw_c_free(opt->value);
    }
    
    opt->value = NULL;
    opt->len = 0;
    opt->has_value = false;
}

void rgw_optional_destroy(rgw_optional_t *opt)
{
    rgw_optional_clear(opt);
}

int rgw_optional_copy(rgw_optional_t *dest, const rgw_optional_t *src)
{
    if (!dest || !src) {
        return -EINVAL;
    }
    
    rgw_optional_clear(dest);
    
    if (!src->has_value || !src->value) {
        return 0;
    }
    
    dest->value = rgw_c_alloc(src->len);
    if (!dest->value) {
        return -ENOMEM;
    }
    
    rgw_c_memcpy(dest->value, src->value, src->len);
    dest->len = src->len;
    dest->has_value = true;
    
    return 0;
}

void rgw_optional_move(rgw_optional_t *dest, rgw_optional_t *src)
{
    if (!dest || !src) {
        return;
    }
    
    rgw_optional_clear(dest);
    
    dest->value = src->value;
    dest->len = src->len;
    dest->has_value = src->has_value;
    
    src->value = NULL;
    src->len = 0;
    src->has_value = false;
}

void rgw_optional_swap(rgw_optional_t *a, rgw_optional_t *b)
{
    if (!a || !b) {
        return;
    }
    
    void *temp_value = a->value;
    uint32_t temp_len = a->len;
    bool temp_has_value = a->has_value;
    
    a->value = b->value;
    a->len = b->len;
    a->has_value = b->has_value;
    
    b->value = temp_value;
    b->len = temp_len;
    b->has_value = temp_has_value;
}

bool rgw_optional_equals(const rgw_optional_t *a, const rgw_optional_t *b)
{
    if (!a || !b) {
        return false;
    }
    
    if (!a->has_value && !b->has_value) {
        return true;
    }
    
    if (a->has_value != b->has_value) {
        return false;
    }
    
    if (!a->value || !b->value) {
        return false;
    }
    
    if (a->len != b->len) {
        return false;
    }
    
    return rgw_c_memcmp(a->value, b->value, a->len) == 0;
}

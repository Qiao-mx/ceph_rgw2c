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
 * @file rgw_cstring.c
 * @brief String implementation using utstring (替代 std::string)
 */

#include "containers/rgw_cstring.h"
#include "containers/rgw_cmemory.h"
#include "internal/utstring.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

#define DEFAULT_STRING_CAPACITY 32

struct rgw_string {
    UT_string *utstring;
};

rgw_string_t* rgw_string_create(const char *cstr)
{
    rgw_string_t *str = rgw_c_alloc(sizeof(rgw_string_t));
    if (!str) {
        return NULL;
    }
    
    utstring_new(str->utstring);
    
    if (cstr) {
        size_t len = strlen(cstr);
        if (len > 0) {
            utstring_bincpy(str->utstring, cstr, len);
        }
    }
    
    return str;
}

rgw_string_t* rgw_string_create_with_capacity(size_t capacity)
{
    rgw_string_t *str = rgw_c_alloc(sizeof(rgw_string_t));
    if (!str) {
        return NULL;
    }
    
    utstring_new(str->utstring);
    
    if (capacity > 0) {
        utstring_reserve(str->utstring, capacity);
    }
    
    return str;
}

rgw_string_t* rgw_string_create_from_data(const void *data, size_t len)
{
    if (!data || len == 0) {
        return rgw_string_create(NULL);
    }
    
    rgw_string_t *str = rgw_c_alloc(sizeof(rgw_string_t));
    if (!str) {
        return NULL;
    }
    
    utstring_new(str->utstring);
    utstring_bincpy(str->utstring, data, len);
    
    return str;
}

void rgw_string_destroy(rgw_string_t *str)
{
    if (!str) {
        return;
    }
    
    if (str->utstring) {
        utstring_free(str->utstring);
    }
    
    rgw_c_free(str);
}

int rgw_string_assign(rgw_string_t *str, const char *cstr)
{
    if (!str || !cstr) {
        return -EINVAL;
    }
    
    size_t len = strlen(cstr);
    
    utstring_clear(str->utstring);
    
    if (len > 0) {
        utstring_bincpy(str->utstring, cstr, len);
    }
    
    return 0;
}

int rgw_string_append(rgw_string_t *str, const char *cstr)
{
    if (!str || !cstr) {
        return -EINVAL;
    }
    
    size_t append_len = strlen(cstr);
    if (append_len == 0) {
        return 0;
    }
    
    utstring_bincpy(str->utstring, cstr, append_len);
    
    return 0;
}

int rgw_string_append_format(rgw_string_t *str, const char *format, ...)
{
    if (!str || !format) {
        return -EINVAL;
    }
    
    va_list args;
    va_start(args, format);
    
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);
    
    if (len < 0) {
        return -EINVAL;
    }
    
    utstring_reserve(str->utstring, len + 1);
    
    va_start(args, format);
    vsnprintf(str->utstring->d + str->utstring->i, len + 1, format, args);
    va_end(args);
    
    str->utstring->i += len;
    
    return 0;
}

int rgw_string_append_data(rgw_string_t *str, const void *data, size_t len)
{
    if (!str || !data || len == 0) {
        return -EINVAL;
    }
    
    utstring_bincpy(str->utstring, data, len);
    
    return 0;
}

int rgw_string_insert(rgw_string_t *str, size_t pos, const char *cstr)
{
    if (!str || !cstr) {
        return -EINVAL;
    }
    
    if (pos > str->utstring->i) {
        return -EINVAL;
    }
    
    size_t insert_len = strlen(cstr);
    if (insert_len == 0) {
        return 0;
    }
    
    utstring_reserve(str->utstring, insert_len + 1);
    
    if (pos < str->utstring->i) {
        memmove(str->utstring->d + pos + insert_len,
                str->utstring->d + pos,
                str->utstring->i - pos);
    }
    
    memcpy(str->utstring->d + pos, cstr, insert_len);
    str->utstring->i += insert_len;
    str->utstring->d[str->utstring->i] = '\0';
    
    return 0;
}

int rgw_string_erase(rgw_string_t *str, size_t pos, size_t len)
{
    if (!str) {
        return -EINVAL;
    }
    
    if (pos >= str->utstring->i) {
        return 0;
    }
    
    if (len == 0 || pos + len >= str->utstring->i) {
        str->utstring->i = pos;
        str->utstring->d[str->utstring->i] = '\0';
        return 0;
    }
    
    memmove(str->utstring->d + pos,
            str->utstring->d + pos + len,
            str->utstring->i - pos - len);
    
    str->utstring->i -= len;
    str->utstring->d[str->utstring->i] = '\0';
    
    return 0;
}

int rgw_string_replace(rgw_string_t *str, size_t pos, size_t len, const char *cstr)
{
    if (!str || !cstr) {
        return -EINVAL;
    }
    
    if (pos > str->utstring->i) {
        return -EINVAL;
    }
    
    size_t replace_len = strlen(cstr);
    
    if (pos + len > str->utstring->i) {
        len = str->utstring->i - pos;
    }
    
    size_t new_length = str->utstring->i - len + replace_len;
    
    if (new_length + 1 > str->utstring->n) {
        utstring_reserve(str->utstring, new_length + 1);
    }
    
    if (pos + len < str->utstring->i) {
        memmove(str->utstring->d + pos + replace_len,
                str->utstring->d + pos + len,
                str->utstring->i - pos - len);
    } else {
        str->utstring->d[pos + replace_len] = '\0';
    }
    
    if (replace_len > 0) {
        memcpy(str->utstring->d + pos, cstr, replace_len);
    }
    
    str->utstring->i = new_length;
    str->utstring->d[str->utstring->i] = '\0';
    
    return 0;
}

const char* rgw_string_c_str(const rgw_string_t *str)
{
    return str ? (str->utstring->d ? str->utstring->d : "") : "";
}

const void* rgw_string_data(const rgw_string_t *str)
{
    return str ? (const void *)str->utstring->d : NULL;
}

size_t rgw_string_length(const rgw_string_t *str)
{
    return str ? str->utstring->i : 0;
}

size_t rgw_string_capacity(const rgw_string_t *str)
{
    return str ? str->utstring->n : 0;
}

bool rgw_string_empty(const rgw_string_t *str)
{
    return rgw_string_length(str) == 0;
}

void rgw_string_clear(rgw_string_t *str)
{
    if (str && str->utstring) {
        utstring_clear(str->utstring);
    }
}

int rgw_string_compare(const rgw_string_t *str1, const rgw_string_t *str2)
{
    if (!str1 || !str2) {
        return str1 ? 1 : (str2 ? -1 : 0);
    }
    
    if (str1->utstring->i != str2->utstring->i) {
        return str1->utstring->i < str2->utstring->i ? -1 : 1;
    }
    
    if (str1->utstring->i == 0) {
        return 0;
    }
    
    return rgw_c_memcmp(str1->utstring->d, str2->utstring->d, str1->utstring->i);
}

int rgw_string_compare_cstr(const rgw_string_t *str, const char *cstr)
{
    if (!str || !cstr) {
        return str ? 1 : (cstr ? -1 : 0);
    }
    
    size_t cstr_len = strlen(cstr);
    
    if (str->utstring->i != cstr_len) {
        return str->utstring->i < cstr_len ? -1 : 1;
    }
    
    if (str->utstring->i == 0) {
        return 0;
    }
    
    return rgw_c_memcmp(str->utstring->d, cstr, str->utstring->i);
}

size_t rgw_string_find(const rgw_string_t *str, const char *substr)
{
    if (!str || !substr) {
        return (size_t)-1;
    }
    
    size_t substr_len = strlen(substr);
    if (substr_len == 0 || substr_len > str->utstring->i) {
        return (size_t)-1;
    }
    
    for (size_t i = 0; i <= str->utstring->i - substr_len; i++) {
        if (rgw_c_memcmp(str->utstring->d + i, substr, substr_len) == 0) {
            return i;
        }
    }
    
    return (size_t)-1;
}

size_t rgw_string_find_char(const rgw_string_t *str, char c)
{
    if (!str) {
        return (size_t)-1;
    }
    
    for (size_t i = 0; i < str->utstring->i; i++) {
        if (str->utstring->d[i] == c) {
            return i;
        }
    }
    
    return (size_t)-1;
}

rgw_string_t* rgw_string_substring(const rgw_string_t *str, size_t pos, size_t len)
{
    if (!str || pos >= str->utstring->i) {
        return rgw_string_create(NULL);
    }
    
    if (len == 0 || pos + len > str->utstring->i) {
        len = str->utstring->i - pos;
    }
    
    return rgw_string_create_from_data(str->utstring->d + pos, len);
}

void rgw_string_trim(rgw_string_t *str)
{
    if (!str || str->utstring->i == 0) {
        return;
    }
    
    size_t start = 0;
    while (start < str->utstring->i && isspace((unsigned char)str->utstring->d[start])) {
        start++;
    }
    
    size_t end = str->utstring->i;
    while (end > start && isspace((unsigned char)str->utstring->d[end - 1])) {
        end--;
    }
    
    if (start > 0 || end < str->utstring->i) {
        if (end > start) {
            memmove(str->utstring->d, str->utstring->d + start, end - start);
        }
        str->utstring->d[end - start] = '\0';
        str->utstring->i = end - start;
    }
}

void rgw_string_to_upper(rgw_string_t *str)
{
    if (!str || str->utstring->i == 0) {
        return;
    }
    
    for (size_t i = 0; i < str->utstring->i; i++) {
        str->utstring->d[i] = (char)toupper((unsigned char)str->utstring->d[i]);
    }
}

void rgw_string_to_lower(rgw_string_t *str)
{
    if (!str || str->utstring->i == 0) {
        return;
    }
    
    for (size_t i = 0; i < str->utstring->i; i++) {
        str->utstring->d[i] = (char)tolower((unsigned char)str->utstring->d[i]);
    }
}

rgw_string_t* rgw_string_dup(const rgw_string_t *str)
{
    if (!str) {
        return NULL;
    }
    
    return rgw_string_create_from_data(str->utstring->d, str->utstring->i);
}

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
 * @file test_buffer.c
 * @brief Unit tests for rgw_buffer
 */

#include "rgw_buffer.h"
#include "containers/rgw_cmemory.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static void test_create_destroy(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);
    assert(rgw_buffer_length(buf) == 0);
    assert(rgw_buffer_capacity(buf) > 0);
    rgw_buffer_destroy(buf);
    printf("test_create_destroy: PASSED\n");
}

static void test_append_binary(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    const char* data = "Hello World";
    int rc = rgw_buffer_append(buf, data, strlen(data));
    assert(rc == 0);
    assert(rgw_buffer_length(buf) == strlen(data));

    // 追加更多数据
    const char* data2 = " More Data";
    rc = rgw_buffer_append(buf, data2, strlen(data2));
    assert(rc == 0);
    assert(rgw_buffer_length(buf) == strlen(data) + strlen(data2));

    rgw_buffer_destroy(buf);
    printf("test_append_binary: PASSED");
}

static void test_append_string(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    int rc = rgw_buffer_append_str(buf, "First");
    assert(rc == 0);
    rc = rgw_buffer_append_str(buf, " Second");
    assert(rc == 0);
    rc = rgw_buffer_append_str(buf, " Third");
    assert(rc == 0);

    assert(rgw_buffer_length(buf) == 18);  // "First Second Third"

    rgw_buffer_destroy(buf);
    printf("test_append_string: PASSED\n");
}

static void test_clear(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    rgw_buffer_append_str(buf, "Test Data");
    assert(rgw_buffer_length(buf) > 0);

    rgw_buffer_clear(buf);
    assert(rgw_buffer_length(buf) == 0);

    rgw_buffer_destroy(buf);
    printf("test_clear: PASSED\n");
}

static void test_reserve(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    size_t initial_cap = rgw_buffer_capacity(buf);
    int rc = rgw_buffer_reserve(buf, 1024);
    assert(rc == 0);
    assert(rgw_buffer_capacity(buf) >= 1024);

    rgw_buffer_destroy(buf);
    printf("test_reserve: PASSED\n");
}

static void test_truncate(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    rgw_buffer_append_str(buf, "Hello World");
    assert(rgw_buffer_length(buf) == 11);

    int rc = rgw_buffer_truncate(buf, 5);
    assert(rc == 0);
    assert(rgw_buffer_length(buf) == 5);

    rgw_buffer_destroy(buf);
    printf("test_truncate: PASSED\n");
}

static void test_copy(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    rgw_buffer_append_str(buf, "Original Data");

    rgw_buffer_t* copy = rgw_buffer_copy(buf);
    assert(copy != NULL);
    assert(rgw_buffer_length(copy) == rgw_buffer_length(buf));

    // 修改原缓冲区不影响副本
    rgw_buffer_append_str(buf, " Modified");
    assert(rgw_buffer_length(buf) == 22);
    assert(rgw_buffer_length(copy) == 13);

    rgw_buffer_destroy(buf);
    rgw_buffer_destroy(copy);
    printf("test_copy: PASSED\n");
}

static void test_append_buffer(void) {
    rgw_buffer_t* buf1 = rgw_buffer_create(0);
    rgw_buffer_t* buf2 = rgw_buffer_create(0);

    rgw_buffer_append_str(buf1, "Hello");
    rgw_buffer_append_str(buf2, " World");

    int rc = rgw_buffer_append_buffer(buf1, buf2);
    assert(rc == 0);
    assert(rgw_buffer_length(buf1) == 11);  // "Hello World"

    rgw_buffer_destroy(buf1);
    rgw_buffer_destroy(buf2);
    printf("test_append_buffer: PASSED\n");
}

static void test_binary_with_null(void) {
    rgw_buffer_t* buf = rgw_buffer_create(0);
    assert(buf != NULL);

    // 测试包含 null 字节的二进制数据
    char data[] = {'A', '\0', 'B', '\0', 'C'};
    int rc = rgw_buffer_append(buf, data, sizeof(data));
    assert(rc == 0);
    assert(rgw_buffer_length(buf) == sizeof(data));

    // 验证数据正确
    void* retrieved = rgw_buffer_data(buf);
    assert(retrieved != NULL);
    assert(memcmp(retrieved, data, sizeof(data)) == 0);

    rgw_buffer_destroy(buf);
    printf("test_binary_with_null: PASSED\n");
}

int main() {
    printf("Running rgw_buffer tests...\n");

    test_create_destroy();
    test_append_binary();
    test_append_string();
    test_clear();
    test_reserve();
    test_truncate();
    test_copy();
    test_append_buffer();
    test_binary_with_null();

    printf("\nAll tests PASSED!\n");
    return 0;
}

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
 * @file test_rgw_types.c
 * @brief Comprehensive integration test for RGW C types
 *
 * Tests all converted data types:
 * - rgw_string_c (string utilities)
 * - rgw_buffer (buffer)
 * - rgw_xml (XML parsing)
 * - rgw_b64 (Base64 encoding/decoding)
 * - rgw_hex (hex encoding/decoding)
 */

#include "rgw_string_c.h"
#include "rgw_buffer.h"
#include "rgw_xml.h"
#include "rgw_b64.h"
#include "rgw_hex.h"
#include "rgw_errors.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Test string + buffer combination */
static void test_string_buffer_integration(void) {
    printf("Testing string + buffer integration...\n");

    /* Use string functions to build content */
    const char* part1 = "Hello";
    const char* part2 = " ";
    const char* part3 = "World";

    /* Calculate total size needed */
    size_t total_size = rgw_str_size_calc(part1, part2, part3, NULL);

    /* Allocate buffer */
    rgw_buffer_t* buf = rgw_buffer_create(total_size);
    assert(buf != NULL);

    /* Append strings using buffer functions */
    int rc = rgw_buffer_append_str(buf, part1);
    assert(rc == 0);

    rc = rgw_buffer_append_str(buf, part2);
    assert(rc == 0);

    rc = rgw_buffer_append_str(buf, part3);
    assert(rc == 0);

    /* Verify content */
    assert(rgw_buffer_length(buf) == 11);

    /* Get raw data and verify with string functions */
    const char* data = (const char*)rgw_buffer_data(buf);
    assert(rgw_str_casecmp(data, "hello world") != 0);  /* Original case */
    assert(rgw_str_casecmp(data, "HELLO WORLD") != 0); /* Different case */

    rgw_buffer_destroy(buf);
    printf("  string + buffer: PASSED\n");
}

/* Test string + Base64 combination */
static void test_string_base64_integration(void) {
    printf("Testing string + Base64 integration...\n");

    const char* original = "Hello World";

    /* Encode string to Base64 */
    char* encoded = NULL;
    int rc = rgw_b64_encode((const uint8_t*)original, strlen(original), &encoded);
    assert(rc == 0);
    assert(encoded != NULL);

    /* Verify encoded result (Hello World -> SGVsbG8gV29ybGQ=) */
    assert(strcmp(encoded, "SGVsbG8gV29ybGQ=") == 0);

    /* Decode back */
    uint8_t* decoded = NULL;
    size_t decoded_len = 0;
    rc = rgw_b64_decode(encoded, &decoded, &decoded_len);
    assert(rc == 0);
    assert(decoded != NULL);
    assert(decoded_len == strlen(original));
    assert(memcmp(decoded, original, decoded_len) == 0);

    /* Use string compare to verify */
    assert(rgw_str_casecmp((char*)decoded, "hello world") == 0);

    free(encoded);
    free(decoded);
    printf("  string + Base64: PASSED\n");
}

/* Test string + hex combination */
static void test_string_hex_integration(void) {
    printf("Testing string + hex integration...\n");

    const char* data = "Hi";
    size_t data_len = strlen(data);

    /* Encode to hex */
    size_t hex_len = rgw_hex_encoded_length(data_len);
    char* hex_out = (char*)malloc(hex_len + 1);
    assert(hex_out != NULL);

    int rc = rgw_hex_encode(data, data_len, hex_out);
    assert(rc == 0);

    /* Verify hex encoding (Hi -> 4869) */
    assert(strcmp(hex_out, "4869") == 0);

    /* Decode back */
    size_t decoded_len = 0;
    rc = rgw_hex_decode(hex_out, hex_out, &decoded_len);  /* Reuse buffer */
    assert(rc == 0);
    assert(decoded_len == data_len);
    assert(memcmp(hex_out, data, decoded_len) == 0);

    free(hex_out);
    printf("  string + hex: PASSED\n");
}

/* Test buffer + Base64 combination */
static void test_buffer_base64_integration(void) {
    printf("Testing buffer + Base64 integration...\n");

    /* Create buffer with binary data */
    rgw_buffer_t* buf = rgw_buffer_create(16);
    assert(buf != NULL);

    const uint8_t binary_data[] = {0x00, 0xFF, 0x10, 0x20};
    int rc = rgw_buffer_append(buf, binary_data, sizeof(binary_data));
    assert(rc == 0);

    /* Encode buffer content to Base64 */
    const uint8_t* buf_data = (const uint8_t*)rgw_buffer_data(buf);
    size_t buf_len = rgw_buffer_length(buf);

    char* encoded = NULL;
    rc = rgw_b64_encode(buf_data, buf_len, &encoded);
    assert(rc == 0);

    /* Decode */
    uint8_t* decoded = NULL;
    size_t decoded_len = 0;
    rc = rgw_b64_decode(encoded, &decoded, &decoded_len);
    assert(rc == 0);

    /* Verify */
    assert(decoded_len == sizeof(binary_data));
    assert(memcmp(decoded, binary_data, sizeof(binary_data)) == 0);

    free(encoded);
    free(decoded);
    rgw_buffer_destroy(buf);
    printf("  buffer + Base64: PASSED\n");
}

/* Test string + XML combination */
static void test_string_xml_integration(void) {
    printf("Testing string + XML integration...\n");

    /* Build XML string using string functions */
    const char* tag = "data";
    const char* value = "test";

    /* Create XML document */
    rgw_xml_doc_t* doc = rgw_xml_doc_create();
    assert(doc != NULL);

    /* Build XML string manually for simplicity */
    char xml_str[256];
    rgw_str_cat(xml_str, sizeof(xml_str), "<", tag, ">", value, "</", tag, ">", NULL);

    /* Parse XML */
    int rc = rgw_xml_doc_parse_str(doc, xml_str);
    assert(rc == 0);

    /* Verify parsed content */
    rgw_xml_node_t* root = rgw_xml_doc_root(doc);
    assert(root != NULL);
    assert(strcmp(rgw_xml_node_name(root), tag) == 0);
    assert(strcmp(rgw_xml_node_data(root), value) == 0);

    rgw_xml_doc_destroy(doc);
    printf("  string + XML: PASSED\n");
}

/* Test error handling integration */
static void test_error_handling_integration(void) {
    printf("Testing error handling integration...\n");

    /* Test string to number conversion error handling */
    int64_t val = 0;
    int rc = rgw_str_to_ll("not_a_number", &val);
    assert(rc != 0);  /* Should fail */

    rc = rgw_str_to_ll("123", &val);
    assert(rc == 0);
    assert(val == 123);

    /* Test hex invalid input */
    char hex_out[16];
    rc = rgw_hex_decode("not_hex", hex_out, &(size_t){0});
    assert(rc != 0);  /* Should fail */

    /* Test Base64 invalid input */
    uint8_t* out = NULL;
    size_t out_len = 0;
    rc = rgw_b64_decode("invalid!!!", &out, &out_len);
    assert(rc != 0);  /* Should fail */

    printf("  error handling: PASSED\n");
}

/* Test wildcard matching */
static void test_wildcard_matching(void) {
    printf("Testing wildcard matching...\n");

    /* Test basic wildcards */
    assert(rgw_str_match_wildcards("*", "anything", RGW_STR_MATCH_NONE) == true);
    assert(rgw_str_match_wildcards("test*", "test123", RGW_STR_MATCH_NONE) == true);
    assert(rgw_str_match_wildcards("test*", "other", RGW_STR_MATCH_NONE) == false);
    assert(rgw_str_match_wildcards("test?", "test1", RGW_STR_MATCH_NONE) == true);
    assert(rgw_str_match_wildcards("test?", "test12", RGW_STR_MATCH_NONE) == false);

    /* Test case insensitive */
    assert(rgw_str_match_wildcards("TEST*", "test123", RGW_STR_MATCH_CASE_INSENSITIVE) == true);
    assert(rgw_str_match_wildcards("test*", "TEST123", RGW_STR_MATCH_CASE_INSENSITIVE) == true);

    printf("  wildcard matching: PASSED\n");
}

/* Test string manipulation */
static void test_string_manipulation(void) {
    printf("Testing string manipulation...\n");

    /* Test case conversion */
    char* lower = rgw_str_case("HELLO", RGW_STR_CASE_LOWER);
    assert(lower != NULL);
    assert(strcmp(lower, "hello") == 0);
    free(lower);

    char* upper = rgw_str_case("hello", RGW_STR_CASE_UPPER);
    assert(upper != NULL);
    assert(strcmp(upper, "HELLO") == 0);
    free(upper);

    /* Test trim */
    char* trimmed = rgw_str_trim("  hello  ", RGW_STR_TRIM_BOTH);
    assert(trimmed != NULL);
    assert(strcmp(trimmed, "hello") == 0);
    free(trimmed);

    /* Test prefix/suffix */
    assert(rgw_str_starts_with("hello world", "hello") == true);
    assert(rgw_str_starts_with("hello world", "world") == false);
    assert(rgw_str_ends_with("hello world", "world") == true);
    assert(rgw_str_ends_with("hello world", "hello") == false);

    /* Test replace */
    char* replaced = rgw_str_replace("hello world", "world", "there");
    assert(replaced != NULL);
    assert(strcmp(replaced, "hello there") == 0);
    free(replaced);

    printf("  string manipulation: PASSED\n");
}

int main(void) {
    printf("========================================\n");
    printf("RGW Types Comprehensive Integration Test\n");
    printf("========================================\n\n");

    test_string_buffer_integration();
    test_string_base64_integration();
    test_string_hex_integration();
    test_buffer_base64_integration();
    test_string_xml_integration();
    test_error_handling_integration();
    test_wildcard_matching();
    test_string_manipulation();

    printf("\n========================================\n");
    printf("All integration tests PASSED!\n");
    printf("========================================\n");

    return 0;
}

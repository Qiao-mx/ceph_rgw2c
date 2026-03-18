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
 * @file test_hex.c
 * @brief Unit tests for rgw_hex
 */

#include "rgw_hex.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_encode_abc(void) {
    const char* input = "abc";
    char output[16];
    
    int rc = rgw_hex_encode(input, 3, output);
    assert(rc == 0);
    assert(strcmp(output, "616263") == 0);
    
    printf("test_encode_abc: PASSED\n");
}

static void test_decode_abc(void) {
    const char* input = "616263";
    char output[16];
    size_t len = sizeof(output);
    
    int rc = rgw_hex_decode(input, output, &len);
    assert(rc == 0);
    assert(len == 3);
    assert(memcmp(output, "abc", 3) == 0);
    
    printf("test_decode_abc: PASSED\n");
}

static void test_encode_binary(void) {
    // Test encoding binary data with null bytes
    const unsigned char input[] = {0x00, 0xFF, 0xAB, 0x12};
    char output[16];
    
    int rc = rgw_hex_encode(input, 4, output);
    assert(rc == 0);
    assert(strcmp(output, "00ffab12") == 0);
    
    printf("test_encode_binary: PASSED\n");
}

static void test_decode_binary(void) {
    const char* input = "00ffab12";
    unsigned char output[16];
    size_t len = sizeof(output);
    
    int rc = rgw_hex_decode(input, output, &len);
    assert(rc == 0);
    assert(len == 4);
    assert(output[0] == 0x00);
    assert(output[1] == 0xFF);
    assert(output[2] == 0xAB);
    assert(output[3] == 0x12);
    
    printf("test_decode_binary: PASSED\n");
}

static void test_encode_upper(void) {
    const char* input = "abc";
    char output[16];
    
    int rc = rgw_hex_encode_upper(input, 3, output);
    assert(rc == 0);
    assert(strcmp(output, "616263") == 0);
    
    printf("test_encode_upper: PASSED\n");
}

static void test_roundtrip(void) {
    // Test encode then decode returns original data
    const char* original = "Hello World!";
    char encoded[64];
    char decoded[64];
    size_t decoded_len;
    
    int rc = rgw_hex_encode(original, strlen(original), encoded);
    assert(rc == 0);
    
    decoded_len = sizeof(decoded);
    rc = rgw_hex_decode(encoded, decoded, &decoded_len);
    assert(rc == 0);
    assert(decoded_len == strlen(original));
    assert(memcmp(decoded, original, strlen(original)) == 0);
    
    printf("test_roundtrip: PASSED\n");
}

static void test_invalid_char(void) {
    const char* input = "12GG";  // GG is invalid
    char output[16];
    size_t len = sizeof(output);
    
    int rc = rgw_hex_decode(input, output, &len);
    assert(rc != 0);  // Should fail
    
    printf("test_invalid_char: PASSED\n");
}

static void test_empty_input(void) {
    const char* input = "";
    char output[16];
    
    int rc = rgw_hex_encode(input, 0, output);
    assert(rc == 0);
    assert(output[0] == '\0');
    
    // Empty hex string decode
    size_t len = sizeof(output);
    rc = rgw_hex_decode(input, output, &len);
    assert(rc == 0);
    assert(len == 0);
    
    printf("test_empty_input: PASSED\n");
}

static void test_length_functions(void) {
    assert(rgw_hex_encoded_length(4) == 8);
    assert(rgw_hex_decoded_length(8) == 4);
    
    printf("test_length_functions: PASSED\n");
}

static void test_is_valid_char(void) {
    assert(rgw_hex_is_valid_char('0') == true);
    assert(rgw_hex_is_valid_char('9') == true);
    assert(rgw_hex_is_valid_char('a') == true);
    assert(rgw_hex_is_valid_char('f') == true);
    assert(rgw_hex_is_valid_char('A') == true);
    assert(rgw_hex_is_valid_char('F') == true);
    assert(rgw_hex_is_valid_char('g') == false);
    assert(rgw_hex_is_valid_char('G') == false);
    assert(rgw_hex_is_valid_char('@') == false);
    
    printf("test_is_valid_char: PASSED\n");
}

static void test_uppercase_input(void) {
    // Test decoding uppercase hex string
    const char* input = "DEADBEEF";
    unsigned char output[16];
    size_t len = sizeof(output);
    
    int rc = rgw_hex_decode(input, output, &len);
    assert(rc == 0);
    assert(len == 4);
    assert(output[0] == 0xDE);
    assert(output[1] == 0xAD);
    assert(output[2] == 0xBE);
    assert(output[3] == 0xEF);
    
    printf("test_uppercase_input: PASSED\n");
}

int main() {
    printf("Running rgw_hex tests...\n");
    
    test_encode_abc();
    test_decode_abc();
    test_encode_binary();
    test_decode_binary();
    test_encode_upper();
    test_roundtrip();
    test_invalid_char();
    test_empty_input();
    test_length_functions();
    test_is_valid_char();
    test_uppercase_input();
    
    printf("\nAll tests PASSED!\n");
    return 0;
}

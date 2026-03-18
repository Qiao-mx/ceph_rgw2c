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
 * @file rgw_hex.c
 * @brief Hexadecimal encoding/decoding implementation
 */

#include "rgw_hex.h"
#include "containers/rgw_cmemory.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static const char hex_chars_lower[] = "0123456789abcdef";
static const char hex_chars_upper[] = "0123456789ABCDEF";

int rgw_hex_encode(const void* input, size_t len, char* output) {
    if (!output) {
        return -1;
    }
    if (len == 0) {
        output[0] = '\0';
        return 0;
    }
    if (!input) {
        return -1;
    }

    const unsigned char* data = (const unsigned char*)input;
    for (size_t i = 0; i < len; i++) {
        output[i * 2] = hex_chars_lower[(data[i] >> 4) & 0x0F];
        output[i * 2 + 1] = hex_chars_lower[data[i] & 0x0F];
    }
    output[len * 2] = '\0';

    return 0;
}

int rgw_hex_encode_upper(const void* input, size_t len, char* output) {
    if (!output) {
        return -1;
    }
    if (len == 0) {
        output[0] = '\0';
        return 0;
    }
    if (!input) {
        return -1;
    }

    const unsigned char* data = (const unsigned char*)input;
    for (size_t i = 0; i < len; i++) {
        output[i * 2] = hex_chars_upper[(data[i] >> 4) & 0x0F];
        output[i * 2 + 1] = hex_chars_upper[data[i] & 0x0F];
    }
    output[len * 2] = '\0';

    return 0;
}

static int hex_char_to_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int rgw_hex_decode(const char* input, void* output, size_t* len) {
    if (!input || !output || !len) {
        return -1;
    }

    size_t input_len = strlen(input);
    if (input_len == 0) {
        *len = 0;
        return 0;
    }
    if (input_len % 2 != 0) {
        return -2;
    }

    size_t output_len = input_len / 2;
    if (*len < output_len) {
        *len = output_len;
        return -3;
    }

    unsigned char* data = (unsigned char*)output;
    for (size_t i = 0; i < output_len; i++) {
        int high = hex_char_to_value(input[i * 2]);
        int low = hex_char_to_value(input[i * 2 + 1]);

        if (high < 0 || low < 0) {
            return -4;
        }

        data[i] = (high << 4) | low;
    }

    *len = output_len;
    return 0;
}

size_t rgw_hex_encoded_length(size_t input_len) {
    return input_len * 2;
}

size_t rgw_hex_decoded_length(size_t hex_len) {
    return hex_len / 2;
}

bool rgw_hex_is_valid_char(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

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
 * @file rgw_errors.h
 * @brief Error handling framework for C
 *
 * Features:
 * - Unified error codes
 * - Error context with file/line information
 * - Error chaining (cause tracking)
 * - Error propagation macros
 *
 * Usage example:
 * @code
 *   // Set error
 *   rgw_set_error(RGW_ERR_OUT_OF_MEMORY, "Failed to allocate", __FILE__, __LINE__);
 *
 *   // Check error
 *   rgw_error_context_t *err = rgw_get_error();
 *   if (err) {
 *       printf("Error: %s at %s:%d\n", err->message, err->file, err->line);
 *   }
 *
 *   // Clear error
 *   rgw_clear_error();
 *
 *   // Error propagation macro
 *   int rc = some_function();
 *   RGW_CHECK(rc);
 *
 *   // Error propagation with message
 *   RGW_CHECK_MSG(do_something(), "Operation failed");
 * @endcode
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes
 */
typedef enum rgw_error_code {
    RGW_OK = 0,                      /**< Success */
    RGW_ERR_INVALID_ARG = 1,         /**< Invalid argument */
    RGW_ERR_OUT_OF_MEMORY = 2,        /**< Out of memory */
    RGW_ERR_IO_ERROR = 3,             /**< I/O error */
    RGW_ERR_NOT_FOUND = 4,            /**< Resource not found */
    RGW_ERR_ALREADY_EXISTS = 5,       /**< Resource already exists */
    RGW_ERR_PERMISSION_DENIED = 6,    /**< Permission denied */
    RGW_ERR_TIMEOUT = 7,              /**< Operation timed out */
    RGW_ERR_NOT_IMPLEMENTED = 8,      /**< Not implemented */
    RGW_ERR_SYSTEM = 9,                /**< System error */
    RGW_ERR_BUFFER_OVERFLOW = 10,     /**< Buffer overflow */
    RGW_ERR_BUFFER_UNDERFLOW = 11,    /**< Buffer underflow */
    RGW_ERR_PARSE_ERROR = 12,         /**< Parse error */
    RGW_ERR_CONNECTION_FAILED = 13,   /**< Connection failed */
    RGW_ERR_PROTOCOL_ERROR = 14,      /**< Protocol error */
    RGW_ERR_UNKNOWN = 255             /**< Unknown error */
} rgw_error_code_t;

/**
 * Error context structure
 */
typedef struct rgw_error_context {
    rgw_error_code_t code;            /**< Error code */
    const char *message;              /**< Error message */
    const char *file;                 /**< Source file where error occurred */
    int line;                         /**< Source line where error occurred */
    struct rgw_error_context *cause;  /**< Cause of this error (for chaining) */
} rgw_error_context_t;

/**
 * Thread-local error context
 * In a multi-threaded environment, this should be replaced with thread-local storage
 */
extern rgw_error_context_t *rgw_global_error;

/**
 * @brief Set current error
 * @param code Error code
 * @param message Error message (can be NULL)
 * @param file Source file (use __FILE__)
 * @param line Source line (use __LINE__)
 */
void rgw_set_error(rgw_error_code_t code, const char *message,
                   const char *file, int line);

/**
 * @brief Get current error
 * @return Current error context, or NULL if no error
 */
rgw_error_context_t* rgw_get_error(void);

/**
 * @brief Clear current error
 */
void rgw_clear_error(void);

/**
 * @brief Add cause to existing error
 * @param error Error to add cause to
 * @param cause Cause error
 */
void rgw_error_add_cause(rgw_error_context_t *error, rgw_error_context_t *cause);

/**
 * @brief Get error string for code
 * @param code Error code
 * @return Static string describing error
 */
const char* rgw_error_string(rgw_error_code_t code);

/**
 * Error propagation macro - returns on error
 * Usage: RGW_CHECK(function_that_returns_int());
 */
#define RGW_CHECK(expr) \
    do { \
        rgw_error_code_t _rc = (expr); \
        if (_rc != RGW_OK) { \
            return _rc; \
        } \
    } while (0)

/**
 * Error propagation macro with custom message - returns on error
 * Usage: RGW_CHECK_MSG(function_that_returns_int(), "Custom error message");
 */
#define RGW_CHECK_MSG(expr, msg) \
    do { \
        rgw_error_code_t _rc = (expr); \
        if (_rc != RGW_OK) { \
            rgw_set_error(_rc, msg, __FILE__, __LINE__); \
            return _rc; \
        } \
    } while (0)

/**
 * Error propagation macro with goto cleanup - jumps to label on error
 * Usage: RGW_CHECK_GOTO(function_that_returns_int(), cleanup);
 */
#define RGW_CHECK_GOTO(expr, label) \
    do { \
        rgw_error_code_t _rc = (expr); \
        if (_rc != RGW_OK) { \
            goto label; \
        } \
    } while (0)

/**
 * Set error and return - convenience macro
 * Usage: RGW_RETURN(code, message);
 */
#define RGW_RETURN(code, msg) \
    do { \
        rgw_set_error((code), (msg), __FILE__, __LINE__); \
        return (code); \
    } while (0)

#ifdef __cplusplus
}
#endif

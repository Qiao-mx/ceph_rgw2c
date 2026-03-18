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
 * @file rgw_oop.h
 * @brief Object-Oriented Programming framework for C
 *
 * Features:
 * - Virtual function table (vtable) pattern for polymorphism
 * - Reference counting for memory management
 * - RTTI (Run-Time Type Information) support
 * - Smart pointer support (shared_ptr)
 *
 * Usage example:
 * @code
 *   // Define a base class
 *   typedef struct {
 *       rgw_object_t base;
 *       int value;
 *   } my_object_t;
 *
 *   // Define vtable
 *   static rgw_object_vtable_t my_object_vtable = {
 *       .destroy = my_object_destroy,
 *       .type_name = my_object_type_name,
 *       .is_a = my_object_is_a
 *   };
 *
 *   // Create object
 *   my_object_t *obj = rgw_c_alloc(sizeof(my_object_t));
 *   obj->base.vtable = &my_object_vtable;
 *   obj->base.ref_count = 1;
 *   strncpy(obj->base.type_id, "my_object_t", sizeof(obj->base.type_id) - 1);
 *
 *   // Use reference counting
 *   rgw_object_ref(&obj->base);
 *   rgw_object_unref(&obj->base);
 *
 *   // Use smart pointer
 *   rgw_shared_ptr_t ptr = rgw_shared_ptr_create(&obj->base);
 *   rgw_shared_ptr_destroy(&ptr);
 * @endcode
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Forward declaration
 */
typedef struct rgw_object rgw_object_t;

/**
 * Virtual function table structure
 */
typedef struct rgw_object_vtable {
    /**
     * @brief Destroy the object (destructor)
     */
    void (*destroy)(rgw_object_t *self);

    /**
     * @brief Get type name for RTTI
     */
    const char* (*type_name)(void);

    /**
     * @brief Check if object is instance of given type
     */
    bool (*is_a)(const rgw_object_t *self, const char *type_name);
} rgw_object_vtable_t;

/**
 * Base object structure (equivalent to C++ base class)
 */
struct rgw_object {
    rgw_object_vtable_t *vtable;  /**< Pointer to virtual function table */
    uint32_t ref_count;            /**< Reference count for memory management */
    char type_id[32];              /**< Type identifier for RTTI */
};

/**
 * @brief Increment reference count
 * @param obj Object to reference
 */
void rgw_object_ref(rgw_object_t *obj);

/**
 * @brief Decrement reference count, destroy if count reaches 0
 * @param obj Object to unreference
 */
void rgw_object_unref(rgw_object_t *obj);

/**
 * Smart pointer structure (equivalent to C++ shared_ptr)
 */
typedef struct rgw_shared_ptr {
    rgw_object_t *obj;  /**< Pointer to managed object */
} rgw_shared_ptr_t;

/**
 * @brief Create smart pointer from object
 * @param obj Object to manage (will be referenced)
 * @return Smart pointer, or NULL if obj is NULL
 */
rgw_shared_ptr_t rgw_shared_ptr_create(rgw_object_t *obj);

/**
 * @brief Destroy smart pointer, unreference the object
 * @param ptr Smart pointer to destroy
 */
void rgw_shared_ptr_destroy(rgw_shared_ptr_t *ptr);

/**
 * @brief Copy smart pointer (increments reference count)
 * @param ptr Smart pointer to copy
 * @return Copied smart pointer
 */
rgw_shared_ptr_t rgw_shared_ptr_copy(const rgw_shared_ptr_t *ptr);

/**
 * @brief Get raw pointer from smart pointer
 * @param ptr Smart pointer
 * @return Raw object pointer
 */
static inline rgw_object_t* rgw_shared_ptr_get(const rgw_shared_ptr_t *ptr)
{
    return ptr ? ptr->obj : NULL;
}

/**
 * @brief Check if smart pointer is valid
 * @param ptr Smart pointer
 * @return true if pointer is valid, false otherwise
 */
static inline bool rgw_shared_ptr_valid(const rgw_shared_ptr_t *ptr)
{
    return ptr != NULL && ptr->obj != NULL;
}

/**
 * Polymorphic call macro
 * Usage: RGW_CALL(obj, method_name, args...)
 * Note: Assumes vtable has method_name function pointer
 */
#define RGW_CALL(obj, method, ...) \
    ((obj) && (obj)->vtable && (obj)->vtable->method ? \
     (obj)->vtable->method((obj), ##__VA_ARGS__) : (void)0)

/**
 * Type check macro
 * Usage: RGW_IS_A(obj, "type_name")
 */
#define RGW_IS_A(obj, type) \
    ((obj) != NULL && \
     (obj)->vtable && \
     (obj)->vtable->is_a != NULL && \
     (obj)->vtable->is_a((obj), (type)))

/**
 * Type name macro
 * Usage: RGW_TYPE_NAME(obj)
 */
#define RGW_TYPE_NAME(obj) \
    ((obj) && (obj)->vtable && (obj)->vtable->type_name ? \
     (obj)->vtable->type_name() : "unknown")

/**
 * Destroy macro
 * Usage: RGW_DESTROY(obj)
 */
#define RGW_DESTROY(obj) \
    do { \
        if ((obj) && (obj)->vtable && (obj)->vtable->destroy) { \
            (obj)->vtable->destroy((obj)); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif

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
 * @file rgw_oop.c
 * @brief Object-Oriented Programming framework implementation
 */

#include "rgw_oop.h"
#include "containers/rgw_cmemory.h"
#include <stdlib.h>

void rgw_object_ref(rgw_object_t *obj)
{
    if (obj == NULL) {
        return;
    }
    obj->ref_count++;
}

void rgw_object_unref(rgw_object_t *obj)
{
    if (obj == NULL) {
        return;
    }

    if (obj->ref_count > 0) {
        obj->ref_count--;
    }

    if (obj->ref_count == 0) {
        if (obj->vtable && obj->vtable->destroy) {
            obj->vtable->destroy(obj);
        }
    }
}

rgw_shared_ptr_t rgw_shared_ptr_create(rgw_object_t *obj)
{
    rgw_shared_ptr_t ptr = { .obj = NULL };

    if (obj == NULL) {
        return ptr;
    }

    rgw_object_ref(obj);
    ptr.obj = obj;

    return ptr;
}

void rgw_shared_ptr_destroy(rgw_shared_ptr_t *ptr)
{
    if (ptr == NULL) {
        return;
    }

    if (ptr->obj != NULL) {
        rgw_object_unref(ptr->obj);
        ptr->obj = NULL;
    }
}

rgw_shared_ptr_t rgw_shared_ptr_copy(const rgw_shared_ptr_t *ptr)
{
    rgw_shared_ptr_t copy = { .obj = NULL };

    if (ptr == NULL || ptr->obj == NULL) {
        return copy;
    }

    rgw_object_ref(ptr->obj);
    copy.obj = ptr->obj;

    return copy;
}

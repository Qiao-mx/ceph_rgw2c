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
 * @file rgw_ccommon.h
 * @brief Unified header file for RGW C Common Library
 *
 * This header includes all available containers and frameworks:
 * - Memory management
 * - Basic types
 * - Containers (array, string, map, set, list, deque, stack, queue, priority queue, hash map, optional)
 * - OOP framework
 * - Error handling
 * - Sorting utilities
 *
 * Usage:
 * @code
 *   #include "rgw_ccommon.h"
 *
 *   // Now you can use all containers
 *   rgw_array_t *arr = rgw_array_create(10);
 *   rgw_queue_t *queue = rgw_queue_create(free);
 *   rgw_priority_queue_t *pq = rgw_priority_queue_create(cmp, free);
 * @endcode
 */

#pragma once

/*
 * Memory management
 */
#include "containers/rgw_cmemory.h"

/*
 * Basic types
 */
#include "containers/rgw_ctypes.h"

/*
 * Buffer
 */
#include "rgw_buffer.h"

/*
 * Base64 encoding/decoding
 */
#include "rgw_b64.h"

/*
 * Hex encoding/decoding
 */
#include "rgw_hex.h"

/*
 * XML parsing
 */
#include "rgw_xml.h"

/*
 * Containers
 */
#include "containers/rgw_carray.h"
#include "containers/rgw_cstring.h"
#include "containers/rgw_cmap.h"
#include "containers/rgw_cset.h"
#include "containers/rgw_clist.h"
#include "containers/rgw_cdeque.h"
#include "containers/rgw_cstack.h"
#include "containers/rgw_cqueue.h"
#include "containers/rgw_cpriority_queue.h"
#include "containers/rgw_chash_map.h"
#include "containers/rgw_coptional.h"

/*
 * OOP Framework
 */
#include "rgw_oop.h"

/*
 * Error Handling
 */
#include "rgw_errors.h"

/*
 * Note: csort.h is included via internal header and provides sorting functions
 * that work with the container types above
 */

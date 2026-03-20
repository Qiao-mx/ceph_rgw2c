/**
 * @file test_basic.c
 * @brief 测试 SAL 核心类型功能
 *
 * 本测试仅测试 SAL 核心类型的创建、设置和销毁，
 * 不依赖任何驱动实现。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rgw_sal_types.h"

static int test_user_id_create(void) {
    printf("Test: user_id_create_destroy\n");
    fflush(stdout);

    rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
    if (!uid) {
        printf("  FAILED: user_id_create returned NULL\n");
        return 1;
    }

    uid->id = strdup("test_user");
    uid->tenant = strdup("test_tenant");
    uid->type = 0;

    printf("  Created user_id: id='%s', tenant='%s'\n",
           uid->id ? uid->id : "NULL",
           uid->tenant ? uid->tenant : "NULL");

    /* 验证值 */
    if (!uid->id || strcmp(uid->id, "test_user") != 0) {
        printf("  FAILED: id mismatch\n");
        free(uid->id);
        free(uid->tenant);
        rgw_sal_user_id_destroy(uid);
        return 1;
    }

    rgw_sal_user_id_destroy(uid);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_bucket_id_create(void) {
    printf("Test: bucket_id_create_destroy\n");
    fflush(stdout);

    rgw_sal_bucket_id_t* bid = rgw_sal_bucket_id_create();
    if (!bid) {
        printf("  FAILED: bucket_id_create returned NULL\n");
        return 1;
    }

    bid->name = strdup("test_bucket");
    bid->tenant = strdup("test_tenant");
    bid->marker = strdup("marker123");
    bid->bucket_id = strdup("bucket_uuid");

    printf("  Created bucket_id: name='%s', tenant='%s'\n",
           bid->name ? bid->name : "NULL",
           bid->tenant ? bid->tenant : "NULL");

    /* 验证值 */
    if (!bid->name || strcmp(bid->name, "test_bucket") != 0) {
        printf("  FAILED: name mismatch\n");
        rgw_sal_bucket_id_destroy(bid);
        return 1;
    }

    rgw_sal_bucket_id_destroy(bid);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_obj_key_create(void) {
    printf("Test: obj_key_create_destroy\n");
    fflush(stdout);

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    if (!key) {
        printf("  FAILED: obj_key_create returned NULL\n");
        return 1;
    }

    key->name = strdup("test_object");
    key->instance = strdup("version_1");
    key->is_null = false;
    key->is_current = true;

    printf("  Created obj_key: name='%s', instance='%s'\n",
           key->name ? key->name : "NULL",
           key->instance ? key->instance : "NULL");

    /* 验证值 */
    if (!key->name || strcmp(key->name, "test_object") != 0) {
        printf("  FAILED: name mismatch\n");
        rgw_sal_obj_key_destroy(key);
        return 1;
    }

    rgw_sal_obj_key_destroy(key);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_attrs_create(void) {
    printf("Test: attrs_create_destroy\n");
    fflush(stdout);

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    if (!attrs) {
        printf("  FAILED: attrs_create returned NULL\n");
        return 1;
    }

    printf("  Created empty attrs container\n");

    /* 测试设置属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";
    uint8_t val3[] = "value3";

    int ret = rgw_sal_attrs_set(attrs, "key1", val1, strlen((char*)val1));
    if (ret != 0) {
        printf("  FAILED: attrs_set key1 returned %d\n", ret);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }

    ret = rgw_sal_attrs_set(attrs, "key2", val2, strlen((char*)val2));
    if (ret != 0) {
        printf("  FAILED: attrs_set key2 returned %d\n", ret);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }

    ret = rgw_sal_attrs_set(attrs, "key3", val3, strlen((char*)val3));
    if (ret != 0) {
        printf("  FAILED: attrs_set key3 returned %d\n", ret);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }

    printf("  Set 3 attributes\n");

    /* 测试获取属性 */
    uint8_t* out = NULL;
    size_t len = 0;

    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    if (ret != 0 || len != 6 || memcmp(out, "value1", 6) != 0) {
        printf("  FAILED: attrs_get key1 verification failed\n");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "key2", &out, &len);
    if (ret != 0 || len != 6 || memcmp(out, "value2", 6) != 0) {
        printf("  FAILED: attrs_get key2 verification failed\n");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "key3", &out, &len);
    if (ret != 0 || len != 6 || memcmp(out, "value3", 6) != 0) {
        printf("  FAILED: attrs_get key3 verification failed\n");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }
    free(out);

    printf("  Verified 3 attributes\n");

    /* 测试更新属性 */
    uint8_t new_val[] = "updated";
    ret = rgw_sal_attrs_set(attrs, "key1", new_val, strlen((char*)new_val));
    if (ret != 0) {
        printf("  FAILED: attrs_set update returned %d\n", ret);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }

    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    if (ret != 0 || len != 7 || memcmp(out, "updated", 7) != 0) {
        printf("  FAILED: attrs_get update verification failed\n");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }
    free(out);

    printf("  Attribute update verified\n");

    rgw_sal_attrs_destroy(attrs);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_attrs_not_found(void) {
    printf("Test: attrs_not_found\n");
    fflush(stdout);

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    if (!attrs) {
        printf("  FAILED: attrs_create returned NULL\n");
        return 1;
    }

    /* 测试获取不存在的属性 */
    uint8_t* out = NULL;
    size_t len = 0;
    int ret = rgw_sal_attrs_get(attrs, "nonexistent", &out, &len);

    if (ret == 0) {
        printf("  FAILED: expected error for nonexistent key\n");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        return 1;
    }

    printf("  Correctly returned error for nonexistent key\n");
    rgw_sal_attrs_destroy(attrs);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

int main(void) {
    int failed = 0;

    printf("========================================\n");
    printf("SAL Basic Types Test Suite\n");
    printf("========================================\n\n");
    fflush(stdout);

    failed += test_user_id_create();
    failed += test_bucket_id_create();
    failed += test_obj_key_create();
    failed += test_attrs_create();
    failed += test_attrs_not_found();

    printf("\n========================================\n");
    if (failed == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Tests FAILED: %d failures\n", failed);
    }
    printf("========================================\n");
    fflush(stdout);

    return failed;
}

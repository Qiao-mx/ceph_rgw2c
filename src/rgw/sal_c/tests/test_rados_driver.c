/**
 * @file test_rados_driver.c
 * @brief RADOS 驱动完整测试用例
 *
 * 测试 RADOS 驱动的所有核心功能
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "rgw_sal.h"
#include "rgw_sal_rados.h"

static int test_driver_create(void) {
    printf("Test: driver_create\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    const char* name = rgw_sal_get_driver_name(driver);
    if (!name || strcmp(name, "rados") != 0) {
        printf("FAILED: name mismatch\n");
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_driver_initialize(void) {
    printf("Test: driver_initialize\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    int ret = rgw_sal_init_driver(driver, NULL, NULL);
    if (ret != RGW_SAL_OK) {
        printf("FAILED: init returned %d\n", ret);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_driver_get_cluster_id(void) {
    printf("Test: driver_get_cluster_id\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    char* cluster_id = NULL;
    int ret = rgw_sal_rados_get_cluster_id(driver, &cluster_id, NULL, NULL);
    if (ret != RGW_SAL_OK) {
        printf("FAILED: get_cluster_id returned %d\n", ret);
        rgw_sal_destroy_driver(driver);
        return 1;
    }
    
    if (!cluster_id) {
        printf("FAILED: cluster_id is NULL\n");
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    printf("  cluster_id: '%s'\n", cluster_id);
    free(cluster_id);
    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_user_create(void) {
    printf("Test: user_create\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        printf("FAILED: user is NULL\n");
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    const char* id = user->vtable->get_id(user);
    if (!id || strcmp(id, "test_user") != 0) {
        printf("FAILED: user id mismatch\n");
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_user_attrs(void) {
    printf("Test: user_attrs\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        printf("FAILED: user is NULL\n");
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);
    if (!attrs) {
        printf("FAILED: attrs is NULL\n");
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    uint8_t value[] = {0x01, 0x02, 0x03};
    int ret = rgw_sal_attrs_set(attrs, "test_key", value, sizeof(value));
    if (ret != RGW_SAL_OK) {
        printf("FAILED: attrs_set returned %d\n", ret);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    uint8_t* out_value = NULL;
    size_t out_len = 0;
    ret = rgw_sal_attrs_get(attrs, "test_key", &out_value, &out_len);
    if (ret != RGW_SAL_OK) {
        printf("FAILED: attrs_get returned %d\n", ret);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    if (out_len != 3) {
        printf("FAILED: value_len mismatch\n");
        free(out_value);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    free(out_value);
    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_bucket_create(void) {
    printf("Test: bucket_create\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.tenant = strdup("test_tenant");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        printf("FAILED: bucket is NULL\n");
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    const char* name = bucket->vtable->get_name(bucket);
    if (!name || strcmp(name, "test_bucket") != 0) {
        printf("FAILED: bucket name mismatch\n");
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.tenant);
    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

static int test_object_create(void) {
    printf("Test: object_create\n");
    fflush(stdout);

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        printf("FAILED: driver is NULL\n");
        return 1;
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        printf("FAILED: bucket is NULL\n");
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");
    key.instance = strdup("version_id");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        printf("FAILED: object is NULL\n");
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    const char* obj_name = obj->vtable->get_name(obj);
    if (!obj_name || strcmp(obj_name, "test_object") != 0) {
        printf("FAILED: object name mismatch\n");
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    const char* instance = obj->vtable->get_instance(obj);
    if (!instance || strcmp(instance, "version_id") != 0) {
        printf("FAILED: object instance mismatch\n");
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        return 1;
    }

    rgw_sal_object_destroy(obj);
    rgw_sal_bucket_destroy(bucket);
    free(key.name);
    free(key.instance);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);
    printf("  PASSED\n");
    fflush(stdout);
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("RADOS Driver Complete Test Suite\n");
    printf("========================================\n\n");
    fflush(stdout);

    int failed = 0;

    printf("--- Driver Tests ---\n");
    fflush(stdout);
    failed += test_driver_create();
    failed += test_driver_initialize();
    failed += test_driver_get_cluster_id();

    printf("\n--- User Tests ---\n");
    fflush(stdout);
    failed += test_user_create();
    failed += test_user_attrs();

    printf("\n--- Bucket Tests ---\n");
    fflush(stdout);
    failed += test_bucket_create();

    printf("\n--- Object Tests ---\n");
    fflush(stdout);
    failed += test_object_create();

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

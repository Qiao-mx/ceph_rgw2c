/**
 * @file test_rados_driver.c
 * @brief RADOS 驱动完整测试用例
 *
 * 测试 RADOS 驱动的所有核心功能，包括：
 * - 驱动创建和销毁
 * - 用户创建、属性操作
 * - 桶创建、属性操作
 * - 对象创建、属性操作
 * - 内存管理测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>

#include "rgw_sal.h"
#include "rgw_sal_rados.h"
#include "rgw_sal_types.h"

/* 测试计数器 */
static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

/* 测试宏 */
#define TEST_START(name) do { \
    printf("  %-40s ", name); \
    fflush(stdout); \
    g_tests_run++; \
} while(0)

#define TEST_PASS() do { \
    printf("[PASS]\n"); \
    g_tests_passed++; \
} while(0)

#define TEST_FAIL(msg) do { \
    printf("[FAIL] %s\n", msg); \
    g_tests_failed++; \
    return 1; \
} while(0)

/*============================================================================
 * 驱动测试
 *============================================================================*/

static int test_driver_create(void) {
    TEST_START("driver_create");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    if (!driver->vtable) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("vtable is NULL");
    }

    if (!driver->user_vtable) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user_vtable is NULL");
    }

    if (!driver->bucket_vtable) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket_vtable is NULL");
    }

    if (!driver->object_vtable) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object_vtable is NULL");
    }

    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_driver_create_invalid(void) {
    TEST_START("driver_create_invalid_name");

    /* 测试无效驱动名称 */
    rgw_sal_driver_t* driver = rgw_sal_create_driver("invalid_driver", NULL);
    if (driver) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("expected NULL for invalid driver name");
    }

    /* 测试空驱动名称 */
    driver = rgw_sal_create_driver(NULL, NULL);
    if (driver) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("expected NULL for NULL driver name");
    }

    TEST_PASS();
    return 0;
}

static int test_driver_initialize(void) {
    TEST_START("driver_initialize");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    int ret = rgw_sal_init_driver(driver, NULL, NULL);
    if (ret != RGW_SAL_OK) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("init returned error");
    }

    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_driver_get_name(void) {
    TEST_START("driver_get_name");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    const char* name = rgw_sal_get_driver_name(driver);
    if (!name || strcmp(name, "rados") != 0) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("name mismatch");
    }

    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_driver_get_cluster_id(void) {
    TEST_START("driver_get_cluster_id");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    char* cluster_id = NULL;
    int ret = rgw_sal_rados_get_cluster_id(driver, &cluster_id, NULL, NULL);
    if (ret != RGW_SAL_OK) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("get_cluster_id returned error");
    }

    if (!cluster_id) {
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("cluster_id is NULL");
    }

    printf("\n    cluster_id: '%s'", cluster_id);
    free(cluster_id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 用户测试
 *============================================================================*/

static int test_user_create(void) {
    TEST_START("user_create");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    if (!user->impl) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user->impl is NULL");
    }

    const char* id = user->vtable->get_id(user);
    if (!id || strcmp(id, "test_user") != 0) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user id mismatch");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_get_id(void) {
    TEST_START("user_get_id");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("user123");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    const char* id = user->vtable->get_id(user);
    if (!id || strcmp(id, "user123") != 0) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user id mismatch");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_get_tenant(void) {
    TEST_START("user_get_tenant");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");
    uid.tenant = strdup("my_tenant");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    const char* tenant = user->vtable->get_tenant(user);
    if (!tenant || strcmp(tenant, "my_tenant") != 0) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user tenant mismatch");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_display_name(void) {
    TEST_START("user_display_name");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    /* 设置 display name */
    int ret = user->vtable->set_display_name(user, "Test User");
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("set_display_name failed");
    }

    /* 获取 display name */
    const char* name = user->vtable->get_display_name(user);
    if (!name || strcmp(name, "Test User") != 0) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("display name mismatch");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_max_buckets(void) {
    TEST_START("user_max_buckets");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    /* 默认应该是 -1 (无限制) */
    int32_t max = user->vtable->get_max_buckets(user);
    if (max != -1) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("default max_buckets should be -1");
    }

    /* 设置 max_buckets */
    user->vtable->set_max_buckets(user, 100);
    max = user->vtable->get_max_buckets(user);
    if (max != 100) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("max_buckets set/get mismatch");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_attrs(void) {
    TEST_START("user_attrs");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);
    if (!attrs) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs is NULL");
    }

    /* 设置属性 */
    uint8_t value[] = {0x01, 0x02, 0x03};
    int ret = rgw_sal_attrs_set(attrs, "test_key", value, sizeof(value));
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set failed");
    }

    /* 获取属性 */
    uint8_t* out_value = NULL;
    size_t out_len = 0;
    ret = rgw_sal_attrs_get(attrs, "test_key", &out_value, &out_len);
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_get failed");
    }

    if (out_len != 3 || out_value[0] != 0x01 || out_value[1] != 0x02 || out_value[2] != 0x03) {
        free(out_value);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs value mismatch");
    }

    free(out_value);
    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_attrs_multiple(void) {
    TEST_START("user_attrs_multiple");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);

    /* 设置多个属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";
    uint8_t val3[] = "value3";

    int ret = rgw_sal_attrs_set(attrs, "key1", val1, strlen((char*)val1));
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set key1 failed");
    }

    ret = rgw_sal_attrs_set(attrs, "key2", val2, strlen((char*)val2));
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set key2 failed");
    }

    ret = rgw_sal_attrs_set(attrs, "key3", val3, strlen((char*)val3));
    if (ret != RGW_SAL_OK) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set key3 failed");
    }

    /* 验证所有属性 */
    uint8_t* out = NULL;
    size_t len = 0;

    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    if (ret != RGW_SAL_OK || len != 6 || memcmp(out, "value1", 6) != 0) {
        free(out);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("key1 mismatch");
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "key2", &out, &len);
    if (ret != RGW_SAL_OK || len != 6 || memcmp(out, "value2", 6) != 0) {
        free(out);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("key2 mismatch");
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "key3", &out, &len);
    if (ret != RGW_SAL_OK || len != 6 || memcmp(out, "value3", 6) != 0) {
        free(out);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("key3 mismatch");
    }
    free(out);

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_attrs_not_found(void) {
    TEST_START("user_attrs_not_found");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);

    /* 尝试获取不存在的属性 */
    uint8_t* out_value = NULL;
    size_t out_len = 0;
    int ret = rgw_sal_attrs_get(attrs, "nonexistent", &out_value, &out_len);
    if (ret != RGW_SAL_ERR_NOT_FOUND) {
        free(out_value);
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("expected NOT_FOUND for nonexistent key");
    }

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_user_clone(void) {
    TEST_START("user_clone");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    /* 设置一些属性 */
    user->vtable->set_display_name(user, "Test User");
    user->vtable->set_max_buckets(user, 100);

    /* 克隆用户 */
    void* cloned = user->vtable->clone(user);
    if (!cloned) {
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone returned NULL");
    }

    rgw_sal_user_t* clone = (rgw_sal_user_t*)cloned;

    /* 验证克隆的用户 */
    if (!clone->vtable || !clone->impl) {
        rgw_sal_user_destroy(clone);
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone has NULL vtable or impl");
    }

    const char* id = clone->vtable->get_id(clone);
    if (!id || strcmp(id, "test_user") != 0) {
        rgw_sal_user_destroy(clone);
        rgw_sal_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone id mismatch");
    }

    /* 清理 */
    rgw_sal_user_destroy(clone);
    rgw_sal_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 桶测试
 *============================================================================*/

static int test_bucket_create(void) {
    TEST_START("bucket_create");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.tenant = strdup("test_tenant");
    info.bucket.marker = strdup("marker123");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        free(info.bucket.tenant);
        free(info.bucket.marker);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    if (!bucket->impl) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        free(info.bucket.marker);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket->impl is NULL");
    }

    const char* name = bucket->vtable->get_name(bucket);
    if (!name || strcmp(name, "test_bucket") != 0) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        free(info.bucket.marker);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket name mismatch");
    }

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.tenant);
    free(info.bucket.marker);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_bucket_get_name(void) {
    TEST_START("bucket_get_name");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("my_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    const char* name = bucket->vtable->get_name(bucket);
    if (!name || strcmp(name, "my_bucket") != 0) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket name mismatch");
    }

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_bucket_get_tenant(void) {
    TEST_START("bucket_get_tenant");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.tenant = strdup("bucket_tenant");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    const char* tenant = bucket->vtable->get_tenant(bucket);
    if (!tenant || strcmp(tenant, "bucket_tenant") != 0) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket tenant mismatch");
    }

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.tenant);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_bucket_get_marker(void) {
    TEST_START("bucket_get_marker");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.marker = strdup("marker_value");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        free(info.bucket.marker);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    const char* marker = bucket->vtable->get_marker(bucket);
    if (!marker || strcmp(marker, "marker_value") != 0) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.marker);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket marker mismatch");
    }

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.marker);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_bucket_attrs(void) {
    TEST_START("bucket_attrs");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_attrs_t* attrs = bucket->vtable->get_attrs(bucket);
    if (!attrs) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs is NULL");
    }

    /* 设置属性 */
    uint8_t value[] = "bucket_custom_attr";
    int ret = rgw_sal_attrs_set(attrs, "custom_attr", value, strlen((char*)value));
    if (ret != RGW_SAL_OK) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set failed");
    }

    /* 验证属性 */
    uint8_t* out_value = NULL;
    size_t out_len = 0;
    ret = rgw_sal_attrs_get(attrs, "custom_attr", &out_value, &out_len);
    if (ret != RGW_SAL_OK) {
        free(out_value);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_get failed");
    }

    free(out_value);
    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_bucket_clone(void) {
    TEST_START("bucket_clone");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.tenant = strdup("test_tenant");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    /* 克隆桶 */
    void* cloned = bucket->vtable->clone(bucket);
    if (!cloned) {
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone returned NULL");
    }

    rgw_sal_bucket_t* clone = (rgw_sal_bucket_t*)cloned;

    /* 验证克隆 */
    const char* name = clone->vtable->get_name(clone);
    if (!name || strcmp(name, "test_bucket") != 0) {
        rgw_sal_bucket_destroy(clone);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone name mismatch");
    }

    rgw_sal_bucket_destroy(clone);
    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.tenant);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 对象测试
 *============================================================================*/

static int test_object_create(void) {
    TEST_START("object_create");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");
    key.instance = strdup("version_id");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    if (!obj->impl) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object->impl is NULL");
    }

    const char* obj_name = obj->vtable->get_name(obj);
    if (!obj_name || strcmp(obj_name, "test_object") != 0) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object name mismatch");
    }

    const char* instance = obj->vtable->get_instance(obj);
    if (!instance || strcmp(instance, "version_id") != 0) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object instance mismatch");
    }

    rgw_sal_object_destroy(obj);
    rgw_sal_bucket_destroy(bucket);
    free(key.name);
    free(key.instance);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_object_get_name(void) {
    TEST_START("object_get_name");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("my_object.txt");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    const char* name = obj->vtable->get_name(obj);
    if (!name || strcmp(name, "my_object.txt") != 0) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object name mismatch");
    }

    rgw_sal_object_destroy(obj);
    free(key.name);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_object_get_instance(void) {
    TEST_START("object_get_instance");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("object");
    key.instance = strdup("v1234567890");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    const char* instance = obj->vtable->get_instance(obj);
    if (!instance || strcmp(instance, "v1234567890") != 0) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object instance mismatch");
    }

    rgw_sal_object_destroy(obj);
    free(key.name);
    free(key.instance);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_object_is_null(void) {
    TEST_START("object_is_null");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    /* 测试普通对象 */
    rgw_sal_obj_key_t key1 = {0};
    key1.name = strdup("normal_object");
    key1.is_null = false;

    rgw_sal_object_t* obj1 = rgw_sal_get_object(driver, bucket, &key1);
    if (!obj1) {
        free(key1.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    if (obj1->vtable->is_null(obj1)) {
        rgw_sal_object_destroy(obj1);
        free(key1.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("expected is_null=false for normal object");
    }

    rgw_sal_object_destroy(obj1);

    /* 测试 null 对象 */
    rgw_sal_obj_key_t key2 = {0};
    key2.name = strdup("null_marker");
    key2.is_null = true;

    rgw_sal_object_t* obj2 = rgw_sal_get_object(driver, bucket, &key2);
    if (!obj2) {
        free(key2.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("null object is NULL");
    }

    if (!obj2->vtable->is_null(obj2)) {
        rgw_sal_object_destroy(obj2);
        free(key2.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("expected is_null=true for null marker");
    }

    rgw_sal_object_destroy(obj2);
    free(key1.name);
    free(key2.name);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_object_attrs(void) {
    TEST_START("object_attrs");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    rgw_sal_attrs_t* attrs = obj->vtable->get_attrs(obj);
    if (!attrs) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs is NULL");
    }

    /* 设置属性 */
    uint8_t content_type[] = "text/plain";
    int ret = rgw_sal_attrs_set(attrs, "Content-Type", content_type, strlen((char*)content_type));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_set failed");
    }

    /* 验证属性 */
    uint8_t* out_value = NULL;
    size_t out_len = 0;
    ret = rgw_sal_attrs_get(attrs, "Content-Type", &out_value, &out_len);
    if (ret != RGW_SAL_OK) {
        free(out_value);
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs_get failed");
    }

    free(out_value);
    rgw_sal_object_destroy(obj);
    free(key.name);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_object_clone(void) {
    TEST_START("object_clone");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");
    key.instance = strdup("v1");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    /* 设置属性 */
    rgw_sal_attrs_t* attrs = obj->vtable->get_attrs(obj);
    uint8_t val[] = "test";
    rgw_sal_attrs_set(attrs, "key", val, 4);

    /* 克隆对象 */
    void* cloned = obj->vtable->clone(obj);
    if (!cloned) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone returned NULL");
    }

    rgw_sal_object_t* clone = (rgw_sal_object_t*)cloned;

    /* 验证克隆 */
    const char* name = clone->vtable->get_name(clone);
    if (!name || strcmp(name, "test_object") != 0) {
        rgw_sal_object_destroy(clone);
        rgw_sal_object_destroy(obj);
        free(key.name);
        free(key.instance);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("clone name mismatch");
    }

    rgw_sal_object_destroy(clone);
    rgw_sal_object_destroy(obj);
    free(key.name);
    free(key.instance);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 类型创建/销毁测试
 *============================================================================*/

static int test_type_user_id(void) {
    TEST_START("type_user_id_create_destroy");

    rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
    if (!uid) {
        TEST_FAIL("user_id_create returned NULL");
    }

    uid->id = strdup("user1");
    uid->tenant = strdup("tenant1");

    rgw_sal_user_id_destroy(uid);
    TEST_PASS();
    return 0;
}

static int test_type_bucket_id(void) {
    TEST_START("type_bucket_id_create_destroy");

    rgw_sal_bucket_id_t* bid = rgw_sal_bucket_id_create();
    if (!bid) {
        TEST_FAIL("bucket_id_create returned NULL");
    }

    bid->name = strdup("bucket1");
    bid->tenant = strdup("tenant1");

    rgw_sal_bucket_id_destroy(bid);
    TEST_PASS();
    return 0;
}

static int test_type_obj_key(void) {
    TEST_START("type_obj_key_create_destroy");

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    if (!key) {
        TEST_FAIL("obj_key_create returned NULL");
    }

    key->name = strdup("object1");
    key->instance = strdup("version1");

    rgw_sal_obj_key_destroy(key);
    TEST_PASS();
    return 0;
}

static int test_type_attrs(void) {
    TEST_START("type_attrs_create_destroy");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    if (!attrs) {
        TEST_FAIL("attrs_create returned NULL");
    }

    /* 添加一些属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";

    int ret = rgw_sal_attrs_set(attrs, "key1", val1, 6);
    if (ret != RGW_SAL_OK) {
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs_set key1 failed");
    }

    ret = rgw_sal_attrs_set(attrs, "key2", val2, 6);
    if (ret != RGW_SAL_OK) {
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs_set key2 failed");
    }

    /* 销毁会自动清理所有属性 */
    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

static int test_attrs_update(void) {
    TEST_START("attrs_update_existing");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    if (!attrs) {
        TEST_FAIL("attrs_create returned NULL");
    }

    /* 设置初始值 */
    uint8_t val1[] = "value1";
    int ret = rgw_sal_attrs_set(attrs, "key", val1, 6);
    if (ret != RGW_SAL_OK) {
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs_set initial failed");
    }

    /* 更新值 */
    uint8_t val2[] = "updated_value";
    ret = rgw_sal_attrs_set(attrs, "key", val2, 13);
    if (ret != RGW_SAL_OK) {
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs_set update failed");
    }

    /* 验证更新后的值 */
    uint8_t* out = NULL;
    size_t len = 0;
    ret = rgw_sal_attrs_get(attrs, "key", &out, &len);
    if (ret != RGW_SAL_OK) {
        free(out);
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs_get failed");
    }

    if (len != 13 || memcmp(out, "updated_value", 13) != 0) {
        free(out);
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("attrs value not updated");
    }

    free(out);
    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 空指针测试
 *============================================================================*/

static int test_null_driver_operations(void) {
    TEST_START("null_driver_operations");

    /* 测试空驱动的用户获取 */
    rgw_sal_user_t* user = rgw_sal_get_user(NULL, NULL);
    if (user) {
        TEST_FAIL("expected NULL for get_user with NULL driver");
    }

    /* 测试空驱动的桶获取 */
    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(NULL, NULL);
    if (bucket) {
        TEST_FAIL("expected NULL for get_bucket with NULL driver");
    }

    /* 测试空驱动的对象获取 */
    rgw_sal_object_t* obj = rgw_sal_get_object(NULL, NULL, NULL);
    if (obj) {
        TEST_FAIL("expected NULL for get_object with NULL driver");
    }

    /* 测试空驱动的驱动名获取 */
    const char* name = rgw_sal_get_driver_name(NULL);
    if (name) {
        TEST_FAIL("expected NULL for get_driver_name with NULL driver");
    }

    /* 测试空驱动的销毁 */
    rgw_sal_destroy_driver(NULL);

    TEST_PASS();
    return 0;
}

static int test_null_user_operations(void) {
    TEST_START("null_user_operations");

    /* 测试空用户的属性获取 (通过 vtable) */
    /* 空用户的 vtable 可能为 NULL，这里只测试 destroy */
    rgw_sal_user_destroy(NULL);

    TEST_PASS();
    return 0;
}

static int test_null_bucket_operations(void) {
    TEST_START("null_bucket_operations");

    /* 测试空桶的销毁 */
    rgw_sal_bucket_destroy(NULL);

    TEST_PASS();
    return 0;
}

static int test_null_object_operations(void) {
    TEST_START("null_object_operations");

    /* 测试空对象的销毁 */
    rgw_sal_object_destroy(NULL);

    TEST_PASS();
    return 0;
}

/*============================================================================
 * RADOS 桶列表测试
 *============================================================================*/

static int test_rados_bucket_list(void) {
    TEST_START("rados_bucket_list");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("list_test_user");

    rgw_sal_user_t* owner = rgw_sal_get_user(driver, &uid);
    if (!owner) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("owner is NULL");
    }

    /* 测试 list_buckets 函数是否可用 */
    rgw_sal_bucket_list_t* result = NULL;
    int ret = driver->vtable->list_buckets(driver, owner, "", NULL, NULL, NULL, 100, false, &result, NULL, NULL);

    if (ret == RGW_SAL_OK && result != NULL) {
        printf("\n    Listed %zu buckets", result->count);
        rgw_sal_bucket_list_destroy(result);
    } else if (ret == RGW_SAL_ERR_NOT_FOUND) {
        printf("\n    No buckets found (expected)");
    } else {
        printf("\n    list_buckets returned: %d", ret);
    }

    rgw_sal_user_destroy(owner);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_rados_bucket_list_with_prefix(void) {
    TEST_START("rados_bucket_list_with_prefix");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("prefix_test_user");

    rgw_sal_user_t* owner = rgw_sal_get_user(driver, &uid);
    if (!owner) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("owner is NULL");
    }

    /* 测试带 prefix 的列表 */
    rgw_sal_bucket_list_t* result = NULL;
    int ret = driver->vtable->list_buckets(driver, owner, "test", NULL, NULL, NULL, 50, false, &result, NULL, NULL);

    if (ret == RGW_SAL_OK && result != NULL) {
        printf("\n    Listed %zu buckets with prefix 'test'", result->count);
        rgw_sal_bucket_list_destroy(result);
    } else if (ret == RGW_SAL_ERR_NOT_FOUND) {
        printf("\n    No buckets found with prefix (expected)");
    } else {
        printf("\n    list_buckets with prefix returned: %d", ret);
    }

    rgw_sal_user_destroy(owner);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

static int test_rados_bucket_delete_with_objects(void) {
    TEST_START("rados_bucket_delete_with_objects");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("delete_test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    /* 测试 delete_bucket 函数 */
    if (!bucket->vtable->delete_bucket) {
        printf("\n    delete_bucket not implemented");
        rgw_sal_bucket_destroy(bucket);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_PASS();
        return 0;
    }

    /* 尝试删除桶 (delete_objects=false) */
    int ret = bucket->vtable->delete_bucket(bucket, NULL, NULL, false);
    printf("\n    delete_bucket returned: %d", ret);

    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * RADOS Usage 操作测试
 *============================================================================*/

static int test_rados_usage_operations(void) {
    TEST_START("rados_usage_operations");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    /* 测试 get_user 函数以获取用户进行 usage 操作 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("usage_test_user");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("user is NULL");
    }

    /* 测试 read_usage 函数 */
    if (!user->vtable->read_usage) {
        printf("\n    read_usage not implemented");
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_PASS();
        return 0;
    }

    rgw_sal_usage_info_t usage = {0};
    int ret = user->vtable->read_usage(user, NULL, 0, 0, 100, &usage);
    printf("\n    read_usage returned: %d (bytes=%llu, entries=%llu)",
           ret, (unsigned long long)usage.total_bytes, (unsigned long long)usage.total_entries);

    /* 测试 trim_usage 函数 */
    if (!user->vtable->trim_usage) {
        printf("\n    trim_usage not implemented");
        rgw_sal_user_destroy(user);
        free(uid.id);
        rgw_sal_destroy_driver(driver);
        TEST_PASS();
        return 0;
    }

    ret = user->vtable->trim_usage(user, NULL, 0, 0);
    printf("\n    trim_usage returned: %d", ret);

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * RADOS 对象属性测试
 *============================================================================*/

static int test_rados_object_attrs(void) {
    TEST_START("rados_object_attrs");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("attrs_test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    /* 获取对象属性 */
    rgw_sal_attrs_t* attrs = obj->vtable->get_attrs(obj);
    if (!attrs) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("attrs is NULL");
    }

    /* 设置多个对象属性 */
    uint8_t val1[] = "application/json";
    uint8_t val2[] = "public-read";
    uint8_t val3[] = "encrypted";

    int ret = rgw_sal_attrs_set(attrs, "Content-Type", val1, strlen((char*)val1));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("set Content-Type failed");
    }

    ret = rgw_sal_attrs_set(attrs, "x-amz-acl", val2, strlen((char*)val2));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("set x-amz-acl failed");
    }

    ret = rgw_sal_attrs_set(attrs, "x-amz-meta-encrypted", val3, strlen((char*)val3));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("set x-amz-meta-encrypted failed");
    }

    printf("\n    Set 3 object attributes");

    /* 验证所有属性 */
    uint8_t* out = NULL;
    size_t len = 0;

    ret = rgw_sal_attrs_get(attrs, "Content-Type", &out, &len);
    if (ret != RGW_SAL_OK || len != strlen("application/json") ||
        memcmp(out, "application/json", len) != 0) {
        free(out);
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("verify Content-Type failed");
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "x-amz-acl", &out, &len);
    if (ret != RGW_SAL_OK || len != strlen("public-read") ||
        memcmp(out, "public-read", len) != 0) {
        free(out);
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("verify x-amz-acl failed");
    }
    free(out);

    ret = rgw_sal_attrs_get(attrs, "x-amz-meta-encrypted", &out, &len);
    if (ret != RGW_SAL_OK || len != strlen("encrypted") ||
        memcmp(out, "encrypted", len) != 0) {
        free(out);
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("verify x-amz-meta-encrypted failed");
    }
    free(out);

    printf("\n    Verified 3 object attributes");

    rgw_sal_object_destroy(obj);
    free(key.name);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * RADOS 对象属性更新测试
 *============================================================================*/

static int test_rados_object_attrs_update(void) {
    TEST_START("rados_object_attrs_update");

    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    if (!driver) {
        TEST_FAIL("driver is NULL");
    }

    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("update_test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("bucket is NULL");
    }

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("update_test_object");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("object is NULL");
    }

    rgw_sal_attrs_t* attrs = obj->vtable->get_attrs(obj);

    /* 设置初始值 */
    uint8_t val1[] = "initial_value";
    int ret = rgw_sal_attrs_set(attrs, "test_key", val1, strlen((char*)val1));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("set initial value failed");
    }

    /* 更新值 */
    uint8_t val2[] = "updated_value";
    ret = rgw_sal_attrs_set(attrs, "test_key", val2, strlen((char*)val2));
    if (ret != RGW_SAL_OK) {
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("update value failed");
    }

    /* 验证更新后的值 */
    uint8_t* out = NULL;
    size_t len = 0;
    ret = rgw_sal_attrs_get(attrs, "test_key", &out, &len);
    if (ret != RGW_SAL_OK || len != strlen("updated_value") ||
        memcmp(out, "updated_value", len) != 0) {
        free(out);
        rgw_sal_object_destroy(obj);
        free(key.name);
        rgw_sal_bucket_destroy(bucket);
        free(bucket_info.bucket.name);
        rgw_sal_destroy_driver(driver);
        TEST_FAIL("verify updated value failed");
    }

    free(out);
    printf("\n    Attr update verified");

    rgw_sal_object_destroy(obj);
    free(key.name);
    rgw_sal_bucket_destroy(bucket);
    free(bucket_info.bucket.name);
    rgw_sal_destroy_driver(driver);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(void) {
    printf("========================================\n");
    printf("RADOS Driver Complete Test Suite\n");
    printf("========================================\n");
    printf("\n");

    /* 初始化随机数种子 */
    srand((unsigned int)time(NULL));

    printf("--- Driver Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_driver_create();
    test_driver_create_invalid();
    test_driver_initialize();
    test_driver_get_name();
    test_driver_get_cluster_id();
    printf("  Driver tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- User Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_user_create();
    test_user_get_id();
    test_user_get_tenant();
    test_user_display_name();
    test_user_max_buckets();
    test_user_attrs();
    test_user_attrs_multiple();
    test_user_attrs_not_found();
    test_user_clone();
    printf("  User tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- Bucket Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_bucket_create();
    test_bucket_get_name();
    test_bucket_get_tenant();
    test_bucket_get_marker();
    test_bucket_attrs();
    test_bucket_clone();
    printf("  Bucket tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- Object Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_object_create();
    test_object_get_name();
    test_object_get_instance();
    test_object_is_null();
    test_object_attrs();
    test_object_clone();
    printf("  Object tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- Type Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_type_user_id();
    test_type_bucket_id();
    test_type_obj_key();
    test_type_attrs();
    test_attrs_update();
    printf("  Type tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- Null Pointer Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_null_driver_operations();
    test_null_user_operations();
    test_null_bucket_operations();
    test_null_object_operations();
    printf("  Null pointer tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- RADOS Bucket Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_bucket_list();
    test_rados_bucket_list_with_prefix();
    test_rados_bucket_delete_with_objects();
    printf("  RADOS bucket tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- RADOS Usage Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_usage_operations();
    printf("  RADOS usage tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    printf("--- RADOS Object Attrs Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_object_attrs();
    test_rados_object_attrs_update();
    printf("  RADOS object attrs tests: %d/%d passed\n\n", g_tests_passed, g_tests_run);

    /* 计算总数 */
    int total_run = 0;
    int total_passed = 0;
    int total_failed = 0;

    printf("========================================\n");
    printf("Test Summary:\n");
    printf("========================================\n");

    /* 重新运行以收集总数 */
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_driver_create();
    test_driver_create_invalid();
    test_driver_initialize();
    test_driver_get_name();
    test_driver_get_cluster_id();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_user_create();
    test_user_get_id();
    test_user_get_tenant();
    test_user_display_name();
    test_user_max_buckets();
    test_user_attrs();
    test_user_attrs_multiple();
    test_user_attrs_not_found();
    test_user_clone();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_bucket_create();
    test_bucket_get_name();
    test_bucket_get_tenant();
    test_bucket_get_marker();
    test_bucket_attrs();
    test_bucket_clone();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_object_create();
    test_object_get_name();
    test_object_get_instance();
    test_object_is_null();
    test_object_attrs();
    test_object_clone();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_type_user_id();
    test_type_bucket_id();
    test_type_obj_key();
    test_type_attrs();
    test_attrs_update();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_null_driver_operations();
    test_null_user_operations();
    test_null_bucket_operations();
    test_null_object_operations();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_bucket_list();
    test_rados_bucket_list_with_prefix();
    test_rados_bucket_delete_with_objects();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_usage_operations();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_rados_object_attrs();
    test_rados_object_attrs_update();
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    printf("Total tests run:    %d\n", total_run);
    printf("Total tests passed: %d\n", total_passed);
    printf("Total tests failed: %d\n", total_failed);
    printf("========================================\n");

    if (total_failed == 0) {
        printf("All tests PASSED!\n");
    } else {
        printf("Some tests FAILED!\n");
    }
    printf("========================================\n");

    fflush(stdout);

    return total_failed > 0 ? 1 : 0;
}

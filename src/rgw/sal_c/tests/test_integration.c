/**
 * @file test_integration.c
 * @brief RADOS 驱动集成测试
 *
 * 测试 RADOS 驱动的完整工作流程，包括：
 * - 用户完整生命周期
 * - 桶完整生命周期
 * - 对象操作流程
 * - 复合操作测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#include "rgw_sal.h"
#include "rgw_sal_types.h"
#include "rgw_sal_rados.h"

/*============================================================================
 * 测试框架
 *============================================================================*/

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_tests_skipped = 0;

#define TEST_START(name) do { \
    printf("  %-50s ", name); \
    fflush(stdout); \
    g_tests_run++; \
} while(0)

#define TEST_PASS() do { \
    printf("[PASS]\n"); \
    fflush(stdout); \
    g_tests_passed++; \
} while(0)

#define TEST_FAIL(msg) do { \
    printf("[FAIL] %s\n", msg); \
    fflush(stdout); \
    g_tests_failed++; \
} while(0)

#define TEST_SKIP(msg) do { \
    printf("[SKIP] %s\n", msg); \
    g_tests_skipped++; \
} while(0)

#define TEST_EXPECT(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        printf("[FAIL] %s (expected %d, got %d)\n", msg, (int)(expected), (int)(actual)); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

#define TEST_EXPECT_STR(actual, expected, msg) do { \
    if (!actual || !expected || strcmp(actual, expected) != 0) { \
        printf("[FAIL] %s (expected '%s', got '%s')\n", msg, expected, actual ? actual : "NULL"); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

#define TEST_EXPECT_NOT_NULL(ptr, msg) do { \
    if (!(ptr)) { \
        printf("[FAIL] %s (expected non-NULL)\n", msg); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

/*============================================================================
 * 测试夹具
 *============================================================================*/

typedef struct {
    rgw_sal_driver_t* driver;
    rgw_sal_user_t* user;
    rgw_sal_bucket_t* bucket;
    int passed;
    int failed;
} test_fixture_t;

static test_fixture_t* create_fixture(void) {
    test_fixture_t* fixture = (test_fixture_t*)calloc(1, sizeof(test_fixture_t));
    if (!fixture) return NULL;

    fixture->driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!fixture->driver) {
        free(fixture);
        return NULL;
    }

    return fixture;
}

static void destroy_fixture(test_fixture_t* fixture) {
    if (!fixture) return;

    if (fixture->user) {
        rgw_sal_rados_user_destroy(fixture->user);
    }

    if (fixture->bucket) {
        rgw_sal_rados_bucket_destroy(fixture->bucket);
    }

    if (fixture->driver) {
        rgw_sal_rados_driver_destroy(fixture->driver);
    }

    free(fixture);
}

static void print_fixture_result(test_fixture_t* fixture, const char* name) {
    printf("  %s: %d/%d passed, %d failed\n",
           name, fixture->passed, fixture->passed + fixture->failed, fixture->failed);
}

/*============================================================================
 * 测试用例
 *============================================================================*/

/* 测试用户生命周期 */
static int test_user_lifecycle(test_fixture_t* fixture) {
    TEST_START("user_lifecycle");

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("lifecycle_user");
    uid.tenant = strdup("test_tenant");

    fixture->user = rgw_sal_rados_get_user(fixture->driver, &uid);
    if (!fixture->user) {
        free(uid.id);
        free(uid.tenant);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 设置用户属性 */
    rgw_sal_rados_user_set_display_name(fixture->user, "Lifecycle Test User");
    rgw_sal_rados_user_set_max_buckets(fixture->user, 50);

    /* 验证属性 */
    const char* display_name = rgw_sal_rados_user_get_display_name(fixture->user);
    if (!display_name || strcmp(display_name, "Lifecycle Test User") != 0) {
        free(uid.id);
        free(uid.tenant);
        TEST_FAIL("display_name mismatch");
        return 1;
    }

    int32_t max_buckets = rgw_sal_rados_user_get_max_buckets(fixture->user);
    if (max_buckets != 50) {
        free(uid.id);
        free(uid.tenant);
        TEST_FAIL("max_buckets mismatch");
        return 1;
    }

    printf("\n    User created with display_name='%s', max_buckets=%d",
           display_name, max_buckets);

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    fixture->passed++;
    TEST_PASS();
    return 0;
}

/* 测试桶生命周期 */
static int test_bucket_lifecycle(test_fixture_t* fixture) {
    TEST_START("bucket_lifecycle");

    /* 创建桶 */
    rgw_sal_bucket_id_t bid = {0};
    bid.name = strdup("lifecycle_bucket");
    bid.tenant = strdup("test_tenant");
    bid.marker = strdup("marker_123");
    bid.bucket_id = strdup("bucket_uuid_456");

    fixture->bucket = rgw_sal_rados_get_bucket(fixture->driver, &bid);
    if (!fixture->bucket) {
        free(bid.name);
        free(bid.tenant);
        free(bid.marker);
        free(bid.bucket_id);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 设置标签 */
    rgw_sal_rados_bucket_set_tag(fixture->bucket, "bucket_tag");

    /* 验证标签 */
    const char* tag = rgw_sal_rados_bucket_get_tag(fixture->bucket);
    if (!tag || strcmp(tag, "bucket_tag") != 0) {
        free(bid.name);
        free(bid.tenant);
        free(bid.marker);
        free(bid.bucket_id);
        TEST_FAIL("tag mismatch");
        return 1;
    }

    printf("\n    Bucket created with name='%s', tag='%s'", bid.name, tag);

    /* 清理 */
    free(bid.name);
    free(bid.tenant);
    free(bid.marker);
    free(bid.bucket_id);
    fixture->passed++;
    TEST_PASS();
    return 0;
}

/* 测试对象生命周期 */
static int test_object_lifecycle(test_fixture_t* fixture) {
    TEST_START("object_lifecycle");

    /* 创建桶 */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("test_bucket");

    fixture->bucket = rgw_sal_rados_get_bucket(fixture->driver, &bucket_id);
    if (!fixture->bucket) {
        free(bucket_id.name);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object");
    key.instance = strdup("v1");

    rgw_sal_object_t* obj = rgw_sal_rados_get_object(fixture->driver, fixture->bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        free(bucket_info.bucket.name);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 设置属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_object_get_attrs(obj);
    if (attrs) {
        uint8_t val[] = "test_value";
        rgw_sal_attrs_set(attrs, "content-type", val, sizeof(val) - 1);
    }

    /* 验证属性 */
    uint8_t* out = NULL;
    size_t len = 0;
    attrs = rgw_sal_rados_object_get_attrs(obj);
    int ret = rgw_sal_attrs_get(attrs, "content-type", &out, &len);
    if (ret != 0 || len != 10 || memcmp(out, "test_value", 10) != 0) {
        free(key.name);
        free(key.instance);
        free(bucket_info.bucket.name);
        if (out) free(out);
        TEST_FAIL("attribute mismatch");
        return 1;
    }

    printf("\n    Object created with name='%s', attrs set", key.name);

    if (out) free(out);

    /* 清理 */
    free(key.name);
    free(key.instance);
    free(bucket_info.bucket.name);
    rgw_sal_rados_object_destroy(obj);
    fixture->passed++;
    TEST_PASS();
    return 0;
}

/* 测试复合操作 */
static int test_compound_operations(test_fixture_t* fixture) {
    TEST_START("compound_operations");

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("compound_user");
    uid.tenant = strdup("test_tenant");

    fixture->user = rgw_sal_rados_get_user(fixture->driver, &uid);
    if (!fixture->user) {
        free(uid.id);
        free(uid.tenant);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("compound_bucket");
    info.bucket.tenant = strdup("test_tenant");

    fixture->bucket = rgw_sal_rados_get_bucket(fixture->driver, &info);
    if (!fixture->bucket) {
        free(uid.id);
        free(uid.tenant);
        free(info.bucket.name);
        free(info.bucket.tenant);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("compound_object");

    rgw_sal_object_t* obj = rgw_sal_rados_get_object(fixture->driver, fixture->bucket, &key);
    if (!obj) {
        free(uid.id);
        free(uid.tenant);
        free(info.bucket.name);
        free(info.bucket.tenant);
        free(key.name);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 验证对象和桶的关联 */
    TEST_EXPECT_NOT_NULL(obj->bucket, "object should have bucket");

    printf("\n    Compound: user='%s', bucket='%s', object='%s'",
           uid.id, info.bucket.name, key.name);

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    free(info.bucket.name);
    free(info.bucket.tenant);
    free(key.name);
    rgw_sal_rados_object_destroy(obj);
    fixture->passed++;
    TEST_PASS();
    return 0;
}

/* 测试属性继承 */
static int test_attr_inheritance(test_fixture_t* fixture) {
    TEST_START("attr_inheritance");

    /* 创建桶 */
    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("attr_bucket");

    fixture->bucket = rgw_sal_rados_get_bucket(fixture->driver, &info);
    if (!fixture->bucket) {
        free(info.bucket.name);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("attr_object");

    rgw_sal_object_t* obj = rgw_sal_rados_get_object(fixture->driver, fixture->bucket, &key);
    if (!obj) {
        free(info.bucket.name);
        free(key.name);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 获取对象属性 */
    rgw_sal_attrs_t* obj_attrs = rgw_sal_rados_object_get_attrs(obj);
    TEST_EXPECT_NOT_NULL(obj_attrs, "object attrs should not be NULL");

    /* 验证桶有属性 */
    rgw_sal_attrs_t* bucket_attrs = rgw_sal_rados_bucket_get_attrs(fixture->bucket);
    TEST_EXPECT_NOT_NULL(bucket_attrs, "bucket attrs should not be NULL");

    printf("\n    Attr inheritance: bucket and object both have attrs");

    /* 清理 */
    free(info.bucket.name);
    free(key.name);
    rgw_sal_rados_object_destroy(obj);
    fixture->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("========================================\n");
    printf("RADOS 驱动集成测试\n");
    printf("========================================\n\n");

    int failed = 0;

    /* 创建测试夹具 */
    test_fixture_t* fixture = create_fixture();
    if (!fixture) {
        printf("FAIL: Failed to create test fixture\n");
        return 1;
    }

    printf("\n[Test Fixture Created]\n");

    /* 执行集成测试 */
    failed += test_user_lifecycle(fixture);
    failed += test_bucket_lifecycle(fixture);
    failed += test_object_lifecycle(fixture);
    failed += test_compound_operations(fixture);
    failed += test_attr_inheritance(fixture);

    printf("\n");
    printf("========================================\n");
    printf("集成测试结果汇总\n");
    printf("========================================\n");
    printf("  运行: %d\n", g_tests_run);
    printf("  通过: %d\n", g_tests_passed);
    printf("  失败: %d\n", g_tests_failed);
    printf("  跳过: %d\n", g_tests_skipped);
    printf("========================================\n\n");

    /* 清理 */
    destroy_fixture(fixture);

    return g_tests_failed > 0 ? 1 : 0;
}

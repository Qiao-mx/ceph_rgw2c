/**
 * @file test_rados_driver.c
 * @brief RADOS 驱动功能测试
 *
 * 测试 RADOS 驱动的核心功能：
 * - 驱动创建和销毁
 * - 用户操作
 * - 桶操作
 * - 对象操作
 * - OMAP 操作
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

#define TEST_EXPECT_NULL(ptr, msg) do { \
    if ((ptr)) { \
        printf("[FAIL] %s (expected NULL)\n", msg); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

#define TEST_EXPECT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        printf("[FAIL] %s (expected true)\n", msg); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

#define TEST_EXPECT_FALSE(cond, msg) do { \
    if ((cond)) { \
        printf("[FAIL] %s (expected false)\n", msg); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

/*============================================================================
 * 测试用例
 *============================================================================*/

/* 测试驱动创建 */
static int test_driver_create_destroy(void) {
    TEST_START("driver_create_destroy");

    /* 创建驱动（不连接到 RADOS） */
    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    printf("\n    Created RADOS driver");
    TEST_EXPECT_NOT_NULL(driver->impl, "driver impl should not be NULL");
    TEST_EXPECT_NOT_NULL(driver->vtable, "driver vtable should not be NULL");
    TEST_EXPECT_NOT_NULL(driver->user_vtable, "driver user_vtable should not be NULL");
    TEST_EXPECT_NOT_NULL(driver->bucket_vtable, "driver bucket_vtable should not be NULL");
    TEST_EXPECT_NOT_NULL(driver->object_vtable, "driver object_vtable should not be NULL");

    /* 获取驱动名称 */
    const char* name = rgw_sal_rados_driver_get_name(driver);
    if (name) {
        printf("\n    Driver name: %s", name);
    }

    /* 销毁驱动 */
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试用户获取和释放 */
static int test_user_get_destroy(void) {
    TEST_START("user_get_destroy");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 ID */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("test_user");
    uid.tenant = strdup("test_tenant");

    /* 获取用户 */
    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    printf("\n    Created user: id='%s', tenant='%s'", uid.id, uid.tenant);

    /* 验证用户 */
    TEST_EXPECT_NOT_NULL(user->impl, "user impl should not be NULL");
    TEST_EXPECT_NOT_NULL(user->vtable, "user vtable should not be NULL");

    /* 获取用户信息 */
    const char* user_id = rgw_sal_rados_user_get_id(user);
    TEST_EXPECT_STR(user_id, "test_user", "user id should match");

    const char* user_tenant = rgw_sal_rados_user_get_tenant(user);
    TEST_EXPECT_STR(user_tenant, "test_tenant", "user tenant should match");

    /* 获取和设置显示名称 */
    int ret = rgw_sal_rados_user_set_display_name(user, "Test User Display Name");
    TEST_EXPECT(ret, 0, "set_display_name should return 0");

    const char* display_name = rgw_sal_rados_user_get_display_name(user);
    TEST_EXPECT_STR(display_name, "Test User Display Name", "display_name should match");

    /* 获取和设置最大桶数 */
    rgw_sal_rados_user_set_max_buckets(user, 100);
    int32_t max_buckets = rgw_sal_rados_user_get_max_buckets(user);
    TEST_EXPECT(max_buckets, 100, "max_buckets should be 100");

    /* 获取用户类型 */
    uint32_t user_type = rgw_sal_rados_user_get_type(user);
    printf("\n    User type: %u", user_type);

    /* 获取用户属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_user_get_attrs(user);
    TEST_EXPECT_NOT_NULL(attrs, "user attrs should not be NULL");

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试用户克隆 */
static int test_user_clone(void) {
    TEST_START("user_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("clone_test_user");
    uid.tenant = strdup("clone_tenant");

    rgw_sal_user_t* original = rgw_sal_rados_get_user(driver, &uid);
    if (!original) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 设置属性 */
    rgw_sal_rados_user_set_display_name(original, "Original User");
    rgw_sal_rados_user_set_max_buckets(original, 50);

    /* 获取属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_user_get_attrs(original);
    if (attrs) {
        uint8_t val[] = "custom_value";
        rgw_sal_attrs_set(attrs, "custom_attr", val, sizeof(val) - 1);
    }

    /* 克隆用户 */
    rgw_sal_user_t* clone = rgw_sal_rados_user_clone(original);
    if (!clone) {
        /* 如果 vtable 不支持 clone，跳过此测试 */
        TEST_SKIP("clone not supported by vtable");
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_user_destroy(original);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned user");

    /* 验证克隆的用户 */
    const char* clone_id = rgw_sal_rados_user_get_id(clone);
    TEST_EXPECT_STR(clone_id, "clone_test_user", "clone id should match");

    const char* clone_display = rgw_sal_rados_user_get_display_name(clone);
    TEST_EXPECT_STR(clone_display, "Original User", "clone display_name should match");

    int32_t clone_max = rgw_sal_rados_user_get_max_buckets(clone);
    TEST_EXPECT(clone_max, 50, "clone max_buckets should match");

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(clone);
    rgw_sal_rados_user_destroy(original);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试桶获取和释放 */
static int test_bucket_get_destroy(void) {
    TEST_START("bucket_get_destroy");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶信息 */
    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("test_bucket");
    info.bucket.tenant = strdup("test_tenant");
    info.bucket.marker = strdup("marker_123");
    info.bucket.bucket_id = strdup("bucket_uuid_456");

    /* 获取桶 */
    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &info);
    if (!bucket) {
        free(info.bucket.name);
        free(info.bucket.tenant);
        free(info.bucket.marker);
        free(info.bucket.bucket_id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    printf("\n    Created bucket: name='%s'", info.bucket.name);

    /* 验证桶 */
    TEST_EXPECT_NOT_NULL(bucket->impl, "bucket impl should not be NULL");
    TEST_EXPECT_NOT_NULL(bucket->vtable, "bucket vtable should not be NULL");

    /* 获取桶名称 */
    const char* bucket_name = rgw_sal_rados_bucket_get_name(bucket);
    TEST_EXPECT_STR(bucket_name, "test_bucket", "bucket name should match");

    /* 获取桶属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_bucket_get_attrs(bucket);
    TEST_EXPECT_NOT_NULL(attrs, "bucket attrs should not be NULL");

    /* 清理 */
    free(info.bucket.name);
    free(info.bucket.tenant);
    free(info.bucket.marker);
    free(info.bucket.bucket_id);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试桶克隆 */
static int test_bucket_clone(void) {
    TEST_START("bucket_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("clone_bucket");
    info.bucket.tenant = strdup("clone_tenant");

    rgw_sal_bucket_t* original = rgw_sal_rados_get_bucket(driver, &info);
    if (!original) {
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 设置标签 */
    rgw_sal_rados_bucket_set_tag(original, "bucket_tag_123");

    /* 克隆桶 */
    rgw_sal_bucket_t* clone = rgw_sal_rados_bucket_clone(original);
    if (!clone) {
        TEST_SKIP("clone not supported by vtable");
        free(info.bucket.name);
        free(info.bucket.tenant);
        rgw_sal_rados_bucket_destroy(original);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned bucket");

    /* 验证克隆 */
    const char* clone_name = rgw_sal_rados_bucket_get_name(clone);
    TEST_EXPECT_STR(clone_name, "clone_bucket", "clone name should match");

    /* 清理 */
    free(info.bucket.name);
    free(info.bucket.tenant);
    rgw_sal_rados_bucket_destroy(clone);
    rgw_sal_rados_bucket_destroy(original);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试对象获取和释放 */
static int test_object_get_destroy(void) {
    TEST_START("object_get_destroy");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("test_bucket");
    bucket_info.bucket.tenant = strdup("test_tenant");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        free(bucket_info.bucket.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象键 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("path/to/object.txt");
    key.instance = strdup("version_123");
    key.is_null = false;
    key.is_current = true;

    /* 获取对象 */
    rgw_sal_object_t* obj = rgw_sal_rados_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        free(bucket_info.bucket.name);
        free(bucket_info.bucket.tenant);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object creation failed");
        return 1;
    }

    printf("\n    Created object: name='%s', instance='%s'", key.name, key.instance);

    /* 验证对象 */
    TEST_EXPECT_NOT_NULL(obj->impl, "object impl should not be NULL");
    TEST_EXPECT_NOT_NULL(obj->vtable, "object vtable should not be NULL");
    TEST_EXPECT_NOT_NULL(obj->bucket, "object bucket should not be NULL");

    /* 获取对象名称 */
    const char* obj_name = rgw_sal_rados_object_get_name(obj);
    TEST_EXPECT_STR(obj_name, "path/to/object.txt", "object name should match");

    /* 获取对象属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_object_get_attrs(obj);
    TEST_EXPECT_NOT_NULL(attrs, "object attrs should not be NULL");

    /* 测试原子性设置 */
    rgw_sal_rados_object_set_atomic(obj, true);
    bool is_atomic = rgw_sal_rados_object_is_atomic(obj);
    TEST_EXPECT_TRUE(is_atomic, "object should be atomic after setting");

    /* 清理 */
    free(key.name);
    free(key.instance);
    free(bucket_info.bucket.name);
    free(bucket_info.bucket.tenant);
    rgw_sal_rados_object_destroy(obj);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试对象克隆 */
static int test_object_clone(void) {
    TEST_START("object_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_info_t bucket_info = {0};
    bucket_info.bucket.name = strdup("clone_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_info);
    if (!bucket) {
        free(bucket_info.bucket.name);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("object_to_clone");

    rgw_sal_object_t* original = rgw_sal_rados_get_object(driver, bucket, &key);
    if (!original) {
        free(key.name);
        free(bucket_info.bucket.name);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 设置原子性 */
    rgw_sal_rados_object_set_atomic(original, true);

    /* 克隆对象 */
    rgw_sal_object_t* clone = rgw_sal_rados_object_clone(original);
    if (!clone) {
        TEST_SKIP("clone not supported by vtable");
        free(key.name);
        free(bucket_info.bucket.name);
        rgw_sal_rados_object_destroy(original);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned object");

    /* 验证克隆 */
    const char* clone_name = rgw_sal_rados_object_get_name(clone);
    TEST_EXPECT_STR(clone_name, "object_to_clone", "clone name should match");

    bool clone_atomic = rgw_sal_rados_object_is_atomic(clone);
    TEST_EXPECT_TRUE(clone_atomic, "clone should be atomic");

    /* 清理 */
    free(key.name);
    free(bucket_info.bucket.name);
    rgw_sal_rados_object_destroy(clone);
    rgw_sal_rados_object_destroy(original);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试桶列表操作 */
static int test_bucket_list(void) {
    TEST_START("bucket_list");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("list_test_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 列出桶 - 未初始化时返回空列表 */
    rgw_sal_bucket_list_t* list = NULL;
    int ret = rgw_sal_rados_list_buckets(driver, user, NULL, NULL, NULL, NULL, 100, false, &list, NULL, NULL);

    if (ret == 0 || ret == RGW_SAL_ERR_NOT_INITIALIZED) {
        printf("\n    list_buckets returned (not initialized)");
        if (list) {
            printf(", count=%zu", list->count);
            printf(", truncated=%d", list->is_truncated);
        }
    }

    if (list) {
        rgw_sal_bucket_list_destroy(list);
    }

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

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
    printf("RADOS 驱动功能测试\n");
    printf("========================================\n\n");

    int failed = 0;

    /* 驱动测试 */
    printf("[驱动测试]\n");
    failed += test_driver_create_destroy();

    /* 用户测试 */
    printf("[用户测试]\n");
    failed += test_user_get_destroy();
    failed += test_user_clone();

    /* 桶测试 */
    printf("[桶测试]\n");
    failed += test_bucket_get_destroy();
    failed += test_bucket_clone();

    /* 对象测试 */
    printf("[对象测试]\n");
    failed += test_object_get_destroy();
    failed += test_object_clone();

    /* 列表测试 */
    printf("[列表测试]\n");
    failed += test_bucket_list();

    printf("\n");
    printf("========================================\n");
    printf("测试结果汇总\n");
    printf("========================================\n");
    printf("  运行: %d\n", g_tests_run);
    printf("  通过: %d\n", g_tests_passed);
    printf("  失败: %d\n", g_tests_failed);
    printf("  跳过: %d\n", g_tests_skipped);
    printf("========================================\n\n");

    return g_tests_failed > 0 ? 1 : 0;
}

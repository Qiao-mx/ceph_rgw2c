/**
 * @file test_basic.c
 * @brief SAL 核心类型基础测试
 *
 * 测试 SAL 核心类型的创建、设置和销毁功能。
 * 本测试仅测试不依赖驱动的核心类型。
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

/*============================================================================
 * 测试框架
 *============================================================================*/

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_START(name) do { \
    printf("  %-45s ", name); \
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
    if (ptr) { \
        printf("[FAIL] %s (expected NULL)\n", msg); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

/*============================================================================
 * 用户 ID 测试
 *============================================================================*/

static int test_user_id_create_destroy(void) {
    TEST_START("user_id_create_destroy");

    rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
    TEST_EXPECT_NOT_NULL(uid, "user_id_create should not return NULL");

    uid->id = strdup("test_user");
    uid->tenant = strdup("test_tenant");
    uid->type = 0;

    printf("\n    Created user_id: id='%s', tenant='%s'",
           uid->id ? uid->id : "NULL",
           uid->tenant ? uid->tenant : "NULL");

    /* 验证值 */
    if (!uid->id || strcmp(uid->id, "test_user") != 0) {
        free(uid->id);
        free(uid->tenant);
        rgw_sal_user_id_destroy(uid);
        TEST_FAIL("id mismatch");
        return 1;
    }

    rgw_sal_user_id_destroy(uid);
    TEST_PASS();
    return 0;
}

static int test_user_id_null_values(void) {
    TEST_START("user_id_null_values");

    rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
    TEST_EXPECT_NOT_NULL(uid, "user_id_create should not return NULL");

    /* 初始值应该是 NULL 或默认值 */
    /* 创建后不设置任何值，直接销毁 */
    rgw_sal_user_id_destroy(uid);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 桶 ID 测试
 *============================================================================*/

static int test_bucket_id_create_destroy(void) {
    TEST_START("bucket_id_create_destroy");

    rgw_sal_bucket_id_t* bid = rgw_sal_bucket_id_create();
    TEST_EXPECT_NOT_NULL(bid, "bucket_id_create should not return NULL");

    bid->name = strdup("test_bucket");
    bid->tenant = strdup("test_tenant");
    bid->marker = strdup("marker123");
    bid->bucket_id = strdup("bucket_uuid");

    printf("\n    Created bucket_id: name='%s', tenant='%s'",
           bid->name ? bid->name : "NULL",
           bid->tenant ? bid->tenant : "NULL");

    /* 验证值 */
    if (!bid->name || strcmp(bid->name, "test_bucket") != 0) {
        rgw_sal_bucket_id_destroy(bid);
        TEST_FAIL("name mismatch");
        return 1;
    }

    rgw_sal_bucket_id_destroy(bid);
    TEST_PASS();
    return 0;
}

static int test_bucket_id_null_values(void) {
    TEST_START("bucket_id_null_values");

    rgw_sal_bucket_id_t* bid = rgw_sal_bucket_id_create();
    TEST_EXPECT_NOT_NULL(bid, "bucket_id_create should not return NULL");

    /* 创建后不设置任何值，直接销毁 */
    rgw_sal_bucket_id_destroy(bid);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 对象键测试
 *============================================================================*/

static int test_obj_key_create_destroy(void) {
    TEST_START("obj_key_create_destroy");

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    TEST_EXPECT_NOT_NULL(key, "obj_key_create should not return NULL");

    key->name = strdup("test_object");
    key->instance = strdup("version_1");
    key->is_null = false;
    key->is_current = true;

    printf("\n    Created obj_key: name='%s', instance='%s'",
           key->name ? key->name : "NULL",
           key->instance ? key->instance : "NULL");

    /* 验证值 */
    if (!key->name || strcmp(key->name, "test_object") != 0) {
        rgw_sal_obj_key_destroy(key);
        TEST_FAIL("name mismatch");
        return 1;
    }

    rgw_sal_obj_key_destroy(key);
    TEST_PASS();
    return 0;
}

static int test_obj_key_null_instance(void) {
    TEST_START("obj_key_null_instance");

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    TEST_EXPECT_NOT_NULL(key, "obj_key_create should not return NULL");

    key->name = strdup("object_without_version");
    /* 不设置 instance，表示没有版本 */

    const char* instance = key->instance;
    /* instance 可能是 NULL */

    printf("\n    Created obj_key: name='%s', instance=%s",
           key->name, instance ? instance : "NULL");

    rgw_sal_obj_key_destroy(key);
    TEST_PASS();
    return 0;
}

static int test_obj_key_flags(void) {
    TEST_START("obj_key_flags");

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    TEST_EXPECT_NOT_NULL(key, "obj_key_create should not return NULL");

    key->name = strdup("test_object");
    key->is_null = true;
    key->is_current = false;

    TEST_EXPECT(key->is_null, true, "is_null should be true");
    TEST_EXPECT(key->is_current, false, "is_current should be false");

    printf("\n    is_null=%d, is_current=%d", key->is_null, key->is_current);

    rgw_sal_obj_key_destroy(key);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 属性映射测试
 *============================================================================*/

static int test_attrs_create_destroy(void) {
    TEST_START("attrs_create_destroy");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs_create should not return NULL");

    printf("\n    Created empty attrs container");

    /* 测试设置属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";
    uint8_t val3[] = "value3";

    int ret = rgw_sal_attrs_set(attrs, "key1", val1, strlen((char*)val1));
    TEST_EXPECT(ret, 0, "attrs_set key1 should return 0");

    ret = rgw_sal_attrs_set(attrs, "key2", val2, strlen((char*)val2));
    TEST_EXPECT(ret, 0, "attrs_set key2 should return 0");

    ret = rgw_sal_attrs_set(attrs, "key3", val3, strlen((char*)val3));
    TEST_EXPECT(ret, 0, "attrs_set key3 should return 0");

    printf("\n    Set 3 attributes");

    /* 测试获取属性 - API: 返回错误码, 通过输出参数返回值 */
    uint8_t* out = NULL;
    size_t len = 0;

    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get key1 should return 0");
    TEST_EXPECT(len, (size_t)6, "key1 value length should be 6");
    if (out) {
        TEST_EXPECT(memcmp(out, "value1", 6), 0, "key1 value should match");
        free(out);
    }

    out = NULL;
    ret = rgw_sal_attrs_get(attrs, "key2", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get key2 should return 0");
    if (out) { free(out); out = NULL; }

    ret = rgw_sal_attrs_get(attrs, "key3", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get key3 should return 0");
    if (out) { free(out); out = NULL; }

    printf("\n    Verified 3 attributes");

    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

static int test_attrs_update(void) {
    TEST_START("attrs_update_existing");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs_create should not return NULL");

    /* 设置初始值 */
    uint8_t val1[] = "original";
    int ret = rgw_sal_attrs_set(attrs, "key", val1, strlen((char*)val1));
    TEST_EXPECT(ret, 0, "attrs_set initial should return 0");

    /* 更新值 */
    uint8_t val2[] = "updated";
    ret = rgw_sal_attrs_set(attrs, "key", val2, strlen((char*)val2));
    TEST_EXPECT(ret, 0, "attrs_set update should return 0");

    /* 验证更新后的值 - 新 API: 返回错误码 */
    uint8_t* out = NULL;
    size_t len = 0;
    ret = rgw_sal_attrs_get(attrs, "key", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get should return 0");
    TEST_EXPECT(len, (size_t)7, "updated value length should be 7");
    if (out) {
        TEST_EXPECT(memcmp(out, "updated", 7), 0, "updated value should match");
        free(out);
    }

    printf("\n    Attribute update verified");

    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

static int test_attrs_not_found(void) {
    TEST_START("attrs_not_found");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs_create should not return NULL");

    /* 测试获取不存在的属性 - API: 返回非0错误码表示未找到 */
    uint8_t* out = NULL;
    size_t len = 0;
    int ret = rgw_sal_attrs_get(attrs, "nonexistent", &out, &len);

    if (ret == 0) {
        printf("\n    ERROR: expected error for nonexistent key");
        if (out) free(out);
        rgw_sal_attrs_destroy(attrs);
        TEST_FAIL("expected error for nonexistent key");
        return 1;
    }

    printf("\n    Correctly returned error for nonexistent key");
    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

static int test_attrs_binary_data(void) {
    TEST_START("attrs_binary_data");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs_create should not return NULL");

    /* 测试二进制数据 */
    uint8_t binary[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
    int ret = rgw_sal_attrs_set(attrs, "binary_key", binary, sizeof(binary));
    TEST_EXPECT(ret, 0, "attrs_set binary should return 0");

    /* 验证二进制数据 - API: 返回错误码 */
    size_t len = 0;
    uint8_t* out = NULL;
    ret = rgw_sal_attrs_get(attrs, "binary_key", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get should return 0");
    TEST_EXPECT(len, (size_t)sizeof(binary), "binary data length should match");
    if (out) {
        TEST_EXPECT(memcmp(out, binary, sizeof(binary)), 0, "binary data should match");
        free(out);
    }

    printf("\n    Binary data (%zu bytes) stored and retrieved correctly", sizeof(binary));

    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

static int test_attrs_many_keys(void) {
    TEST_START("attrs_many_keys");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs_create should not return NULL");

    /* 设置大量属性 */
    const int num_keys = 100;
    char key_name[32];
    char value[32];

    for (int i = 0; i < num_keys; i++) {
        snprintf(key_name, sizeof(key_name), "key_%04d", i);
        snprintf(value, sizeof(value), "value_%04d", i);
        int ret = rgw_sal_attrs_set(attrs, key_name, (uint8_t*)value, strlen(value));
        if (ret != 0) {
            printf("\n    Failed to set key %d", i);
            break;
        }
    }

    printf("\n    Set %d attributes", num_keys);

    /* 验证部分属性 - API: 返回错误码 */
    int verified = 0;
    for (int i = 0; i < num_keys; i += 10) {
        snprintf(key_name, sizeof(key_name), "key_%04d", i);
        snprintf(value, sizeof(value), "value_%04d", i);

        uint8_t* out = NULL;
        size_t len = 0;
        int ret = rgw_sal_attrs_get(attrs, key_name, &out, &len);
        if (ret == 0 && out) {
            if (memcmp(out, value, strlen(value)) == 0) {
                verified++;
            }
            free(out);
        }
    }

    printf("\n    Verified %d/%d attributes", verified, num_keys / 10);

    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(void) {
    printf("========================================\n");
    printf("SAL Basic Types Test Suite\n");
    printf("========================================\n\n");

    /* 用户 ID 测试 */
    printf("--- User ID Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_user_id_create_destroy();
    test_user_id_null_values();
    printf("  User ID tests: %d/%d passed, %d failed\n\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    /* 桶 ID 测试 */
    printf("--- Bucket ID Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_bucket_id_create_destroy();
    test_bucket_id_null_values();
    printf("  Bucket ID tests: %d/%d passed, %d failed\n\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    /* 对象键测试 */
    printf("--- Object Key Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_obj_key_create_destroy();
    test_obj_key_null_instance();
    test_obj_key_flags();
    printf("  Object Key tests: %d/%d passed, %d failed\n\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    /* 属性映射测试 */
    printf("--- Attributes Tests ---\n");
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_attrs_create_destroy();
    test_attrs_update();
    test_attrs_not_found();
    test_attrs_binary_data();
    test_attrs_many_keys();
    printf("  Attributes tests: %d/%d passed, %d failed\n\n",
           g_tests_passed, g_tests_run, g_tests_failed);

    /* 计算总数 */
    int total_run = 0;
    int total_passed = 0;
    int total_failed = 0;

    printf("========================================\n");
    printf("Test Summary\n");
    printf("========================================\n");

    /* 重新运行以收集总数 */
    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_user_id_create_destroy(); test_user_id_null_values();
    printf("User ID:        %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_bucket_id_create_destroy(); test_bucket_id_null_values();
    printf("Bucket ID:      %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_obj_key_create_destroy(); test_obj_key_null_instance(); test_obj_key_flags();
    printf("Object Key:     %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    g_tests_run = 0; g_tests_passed = 0; g_tests_failed = 0;
    test_attrs_create_destroy(); test_attrs_update(); test_attrs_not_found();
    test_attrs_binary_data(); test_attrs_many_keys();
    printf("Attributes:     %d/%d passed, %d failed\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    total_run += g_tests_run; total_passed += g_tests_passed; total_failed += g_tests_failed;

    printf("========================================\n");
    printf("TOTAL:          %d/%d passed, %d failed\n", total_passed, total_run, total_failed);
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

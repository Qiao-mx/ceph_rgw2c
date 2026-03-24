/**
 * @file test_basic.c
 * @brief SAL 核心类型基础测试
 *
 * 测试 SAL 核心类型：
 * - 用户 ID 结构
 * - 桶 ID 结构
 * - 对象键结构
 * - 配额信息结构
 * - 属性映射
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
    fflush(stdout); \
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

#define TEST_EXPECT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        printf("[FAIL] %s (expected %llu, got %llu)\n", msg, \
               (unsigned long long)(expected), (unsigned long long)(actual)); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

/*============================================================================
 * 测试用例
 *============================================================================*/

/* 测试用户 ID 创建销毁 */
static int test_user_id_create_destroy(void) {
    TEST_START("user_id_create_destroy");

    rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
    TEST_EXPECT_NOT_NULL(uid, "user_id_create should not return NULL");

    uid->id = strdup("test_user");
    uid->tenant = strdup("test_tenant");

    printf("\n    Created user_id: id='%s', tenant='%s'",
           uid->id ? uid->id : "NULL",
           uid->tenant ? uid->tenant : "NULL");

    /* 验证值 */
    if (!uid->id || strcmp(uid->id, "test_user") != 0) {
        rgw_sal_user_id_destroy(uid);
        TEST_FAIL("id mismatch");
        return 1;
    }

    if (!uid->tenant || strcmp(uid->tenant, "test_tenant") != 0) {
        rgw_sal_user_id_destroy(uid);
        TEST_FAIL("tenant mismatch");
        return 1;
    }

    rgw_sal_user_id_destroy(uid);

    TEST_PASS();
    return 0;
}

/* 测试桶 ID 创建销毁 */
static int test_bucket_id_create_destroy(void) {
    TEST_START("bucket_id_create_destroy");

    rgw_sal_bucket_id_t* bid = rgw_sal_bucket_id_create();
    TEST_EXPECT_NOT_NULL(bid, "bucket_id_create should not return NULL");

    bid->name = strdup("test_bucket");
    bid->tenant = strdup("test_tenant");
    bid->marker = strdup("marker_123");
    bid->bucket_id = strdup("bucket_uuid_456");

    printf("\n    Created bucket_id: name='%s', marker='%s'",
           bid->name ? bid->name : "NULL",
           bid->marker ? bid->marker : "NULL");

    /* 验证值 */
    TEST_EXPECT_STR(bid->name, "test_bucket", "bucket name should match");
    TEST_EXPECT_STR(bid->marker, "marker_123", "marker should match");

    rgw_sal_bucket_id_destroy(bid);

    TEST_PASS();
    return 0;
}

/* 测试对象键创建销毁 */
static int test_obj_key_create_destroy(void) {
    TEST_START("obj_key_create_destroy");

    rgw_sal_obj_key_t* key = rgw_sal_obj_key_create();
    TEST_EXPECT_NOT_NULL(key, "obj_key_create should not return NULL");

    key->name = strdup("path/to/object.txt");
    key->instance = strdup("version_123");
    key->is_null = false;
    key->is_current = true;

    printf("\n    Created obj_key: name='%s', instance='%s'",
           key->name ? key->name : "NULL",
           key->instance ? key->instance : "NULL");

    /* 验证值 */
    TEST_EXPECT_STR(key->name, "path/to/object.txt", "object name should match");
    TEST_EXPECT_STR(key->instance, "version_123", "instance should match");
    TEST_EXPECT_FALSE(key->is_null, "is_null should be false");
    TEST_EXPECT_TRUE(key->is_current, "is_current should be true");

    rgw_sal_obj_key_destroy(key);

    TEST_PASS();
    return 0;
}

/* 测试配额信息 */
static int test_quota_info(void) {
    TEST_START("quota_info");

    rgw_sal_quota_info_t quota = {0};

    /* 测试默认值 */
    TEST_EXPECT_FALSE(quota.enabled, "enabled should be false by default");
    TEST_EXPECT_FALSE(quota.check_on_raw, "check_on_raw should be false by default");
    TEST_EXPECT_EQ(quota.max_size, 0ULL, "max_size should be 0");
    TEST_EXPECT_EQ(quota.max_objects, 0ULL, "max_objects should be 0");
    TEST_EXPECT_EQ(quota.quota_bytes, 0ULL, "quota_bytes should be 0");
    TEST_EXPECT_EQ(quota.quota_max_objects, 0ULL, "quota_max_objects should be 0");

    /* 设置配额值 */
    quota.enabled = true;
    quota.max_size = 1024 * 1024 * 1024;  /* 1GB */
    quota.max_objects = 10000;

    TEST_EXPECT_TRUE(quota.enabled, "enabled should be true after setting");
    TEST_EXPECT_EQ(quota.max_size, 1073741824ULL, "max_size should be 1GB");

    TEST_PASS();
    return 0;
}

/* 测试属性映射创建销毁 */
static int test_attrs_create_destroy(void) {
    TEST_START("attrs_create_destroy");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs should not be NULL");

    printf("\n    Created empty attrs container");

    /* 设置多个属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";
    uint8_t val3[] = "long_value_for_testing";

    int ret = rgw_sal_attrs_set(attrs, "key1", val1, strlen((char*)val1));
    TEST_EXPECT(ret, 0, "attrs_set key1 should return 0");

    ret = rgw_sal_attrs_set(attrs, "key2", val2, strlen((char*)val2));
    TEST_EXPECT(ret, 0, "attrs_set key2 should return 0");

    ret = rgw_sal_attrs_set(attrs, "key3", val3, strlen((char*)val3));
    TEST_EXPECT(ret, 0, "attrs_set key3 should return 0");

    printf("\n    Set 3 attributes");

    /* 验证所有属性 */
    uint8_t* out = NULL;
    size_t len = 0;

    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get key1 should return 0");
    TEST_EXPECT(len, (size_t)6, "key1 value length should be 6");
    if (out) {
        TEST_EXPECT(memcmp(out, "value1", 6), 0, "key1 value should match");
        free(out);
        out = NULL;
    }

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

/* 测试属性映射更新和删除 */
static int test_attrs_update_delete(void) {
    TEST_START("attrs_update_delete");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs should not be NULL");

    /* 设置属性 */
    uint8_t val[] = "initial";
    int ret = rgw_sal_attrs_set(attrs, "key1", val, strlen((char*)val));
    TEST_EXPECT(ret, 0, "attrs_set should return 0");

    /* 更新属性 */
    uint8_t new_val[] = "updated";
    ret = rgw_sal_attrs_set(attrs, "key1", new_val, strlen((char*)new_val));
    TEST_EXPECT(ret, 0, "attrs_set update should return 0");

    /* 验证更新后的值 */
    uint8_t* out = NULL;
    size_t len = 0;
    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    TEST_EXPECT(ret, 0, "attrs_get should return 0");
    TEST_EXPECT(memcmp(out, "updated", 7), 0, "value should be updated");
    if (out) { free(out); out = NULL; }

    /* 删除属性 */
    ret = rgw_sal_attrs_del(attrs, "key1");
    TEST_EXPECT(ret, 0, "attrs_del should return 0");

    /* 验证删除 */
    ret = rgw_sal_attrs_get(attrs, "key1", &out, &len);
    TEST_EXPECT(ret, -ENOENT, "attrs_get after delete should return -ENOENT");

    rgw_sal_attrs_destroy(attrs);
    TEST_PASS();
    return 0;
}

/* 测试属性映射克隆 */
static int test_attrs_clone(void) {
    TEST_START("attrs_clone");

    rgw_sal_attrs_t* attrs = rgw_sal_attrs_create();
    TEST_EXPECT_NOT_NULL(attrs, "attrs should not be NULL");

    /* 设置属性 */
    uint8_t val1[] = "value1";
    uint8_t val2[] = "value2";
    rgw_sal_attrs_set(attrs, "key1", val1, strlen((char*)val1));
    rgw_sal_attrs_set(attrs, "key2", val2, strlen((char*)val2));

    /* 克隆属性 */
    rgw_sal_attrs_t* clone = rgw_sal_attrs_clone(attrs);
    TEST_EXPECT_NOT_NULL(clone, "attrs_clone should not return NULL");

    /* 验证克隆的属性 */
    uint8_t* out = NULL;
    size_t len = 0;
    int ret = rgw_sal_attrs_get(clone, "key1", &out, &len);
    TEST_EXPECT(ret, 0, "cloned attrs_get should return 0");
    if (out) {
        TEST_EXPECT(memcmp(out, "value1", 6), 0, "cloned value should match");
        free(out);
    }

    /* 修改原始不影响克隆 */
    uint8_t new_val[] = "modified";
    rgw_sal_attrs_set(attrs, "key1", new_val, strlen((char*)new_val));

    out = NULL;
    ret = rgw_sal_attrs_get(clone, "key1", &out, &len);
    TEST_EXPECT(ret, 0, "clone should be independent");
    if (out) {
        TEST_EXPECT(memcmp(out, "value1", 6), 0, "clone should not be affected by original change");
        free(out);
    }

    /* 清理 */
    rgw_sal_attrs_destroy(clone);
    rgw_sal_attrs_destroy(attrs);

    TEST_PASS();
    return 0;
}

/* 测试使用统计信息 */
static int test_usage_info(void) {
    TEST_START("usage_info");

    rgw_sal_usage_info_t usage = {0};

    /* 测试默认值 */
    TEST_EXPECT_EQ(usage.bytes_sent, 0ULL, "bytes_sent should be 0");
    TEST_EXPECT_EQ(usage.bytes_received, 0ULL, "bytes_received should be 0");
    TEST_EXPECT_EQ(usage.ops, 0ULL, "ops should be 0");
    TEST_EXPECT_EQ(usage.successful_ops, 0ULL, "successful_ops should be 0");

    /* 设置值 */
    usage.bytes_sent = 1024 * 1024;
    usage.bytes_received = 512 * 1024;
    usage.ops = 100;
    usage.successful_ops = 95;

    TEST_EXPECT_EQ(usage.bytes_sent, 1048576ULL, "bytes_sent should be 1MB");
    TEST_EXPECT_EQ(usage.ops, 100ULL, "ops should be 100");
    TEST_EXPECT_EQ(usage.successful_ops, 95ULL, "successful_ops should be 95");

    TEST_PASS();
    return 0;
}

/* 测试用户组创建 */
static int test_user_groups_create(void) {
    TEST_START("user_groups_create");

    rgw_sal_user_groups_t* groups = rgw_sal_user_groups_create();
    TEST_EXPECT_NOT_NULL(groups, "user_groups_create should not return NULL");
    TEST_EXPECT_EQ(groups->count, (size_t)0, "groups count should be 0");
    TEST_EXPECT_NULL(groups->groups, "groups array should be NULL initially");

    /* 添加组 */
    int ret = rgw_sal_user_groups_add(groups, "group1", "Group One");
    TEST_EXPECT(ret, 0, "user_groups_add should return 0");
    TEST_EXPECT_EQ(groups->count, (size_t)1, "groups count should be 1");

    ret = rgw_sal_user_groups_add(groups, "group2", "Group Two");
    TEST_EXPECT(ret, 0, "user_groups_add should return 0");
    TEST_EXPECT_EQ(groups->count, (size_t)2, "groups count should be 2");

    /* 清理 */
    rgw_sal_user_groups_destroy(groups);

    TEST_PASS();
    return 0;
}

/* 测试 TOTP 验证函数存在性 */
static int test_totp_verify_exists(void) {
    TEST_START("totp_verify_exists");

    /* TOTP 验证函数存在，不验证实际功能（需要密钥） */
    bool result = rgw_sal_verify_totp("secret_key", "123456", 1234567890);
    printf("\n    TOTP function exists and can be called");

    /* 记录测试通过 */
    (void)result;
    TEST_PASS();
    return 0;
}

/* 测试桶列表创建销毁 */
static int test_bucket_list_create_destroy(void) {
    TEST_START("bucket_list_create_destroy");

    rgw_sal_bucket_list_t* list = rgw_sal_bucket_list_create();
    TEST_EXPECT_NOT_NULL(list, "bucket_list_create should not return NULL");

    /* 验证默认值 */
    TEST_EXPECT_EQ(list->count, (size_t)0, "count should be 0");
    TEST_EXPECT_TRUE(list->is_truncated, "is_truncated should be true initially");

    /* 清理 */
    rgw_sal_bucket_list_destroy(list);

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
    printf("SAL 核心类型基础测试\n");
    printf("========================================\n\n");

    int failed = 0;

    /* 用户 ID 测试 */
    printf("[用户 ID 测试]\n");
    failed += test_user_id_create_destroy();

    /* 桶 ID 测试 */
    printf("[桶 ID 测试]\n");
    failed += test_bucket_id_create_destroy();

    /* 对象键测试 */
    printf("[对象键测试]\n");
    failed += test_obj_key_create_destroy();

    /* 配额信息测试 */
    printf("[配额信息测试]\n");
    failed += test_quota_info();

    /* 属性映射测试 */
    printf("[属性映射测试]\n");
    failed += test_attrs_create_destroy();
    failed += test_attrs_update_delete();
    failed += test_attrs_clone();

    /* 使用统计测试 */
    printf("[使用统计测试]\n");
    failed += test_usage_info();

    /* 用户组测试 */
    printf("[用户组测试]\n");
    failed += test_user_groups_create();

    /* TOTP 测试 */
    printf("[TOTP 测试]\n");
    failed += test_totp_verify_exists();

    /* 桶列表测试 */
    printf("[桶列表测试]\n");
    failed += test_bucket_list_create_destroy();

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

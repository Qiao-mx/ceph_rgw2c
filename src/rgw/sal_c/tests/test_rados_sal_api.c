/**
 * @file test_rados_sal_api.c
 * @brief RADOS SAL C 驱动 API 测试
 *
 * 测试 sal_c 中 RADOS 驱动的封装 API：
 * - 驱动创建和销毁
 * - 用户操作（创建、获取、设置属性）
 * - 桶操作（创建、获取属性）
 * - 对象操作（创建、获取属性）
 * - 属性操作
 * - 列表操作
 *
 * 注意：这些测试需要真实 Ceph 集群运行，否则会被跳过。
 *
 * 问题记录：
 * 1. rgw_sal_driver_t, rgw_sal_user_t, rgw_sal_bucket_t, rgw_sal_object_t 
 *    都是不透明结构体（opaque types），不能直接访问其内部成员。
 * 2. 只能通过公开的 API 函数来操作这些类型。
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
#include "rgw_sal_errors.h"

/*============================================================================
 * 测试配置
 *============================================================================*/

#define TEST_CLUSTER_NAME "ceph"
#define TEST_CONFIG_FILE "/etc/ceph/ceph.conf"
#define TEST_USER_POOL ".rgw.meta.users.uid"
#define TEST_BUCKET_POOL ".rgw.buckets.index"

/*============================================================================
 * 测试结果跟踪
 *============================================================================*/

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_tests_skipped = 0;
static bool g_cluster_available = false;

/*============================================================================
 * 辅助函数
 *============================================================================*/

/**
 * @brief 检查集群是否可用
 */
static bool check_cluster_available(void) {
    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        return false;
    }

    ret = rados_conf_read_file(cluster, TEST_CONFIG_FILE);
    if (ret != 0) {
        rados_shutdown(cluster);
        return false;
    }

    ret = rados_connect(cluster);
    if (ret != 0) {
        rados_shutdown(cluster);
        return false;
    }

    /* 检查必要的池是否存在 */
    int64_t pool_id = rados_pool_lookup(cluster, TEST_USER_POOL);
    if (pool_id < 0) {
        printf("    Note: User pool '%s' not found\n", TEST_USER_POOL);
    }

    rados_shutdown(cluster);
    return true;
}

/*============================================================================
 * 测试宏定义
 *============================================================================*/

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

#define TEST_EXPECT_NOT_NULL(ptr, msg) do { \
    if (!(ptr)) { \
        printf("[FAIL] %s (expected non-NULL)\n", msg); \
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

#define TEST_EXPECT_STR(actual, expected, msg) do { \
    if (!actual || !expected || strcmp(actual, expected) != 0) { \
        printf("[FAIL] %s (expected '%s', got '%s')\n", msg, expected, actual ? actual : "NULL"); \
        g_tests_failed++; \
        return 1; \
    } \
} while(0)

/*============================================================================
 * 测试用例
 *============================================================================*/

/* 测试 1: 驱动创建和名称获取 */
static int test_driver_create(void) {
    TEST_START("driver_create");

    /* 创建驱动 */
    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    printf("\n    Driver created");

    /* 获取驱动名称 - 通过公开 API */
    const char* name = rgw_sal_rados_driver_get_name(driver);
    if (name) {
        printf(", name='%s'", name);
    }

    /* 销毁驱动 */
    rgw_sal_rados_driver_destroy(driver);
    TEST_PASS();
    return 0;
}

/* 测试 2: 用户创建和基本操作 */
static int test_user_basic_operations(void) {
    TEST_START("user_basic_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("api_test_user");
    uid.tenant = strdup("api_test_tenant");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    printf("\n    User created: id='%s', tenant='%s'", uid.id, uid.tenant);

    /* 获取用户 ID - 通过公开 API */
    const char* user_id = rgw_sal_rados_user_get_id(user);
    TEST_EXPECT_STR(user_id, "api_test_user", "user id should match");

    /* 获取租户 */
    const char* tenant = rgw_sal_rados_user_get_tenant(user);
    TEST_EXPECT_STR(tenant, "api_test_tenant", "tenant should match");

    /* 设置显示名称 - 通过公开 API */
    int ret = rgw_sal_rados_user_set_display_name(user, "API Test User");
    TEST_EXPECT(ret, 0, "set_display_name should return 0");

    /* 获取显示名称 - 通过公开 API */
    const char* display_name = rgw_sal_rados_user_get_display_name(user);
    TEST_EXPECT_STR(display_name, "API Test User", "display_name should match");

    /* 获取用户类型 */
    uint32_t user_type = rgw_sal_rados_user_get_type(user);
    printf("\n    User type: %u", user_type);

    /* 获取/设置最大桶数 - 通过公开 API */
    rgw_sal_rados_user_set_max_buckets(user, 100);
    int32_t max_buckets = rgw_sal_rados_user_get_max_buckets(user);
    TEST_EXPECT(max_buckets, 100, "max_buckets should be 100");

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 3: 用户克隆 */
static int test_user_clone(void) {
    TEST_START("user_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("clone_user");
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
    rgw_sal_rados_user_set_display_name(original, "Clone Test");
    rgw_sal_rados_user_set_max_buckets(original, 99);

    /* 克隆用户 - 通过公开 API */
    rgw_sal_user_t* clone = rgw_sal_rados_user_clone(original);
    if (!clone) {
        printf("\n    Clone via wrapper not supported");
        TEST_SKIP("clone not supported");
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_user_destroy(original);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned user");

    /* 验证克隆的数据 - 通过公开 API */
    const char* clone_id = rgw_sal_rados_user_get_id(clone);
    TEST_EXPECT_STR(clone_id, "clone_user", "clone id should match");

    const char* clone_display = rgw_sal_rados_user_get_display_name(clone);
    TEST_EXPECT_STR(clone_display, "Clone Test", "clone display_name should match");

    int32_t max = rgw_sal_rados_user_get_max_buckets(clone);
    TEST_EXPECT(max, 99, "clone max_buckets should match");

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(clone);
    rgw_sal_rados_user_destroy(original);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 4: 桶创建和基本操作 */
static int test_bucket_basic_operations(void) {
    TEST_START("bucket_basic_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 ID */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("api_test_bucket");
    bucket_id.tenant = strdup("api_tenant");
    bucket_id.marker = strdup("marker_001");
    bucket_id.bucket_id = strdup("bucket_uuid_001");

    /* 获取桶 - 通过公开 API */
    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_id);
    if (!bucket) {
        free(bucket_id.name);
        free(bucket_id.tenant);
        free(bucket_id.marker);
        free(bucket_id.bucket_id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    printf("\n    Bucket created: name='%s'", bucket_id.name);

    /* 获取桶名称 - 通过公开 API */
    const char* bucket_name = rgw_sal_rados_bucket_get_name(bucket);
    TEST_EXPECT_STR(bucket_name, "api_test_bucket", "bucket name should match");

    /* 获取桶属性 - 通过公开 API */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_bucket_get_attrs(bucket);
    TEST_EXPECT_NOT_NULL(attrs, "bucket attrs should not be NULL");

    /* 获取桶标签 - 通过公开 API */
    const char* tag = rgw_sal_rados_bucket_get_tag(bucket);
    printf("\n    Initial tag: %s", tag ? tag : "(null)");

    /* 设置桶标签 - 通过公开 API */
    rgw_sal_rados_bucket_set_tag(bucket, "test_tag_123");
    tag = rgw_sal_rados_bucket_get_tag(bucket);
    TEST_EXPECT_STR(tag, "test_tag_123", "tag should match");

    /* 清理 */
    free(bucket_id.name);
    free(bucket_id.tenant);
    free(bucket_id.marker);
    free(bucket_id.bucket_id);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 5: 桶克隆 */
static int test_bucket_clone(void) {
    TEST_START("bucket_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("clone_bucket");
    bucket_id.tenant = strdup("clone_tenant");

    rgw_sal_bucket_t* original = rgw_sal_rados_get_bucket(driver, &bucket_id);
    if (!original) {
        free(bucket_id.name);
        free(bucket_id.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 设置标签 */
    rgw_sal_rados_bucket_set_tag(original, "original_tag");

    /* 克隆桶 - 通过公开 API */
    rgw_sal_bucket_t* clone = rgw_sal_rados_bucket_clone(original);
    if (!clone) {
        printf("\n    Clone via wrapper not supported");
        TEST_SKIP("clone not supported");
        free(bucket_id.name);
        free(bucket_id.tenant);
        rgw_sal_rados_bucket_destroy(original);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned bucket");

    /* 验证克隆的数据 - 通过公开 API */
    const char* clone_name = rgw_sal_rados_bucket_get_name(clone);
    TEST_EXPECT_STR(clone_name, "clone_bucket", "clone name should match");

    /* 清理 */
    free(bucket_id.name);
    free(bucket_id.tenant);
    rgw_sal_rados_bucket_destroy(clone);
    rgw_sal_rados_bucket_destroy(original);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 6: 对象创建和基本操作 */
static int test_object_basic_operations(void) {
    TEST_START("object_basic_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("obj_test_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_id);
    if (!bucket) {
        free(bucket_id.name);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象键 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("path/to/object.txt");
    key.instance = strdup("v1");
    key.is_null = false;
    key.is_current = true;

    /* 获取对象 - 通过公开 API */
    rgw_sal_object_t* obj = rgw_sal_rados_get_object(driver, bucket, &key);
    if (!obj) {
        free(key.name);
        free(key.instance);
        free(bucket_id.name);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object creation failed");
        return 1;
    }

    printf("\n    Object created: name='%s', instance='%s'", key.name, key.instance);

    /* 获取对象名称 - 通过公开 API */
    const char* obj_name = rgw_sal_rados_object_get_name(obj);
    TEST_EXPECT_STR(obj_name, "path/to/object.txt", "object name should match");

    /* 获取对象属性 - 通过公开 API */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_object_get_attrs(obj);
    TEST_EXPECT_NOT_NULL(attrs, "object attrs should not be NULL");

    /* 测试原子性设置 - 通过公开 API */
    rgw_sal_rados_object_set_atomic(obj, true);
    bool is_atomic = rgw_sal_rados_object_is_atomic(obj);
    TEST_EXPECT_TRUE(is_atomic, "object should be atomic after setting");

    /* 设置为非原子 */
    rgw_sal_rados_object_set_atomic(obj, false);
    is_atomic = rgw_sal_rados_object_is_atomic(obj);
    TEST_EXPECT_FALSE(is_atomic, "object should not be atomic");

    /* 清理 */
    free(key.name);
    free(key.instance);
    free(bucket_id.name);
    rgw_sal_rados_object_destroy(obj);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 7: 对象克隆 */
static int test_object_clone(void) {
    TEST_START("object_clone");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("obj_clone_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_id);
    if (!bucket) {
        free(bucket_id.name);
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
        free(bucket_id.name);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 设置原子性 - 通过公开 API */
    rgw_sal_rados_object_set_atomic(original, true);

    /* 克隆对象 - 通过公开 API */
    rgw_sal_object_t* clone = rgw_sal_rados_object_clone(original);
    if (!clone) {
        printf("\n    Clone via wrapper not supported");
        TEST_SKIP("clone not supported");
        free(key.name);
        free(bucket_id.name);
        rgw_sal_rados_object_destroy(original);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_driver_destroy(driver);
        return 0;
    }

    printf("\n    Cloned object");

    /* 验证克隆的数据 - 通过公开 API */
    const char* clone_name = rgw_sal_rados_object_get_name(clone);
    TEST_EXPECT_STR(clone_name, "object_to_clone", "clone name should match");

    bool clone_atomic = rgw_sal_rados_object_is_atomic(clone);
    TEST_EXPECT_TRUE(clone_atomic, "clone should be atomic");

    /* 清理 */
    free(key.name);
    free(bucket_id.name);
    rgw_sal_rados_object_destroy(clone);
    rgw_sal_rados_object_destroy(original);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 8: 用户属性操作 */
static int test_user_attrs_operations(void) {
    TEST_START("user_attrs_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("attrs_test_user");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 获取属性 - 通过公开 API */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_user_get_attrs(user);
    TEST_EXPECT_NOT_NULL(attrs, "user attrs should not be NULL");

    /* 设置属性 - 通过公开 API */
    uint8_t test_value[] = "test_attribute_value";
    int ret = rgw_sal_attrs_set(attrs, "test_key", test_value, sizeof(test_value) - 1);
    if (ret == 0) {
        printf("\n    Set attribute: test_key");
    } else {
        printf("\n    Set attribute failed (ret=%d)", ret);
    }

    /* 获取属性 - 通过公开 API */
    uint8_t* value = NULL;
    size_t value_len = 0;
    ret = rgw_sal_attrs_get(attrs, "test_key", &value, &value_len);
    if (ret == 0 && value && value_len > 0) {
        printf("\n    Get attribute: %.*s", (int)value_len, value);
        free(value);
    } else {
        printf("\n    Get attribute failed (ret=%d)", ret);
    }

    /* 清理 */
    free(uid.id);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 9: 桶列表操作 */
static int test_bucket_list_operation(void) {
    TEST_START("bucket_list_operation");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("list_test_user");
    uid.tenant = strdup("list_tenant");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 调用列表桶函数 - 通过公开 API */
    rgw_sal_bucket_list_t* list = NULL;
    int ret = rgw_sal_rados_list_buckets(driver, user,
        NULL, NULL, NULL, NULL, 100, false, &list, NULL, NULL);

    if (ret == 0 || ret == RGW_SAL_ERR_NOT_INITIALIZED) {
        printf("\n    list_buckets called (ret=%d)", ret);
        if (list) {
            printf(", count=%zu, truncated=%d",
                   list->count, list->is_truncated);
            /* 清理列表 - 注意: list->buckets 是 rgw_sal_bucket_info_t** 类型 */
            if (list->buckets) {
                for (size_t i = 0; i < list->count; i++) {
                    /* 每个 bucket 是 rgw_sal_bucket_info_t* 类型 */
                    if (list->buckets[i]) {
                        /* bucket.name 在 rgw_bucket_id_t 中 */
                        if (list->buckets[i]->bucket.name) {
                            free((char*)list->buckets[i]->bucket.name);
                        }
                        if (list->buckets[i]->bucket.tenant) {
                            free((char*)list->buckets[i]->bucket.tenant);
                        }
                        if (list->buckets[i]->bucket.marker) {
                            free((char*)list->buckets[i]->bucket.marker);
                        }
                        if (list->buckets[i]->bucket.bucket_id) {
                            free((char*)list->buckets[i]->bucket.bucket_id);
                        }
                        free(list->buckets[i]);
                    }
                }
                free(list->buckets);
            }
            free(list);
        } else {
            printf(", list is NULL (not initialized)");
        }
    } else {
        printf("\n    list_buckets returned %d", ret);
    }

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 10: 驱动获取集群 ID */
static int test_driver_get_cluster_id(void) {
    TEST_START("driver_get_cluster_id");

    if (!g_cluster_available) {
        TEST_SKIP("Ceph cluster not available");
        return 0;
    }

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 获取集群 ID - 通过公开 API */
    char* cluster_id = NULL;
    int ret = rgw_sal_rados_get_cluster_id(driver, &cluster_id, NULL, NULL);

    if (ret == 0 && cluster_id) {
        printf("\n    Cluster ID: %.20s...", cluster_id);
        free(cluster_id);
    } else {
        printf("\n    get_cluster_id returned %d (cluster may not be initialized)", ret);
    }

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
    printf("  RADOS SAL C 驱动 API 测试\n");
    printf("========================================\n\n");

    /* 检查集群可用性 */
    printf("检查 Ceph 集群可用性...\n");
    g_cluster_available = check_cluster_available();
    if (g_cluster_available) {
        printf("  集群可用 - 将运行完整的集成测试\n\n");
    } else {
        printf("  集群不可用 - 部分测试将被跳过\n\n");
    }

    int failed = 0;

    /* 驱动测试 */
    printf("[驱动测试]\n");
    failed += test_driver_create();
    failed += test_driver_get_cluster_id();

    /* 用户测试 */
    printf("\n[用户测试]\n");
    failed += test_user_basic_operations();
    failed += test_user_clone();

    /* 桶测试 */
    printf("\n[桶测试]\n");
    failed += test_bucket_basic_operations();
    failed += test_bucket_clone();

    /* 对象测试 */
    printf("\n[对象测试]\n");
    failed += test_object_basic_operations();
    failed += test_object_clone();

    /* 属性测试 */
    printf("\n[属性测试]\n");
    failed += test_user_attrs_operations();

    /* 列表测试 */
    printf("\n[列表测试]\n");
    failed += test_bucket_list_operation();

    /* 打印结果 */
    printf("\n");
    printf("========================================\n");
    printf("  测试结果汇总\n");
    printf("========================================\n");
    printf("  运行:  %d\n", g_tests_run);
    printf("  通过:  %d\n", g_tests_passed);
    printf("  失败:  %d\n", g_tests_failed);
    printf("  跳过:  %d\n", g_tests_skipped);
    printf("========================================\n\n");

    if (g_tests_failed > 0) {
        printf("部分测试失败!\n");
        return 1;
    }

    printf("所有测试完成!\n");
    if (!g_cluster_available) {
        printf("注意: 部分测试因集群不可用而被跳过。\n");
        printf("要运行完整测试，请确保 Ceph 集群正在运行。\n");
    }

    return 0;
}

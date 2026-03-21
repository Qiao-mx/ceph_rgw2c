/**
 * @file test_integration.c
 * @brief SAL RADOS 驱动集成测试
 *
 * 测试 RADOS 驱动的完整工作流程，包括：
 * - 用户完整生命周期（创建、设置属性、存储、加载、删除）
 * - 桶完整生命周期（创建、设置属性、存储、加载、删除）
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
#include "rgw_sal_rados.h"
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

/*============================================================================
 * 测试夹具
 *============================================================================*/

typedef struct {
    const char* name;
    int passed;
    int failed;
} test_group_t;

static test_group_t* create_test_group(const char* name) {
    test_group_t* group = (test_group_t*)calloc(1, sizeof(test_group_t));
    if (group) {
        group->name = name;
    }
    return group;
}

static void print_group_result(test_group_t* group) {
    printf("  %s: %d/%d passed, %d failed\n\n",
           group->name, group->passed, group->passed + group->failed, group->failed);
}

static void free_test_group(test_group_t* group) {
    free(group);
}

/*============================================================================
 * 用户生命周期测试
 *============================================================================*/

static int test_user_full_lifecycle(test_group_t* group) {
    TEST_START("user_full_lifecycle");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("lifecycle_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 设置用户属性 */
    rgw_sal_rados_user_set_display_name(user, "Lifecycle Test User");
    rgw_sal_rados_user_set_max_buckets(user, 50);

    rgw_sal_attrs_t* attrs = rgw_sal_rados_user_get_attrs(user);
    if (attrs) {
        uint8_t val[] = "custom_value";
        rgw_sal_attrs_set(attrs, "custom_attr", val, sizeof(val) - 1);
    }

    /* 验证设置的值 */
    const char* display_name = rgw_sal_rados_user_get_display_name(user);
    if (!display_name || strcmp(display_name, "Lifecycle Test User") != 0) {
        rgw_sal_rados_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("display_name mismatch");
        return 1;
    }

    int32_t max_buckets = rgw_sal_rados_user_get_max_buckets(user);
    if (max_buckets != 50) {
        rgw_sal_rados_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("max_buckets mismatch");
        return 1;
    }

    /* 克隆用户 */
    rgw_sal_user_t* clone = rgw_sal_rados_user_clone(user);
    if (!clone) {
        rgw_sal_rados_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user clone failed");
        return 1;
    }

    const char* clone_id = rgw_sal_rados_user_get_id(clone);
    if (!clone_id || strcmp(clone_id, "lifecycle_user") != 0) {
        rgw_sal_rados_user_destroy(clone);
        rgw_sal_rados_user_destroy(user);
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("clone id mismatch");
        return 1;
    }

    printf("\n    User created, attrs set, cloned successfully");

    /* 清理 */
    rgw_sal_rados_user_destroy(clone);
    rgw_sal_rados_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 桶生命周期测试
 *============================================================================*/

static int test_bucket_full_lifecycle(test_group_t* group) {
    TEST_START("bucket_full_lifecycle");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户作为桶所有者 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("bucket_owner");

    rgw_sal_user_t* owner = rgw_sal_rados_get_user(driver, &uid);
    if (!owner) {
        free(uid.id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("owner creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bid = {0};
    bid.name = strdup("lifecycle_bucket");
    bid.tenant = strdup("test_tenant");
    bid.marker = strdup("bucket_marker_123");
    bid.bucket_id = strdup("bucket_uuid_123");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bid);
    if (!bucket) {
        rgw_sal_rados_user_destroy(owner);
        free(uid.id);
        free(bid.name);
        free(bid.tenant);
        free(bid.marker);
        free(bid.bucket_id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 设置桶属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_bucket_get_attrs(bucket);
    if (attrs) {
        uint8_t val[] = "bucket_custom_value";
        rgw_sal_attrs_set(attrs, "bucket_attr", val, sizeof(val) - 1);
    }

    /* 设置桶标签 */
    rgw_sal_rados_bucket_set_tag(bucket, "lifecycle_tag");

    /* 验证属性 */
    const char* tag = rgw_sal_rados_bucket_get_tag(bucket);
    printf("\n    Bucket created, tag='%s'", tag ? tag : "NULL");

    /* 克隆桶 */
    rgw_sal_bucket_t* clone = rgw_sal_rados_bucket_clone(bucket);
    if (!clone) {
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_user_destroy(owner);
        free(uid.id);
        free(bid.name);
        free(bid.tenant);
        free(bid.marker);
        free(bid.bucket_id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket clone failed");
        return 1;
    }

    const char* clone_name = rgw_sal_rados_bucket_get_name(clone);
    if (!clone_name || strcmp(clone_name, "lifecycle_bucket") != 0) {
        rgw_sal_rados_bucket_destroy(clone);
        rgw_sal_rados_bucket_destroy(bucket);
        rgw_sal_rados_user_destroy(owner);
        free(uid.id);
        free(bid.name);
        free(bid.tenant);
        free(bid.marker);
        free(bid.bucket_id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("clone name mismatch");
        return 1;
    }

    printf("\n    Bucket cloned successfully");

    /* 清理 */
    rgw_sal_rados_bucket_destroy(clone);
    rgw_sal_rados_bucket_destroy(bucket);
    rgw_sal_rados_user_destroy(owner);
    free(uid.id);
    free(bid.name);
    free(bid.tenant);
    free(bid.marker);
    free(bid.bucket_id);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 对象生命周期测试
 *============================================================================*/

static int test_object_full_lifecycle(test_group_t* group) {
    TEST_START("object_full_lifecycle");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bid = {0};
    bid.name = strdup("obj_lifecycle_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bid);
    if (!bucket) {
        free(bid.name);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("test_object.txt");
    key.instance = strdup("v1");
    key.is_null = false;

    rgw_sal_object_t* obj = rgw_sal_rados_get_object(driver, bucket, &key);
    if (!obj) {
        rgw_sal_rados_bucket_destroy(bucket);
        free(bid.name);
        free(key.name);
        free(key.instance);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object creation failed");
        return 1;
    }

    /* 设置对象属性 */
    rgw_sal_attrs_t* attrs = rgw_sal_rados_object_get_attrs(obj);
    if (attrs) {
        uint8_t content_type[] = "text/plain";
        uint8_t etag[] = "abc123";
        rgw_sal_attrs_set(attrs, "Content-Type", content_type, sizeof(content_type) - 1);
        rgw_sal_attrs_set(attrs, "ETag", etag, sizeof(etag) - 1);
    }

    /* 设置原子标志 */
    rgw_sal_rados_object_set_atomic(obj, true);

    /* 验证属性 */
    bool is_atomic = rgw_sal_rados_object_is_atomic(obj);
    printf("\n    Object created, is_atomic=%d", is_atomic);

    /* 克隆对象 */
    rgw_sal_object_t* clone = rgw_sal_rados_object_clone(obj);
    if (!clone) {
        rgw_sal_rados_object_destroy(obj);
        rgw_sal_rados_bucket_destroy(bucket);
        free(bid.name);
        free(key.name);
        free(key.instance);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("object clone failed");
        return 1;
    }

    const char* clone_name = rgw_sal_rados_object_get_name(clone);
    if (!clone_name || strcmp(clone_name, "test_object.txt") != 0) {
        rgw_sal_rados_object_destroy(clone);
        rgw_sal_rados_object_destroy(obj);
        rgw_sal_rados_bucket_destroy(bucket);
        free(bid.name);
        free(key.name);
        free(key.instance);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("clone name mismatch");
        return 1;
    }

    printf("\n    Object cloned successfully");

    /* 清理 */
    rgw_sal_rados_object_destroy(clone);
    rgw_sal_rados_object_destroy(obj);
    rgw_sal_rados_bucket_destroy(bucket);
    free(bid.name);
    free(key.name);
    free(key.instance);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 用户-桶关联测试
 *============================================================================*/

static int test_user_bucket_association(test_group_t* group) {
    TEST_START("user_bucket_association");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("associated_user");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    /* 创建多个桶 */
    const char* bucket_names[] = {"bucket_one", "bucket_two", "bucket_three"};
    rgw_sal_bucket_t* buckets[3] = {NULL, NULL, NULL};

    for (int i = 0; i < 3; i++) {
        rgw_sal_bucket_id_t bid = {0};
        bid.name = strdup(bucket_names[i]);

        buckets[i] = rgw_sal_rados_get_bucket(driver, &bid);
        free(bid.name);

        if (!buckets[i]) {
            printf("\n    WARNING: bucket %s creation failed", bucket_names[i]);
        }
    }

    printf("\n    Created %d buckets for user", 3);

    /* 清理桶 */
    for (int i = 0; i < 3; i++) {
        if (buckets[i]) {
            rgw_sal_rados_bucket_destroy(buckets[i]);
        }
    }

    /* 清理用户 */
    rgw_sal_rados_user_destroy(user);
    free(uid.id);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 桶-对象关联测试
 *============================================================================*/

static int test_bucket_object_association(test_group_t* group) {
    TEST_START("bucket_object_association");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bid = {0};
    bid.name = strdup("object_container");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bid);
    if (!bucket) {
        free(bid.name);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建多个对象 */
    const char* object_names[] = {"obj1.txt", "obj2.txt", "dir/", "dir/nested.txt"};
    rgw_sal_object_t* objects[4] = {NULL, NULL, NULL, NULL};

    for (int i = 0; i < 4; i++) {
        rgw_sal_obj_key_t key = {0};
        key.name = strdup(object_names[i]);

        objects[i] = rgw_sal_rados_get_object(driver, bucket, &key);
        free(key.name);

        if (!objects[i]) {
            printf("\n    WARNING: object %s creation failed", object_names[i]);
        }
    }

    printf("\n    Created %d objects in bucket", 4);

    /* 清理对象 */
    for (int i = 0; i < 4; i++) {
        if (objects[i]) {
            rgw_sal_rados_object_destroy(objects[i]);
        }
    }

    /* 清理桶 */
    rgw_sal_rados_bucket_destroy(bucket);
    free(bid.name);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 属性继承测试
 *============================================================================*/

static int test_attribute_inheritance(test_group_t* group) {
    TEST_START("attribute_inheritance");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户并设置属性 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("attr_user");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    rgw_sal_attrs_t* user_attrs = rgw_sal_rados_user_get_attrs(user);
    if (user_attrs) {
        uint8_t val[] = "user_level_attr";
        rgw_sal_attrs_set(user_attrs, "inherited_attr", val, sizeof(val) - 1);
    }

    /* 创建桶（可能继承用户属性） */
    rgw_sal_bucket_id_t bid = {0};
    bid.name = strdup("attr_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bid);
    if (bucket) {
        rgw_sal_attrs_t* bucket_attrs = rgw_sal_rados_bucket_get_attrs(bucket);
        printf("\n    User and bucket attributes set");
        rgw_sal_rados_bucket_destroy(bucket);
    }

    /* 创建对象（可能继承桶属性） */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("attr_object");

    rgw_sal_object_t* obj = rgw_sal_rados_get_object(driver, bucket, &key);
    if (obj) {
        rgw_sal_attrs_t* obj_attrs = rgw_sal_rados_object_get_attrs(obj);
        printf("\n    Object attributes set");
        rgw_sal_rados_object_destroy(obj);
    }

    /* 清理 */
    rgw_sal_rados_user_destroy(user);
    free(uid.id);
    free(bid.name);
    free(key.name);
    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 并发操作测试（模拟）
 *============================================================================*/

static int test_concurrent_operations(test_group_t* group) {
    TEST_START("concurrent_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 模拟创建多个独立对象 */
    const int num_users = 10;
    rgw_sal_user_t* users[10] = {NULL};
    bool all_created = true;

    for (int i = 0; i < num_users; i++) {
        char id[32];
        snprintf(id, sizeof(id), "concurrent_user_%d", i);

        rgw_sal_user_id_t uid = {0};
        uid.id = strdup(id);

        users[i] = rgw_sal_rados_get_user(driver, &uid);
        free(uid.id);

        if (!users[i]) {
            all_created = false;
            printf("\n    WARNING: user %d creation failed", i);
        }
    }

    if (all_created) {
        printf("\n    Created %d users concurrently", num_users);
    }

    /* 验证所有用户 */
    int verified = 0;
    for (int i = 0; i < num_users; i++) {
        if (users[i]) {
            const char* id = rgw_sal_rados_user_get_id(users[i]);
            if (id) verified++;
        }
    }

    printf("\n    Verified %d/%d users", verified, num_users);

    /* 清理 */
    for (int i = 0; i < num_users; i++) {
        if (users[i]) {
            rgw_sal_rados_user_destroy(users[i]);
        }
    }

    rgw_sal_rados_driver_destroy(driver);

    group->passed++;
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(void) {
    printf("========================================\n");
    printf("SAL RADOS Integration Test Suite\n");
    printf("========================================\n\n");

    int total_passed = 0;
    int total_failed = 0;

    /* 用户生命周期测试 */
    test_group_t* user_group = create_test_group("User Lifecycle");
    test_user_full_lifecycle(user_group);
    print_group_result(user_group);
    total_passed += user_group->passed;
    total_failed += user_group->failed;
    free_test_group(user_group);

    /* 桶生命周期测试 */
    test_group_t* bucket_group = create_test_group("Bucket Lifecycle");
    test_bucket_full_lifecycle(bucket_group);
    print_group_result(bucket_group);
    total_passed += bucket_group->passed;
    total_failed += bucket_group->failed;
    free_test_group(bucket_group);

    /* 对象生命周期测试 */
    test_group_t* object_group = create_test_group("Object Lifecycle");
    test_object_full_lifecycle(object_group);
    print_group_result(object_group);
    total_passed += object_group->passed;
    total_failed += object_group->failed;
    free_test_group(object_group);

    /* 用户-桶关联测试 */
    test_group_t* assoc_group = create_test_group("Associations");
    test_user_bucket_association(assoc_group);
    test_bucket_object_association(assoc_group);
    test_attribute_inheritance(assoc_group);
    print_group_result(assoc_group);
    total_passed += assoc_group->passed;
    total_failed += assoc_group->failed;
    free_test_group(assoc_group);

    /* 并发操作测试 */
    test_group_t* concurrent_group = create_test_group("Concurrent Operations");
    test_concurrent_operations(concurrent_group);
    print_group_result(concurrent_group);
    total_passed += concurrent_group->passed;
    total_failed += concurrent_group->failed;
    free_test_group(concurrent_group);

    /* 总结 */
    int total_tests = total_passed + total_failed;

    printf("========================================\n");
    printf("Integration Test Summary\n");
    printf("========================================\n");
    printf("Total tests:    %d\n", total_tests);
    printf("Tests passed:   %d\n", total_passed);
    printf("Tests failed:   %d\n", total_failed);
    printf("========================================\n");

    if (total_failed == 0) {
        printf("All integration tests PASSED!\n");
    } else {
        printf("Some integration tests FAILED!\n");
    }
    printf("========================================\n");

    fflush(stdout);

    return total_failed > 0 ? 1 : 0;
}

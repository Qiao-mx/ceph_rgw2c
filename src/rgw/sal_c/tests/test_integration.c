/**
 * @file test_integration.c
 * @brief 综合集成测试
 *
 * 测试内容:
 * 1. 多驱动对比测试 (RADOS vs DBStore)
 * 2. C/C++ 互操作性测试
 * 3. 内存管理测试
 * 4. 完整功能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "rgw_sal.h"
#include "rgw_sal_rados.h"
#include "rgw_sal_dbstore.h"

/*============================================================================
 * 测试辅助函数
 *============================================================================*/

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAILED: %s\n", msg); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            printf("FAILED: %s (expected %d, got %d)\n", msg, (int)(b), (int)(a)); \
            return 1; \
        } \
    } while(0)

#define TEST_ASSERT_STR_EQ(a, b, msg) \
    do { \
        if (!a || !b || strcmp(a, b) != 0) { \
            printf("FAILED: %s (expected '%s', got '%s')\n", msg, (b) ? b : "NULL", (a) ? a : "NULL"); \
            return 1; \
        } \
    } while(0)

static int test_num = 0;
static int pass_num = 0;

#define RUN_TEST(name) do { \
    test_num++; \
    printf("\n[Test %d] %s\n", test_num, #name); \
    fflush(stdout); \
    int result = name(); \
    if (result == 0) { \
        pass_num++; \
        printf("[PASS] %s\n", #name); \
    } else { \
        printf("[FAIL] %s (error: %d)\n", #name, result); \
    } \
    fflush(stdout); \
} while(0)

/*============================================================================
 * 测试用例
 *============================================================================*/

/**
 * @brief 测试驱动创建和销毁
 */
static int test_driver_create(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create rados driver");

    const char* name = rgw_sal_get_driver_name(driver);
    TEST_ASSERT_STR_EQ(name, "rados", "driver name");

    rgw_sal_destroy_driver(driver);

    driver = rgw_sal_create_driver("dbstore", NULL);
    TEST_ASSERT(driver != NULL, "create dbstore driver");

    name = rgw_sal_get_driver_name(driver);
    TEST_ASSERT_STR_EQ(name, "dbstore", "driver name");

    rgw_sal_destroy_driver(driver);

    /* 测试无效驱动名称 */
    driver = rgw_sal_create_driver("invalid", NULL);
    TEST_ASSERT(driver == NULL, "invalid driver should return NULL");

    return 0;
}

/**
 * @brief 测试用户创建和属性
 */
static int test_user_operations(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("testuser");
    uid.tenant = strdup("testtenant");

    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    TEST_ASSERT(user != NULL, "get user");

    const char* id = user->vtable->get_id(user);
    TEST_ASSERT_STR_EQ(id, "testuser", "user id");

    const char* tenant = user->vtable->get_tenant(user);
    TEST_ASSERT_STR_EQ(tenant, "testtenant", "user tenant");

    /* 设置显示名称 */
    int ret = user->vtable->set_display_name(user, "Test User");
    TEST_ASSERT_EQ(ret, RGW_SAL_OK, "set display name");

    const char* display_name = user->vtable->get_display_name(user);
    TEST_ASSERT_STR_EQ(display_name, "Test User", "display name");

    /* 测试属性 */
    rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);
    TEST_ASSERT(attrs != NULL, "get attrs");

    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    ret = rgw_sal_attrs_set(attrs, "test_attr", data, sizeof(data));
    TEST_ASSERT_EQ(ret, RGW_SAL_OK, "set attr");

    uint8_t* out_data = NULL;
    size_t out_len = 0;
    ret = rgw_sal_attrs_get(attrs, "test_attr", &out_data, &out_len);
    TEST_ASSERT_EQ(ret, RGW_SAL_OK, "get attr");
    TEST_ASSERT_EQ(out_len, 4UL, "attr length");

    free(out_data);
    rgw_sal_user_destroy(user);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);

    return 0;
}

/**
 * @brief 测试桶操作
 */
static int test_bucket_operations(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("testbucket");
    info.bucket.tenant = strdup("testtenant");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    TEST_ASSERT(bucket != NULL, "get bucket");

    const char* name = bucket->vtable->get_name(bucket);
    TEST_ASSERT_STR_EQ(name, "testbucket", "bucket name");

    const char* tenant = bucket->vtable->get_tenant(bucket);
    TEST_ASSERT_STR_EQ(tenant, "testtenant", "bucket tenant");

    /* 测试克隆 */
    rgw_sal_bucket_t* cloned = bucket->vtable->clone(bucket);
    TEST_ASSERT(cloned != NULL, "clone bucket");

    const char* cloned_name = cloned->vtable->get_name(cloned);
    TEST_ASSERT_STR_EQ(cloned_name, "testbucket", "cloned bucket name");

    rgw_sal_bucket_destroy(cloned);
    rgw_sal_bucket_destroy(bucket);
    free(info.bucket.name);
    free(info.bucket.tenant);
    rgw_sal_destroy_driver(driver);

    return 0;
}

/**
 * @brief 测试对象操作
 */
static int test_object_operations(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");

    rgw_sal_bucket_info_t info = {0};
    info.bucket.name = strdup("testbucket");

    rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
    TEST_ASSERT(bucket != NULL, "get bucket");

    rgw_sal_obj_key_t key = {0};
    key.name = strdup("testobject");
    key.instance = strdup("version1");

    rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
    TEST_ASSERT(obj != NULL, "get object");

    const char* obj_name = obj->vtable->get_name(obj);
    TEST_ASSERT_STR_EQ(obj_name, "testobject", "object name");

    const char* instance = obj->vtable->get_instance(obj);
    TEST_ASSERT_STR_EQ(instance, "version1", "object instance");

    bool is_null = obj->vtable->is_null(obj);
    TEST_ASSERT(is_null == false, "object is not null");

    /* 测试克隆 */
    rgw_sal_object_t* cloned = obj->vtable->clone(obj);
    TEST_ASSERT(cloned != NULL, "clone object");

    const char* cloned_name = cloned->vtable->get_name(cloned);
    TEST_ASSERT_STR_EQ(cloned_name, "testobject", "cloned object name");

    rgw_sal_object_destroy(cloned);
    rgw_sal_object_destroy(obj);
    rgw_sal_bucket_destroy(bucket);
    free(key.name);
    free(key.instance);
    free(info.bucket.name);
    rgw_sal_destroy_driver(driver);

    return 0;
}

/**
 * @brief 多驱动对比测试
 */
static int test_multi_driver_comparison(void) {
    const char* driver_names[] = {"rados", "dbstore"};
    int num_drivers = 2;

    for (int i = 0; i < num_drivers; i++) {
        printf("  Testing driver: %s\n", driver_names[i]);
        fflush(stdout);

        rgw_sal_driver_t* driver = rgw_sal_create_driver(driver_names[i], NULL);
        TEST_ASSERT(driver != NULL, "create driver");

        /* 测试驱动名称 */
        const char* name = rgw_sal_get_driver_name(driver);
        TEST_ASSERT_STR_EQ(name, driver_names[i], "driver name");

        /* 测试用户创建 */
        rgw_sal_user_id_t uid = {0};
        uid.id = strdup("testuser");

        rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
        TEST_ASSERT(user != NULL, "get user");

        const char* id = user->vtable->get_id(user);
        TEST_ASSERT_STR_EQ(id, "testuser", "user id");

        rgw_sal_user_destroy(user);
        free(uid.id);

        /* 测试桶创建 */
        rgw_sal_bucket_info_t info = {0};
        info.bucket.name = strdup("testbucket");

        rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
        TEST_ASSERT(bucket != NULL, "get bucket");

        const char* bucket_name = bucket->vtable->get_name(bucket);
        TEST_ASSERT_STR_EQ(bucket_name, "testbucket", "bucket name");

        /* 测试对象创建 */
        rgw_sal_obj_key_t key = {0};
        key.name = strdup("testobject");

        rgw_sal_object_t* obj = rgw_sal_get_object(driver, bucket, &key);
        TEST_ASSERT(obj != NULL, "get object");

        const char* obj_name = obj->vtable->get_name(obj);
        TEST_ASSERT_STR_EQ(obj_name, "testobject", "object name");

        rgw_sal_object_destroy(obj);
        rgw_sal_bucket_destroy(bucket);
        free(key.name);
        free(info.bucket.name);
        rgw_sal_destroy_driver(driver);
    }

    return 0;
}

/**
 * @brief 测试内存管理
 */
static int test_memory_management(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");

    /* 大量创建/销毁用户 */
    char id_buf[32];
    for (int i = 0; i < 100; i++) {
        snprintf(id_buf, sizeof(id_buf), "user%d", i);
        rgw_sal_user_id_t uid = {0};
        uid.id = strdup(id_buf);

        rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
        if (user) {
            /* 设置属性 */
            rgw_sal_attrs_t* attrs = user->vtable->get_attrs(user);
            if (attrs) {
                uint8_t data[] = {0x01};
                rgw_sal_attrs_set(attrs, "key", data, 1);
            }
            rgw_sal_user_destroy(user);
        }
        free(uid.id);
    }

    /* 大量创建/销毁桶 */
    for (int i = 0; i < 100; i++) {
        snprintf(id_buf, sizeof(id_buf), "bucket%d", i);
        rgw_sal_bucket_info_t info = {0};
        info.bucket.name = strdup(id_buf);

        rgw_sal_bucket_t* bucket = rgw_sal_get_bucket(driver, &info);
        if (bucket) {
            rgw_sal_bucket_destroy(bucket);
        }
        free(info.bucket.name);
    }

    rgw_sal_destroy_driver(driver);
    printf("  Memory stress test completed\n");
    fflush(stdout);

    return 0;
}

/**
 * @brief 测试 vtable 函数指针
 */
static int test_vtable_functions(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");
    TEST_ASSERT(driver->vtable != NULL, "driver vtable");
    TEST_ASSERT(driver->user_vtable != NULL, "user vtable");
    TEST_ASSERT(driver->bucket_vtable != NULL, "bucket vtable");
    TEST_ASSERT(driver->object_vtable != NULL, "object vtable");

    /* 测试 driver vtable 函数指针 */
    TEST_ASSERT(driver->vtable->destroy != NULL, "destroy");
    TEST_ASSERT(driver->vtable->initialize != NULL, "initialize");
    TEST_ASSERT(driver->vtable->get_name != NULL, "get_name");
    TEST_ASSERT(driver->vtable->get_user != NULL, "get_user");
    TEST_ASSERT(driver->vtable->get_bucket != NULL, "get_bucket");
    TEST_ASSERT(driver->vtable->get_object != NULL, "get_object");

    /* 测试 user vtable 函数指针 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("testuser");
    rgw_sal_user_t* user = rgw_sal_get_user(driver, &uid);
    TEST_ASSERT(user != NULL, "get user");
    TEST_ASSERT(user->vtable != NULL, "user vtable");
    TEST_ASSERT(user->vtable->clone != NULL, "clone");
    TEST_ASSERT(user->vtable->destroy != NULL, "destroy");
    TEST_ASSERT(user->vtable->get_id != NULL, "get_id");
    TEST_ASSERT(user->vtable->get_attrs != NULL, "get_attrs");

    rgw_sal_user_destroy(user);
    free(uid.id);
    rgw_sal_destroy_driver(driver);

    return 0;
}

/**
 * @brief 测试集群 ID 获取
 */
static int test_cluster_id(void) {
    const char* driver_names[] = {"rados", "dbstore"};

    for (int i = 0; i < 2; i++) {
        rgw_sal_driver_t* driver = rgw_sal_create_driver(driver_names[i], NULL);
        TEST_ASSERT(driver != NULL, "create driver");

        char* cluster_id = NULL;
        int ret = driver->vtable->get_cluster_id(driver, &cluster_id, NULL, NULL);
        TEST_ASSERT_EQ(ret, RGW_SAL_OK, "get_cluster_id");
        TEST_ASSERT(cluster_id != NULL, "cluster_id not null");

        printf("  Driver '%s' cluster_id: '%s'\n", driver_names[i], cluster_id);
        free(cluster_id);
        rgw_sal_destroy_driver(driver);
    }

    return 0;
}

/**
 * @brief 测试用户克隆
 */
static int test_user_clone(void) {
    rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", NULL);
    TEST_ASSERT(driver != NULL, "create driver");

    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("original");
    uid.tenant = strdup("tenant");

    rgw_sal_user_t* original = rgw_sal_get_user(driver, &uid);
    TEST_ASSERT(original != NULL, "get original user");

    /* 设置一些属性 */
    original->vtable->set_display_name(original, "Original Name");
    rgw_sal_attrs_t* attrs = original->vtable->get_attrs(original);
    uint8_t data[] = {0xAA, 0xBB};
    rgw_sal_attrs_set(attrs, "test", data, 2);

    /* 克隆用户 */
    rgw_sal_user_t* cloned = original->vtable->clone(original);
    TEST_ASSERT(cloned != NULL, "clone user");

    /* 验证克隆的用户 */
    const char* id = cloned->vtable->get_id(cloned);
    TEST_ASSERT_STR_EQ(id, "original", "cloned id");

    const char* display_name = cloned->vtable->get_display_name(cloned);
    TEST_ASSERT_STR_EQ(display_name, "Original Name", "cloned display name");

    /* 验证属性是独立的 (修改克隆不影响原始) */
    cloned->vtable->set_display_name(cloned, "Cloned Name");
    const char* orig_name = original->vtable->get_display_name(original);
    TEST_ASSERT_STR_EQ(orig_name, "Original Name", "original unchanged");

    rgw_sal_user_destroy(cloned);
    rgw_sal_user_destroy(original);
    free(uid.id);
    free(uid.tenant);
    rgw_sal_destroy_driver(driver);

    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(void) {
    printf("========================================\n");
    printf("SAL Comprehensive Integration Test Suite\n");
    printf("========================================\n");
    printf("Test started at: ");
    fflush(stdout);

    time_t now = time(NULL);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    printf("%s\n", time_buf);
    fflush(stdout);

    printf("\n========================================\n");
    printf("Running Tests...\n");
    printf("========================================\n");
    fflush(stdout);

    /* 基础功能测试 */
    RUN_TEST(test_driver_create);
    RUN_TEST(test_user_operations);
    RUN_TEST(test_bucket_operations);
    RUN_TEST(test_object_operations);

    /* 高级功能测试 */
    RUN_TEST(test_vtable_functions);
    RUN_TEST(test_cluster_id);
    RUN_TEST(test_user_clone);

    /* 压力测试 */
    RUN_TEST(test_memory_management);
    RUN_TEST(test_multi_driver_comparison);

    /* 测试结果 */
    printf("\n========================================\n");
    printf("Test Results Summary\n");
    printf("========================================\n");
    printf("Total tests:  %d\n", test_num);
    printf("Passed:       %d\n", pass_num);
    printf("Failed:       %d\n", test_num - pass_num);
    printf("========================================\n");

    if (pass_num == test_num) {
        printf("ALL TESTS PASSED!\n");
    } else {
        printf("SOME TESTS FAILED!\n");
    }
    printf("========================================\n");
    fflush(stdout);

    return (pass_num == test_num) ? 0 : 1;
}

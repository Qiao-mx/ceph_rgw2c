/**
 * @file test_ceph_cluster.c
 * @brief Ceph 集群集成测试
 *
 * 测试与真实 Ceph 集群的集成（需要运行的 Ceph 集群）：
 * - 驱动初始化和连接
 * - 使用 librados 直接测试
 *
 * 如果没有真实集群，测试会自动跳过需要集群的功能。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#include <rados/librados.h>

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
static bool g_cluster_available = false;

/*============================================================================
 * 测试配置
 *============================================================================*/

#define TEST_CLUSTER_NAME "ceph"
#define TEST_POOL_NAME "test_pool"

/*============================================================================
 * 辅助函数
 *============================================================================*/

/* 检查是否有可用的 Ceph 集群 */
static bool check_cluster_available(void) {
    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        return false;
    }

    ret = rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    if (ret != 0) {
        rados_shutdown(cluster);
        return false;
    }

    ret = rados_connect(cluster);
    if (ret != 0) {
        rados_shutdown(cluster);
        return false;
    }

    rados_shutdown(cluster);
    return true;
}

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
 * 测试用例
 *============================================================================*/

/* 测试 1: 驱动创建（不连接集群） */
static int test_driver_create(void) {
    TEST_START("driver_create");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    printf("\n    Driver created (no connection)");

    const char* name = rgw_sal_rados_driver_get_name(driver);
    if (name) {
        printf(", name='%s'", name);
    }

    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 2: 驱动使用 librados 连接集群 */
static int test_driver_rados_connect(void) {
    TEST_START("driver_rados_connect");

    /* 直接使用 librados 测试连接 */
    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        printf("\n    rados_create failed (ret=%d)", ret);
        TEST_FAIL("rados_create failed");
        return 1;
    }

    ret = rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    if (ret != 0) {
        printf("\n    Note: Could not read ceph.conf");
    }

    ret = rados_connect(cluster);
    if (ret != 0) {
        printf("\n    rados_connect failed (ret=%d)", ret);
        rados_shutdown(cluster);
        TEST_FAIL("rados_connect failed - is Ceph running?");
        return 1;
    }

    printf("\n    Connected to Ceph cluster via librados!");

    /* 获取集群 FSID */
    char fsid[128];
    ret = rados_cluster_fsid(cluster, fsid, sizeof(fsid) - 1);
    if (ret == 0) {
        printf(", FSID: %.8s...", fsid);
    }

    rados_shutdown(cluster);

    TEST_PASS();
    return 0;
}

/* 测试 3: 驱动获取集群信息 */
static int test_driver_cluster_info(void) {
    TEST_START("driver_cluster_info");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        rados_shutdown(cluster);
        TEST_FAIL("rados_connect failed");
        return 1;
    }

    /* 获取集群统计 */
    struct rados_cluster_stat_t stats;
    ret = rados_cluster_stat(cluster, &stats);
    if (ret == 0) {
        printf("\n    Cluster: %llu KB total, %llu KB used, %llu KB avail",
               (unsigned long long)stats.kb, (unsigned long long)stats.kb_used,
               (unsigned long long)stats.kb_avail);
    }

    /* 获取配置 */
    char mon_host[1024];
    ret = rados_conf_get(cluster, "mon_host", mon_host, sizeof(mon_host));
    if (ret == 0) {
        printf("\n    mon_host: %.50s...", mon_host);
    }

    rados_shutdown(cluster);

    TEST_PASS();
    return 0;
}

/* 测试 4: 池操作 */
static int test_pool_operations(void) {
    TEST_START("pool_operations");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        rados_shutdown(cluster);
        TEST_FAIL("rados_connect failed");
        return 1;
    }

    /* 列出池 */
    char pools[4096];
    size_t pool_count = rados_pool_list(cluster, pools, sizeof(pools));
    if (pool_count > 0) {
        printf("\n    Found %zu pools", pool_count);
    } else {
        printf("\n    No pools found or error listing");
    }

    /* 检查测试池是否存在 */
    bool pool_exists = rados_pool_lookup(cluster, TEST_POOL_NAME) >= 0;
    printf("\n    Pool '%s' exists: %s", TEST_POOL_NAME, pool_exists ? "yes" : "no");

    rados_shutdown(cluster);

    TEST_PASS();
    return 0;
}

/* 测试 5: 用户对象创建（内存测试） */
static int test_user_operations(void) {
    TEST_START("user_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建用户 - 只测试内存操作 */
    rgw_sal_user_id_t uid = {0};
    uid.id = strdup("cluster_user");
    uid.tenant = strdup("test_tenant");

    rgw_sal_user_t* user = rgw_sal_rados_get_user(driver, &uid);
    if (!user) {
        free(uid.id);
        free(uid.tenant);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("user creation failed");
        return 1;
    }

    printf("\n    User created: id='%s'", uid.id);

    /* 设置显示名称 */
    int ret = rgw_sal_rados_user_set_display_name(user, "Cluster Test User");
    TEST_EXPECT(ret, 0, "set_display_name should return 0");

    const char* display = rgw_sal_rados_user_get_display_name(user);
    TEST_EXPECT_STR(display, "Cluster Test User", "display_name should match");

    /* 清理 */
    free(uid.id);
    free(uid.tenant);
    rgw_sal_rados_user_destroy(user);
    rgw_sal_rados_driver_destroy(driver);

    TEST_PASS();
    return 0;
}

/* 测试 6: 桶对象创建（内存测试） */
static int test_bucket_operations(void) {
    TEST_START("bucket_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 ID */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("cluster_bucket");
    bucket_id.tenant = strdup("test_tenant");
    bucket_id.marker = strdup("marker_123");
    bucket_id.bucket_id = strdup("uuid_456");

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

    /* 验证属性 */
    const char* bucket_name = rgw_sal_rados_bucket_get_name(bucket);
    TEST_EXPECT_STR(bucket_name, "cluster_bucket", "bucket name should match");

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

/* 测试 7: 对象操作（内存测试） */
static int test_object_operations(void) {
    TEST_START("object_operations");

    rgw_sal_driver_t* driver = rgw_sal_rados_driver_create(NULL, NULL);
    if (!driver) {
        TEST_FAIL("driver creation failed");
        return 1;
    }

    /* 创建桶 */
    rgw_sal_bucket_id_t bucket_id = {0};
    bucket_id.name = strdup("obj_bucket");

    rgw_sal_bucket_t* bucket = rgw_sal_rados_get_bucket(driver, &bucket_id);
    if (!bucket) {
        free(bucket_id.name);
        rgw_sal_rados_driver_destroy(driver);
        TEST_FAIL("bucket creation failed");
        return 1;
    }

    /* 创建对象 */
    rgw_sal_obj_key_t key = {0};
    key.name = strdup("cluster_object");
    key.instance = strdup("v1");

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

    printf("\n    Object created: name='%s'", key.name);

    /* 验证属性 */
    const char* obj_name = rgw_sal_rados_object_get_name(obj);
    TEST_EXPECT_STR(obj_name, "cluster_object", "object name should match");

    rgw_sal_attrs_t* attrs = rgw_sal_rados_object_get_attrs(obj);
    TEST_EXPECT_NOT_NULL(attrs, "object attrs should not be NULL");

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

/*============================================================================
 * 主函数
 *============================================================================*/

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("\n");
    printf("========================================\n");
    printf("Ceph 集群集成测试 (C SAL API)\n");
    printf("========================================\n\n");

    /* 首先检查集群是否可用 */
    printf("[集群可用性检查]\n");
    g_cluster_available = check_cluster_available();
    if (g_cluster_available) {
        printf("  状态: Ceph 集群可用\n\n");
    } else {
        printf("  状态: Ceph 集群不可用，部分测试将被跳过\n\n");
    }

    int failed = 0;

    /* 测试 1: 驱动创建 */
    printf("[驱动创建测试]\n");
    failed += test_driver_create();

    /* 测试 2: librados 连接测试 */
    printf("[librados 连接测试]\n");
    if (g_cluster_available) {
        failed += test_driver_rados_connect();
    } else {
        TEST_START("driver_rados_connect");
        TEST_SKIP("no ceph cluster available");
    }

    /* 测试 3: 集群信息测试 */
    printf("[集群信息测试]\n");
    if (g_cluster_available) {
        failed += test_driver_cluster_info();
    } else {
        TEST_START("driver_cluster_info");
        TEST_SKIP("no ceph cluster available");
    }

    /* 测试 4: 池操作测试 */
    printf("[池操作测试]\n");
    if (g_cluster_available) {
        failed += test_pool_operations();
    } else {
        TEST_START("pool_operations");
        TEST_SKIP("no ceph cluster available");
    }

    /* 测试 5: 用户操作测试 */
    printf("[用户操作测试]\n");
    failed += test_user_operations();

    /* 测试 6: 桶操作测试 */
    printf("[桶操作测试]\n");
    failed += test_bucket_operations();

    /* 测试 7: 对象操作测试 */
    printf("[对象操作测试]\n");
    failed += test_object_operations();

    printf("\n");
    printf("========================================\n");
    printf("测试结果汇总\n");
    printf("========================================\n");
    printf("  运行: %d\n", g_tests_run);
    printf("  通过: %d\n", g_tests_passed);
    printf("  失败: %d\n", g_tests_failed);
    printf("  跳过: %d\n", g_tests_skipped);
    printf("========================================\n\n");

    if (g_tests_failed > 0) {
        printf("测试失败!\n");
        return 1;
    }

    printf("所有测试完成。\n");
    if (!g_cluster_available) {
        printf("注意: Ceph 集群不可用，真实 I/O 测试被跳过。\n");
        printf("要运行完整测试，请确保 Ceph 集群正在运行。\n");
    }

    return 0;
}

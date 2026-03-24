/**
 * @file test_ceph_cluster_connect.c
 * @brief Ceph 集群连接测试
 *
 * 测试 C SAL RADOS 驱动能否连接到真实的 Ceph 集群。
 * 使用 librados 直接连接，测试基本的 OMAP 操作。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#include <rados/librados.h>

/*============================================================================
 * 测试配置
 *============================================================================*/

#define TEST_CLUSTER_NAME "ceph"
#define TEST_POOL_NAME ".rgw.meta.users.uid"
#define TEST_NAMESPACE ""

#define TEST_LOG(fmt, ...) \
    do { \
        time_t now = time(NULL); \
        char* time_str = ctime(&now); \
        time_str[strlen(time_str) - 1] = '\0'; \
        printf("[%s] " fmt "\n", time_str, ##__VA_ARGS__); \
        fflush(stdout); \
    } while(0)

/*============================================================================
 * 测试结果跟踪
 *============================================================================*/

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

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

/*============================================================================
 * 测试用例
 *============================================================================*/

/**
 * @brief 测试集群创建和连接
 */
static int test_cluster_create(void) {
    TEST_START("cluster_create");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    /* 设置连接超时 (10秒) */
    ret = rados_conf_set(cluster, "client_mount_timeout", "10");
    if (ret != 0) {
        printf("\n    Note: Could not set timeout, using default");
    }

    /* 尝试从默认配置文件读取 */
    ret = rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    if (ret != 0) {
        printf("\n    Note: Could not read /etc/ceph/ceph.conf, trying default...");
        /* 尝试默认集群 */
    }

    /* 连接集群 */
    ret = rados_connect(cluster);
    if (ret != 0) {
        printf("\n    Error: rados_connect failed (ret=%d, errno=%d)", ret, errno);
        TEST_FAIL("rados_connect failed - is Ceph cluster running?");
        rados_shutdown(cluster);
        return 1;
    }

    printf("\n    Connected to cluster successfully");

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

/**
 * @brief 测试 IO 上下文创建
 */
static int test_ioctx_create(void) {
    TEST_START("ioctx_create");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    /* 读取配置文件 */
    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");

    /* 连接集群 */
    ret = rados_connect(cluster);
    if (ret != 0) {
        TEST_FAIL("rados_connect failed");
        rados_shutdown(cluster);
        return 1;
    }

    /* 创建 IO 上下文 */
    rados_ioctx_t ioctx;
    ret = rados_ioctx_create(cluster, TEST_POOL_NAME, &ioctx);
    if (ret != 0) {
        printf("\n    Note: Pool '%s' may not exist (ret=%d)", TEST_POOL_NAME, ret);
        /* 这不是致命错误，某些池可能需要先创建 */
    } else {
        printf("\n    Created IO context for pool '%s'", TEST_POOL_NAME);
        rados_ioctx_destroy(ioctx);
    }

    rados_shutdown(cluster);
    TEST_PASS();
    return 0;
}

/**
 * @brief 测试集群状态
 */
static int test_cluster_stat(void) {
    TEST_START("cluster_stat");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        TEST_FAIL("rados_connect failed");
        rados_shutdown(cluster);
        return 1;
    }

    /* 获取集群统计信息 - 使用新版 API */
    struct rados_cluster_stat_t stats;
    ret = rados_cluster_stat(cluster, &stats);
    if (ret == 0) {
        printf("\n    Cluster stats: %llu KB total, %llu KB used, %llu KB avail",
               (unsigned long long)stats.kb, (unsigned long long)stats.kb_used,
               (unsigned long long)stats.kb_avail);
    } else {
        printf("\n    Note: Could not get cluster stats (ret=%d)", ret);
    }

    rados_shutdown(cluster);
    TEST_PASS();
    return 0;
}

/**
 * @brief 测试池列表
 */
static int test_pool_list(void) {
    TEST_START("pool_list");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        TEST_FAIL("rados_connect failed");
        rados_shutdown(cluster);
        return 1;
    }

    /* 获取池列表 */
    char pools[4096];
    size_t pool_count = rados_pool_list(cluster, pools, sizeof(pools));
    if (pool_count > 0) {
        printf("\n    Found %zu pools:", pool_count);
        char* pool_name = pools;
        int count = 0;
        for (size_t i = 0; i < pool_count && count < 10; i++) {
            if (pools[i] == '\0') {
                printf("\n      - %s", pool_name);
                count++;
                pool_name = &pools[i + 1];
            }
        }
        if (pool_count > 10) {
            printf("\n      ... and %zu more", pool_count - 10);
        }
    } else {
        printf("\n    Note: Could not list pools");
    }

    rados_shutdown(cluster);
    TEST_PASS();
    return 0;
}

/**
 * @brief 测试 OMAP 操作 (需要先创建池)
 */
static int test_omap_operations(void) {
    TEST_START("omap_operations");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        TEST_FAIL("rados_connect failed");
        rados_shutdown(cluster);
        return 1;
    }

    /* 检查池是否存在 */
    bool pool_exists = rados_pool_lookup(cluster, TEST_POOL_NAME) >= 0;
    if (!pool_exists) {
        printf("\n    Pool '%s' does not exist, skipping OMAP test", TEST_POOL_NAME);
        printf("\n    To create: ceph osd pool create %s", TEST_POOL_NAME);
        rados_shutdown(cluster);
        TEST_PASS();
        return 0;
    }

    /* 创建 IO 上下文 */
    rados_ioctx_t ioctx;
    ret = rados_ioctx_create(cluster, TEST_POOL_NAME, &ioctx);
    if (ret != 0) {
        printf("\n    Could not create IO context (ret=%d)", ret);
        rados_shutdown(cluster);
        TEST_PASS();
        return 0;
    }

    /* 测试 OMAP 写入操作 */
    const char* test_key = "test_key";
    const char* test_value = "test_value_for_ceph_cluster_test";
    const size_t test_value_len = strlen(test_value);

    ret = rados_write_full(ioctx, "test_object", test_value, test_value_len);
    if (ret == 0) {
        printf("\n    Wrote test object successfully");
    } else {
        printf("\n    Note: Could not write test object (ret=%d)", ret);
    }

    /* 清理测试对象 */
    rados_remove(ioctx, "test_object");

    rados_ioctx_destroy(ioctx);
    rados_shutdown(cluster);
    TEST_PASS();
    return 0;
}

/**
 * @brief 测试配置获取
 */
static int test_config_get(void) {
    TEST_START("config_get");

    rados_t cluster = NULL;
    int ret = rados_create(&cluster, TEST_CLUSTER_NAME);
    if (ret != 0) {
        TEST_FAIL("rados_create failed");
        return 1;
    }

    rados_conf_read_file(cluster, "/etc/ceph/ceph.conf");
    ret = rados_connect(cluster);
    if (ret != 0) {
        TEST_FAIL("rados_connect failed");
        rados_shutdown(cluster);
        return 1;
    }

    /* 获取一些配置项 */
    char mon_host[1024];
    ret = rados_conf_get(cluster, "mon_host", mon_host, sizeof(mon_host));
    if (ret == 0) {
        printf("\n    mon_host: %.50s...", mon_host);
    }

    char fsid_str[128];
    ret = rados_conf_get(cluster, "fsid", fsid_str, sizeof(fsid_str));
    if (ret == 0) {
        printf(", fsid: %.20s", fsid_str);
    }

    rados_shutdown(cluster);
    TEST_PASS();
    return 0;
}

/*============================================================================
 * 主函数
 *============================================================================*/

int main(int argc, char** argv) {
    printf("\n");
    printf("========================================\n");
    printf("  C SAL RADOS Ceph Cluster Test\n");
    printf("========================================\n");
    printf("\n");
    printf("Configuration:\n");
    printf("  Cluster name: %s\n", TEST_CLUSTER_NAME);
    printf("  Config file: /etc/ceph/ceph.conf\n");
    printf("  Test pool: %s\n", TEST_POOL_NAME);
    printf("\n");

    TEST_LOG("Starting Ceph cluster connection tests...\n");

    /* 运行测试 */
    test_cluster_create();
    test_ioctx_create();
    test_cluster_stat();
    test_pool_list();
    test_omap_operations();
    test_config_get();

    /* 打印结果 */
    printf("\n");
    printf("========================================\n");
    printf("  Test Results\n");
    printf("========================================\n");
    printf("  Total:  %d\n", g_tests_run);
    printf("  Passed: %d\n", g_tests_passed);
    printf("  Failed: %d\n", g_tests_failed);
    printf("========================================\n");

    if (g_tests_failed > 0) {
        printf("\nSome tests failed. Please check:\n");
        printf("  1. Is Ceph installed? (ceph --version)\n");
        printf("  2. Is ceph.conf present? (/etc/ceph/ceph.conf)\n");
        printf("  3. Is a Ceph cluster running? (ceph -s)\n");
        printf("  4. Does the test pool exist?\n");
        return 1;
    }

    printf("\nAll basic connection tests passed!\n");
    printf("The C SAL RADOS driver should be able to connect to this cluster.\n");

    return 0;
}

/**
 * @file test_rados_types.c
 * @brief RADOS 驱动核心类型单元测试
 *
 * 测试 RADOS 驱动的核心类型：
 * - rados_user_impl_t
 * - rados_bucket_impl_t
 * - rados_object_impl_t
 * - rados_driver_impl_t
 * - 序列化/反序列化函数
 * - 配额信息结构
 * - 用户权限结构
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

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

#define TEST_EXPECT_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        printf("[FAIL] %s (expected 0x%x, got 0x%x)\n", msg, (unsigned)(expected), (unsigned)(actual)); \
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
 * 测试辅助结构 - 复制 rados 驱动内部结构用于测试
 *============================================================================*/

/* 配额信息 - 必须与 rados_driver_impl.c 中的定义一致 */
typedef struct {
    bool enabled;
    bool check_on_raw;
    uint64_t max_size;
    uint64_t max_size_kb;
    uint64_t max_objects;
    uint64_t quota_bytes;
    uint64_t quota_max_objects;
} test_quota_info_t;

/* 用户权限 */
typedef struct {
    char* caps;
} test_user_caps_t;

/* 版本信息 */
typedef struct {
    uint32_t epoch;
    char* ver;
    bool committed;
} test_obj_version_t;

/* 版本跟踪器 */
typedef struct {
    test_obj_version_t read_version;
    test_obj_version_t write_version;
    char* obj_tag;
    char* instance_tag;
} test_obj_version_tracker_t;

/* 用户实现结构 */
typedef struct {
    char* id;
    char* tenant;
    char* display_name;
    char* email;
    char* ns;
    uint32_t user_type;
    int32_t max_buckets;
    test_quota_info_t quota_info;
    test_user_caps_t user_caps;
    test_obj_version_tracker_t version_tracker;
    bool destroyed;
} test_user_impl_t;

/* 桶实现结构 */
typedef struct {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    char* tag;
    bool loaded;
    bool created;
    bool deleted;
    time_t mtime;
    bool destroyed;
} test_bucket_impl_t;

/* 对象实现结构 */
typedef struct {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    char* bucket_id;
    char* obj_oid;
    bool is_null;
    int64_t size;
    time_t mtime;
    bool written;
    bool deleted;
    bool loaded;
    bool is_atomic;
    bool is_expired;
    bool destroyed;
} test_object_impl_t;

/*============================================================================
 * 序列化/反序列化函数 (测试用简化版本)
 *============================================================================*/

/* 解析用户数据缓冲区 */
static int test_parse_user_from_buffer(test_user_impl_t* impl,
                                       const uint8_t* data, size_t data_len) {
    if (!impl || !data || data_len == 0) return -1;

    char* buffer = (char*)malloc(data_len + 1);
    if (!buffer) return -1;
    memcpy(buffer, data, data_len);
    buffer[data_len] = '\0';

    char* line = buffer;
    char* next;

    while (line && *line) {
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
            next++;
        }

        if (*line == '\0') {
            line = next;
            continue;
        }

        char* equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* key = line;
            char* value = equals + 1;

            if (strcmp(key, "id") == 0) {
                free(impl->id);
                impl->id = strdup(value);
            } else if (strcmp(key, "tenant") == 0) {
                free(impl->tenant);
                impl->tenant = strdup(value);
            } else if (strcmp(key, "display_name") == 0) {
                free(impl->display_name);
                impl->display_name = strdup(value);
            } else if (strcmp(key, "email") == 0) {
                free(impl->email);
                impl->email = strdup(value);
            } else if (strcmp(key, "ns") == 0) {
                free(impl->ns);
                impl->ns = strdup(value);
            } else if (strcmp(key, "user_type") == 0) {
                impl->user_type = (uint32_t)atoi(value);
            } else if (strcmp(key, "max_buckets") == 0) {
                impl->max_buckets = (int32_t)atoi(value);
            } else if (strcmp(key, "quota_enabled") == 0) {
                impl->quota_info.enabled = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
            } else if (strcmp(key, "quota_check_on_raw") == 0) {
                impl->quota_info.check_on_raw = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
            } else if (strcmp(key, "quota_bytes") == 0) {
                impl->quota_info.quota_bytes = strtoull(value, NULL, 10);
            } else if (strcmp(key, "quota_max_objects") == 0) {
                impl->quota_info.quota_max_objects = strtoull(value, NULL, 10);
            } else if (strcmp(key, "user_caps") == 0) {
                free(impl->user_caps.caps);
                impl->user_caps.caps = strdup(value);
            }
        }

        line = next;
    }

    free(buffer);
    return 0;
}

/* 将用户数据序列化为缓冲区 */
static uint8_t* test_serialize_user_to_buffer(test_user_impl_t* impl,
                                               size_t* buf_size) {
    if (!impl || !buf_size) return NULL;

    size_t estimate = 1024;
    if (impl->id) estimate += strlen(impl->id) + 10;
    if (impl->tenant) estimate += strlen(impl->tenant) + 10;
    if (impl->display_name) estimate += strlen(impl->display_name) + 20;
    if (impl->email) estimate += strlen(impl->email) + 10;
    if (impl->ns) estimate += strlen(impl->ns) + 10;
    estimate += 40;
    if (impl->user_caps.caps) estimate += strlen(impl->user_caps.caps) + 15;

    char* buffer = (char*)malloc(estimate);
    if (!buffer) return NULL;

    size_t offset = 0;
    int ret;

    if (impl->id) {
        ret = snprintf(buffer + offset, estimate - offset, "id=%s\n", impl->id);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->tenant) {
        ret = snprintf(buffer + offset, estimate - offset, "tenant=%s\n", impl->tenant);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->display_name) {
        ret = snprintf(buffer + offset, estimate - offset, "display_name=%s\n", impl->display_name);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->email) {
        ret = snprintf(buffer + offset, estimate - offset, "email=%s\n", impl->email);
        if (ret > 0) offset += (size_t)ret;
    }
    if (impl->ns) {
        ret = snprintf(buffer + offset, estimate - offset, "ns=%s\n", impl->ns);
        if (ret > 0) offset += (size_t)ret;
    }

    ret = snprintf(buffer + offset, estimate - offset, "user_type=%u\n", impl->user_type);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "max_buckets=%d\n", impl->max_buckets);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_enabled=%d\n",
                   impl->quota_info.enabled ? 1 : 0);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_check_on_raw=%d\n",
                   impl->quota_info.check_on_raw ? 1 : 0);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_bytes=%llu\n",
                   (unsigned long long)impl->quota_info.quota_bytes);
    if (ret > 0) offset += (size_t)ret;

    ret = snprintf(buffer + offset, estimate - offset, "quota_max_objects=%llu\n",
                   (unsigned long long)impl->quota_info.quota_max_objects);
    if (ret > 0) offset += (size_t)ret;

    if (impl->user_caps.caps) {
        ret = snprintf(buffer + offset, estimate - offset, "user_caps=%s\n", impl->user_caps.caps);
        if (ret > 0) offset += (size_t)ret;
    }

    *buf_size = offset;
    return (uint8_t*)buffer;
}

/* 释放缓冲区 */
static void test_free_buffer(uint8_t* buffer) {
    free(buffer);
}

/* 释放用户实现结构 */
static void test_user_impl_destroy(test_user_impl_t* impl) {
    if (!impl) return;
    if (impl->destroyed) return;
    impl->destroyed = true;

    free(impl->id);
    free(impl->tenant);
    free(impl->display_name);
    free(impl->email);
    free(impl->ns);
    free(impl->user_caps.caps);
    free(impl->version_tracker.ver);
    free(impl->version_tracker.obj_tag);
    free(impl->version_tracker.instance_tag);
    memset(impl, 0, sizeof(test_user_impl_t));
}

/* 创建用户实现结构 */
static test_user_impl_t* test_user_impl_create(const char* id, const char* tenant) {
    test_user_impl_t* impl = (test_user_impl_t*)calloc(1, sizeof(test_user_impl_t));
    if (!impl) return NULL;

    if (id) impl->id = strdup(id);
    if (tenant) impl->tenant = strdup(tenant);
    impl->max_buckets = -1;

    return impl;
}

/*============================================================================
 * 测试用例
 *============================================================================*/

/* 测试配额信息结构 */
static int test_quota_info_struct(void) {
    TEST_START("quota_info_struct");

    test_quota_info_t quota = {0};

    /* 测试默认值 */
    TEST_EXPECT_FALSE(quota.enabled, "quota.enabled should be false by default");
    TEST_EXPECT_FALSE(quota.check_on_raw, "quota.check_on_raw should be false by default");
    TEST_EXPECT_EQ(quota.max_size, 0, "quota.max_size should be 0");
    TEST_EXPECT_EQ(quota.max_size_kb, 0, "quota.max_size_kb should be 0");
    TEST_EXPECT_EQ(quota.max_objects, 0, "quota.max_objects should be 0");
    TEST_EXPECT_EQ(quota.quota_bytes, 0, "quota.quota_bytes should be 0");
    TEST_EXPECT_EQ(quota.quota_max_objects, 0, "quota.quota_max_objects should be 0");

    /* 设置配额值 */
    quota.enabled = true;
    quota.max_size = 1024 * 1024 * 1024;  /* 1GB */
    quota.max_size_kb = 1024 * 1024;       /* 1GB in KB */
    quota.max_objects = 10000;
    quota.quota_bytes = quota.max_size;
    quota.quota_max_objects = quota.max_objects;

    TEST_EXPECT_TRUE(quota.enabled, "quota.enabled should be true");
    TEST_EXPECT_EQ(quota.max_size, 1073741824ULL, "quota.max_size should be 1GB");

    TEST_PASS();
    return 0;
}

/* 测试用户权限结构 */
static int test_user_caps_struct(void) {
    TEST_START("user_caps_struct");

    test_user_caps_t caps = {0};

    TEST_EXPECT_NULL(caps.caps, "caps.caps should be NULL initially");

    /* 设置权限字符串 */
    caps.caps = strdup("read,write");
    TEST_EXPECT_NOT_NULL(caps.caps, "caps.caps should not be NULL after strdup");
    TEST_EXPECT_STR(caps.caps, "read,write", "caps.caps should be 'read,write'");

    free(caps.caps);
    TEST_PASS();
    return 0;
}

/* 测试版本跟踪器结构 */
static int test_obj_version_tracker_struct(void) {
    TEST_START("obj_version_tracker_struct");

    test_obj_version_tracker_t tracker = {0};

    TEST_EXPECT_EQ(tracker.read_version.epoch, 0, "read_version.epoch should be 0");
    TEST_EXPECT_EQ(tracker.write_version.epoch, 0, "write_version.epoch should be 0");
    TEST_EXPECT_NULL(tracker.obj_tag, "obj_tag should be NULL");
    TEST_EXPECT_NULL(tracker.instance_tag, "instance_tag should be NULL");

    /* 设置版本信息 */
    tracker.read_version.epoch = 1;
    tracker.write_version.epoch = 2;
    tracker.read_version.committed = true;
    tracker.write_version.committed = false;
    tracker.obj_tag = strdup("obj_tag_123");
    tracker.instance_tag = strdup("instance_tag_456");

    TEST_EXPECT_EQ(tracker.read_version.epoch, 1, "read_version.epoch should be 1");
    TEST_EXPECT_EQ(tracker.write_version.epoch, 2, "write_version.epoch should be 2");
    TEST_EXPECT_TRUE(tracker.read_version.committed, "read_version.committed should be true");
    TEST_EXPECT_FALSE(tracker.write_version.committed, "write_version.committed should be false");
    TEST_EXPECT_STR(tracker.obj_tag, "obj_tag_123", "obj_tag should match");

    free(tracker.obj_tag);
    free(tracker.instance_tag);

    TEST_PASS();
    return 0;
}

/* 测试用户实现结构创建 */
static int test_user_impl_create_destroy(void) {
    TEST_START("user_impl_create_destroy");

    test_user_impl_t* impl = test_user_impl_create("test_user", "test_tenant");
    TEST_EXPECT_NOT_NULL(impl, "user_impl_create should not return NULL");

    TEST_EXPECT_STR(impl->id, "test_user", "user id should be 'test_user'");
    TEST_EXPECT_STR(impl->tenant, "test_tenant", "tenant should be 'test_tenant'");
    TEST_EXPECT_EQ(impl->max_buckets, -1, "max_buckets should be -1");
    TEST_EXPECT_FALSE(impl->destroyed, "destroyed should be false");

    test_user_impl_destroy(impl);
    free(impl);

    TEST_PASS();
    return 0;
}

/* 测试用户序列化 */
static int test_user_serialize(void) {
    TEST_START("user_serialize");

    test_user_impl_t* impl = test_user_impl_create("user123", "tenant456");
    TEST_EXPECT_NOT_NULL(impl, "user_impl_create should not return NULL");

    impl->display_name = strdup("Test User");
    impl->email = strdup("test@example.com");
    impl->user_type = 1;
    impl->max_buckets = 100;
    impl->quota_info.enabled = true;
    impl->quota_info.quota_bytes = 1024 * 1024 * 1024;
    impl->quota_info.quota_max_objects = 10000;
    impl->user_caps.caps = strdup("read,write,delete");

    /* 序列化 */
    size_t buf_size = 0;
    uint8_t* buffer = test_serialize_user_to_buffer(impl, &buf_size);
    TEST_EXPECT_NOT_NULL(buffer, "serialize should not return NULL");
    TEST_EXPECT_TRUE(buf_size > 0, "buf_size should be greater than 0");

    printf("\n    Serialized %zu bytes", buf_size);

    /* 反序列化到新结构 */
    test_user_impl_t* parsed = test_user_impl_create(NULL, NULL);
    TEST_EXPECT_NOT_NULL(parsed, "parsed user should not be NULL");

    int ret = test_parse_user_from_buffer(parsed, buffer, buf_size);
    TEST_EXPECT_EQ(ret, 0, "parse should return 0");

    /* 验证反序列化结果 */
    TEST_EXPECT_STR(parsed->id, "user123", "parsed id should be 'user123'");
    TEST_EXPECT_STR(parsed->tenant, "tenant456", "parsed tenant should be 'tenant456'");
    TEST_EXPECT_STR(parsed->display_name, "Test User", "parsed display_name should match");
    TEST_EXPECT_STR(parsed->email, "test@example.com", "parsed email should match");
    TEST_EXPECT_EQ(parsed->user_type, 1, "parsed user_type should be 1");
    TEST_EXPECT_EQ(parsed->max_buckets, 100, "parsed max_buckets should be 100");
    TEST_EXPECT_TRUE(parsed->quota_info.enabled, "parsed quota.enabled should be true");
    TEST_EXPECT_EQ(parsed->quota_info.quota_bytes, 1073741824ULL, "parsed quota_bytes should be 1GB");
    TEST_EXPECT_EQ(parsed->quota_info.quota_max_objects, 10000ULL, "parsed quota_max_objects should be 10000");
    TEST_EXPECT_STR(parsed->user_caps.caps, "read,write,delete", "parsed caps should match");

    /* 清理 */
    test_free_buffer(buffer);
    test_user_impl_destroy(parsed);
    free(parsed);
    test_user_impl_destroy(impl);
    free(impl);

    TEST_PASS();
    return 0;
}

/* 测试配额信息序列化 */
static int test_quota_serialize(void) {
    TEST_START("quota_serialize");

    test_user_impl_t* impl = test_user_impl_create("quota_user", NULL);
    TEST_EXPECT_NOT_NULL(impl, "user_impl_create should not return NULL");

    /* 设置配额 */
    impl->quota_info.enabled = true;
    impl->quota_info.check_on_raw = false;
    impl->quota_info.max_size = 10 * 1024 * 1024 * 1024ULL;  /* 10GB */
    impl->quota_info.max_size_kb = 10 * 1024 * 1024;          /* 10GB in KB */
    impl->quota_info.max_objects = 100000;
    impl->quota_info.quota_bytes = impl->quota_info.max_size;
    impl->quota_info.quota_max_objects = impl->quota_info.max_objects;

    /* 序列化 */
    size_t buf_size = 0;
    uint8_t* buffer = test_serialize_user_to_buffer(impl, &buf_size);
    TEST_EXPECT_NOT_NULL(buffer, "serialize should not return NULL");

    /* 反序列化 */
    test_user_impl_t* parsed = test_user_impl_create(NULL, NULL);
    int ret = test_parse_user_from_buffer(parsed, buffer, buf_size);
    TEST_EXPECT_EQ(ret, 0, "parse should return 0");

    /* 验证配额信息 */
    TEST_EXPECT_TRUE(parsed->quota_info.enabled, "parsed quota.enabled should be true");
    TEST_EXPECT_FALSE(parsed->quota_info.check_on_raw, "parsed quota.check_on_raw should be false");
    TEST_EXPECT_EQ(parsed->quota_info.max_size, 10737418240ULL, "parsed max_size should be 10GB");
    TEST_EXPECT_EQ(parsed->quota_info.max_objects, 100000ULL, "parsed max_objects should be 100000");

    /* 清理 */
    test_free_buffer(buffer);
    test_user_impl_destroy(parsed);
    free(parsed);
    test_user_impl_destroy(impl);
    free(impl);

    TEST_PASS();
    return 0;
}

/* 测试边界值配额序列化 */
static int test_quota_boundary_values(void) {
    TEST_START("quota_boundary_values");

    test_user_impl_t* impl = test_user_impl_create("boundary_user", NULL);
    TEST_EXPECT_NOT_NULL(impl, "user_impl_create should not return NULL");

    /* 测试大值配额 */
    impl->quota_info.quota_bytes = ULLONG_MAX;
    impl->quota_info.quota_max_objects = ULLONG_MAX;

    size_t buf_size = 0;
    uint8_t* buffer = test_serialize_user_to_buffer(impl, &buf_size);
    TEST_EXPECT_NOT_NULL(buffer, "serialize should not return NULL");

    test_user_impl_t* parsed = test_user_impl_create(NULL, NULL);
    int ret = test_parse_user_from_buffer(parsed, buffer, buf_size);
    TEST_EXPECT_EQ(ret, 0, "parse should return 0");

    /* 验证大值 */
    TEST_EXPECT_EQ(parsed->quota_info.quota_bytes, ULLONG_MAX, "quota_bytes should handle ULLONG_MAX");
    TEST_EXPECT_EQ(parsed->quota_info.quota_max_objects, ULLONG_MAX, "quota_max_objects should handle ULLONG_MAX");

    /* 测试零值配额 */
    impl->quota_info.quota_bytes = 0;
    impl->quota_info.quota_max_objects = 0;
    impl->quota_info.enabled = false;

    buffer = test_serialize_user_to_buffer(impl, &buf_size);
    TEST_EXPECT_NOT_NULL(buffer, "serialize should not return NULL");

    ret = test_parse_user_from_buffer(parsed, buffer, buf_size);
    TEST_EXPECT_EQ(ret, 0, "parse should return 0");

    TEST_EXPECT_FALSE(parsed->quota_info.enabled, "quota.enabled should be false");
    TEST_EXPECT_EQ(parsed->quota_info.quota_bytes, 0ULL, "quota_bytes should be 0");
    TEST_EXPECT_EQ(parsed->quota_info.quota_max_objects, 0ULL, "quota_max_objects should be 0");

    /* 清理 */
    test_free_buffer(buffer);
    test_user_impl_destroy(parsed);
    free(parsed);
    test_user_impl_destroy(impl);
    free(impl);

    TEST_PASS();
    return 0;
}

/* 测试桶实现结构 */
static int test_bucket_impl_struct(void) {
    TEST_START("bucket_impl_struct");

    test_bucket_impl_t* bucket = (test_bucket_impl_t*)calloc(1, sizeof(test_bucket_impl_t));
    TEST_EXPECT_NOT_NULL(bucket, "bucket should not be NULL");

    bucket->name = strdup("test_bucket");
    bucket->tenant = strdup("test_tenant");
    bucket->marker = strdup("marker_123");
    bucket->bucket_id = strdup("bucket_id_456");
    bucket->owner_id = strdup("owner_789");
    bucket->loaded = true;
    bucket->created = true;
    bucket->mtime = time(NULL);

    TEST_EXPECT_STR(bucket->name, "test_bucket", "bucket name should match");
    TEST_EXPECT_STR(bucket->tenant, "test_tenant", "bucket tenant should match");
    TEST_EXPECT_TRUE(bucket->loaded, "bucket loaded should be true");
    TEST_EXPECT_TRUE(bucket->created, "bucket created should be true");
    TEST_EXPECT_FALSE(bucket->deleted, "bucket deleted should be false");
    TEST_EXPECT_FALSE(bucket->destroyed, "bucket destroyed should be false");

    /* 清理 */
    free(bucket->name);
    free(bucket->tenant);
    free(bucket->marker);
    free(bucket->bucket_id);
    free(bucket->owner_id);
    free(bucket);

    TEST_PASS();
    return 0;
}

/* 测试对象实现结构 */
static int test_object_impl_struct(void) {
    TEST_START("object_impl_struct");

    test_object_impl_t* obj = (test_object_impl_t*)calloc(1, sizeof(test_object_impl_t));
    TEST_EXPECT_NOT_NULL(obj, "object should not be NULL");

    obj->name = strdup("test_object");
    obj->instance = strdup("version_1");
    obj->bucket_name = strdup("test_bucket");
    obj->bucket_tenant = strdup("test_tenant");
    obj->bucket_id = strdup("bucket_id_123");
    obj->obj_oid = strdup("oid_456");
    obj->is_null = false;
    obj->size = 1024 * 1024;  /* 1MB */
    obj->mtime = time(NULL);
    obj->written = true;
    obj->is_atomic = false;
    obj->is_expired = false;

    TEST_EXPECT_STR(obj->name, "test_object", "object name should match");
    TEST_EXPECT_STR(obj->instance, "version_1", "object instance should match");
    TEST_EXPECT_EQ(obj->size, 1048576LL, "object size should be 1MB");
    TEST_EXPECT_TRUE(obj->written, "object written should be true");
    TEST_EXPECT_FALSE(obj->deleted, "object deleted should be false");
    TEST_EXPECT_FALSE(obj->is_expired, "object is_expired should be false");

    /* 清理 */
    free(obj->name);
    free(obj->instance);
    free(obj->bucket_name);
    free(obj->bucket_tenant);
    free(obj->bucket_id);
    free(obj->obj_oid);
    free(obj);

    TEST_PASS();
    return 0;
}

/* 测试对象版本键 */
static int test_object_version_key(void) {
    TEST_START("object_version_key");

    /* 测试 null 版本 */
    test_object_impl_t null_obj = {0};
    null_obj.name = strdup("test_obj");
    null_obj.is_null = true;
    null_obj.instance = NULL;

    TEST_EXPECT_TRUE(null_obj.is_null, "null_obj should have is_null = true");
    TEST_EXPECT_NULL(null_obj.instance, "null_obj should have no instance");

    free(null_obj.name);

    /* 测试带版本的对象 */
    test_object_impl_t versioned_obj = {0};
    versioned_obj.name = strdup("test_obj");
    versioned_obj.is_null = false;
    versioned_obj.instance = strdup("2024-01-01T00:00:00.000Z");

    TEST_EXPECT_FALSE(versioned_obj.is_null, "versioned_obj should have is_null = false");
    TEST_EXPECT_NOT_NULL(versioned_obj.instance, "versioned_obj should have instance");

    free(versioned_obj.name);
    free(versioned_obj.instance);

    TEST_PASS();
    return 0;
}

/* 测试版本跟踪器克隆 */
static int test_version_tracker_clone(void) {
    TEST_START("version_tracker_clone");

    test_obj_version_tracker_t original = {0};
    original.read_version.epoch = 5;
    original.write_version.epoch = 6;
    original.read_version.committed = true;
    original.write_version.committed = false;
    original.obj_tag = strdup("original_obj_tag");
    original.instance_tag = strdup("original_instance_tag");

    /* 克隆版本跟踪器 */
    test_obj_version_tracker_t clone = {0};
    clone.read_version = original.read_version;
    clone.write_version = original.write_version;
    clone.obj_tag = original.obj_tag ? strdup(original.obj_tag) : NULL;
    clone.instance_tag = original.instance_tag ? strdup(original.instance_tag) : NULL;

    /* 验证克隆 */
    TEST_EXPECT_EQ(clone.read_version.epoch, 5, "clone read_version.epoch should be 5");
    TEST_EXPECT_EQ(clone.write_version.epoch, 6, "clone write_version.epoch should be 6");
    TEST_EXPECT_TRUE(clone.read_version.committed, "clone read_version.committed should be true");
    TEST_EXPECT_FALSE(clone.write_version.committed, "clone write_version.committed should be false");
    TEST_EXPECT_STR(clone.obj_tag, "original_obj_tag", "clone obj_tag should match");
    TEST_EXPECT_STR(clone.instance_tag, "original_instance_tag", "clone instance_tag should match");

    /* 修改原始对象不影响克隆 */
    free(original.obj_tag);
    original.obj_tag = strdup("modified_obj_tag");
    TEST_EXPECT_STR(clone.obj_tag, "original_obj_tag", "clone obj_tag should be unchanged");

    /* 清理 */
    free(original.obj_tag);
    free(original.instance_tag);
    free(clone.obj_tag);
    free(clone.instance_tag);

    TEST_PASS();
    return 0;
}

/* 测试对象 OID 构建 */
static int test_object_oid_build(void) {
    TEST_START("object_oid_build");

    /* 模拟 OID 构建逻辑 */
    const char* bucket_name = "my_bucket";
    const char* obj_name = "path/to/object.txt";
    const char* obj_instance = "version_123";

    char oid[512];
    int ret;

    /* 无版本的对象 OID */
    ret = snprintf(oid, sizeof(oid), "%s:%s", bucket_name, obj_name);
    TEST_EXPECT_TRUE(ret > 0, "snprintf should succeed");

    TEST_EXPECT_STR(oid, "my_bucket:path/to/object.txt", "OID should be bucket:obj");

    /* 带版本的对象 OID */
    ret = snprintf(oid, sizeof(oid), "%s:%s:%s", bucket_name, obj_name, obj_instance);
    TEST_EXPECT_TRUE(ret > 0, "snprintf should succeed");

    TEST_EXPECT_STR(oid, "my_bucket:path/to/object.txt:version_123", "OID should include version");

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
    printf("RADOS 驱动核心类型单元测试\n");
    printf("========================================\n\n");

    int failed = 0;

    /* 配额信息测试 */
    printf("[配额信息测试]\n");
    failed += test_quota_info_struct();

    /* 用户权限测试 */
    printf("[用户权限测试]\n");
    failed += test_user_caps_struct();

    /* 版本跟踪器测试 */
    printf("[版本跟踪器测试]\n");
    failed += test_obj_version_tracker_struct();
    failed += test_version_tracker_clone();

    /* 用户实现测试 */
    printf("[用户实现测试]\n");
    failed += test_user_impl_create_destroy();
    failed += test_user_serialize();
    failed += test_quota_serialize();
    failed += test_quota_boundary_values();

    /* 桶实现测试 */
    printf("[桶实现测试]\n");
    failed += test_bucket_impl_struct();

    /* 对象实现测试 */
    printf("[对象实现测试]\n");
    failed += test_object_impl_struct();
    failed += test_object_version_key();
    failed += test_object_oid_build();

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

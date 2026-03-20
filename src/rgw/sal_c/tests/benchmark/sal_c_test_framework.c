/**
 * @file sal_c_test_framework.c
 * @brief SAL C 测试框架实现
 */

#include "sal_c_test_framework.h"
#include "rgw_sal_errors.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 测试上下文实现
 *============================================================================*/

sal_c_test_context_t* sal_c_test_context_create(void* cct) {
    sal_c_test_context_t* ctx = (sal_c_test_context_t*)
        malloc(sizeof(sal_c_test_context_t));
    if (!ctx) {
        return NULL;
    }

    ctx->cct = cct;
    ctx->dpp = NULL;
    ctx->driver = NULL;
    ctx->verbose = false;
    ctx->operations_count = 0;
    ctx->total_time_us = 0;

    return ctx;
}

void sal_c_test_context_destroy(sal_c_test_context_t* ctx) {
    if (!ctx) {
        return;
    }

    if (ctx->driver) {
        /* 销毁驱动 */
        if (ctx->driver) {
            /* TODO: 调用驱动销毁函数 */
        }
        ctx->driver = NULL;
    }

    free(ctx);
}

bool sal_c_test_init_driver(sal_c_test_context_t* ctx, const char* driver_type) {
    if (!ctx || !driver_type) {
        return false;
    }

    /* 通过 C SAL API 创建驱动 */
    ctx->driver = (void*)rgw_sal_create_driver(driver_type, ctx->cct);
    if (!ctx->driver) {
        if (ctx->verbose) {
            fprintf(stderr, "Failed to create driver: %s\n", driver_type);
        }
        return false;
    }

    /* 初始化驱动 */
    int ret = rgw_sal_init_driver((rgw_sal_driver_t*)ctx->driver,
                                   ctx->cct,
                                   (const rgw_sal_dpp_t*)ctx->dpp);
    if (ret != 0) {
        if (ctx->verbose) {
            fprintf(stderr, "Failed to initialize driver: %d\n", ret);
        }
        rgw_sal_destroy_driver((rgw_sal_driver_t*)ctx->driver);
        ctx->driver = NULL;
        return false;
    }

    if (ctx->verbose) {
        const char* name = rgw_sal_get_driver_name((rgw_sal_driver_t*)ctx->driver);
        fprintf(stdout, "Driver initialized: %s\n", name ? name : "unknown");
    }

    return true;
}

void sal_c_test_set_verbose(sal_c_test_context_t* ctx, bool verbose) {
    if (ctx) {
        ctx->verbose = verbose;
    }
}

/*============================================================================
 * 用户操作测试实现
 *============================================================================*/

int sal_c_test_user_create(sal_c_test_context_t* ctx,
                           const sal_c_test_user_input_t* input,
                           sal_c_test_user_output_t* output) {
    if (!ctx || !input || !output) {
        return -EINVAL;
    }

    /* 准备用户信息 */
    rgw_sal_user_id_t uid = {};
    uid.id = (char*)input->user_id;
    uid.tenant = (char*)input->tenant;
    uid.type = input->user_type;

    /* 获取用户对象 */
    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user(driver, &uid);
    if (!user) {
        output->result = -ENOMEM;
        return output->result;
    }

    /* 设置用户属性 */
    if (input->display_name) {
        driver->vtable->get_user_by_id(driver, &uid, user);
        driver->vtable->set_display_name(user, input->display_name);
    }

    /* 存储用户 */
    int ret = driver->vtable->store_user(driver, user,
                                          (const rgw_sal_dpp_t*)ctx->dpp,
                                          NULL, false);

    output->result = ret;
    if (ret == 0) {
        output->user_id = input->user_id;
        output->display_name = input->display_name;
        output->tenant = input->tenant;
    }

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return ret;
}

int sal_c_test_user_get(sal_c_test_context_t* ctx,
                        const char* user_id,
                        sal_c_test_user_output_t* output) {
    if (!ctx || !user_id || !output) {
        return -EINVAL;
    }

    rgw_sal_user_id_t uid = {};
    uid.id = (char*)user_id;

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user(driver, &uid);
    if (!user) {
        output->result = -ENOENT;
        return output->result;
    }

    /* 加载用户 */
    int ret = driver->vtable->load_user((const rgw_sal_dpp_t*)ctx->dpp, user, NULL);
    output->result = ret;

    if (ret == 0) {
        output->user_id = user->vtable->get_id(user);
        output->display_name = user->vtable->get_display_name(user);
        output->tenant = user->vtable->get_tenant(user);
    }

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return ret;
}

int sal_c_test_user_get_by_access_key(sal_c_test_context_t* ctx,
                                      const char* access_key,
                                      sal_c_test_user_output_t* output) {
    if (!ctx || !access_key || !output) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user_by_access_key(
        driver, access_key, (const rgw_sal_dpp_t*)ctx->dpp);

    if (!user) {
        output->result = -ENOENT;
        return output->result;
    }

    output->result = 0;
    output->user_id = user->vtable->get_id(user);
    output->display_name = user->vtable->get_display_name(user);
    output->tenant = user->vtable->get_tenant(user);

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return 0;
}

int sal_c_test_user_get_by_email(sal_c_test_context_t* ctx,
                                  const char* email,
                                  sal_c_test_user_output_t* output) {
    if (!ctx || !email || !output) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user_by_email(
        driver, email, (const rgw_sal_dpp_t*)ctx->dpp);

    if (!user) {
        output->result = -ENOENT;
        return output->result;
    }

    output->result = 0;
    output->user_id = user->vtable->get_id(user);
    output->display_name = user->vtable->get_display_name(user);
    output->tenant = user->vtable->get_tenant(user);

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return 0;
}

int sal_c_test_user_update(sal_c_test_context_t* ctx,
                           const char* user_id,
                           const char* new_display_name) {
    if (!ctx || !user_id || !new_display_name) {
        return -EINVAL;
    }

    rgw_sal_user_id_t uid = {};
    uid.id = (char*)user_id;

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user(driver, &uid);
    if (!user) {
        return -ENOENT;
    }

    /* 设置新显示名称 */
    int ret = driver->vtable->set_display_name(user, new_display_name);
    if (ret != 0) {
        if (user->vtable && user->vtable->destroy) {
            user->vtable->destroy(user);
        }
        return ret;
    }

    /* 存储用户 */
    ret = driver->vtable->store_user(driver, user,
                                     (const rgw_sal_dpp_t*)ctx->dpp,
                                     NULL, false);

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return ret;
}

int sal_c_test_user_delete(sal_c_test_context_t* ctx,
                           const char* user_id) {
    if (!ctx || !user_id) {
        return -EINVAL;
    }

    rgw_sal_user_id_t uid = {};
    uid.id = (char*)user_id;

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;
    rgw_sal_user_t* user = driver->vtable->get_user(driver, &uid);
    if (!user) {
        return -ENOENT;
    }

    int ret = driver->vtable->remove_user(user,
                                           (const rgw_sal_dpp_t*)ctx->dpp,
                                           NULL);

    /* 销毁用户对象 */
    if (user->vtable && user->vtable->destroy) {
        user->vtable->destroy(user);
    }

    return ret;
}

/*============================================================================
 * 桶操作测试实现
 *============================================================================*/

int sal_c_test_bucket_create(sal_c_test_context_t* ctx,
                             const sal_c_test_bucket_input_t* input,
                             sal_c_test_bucket_output_t* output) {
    if (!ctx || !input || !output) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    /* 准备桶信息 */
    rgw_sal_bucket_info_t info = {};
    info.bucket.name = (char*)input->bucket_name;
    info.bucket.tenant = (char*)input->tenant;

    /* 获取桶对象 */
    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &info);
    if (!bucket) {
        output->result = -ENOMEM;
        return output->result;
    }

    /* 设置所有者 */
    if (input->owner_user_id) {
        rgw_sal_user_id_t uid = {};
        uid.id = (char*)input->owner_user_id;
        rgw_sal_user_t* owner = driver->vtable->get_user(driver, &uid);
        if (owner) {
            driver->vtable->set_owner(bucket, owner);
            if (owner->vtable && owner->vtable->destroy) {
                owner->vtable->destroy(owner);
            }
        }
    }

    /* 存储桶 */
    int ret = driver->vtable->store_bucket(driver, bucket,
                                            (const rgw_sal_dpp_t*)ctx->dpp,
                                            NULL, false);

    output->result = ret;
    if (ret == 0) {
        output->bucket_name = input->bucket_name;
        output->tenant = input->tenant;
        output->owner_user_id = input->owner_user_id;
    }

    /* 销毁桶对象 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return ret;
}

int sal_c_test_bucket_get(sal_c_test_context_t* ctx,
                          const char* bucket_name,
                          sal_c_test_bucket_output_t* output) {
    if (!ctx || !bucket_name || !output) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    rgw_sal_bucket_info_t info = {};
    info.bucket.name = (char*)bucket_name;

    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &info);
    if (!bucket) {
        output->result = -ENOENT;
        return output->result;
    }

    /* 加载桶 */
    int ret = driver->vtable->load_bucket(bucket,
                                           (const rgw_sal_dpp_t*)ctx->dpp,
                                           NULL);
    output->result = ret;

    if (ret == 0) {
        output->bucket_name = bucket->vtable->get_name(bucket);
        output->tenant = bucket->vtable->get_tenant(bucket);
        output->marker = bucket->vtable->get_marker(bucket);
        output->bucket_id = bucket->vtable->get_bucket_id(bucket);
    }

    /* 销毁桶对象 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return ret;
}

int sal_c_test_bucket_list(sal_c_test_context_t* ctx,
                           const char* owner_user_id,
                           sal_c_test_bucket_output_t** buckets,
                           size_t* count,
                           size_t max) {
    if (!ctx || !owner_user_id || !buckets || !count) {
        return -EINVAL;
    }

    *buckets = (sal_c_test_bucket_output_t*)calloc(max, sizeof(sal_c_test_bucket_output_t));
    if (!*buckets) {
        return -ENOMEM;
    }

    *count = 0;

    /* TODO: 实现桶列表 */
    /* 需要调用 driver->vtable->list_buckets */

    return 0;
}

int sal_c_test_bucket_delete(sal_c_test_context_t* ctx,
                              const char* bucket_name,
                              bool delete_objects) {
    if (!ctx || !bucket_name) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    rgw_sal_bucket_info_t info = {};
    info.bucket.name = (char*)bucket_name;

    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &info);
    if (!bucket) {
        return -ENOENT;
    }

    int ret = driver->vtable->remove_bucket(bucket,
                                            (const rgw_sal_dpp_t*)ctx->dpp,
                                            NULL, delete_objects);

    /* 销毁桶对象 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return ret;
}

/*============================================================================
 * 对象操作测试实现
 *============================================================================*/

int sal_c_test_object_create(sal_c_test_context_t* ctx,
                             const sal_c_test_object_input_t* input,
                             sal_c_test_object_output_t* output) {
    if (!ctx || !input || !output) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    /* 获取桶 */
    rgw_sal_bucket_info_t bkt_info = {};
    bkt_info.bucket.name = (char*)input->bucket_name;
    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &bkt_info);
    if (!bucket) {
        output->result = -ENOENT;
        return output->result;
    }

    /* 获取对象 */
    rgw_sal_obj_key_t key = {};
    key.name = (char*)input->object_name;
    rgw_sal_object_t* obj = driver->vtable->get_object(driver, bucket, &key);
    if (!obj) {
        if (bucket->vtable && bucket->vtable->destroy) {
            bucket->vtable->destroy(bucket);
        }
        output->result = -ENOMEM;
        return output->result;
    }

    /* 写入数据 */
    if (input->data && input->data_size > 0) {
        output->result = driver->vtable->put_obj(obj,
                                                 (const uint8_t*)input->data,
                                                 input->data_size,
                                                 (const rgw_sal_dpp_t*)ctx->dpp,
                                                 NULL);
    } else {
        output->result = 0;
    }

    output->object_name = input->object_name;

    /* 销毁对象 */
    if (obj->vtable && obj->vtable->destroy) {
        obj->vtable->destroy(obj);
    }
    /* 销毁桶 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return output->result;
}

int sal_c_test_object_read(sal_c_test_context_t* ctx,
                           const char* bucket_name,
                           const char* object_name,
                           char* data,
                           size_t data_size,
                           size_t* bytes_read) {
    if (!ctx || !bucket_name || !object_name || !data || !bytes_read) {
        return -EINVAL;
    }

    *bytes_read = 0;

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    /* 获取桶 */
    rgw_sal_bucket_info_t bkt_info = {};
    bkt_info.bucket.name = (char*)bucket_name;
    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &bkt_info);
    if (!bucket) {
        return -ENOENT;
    }

    /* 获取对象 */
    rgw_sal_obj_key_t key = {};
    key.name = (char*)object_name;
    rgw_sal_object_t* obj = driver->vtable->get_object(driver, bucket, &key);
    if (!obj) {
        if (bucket->vtable && bucket->vtable->destroy) {
            bucket->vtable->destroy(bucket);
        }
        return -ENOENT;
    }

    /* 读取数据 */
    int ret = driver->vtable->read_obj(obj, 0, input->data_size - 1,
                                        (uint8_t*)data, bytes_read,
                                        (const rgw_sal_dpp_t*)ctx->dpp,
                                        NULL);

    /* 销毁对象 */
    if (obj->vtable && obj->vtable->destroy) {
        obj->vtable->destroy(obj);
    }
    /* 销毁桶 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return ret;
}

int sal_c_test_object_list(sal_c_test_context_t* ctx,
                           const char* bucket_name,
                           const char* prefix,
                           const char* marker,
                           size_t max_keys,
                           sal_c_test_object_output_t** objects,
                           size_t* count) {
    if (!ctx || !bucket_name || !objects || !count) {
        return -EINVAL;
    }

    *objects = (sal_c_test_object_output_t*)calloc(max_keys, sizeof(sal_c_test_object_output_t));
    if (!*objects) {
        return -ENOMEM;
    }

    *count = 0;

    /* TODO: 实现对象列表 */
    /* 需要调用 bucket->vtable->list_objects */

    return 0;
}

int sal_c_test_object_delete(sal_c_test_context_t* ctx,
                             const char* bucket_name,
                             const char* object_name) {
    if (!ctx || !bucket_name || !object_name) {
        return -EINVAL;
    }

    rgw_sal_driver_t* driver = (rgw_sal_driver_t*)ctx->driver;

    /* 获取桶 */
    rgw_sal_bucket_info_t bkt_info = {};
    bkt_info.bucket.name = (char*)bucket_name;
    rgw_sal_bucket_t* bucket = driver->vtable->get_bucket(driver, &bkt_info);
    if (!bucket) {
        return -ENOENT;
    }

    /* 获取对象 */
    rgw_sal_obj_key_t key = {};
    key.name = (char*)object_name;
    rgw_sal_object_t* obj = driver->vtable->get_object(driver, bucket, &key);
    if (!obj) {
        if (bucket->vtable && bucket->vtable->destroy) {
            bucket->vtable->destroy(bucket);
        }
        return -ENOENT;
    }

    /* 删除对象 */
    int ret = driver->vtable->delete_obj(obj,
                                          (const rgw_sal_dpp_t*)ctx->dpp,
                                          NULL, 0);

    /* 销毁对象 */
    if (obj->vtable && obj->vtable->destroy) {
        obj->vtable->destroy(obj);
    }
    /* 销毁桶 */
    if (bucket->vtable && bucket->vtable->destroy) {
        bucket->vtable->destroy(bucket);
    }

    return ret;
}

/*============================================================================
 * 性能测试实现
 *============================================================================*/

static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

void sal_c_test_user_throughput(sal_c_test_context_t* ctx,
                                size_t iterations,
                                sal_c_test_perf_result_t* result) {
    if (!ctx || !result) {
        return;
    }

    memset(result, 0, sizeof(sal_c_test_perf_result_t));

    uint64_t start_time = get_time_us();
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;

    char user_id[64];
    for (size_t i = 0; i < iterations; i++) {
        snprintf(user_id, sizeof(user_id), "test_user_%zu", i);

        uint64_t op_start = get_time_us();

        sal_c_test_user_input_t input = {};
        input.user_id = user_id;
        input.display_name = "Test User";

        sal_c_test_user_output_t output = {};
        sal_c_test_user_create(ctx, &input, &output);

        uint64_t op_end = get_time_us();
        uint64_t op_time = op_end - op_start;

        result->iterations++;

        if (op_time < min_time) min_time = op_time;
        if (op_time > max_time) max_time = op_time;
        result->total_time_us += op_time;

        /* 清理 */
        sal_c_test_user_delete(ctx, user_id);
    }

    uint64_t end_time = get_time_us();
    result->total_time_us = end_time - start_time;
    result->min_time_us = min_time == UINT64_MAX ? 0 : min_time;
    result->max_time_us = max_time;
    result->avg_time_us = (double)result->total_time_us / iterations;
    result->throughput = (double)iterations * 1000000.0 / result->total_time_us;
}

void sal_c_test_bucket_throughput(sal_c_test_context_t* ctx,
                                  size_t iterations,
                                  sal_c_test_perf_result_t* result) {
    if (!ctx || !result) {
        return;
    }

    memset(result, 0, sizeof(sal_c_test_perf_result_t));

    uint64_t start_time = get_time_us();
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;

    char bucket_name[64];
    for (size_t i = 0; i < iterations; i++) {
        snprintf(bucket_name, sizeof(bucket_name), "test_bucket_%zu", i);

        uint64_t op_start = get_time_us();

        sal_c_test_bucket_input_t input = {};
        input.bucket_name = bucket_name;
        input.owner_user_id = "test_owner";

        sal_c_test_bucket_output_t output = {};
        sal_c_test_bucket_create(ctx, &input, &output);

        uint64_t op_end = get_time_us();
        uint64_t op_time = op_end - op_start;

        result->iterations++;

        if (op_time < min_time) min_time = op_time;
        if (op_time > max_time) max_time = op_time;
        result->total_time_us += op_time;

        /* 清理 */
        sal_c_test_bucket_delete(ctx, bucket_name, true);
    }

    uint64_t end_time = get_time_us();
    result->total_time_us = end_time - start_time;
    result->min_time_us = min_time == UINT64_MAX ? 0 : min_time;
    result->max_time_us = max_time;
    result->avg_time_us = (double)result->total_time_us / iterations;
    result->throughput = (double)iterations * 1000000.0 / result->total_time_us;
}

void sal_c_test_object_throughput(sal_c_test_context_t* ctx,
                                  size_t iterations,
                                  size_t object_size,
                                  sal_c_test_perf_result_t* result) {
    if (!ctx || !result) {
        return;
    }

    memset(result, 0, sizeof(sal_c_test_perf_result_t));

    /* 先创建一个测试桶 */
    sal_c_test_bucket_input_t bkt_input = {};
    bkt_input.bucket_name = "perf_test_bucket";
    bkt_input.owner_user_id = "test_owner";

    sal_c_test_bucket_output_t bkt_output = {};
    sal_c_test_bucket_create(ctx, &bkt_input, &bkt_output);

    /* 准备测试数据 */
    char* test_data = (char*)malloc(object_size);
    if (!test_data) {
        return;
    }
    memset(test_data, 'A', object_size);

    uint64_t start_time = get_time_us();
    uint64_t min_time = UINT64_MAX;
    uint64_t max_time = 0;

    char object_name[64];
    for (size_t i = 0; i < iterations; i++) {
        snprintf(object_name, sizeof(object_name), "test_object_%zu", i);

        uint64_t op_start = get_time_us();

        sal_c_test_object_input_t input = {};
        input.bucket_name = "perf_test_bucket";
        input.object_name = object_name;
        input.data = test_data;
        input.data_size = object_size;

        sal_c_test_object_output_t output = {};
        sal_c_test_object_create(ctx, &input, &output);

        uint64_t op_end = get_time_us();
        uint64_t op_time = op_end - op_start;

        result->iterations++;

        if (op_time < min_time) min_time = op_time;
        if (op_time > max_time) max_time = op_time;
        result->total_time_us += op_time;

        /* 清理 */
        sal_c_test_object_delete(ctx, "perf_test_bucket", object_name);
    }

    uint64_t end_time = get_time_us();
    result->total_time_us = end_time - start_time;
    result->min_time_us = min_time == UINT64_MAX ? 0 : min_time;
    result->max_time_us = max_time;
    result->avg_time_us = (double)result->total_time_us / iterations;
    result->throughput = (double)iterations * 1000000.0 / result->total_time_us;

    /* 清理 */
    free(test_data);
    sal_c_test_bucket_delete(ctx, "perf_test_bucket", true);
}

void sal_c_test_user_latency(sal_c_test_context_t* ctx,
                             size_t iterations,
                             sal_c_test_latency_percentile_t* result) {
    if (!ctx || !result) {
        return;
    }

    memset(result, 0, sizeof(sal_c_test_latency_percentile_t));

    /* 分配延迟数组 */
    uint64_t* latencies = (uint64_t*)malloc(iterations * sizeof(uint64_t));
    if (!latencies) {
        return;
    }

    /* 收集延迟数据 */
    for (size_t i = 0; i < iterations; i++) {
        char user_id[64];
        snprintf(user_id, sizeof(user_id), "test_user_%zu", i);

        uint64_t op_start = get_time_us();

        sal_c_test_user_input_t input = {};
        input.user_id = user_id;
        input.display_name = "Test User";

        sal_c_test_user_output_t output = {};
        sal_c_test_user_create(ctx, &input, &output);

        uint64_t op_end = get_time_us();
        latencies[i] = op_end - op_start;

        /* 清理 */
        sal_c_test_user_delete(ctx, user_id);
    }

    /* 排序并计算百分位 */
    /* 简单的选择排序用于演示 */
    for (size_t i = 0; i < iterations; i++) {
        for (size_t j = i + 1; j < iterations; j++) {
            if (latencies[j] < latencies[i]) {
                uint64_t tmp = latencies[i];
                latencies[i] = latencies[j];
                latencies[j] = tmp;
            }
        }
    }

    size_t p50_idx = (size_t)(iterations * 0.50);
    size_t p95_idx = (size_t)(iterations * 0.95);
    size_t p99_idx = (size_t)(iterations * 0.99);
    size_t p999_idx = (size_t)(iterations * 0.999);

    result->p50_us = (double)latencies[p50_idx];
    result->p95_us = (double)latencies[p95_idx];
    result->p99_us = (double)latencies[p99_idx];
    result->p999_us = (double)latencies[p999_idx];

    free(latencies);
}

void sal_c_test_bucket_latency(sal_c_test_context_t* ctx,
                               size_t iterations,
                               sal_c_test_latency_percentile_t* result) {
    /* 类似用户延迟测试 */
    memset(result, 0, sizeof(sal_c_test_latency_percentile_t));
    /* TODO: 实现 */
}

void sal_c_test_object_latency(sal_c_test_context_t* ctx,
                               size_t iterations,
                               size_t object_size,
                               sal_c_test_latency_percentile_t* result) {
    /* 类似用户延迟测试 */
    memset(result, 0, sizeof(sal_c_test_latency_percentile_t));
    /* TODO: 实现 */
}

/*============================================================================
 * 内存测试实现
 *============================================================================*/

static sal_c_test_memory_stats_t g_memory_stats = {};

void sal_c_test_get_memory_stats(sal_c_test_memory_stats_t* stats) {
    if (stats) {
        *stats = g_memory_stats;
    }
}

void sal_c_test_reset_memory_stats(void) {
    memset(&g_memory_stats, 0, sizeof(sal_c_test_memory_stats_t));
}

/*============================================================================
 * 测试报告实现
 *============================================================================*/

size_t sal_c_test_run_suite(sal_c_test_context_t* ctx,
                            sal_c_test_suite_t* suite) {
    if (!ctx || !suite) {
        return 0;
    }

    suite->passed = 0;
    suite->failed = 0;
    suite->skipped = 0;

    for (size_t i = 0; i < suite->case_count; i++) {
        sal_c_test_case_t* test_case = &suite->cases[i];

        if (!test_case->func) {
            test_case->result = SAL_C_TEST_SKIP;
            suite->skipped++;
            continue;
        }

        test_case->result = test_case->func(ctx);

        switch (test_case->result) {
            case SAL_C_TEST_PASS:
                suite->passed++;
                break;
            case SAL_C_TEST_FAIL:
                suite->failed++;
                break;
            case SAL_C_TEST_SKIP:
                suite->skipped++;
                break;
        }
    }

    return suite->passed;
}

void sal_c_test_print_report(const sal_c_test_suite_t* suite,
                             bool verbose) {
    if (!suite) {
        return;
    }

    printf("\n========================================\n");
    printf("Test Suite: %s\n", suite->name);
    printf("Description: %s\n", suite->description ? suite->description : "N/A");
    printf("========================================\n");
    printf("Total: %zu | Passed: %zu | Failed: %zu | Skipped: %zu\n",
           suite->case_count, suite->passed, suite->failed, suite->skipped);
    printf("----------------------------------------\n");

    if (verbose) {
        for (size_t i = 0; i < suite->case_count; i++) {
            const sal_c_test_case_t* test_case = &suite->cases[i];

            const char* status;
            switch (test_case->result) {
                case SAL_C_TEST_PASS:
                    status = "[PASS]";
                    break;
                case SAL_C_TEST_FAIL:
                    status = "[FAIL]";
                    break;
                case SAL_C_TEST_SKIP:
                    status = "[SKIP]";
                    break;
                default:
                    status = "[????]";
                    break;
            }

            printf("%s %s\n", status, test_case->name);
            if (test_case->result == SAL_C_TEST_FAIL && test_case->error_message) {
                printf("       Error: %s\n", test_case->error_message);
            }
        }
    }

    printf("========================================\n\n");
}

#ifdef __cplusplus
}
#endif

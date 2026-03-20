/**
 * @file sal_c_test_framework.h
 * @brief SAL C 测试框架
 *
 * 提供用于测试 SAL C 实现的工具函数和数据结构。
 */

#ifndef __CEPH_RGW_SAL_C_TEST_FRAMEWORK_H
#define __CEPH_RGW_SAL_C_TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "rgw_sal.h"
#include "rgw_sal_rados.h"
#include "rgw_sal_dbstore.h"

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <string>
#include <vector>
#include <chrono>

namespace ceph {
namespace sal_c {
namespace test {
#endif

/*============================================================================
 * 测试上下文
 *============================================================================*/

/**
 * @brief 测试上下文结构
 */
typedef struct sal_c_test_context {
    void* cct;
    void* dpp;
    void* driver;
    bool verbose;
    size_t operations_count;
    uint64_t total_time_us;
} sal_c_test_context_t;

/**
 * @brief 创建测试上下文
 * @param cct Ceph 上下文
 * @return 测试上下文指针，失败返回 NULL
 */
sal_c_test_context_t* sal_c_test_context_create(void* cct);

/**
 * @brief 销毁测试上下文
 * @param ctx 测试上下文
 */
void sal_c_test_context_destroy(sal_c_test_context_t* ctx);

/**
 * @brief 初始化驱动
 * @param ctx 测试上下文
 * @param driver_type 驱动类型 ("rados", "dbstore", "posix")
 * @return 成功返回 true
 */
bool sal_c_test_init_driver(sal_c_test_context_t* ctx, const char* driver_type);

/**
 * @brief 设置 verbose 模式
 * @param ctx 测试上下文
 * @param verbose 是否 verbose
 */
void sal_c_test_set_verbose(sal_c_test_context_t* ctx, bool verbose);

/*============================================================================
 * 用户操作测试
 *============================================================================*/

/**
 * @brief 用户创建输入
 */
typedef struct sal_c_test_user_input {
    const char* user_id;
    const char* display_name;
    const char* tenant;
    const char* email;
    const char* access_key;
    const char* secret_key;
    uint32_t user_type;
} sal_c_test_user_input_t;

/**
 * @brief 用户创建输出
 */
typedef struct sal_c_test_user_output {
    int result;
    const char* user_id;
    const char* display_name;
    const char* tenant;
    int32_t max_buckets;
    bool suspended;
    uint64_t op_mask;
} sal_c_test_user_output_t;

/**
 * @brief 测试用户创建
 * @param ctx 测试上下文
 * @param input 用户输入
 * @param output 用户输出
 * @return 成功返回 0
 */
int sal_c_test_user_create(sal_c_test_context_t* ctx,
                           const sal_c_test_user_input_t* input,
                           sal_c_test_user_output_t* output);

/**
 * @brief 测试用户获取（通过 ID）
 * @param ctx 测试上下文
 * @param user_id 用户 ID
 * @param output 用户输出
 * @return 成功返回 0
 */
int sal_c_test_user_get(sal_c_test_context_t* ctx,
                        const char* user_id,
                        sal_c_test_user_output_t* output);

/**
 * @brief 测试用户获取（通过 Access Key）
 * @param ctx 测试上下文
 * @param access_key Access Key
 * @param output 用户输出
 * @return 成功返回 0
 */
int sal_c_test_user_get_by_access_key(sal_c_test_context_t* ctx,
                                      const char* access_key,
                                      sal_c_test_user_output_t* output);

/**
 * @brief 测试用户获取（通过 Email）
 * @param ctx 测试上下文
 * @param email 邮箱
 * @param output 用户输出
 * @return 成功返回 0
 */
int sal_c_test_user_get_by_email(sal_c_test_context_t* ctx,
                                  const char* email,
                                  sal_c_test_user_output_t* output);

/**
 * @brief 测试用户更新
 * @param ctx 测试上下文
 * @param user_id 用户 ID
 * @param new_display_name 新的显示名称
 * @return 成功返回 0
 */
int sal_c_test_user_update(sal_c_test_context_t* ctx,
                           const char* user_id,
                           const char* new_display_name);

/**
 * @brief 测试用户删除
 * @param ctx 测试上下文
 * @param user_id 用户 ID
 * @return 成功返回 0
 */
int sal_c_test_user_delete(sal_c_test_context_t* ctx,
                           const char* user_id);

/*============================================================================
 * 桶操作测试
 *============================================================================*/

/**
 * @brief 桶创建输入
 */
typedef struct sal_c_test_bucket_input {
    const char* bucket_name;
    const char* owner_user_id;
    const char* zonegroup;
    const char* placement_rule;
} sal_c_test_bucket_input_t;

/**
 * @brief 桶创建输出
 */
typedef struct sal_c_test_bucket_output {
    int result;
    const char* bucket_name;
    const char* tenant;
    const char* marker;
    const char* bucket_id;
    const char* owner_user_id;
    uint64_t num_objects;
    uint64_t size_bytes;
} sal_c_test_bucket_output_t;

/**
 * @brief 测试桶创建
 * @param ctx 测试上下文
 * @param input 桶输入
 * @param output 桶输出
 * @return 成功返回 0
 */
int sal_c_test_bucket_create(sal_c_test_context_t* ctx,
                             const sal_c_test_bucket_input_t* input,
                             sal_c_test_bucket_output_t* output);

/**
 * @brief 测试桶获取
 * @param ctx 测试上下文
 * @param bucket_name 桶名称
 * @param output 桶输出
 * @return 成功返回 0
 */
int sal_c_test_bucket_get(sal_c_test_context_t* ctx,
                          const char* bucket_name,
                          sal_c_test_bucket_output_t* output);

/**
 * @brief 测试桶列表
 * @param ctx 测试上下文
 * @param owner_user_id 所有者用户 ID
 * @param buckets 输出桶列表
 * @param max 最大数量
 * @return 成功返回 0
 */
int sal_c_test_bucket_list(sal_c_test_context_t* ctx,
                           const char* owner_user_id,
                           sal_c_test_bucket_output_t** buckets,
                           size_t* count,
                           size_t max);

/**
 * @brief 测试桶删除
 * @param ctx 测试上下文
 * @param bucket_name 桶名称
 * @param delete_objects 是否删除对象
 * @return 成功返回 0
 */
int sal_c_test_bucket_delete(sal_c_test_context_t* ctx,
                              const char* bucket_name,
                              bool delete_objects);

/*============================================================================
 * 对象操作测试
 *============================================================================*/

/**
 * @brief 对象创建输入
 */
typedef struct sal_c_test_object_input {
    const char* bucket_name;
    const char* object_name;
    const char* data;
    size_t data_size;
    const char* content_type;
} sal_c_test_object_input_t;

/**
 * @brief 对象创建输出
 */
typedef struct sal_c_test_object_output {
    int result;
    const char* object_name;
    const char* instance;
    uint64_t size_bytes;
    const char* etag;
} sal_c_test_object_output_t;

/**
 * @brief 测试对象创建
 * @param ctx 测试上下文
 * @param input 对象输入
 * @param output 对象输出
 * @return 成功返回 0
 */
int sal_c_test_object_create(sal_c_test_context_t* ctx,
                             const sal_c_test_object_input_t* input,
                             sal_c_test_object_output_t* output);

/**
 * @brief 测试对象读取
 * @param ctx 测试上下文
 * @param bucket_name 桶名称
 * @param object_name 对象名称
 * @param data 输出数据缓冲区
 * @param data_size 缓冲区大小
 * @param bytes_read 实际读取的字节数
 * @return 成功返回 0
 */
int sal_c_test_object_read(sal_c_test_context_t* ctx,
                           const char* bucket_name,
                           const char* object_name,
                           char* data,
                           size_t data_size,
                           size_t* bytes_read);

/**
 * @brief 测试对象列表
 * @param ctx 测试上下文
 * @param bucket_name 桶名称
 * @param prefix 前缀过滤
 * @param marker 标记
 * @param max_keys 最大数量
 * @param objects 输出对象列表
 * @param count 输出对象数量
 * @return 成功返回 0
 */
int sal_c_test_object_list(sal_c_test_context_t* ctx,
                           const char* bucket_name,
                           const char* prefix,
                           const char* marker,
                           size_t max_keys,
                           sal_c_test_object_output_t** objects,
                           size_t* count);

/**
 * @brief 测试对象删除
 * @param ctx 测试上下文
 * @param bucket_name 桶名称
 * @param object_name 对象名称
 * @return 成功返回 0
 */
int sal_c_test_object_delete(sal_c_test_context_t* ctx,
                             const char* bucket_name,
                             const char* object_name);

/*============================================================================
 * 性能测试
 *============================================================================*/

/**
 * @brief 性能测试结果
 */
typedef struct sal_c_test_perf_result {
    size_t iterations;
    uint64_t total_time_us;
    uint64_t min_time_us;
    uint64_t max_time_us;
    double avg_time_us;
    double stddev_us;
    double throughput;  /* ops/sec */
} sal_c_test_perf_result_t;

/**
 * @brief 延迟百分位
 */
typedef struct sal_c_test_latency_percentile {
    double p50_us;
    double p95_us;
    double p99_us;
    double p999_us;
} sal_c_test_latency_percentile_t;

/**
 * @brief 测试用户操作吞吐量
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param result 输出结果
 */
void sal_c_test_user_throughput(sal_c_test_context_t* ctx,
                                 size_t iterations,
                                 sal_c_test_perf_result_t* result);

/**
 * @brief 测试桶操作吞吐量
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param result 输出结果
 */
void sal_c_test_bucket_throughput(sal_c_test_context_t* ctx,
                                   size_t iterations,
                                   sal_c_test_perf_result_t* result);

/**
 * @brief 测试对象操作吞吐量
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param object_size 对象大小
 * @param result 输出结果
 */
void sal_c_test_object_throughput(sal_c_test_context_t* ctx,
                                   size_t iterations,
                                   size_t object_size,
                                   sal_c_test_perf_result_t* result);

/**
 * @brief 测试用户操作延迟
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param result 输出延迟百分位
 */
void sal_c_test_user_latency(sal_c_test_context_t* ctx,
                              size_t iterations,
                              sal_c_test_latency_percentile_t* result);

/**
 * @brief 测试桶操作延迟
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param result 输出延迟百分位
 */
void sal_c_test_bucket_latency(sal_c_test_context_t* ctx,
                                size_t iterations,
                                sal_c_test_latency_percentile_t* result);

/**
 * @brief 测试对象操作延迟
 * @param ctx 测试上下文
 * @param iterations 迭代次数
 * @param object_size 对象大小
 * @param result 输出延迟百分位
 */
void sal_c_test_object_latency(sal_c_test_context_t* ctx,
                                size_t iterations,
                                size_t object_size,
                                sal_c_test_latency_percentile_t* result);

/*============================================================================
 * 内存测试
 *============================================================================*/

/**
 * @brief 内存使用统计
 */
typedef struct sal_c_test_memory_stats {
    size_t current_bytes;
    size_t peak_bytes;
    size_t allocations;
    size_t deallocations;
} sal_c_test_memory_stats_t;

/**
 * @brief 获取当前内存使用统计
 * @param stats 输出统计
 */
void sal_c_test_get_memory_stats(sal_c_test_memory_stats_t* stats);

/**
 * @brief 重置内存统计
 */
void sal_c_test_reset_memory_stats(void);

/*============================================================================
 * 测试报告
 *============================================================================*/

/**
 * @brief 测试结果
 */
typedef enum {
    SAL_C_TEST_PASS = 0,
    SAL_C_TEST_FAIL = 1,
    SAL_C_TEST_SKIP = 2
} sal_c_test_result_t;

/**
 * @brief 单个测试用例
 */
typedef struct sal_c_test_case {
    const char* name;
    const char* description;
    sal_c_test_result_t (*func)(sal_c_test_context_t* ctx);
    sal_c_test_result_t result;
    const char* error_message;
} sal_c_test_case_t;

/**
 * @brief 测试套件
 */
typedef struct sal_c_test_suite {
    const char* name;
    const char* description;
    sal_c_test_case_t* cases;
    size_t case_count;
    size_t passed;
    size_t failed;
    size_t skipped;
} sal_c_test_suite_t;

/**
 * @brief 运行测试套件
 * @param ctx 测试上下文
 * @param suite 测试套件
 * @return 通过的测试数量
 */
size_t sal_c_test_run_suite(sal_c_test_context_t* ctx,
                            sal_c_test_suite_t* suite);

/**
 * @brief 打印测试报告
 * @param suite 测试套件
 * @param verbose 是否详细输出
 */
void sal_c_test_print_report(const sal_c_test_suite_t* suite,
                              bool verbose);

/*============================================================================
 * C++ 包装器
 *============================================================================*/

#ifdef __cplusplus
} // namespace test

/**
 * @brief C++ 测试上下文包装器
 */
class TestContext {
public:
    TestContext(void* cct);
    ~TestContext();

    bool init_driver(const std::string& driver_type);
    void set_verbose(bool verbose);

    /* 用户测试 */
    bool test_user_create(const std::string& user_id,
                          const std::string& display_name,
                          const std::string& tenant = "",
                          const std::string& email = "",
                          const std::string& access_key = "",
                          const std::string& secret_key = "");

    bool test_user_get(const std::string& user_id);

    bool test_user_get_by_access_key(const std::string& access_key);

    bool test_user_get_by_email(const std::string& email);

    bool test_user_update(const std::string& user_id,
                          const std::string& new_display_name);

    bool test_user_delete(const std::string& user_id);

    /* 桶测试 */
    bool test_bucket_create(const std::string& bucket_name,
                            const std::string& owner_user_id,
                            const std::string& zonegroup = "",
                            const std::string& placement_rule = "");

    bool test_bucket_get(const std::string& bucket_name);

    std::vector<std::string> test_bucket_list(const std::string& owner_user_id,
                                              size_t max = 100);

    bool test_bucket_delete(const std::string& bucket_name,
                            bool delete_objects = false);

    /* 对象测试 */
    bool test_object_create(const std::string& bucket_name,
                            const std::string& object_name,
                            const std::string& data);

    std::string test_object_read(const std::string& bucket_name,
                                  const std::string& object_name);

    std::vector<std::string> test_object_list(const std::string& bucket_name,
                                               const std::string& prefix = "",
                                               const std::string& marker = "",
                                               size_t max_keys = 100);

    bool test_object_delete(const std::string& bucket_name,
                            const std::string& object_name);

    /* 性能测试 */
    void test_user_throughput(size_t iterations,
                               sal_c_test_perf_result_t* result);

    void test_bucket_throughput(size_t iterations,
                                sal_c_test_perf_result_t* result);

    void test_object_throughput(size_t iterations,
                                size_t object_size,
                                sal_c_test_perf_result_t* result);

    void test_user_latency(size_t iterations,
                           sal_c_test_latency_percentile_t* result);

    void test_bucket_latency(size_t iterations,
                             sal_c_test_latency_percentile_t* result);

    void test_object_latency(size_t iterations,
                             size_t object_size,
                             sal_c_test_latency_percentile_t* result);

    /* 内存测试 */
    sal_c_test_memory_stats_t get_memory_stats();
    void reset_memory_stats();

    /* 测试套件 */
    size_t run_suite(sal_c_test_suite_t* suite);
    void print_report(const sal_c_test_suite_t* suite, bool verbose = false);

private:
    sal_c_test_context_t* ctx_;
};

} // namespace sal_c
} // namespace ceph
#endif

#endif /* __CEPH_RGW_SAL_C_TEST_FRAMEWORK_H */

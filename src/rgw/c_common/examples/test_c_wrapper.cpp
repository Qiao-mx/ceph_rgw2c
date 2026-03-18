/**
 * @file test_c_wrapper.c
 * @brief Test C wrapper for C++ classes
 *
 * This file demonstrates calling C++ code from C through a wrapper layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpp_wrapper_example.hpp"

int main(void) {
    printf("=== 测试 C 调用 C++ 包装层 ===\n\n");

    // 测试 1: 数据处理器
    printf("[测试 1] 数据处理器\n");
    data_processor_handle_t processor = data_processor_create("TestProcessor");
    if (!processor) {
        printf("FAIL: 无法创建处理器\n");
        return 1;
    }
    printf("✓ 创建处理器: %s\n", data_processor_get_name(processor));

    // 添加数据
    data_processor_add(processor, 10);
    data_processor_add(processor, 20);
    data_processor_add(processor, 30);
    printf("✓ 添加数据: 大小 = %zu\n", data_processor_size(processor));

    // 处理数据
    int sum = data_processor_process(processor);
    printf("✓ 处理数据: 总和 = %d\n", sum);
    printf("✓ 处理次数: %d\n", data_processor_get_count(processor));

    // 再次处理
    sum = data_processor_process(processor);
    printf("✓ 再次处理: 总和 = %d\n", sum);
    printf("✓ 处理次数: %d\n", data_processor_get_count(processor));

    // 清空
    data_processor_clear(processor);
    printf("✓ 清空数据: 大小 = %zu\n", data_processor_size(processor));

    // 销毁
    data_processor_destroy(processor);
    printf("✓ 销毁处理器\n\n");

    // 测试 2: 字符串工具
    printf("[测试 2] 字符串工具\n");

    const char* result = string_utils_concat("Hello, ", "World!");
    printf("✓ 字符串拼接: \"%s\"\n", result);

    result = string_utils_to_upper("Hello World");
    printf("✓ 转大写: \"%s\"\n", result);

    result = string_utils_to_lower("HELLO WORLD");
    printf("✓ 转小写: \"%s\"\n", result);

    printf("\n=== 所有测试通过 ===\n");
    return 0;
}

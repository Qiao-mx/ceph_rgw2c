/**
 * @file cpp_wrapper_example.cpp
 * @brief C++ to C wrapper layer example
 *
 * This example demonstrates how to wrap C++ classes with a C API
 * for use in C programs or when mixed C/C++ compilation is needed.
 */

#ifndef CPP_WRAPPER_EXAMPLE_HPP
#define CPP_WRAPPER_EXAMPLE_HPP

#include <string>
#include <vector>
#include <iostream>
#include <cstring>

// ===================== C++ 实现 =====================

/**
 * @brief 简单的数据处理器类 (C++ 实现)
 */
class DataProcessor {
private:
    std::string name;
    std::vector<int> data;
    int processed_count;

public:
    DataProcessor(const char* processor_name)
        : name(processor_name ? processor_name : ""), processed_count(0) {
    }

    ~DataProcessor() {
        // 清理资源
        data.clear();
    }

    /**
     * @brief 添加数据
     */
    void add_data(int value) {
        data.push_back(value);
    }

    /**
     * @brief 处理所有数据（简单求和）
     */
    int process() {
        int sum = 0;
        for (int v : data) {
            sum += v;
        }
        processed_count++;
        return sum;
    }

    /**
     * @brief 获取数据大小
     */
    size_t size() const {
        return data.size();
    }

    /**
     * @brief 获取处理次数
     */
    int get_processed_count() const {
        return processed_count;
    }

    /**
     * @brief 清空数据
     */
    void clear() {
        data.clear();
        processed_count = 0;
    }

    /**
     * @brief 获取处理器名称
     */
    const char* get_name() const {
        return name.c_str();
    }
};

/**
 * @brief 字符串工具类 (C++ 实现)
 */
class StringUtils {
public:
    /**
     * @brief 字符串拼接
     */
    static std::string concat(const std::string& a, const std::string& b) {
        return a + b;
    }

    /**
     * @brief 字符串转大写
     */
    static std::string to_upper(const std::string& str) {
        std::string result = str;
        for (char& c : result) {
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
        return result;
    }

    /**
     * @brief 字符串转小写
     */
    static std::string to_lower(const std::string& str) {
        std::string result = str;
        for (char& c : result) {
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        }
        return result;
    }
};

// ===================== C 接口包装层 =====================

/**
 * @brief C 接口句柄类型
 */
typedef void* data_processor_handle_t;
typedef void* string_utils_handle_t;

// 数据处理器 C API
extern "C" {

    /**
     * @brief 创建数据处理器
     */
    data_processor_handle_t data_processor_create(const char* name) {
        return new DataProcessor(name);
    }

    /**
     * @brief 销毁数据处理器
     */
    void data_processor_destroy(data_processor_handle_t handle) {
        if (handle) {
            delete static_cast<DataProcessor*>(handle);
        }
    }

    /**
     * @brief 添加数据
     */
    void data_processor_add(data_processor_handle_t handle, int value) {
        if (handle) {
            static_cast<DataProcessor*>(handle)->add_data(value);
        }
    }

    /**
     * @brief 处理数据
     */
    int data_processor_process(data_processor_handle_t handle) {
        if (handle) {
            return static_cast<DataProcessor*>(handle)->process();
        }
        return 0;
    }

    /**
     * @brief 获取数据大小
     */
    size_t data_processor_size(data_processor_handle_t handle) {
        if (handle) {
            return static_cast<DataProcessor*>(handle)->size();
        }
        return 0;
    }

    /**
     * @brief 获取处理次数
     */
    int data_processor_get_count(data_processor_handle_t handle) {
        if (handle) {
            return static_cast<DataProcessor*>(handle)->get_processed_count();
        }
        return 0;
    }

    /**
     * @brief 清空数据
     */
    void data_processor_clear(data_processor_handle_t handle) {
        if (handle) {
            static_cast<DataProcessor*>(handle)->clear();
        }
    }

    /**
     * @brief 获取处理器名称
     */
    const char* data_processor_get_name(data_processor_handle_t handle) {
        if (handle) {
            return static_cast<DataProcessor*>(handle)->get_name();
        }
        return "";
    }

    // 字符串工具 C API
    // 注意：返回 C 字符串需要特别小心内存管理

    /**
     * @brief 字符串拼接 (使用静态缓冲区，注意线程安全)
     */
    const char* string_utils_concat(const char* a, const char* b) {
        static std::string result;
        result = std::string(a ? a : "") + std::string(b ? b : "");
        return result.c_str();
    }

    /**
     * @brief 字符串转大写
     */
    const char* string_utils_to_upper(const char* str) {
        static std::string result;
        result = StringUtils::to_upper(str ? str : "");
        return result.c_str();
    }

    /**
     * @brief 字符串转小写
     */
    const char* string_utils_to_lower(const char* str) {
        static std::string result;
        result = StringUtils::to_lower(str ? str : "");
        return result.c_str();
    }

} // extern "C"

#endif // CPP_WRAPPER_EXAMPLE_HPP

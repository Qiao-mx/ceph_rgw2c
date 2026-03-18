/**
 * @file rgw_sal_errors.h
 * @brief SAL C 接口错误码定义
 *
 * 定义存储抽象层 (SAL) C 接口的错误码。
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SAL 错误码
 */
typedef enum rgw_sal_error_code {
    RGW_SAL_OK = 0,

    /* 通用错误 */
    RGW_SAL_ERR_INVALID_ARG = 1,          /**< 无效参数 */
    RGW_SAL_ERR_OUT_OF_MEMORY = 2,         /**< 内存不足 */
    RGW_SAL_ERR_NOT_FOUND = 3,             /**< 资源未找到 */
    RGW_SAL_ERR_ALREADY_EXISTS = 4,        /**< 资源已存在 */
    RGW_SAL_ERR_PERMISSION_DENIED = 5,    /**< 权限拒绝 */
    RGW_SAL_ERR_TIMEOUT = 6,               /**< 操作超时 */
    RGW_SAL_ERR_IO_ERROR = 7,              /**< I/O 错误 */
    RGW_SAL_ERR_NOT_IMPLEMENTED = 8,      /**< 未实现 */

    /* 用户相关错误 */
    RGW_SAL_ERR_USER_NOT_FOUND = 100,      /**< 用户未找到 */
    RGW_SAL_ERR_USER_EXISTS = 101,         /**< 用户已存在 */
    RGW_SAL_ERR_INVALID_USER = 102,        /**< 无效用户 */

    /* 桶相关错误 */
    RGW_SAL_ERR_BUCKET_NOT_FOUND = 200,    /**< 桶未找到 */
    RGW_SAL_ERR_BUCKET_EXISTS = 201,       /**< 桶已存在 */
    RGW_SAL_ERR_BUCKET_NOT_EMPTY = 202,    /**< 桶非空 */
    RGW_SAL_ERR_INVALID_BUCKET = 203,     /**< 无效桶 */

    /* 对象相关错误 */
    RGW_SAL_ERR_OBJECT_NOT_FOUND = 300,    /**< 对象未找到 */
    RGW_SAL_ERR_OBJECT_EXISTS = 301,       /**< 对象已存在 */
    RGW_SAL_ERR_INVALID_OBJECT = 302,      /**< 无效对象 */
    RGW_SAL_ERR_OBJECT_TOO_LARGE = 303,   /**< 对象过大 */

    /* 版本控制错误 */
    RGW_SAL_ERR_VERSION_CONFLICT = 400,    /**< 版本冲突 */
    RGW_SAL_ERR_NO_SUCH_VERSION = 401,    /**< 版本不存在 */

    /* 多部分上传错误 */
    RGW_SAL_ERR_UPLOAD_NOT_FOUND = 500,   /**< 上传未找到 */
    RGW_SAL_ERR_UPLOAD_PART_NOT_FOUND = 501, /**< 上传部分未找到 */

    /* 配额错误 */
    RGW_SAL_ERR_QUOTA_EXCEEDED = 600,     /**< 配额超出 */

    /* 未知错误 */
    RGW_SAL_ERR_UNKNOWN = 999
} rgw_sal_error_code_t;

/**
 * @brief 错误码转字符串
 * @param code 错误码
 * @return 错误码对应的字符串描述
 */
static inline const char* rgw_sal_error_string(rgw_sal_error_code_t code) {
    switch (code) {
        case RGW_SAL_OK: return "Success";
        case RGW_SAL_ERR_INVALID_ARG: return "Invalid argument";
        case RGW_SAL_ERR_OUT_OF_MEMORY: return "Out of memory";
        case RGW_SAL_ERR_NOT_FOUND: return "Not found";
        case RGW_SAL_ERR_ALREADY_EXISTS: return "Already exists";
        case RGW_SAL_ERR_PERMISSION_DENIED: return "Permission denied";
        case RGW_SAL_ERR_TIMEOUT: return "Timeout";
        case RGW_SAL_ERR_IO_ERROR: return "I/O error";
        case RGW_SAL_ERR_NOT_IMPLEMENTED: return "Not implemented";
        case RGW_SAL_ERR_USER_NOT_FOUND: return "User not found";
        case RGW_SAL_ERR_USER_EXISTS: return "User exists";
        case RGW_SAL_ERR_INVALID_USER: return "Invalid user";
        case RGW_SAL_ERR_BUCKET_NOT_FOUND: return "Bucket not found";
        case RGW_SAL_ERR_BUCKET_EXISTS: return "Bucket exists";
        case RGW_SAL_ERR_BUCKET_NOT_EMPTY: return "Bucket not empty";
        case RGW_SAL_ERR_INVALID_BUCKET: return "Invalid bucket";
        case RGW_SAL_ERR_OBJECT_NOT_FOUND: return "Object not found";
        case RGW_SAL_ERR_OBJECT_EXISTS: return "Object exists";
        case RGW_SAL_ERR_INVALID_OBJECT: return "Invalid object";
        case RGW_SAL_ERR_OBJECT_TOO_LARGE: return "Object too large";
        case RGW_SAL_ERR_VERSION_CONFLICT: return "Version conflict";
        case RGW_SAL_ERR_NO_SUCH_VERSION: return "No such version";
        case RGW_SAL_ERR_UPLOAD_NOT_FOUND: return "Upload not found";
        case RGW_SAL_ERR_UPLOAD_PART_NOT_FOUND: return "Upload part not found";
        case RGW_SAL_ERR_QUOTA_EXCEEDED: return "Quota exceeded";
        default: return "Unknown error";
    }
}

#ifdef __cplusplus
}
#endif

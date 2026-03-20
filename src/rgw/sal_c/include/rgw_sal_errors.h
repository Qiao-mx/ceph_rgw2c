/**
 * @file rgw_sal_errors.h
 * @brief SAL C 接口错误码定义
 *
 * 定义存储抽象层 (SAL) C 接口的错误码。
 */

#pragma once

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * SAL 错误码
 *============================================================================*/

/** 成功 */
#define RGW_SAL_OK 0

/** 通用错误 */
#define RGW_SAL_ERR_GENERIC (-1000)

/** 无效参数 */
#define RGW_SAL_ERR_INVALID_ARG (-1001)

/** 未实现 */
#define RGW_SAL_ERR_NOT_IMPLEMENTED (-1002)

/** 未找到 */
#define RGW_SAL_ERR_NOT_FOUND (-1003)

/** 权限不足 */
#define RGW_SAL_ERR_PERMISSION_DENIED (-1004)

/** 已存在 */
#define RGW_SAL_ERR_EXISTS (-1005)

/** 不存在 */
#define RGW_SAL_ERR_NOENT (-ENOENT)

/** IO 错误 */
#define RGW_SAL_ERR_IO_ERROR (-EIO)

/** 内存不足 */
#define RGW_SAL_ERR_OUT_OF_MEMORY (-ENOMEM)

/** 未初始化 */
#define RGW_SAL_ERR_NOT_INITIALIZED (-1008)

/** 连接失败 */
#define RGW_SAL_ERR_CONNECTION_FAILED (-1009)

/** 超时 */
#define RGW_SAL_ERR_TIMEDOUT (-ETIMEDOUT)

/** 冲突 */
#define RGW_SAL_ERR_CONFLICT (-1011)

/** 范围错误 */
#define RGW_SAL_ERR_RANGE (-ERANGE)

/** 配额超限 */
#define RGW_SAL_ERR_QUOTA_EXCEEDED (-1013)

/** MFA 认证失败 */
#define RGW_SAL_ERR_MFA_AUTH_FAILED (-1014)

/** 版本冲突 */
#define RGW_SAL_ERR_VERSION_CONFLICT (-1015)

/** 索引错误 */
#define RGW_SAL_ERR_INDEX_ERROR (-1016)

/** 数据损坏 */
#define RGW_SAL_ERR_DATA_CORRUPTION (-1017)

/*============================================================================
 * SQLite 特定错误码 (向后兼容)
 *============================================================================*/

#define RGW_SQLITE_OK RGW_SAL_OK
#define RGW_SQLITE_ERROR RGW_SAL_ERR_GENERIC
#define RGW_SQLITE_BUSY RGW_SAL_ERR_IO_ERROR
#define RGW_SQLITE_NOMEM RGW_SAL_ERR_OUT_OF_MEMORY
#define RGW_SQLITE_NOTFOUND RGW_SAL_ERR_NOT_FOUND
#define RGW_SQLITE_EXISTS RGW_SAL_ERR_EXISTS

/*============================================================================
 * 错误处理辅助函数
 *============================================================================*/

/**
 * @brief 获取错误码对应的描述
 * @param err 错误码
 * @return 错误描述字符串
 */
const char* rgw_sal_strerror(int err);

/**
 * @brief 检查是否为致命错误
 * @param err 错误码
 * @return 如果是致命错误返回 true
 */
bool rgw_sal_is_fatal_error(int err);

#ifdef __cplusplus
}
#endif

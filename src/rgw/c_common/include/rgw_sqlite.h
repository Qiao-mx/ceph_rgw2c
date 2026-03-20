/**
 * @file rgw_sqlite.h
 * @brief SQLite 数据库封装接口
 *
 * 提供 SQLite 数据库的 C 接口封装，用于 DBStore 驱动的数据持久化。
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 前向声明
 *============================================================================*/

/** SQLite 连接 */
typedef struct rgw_sqlite_db rgw_sqlite_db_t;

/** SQLite 语句 */
typedef struct rgw_sqlite_stmt rgw_sqlite_stmt_t;

/** SQLite 结果回调函数类型 */
typedef int (*rgw_sqlite_callback_t)(void* user_data, int columns, char** values, char** names);

/*============================================================================
 * 错误码定义
 *============================================================================*/

/** SQLite 操作成功 */
#define RGW_SQLITE_OK          0

/** 通用错误 */
#define RGW_SQLITE_ERROR       1

/** 参数无效 */
#define RGW_SQLITE_INVALID_ARG 2

/** 内存分配失败 */
#define RGW_SQLITE_NOMEM       3

/** 数据库未打开 */
#define RGW_SQLITE_NOT_OPEN    4

/** SQL 执行失败 */
#define RGW_SQLITE_EXEC_FAILED 5

/** 语句准备失败 */
#define RGW_SQLITE_PREPARE_FAILED 6

/** 步骤执行失败 */
#define RGW_SQLITE_STEP_FAILED 7

/** 行不存在 */
#define RGW_SQLITE_DONE        100

/** 行可用 */
#define RGW_SQLITE_ROW         101

/*============================================================================
 * 数据库连接管理
 *============================================================================*/

/**
 * @brief 打开 SQLite 数据库
 *
 * @param db_path 数据库文件路径
 * @param db 输出参数，返回数据库连接句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_open(const char* db_path, rgw_sqlite_db_t** db);

/**
 * @brief 关闭 SQLite 数据库
 *
 * @param db 数据库连接句柄
 */
void rgw_sqlite_close(rgw_sqlite_db_t* db);

/**
 * @brief 检查数据库是否打开
 *
 * @param db 数据库连接句柄
 * @return 已打开返回 true，否则返回 false
 */
bool rgw_sqlite_is_open(const rgw_sqlite_db_t* db);

/**
 * @brief 获取数据库路径
 *
 * @param db 数据库连接句柄
 * @return 数据库路径字符串，如果未打开返回 NULL
 */
const char* rgw_sqlite_get_path(const rgw_sqlite_db_t* db);

/**
 * @brief 执行 SQL 语句
 *
 * @param db 数据库连接句柄
 * @param sql SQL 语句
 * @param callback 回调函数，用于处理每一行结果（可为空）
 * @param user_data 传递给回调函数的用户数据
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_exec(rgw_sqlite_db_t* db, const char* sql,
                    rgw_sqlite_callback_t callback, void* user_data);

/**
 * @brief 获取最近一次错误信息
 *
 * @param db 数据库连接句柄
 * @return 错误信息字符串
 */
const char* rgw_sqlite_errmsg(const rgw_sqlite_db_t* db);

/*============================================================================
 * 事务管理
 *============================================================================*/

/**
 * @brief 开始事务
 *
 * @param db 数据库连接句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_begin(rgw_sqlite_db_t* db);

/**
 * @brief 提交事务
 *
 * @param db 数据库连接句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_commit(rgw_sqlite_db_t* db);

/**
 * @brief 回滚事务
 *
 * @param db 数据库连接句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_rollback(rgw_sqlite_db_t* db);

/*============================================================================
 * 语句准备与执行
 *============================================================================*/

/**
 * @brief 准备 SQL 语句
 *
 * @param db 数据库连接句柄
 * @param sql SQL 语句模板
 * @param stmt 输出参数，返回语句句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_prepare(rgw_sqlite_db_t* db, const char* sql, rgw_sqlite_stmt_t** stmt);

/**
 * @brief 重置语句
 *
 * 将语句重置为初始状态，绑定参数保留。
 *
 * @param stmt 语句句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_reset(rgw_sqlite_stmt_t* stmt);

/**
 * @brief 销毁语句
 *
 * @param stmt 语句句柄
 */
void rgw_sqlite_finalize(rgw_sqlite_stmt_t* stmt);

/**
 * @brief 执行语句
 *
 * 执行准备好的语句，调用一次。
 *
 * @param stmt 语句句柄
 * @return RGW_SQLITE_ROW 行可用，RGW_SQLITE_DONE 无更多行，失败返回错误码
 */
int rgw_sqlite_step(rgw_sqlite_stmt_t* stmt);

/**
 * @brief 执行单个查询
 *
 * 准备、执行、销毁语句一步完成。
 *
 * @param db 数据库连接句柄
 * @param sql SQL 语句
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_exec_query(rgw_sqlite_db_t* db, const char* sql,
                          rgw_sqlite_callback_t callback, void* user_data);

/*============================================================================
 * 参数绑定
 *============================================================================*/

/**
 * @brief 绑定整数参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @param value 整数值
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_int(rgw_sqlite_stmt_t* stmt, int idx, int value);

/**
 * @brief 绑定 64 位整数参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @param value 64 位整数值
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_int64(rgw_sqlite_stmt_t* stmt, int idx, int64_t value);

/**
 * @brief 绑定双精度浮点数参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @param value 双精度浮点值
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_double(rgw_sqlite_stmt_t* stmt, int idx, double value);

/**
 * @brief 绑定文本参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @param value 文本值（以 null 结尾）
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_text(rgw_sqlite_stmt_t* stmt, int idx, const char* value);

/**
 * @brief 绑定 blob 参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @param data blob 数据指针
 * @param len 数据长度
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_blob(rgw_sqlite_stmt_t* stmt, int idx, const void* data, size_t len);

/**
 * @brief 绑定 NULL 参数
 *
 * @param stmt 语句句柄
 * @param idx 参数索引（从 1 开始）
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_bind_null(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 清空绑定参数
 *
 * @param stmt 语句句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_clear_bindings(rgw_sqlite_stmt_t* stmt);

/*============================================================================
 * 结果获取
 *============================================================================*/

/**
 * @brief 获取列数
 *
 * @param stmt 语句句柄
 * @return 列数
 */
int rgw_sqlite_column_count(rgw_sqlite_stmt_t* stmt);

/**
 * @brief 获取列类型
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 列类型（SQLITE_INTEGER, SQLITE_FLOAT, SQLITE_TEXT, SQLITE_BLOB, SQLITE_NULL）
 */
int rgw_sqlite_column_type(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取列名
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 列名字符串
 */
const char* rgw_sqlite_column_name(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取整数列值
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 整数值
 */
int rgw_sqlite_column_int(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取 64 位整数列值
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 64 位整数值
 */
int64_t rgw_sqlite_column_int64(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取双精度浮点数列值
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 双精度浮点值
 */
double rgw_sqlite_column_double(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取文本列值
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 文本字符串（调用后不要释放）
 */
const char* rgw_sqlite_column_text(rgw_sqlite_stmt_t* stmt, int idx);

/**
 * @brief 获取 blob 列值
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @param len 输出参数，返回 blob 长度
 * @return blob 数据指针
 */
const void* rgw_sqlite_column_blob(rgw_sqlite_stmt_t* stmt, int idx, size_t* len);

/**
 * @brief 获取列值字节数
 *
 * @param stmt 语句句柄
 * @param idx 列索引（从 0 开始）
 * @return 字节数（文本为字节数，blob 为长度）
 */
size_t rgw_sqlite_column_bytes(rgw_sqlite_stmt_t* stmt, int idx);

/*============================================================================
 * 便捷宏
 *============================================================================*/

/** 获取文本列并复制（调用者需释放） */
#define RGW_SQLITE_COLUMN_STRdup(stmt, idx) \
    (rgw_sqlite_column_text(stmt, idx) ? \
     rgw_c_strdup(rgw_sqlite_column_text(stmt, idx)) : NULL)

/*============================================================================
 * 数据库初始化
 *============================================================================*/

/**
 * @brief 初始化 SQLite 数据库
 *
 * 创建必要的表结构。如果表已存在则跳过。
 *
 * @param db 数据库连接句柄
 * @return 成功返回 RGW_SQLITE_OK，失败返回错误码
 */
int rgw_sqlite_init_db(rgw_sqlite_db_t* db);

/*============================================================================
 * 错误码转字符串
 *============================================================================*/

/**
 * @brief 获取错误码对应的字符串描述
 *
 * @param err_code 错误码
 * @return 错误描述字符串
 */
const char* rgw_sqlite_errstr(int err_code);

#ifdef __cplusplus
}
#endif

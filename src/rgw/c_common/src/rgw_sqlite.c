/**
 * @file rgw_sqlite.c
 * @brief SQLite 数据库封装实现
 */

#include "rgw_sqlite.h"
#include "rgw_errors.h"
#include "containers/rgw_cmemory.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*============================================================================
 * 内部结构定义
 *============================================================================*/

/** SQLite 连接结构 */
struct rgw_sqlite_db {
    sqlite3* db;              /**< SQLite 数据库句柄 */
    char* db_path;            /**< 数据库路径 */
    char* errmsg;             /**< 最近错误信息 */
};

/** SQLite 语句结构 */
struct rgw_sqlite_stmt {
    sqlite3_stmt* stmt;       /**< SQLite 语句句柄 */
    rgw_sqlite_db_t* db;       /**< 关联的数据库连接 */
};

/*============================================================================
 * 辅助函数
 *============================================================================*/

/**
 * @brief 设置错误信息
 */
static void rgw_sqlite_set_error(rgw_sqlite_db_t* db, const char* msg) {
    if (db && msg) {
        if (db->errmsg) {
            rgw_c_free(db->errmsg);
        }
        db->errmsg = rgw_c_strdup(msg);
    }
}

/**
 * @brief 获取 SQLite 错误码对应的错误码
 */
static int rgw_sqlite_map_error(int sqlite_ret) {
    switch (sqlite_ret) {
        case SQLITE_OK:
        case SQLITE_DONE:
        case SQLITE_ROW:
            return RGW_SQLITE_OK;
        case SQLITE_NOMEM:
            return RGW_SQLITE_NOMEM;
        case SQLITE_MISUSE:
        case SQLITE_RANGE:
            return RGW_SQLITE_INVALID_ARG;
        default:
            return RGW_SQLITE_ERROR;
    }
}

/*============================================================================
 * 数据库连接管理
 *============================================================================*/

int rgw_sqlite_open(const char* db_path, rgw_sqlite_db_t** db_out) {
    if (!db_path || !db_out) {
        return RGW_SQLITE_INVALID_ARG;
    }

    rgw_sqlite_db_t* db = (rgw_sqlite_db_t*)rgw_c_alloc(sizeof(rgw_sqlite_db_t));
    if (!db) {
        return RGW_SQLITE_NOMEM;
    }

    db->db_path = rgw_c_strdup(db_path);
    if (!db->db_path) {
        rgw_c_free(db);
        return RGW_SQLITE_NOMEM;
    }

    int ret = sqlite3_open(db_path, &db->db);
    if (ret != SQLITE_OK) {
        rgw_sqlite_set_error(db, sqlite3_errmsg(db->db));
        if (db->db) {
            sqlite3_close(db->db);
        }
        rgw_c_free(db->db_path);
        rgw_c_free(db);
        return RGW_SQLITE_ERROR;
    }

    /* 启用外键约束 */
    char* err = NULL;
    ret = sqlite3_exec(db->db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err);
    if (ret != SQLITE_OK && err) {
        rgw_sqlite_set_error(db, err);
        sqlite3_free(err);
    }

    *db_out = db;
    return RGW_SQLITE_OK;
}

void rgw_sqlite_close(rgw_sqlite_db_t* db) {
    if (!db) {
        return;
    }

    if (db->db) {
        sqlite3_close(db->db);
        db->db = NULL;
    }

    if (db->db_path) {
        rgw_c_free(db->db_path);
        db->db_path = NULL;
    }

    if (db->errmsg) {
        rgw_c_free(db->errmsg);
        db->errmsg = NULL;
    }

    rgw_c_free(db);
}

bool rgw_sqlite_is_open(const rgw_sqlite_db_t* db) {
    return (db && db->db != NULL);
}

const char* rgw_sqlite_get_path(const rgw_sqlite_db_t* db) {
    return db ? db->db_path : NULL;
}

int rgw_sqlite_exec(rgw_sqlite_db_t* db, const char* sql,
                    rgw_sqlite_callback_t callback, void* user_data) {
    if (!db || !db->db || !sql) {
        return RGW_SQLITE_INVALID_ARG;
    }

    char* err = NULL;
    int ret = sqlite3_exec(db->db, sql, callback, user_data, &err);

    if (ret != SQLITE_OK) {
        rgw_sqlite_set_error(db, err ? err : "Unknown error");
        if (err) {
            sqlite3_free(err);
        }
        return RGW_SQLITE_EXEC_FAILED;
    }

    return RGW_SQLITE_OK;
}

const char* rgw_sqlite_errmsg(const rgw_sqlite_db_t* db) {
    if (!db) {
        return "NULL database handle";
    }

    if (db->errmsg) {
        return db->errmsg;
    }

    if (db->db) {
        return sqlite3_errmsg(db->db);
    }

    return "Unknown error";
}

/*============================================================================
 * 事务管理
 *============================================================================*/

int rgw_sqlite_begin(rgw_sqlite_db_t* db) {
    return rgw_sqlite_exec(db, "BEGIN TRANSACTION;", NULL, NULL);
}

int rgw_sqlite_commit(rgw_sqlite_db_t* db) {
    return rgw_sqlite_exec(db, "COMMIT;", NULL, NULL);
}

int rgw_sqlite_rollback(rgw_sqlite_db_t* db) {
    return rgw_sqlite_exec(db, "ROLLBACK;", NULL, NULL);
}

/*============================================================================
 * 语句准备与执行
 *============================================================================*/

int rgw_sqlite_prepare(rgw_sqlite_db_t* db, const char* sql, rgw_sqlite_stmt_t** stmt_out) {
    if (!db || !db->db || !sql || !stmt_out) {
        return RGW_SQLITE_INVALID_ARG;
    }

    sqlite3_stmt* sqlite_stmt = NULL;
    int ret = sqlite3_prepare_v2(db->db, sql, -1, &sqlite_stmt, NULL);

    if (ret != SQLITE_OK) {
        rgw_sqlite_set_error(db, sqlite3_errmsg(db->db));
        return RGW_SQLITE_PREPARE_FAILED;
    }

    rgw_sqlite_stmt_t* stmt = (rgw_sqlite_stmt_t*)rgw_c_alloc(sizeof(rgw_sqlite_stmt_t));
    if (!stmt) {
        sqlite3_finalize(sqlite_stmt);
        return RGW_SQLITE_NOMEM;
    }

    stmt->stmt = sqlite_stmt;
    stmt->db = db;

    *stmt_out = stmt;
    return RGW_SQLITE_OK;
}

int rgw_sqlite_reset(rgw_sqlite_stmt_t* stmt) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_reset(stmt->stmt);
    return rgw_sqlite_map_error(ret);
}

void rgw_sqlite_finalize(rgw_sqlite_stmt_t* stmt) {
    if (!stmt) {
        return;
    }

    if (stmt->stmt) {
        sqlite3_finalize(stmt->stmt);
        stmt->stmt = NULL;
    }

    stmt->db = NULL;
    rgw_c_free(stmt);
}

int rgw_sqlite_step(rgw_sqlite_stmt_t* stmt) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_step(stmt->stmt);

    switch (ret) {
        case SQLITE_ROW:
            return RGW_SQLITE_ROW;
        case SQLITE_DONE:
            return RGW_SQLITE_DONE;
        case SQLITE_BUSY:
        case SQLITE_LOCKED:
            return RGW_SQLITE_ERROR;
        default:
            if (stmt->db) {
                rgw_sqlite_set_error(stmt->db, sqlite3_errmsg(stmt->db->db));
            }
            return RGW_SQLITE_STEP_FAILED;
    }
}

int rgw_sqlite_exec_query(rgw_sqlite_db_t* db, const char* sql,
                          rgw_sqlite_callback_t callback, void* user_data) {
    rgw_sqlite_stmt_t* stmt = NULL;
    int ret = rgw_sqlite_prepare(db, sql, &stmt);
    if (ret != RGW_SQLITE_OK) {
        return ret;
    }

    int step_ret;
    while ((step_ret = rgw_sqlite_step(stmt)) == RGW_SQLITE_ROW) {
        if (callback) {
            int col_count = rgw_sqlite_column_count(stmt);
            /* 构建 values 数组 */
            char** values = (char**)rgw_c_alloc(sizeof(char*) * col_count);
            char** names = (char**)rgw_c_alloc(sizeof(char*) * col_count);
            if (values && names) {
                for (int i = 0; i < col_count; i++) {
                    names[i] = (char*)rgw_sqlite_column_name(stmt, i);
                    const char* text = rgw_sqlite_column_text(stmt, i);
                    values[i] = text ? rgw_c_strdup(text) : NULL;
                }
                callback(user_data, col_count, values, names);
                for (int i = 0; i < col_count; i++) {
                    if (values[i]) rgw_c_free(values[i]);
                }
            }
            if (values) rgw_c_free(values);
            if (names) rgw_c_free(names);
        }
    }

    rgw_sqlite_finalize(stmt);

    if (step_ret == RGW_SQLITE_DONE) {
        return RGW_SQLITE_OK;
    }
    return step_ret;
}

/*============================================================================
 * 参数绑定
 *============================================================================*/

int rgw_sqlite_bind_int(rgw_sqlite_stmt_t* stmt, int idx, int value) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_bind_int(stmt->stmt, idx, value);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_bind_int64(rgw_sqlite_stmt_t* stmt, int idx, int64_t value) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_bind_int64(stmt->stmt, idx, value);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_bind_double(rgw_sqlite_stmt_t* stmt, int idx, double value) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_bind_double(stmt->stmt, idx, value);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_bind_text(rgw_sqlite_stmt_t* stmt, int idx, const char* value) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    if (!value) {
        int ret = sqlite3_bind_null(stmt->stmt, idx);
        return rgw_sqlite_map_error(ret);
    }

    int ret = sqlite3_bind_text(stmt->stmt, idx, value, -1, SQLITE_TRANSIENT);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_bind_blob(rgw_sqlite_stmt_t* stmt, int idx, const void* data, size_t len) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    if (!data) {
        int ret = sqlite3_bind_null(stmt->stmt, idx);
        return rgw_sqlite_map_error(ret);
    }

    int ret = sqlite3_bind_blob(stmt->stmt, idx, data, len, SQLITE_TRANSIENT);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_bind_null(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_bind_null(stmt->stmt, idx);
    return rgw_sqlite_map_error(ret);
}

int rgw_sqlite_clear_bindings(rgw_sqlite_stmt_t* stmt) {
    if (!stmt || !stmt->stmt) {
        return RGW_SQLITE_INVALID_ARG;
    }

    int ret = sqlite3_clear_bindings(stmt->stmt);
    return rgw_sqlite_map_error(ret);
}

/*============================================================================
 * 结果获取
 *============================================================================*/

int rgw_sqlite_column_count(rgw_sqlite_stmt_t* stmt) {
    if (!stmt || !stmt->stmt) {
        return 0;
    }
    return sqlite3_column_count(stmt->stmt);
}

int rgw_sqlite_column_type(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return SQLITE_NULL;
    }
    return sqlite3_column_type(stmt->stmt, idx);
}

const char* rgw_sqlite_column_name(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return NULL;
    }
    return (const char*)sqlite3_column_name(stmt->stmt, idx);
}

int rgw_sqlite_column_int(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return 0;
    }
    return sqlite3_column_int(stmt->stmt, idx);
}

int64_t rgw_sqlite_column_int64(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return 0;
    }
    return sqlite3_column_int64(stmt->stmt, idx);
}

double rgw_sqlite_column_double(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return 0.0;
    }
    return sqlite3_column_double(stmt->stmt, idx);
}

const char* rgw_sqlite_column_text(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return NULL;
    }
    return (const char*)sqlite3_column_text(stmt->stmt, idx);
}

const void* rgw_sqlite_column_blob(rgw_sqlite_stmt_t* stmt, int idx, size_t* len_out) {
    if (!stmt || !stmt->stmt) {
        if (len_out) *len_out = 0;
        return NULL;
    }

    const void* blob = sqlite3_column_blob(stmt->stmt, idx);
    if (len_out) {
        *len_out = sqlite3_column_bytes(stmt->stmt, idx);
    }
    return blob;
}

size_t rgw_sqlite_column_bytes(rgw_sqlite_stmt_t* stmt, int idx) {
    if (!stmt || !stmt->stmt) {
        return 0;
    }
    return sqlite3_column_bytes(stmt->stmt, idx);
}

/*============================================================================
 * 数据库初始化
 *============================================================================*/

static const char* SQL_CREATE_TABLES =
    "-- 用户表\n"
    "CREATE TABLE IF NOT EXISTS users (\n"
    "    user_id TEXT PRIMARY KEY,\n"
    "    tenant TEXT,\n"
    "    ns TEXT,\n"
    "    display_name TEXT,\n"
    "    email TEXT,\n"
    "    user_type INTEGER DEFAULT 0,\n"
    "    max_buckets INTEGER DEFAULT 1000,\n"
    "    suspended INTEGER DEFAULT 0,\n"
    "    max_size INTEGER DEFAULT -1,\n"
    "    max_objects INTEGER DEFAULT -1,\n"
    "    op_mask TEXT DEFAULT 'read,write,delete',\n"
    "    explicit_placement TEXT,\n"
    "    default_placement TEXT,\n"
    "    default_storage_class TEXT,\n"
    "    placement_tags TEXT,\n"
    "    quota_enabled INTEGER DEFAULT 0,\n"
    "    user_stats_ver INTEGER DEFAULT 0,\n"
    "    stats_num_users INTEGER DEFAULT 0,\n"
    "    stats_sum_bytes INTEGER DEFAULT 0,\n"
    "    stats_sum_objects INTEGER DEFAULT 0,\n"
    "    created_at INTEGER,\n"
    "    modified_at INTEGER\n"
    ");\n"
    "\n"
    "-- Access Key 索引表\n"
    "CREATE TABLE IF NOT EXISTS access_keys (\n"
    "    access_key TEXT PRIMARY KEY,\n"
    "    user_id TEXT NOT NULL,\n"
    "    secret_key TEXT NOT NULL,\n"
    "    active INTEGER DEFAULT 1,\n"
    "    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE\n"
    ");\n"
    "\n"
    "-- Swift 认证表\n"
    "CREATE TABLE IF NOT EXISTS swift_keys (\n"
    "    user_id TEXT PRIMARY KEY,\n"
    "    secret_key TEXT NOT NULL,\n"
    "    subuser TEXT,\n"
    "    FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE\n"
    ");\n"
    "\n"
    "-- 桶表\n"
    "CREATE TABLE IF NOT EXISTS buckets (\n"
    "    bucket_id TEXT PRIMARY KEY,\n"
    "    tenant TEXT,\n"
    "    name TEXT NOT NULL,\n"
    "    marker TEXT,\n"
    "    owner_id TEXT NOT NULL,\n"
    "    created_at INTEGER,\n"
    "    modified_at INTEGER,\n"
    "    removed_at INTEGER,\n"
    "    flags INTEGER DEFAULT 0,\n"
    "    zonegroup_id TEXT,\n"
    "    placement_rule TEXT,\n"
    "    -- 以下为冗余字段，用于快速查询\n"
    "    bucket_size INTEGER DEFAULT 0,\n"
    "    object_count INTEGER DEFAULT 0,\n"
    "    num_shards INTEGER DEFAULT 0\n"
    ");\n"
    "\n"
    "-- 对象表\n"
    "CREATE TABLE IF NOT EXISTS objects (\n"
    "    object_id TEXT PRIMARY KEY,\n"
    "    bucket_id TEXT NOT NULL,\n"
    "    name TEXT NOT NULL,\n"
    "    instance TEXT,\n"
    "    namespace TEXT,\n"
    "    size INTEGER DEFAULT 0,\n"
    "    storage_class TEXT,\n"
    "    etag TEXT,\n"
    "    created_at INTEGER,\n"
    "    modified_at INTEGER,\n"
    "    deleted_at INTEGER,\n"
    "    -- 属性存储为 JSON\n"
    "    attrs TEXT,\n"
    "    FOREIGN KEY (bucket_id) REFERENCES buckets(bucket_id) ON DELETE CASCADE\n"
    ");\n"
    "\n"
    "-- 多版本对象表\n"
    "CREATE TABLE IF NOT EXISTS object_versions (\n"
    "    version_id TEXT PRIMARY KEY,\n"
    "    object_id TEXT NOT NULL,\n"
    "    bucket_id TEXT NOT NULL,\n"
    "    created_at INTEGER,\n"
    "    is_latest INTEGER DEFAULT 1,\n"
    "    FOREIGN KEY (object_id) REFERENCES objects(object_id) ON DELETE CASCADE\n"
    ");\n"
    "\n"
    "-- Usage 日志表\n"
    "CREATE TABLE IF NOT EXISTS usage_log (\n"
    "    log_id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    owner_id TEXT NOT NULL,\n"
    "    bucket_id TEXT,\n"
    "    timestamp INTEGER NOT NULL,\n"
    "    byte_sent INTEGER DEFAULT 0,\n"
    "    byte_received INTEGER DEFAULT 0,\n"
    "    ops INTEGER DEFAULT 0,\n"
    "    successful_ops INTEGER DEFAULT 0,\n"
    "    category TEXT\n"
    ");\n"
    "\n"
    "-- 索引\n"
    "CREATE INDEX IF NOT EXISTS idx_buckets_owner ON buckets(owner_id);\n"
    "CREATE INDEX IF NOT EXISTS idx_buckets_name ON buckets(name);\n"
    "CREATE INDEX IF NOT EXISTS idx_access_keys_user ON access_keys(user_id);\n"
    "CREATE INDEX IF NOT EXISTS idx_objects_bucket ON objects(bucket_id);\n"
    "CREATE INDEX IF NOT EXISTS idx_objects_name ON objects(name);\n"
    "CREATE INDEX IF NOT EXISTS idx_usage_owner ON usage_log(owner_id);\n"
    "CREATE INDEX IF NOT EXISTS idx_usage_timestamp ON usage_log(timestamp);\n";

int rgw_sqlite_init_db(rgw_sqlite_db_t* db) {
    if (!db || !db->db) {
        return RGW_SQLITE_INVALID_ARG;
    }

    return rgw_sqlite_exec(db, SQL_CREATE_TABLES, NULL, NULL);
}

/*============================================================================
 * 错误码转字符串
 *============================================================================*/

const char* rgw_sqlite_errstr(int err_code) {
    switch (err_code) {
        case RGW_SQLITE_OK:
            return "Success";
        case RGW_SQLITE_ERROR:
            return "SQLite error";
        case RGW_SQLITE_INVALID_ARG:
            return "Invalid argument";
        case RGW_SQLITE_NOMEM:
            return "Out of memory";
        case RGW_SQLITE_NOT_OPEN:
            return "Database not open";
        case RGW_SQLITE_EXEC_FAILED:
            return "SQL execution failed";
        case RGW_SQLITE_PREPARE_FAILED:
            return "Statement preparation failed";
        case RGW_SQLITE_STEP_FAILED:
            return "Step execution failed";
        case RGW_SQLITE_DONE:
            return "No more rows";
        case RGW_SQLITE_ROW:
            return "Row available";
        default:
            return "Unknown error";
    }
}

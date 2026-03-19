/**
 * @file rgw_omap.h
 * @brief OMAP 操作封装接口
 *
 * 封装 librados OMAP 操作，提供更高级的接口。
 *
 * OMAP (Object Map) 是 RADOS 中用于存储键值对元数据的功能，
 * 主要用于存储对象的扩展属性和索引信息。
 *
 * 支持功能：
 * - 单键/多键读取
 * - 单键/多键写入
 * - CAS (Compare-And-Swap) 原子操作
 * - 迭代遍历
 */

#pragma once

#include <rados/librados.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 常量定义
 *============================================================================*/

/** OMAP 操作最大键数量 */
#define RGW_OMAP_MAX_KEYS_PER_OP        64

/** OMAP 值最大大小 (4MB) */
#define RGW_OMAP_MAX_VALUE_SIZE          (4 * 1024 * 1024)

/** OMAP 键最大长度 */
#define RGW_OMAP_MAX_KEY_LEN             256

/** OMAP 缓冲区初始大小 */
#define RGW_OMAP_BUFFER_INIT_SIZE        4096

/** OMAP 缓冲区最大大小 (防止恶意数据) */
#define RGW_OMAP_BUFFER_MAX_SIZE          (10 * 1024 * 1024)

/** 默认分页大小 */
#define RGW_OMAP_DEFAULT_PAGE_SIZE       1000

/** OMAP 比较操作类型 */
typedef enum {
    RGW_OMAP_CMP_OP_EQ = 0,    /**< 等于 */
    RGW_OMAP_CMP_OP_NE = 1,    /**< 不等于 */
    RGW_OMAP_CMP_OP_GT = 2,    /**< 大于 */
    RGW_OMAP_CMP_OP_GTE = 3,   /**< 大于等于 */
    RGW_OMAP_CMP_OP_LT = 4,    /**< 小于 */
    RGW_OMAP_CMP_OP_LTE = 5    /**< 小于等于 */
} rgw_omap_cmp_op_t;

/** OMAP 创建标志 */
typedef enum {
    RGW_OMAP_CREATE_NONE = 0,           /**< 无特殊标志 */
    RGW_OMAP_CREATE_EXCLUSIVE = 1,       /**< 独占创建 (LIBRADOS_CREATE_EXCLUSIVE) */
    RGW_OMAP_CREATE_IDEMPOTENT = 2      /**< 幂等创建 (LIBRADOS_CREATE_IDEMPOTENT) */
} rgw_omap_create_flags_t;

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief OMAP 键值对
 */
typedef struct {
    char* key;       /**< 键名 */
    uint8_t* val;   /**< 值 */
    size_t val_len;  /**< 值长度 */
} rgw_omap_kv_t;

/**
 * @brief OMAP 键值对数组
 *
 * 用于存储多个键值对的结果。
 */
typedef struct {
    rgw_omap_kv_t* kvs;      /**< 键值对数组 */
    size_t count;            /**< 键值对数量 */
    size_t capacity;         /**< 数组容量 */
} rgw_omap_kv_array_t;

/**
 * @brief OMAP 迭代器
 *
 * 用于遍历 OMAP 中的键值对。
 */
typedef struct {
    rados_omap_iter_t iter;  /**< librados 迭代器 */
    char* cur_key;          /**< 当前键 */
    uint8_t* cur_val;       /**< 当前值 */
    size_t cur_val_len;     /**< 当前值长度 */
    bool ended;              /**< 是否结束 */
} rgw_omap_iter_t;

/**
 * @brief OMAP 写入操作上下文
 *
 * 用于构建复杂的 OMAP 写入操作。
 */
typedef struct {
    rados_write_op_t* op;   /**< librados 写入操作 */
    rados_ioctx_t ioctx;     /**< IO 上下文 */
    const char* oid;         /**< 对象 ID */
    int result;              /**< 操作结果 */
} rgw_omap_write_ctx_t;

/**
 * @brief OMAP 读取操作上下文
 *
 * 用于构建复杂的 OMAP 读取操作。
 */
typedef struct {
    rados_read_op_t* op;    /**< librados 读取操作 */
    rados_ioctx_t ioctx;     /**< IO 上下文 */
    const char* oid;         /**< 对象 ID */
    int result;              /**< 操作结果 */
} rgw_omap_read_ctx_t;

/*============================================================================
 * 函数声明 - 基础操作
 *============================================================================*/

/**
 * @brief 获取单个 OMAP 值
 *
 * 从对象的 OMAP 中获取指定键的值。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key 键名
 * @param val 输出参数，返回的值（需要调用 rgw_omap_free_value 释放）
 * @param val_len 输出参数，值长度
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT 键不存在
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 rgw_omap_free_value() 释放 val
 * @see rgw_omap_free_value()
 */
int rgw_omap_get(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key,
                  uint8_t** val,
                  size_t* val_len);

/**
 * @brief 设置单个 OMAP 键值对
 *
 * 原子设置单个 OMAP 键值对。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key 键名
 * @param val 值
 * @param val_len 值长度
 * @param exclusive 是否独占创建
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EEXIST 独占模式下键已存在
 * @retval -EINVAL 参数无效
 */
int rgw_omap_set(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key,
                  const uint8_t* val,
                  size_t val_len,
                  bool exclusive);

/**
 * @brief 删除单个 OMAP 键
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key 键名
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_del(rados_ioctx_t ioctx,
                  const char* oid,
                  const char* key);

/*============================================================================
 * 函数声明 - 批量操作
 *============================================================================*/

/**
 * @brief 设置多个 OMAP 键值对
 *
 * 原子设置多个 OMAP 键值对。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param kvs 键值对数组
 * @param num_kvs 键值对数量
 * @param exclusive 是否独占创建
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EEXIST 独占模式下对象已存在
 * @retval -EINVAL 参数无效
 */
int rgw_omap_set_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const rgw_omap_kv_t* kvs,
                        size_t num_kvs,
                        bool exclusive);

/**
 * @brief 删除多个 OMAP 键
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param keys 键名数组
 * @param num_keys 键数量
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_del_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const char** keys,
                        size_t num_keys);

/**
 * @brief 获取多个 OMAP 键值对
 *
 * 获取指定键集合的所有值。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param keys 要获取的键数组，传入 NULL 则获取所有
 * @param num_keys 键数量，为 0 时获取所有
 * @param result 输出参数，返回的键值对数组
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 rgw_omap_kv_array_free() 释放 result
 * @see rgw_omap_kv_array_free()
 */
int rgw_omap_get_multi(rados_ioctx_t ioctx,
                        const char* oid,
                        const char** keys,
                        size_t num_keys,
                        rgw_omap_kv_array_t* result);

/**
 * @brief 获取所有 OMAP 键值对
 *
 * 获取对象的所有 OMAP 键值对，支持分页。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param start_after 起始键（不含），用于分页
 * @param max_return 最大返回数量
 * @param result 输出参数，返回的键值对数组
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 rgw_omap_kv_array_free() 释放 result
 * @see rgw_omap_kv_array_free()
 */
int rgw_omap_get_all(rados_ioctx_t ioctx,
                      const char* oid,
                      const char* start_after,
                      uint64_t max_return,
                      rgw_omap_kv_array_t* result);

/**
 * @brief 获取所有 OMAP 键
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param start_after 起始键
 * @param max_return 最大返回数量
 * @param keys 输出参数，返回的键数组（需要 free 释放）
 * @param keys_count 输出参数，键数量
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 *
 * @note 调用者需要使用 free() 释放 keys
 */
int rgw_omap_get_keys(rados_ioctx_t ioctx,
                      const char* oid,
                      const char* start_after,
                      uint64_t max_return,
                      char*** keys,
                      size_t* keys_count);

/*============================================================================
 * 函数声明 - CAS 操作
 *============================================================================*/

/**
 * @brief 比较并交换 OMAP 值
 *
 * 原子比较 OMAP 值，如果等于预期值则设置为新值。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key 键名
 * @param expected_val 预期值
 * @param expected_len 预期值长度
 * @param new_val 新值
 * @param new_len 新值长度
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ECANCELED 比较失败，值不匹配
 * @retval -ENOENT 键不存在
 * @retval -EINVAL 参数无效
 */
int rgw_omap_cmp_and_set(rados_ioctx_t ioctx,
                           const char* oid,
                           const char* key,
                           const uint8_t* expected_val,
                           size_t expected_len,
                           const uint8_t* new_val,
                           size_t new_len);

/*============================================================================
 * 函数声明 - 写入操作上下文
 *============================================================================*/

/**
 * @brief 创建 OMAP 写入上下文
 *
 * 创建一个 OMAP 写入上下文，用于构建复杂的写入操作。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 写入上下文，失败返回 NULL
 */
rgw_omap_write_ctx_t* rgw_omap_write_ctx_create(rados_ioctx_t ioctx,
                                                  const char* oid);

/**
 * @brief 添加 OMAP 键值对到写入上下文
 *
 * @param ctx 写入上下文
 * @param key 键名
 * @param val 值
 * @param val_len 值长度
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_write_ctx_add(rgw_omap_write_ctx_t* ctx,
                             const char* key,
                             const uint8_t* val,
                             size_t val_len);

/**
 * @brief 添加删除键操作到写入上下文
 *
 * @param ctx 写入上下文
 * @param key 要删除的键名
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_write_ctx_del(rgw_omap_write_ctx_t* ctx,
                              const char* key);

/**
 * @brief 添加断言操作到写入上下文
 *
 * 确保对象存在或版本匹配。
 *
 * @param ctx 写入上下文
 * @param check_exists 是否检查对象存在
 * @param expected_version 预期版本号，为 0 表示不检查
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_write_ctx_assert(rgw_omap_write_ctx_t* ctx,
                                 bool check_exists,
                                 uint64_t expected_version);

/**
 * @brief 添加对象创建操作到写入上下文
 *
 * @param ctx 写入上下文
 * @param flags 创建标志
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_write_ctx_create(rgw_omap_write_ctx_t* ctx,
                                 rgw_omap_create_flags_t flags);

/**
 * @brief 执行 OMAP 写入操作
 *
 * 执行之前添加到上下文的写入操作。
 *
 * @param ctx 写入上下文
 * @param mtime 输出参数，返回修改时间，可为 NULL
 * @param flags 操作标志
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT 对象不存在（使用 assert 时）
 * @retval -EEXIST 对象已存在（独占创建时）
 * @retval -ECANCELED 操作被取消（断言失败时）
 */
int rgw_omap_write_ctx_execute(rgw_omap_write_ctx_t* ctx,
                                  time_t* mtime,
                                  int flags);

/**
 * @brief 销毁 OMAP 写入上下文
 *
 * 释放写入上下文占用的资源。
 *
 * @param ctx 写入上下文
 */
void rgw_omap_write_ctx_destroy(rgw_omap_write_ctx_t* ctx);

/*============================================================================
 * 函数声明 - 读取操作上下文
 *============================================================================*/

/**
 * @brief 创建 OMAP 读取上下文
 *
 * 创建一个 OMAP 读取上下文，用于构建复杂的读取操作。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 读取上下文，失败返回 NULL
 */
rgw_omap_read_ctx_t* rgw_omap_read_ctx_create(rados_ioctx_t ioctx,
                                                const char* oid);

/**
 * @brief 添加断言操作到读取上下文
 *
 * @param ctx 读取上下文
 * @param check_exists 是否检查对象存在
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_read_ctx_assert(rgw_omap_read_ctx_t* ctx,
                                bool check_exists);

/**
 * @brief 添加获取值操作到读取上下文
 *
 * @param ctx 读取上下文
 * @param keys 要获取的键数组
 * @param num_keys 键数量
 * @param result 输出参数，返回的键值对数组
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_read_ctx_get_vals(rgw_omap_read_ctx_t* ctx,
                                  const char** keys,
                                  size_t num_keys,
                                  rgw_omap_kv_array_t* result);

/**
 * @brief 执行 OMAP 读取操作
 *
 * @param ctx 读取上下文
 * @param flags 操作标志
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT 对象不存在
 */
int rgw_omap_read_ctx_execute(rgw_omap_read_ctx_t* ctx,
                                 int flags);

/**
 * @brief 销毁 OMAP 读取上下文
 *
 * @param ctx 读取上下文
 */
void rgw_omap_read_ctx_destroy(rgw_omap_read_ctx_t* ctx);

/*============================================================================
 * 函数声明 - 迭代器
 *============================================================================*/

/**
 * @brief 创建 OMAP 迭代器
 *
 * 创建用于遍历 OMAP 键值对的迭代器。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param start_after 起始键
 * @param filter_prefix 键前缀过滤
 * @param max_return 最大返回数量
 *
 * @return 迭代器，失败返回 NULL
 */
rgw_omap_iter_t* rgw_omap_iter_create(rados_ioctx_t ioctx,
                                         const char* oid,
                                         const char* start_after,
                                         const char* filter_prefix,
                                         uint64_t max_return);

/**
 * @brief 获取迭代器的下一个键值对
 *
 * @param iter 迭代器
 * @param key 输出参数，返回键名（不需要释放）
 * @param val 输出参数，返回值（不需要释放）
 * @param val_len 输出参数，值长度
 *
 * @return 执行结果
 * @retval 0 成功，迭代结束或出错
 * @retval 1 成功获取到下一个键值对
 */
int rgw_omap_iter_next(rgw_omap_iter_t* iter,
                         const char** key,
                         const uint8_t** val,
                         size_t* val_len);

/**
 * @brief 检查迭代器是否结束
 *
 * @param iter 迭代器
 *
 * @return 是否结束
 * @retval true 迭代结束
 * @retval false 还有更多数据
 */
bool rgw_omap_iter_ended(const rgw_omap_iter_t* iter);

/**
 * @brief 销毁 OMAP 迭代器
 *
 * @param iter 迭代器
 */
void rgw_omap_iter_destroy(rgw_omap_iter_t* iter);

/*============================================================================
 * 函数声明 - 内存管理
 *============================================================================*/

/**
 * @brief 释放 OMAP 值内存
 *
 * 释放 rgw_omap_get() 返回的值内存。
 *
 * @param val 值指针
 */
void rgw_omap_free_value(uint8_t* val);

/**
 * @brief 释放 OMAP 键值对数组
 *
 * 释放 rgw_omap_get_multi() 和 rgw_omap_get_all() 返回的数组。
 *
 * @param array 键值对数组
 */
void rgw_omap_kv_array_free(rgw_omap_kv_array_t* array);

/**
 * @brief 创建 OMAP 键值对
 *
 * 分配并初始化一个 OMAP 键值对。
 *
 * @param key 键名
 * @param val 值
 * @param val_len 值长度
 *
 * @return 新建的键值对，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_omap_kv_free() 释放
 * @see rgw_omap_kv_free()
 */
rgw_omap_kv_t* rgw_omap_kv_create(const char* key,
                                     const uint8_t* val,
                                     size_t val_len);

/**
 * @brief 释放单个 OMAP 键值对
 *
 * 释放键值对的键和值内存。
 *
 * @param kv 键值对
 */
void rgw_omap_kv_free(rgw_omap_kv_t* kv);

/*============================================================================
 * 函数声明 - 工具函数
 *============================================================================*/

/**
 * @brief 检查对象是否存在
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 是否存在
 * @retval true 存在
 * @retval false 不存在
 */
bool rgw_omap_exists(rados_ioctx_t ioctx, const char* oid);

/**
 * @brief 获取 OMAP 键数量
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 键数量，失败返回 -1
 */
int64_t rgw_omap_count(rados_ioctx_t ioctx, const char* oid);

/**
 * @brief 清空对象的 OMAP
 *
 * 删除对象的所有 OMAP 键值对。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 */
int rgw_omap_clear(rados_ioctx_t ioctx, const char* oid);

/*============================================================================
 * 函数声明 - 原子事务支持 (完全事务)
 *============================================================================*/

/**
 * @brief 开始 OMAP 事务
 *
 * 开始一个 OMAP 事务，用于跨对象原子操作。
 *
 * 在简化实现中，此函数用于标记事务开始。
 * 后续操作会记录到事务日志中。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 事务 ID，失败返回 NULL
 *
 * @note 当前简化实现返回 NULL，表示不支持跨对象事务
 */
void* rgw_omap_txn_begin(rados_ioctx_t ioctx, const char* oid);

/**
 * @brief 提交 OMAP 事务
 *
 * 提交之前开始的 OMAP 事务。
 *
 * @param ioctx IO 上下文
 * @param txn 事务 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 事务无效
 */
int rgw_omap_txn_commit(rados_ioctx_t ioctx, void* txn);

/**
 * @brief 中止 OMAP 事务
 *
 * 中止并回滚 OMAP 事务。
 *
 * @param ioctx IO 上下文
 * @param txn 事务 ID
 *
 * @return 执行结果
 * @retval 0 成功
 */
int rgw_omap_txn_abort(rados_ioctx_t ioctx, void* txn);

#ifdef __cplusplus
}
#endif

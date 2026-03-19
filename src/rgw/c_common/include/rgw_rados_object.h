/**
 * @file rgw_rados_object.h
 * @brief RADOS 对象操作接口
 *
 * 定义对象存储操作的 C 接口。
 * 使用 librados.h C API 进行对象读写操作。
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

/** 对象名称最大长度 */
#define RGW_OBJECT_MAX_NAME_LEN      1024

/** 对象数据缓冲区最大大小 (64MB) */
#define RGW_OBJECT_MAX_BUFFER_SIZE   (64 * 1024 * 1024)

/** 默认读取缓冲区大小 (4MB) */
#define RGW_OBJECT_DEFAULT_READ_SIZE (4 * 1024 * 1024)

/** 默认写入缓冲区大小 (4MB) */
#define RGW_OBJECT_DEFAULT_WRITE_SIZE (4 * 1024 * 1024)

/** 最大追加大小 (5GB) */
#define RGW_OBJECT_MAX_APPEND_SIZE  (5LL * 1024 * 1024 * 1024)

/** 对象 xattr 键最大长度 */
#define RGW_OBJECT_XATTR_KEY_LEN   256

/** 对象 xattr 值最大长度 */
#define RGW_OBJECT_XATTR_VAL_LEN   4096

/** 对象 xattr 最大数量 */
#define RGW_OBJECT_MAX_XATTRS     64

/*============================================================================
 * 类型定义
 *============================================================================*/

/**
 * @brief 对象读取状态
 */
typedef enum {
    RGW_OBJECT_READ_OK = 0,       /**< 读取成功 */
    RGW_OBJECT_READ_EOF = 1,      /**< 已到达文件末尾 */
    RGW_OBJECT_READ_ERROR = 2,    /**< 读取错误 */
    RGW_OBJECT_READ_NOT_FOUND = 3 /**< 对象不存在 */
} rgw_object_read_status_t;

/**
 * @brief 对象写入标志
 */
typedef enum {
    RGW_OBJECT_WRITE_NONE = 0,          /**< 无特殊标志 */
    RGW_OBJECT_WRITE_CREATE = (1 << 0), /**< 创建新对象 */
    RGW_OBJECT_WRITE_EXCLUSIVE = (1 << 1), /**< 独占创建 */
    RGW_OBJECT_WRITE_TRUNCATE = (1 << 2), /**< 截断现有对象 */
    RGW_OBJECT_WRITE_APPEND = (1 << 3)  /**< 追加模式 */
} rgw_object_write_flags_t;

/**
 * @brief 对象元数据
 */
typedef struct {
    uint64_t size;              /**< 对象大小 */
    int64_t mtime;              /**< 修改时间 */
    uint64_t version;           /**< 版本号 */
    char* etag;                 /**< ETag */
} rgw_object_meta_t;

/**
 * @brief 对象读取上下文
 */
typedef struct {
    rados_ioctx_t ioctx;        /**< IO 上下文 */
    const char* oid;             /**< 对象 ID */
    uint8_t* buffer;           /**< 读取缓冲区 */
    size_t buffer_size;         /**< 缓冲区大小 */
    size_t bytes_read;          /**< 已读取字节数 */
    uint64_t offset;            /**< 当前读取偏移 */
    uint64_t object_size;       /**< 对象总大小 */
    int last_ret;               /**< 最后操作返回值 */
} rgw_object_read_ctx_t;

/**
 * @brief 对象写入上下文
 */
typedef struct {
    rados_ioctx_t ioctx;        /**< IO 上下文 */
    const char* oid;             /**< 对象 ID */
    uint8_t* buffer;           /**< 写入缓冲区 */
    size_t buffer_size;         /**< 缓冲区大小 */
    size_t bytes_written;        /**< 已写入字节数 */
    uint64_t offset;            /**< 当前写入偏移 */
    int write_flags;            /**< 写入标志 */
    int last_ret;               /**< 最后操作返回值 */
} rgw_object_write_ctx_t;

/**
 * @brief 对象 xattr 键值对
 */
typedef struct {
    char key[RGW_OBJECT_XATTR_KEY_LEN];  /**< 键 */
    uint8_t* val;                        /**< 值 */
    size_t val_len;                      /**< 值长度 */
} rgw_object_xattr_t;

/*============================================================================
 * 函数声明 - 基础读写
 *============================================================================*/

/**
 * @brief 创建对象读取上下文
 *
 * 创建一个用于读取对象的上下文。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param buffer_size 读取缓冲区大小
 *
 * @return 读取上下文，失败返回 NULL
 *
 * @note 调用者需要使用 rgw_object_read_ctx_destroy() 释放
 * @see rgw_object_read_ctx_destroy()
 */
rgw_object_read_ctx_t* rgw_object_read_ctx_create(rados_ioctx_t ioctx,
                                                    const char* oid,
                                                    size_t buffer_size);

/**
 * @brief 从对象读取数据
 *
 * 从对象的指定偏移读取数据。
 *
 * @param ctx 读取上下文
 * @param offset 读取偏移
 * @param size 要读取的字节数
 *
 * @return 读取状态
 * @retval RGW_OBJECT_READ_OK 成功，ctx->bytes_read 包含读取的字节数
 * @retval RGW_OBJECT_READ_EOF 已到达文件末尾
 * @retval RGW_OBJECT_READ_ERROR 读取错误
 *
 * @note 数据存储在 ctx->buffer 中
 * @see rgw_object_read_ctx_create()
 */
rgw_object_read_status_t rgw_object_read(rgw_object_read_ctx_t* ctx,
                                          uint64_t offset,
                                          size_t size);

/**
 * @brief 读取对象全部数据
 *
 * 读取对象的全部数据到缓冲区。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param bytes_read 输出参数，实际读取的字节数
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -ENOMEM 内存分配失败
 * @retval -ENODATA 对象为空
 */
int rgw_object_read_full(rados_ioctx_t ioctx,
                           const char* oid,
                           uint8_t* buffer,
                           size_t buffer_size,
                           size_t* bytes_read);

/**
 * @brief 销毁对象读取上下文
 *
 * @param ctx 读取上下文
 */
void rgw_object_read_ctx_destroy(rgw_object_read_ctx_t* ctx);

/*============================================================================
 * 函数声明 - 写入操作
 *============================================================================*/

/**
 * @brief 创建对象写入上下文
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param buffer_size 写入缓冲区大小
 *
 * @return 写入上下文，失败返回 NULL
 *
 * @see rgw_object_write_ctx_destroy()
 */
rgw_object_write_ctx_t* rgw_object_write_ctx_create(rados_ioctx_t ioctx,
                                                       const char* oid,
                                                       size_t buffer_size);

/**
 * @brief 写入数据到对象
 *
 * @param ctx 写入上下文
 * @param offset 写入偏移
 * @param data 要写入的数据
 * @param size 数据大小
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -EIO I/O 错误
 */
int rgw_object_write(rgw_object_write_ctx_t* ctx,
                       uint64_t offset,
                       const uint8_t* data,
                       size_t size);

/**
 * @brief 刷新写入缓冲区
 *
 * 将缓冲区中的数据写入到对象。
 *
 * @param ctx 写入上下文
 *
 * @return 执行结果
 */
int rgw_object_write_flush(rgw_object_write_ctx_t* ctx);

/**
 * @brief 写入完整对象
 *
 * 将缓冲区中的数据作为完整对象写入。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param data 要写入的数据
 * @param size 数据大小
 * @param exclusive 是否独占创建
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EEXIST 独占创建时对象已存在
 */
int rgw_object_write_full(rados_ioctx_t ioctx,
                           const char* oid,
                           const uint8_t* data,
                           size_t size,
                           bool exclusive);

/**
 * @brief 追加数据到对象
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param data 要追加的数据
 * @param size 数据大小
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EINVAL 参数无效
 * @retval -EIO I/O 错误
 */
int rgw_object_append(rgw_ioctx_t ioctx,
                        const char* oid,
                        const uint8_t* data,
                        size_t size);

/**
 * @brief 销毁对象写入上下文
 *
 * @param ctx 写入上下文
 */
void rgw_object_write_ctx_destroy(rgw_object_write_ctx_t* ctx);

/*============================================================================
 * 函数声明 - 删除操作
 *============================================================================*/

/**
 * @brief 删除对象
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT 对象不存在
 * @retval -EIO I/O 错误
 */
int rgw_object_delete(rados_ioctx_t ioctx, const char* oid);

/**
 * @brief 异步删除对象
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param completion 完成回调
 *
 * @return 执行结果
 */
int rgw_object_delete_async(rados_ioctx_t ioctx,
                              const char* oid,
                              rados_completion_t completion);

/*============================================================================
 * 函数声明 - 对象属性
 *============================================================================*/

/**
 * @brief 获取对象元数据
 *
 * 获取对象的大小、修改时间等元数据。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param meta 输出参数，返回对象元数据
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT 对象不存在
 * @retval -EINVAL 参数无效
 */
int rgw_object_stat(rados_ioctx_t ioctx,
                       const char* oid,
                       rgw_object_meta_t* meta);

/**
 * @brief 释放对象元数据
 *
 * @param meta 对象元数据
 */
void rgw_object_meta_free(rgw_object_meta_t* meta);

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
bool rgw_object_exists(rados_ioctx_t ioctx, const char* oid);

/**
 * @brief 设置对象 xattr
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key xattr 键
 * @param val xattr 值
 * @param val_len 值长度
 *
 * @return 执行结果
 */
int rgw_object_set_xattr(rados_ioctx_t ioctx,
                            const char* oid,
                            const char* key,
                            const uint8_t* val,
                            size_t val_len);

/**
 * @brief 获取对象 xattr
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key xattr 键
 * @param val 输出参数，返回值（需要释放）
 * @param val_len 输出参数，返回值长度
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -ENOENT xattr 不存在
 */
int rgw_object_get_xattr(rados_ioctx_t ioctx,
                           const char* oid,
                           const char* key,
                           uint8_t** val,
                           size_t* val_len);

/**
 * @brief 删除对象 xattr
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param key xattr 键
 *
 * @return 执行结果
 */
int rgw_object_del_xattr(rados_ioctx_t ioctx,
                            const char* oid,
                            const char* key);

/**
 * @brief 释放 xattr 值
 *
 * @param val xattr 值
 */
void rgw_object_xattr_free(uint8_t* val);

/*============================================================================
 * 函数声明 - 对象操作封装
 *============================================================================*/

/**
 * @brief 截断对象
 *
 * 将对象截断到指定大小。
 *
 * @param ioctx IO 上下文
 * @param oid 对象 ID
 * @param size 截断后的大小
 *
 * @return 执行结果
 * @retval 0 成功
 * @retval -EIO I/O 错误
 */
int rgw_object_truncate(rados_ioctx_t ioctx,
                          const char* oid,
                          uint64_t size);

/**
 * @brief 复制对象
 *
 * @param src_ioctx 源 IO 上下文
 * @param src_oid 源对象 ID
 * @param dst_ioctx 目标 IO 上下文
 * @param dst_oid 目标对象 ID
 *
 * @return 执行结果
 */
int rgw_object_copy(rados_ioctx_t src_ioctx,
                      const char* src_oid,
                      rados_ioctx_t dst_ioctx,
                      const char* dst_oid);

/*============================================================================
 * 函数声明 - 对象键构建
 *============================================================================*/

/**
 * @brief 构建对象 OID
 *
 * 根据桶布局和对象名构建完整的对象 OID。
 *
 * @param layout 桶布局
 * @param object_name 对象名称
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_object_build_oid(const rgw_bucket_layout_t* layout,
                           const char* object_name,
                           char* buf,
                           size_t buf_size);

/**
 * @brief 构建对象实例 OID
 *
 * 用于版本化对象的特定版本。
 *
 * @param layout 桶布局
 * @param object_name 对象名称
 * @param instance 版本 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_object_build_instance_oid(const rgw_bucket_layout_t* layout,
                                    const char* object_name,
                                    const char* instance,
                                    char* buf,
                                    size_t buf_size);

/**
 * @brief 构建桶索引对象 OID
 *
 * 构建桶的索引对象 OID。
 *
 * @param bucket_id 桶 ID
 * @param shard_id 分片 ID
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 *
 * @return 执行结果
 */
int rgw_bucket_index_oid(const char* bucket_id,
                           uint32_t shard_id,
                           char* buf,
                           size_t buf_size);

#ifdef __cplusplus
}
#endif

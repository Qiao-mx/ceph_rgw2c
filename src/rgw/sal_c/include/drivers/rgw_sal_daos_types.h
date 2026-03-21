/**
 * @file rgw_sal_daos_types.h
 * @brief DAOS 特定类型定义
 *
 * 本文件定义了 DAOS 存储后端所需的特定类型，与 sal_c 框架配合使用。
 */
#ifndef RGW_SAL_DAOS_TYPES_H
#define RGW_SAL_DAOS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * DAOS/DS3 句柄类型
 *============================================================================*/

/**
 * @brief DS3 连接句柄
 */
typedef struct ds3_connection ds3_connection_t;

/**
 * @brief DS3 桶句柄
 */
typedef struct ds3_bucket ds3_bucket_t;

/**
 * @brief DS3 对象句柄
 */
typedef struct ds3_object ds3_object_t;

/**
 * @brief DS3 分片句柄
 */
typedef struct ds3_part ds3_part_t;

/*============================================================================
 * 常量定义
 *============================================================================*/

/**
 * @brief 最大编码长度
 */
#define DS3_MAX_ENCODED_LEN (64 * 1024)  /* 64KB */

/**
 * @brief 最大桶名长度
 */
#define DS3_MAX_BUCKET_NAME 256

/**
 * @brief 最大键缓冲区长度
 */
#define DS3_MAX_KEY_BUFF 512

/**
 * @brief 最新版本实例标识
 */
#define DS3_LATEST_INSTANCE "__latest__"

/**
 * @brief 分片上传 ID 前缀
 */
#define MULTIPART_UPLOAD_ID_PREFIX "upload-"

/*============================================================================
 * DS3 API 函数指针类型
 *============================================================================*/

/**
 * @brief DS3 初始化函数
 */
typedef int (*ds3_init_fn)(void);

/**
 * @brief DS3 清理函数
 */
typedef int (*ds3_fini_fn)(void);

/**
 * @brief DS3 连接函数
 */
typedef int (*ds3_connect_fn)(const char* pool,
                               const char* cont,
                               ds3_connection_t** ds3,
                               void* args);

/**
 * @brief DS3 断开连接函数
 */
typedef int (*ds3_disconnect_fn)(ds3_connection_t* ds3, void* args);

/**
 * @brief DS3 桶创建函数
 */
typedef int (*ds3_bucket_create_fn)(const char* bucket_name,
                                     void* bucket_info,
                                     ds3_bucket_t* owner,
                                     ds3_connection_t* ds3,
                                     void* args);

/**
 * @brief DS3 桶打开函数
 */
typedef int (*ds3_bucket_open_fn)(const char* bucket_name,
                                  ds3_bucket_t** bucket,
                                  ds3_connection_t* ds3,
                                  void* args);

/**
 * @brief DS3 桶关闭函数
 */
typedef int (*ds3_bucket_close_fn)(ds3_bucket_t* bucket, void* args);

/**
 * @brief DS3 桶删除函数
 */
typedef int (*ds3_bucket_destroy_fn)(const char* bucket_name,
                                     bool delete_children,
                                     ds3_connection_t* ds3,
                                     void* args);

/**
 * @brief DS3 桶列表函数
 */
typedef int (*ds3_bucket_list_fn)(uint64_t* bucket_count,
                                   void* bucket_infos,
                                   const char* marker,
                                   bool* is_truncated,
                                   ds3_connection_t* ds3,
                                   void* args);

/**
 * @brief DS3 桶对象列表函数
 */
typedef int (*ds3_bucket_list_obj_fn)(uint32_t* obj_count,
                                      void* obj_infos,
                                      uint32_t* common_prefix_count,
                                      void* common_prefixes,
                                      const char* prefix,
                                      const char* delimiter,
                                      const char* marker,
                                      bool list_versions,
                                      bool* is_truncated,
                                      ds3_bucket_t* bucket);

/**
 * @brief DS3 桶多部分上传列表函数
 */
typedef int (*ds3_bucket_list_multipart_fn)(const char* bucket_name,
                                            uint32_t* mp_count,
                                            void* mp_infos,
                                            uint32_t* cp_count,
                                            void* common_prefixes,
                                            const char* prefix,
                                            const char* delimiter,
                                            const char* marker,
                                            bool* is_truncated,
                                            ds3_connection_t* ds3);

/**
 * @brief DS3 用户获取函数
 */
typedef int (*ds3_user_get_fn)(const char* name,
                                void* user_info,
                                ds3_connection_t* ds3,
                                void* args);

/**
 * @brief DS3 用户通过 key 获取函数
 */
typedef int (*ds3_user_get_by_key_fn)(const char* key,
                                       void* user_info,
                                       ds3_connection_t* ds3,
                                       void* args);

/**
 * @brief DS3 用户通过 email 获取函数
 */
typedef int (*ds3_user_get_by_email_fn)(const char* email,
                                         void* user_info,
                                         ds3_connection_t* ds3,
                                         void* args);

/**
 * @brief DS3 用户设置函数
 */
typedef int (*ds3_user_set_fn)(const char* name,
                                void* user_info,
                                void* old_user_info,
                                ds3_connection_t* ds3,
                                void* args);

/**
 * @brief DS3 用户删除函数
 */
typedef int (*ds3_user_remove_fn)(const char* name,
                                   void* user_info,
                                   ds3_connection_t* ds3,
                                   void* args);

/**
 * @brief DS3 对象创建函数
 */
typedef int (*ds3_obj_create_fn)(const char* obj_name,
                                  ds3_object_t** obj,
                                  ds3_bucket_t* bucket);

/**
 * @brief DS3 对象打开函数
 */
typedef int (*ds3_obj_open_fn)(const char* obj_name,
                                ds3_object_t** obj,
                                ds3_bucket_t* bucket);

/**
 * @brief DS3 对象关闭函数
 */
typedef int (*ds3_obj_close_fn)(ds3_object_t* obj);

/**
 * @brief DS3 对象删除函数
 */
typedef int (*ds3_obj_destroy_fn)(const char* obj_name,
                                   ds3_bucket_t* bucket);

/**
 * @brief DS3 对象读取函数
 */
typedef int (*ds3_obj_read_fn)(char* buffer,
                                uint64_t offset,
                                uint64_t* size,
                                ds3_bucket_t* bucket,
                                ds3_object_t* obj,
                                void* args);

/**
 * @brief DS3 对象写入函数
 */
typedef int (*ds3_obj_write_fn)(const char* buffer,
                                 uint64_t offset,
                                 uint64_t* size,
                                 ds3_bucket_t* bucket,
                                 ds3_object_t* obj,
                                 void* args);

/**
 * @brief DS3 分片打开函数
 */
typedef int (*ds3_part_open_fn)(const char* bucket_name,
                                  const char* upload_id,
                                  uint32_t part_num,
                                  bool create,
                                  ds3_part_t** part,
                                  ds3_connection_t* ds3);

/**
 * @brief DS3 分片关闭函数
 */
typedef int (*ds3_part_close_fn)(ds3_part_t* part);

/**
 * @brief DS3 分片读取函数
 */
typedef int (*ds3_part_read_fn)(char* buffer,
                                 uint64_t offset,
                                 uint64_t* size,
                                 ds3_part_t* part,
                                 ds3_connection_t* ds3,
                                 void* args);

/**
 * @brief DS3 分片写入函数
 */
typedef int (*ds3_part_write_fn)(const char* buffer,
                                  uint64_t offset,
                                  uint64_t* size,
                                  ds3_part_t* part,
                                  ds3_connection_t* ds3,
                                  void* args);

/**
 * @brief DS3 分片列表函数
 */
typedef int (*ds3_upload_list_parts_fn)(const char* bucket_name,
                                         const char* upload_id,
                                         uint32_t* part_count,
                                         void* part_infos,
                                         uint32_t* marker,
                                         bool* truncated,
                                         ds3_connection_t* ds3);

/**
 * @brief DS3 分片上传初始化函数
 */
typedef int (*ds3_upload_init_fn)(void* upload_info,
                                    const char* bucket_name,
                                    ds3_connection_t* ds3);

/**
 * @brief DS3 分片上传完成函数
 */
typedef int (*ds3_upload_complete_fn)(const char* bucket_name,
                                       const char* upload_id,
                                       ds3_connection_t* ds3);

/**
 * @brief DS3 分片上传删除函数
 */
typedef int (*ds3_upload_remove_fn)(const char* bucket_name,
                                     const char* upload_id,
                                     ds3_connection_t* ds3);

/*============================================================================
 * DS3 API 函数表
 *============================================================================*/

/**
 * @brief DS3 API 函数表
 */
typedef struct ds3_api {
    ds3_init_fn init;
    ds3_fini_fn fini;
    ds3_connect_fn connect;
    ds3_disconnect_fn disconnect;
    ds3_bucket_create_fn bucket_create;
    ds3_bucket_open_fn bucket_open;
    ds3_bucket_close_fn bucket_close;
    ds3_bucket_destroy_fn bucket_destroy;
    ds3_bucket_list_fn bucket_list;
    ds3_bucket_list_obj_fn bucket_list_obj;
    ds3_bucket_list_multipart_fn bucket_list_multipart;
    ds3_user_get_fn user_get;
    ds3_user_get_by_key_fn user_get_by_key;
    ds3_user_get_by_email_fn user_get_by_email;
    ds3_user_set_fn user_set;
    ds3_user_remove_fn user_remove;
    ds3_obj_create_fn obj_create;
    ds3_obj_open_fn obj_open;
    ds3_obj_close_fn obj_close;
    ds3_obj_destroy_fn obj_destroy;
    ds3_obj_read_fn obj_read;
    ds3_obj_write_fn obj_write;
    ds3_part_open_fn part_open;
    ds3_part_close_fn part_close;
    ds3_part_read_fn part_read;
    ds3_part_write_fn part_write;
    ds3_upload_list_parts_fn upload_list_parts;
    ds3_upload_init_fn upload_init;
    ds3_upload_complete_fn upload_complete;
    ds3_upload_remove_fn upload_remove;
} ds3_api_t;

/*============================================================================
 * 存储信息结构
 *============================================================================*/

/**
 * @brief 用户编码信息
 */
typedef struct ds3_user_info {
    const char* name;
    const char* email;
    const char** access_ids;
    size_t access_ids_nr;
    char* encoded;
    size_t encoded_length;
} ds3_user_info_t;

/**
 * @brief 桶编码信息
 */
typedef struct ds3_bucket_info {
    char name[DS3_MAX_BUCKET_NAME];
    char* encoded;
    size_t encoded_length;
} ds3_bucket_info_t;

/**
 * @brief 对象编码信息
 */
typedef struct ds3_object_info {
    char* encoded;
    size_t encoded_length;
} ds3_object_info_t;

/**
 * @brief 分片编码信息
 */
typedef struct ds3_multipart_upload_info {
    char upload_id[DS3_MAX_KEY_BUFF];
    char key[DS3_MAX_KEY_BUFF];
    char* encoded;
    size_t encoded_length;
} ds3_multipart_upload_info_t;

/**
 * @brief 分片信息
 */
typedef struct ds3_multipart_part_info {
    uint32_t part_num;
    char* encoded;
    size_t encoded_length;
} ds3_multipart_part_info_t;

/**
 * @brief 共同前缀信息
 */
typedef struct ds3_common_prefix_info {
    char prefix[DS3_MAX_KEY_BUFF];
} ds3_common_prefix_info_t;

/*============================================================================
 * DAOS 驱动内部结构
 *============================================================================*/

/**
 * @brief DAOS 驱动实现
 */
typedef struct rgw_sal_daos_driver {
    char name[64];
    char pool[256];
    char container[256];
    char pool_svc[256];
    int chunk_size;
    bool initialized;
    ds3_connection_t* ds3;
    const ds3_api_t* api;
} rgw_sal_daos_driver_t;

/**
 * @brief DAOS 用户实现
 */
typedef struct rgw_sal_daos_user {
    char* user_id;
    char* tenant;
    char* display_name;
    char* email;
    char* access_key;
    char* secret_key;
    char* ns;
    uint32_t user_type;
    int32_t max_buckets;
    void* attrs;
    uint64_t op_mask;
    bool loaded;
    time_t mtime;
    char user_oid[64];
} rgw_sal_daos_user_t;

/**
 * @brief DAOS 桶实现
 */
typedef struct rgw_sal_daos_bucket {
    char* name;
    char* tenant;
    char* marker;
    char* bucket_id;
    char* owner_id;
    char* root_path;
    void* attrs;
    void* acl;
    void* policy;
    char* tag;
    bool loaded;
    bool created;
    bool deleted;
    bool is_open;
    time_t mtime;
    ds3_bucket_t* ds3b;
    char bucket_oid[64];
} rgw_sal_daos_bucket_t;

/**
 * @brief DAOS 对象实现
 */
typedef struct rgw_sal_daos_object {
    char* name;
    char* instance;
    char* bucket_name;
    char* bucket_tenant;
    char* file_path;
    void* attrs;
    bool is_null;
    bool is_open;
    int64_t size;
    time_t mtime;
    bool written;
    bool deleted;
    bool loaded;
    bool is_atomic;
    bool is_expired;
    ds3_object_t* ds3o;
    char daos_oid[64];
    uint32_t daos_oclass;
    uint32_t daos_otype;
} rgw_sal_daos_object_t;

/**
 * @brief DAOS 分片上传实现
 */
typedef struct rgw_sal_daos_upload {
    char* bucket_name;
    char* upload_id;
    char* object_name;
    uint64_t part_size;
    void* parts;
    bool initialized;
} rgw_sal_daos_upload_t;

/**
 * @brief DAOS 写入器实现
 */
typedef struct rgw_sal_daos_writer {
    void* obj;
    uint64_t total_size;
    uint64_t accounted_size;
    bool prepared;
    bool completed;
} rgw_sal_daos_writer_t;

#ifdef __cplusplus
}
#endif

#endif /* RGW_SAL_DAOS_TYPES_H */

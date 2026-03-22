#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 类型定义 */
typedef struct { char* id; char* tenant; int32_t type; } rgw_sal_user_id_t;
typedef struct { char* name; char* tenant; char* marker; char* bucket_id; } rgw_sal_bucket_id_t;
typedef struct { char* name; char* instance; bool is_null; bool is_current; } rgw_sal_obj_key_t;
typedef struct { bool enabled; bool check_on_raw; uint64_t max_size; uint64_t max_size_kb; uint64_t max_objects; } rgw_sal_quota_info_t;
typedef struct { uint32_t epoch; char* ver; bool committed; } rgw_sal_obj_version_t;
typedef struct { rgw_sal_obj_version_t read_version; rgw_sal_obj_version_t write_version; char* obj_tag; char* instance_tag; } rgw_sal_obj_version_tracker_t;
typedef struct { char* caps; } rgw_sal_user_caps_t;

/* 用户 ID 函数 */
rgw_sal_user_id_t* rgw_sal_user_id_create(void);
void rgw_sal_user_id_destroy(rgw_sal_user_id_t* uid);

/* 桶 ID 函数 */
rgw_sal_bucket_id_t* rgw_sal_bucket_id_create(void);
void rgw_sal_bucket_id_destroy(rgw_sal_bucket_id_t* bid);

/* 对象键函数 */
rgw_sal_obj_key_t* rgw_sal_obj_key_create(void);
void rgw_sal_obj_key_destroy(rgw_sal_obj_key_t* key);

#ifdef __cplusplus
}
#endif

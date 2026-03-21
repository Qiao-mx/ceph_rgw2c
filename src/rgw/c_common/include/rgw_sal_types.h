#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { char* id; char* tenant; int32_t type; } rgw_sal_user_id_t;
typedef struct { char* name; char* tenant; char* marker; char* bucket_id; } rgw_sal_bucket_id_t;
typedef struct { char* name; char* instance; bool is_null; bool is_current; } rgw_sal_obj_key_t;
typedef struct { bool enabled; bool check_on_raw; uint64_t max_size; uint64_t max_size_kb; uint64_t max_objects; } rgw_sal_quota_info_t;
typedef struct { uint32_t epoch; char* ver; bool committed; } rgw_sal_obj_version_t;
typedef struct { rgw_sal_obj_version_t read_version; rgw_sal_obj_version_t write_version; char* obj_tag; char* instance_tag; } rgw_sal_obj_version_tracker_t;
typedef struct { char* caps; } rgw_sal_user_caps_t;
#ifdef __cplusplus
}
#endif

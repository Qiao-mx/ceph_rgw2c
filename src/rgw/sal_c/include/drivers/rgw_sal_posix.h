/**
 * @file rgw_sal_posix.h
 * @brief POSIX 文件系统存储驱动 C 接口
 */
#ifndef RGW_SAL_POSIX_H
#define RGW_SAL_POSIX_H

#include "rgw_sal.h"

/**
 * @brief POSIX 驱动配置
 */
typedef struct rgw_sal_posix_config {
    const char* root_path;       /**< 根目录路径 */
    const char* db_path;          /**< 元数据数据库路径 */
    int         max_handles;      /**< 最大文件句柄数 */
} rgw_sal_posix_config_t;

/**
 * @brief 初始化 POSIX 驱动
 * @param driver 驱动实例
 * @param config 配置参数
 * @return 0 成功, 负值失败
 */
int rgw_sal_posix_init(rgw_sal_driver_t* driver, rgw_sal_posix_config_t* config);

/**
 * @brief 关闭 POSIX 驱动
 * @param driver 驱动实例
 * @return 0 成功, 负值失败
 */
int rgw_sal_posix_shutdown(rgw_sal_driver_t* driver);

#endif /* RGW_SAL_POSIX_H */

/**
 * @file rgw_sal_daos.h
 * @brief DAOS (Intel) 对象存储驱动 C 接口
 */
#ifndef RGW_SAL_DAOS_H
#define RGW_SAL_DAOS_H

#include "rgw_sal.h"

/**
 * @brief DAOS 驱动配置
 */
typedef struct rgw_sal_daos_config {
    const char* pool_uuid;          /**< DAOS 池 UUID */
    const char* container_uuid;    /**< DAOS 容器 UUID */
    const char* pool_svc;          /**< 池服务 */
    int         chunk_size;        /**< 对象块大小 */
} rgw_sal_daos_config_t;

/**
 * @brief 初始化 DAOS 驱动
 * @param driver 驱动实例
 * @param config 配置参数
 * @return 0 成功, 负值失败
 */
int rgw_sal_daos_init(rgw_sal_driver_t* driver, rgw_sal_daos_config_t* config);

/**
 * @brief 关闭 DAOS 驱动
 * @param driver 驱动实例
 * @return 0 成功, 负值失败
 */
int rgw_sal_daos_shutdown(rgw_sal_driver_t* driver);

#endif /* RGW_SAL_DAOS_H */

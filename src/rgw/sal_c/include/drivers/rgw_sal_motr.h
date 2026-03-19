/**
 * @file rgw_sal_motr.h
 * @brief Motr (Dell EMC) 对象存储驱动 C 接口
 */
#ifndef RGW_SAL_MOTR_H
#define RGW_SAL_MOTR_H

#include "rgw_sal.h"

/**
 * @brief Motr 驱动配置
 */
typedef struct rgw_sal_motr_config {
    const char* motr_endpoint;      /**< Motr 端点地址 */
    const char* profile_fid;         /**< Profile FID */
    const char* proc_fid;            /**< Process FID */
    const char* container_id;        /**< 容器 ID */
    int         max_connections;    /**< 最大连接数 */
} rgw_sal_motr_config_t;

/**
 * @brief 初始化 Motr 驱动
 * @param driver 驱动实例
 * @param config 配置参数
 * @return 0 成功, 负值失败
 */
int rgw_sal_motr_init(rgw_sal_driver_t* driver, rgw_sal_motr_config_t* config);

/**
 * @brief 关闭 Motr 驱动
 * @param driver 驱动实例
 * @return 0 成功, 负值失败
 */
int rgw_sal_motr_shutdown(rgw_sal_driver_t* driver);

#endif /* RGW_SAL_MOTR_H */

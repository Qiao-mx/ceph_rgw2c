/**
 * @file rgw_sal_d4n.h
 * @brief D4N (Data for Nginx) 缓存驱动 C 接口
 * 
 * D4N 是一个过滤器驱动，在底层驱动基础上添加缓存功能
 */
#ifndef RGW_SAL_D4N_H
#define RGW_SAL_D4N_H

#include "rgw_sal.h"

/**
 * @brief D4N 驱动配置
 */
typedef struct rgw_sal_d4n_config {
    const char* cache_path;         /**< 缓存目录路径 */
    const char* redis_address;       /**< Redis 地址 */
    int         cache_size;         /**< 缓存大小 (MB) */
    const char* cache_policy;        /**< 缓存策略: lfuda, lru, lfu */
    rgw_sal_driver_t* next_driver;  /**< 底层驱动 */
} rgw_sal_d4n_config_t;

/**
 * @brief 初始化 D4N 驱动
 * @param driver 驱动实例
 * @param config 配置参数
 * @return 0 成功, 负值失败
 */
int rgw_sal_d4n_init(rgw_sal_driver_t* driver, rgw_sal_d4n_config_t* config);

/**
 * @brief 关闭 D4N 驱动
 * @param driver 驱动实例
 * @return 0 成功, 负值失败
 */
int rgw_sal_d4n_shutdown(rgw_sal_driver_t* driver);

#endif /* RGW_SAL_D4N_H */

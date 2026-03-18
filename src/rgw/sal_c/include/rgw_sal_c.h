/**
 * @file rgw_sal_c.h
 * @brief SAL C 接口统一头文件
 *
 * 包含所有 SAL C 接口的头文件。
 */

#pragma once

#include "rgw_sal_errors.h"
#include "rgw_sal_types.h"
#include "rgw_sal.h"

/**
 * @mainpage RGW SAL C 接口
 *
 * @section intro 简介
 *
 * RGW 存储抽象层 (SAL) 的 C 语言接口，用于将原有的 C++ SAL 实现转换为 C 接口。
 *
 * @section usage 使用方法
 *
 * @code
 * #include "rgw_sal_c.h"
 *
 * // 创建驱动
 * rgw_sal_driver_t* driver = rgw_sal_create_driver("rados", cct);
 *
 * // 初始化驱动
 * rgw_sal_init_driver(driver, cct, dpp);
 *
 * // 获取用户
 * rgw_sal_user_id_t* uid = rgw_sal_user_id_create();
 * uid->id = strdup("user1");
 * rgw_sal_user_t* user = rgw_sal_get_user(driver, uid);
 *
 * // 加载用户信息
 * rgw_sal_user_load(user, dpp, y);
 *
 * // 清理资源
 * rgw_sal_user_destroy(user);
 * rgw_sal_user_id_destroy(uid);
 * rgw_sal_destroy_driver(driver);
 * @endcode
 */

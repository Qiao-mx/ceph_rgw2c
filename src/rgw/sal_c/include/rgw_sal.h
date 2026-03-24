/**
 * @file rgw_sal.h
 * @brief SAL C 层主头文件
 *
 * 本文件是 SAL C 层的入口头文件，包含了核心的类型定义和接口。
 * 需要先包含 core/rgw_sal.h 获取完整类型定义，
 * 然后再包含 c_common 中的 SAL 定义。
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-23
 */

#pragma once

/*
 * 首先包含 core 中的 SAL 完整定义（包含 vtable 结构体）
 * 注意：必须在包含 c_common 版本之前定义这些类型
 */
#include <core/rgw_sal.h>

/*
 * 包含 c_common 中的 SAL 辅助定义
 */
#include <rgw_sal.h>

/*
 * 包含 SAL C 特定的头文件
 */
#include <rgw_sal_errors.h>
#include <rgw_sal_usage.h>

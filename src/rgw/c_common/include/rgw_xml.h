/**
 * @file rgw_xml.h
 * @brief RGW XML 解析器 C 接口
 *
 * 提供简单的 XML 解析功能，用于替代原 C++ 版本的 rgw_xml。
 *
 * 功能包括:
 * - XML 文档解析
 * - 节点遍历
 * - 节点查找
 * - 迭代器支持
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-17
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XML 错误码 */
typedef enum rgw_xml_error {
    RGW_XML_OK = 0,
    RGW_XML_ERR_PARSE = -1,
    RGW_XML_ERR_NO_MEMORY = -2,
    RGW_XML_ERR_INVALID_INPUT = -3,
    RGW_XML_ERR_NOT_FOUND = -4,
    RGW_XML_ERR_TOO_DEEP = -5
} rgw_xml_error_t;

/* 前向声明 */
typedef struct rgw_xml_doc rgw_xml_doc_t;
typedef struct rgw_xml_node rgw_xml_node_t;
typedef struct rgw_xml_iter rgw_xml_iter_t;

/**
 * @brief 创建 XML 文档
 * @return 文档指针，失败返回 NULL
 */
rgw_xml_doc_t* rgw_xml_doc_create(void);

/**
 * @brief 销毁 XML 文档
 * @param doc 文档指针
 */
void rgw_xml_doc_destroy(rgw_xml_doc_t* doc);

/**
 * @brief 从字符串解析 XML
 * @param doc 文档指针
 * @param xml_string XML 字符串
 * @return 0 成功，非0 失败
 */
int rgw_xml_doc_parse_string(rgw_xml_doc_t* doc, const char* xml_string);

/**
 * @brief 获取根节点
 * @param doc 文档指针
 * @return 根节点指针，失败返回 NULL
 */
rgw_xml_node_t* rgw_xml_doc_root(const rgw_xml_doc_t* doc);

/**
 * @brief 获取第一个子节点
 * @param node 节点指针
 * @return 子节点指针，没有返回 NULL
 */
rgw_xml_node_t* rgw_xml_node_first_child(const rgw_xml_node_t* node);

/**
 * @brief 获取下一个兄弟节点
 * @param node 节点指针
 * @return 兄弟节点指针，没有返回 NULL
 */
rgw_xml_node_t* rgw_xml_node_next_sibling(const rgw_xml_node_t* node);

/**
 * @brief 查找子节点
 * @param node 节点指针
 * @param name 子节点名称
 * @return 找到的节点指针，未找到返回 NULL
 */
rgw_xml_node_t* rgw_xml_node_find_child(const rgw_xml_node_t* node, const char* name);

/**
 * @brief 获取节点名称
 * @param node 节点指针
 * @return 节点名称字符串
 */
const char* rgw_xml_node_name(const rgw_xml_node_t* node);

/**
 * @brief 获取节点文本内容
 * @param node 节点指针
 * @return 文本内容字符串
 */
const char* rgw_xml_node_data(const rgw_xml_node_t* node);

/**
 * @brief 获取节点属性值
 * @param node 节点指针
 * @param attr 属性名
 * @return 属性值字符串，未找到返回 NULL
 */
const char* rgw_xml_node_attr(const rgw_xml_node_t* node, const char* attr);

/**
 * @brief 创建节点迭代器
 * @param node 节点指针
 * @return 迭代器指针，失败返回 NULL
 */
rgw_xml_iter_t* rgw_xml_iter_create(const rgw_xml_node_t* node);

/**
 * @brief 销毁迭代器
 * @param iter 迭代器指针
 */
void rgw_xml_iter_destroy(rgw_xml_iter_t* iter);

/**
 * @brief 获取迭代器下一个节点
 * @param iter 迭代器指针
 * @return 节点指针，没有更多节点返回 NULL
 */
rgw_xml_node_t* rgw_xml_iter_next(rgw_xml_iter_t* iter);

#ifdef __cplusplus
}
#endif

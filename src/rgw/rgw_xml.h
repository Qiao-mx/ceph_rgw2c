/**
 * @file rgw_xml.h
 * @brief RGW XML 解析器 C 接口
 *
 * 本文件提供简单的 XML 解析功能，用于替代原 C++ 版本的 rgw_xml。
 *
 * 功能包括:
 * - 简单的 XML 解析
 * - 节点数据、属性访问
 * - 子节点遍历
 *
 * @author RGW C++ 到 C 转换项目组
 * @version 1.0
 * @date 2026-03-17
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
typedef struct rgw_xml_doc rgw_xml_doc_t;
typedef struct rgw_xml_node rgw_xml_node_t;
typedef struct rgw_xml_iter rgw_xml_iter_t;

/* XML 解析错误码 */
typedef enum rgw_xml_error {
    RGW_XML_OK = 0,
    RGW_XML_ERR_INVALID = -1,
    RGW_XML_ERR_PARSE = -2,
    RGW_XML_ERR_NO_MEMORY = -3,
    RGW_XML_ERR_NOT_FOUND = -4
} rgw_xml_error_t;

/**
 * @brief 创建 XML 文档
 *
 * @return 新分配的 XML 文档，失败返回 NULL
 */
rgw_xml_doc_t *rgw_xml_doc_create(void);

/**
 * @brief 销毁 XML 文档
 *
 * @param doc XML 文档
 */
void rgw_xml_doc_destroy(rgw_xml_doc_t *doc);

/**
 * @brief 从字符串解析 XML
 *
 * @param doc XML 文档
 * @param xml_str XML 字符串
 * @return 0 成功，非0 失败
 */
int rgw_xml_doc_parse_string(rgw_xml_doc_t *doc, const char *xml_str);

/**
 * @brief 获取 XML 文档的根节点
 *
 * @param doc XML 文档
 * @return 根节点，失败返回 NULL
 */
rgw_xml_node_t *rgw_xml_doc_root(const rgw_xml_doc_t *doc);

/**
 * @brief 获取节点名称
 *
 * @param node XML 节点
 * @return 节点名称字符串
 */
const char *rgw_xml_node_name(const rgw_xml_node_t *node);

/**
 * @brief 获取节点数据（文本内容）
 *
 * @param node XML 节点
 * @return 节点数据字符串
 */
const char *rgw_xml_node_data(const rgw_xml_node_t *node);

/**
 * @brief 获取节点属性
 *
 * @param node XML 节点
 * @param attr_name 属性名称
 * @return 属性值字符串，不存在返回 NULL
 */
const char *rgw_xml_node_attr(const rgw_xml_node_t *node, const char *attr_name);

/**
 * @brief 获取第一个子节点
 *
 * @param node XML 节点
 * @return 第一个子节点，不存在返回 NULL
 */
rgw_xml_node_t *rgw_xml_node_first_child(const rgw_xml_node_t *node);

/**
 * @brief 获取下一个兄弟节点
 *
 * @param node XML 节点
 * @return 下一个兄弟节点，不存在返回 NULL
 */
rgw_xml_node_t *rgw_xml_node_next_sibling(const rgw_xml_node_t *node);

/**
 * @brief 查找第一个匹配名称的子节点
 *
 * @param node XML 节点
 * @param name 子节点名称
 * @return 匹配的子节点，不存在返回 NULL
 */
rgw_xml_node_t *rgw_xml_node_find_child(const rgw_xml_node_t *node, const char *name);

/**
 * @brief 创建节点迭代器
 *
 * @param node XML 节点
 * @return 迭代器对象
 */
rgw_xml_iter_t *rgw_xml_iter_create(const rgw_xml_node_t *node);

/**
 * @brief 获取迭代器的下一个节点
 *
 * @param iter 迭代器
 * @return 下一个节点，迭代结束返回 NULL
 */
rgw_xml_node_t *rgw_xml_iter_next(rgw_xml_iter_t *iter);

/**
 * @brief 销毁迭代器
 *
 * @param iter 迭代器
 */
void rgw_xml_iter_destroy(rgw_xml_iter_t *iter);

/**
 * @brief 将 XML 文档序列化为字符串
 *
 * @param doc XML 文档
 * @param out_str 输出字符串（需要调用者释放）
 * @return 0 成功，非0 失败
 */
int rgw_xml_doc_to_string(const rgw_xml_doc_t *doc, char **out_str);

/**
 * @brief 将节点序列化为字符串
 *
 * @param node XML 节点
 * @param indent 缩进级别
 * @param out_str 输出字符串（需要调用者释放）
 * @return 0 成功，非0 失败
 */
int rgw_xml_node_to_string(const rgw_xml_node_t *node, int indent, char **out_str);

#ifdef __cplusplus
}
#endif

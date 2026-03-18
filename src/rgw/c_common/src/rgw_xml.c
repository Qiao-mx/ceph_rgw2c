/**
 * @file rgw_xml.c
 * @brief RGW XML 解析器 C 实现
 *
 * 提供简单的 XML 解析功能，用于替代原 C++ 版本的 rgw_xml。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "rgw_xml.h"
#include "containers/rgw_cmemory.h"
#include "containers/rgw_cstring.h"

/* XML 节点结构 */
struct rgw_xml_node {
    char *name;              /* 节点名称 */
    char *data;              /* 文本内容 */
    char **attrs;            /* 属性数组 [key1, value1, key2, value2, ...] */
    size_t attr_count;       /* 属性数量 */
    struct rgw_xml_node *first_child;  /* 第一个子节点 */
    struct rgw_xml_node *next_sibling; /* 下一个兄弟节点 */
    struct rgw_xml_node *parent;        /* 父节点 */
};

/* XML 文档结构 */
struct rgw_xml_doc {
    rgw_xml_node_t *root;    /* 根节点 */
    char *xml_str;           /* 原始 XML 字符串 */
};

/* XML 迭代器结构 */
struct rgw_xml_iter {
    rgw_xml_node_t *current; /* 当前节点 */
    rgw_xml_node_t *start;  /* 起始节点 */
};

/* 跳过空白字符 */
static const char *skip_whitespace(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

/* 解析标签名称 */
static const char *parse_tag_name(const char *p, char *buf, size_t buf_size)
{
    size_t i = 0;
    while (*p && !isspace(*p) && *p != '>' && *p != '/' && i < buf_size - 1) {
        buf[i++] = *p++;
    }
    buf[i] = '\0';
    return p;
}

/* 解析属性 */
static int parse_attributes(const char **p, char ***attrs_out, size_t *count_out)
{
    char **attrs = NULL;
    size_t count = 0;
    size_t capacity = 4;
    
    attrs = (char **)rgw_c_alloc(capacity * sizeof(char *));
    if (!attrs) return RGW_XML_ERR_NO_MEMORY;
    
    while (**p && **p != '>') {
        *p = skip_whitespace(*p);
        if (**p == '/' || **p == '>') break;
        
        /* 解析属性名 */
        char name[256];
        *p = parse_tag_name(*p, name, sizeof(name));
        if (strlen(name) == 0) break;
        
        *p = skip_whitespace(*p);
        if (**p != '=') break;
        (*p)++;
        *p = skip_whitespace(*p);
        
        /* 解析属性值 */
        char quote = **p;
        if (quote != '"' && quote != '\'') break;
        (*p)++;
        
        const char *start = *p;
        while (**p && **p != quote) (*p)++;
        size_t val_len = *p - start;
        char *value = (char *)rgw_c_alloc(val_len + 1);
        if (!value) {
            for (size_t i = 0; i < count; i++) rgw_c_free(attrs[i]);
            rgw_c_free(attrs);
            return RGW_XML_ERR_NO_MEMORY;
        }
        memcpy(value, start, val_len);
        value[val_len] = '\0';
        
        if (**p) (*p)++;
        
        /* 存储属性名和值 */
        if (count + 2 >= capacity) {
            capacity *= 2;
            char **new_attrs = (char **)rgw_c_realloc(attrs, capacity * sizeof(char *));
            if (!new_attrs) {
                rgw_c_free(value);
                for (size_t i = 0; i < count; i++) rgw_c_free(attrs[i]);
                rgw_c_free(attrs);
                return RGW_XML_ERR_NO_MEMORY;
            }
            attrs = new_attrs;
        }
        
        char *name_copy = (char *)rgw_c_alloc(strlen(name) + 1);
        if (!name_copy) {
            rgw_c_free(value);
            for (size_t i = 0; i < count; i++) rgw_c_free(attrs[i]);
            rgw_c_free(attrs);
            return RGW_XML_ERR_NO_MEMORY;
        }
        strcpy(name_copy, name);
        
        attrs[count++] = name_copy;
        attrs[count++] = value;
    }
    
    *attrs_out = attrs;
    *count_out = count / 2; /* 每个属性有名称和值，所以除以2得到属性数量 */
    return RGW_XML_OK;
}

/* 创建节点 */
static rgw_xml_node_t *create_node(const char *name)
{
    rgw_xml_node_t *node = (rgw_xml_node_t *)rgw_c_alloc(sizeof(rgw_xml_node_t));
    if (!node) return NULL;
    
    node->name = (char *)rgw_c_alloc(strlen(name) + 1);
    if (!node->name) {
        rgw_c_free(node);
        return NULL;
    }
    strcpy(node->name, name);
    
    node->data = NULL;
    node->attrs = NULL;
    node->attr_count = 0;
    node->first_child = NULL;
    node->next_sibling = NULL;
    node->parent = NULL;
    
    return node;
}

/* 递归解析 XML */
static int parse_xml_recursive(const char **p, rgw_xml_node_t *parent, rgw_xml_node_t **node_out, int depth)
{
    /* 检查递归深度，防止栈溢出 */
    if (depth > 1000) {
        return RGW_XML_ERR_TOO_DEEP;
    }
    
    *p = skip_whitespace(*p);
    if (!**p) return RGW_XML_OK;
    
    if (**p != '<') return RGW_XML_ERR_PARSE;
    (*p)++;
    
    /* 检查是否为注释 */
    if ((*p)[0] == '!' && (*p)[1] == '-' && (*p)[2] == '-') {
        *p += 3;
        const char *end = strstr(*p, "-->");
        if (!end) return RGW_XML_ERR_PARSE;
        *p = end + 3;
        return parse_xml_recursive(p, parent, node_out, depth);
    }
    
    /* 解析标签名 */
    char tag_name[256];
    *p = parse_tag_name(*p, tag_name, sizeof(tag_name));
    if (strlen(tag_name) == 0) return RGW_XML_ERR_PARSE;
    
    /* 检查是否为结束标签 */
    if (tag_name[0] == '/') {
        return RGW_XML_OK;
    }
    
    /* 检查是否为自闭合标签 */
    int self_closing = 0;
    *p = skip_whitespace(*p);
    if (**p == '/') {
        self_closing = 1;
        (*p)++;
    }
    
    /* 创建节点 */
    rgw_xml_node_t *node = create_node(tag_name);
    if (!node) return RGW_XML_ERR_NO_MEMORY;
    node->parent = parent;
    
    /* 解析属性 */
    if (!self_closing) {
        int ret = parse_attributes(p, &node->attrs, &node->attr_count);
        if (ret != RGW_XML_OK) {
            rgw_c_free(node->name);
            rgw_c_free(node);
            return ret;
        }
    }
    
    /* parse_attributes已经将指针移动到了'>'字符，直接跳过它 */
    if (**p == '>') (*p)++;
    
    /* 解析子节点和文本内容 */
    if (!self_closing) {
        rgw_xml_node_t *first_child = NULL;
        rgw_xml_node_t *last_child = NULL;
        
        while (**p) {
            *p = skip_whitespace(*p);
            if (!**p || **p != '<') {
                /* 收集文本内容 */
                const char *start = *p;
                while (**p && **p != '<') (*p)++;
                size_t text_len = *p - start;
                if (text_len > 0) {
                    node->data = (char *)rgw_c_alloc(text_len + 1);
                    if (!node->data) {
                        /* 清理节点 */
                        rgw_c_free(node->name);
                        for (size_t i = 0; i < node->attr_count * 2; i++) {
                            rgw_c_free(node->attrs[i]);
                        }
                        rgw_c_free(node->attrs);
                        rgw_c_free(node);
                        return RGW_XML_ERR_NO_MEMORY;
                    }
                    memcpy(node->data, start, text_len);
                    node->data[text_len] = '\0';
                }
            }
            
            if (!**p || **p == '<') {
                if (**p == '<') {
                        /* 保存当前位置 */
                        const char *current = *p;
                        (*p)++;
                        
                        /* 检查结束标签 */
                        if (**p == '/') {
                            (*p)++;
                            char end_name[256];
                            *p = parse_tag_name(*p, end_name, sizeof(end_name));
                            if (strcmp(end_name, tag_name) != 0) {
                                /* 清理节点 */
                                rgw_c_free(node->name);
                                rgw_c_free(node->data);
                                for (size_t i = 0; i < node->attr_count * 2; i++) {
                                    rgw_c_free(node->attrs[i]);
                                }
                                rgw_c_free(node->attrs);
                                rgw_c_free(node);
                                return RGW_XML_ERR_PARSE;
                            }
                            *p = skip_whitespace(*p);
                            if (**p == '>') (*p)++;
                            break;
                        }
                        
                        /* 递归解析子节点 - 传递原始位置（包含 '<'） */
                        rgw_xml_node_t *child = NULL;
                        int ret = parse_xml_recursive(&current, node, &child, depth + 1);
                        if (ret != RGW_XML_OK) {
                            /* 清理节点 */
                            rgw_c_free(node->name);
                            rgw_c_free(node->data);
                            for (size_t i = 0; i < node->attr_count * 2; i++) {
                                rgw_c_free(node->attrs[i]);
                            }
                            rgw_c_free(node->attrs);
                            rgw_c_free(node);
                            return ret;
                        }
                        
                        /* 更新指针位置 */
                        *p = current;
                        
                        if (child) {
                            if (!first_child) {
                                first_child = child;
                            } else {
                                last_child->next_sibling = child;
                            }
                            last_child = child;
                        }
                } else {
                    break;
                }
            }
        }
        
        node->first_child = first_child;
    }
    
    *node_out = node;
    return RGW_XML_OK;
}

/* 公共 API 实现 */

rgw_xml_doc_t *rgw_xml_doc_create(void)
{
    rgw_xml_doc_t *doc = (rgw_xml_doc_t *)rgw_c_alloc(sizeof(rgw_xml_doc_t));
    if (!doc) return NULL;
    
    doc->root = NULL;
    doc->xml_str = NULL;
    
    return doc;
}

void rgw_xml_doc_destroy(rgw_xml_doc_t *doc)
{
    if (!doc) return;
    
    /* 递归释放节点 */
    typedef struct {
        rgw_xml_node_t *node;
    } StackItem;
    
    if (doc->root) {
        /* 使用栈遍历释放所有节点 */
        rgw_xml_node_t *stack[1024];
        int top = 0;
        
        stack[top++] = doc->root;
        
        while (top > 0) {
            rgw_xml_node_t *node = stack[--top];
            
            /* 将子节点压栈 */
            rgw_xml_node_t *child = node->first_child;
            while (child) {
                if (top < 1024) stack[top++] = child;
                child = child->next_sibling;
            }
            
            /* 释放节点资源 */
            rgw_c_free(node->name);
            rgw_c_free(node->data);
            for (size_t i = 0; i < node->attr_count * 2; i++) {
                rgw_c_free(node->attrs[i]);
            }
            rgw_c_free(node->attrs);
            rgw_c_free(node);
        }
    }
    
    rgw_c_free(doc->xml_str);
    rgw_c_free(doc);
}

int rgw_xml_doc_parse_string(rgw_xml_doc_t *doc, const char *xml_str)
{
    if (!doc || !xml_str) return RGW_XML_ERR_INVALID_INPUT;
    
    /* 保存原始字符串 */
    doc->xml_str = (char *)rgw_c_alloc(strlen(xml_str) + 1);
    if (!doc->xml_str) return RGW_XML_ERR_NO_MEMORY;
    strcpy(doc->xml_str, xml_str);
    
    /* 解析 XML */
    const char *p = xml_str;
    rgw_xml_node_t *first_child = NULL;
    
    int ret = parse_xml_recursive(&p, NULL, &first_child, 0);
    if (ret != RGW_XML_OK) {
        rgw_c_free(doc->xml_str);
        doc->xml_str = NULL;
        return ret;
    }
    
    doc->root = first_child;
    return RGW_XML_OK;
}

rgw_xml_node_t *rgw_xml_doc_root(const rgw_xml_doc_t *doc)
{
    if (!doc) return NULL;
    return doc->root;
}

const char *rgw_xml_node_name(const rgw_xml_node_t *node)
{
    if (!node) return NULL;
    return node->name;
}

const char *rgw_xml_node_data(const rgw_xml_node_t *node)
{
    if (!node) return NULL;
    return node->data;
}

const char *rgw_xml_node_attr(const rgw_xml_node_t *node, const char *attr_name)
{
    if (!node || !attr_name || !node->attrs) return NULL;
    
    for (size_t i = 0; i < node->attr_count; i++) {
        if (strcmp(node->attrs[i * 2], attr_name) == 0) {
            return node->attrs[i * 2 + 1];
        }
    }
    return NULL;
}

rgw_xml_node_t *rgw_xml_node_first_child(const rgw_xml_node_t *node)
{
    if (!node) return NULL;
    return node->first_child;
}

rgw_xml_node_t *rgw_xml_node_next_sibling(const rgw_xml_node_t *node)
{
    if (!node) return NULL;
    return node->next_sibling;
}

rgw_xml_node_t *rgw_xml_node_find_child(const rgw_xml_node_t *node, const char *name)
{
    if (!node || !name) return NULL;
    
    rgw_xml_node_t *child = node->first_child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            return child;
        }
        child = child->next_sibling;
    }
    return NULL;
}

rgw_xml_iter_t *rgw_xml_iter_create(const rgw_xml_node_t *node)
{
    if (!node) return NULL;
    
    rgw_xml_iter_t *iter = (rgw_xml_iter_t *)rgw_c_alloc(sizeof(rgw_xml_iter_t));
    if (!iter) return NULL;
    
    iter->current = node->first_child;
    iter->start = (rgw_xml_node_t *)node;
    
    return iter;
}

rgw_xml_node_t *rgw_xml_iter_next(rgw_xml_iter_t *iter)
{
    if (!iter || !iter->current) return NULL;
    
    rgw_xml_node_t *node = iter->current;
    iter->current = iter->current->next_sibling;
    
    return node;
}

void rgw_xml_iter_destroy(rgw_xml_iter_t *iter)
{
    rgw_c_free(iter);
}

/* 辅助函数：转义 XML 字符 */
static void xml_escape(const char *src, char **dst, size_t *dst_size)
{
    size_t needed = strlen(src) * 6 + 1;
    if (*dst_size < needed) {
        rgw_c_free(*dst);
        *dst = (char *)rgw_c_alloc(needed);
        *dst_size = needed;
    }
    
    char *p = *dst;
    while (*src) {
        switch (*src) {
            case '<': memcpy(p, "&lt;", 4); p += 4; break;
            case '>': memcpy(p, "&gt;", 4); p += 4; break;
            case '&': memcpy(p, "&amp;", 5); p += 5; break;
            case '"': memcpy(p, "&quot;", 6); p += 6; break;
            case '\'': memcpy(p, "&apos;", 6); p += 6; break;
            default: *p++ = *src; break;
        }
        src++;
    }
    *p = '\0';
}

/* 递归序列化节点 */
static int node_to_string_recursive(const rgw_xml_node_t *node, int indent, char **out)
{
    if (!node) return RGW_XML_OK;
    
    /* 缩进 */
    for (int i = 0; i < indent; i++) {
        strcat(*out, "  ");
    }
    
    /* 标签开始 */
    strcat(*out, "<");
    strcat(*out, node->name);
    
    /* 属性 */
    for (size_t i = 0; i < node->attr_count; i++) {
        strcat(*out, " ");
        strcat(*out, node->attrs[i * 2]);
        strcat(*out, "=\"");
        strcat(*out, node->attrs[i * 2 + 1]);
        strcat(*out, "\"");
    }
    
    /* 子节点或文本内容 */
    if (node->first_child) {
        strcat(*out, ">\n");
        
        rgw_xml_node_t *child = node->first_child;
        while (child) {
            int ret = node_to_string_recursive(child, indent + 1, out);
            if (ret != RGW_XML_OK) return ret;
            child = child->next_sibling;
        }
        
        /* 缩进和结束标签 */
        for (int i = 0; i < indent; i++) {
            strcat(*out, "  ");
        }
        strcat(*out, "</");
        strcat(*out, node->name);
        strcat(*out, ">\n");
    } else if (node->data) {
        strcat(*out, ">");
        
        /* 转义文本内容 */
        char *escaped = NULL;
        size_t esc_size = 0;
        xml_escape(node->data, &escaped, &esc_size);
        if (escaped) {
            strcat(*out, escaped);
            rgw_c_free(escaped);
        }
        
        strcat(*out, "</");
        strcat(*out, node->name);
        strcat(*out, ">\n");
    } else {
        strcat(*out, "/>\n");
    }
    
    return RGW_XML_OK;
}

int rgw_xml_doc_to_string(const rgw_xml_doc_t *doc, char **out_str)
{
    if (!doc || !out_str) return RGW_XML_ERR_INVALID_INPUT;
    
    *out_str = (char *)rgw_c_alloc(4096);
    if (!*out_str) return RGW_XML_ERR_NO_MEMORY;
    (*out_str)[0] = '\0';
    
    if (doc->root) {
        return node_to_string_recursive(doc->root, 0, out_str);
    }
    
    return RGW_XML_OK;
}

int rgw_xml_node_to_string(const rgw_xml_node_t *node, int indent, char **out_str)
{
    if (!node || !out_str) return RGW_XML_ERR_INVALID_INPUT;
    
    *out_str = (char *)rgw_c_alloc(4096);
    if (!*out_str) return RGW_XML_ERR_NO_MEMORY;
    (*out_str)[0] = '\0';
    
    return node_to_string_recursive(node, indent, out_str);
}

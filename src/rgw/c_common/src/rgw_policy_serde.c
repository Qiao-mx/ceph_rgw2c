/**
 * @file rgw_policy_serde.c
 * @brief IAM 策略序列化实现
 *
 * 实现 IAM 策略的二进制序列化和 JSON 序列化功能，
 * 用于 RADOS OMAP 存储。
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "rgw_policy_serde.h"
#include "rgw_errors.h"

/*============================================================================
 * 内部辅助函数
 *============================================================================*/

/**
 * @brief 计算字符串编码后的大小
 */
static size_t calc_string_size(const char* str) {
    if (!str) return sizeof(uint32_t);  /* 长度字段 */
    return sizeof(uint32_t) + strlen(str) + 1;  /* 长度 + 字符串 + 终止符 */
}

/**
 * @brief 编码字符串到缓冲区
 */
static int encode_string(const char* str, uint8_t* buf, size_t buf_size, size_t* offset) {
    if (!buf || !offset) return -EINVAL;

    uint32_t len = str ? (uint32_t)strlen(str) + 1 : 0;
    size_t total_len = sizeof(uint32_t) + len;

    if (*offset + total_len > buf_size) {
        return -ENOBUFS;
    }

    /* 写入长度 */
    memcpy(buf + *offset, &len, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* 写入字符串 */
    if (len > 0) {
        memcpy(buf + *offset, str, len);
        *offset += len;
    }

    return 0;
}

/**
 * @brief 解码字符串从缓冲区
 */
static int decode_string(const uint8_t* buf, size_t buf_len, size_t* offset, char** out_str) {
    if (!buf || !offset || !out_str) return -EINVAL;

    if (*offset + sizeof(uint32_t) > buf_len) {
        return -ENOBUFS;
    }

    /* 读取长度 */
    uint32_t len;
    memcpy(&len, buf + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    if (len == 0) {
        *out_str = NULL;
        return 0;
    }

    if (*offset + len > buf_len) {
        return -ENOBUFS;
    }

    /* 复制字符串 */
    *out_str = (char*)malloc(len);
    if (!*out_str) return -ENOMEM;

    memcpy(*out_str, buf + *offset, len);
    *offset += len;

    return 0;
}

/**
 * @brief 编码字符串数组
 */
static int encode_string_array(const char** arr, size_t num, uint8_t* buf,
                               size_t buf_size, size_t* offset) {
    if (!buf || !offset) return -EINVAL;

    /* 写入数量 */
    if (*offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
    uint32_t count = (uint32_t)num;
    memcpy(buf + *offset, &count, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* 写入每个字符串 */
    for (size_t i = 0; i < num; i++) {
        int ret = encode_string(arr[i], buf, buf_size, offset);
        if (ret < 0) return ret;
    }

    return 0;
}

/**
 * @brief 解码字符串数组
 */
static int decode_string_array(const uint8_t* buf, size_t buf_len, size_t* offset,
                               char*** out_arr, size_t* out_num) {
    if (!buf || !offset || !out_arr || !out_num) return -EINVAL;

    /* 读取数量 */
    if (*offset + sizeof(uint32_t) > buf_len) return -ENOBUFS;
    uint32_t count;
    memcpy(&count, buf + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* 分配数组 */
    char** arr = NULL;
    if (count > 0) {
        arr = (char**)calloc(count, sizeof(char*));
        if (!arr) return -ENOMEM;
    }

    /* 读取每个字符串 */
    for (uint32_t i = 0; i < count; i++) {
        int ret = decode_string(buf, buf_len, offset, &arr[i]);
        if (ret < 0) {
            for (uint32_t j = 0; j < i; j++) {
                free(arr[j]);
            }
            free(arr);
            return ret;
        }
    }

    *out_arr = arr;
    *out_num = count;
    return 0;
}

/**
 * @brief 释放字符串数组
 */
static void free_string_array(char** arr, size_t num) {
    if (!arr) return;
    for (size_t i = 0; i < num; i++) {
        free(arr[i]);
    }
    free(arr);
}

/**
 * @brief 释放条件块
 */
static void free_condition_block(rgw_policy_condition_block_t* block) {
    if (!block) return;
    if (block->conditions) {
        for (size_t i = 0; i < block->num_conditions; i++) {
            free(block->conditions[i].key);
            free_string_array(block->conditions[i].values, block->conditions[i].num_values);
        }
        free(block->conditions);
    }
}

/**
 * @brief 释放条件块数组
 */
static void free_condition_blocks(rgw_policy_condition_block_t* blocks, size_t num) {
    if (!blocks) return;
    for (size_t i = 0; i < num; i++) {
        free_condition_block(&blocks[i]);
    }
    free(blocks);
}

/**
 * @brief 计算单个条件的大小
 */
static size_t calc_condition_size(const rgw_policy_condition_t* cond) {
    if (!cond) return 0;

    size_t size = 0;
    size += sizeof(rgw_policy_condition_op_t);
    size += calc_string_size(cond->key);
    size += sizeof(uint32_t);  /* num_values */
    for (size_t i = 0; i < cond->num_values; i++) {
        size += calc_string_size(cond->values[i]);
    }
    return size;
}

/**
 * @brief 计算条件块的大小
 */
static size_t calc_condition_block_size(const rgw_policy_condition_block_t* block) {
    if (!block) return 0;

    size_t size = sizeof(uint32_t);  /* num_conditions */
    for (size_t i = 0; i < block->num_conditions; i++) {
        size += calc_condition_size(&block->conditions[i]);
    }
    return size;
}

/**
 * @brief 编码单个条件
 */
static int encode_condition(const rgw_policy_condition_t* cond, uint8_t* buf,
                            size_t buf_size, size_t* offset) {
    if (!buf || !offset) return -EINVAL;

    /* op */
    if (*offset + sizeof(rgw_policy_condition_op_t) > buf_size) return -ENOBUFS;
    memcpy(buf + *offset, &cond->op, sizeof(rgw_policy_condition_op_t));
    *offset += sizeof(rgw_policy_condition_op_t);

    /* key */
    int ret = encode_string(cond->key, buf, buf_size, offset);
    if (ret < 0) return ret;

    /* values count */
    if (*offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
    uint32_t num_values = (uint32_t)cond->num_values;
    memcpy(buf + *offset, &num_values, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* values */
    for (size_t i = 0; i < cond->num_values; i++) {
        ret = encode_string(cond->values[i], buf, buf_size, offset);
        if (ret < 0) return ret;
    }

    return 0;
}

/**
 * @brief 编码条件块
 */
static int encode_condition_block(const rgw_policy_condition_block_t* block, uint8_t* buf,
                                   size_t buf_size, size_t* offset) {
    if (!buf || !offset) return -EINVAL;

    /* conditions count */
    if (*offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
    uint32_t num_conditions = (uint32_t)block->num_conditions;
    memcpy(buf + *offset, &num_conditions, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* conditions */
    for (size_t i = 0; i < block->num_conditions; i++) {
        int ret = encode_condition(&block->conditions[i], buf, buf_size, offset);
        if (ret < 0) return ret;
    }

    return 0;
}

/**
 * @brief 解码单个条件
 */
static int decode_condition(const uint8_t* buf, size_t buf_len, size_t* offset,
                            rgw_policy_condition_t* cond) {
    if (!buf || !offset || !cond) return -EINVAL;

    memset(cond, 0, sizeof(rgw_policy_condition_t));

    /* op */
    if (*offset + sizeof(rgw_policy_condition_op_t) > buf_len) return -ENOBUFS;
    memcpy(&cond->op, buf + *offset, sizeof(rgw_policy_condition_op_t));
    *offset += sizeof(rgw_policy_condition_op_t);

    /* key */
    int ret = decode_string(buf, buf_len, offset, &cond->key);
    if (ret < 0) return ret;

    /* values count */
    if (*offset + sizeof(uint32_t) > buf_len) return -ENOBUFS;
    uint32_t num_values;
    memcpy(&num_values, buf + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* values */
    if (num_values > 0) {
        cond->values = (char**)calloc(num_values, sizeof(char*));
        if (!cond->values) return -ENOMEM;

        for (uint32_t i = 0; i < num_values; i++) {
            ret = decode_string(buf, buf_len, offset, &cond->values[i]);
            if (ret < 0) {
                for (uint32_t j = 0; j < i; j++) {
                    free(cond->values[j]);
                }
                free(cond->values);
                cond->values = NULL;
                return ret;
            }
        }
        cond->num_values = num_values;
        cond->values_capacity = num_values;
    }

    return 0;
}

/**
 * @brief 解码条件块
 */
static int decode_condition_block(const uint8_t* buf, size_t buf_len, size_t* offset,
                                   rgw_policy_condition_block_t* block) {
    if (!buf || !offset || !block) return -EINVAL;

    memset(block, 0, sizeof(rgw_policy_condition_block_t));

    /* conditions count */
    if (*offset + sizeof(uint32_t) > buf_len) return -ENOBUFS;
    uint32_t num_conditions;
    memcpy(&num_conditions, buf + *offset, sizeof(uint32_t));
    *offset += sizeof(uint32_t);

    /* conditions */
    if (num_conditions > 0) {
        block->conditions = (rgw_policy_condition_t*)malloc(
            num_conditions * sizeof(rgw_policy_condition_t));
        if (!block->conditions) return -ENOMEM;

        for (uint32_t i = 0; i < num_conditions; i++) {
            int ret = decode_condition(buf, buf_len, offset, &block->conditions[i]);
            if (ret < 0) {
                for (uint32_t j = 0; j < i; j++) {
                    free(block->conditions[j].key);
                    free_string_array(block->conditions[j].values, block->conditions[j].num_values);
                }
                free(block->conditions);
                block->conditions = NULL;
                return ret;
            }
        }
        block->num_conditions = num_conditions;
        block->conditions_capacity = num_conditions;
    }

    return 0;
}

/*============================================================================
 * 生命周期管理
 *============================================================================*/

rgw_policy_t* rgw_policy_create(void) {
    rgw_policy_t* policy = (rgw_policy_t*)calloc(1, sizeof(rgw_policy_t));
    if (!policy) return NULL;

    policy->statements_capacity = 16;
    policy->statements = (rgw_policy_statement_t*)calloc(policy->statements_capacity,
                                                          sizeof(rgw_policy_statement_t));
    if (!policy->statements) {
        free(policy);
        return NULL;
    }

    return policy;
}

void rgw_policy_destroy(rgw_policy_t* policy) {
    if (!policy) return;

    free(policy->version);
    free(policy->id);

    if (policy->statements) {
        for (size_t i = 0; i < policy->num_statements; i++) {
            rgw_policy_statement_t* stmt = &policy->statements[i];
            free(stmt->sid);
            free_string_array(stmt->actions, stmt->num_actions);
            free_string_array(stmt->resources, stmt->num_resources);
            free_string_array(stmt->not_actions, stmt->num_not_actions);
            free_string_array(stmt->not_resources, stmt->num_not_resources);

            /* 释放条件块 */
            if (stmt->conditions) {
                for (size_t j = 0; j < stmt->num_conditions; j++) {
                    rgw_policy_condition_block_t* block = &stmt->conditions[j];
                    if (block->conditions) {
                        for (size_t k = 0; k < block->num_conditions; k++) {
                            free(block->conditions[k].key);
                            free_string_array(block->conditions[k].values,
                                              block->conditions[k].num_values);
                        }
                        free(block->conditions);
                    }
                }
                free(stmt->conditions);
            }
        }
        free(policy->statements);
    }

    free(policy);
}

int rgw_policy_deep_copy(const rgw_policy_t* src, rgw_policy_t* dst) {
    if (!src || !dst) return -EINVAL;

    /* 清理目标策略中的现有数据 */
    free(dst->version);
    free(dst->id);

    if (dst->statements) {
        for (size_t i = 0; i < dst->num_statements; i++) {
            rgw_policy_statement_t* stmt = &dst->statements[i];
            free(stmt->sid);
            free_string_array(stmt->actions, stmt->num_actions);
            free_string_array(stmt->resources, stmt->num_resources);
            free_string_array(stmt->not_actions, stmt->num_not_actions);
            free_string_array(stmt->not_resources, stmt->num_not_resources);

            /* 释放条件块 */
            if (stmt->conditions) {
                for (size_t j = 0; j < stmt->num_conditions; j++) {
                    rgw_policy_condition_block_t* block = &stmt->conditions[j];
                    if (block->conditions) {
                        for (size_t k = 0; k < block->num_conditions; k++) {
                            free(block->conditions[k].key);
                            free_string_array(block->conditions[k].values,
                                              block->conditions[k].num_values);
                        }
                        free(block->conditions);
                    }
                }
                free(stmt->conditions);
            }
        }
        free(dst->statements);
    }

    /* 复制基本字段 */
    dst->version = src->version ? strdup(src->version) : NULL;
    dst->id = src->id ? strdup(src->id) : NULL;

    /* 分配 statements 数组 */
    if (src->num_statements > 0) {
        dst->statements = (rgw_policy_statement_t*)calloc(src->num_statements,
                                                          sizeof(rgw_policy_statement_t));
        if (!dst->statements) return -ENOMEM;
        dst->statements_capacity = src->num_statements;
    } else {
        dst->statements = NULL;
        dst->statements_capacity = 0;
    }

    /* 复制每个语句 */
    for (size_t i = 0; i < src->num_statements; i++) {
        const rgw_policy_statement_t* src_stmt = &src->statements[i];
        rgw_policy_statement_t* dst_stmt = &dst->statements[i];

        /* 复制基本字段 */
        dst_stmt->sid = src_stmt->sid ? strdup(src_stmt->sid) : NULL;
        dst_stmt->effect = src_stmt->effect;

        /* 复制 actions */
        if (src_stmt->num_actions > 0) {
            dst_stmt->actions = (char**)malloc(src_stmt->num_actions * sizeof(char*));
            if (!dst_stmt->actions) return -ENOMEM;
            for (size_t j = 0; j < src_stmt->num_actions; j++) {
                dst_stmt->actions[j] = src_stmt->actions[j] ? strdup(src_stmt->actions[j]) : NULL;
            }
            dst_stmt->num_actions = src_stmt->num_actions;
            dst_stmt->actions_capacity = src_stmt->num_actions;
        }

        /* 复制 resources */
        if (src_stmt->num_resources > 0) {
            dst_stmt->resources = (char**)malloc(src_stmt->num_resources * sizeof(char*));
            if (!dst_stmt->resources) return -ENOMEM;
            for (size_t j = 0; j < src_stmt->num_resources; j++) {
                dst_stmt->resources[j] = src_stmt->resources[j] ? strdup(src_stmt->resources[j]) : NULL;
            }
            dst_stmt->num_resources = src_stmt->num_resources;
            dst_stmt->resources_capacity = src_stmt->num_resources;
        }

        /* 复制 not_actions */
        if (src_stmt->num_not_actions > 0) {
            dst_stmt->not_actions = (char**)malloc(src_stmt->num_not_actions * sizeof(char*));
            if (!dst_stmt->not_actions) return -ENOMEM;
            for (size_t j = 0; j < src_stmt->num_not_actions; j++) {
                dst_stmt->not_actions[j] = src_stmt->not_actions[j] ? strdup(src_stmt->not_actions[j]) : NULL;
            }
            dst_stmt->num_not_actions = src_stmt->num_not_actions;
            dst_stmt->not_actions_capacity = src_stmt->num_not_actions;
        }

        /* 复制 not_resources */
        if (src_stmt->num_not_resources > 0) {
            dst_stmt->not_resources = (char**)malloc(src_stmt->num_not_resources * sizeof(char*));
            if (!dst_stmt->not_resources) return -ENOMEM;
            for (size_t j = 0; j < src_stmt->num_not_resources; j++) {
                dst_stmt->not_resources[j] = src_stmt->not_resources[j] ? strdup(src_stmt->not_resources[j]) : NULL;
            }
            dst_stmt->num_not_resources = src_stmt->num_not_resources;
            dst_stmt->not_resources_capacity = src_stmt->num_not_resources;
        }

        /* 复制条件块 */
        if (src_stmt->num_conditions > 0) {
            dst_stmt->conditions = (rgw_policy_condition_block_t*)calloc(
                src_stmt->num_conditions, sizeof(rgw_policy_condition_block_t));
            if (!dst_stmt->conditions) return -ENOMEM;

            for (size_t j = 0; j < src_stmt->num_conditions; j++) {
                const rgw_policy_condition_block_t* src_block = &src_stmt->conditions[j];
                rgw_policy_condition_block_t* dst_block = &dst_stmt->conditions[j];

                if (src_block->num_conditions > 0) {
                    dst_block->conditions = (rgw_policy_condition_t*)malloc(
                        src_block->num_conditions * sizeof(rgw_policy_condition_t));
                    if (!dst_block->conditions) return -ENOMEM;

                    for (size_t k = 0; k < src_block->num_conditions; k++) {
                        const rgw_policy_condition_t* src_cond = &src_block->conditions[k];
                        rgw_policy_condition_t* dst_cond = &dst_block->conditions[k];

                        dst_cond->op = src_cond->op;
                        dst_cond->key = src_cond->key ? strdup(src_cond->key) : NULL;

                        if (src_cond->num_values > 0) {
                            dst_cond->values = (char**)malloc(src_cond->num_values * sizeof(char*));
                            if (!dst_cond->values) return -ENOMEM;
                            for (size_t m = 0; m < src_cond->num_values; m++) {
                                dst_cond->values[m] = src_cond->values[m] ? strdup(src_cond->values[m]) : NULL;
                            }
                            dst_cond->num_values = src_cond->num_values;
                            dst_cond->values_capacity = src_cond->num_values;
                        }
                    }
                    dst_block->num_conditions = src_block->num_conditions;
                    dst_block->conditions_capacity = src_block->num_conditions;
                }
            }
            dst_stmt->num_conditions = src_stmt->num_conditions;
            dst_stmt->conditions_capacity = src_stmt->num_conditions;
        }
    }

    dst->num_statements = src->num_statements;
    return 0;
}

/*============================================================================
 * 语句管理
 *============================================================================*/

int rgw_policy_add_statement(rgw_policy_t* policy, const rgw_policy_statement_t* statement) {
    if (!policy || !statement) return -EINVAL;

    /* 检查容量并扩容 */
    if (policy->num_statements >= policy->statements_capacity) {
        size_t new_capacity = policy->statements_capacity * 2;
        rgw_policy_statement_t* new_stmts = (rgw_policy_statement_t*)realloc(
            policy->statements, new_capacity * sizeof(rgw_policy_statement_t));
        if (!new_stmts) return -ENOMEM;
        policy->statements = new_stmts;
        policy->statements_capacity = new_capacity;
    }

    /* 复制语句 */
    rgw_policy_statement_t* dst = &policy->statements[policy->num_statements];
    memset(dst, 0, sizeof(rgw_policy_statement_t));

    if (statement->sid) {
        dst->sid = strdup(statement->sid);
    }
    dst->effect = statement->effect;

    /* 复制操作 */
    if (statement->num_actions > 0) {
        dst->actions = (char**)malloc(statement->num_actions * sizeof(char*));
        if (!dst->actions) return -ENOMEM;
        for (size_t i = 0; i < statement->num_actions; i++) {
            dst->actions[i] = statement->actions[i] ? strdup(statement->actions[i]) : NULL;
        }
        dst->num_actions = statement->num_actions;
        dst->actions_capacity = statement->num_actions;
    }

    /* 复制资源 */
    if (statement->num_resources > 0) {
        dst->resources = (char**)malloc(statement->num_resources * sizeof(char*));
        if (!dst->resources) return -ENOMEM;
        for (size_t i = 0; i < statement->num_resources; i++) {
            dst->resources[i] = statement->resources[i] ? strdup(statement->resources[i]) : NULL;
        }
        dst->num_resources = statement->num_resources;
        dst->resources_capacity = statement->num_resources;
    }

    policy->num_statements++;
    return 0;
}

int rgw_policy_add_allow_statement(rgw_policy_t* policy,
                                    const char* action,
                                    const char* resource) {
    if (!policy) return -EINVAL;

    rgw_policy_statement_t stmt;
    memset(&stmt, 0, sizeof(stmt));

    stmt.effect = RGW_POLICY_EFFECT_ALLOW;

    if (action) {
        stmt.actions = (char**)malloc(sizeof(char*));
        if (!stmt.actions) return -ENOMEM;
        stmt.actions[0] = strdup(action);
        stmt.num_actions = 1;
        stmt.actions_capacity = 1;
    }

    if (resource) {
        stmt.resources = (char**)malloc(sizeof(char*));
        if (!stmt.resources) {
            free(stmt.actions);
            return -ENOMEM;
        }
        stmt.resources[0] = strdup(resource);
        stmt.num_resources = 1;
        stmt.resources_capacity = 1;
    }

    return rgw_policy_add_statement(policy, &stmt);
}

int rgw_policy_add_deny_statement(rgw_policy_t* policy,
                                  const char* action,
                                  const char* resource) {
    if (!policy) return -EINVAL;

    rgw_policy_statement_t stmt;
    memset(&stmt, 0, sizeof(stmt));

    stmt.effect = RGW_POLICY_EFFECT_DENY;

    if (action) {
        stmt.actions = (char**)malloc(sizeof(char*));
        if (!stmt.actions) return -ENOMEM;
        stmt.actions[0] = strdup(action);
        stmt.num_actions = 1;
        stmt.actions_capacity = 1;
    }

    if (resource) {
        stmt.resources = (char**)malloc(sizeof(char*));
        if (!stmt.resources) {
            free(stmt.actions);
            return -ENOMEM;
        }
        stmt.resources[0] = strdup(resource);
        stmt.num_resources = 1;
        stmt.resources_capacity = 1;
    }

    return rgw_policy_add_statement(policy, &stmt);
}

/*============================================================================
 * 序列化/反序列化
 *============================================================================*/

static size_t calc_statement_size(const rgw_policy_statement_t* stmt) {
    if (!stmt) return 0;

    size_t size = 0;

    /* sid */
    size += calc_string_size(stmt->sid);

    /* effect */
    size += sizeof(rgw_policy_effect_t);

    /* actions */
    size += sizeof(uint32_t);  /* 数量 */
    for (size_t i = 0; i < stmt->num_actions; i++) {
        size += calc_string_size(stmt->actions[i]);
    }

    /* resources */
    size += sizeof(uint32_t);  /* 数量 */
    for (size_t i = 0; i < stmt->num_resources; i++) {
        size += calc_string_size(stmt->resources[i]);
    }

    /* not_actions */
    size += sizeof(uint32_t);  /* 数量 */
    for (size_t i = 0; i < stmt->num_not_actions; i++) {
        size += calc_string_size(stmt->not_actions[i]);
    }

    /* not_resources */
    size += sizeof(uint32_t);  /* 数量 */
    for (size_t i = 0; i < stmt->num_not_resources; i++) {
        size += calc_string_size(stmt->not_resources[i]);
    }

    /* conditions */
    size += sizeof(uint32_t);  /* 条件块数量 */
    for (size_t i = 0; i < stmt->num_conditions; i++) {
        size += calc_condition_block_size(&stmt->conditions[i]);
    }

    return size;
}

size_t rgw_policy_calc_encode_size(const rgw_policy_t* policy) {
    if (!policy) return 0;

    size_t size = 0;

    /* 版本号 */
    size += sizeof(uint8_t);

    /* version */
    size += calc_string_size(policy->version);

    /* id */
    size += calc_string_size(policy->id);

    /* statements 数量 */
    size += sizeof(uint32_t);

    /* statements */
    for (size_t i = 0; i < policy->num_statements; i++) {
        size += calc_statement_size(&policy->statements[i]);
    }

    return size;
}

int rgw_policy_encode(const rgw_policy_t* policy, uint8_t* buf, size_t buf_size) {
    if (!policy || !buf) return -EINVAL;

    size_t offset = 0;
    int ret;

    /* 写入版本号 */
    if (offset + sizeof(uint8_t) > buf_size) return -ENOBUFS;
    uint8_t version = RGW_POLICY_ENCODE_VERSION;
    memcpy(buf + offset, &version, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    /* 写入 version */
    ret = encode_string(policy->version, buf, buf_size, &offset);
    if (ret < 0) return ret;

    /* 写入 id */
    ret = encode_string(policy->id, buf, buf_size, &offset);
    if (ret < 0) return ret;

    /* 写入 statements 数量 */
    if (offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
    uint32_t num_statements = (uint32_t)policy->num_statements;
    memcpy(buf + offset, &num_statements, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    /* 写入每个语句 */
    for (size_t i = 0; i < policy->num_statements; i++) {
        rgw_policy_statement_t* stmt = &policy->statements[i];

        /* sid */
        ret = encode_string(stmt->sid, buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* effect */
        if (offset + sizeof(rgw_policy_effect_t) > buf_size) return -ENOBUFS;
        memcpy(buf + offset, &stmt->effect, sizeof(rgw_policy_effect_t));
        offset += sizeof(rgw_policy_effect_t);

        /* actions */
        ret = encode_string_array((const char**)stmt->actions, stmt->num_actions,
                                  buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* resources */
        ret = encode_string_array((const char**)stmt->resources, stmt->num_resources,
                                  buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* not_actions */
        ret = encode_string_array((const char**)stmt->not_actions, stmt->num_not_actions,
                                  buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* not_resources */
        ret = encode_string_array((const char**)stmt->not_resources, stmt->num_not_resources,
                                  buf, buf_size, &offset);
        if (ret < 0) return ret;

        /* conditions */
        if (offset + sizeof(uint32_t) > buf_size) return -ENOBUFS;
        uint32_t num_conditions = (uint32_t)stmt->num_conditions;
        memcpy(buf + offset, &num_conditions, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        for (size_t j = 0; j < stmt->num_conditions; j++) {
            ret = encode_condition_block(&stmt->conditions[j], buf, buf_size, &offset);
            if (ret < 0) return ret;
        }
    }

    return (int)offset;
}

int rgw_policy_encode_alloc(const rgw_policy_t* policy, uint8_t** out_buf, size_t* out_len) {
    if (!policy || !out_buf || !out_len) return -EINVAL;

    size_t buf_size = rgw_policy_calc_encode_size(policy);
    if (buf_size == 0) return -EINVAL;

    uint8_t* buf = (uint8_t*)malloc(buf_size);
    if (!buf) return -ENOMEM;

    int ret = rgw_policy_encode(policy, buf, buf_size);
    if (ret < 0) {
        free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_len = (size_t)ret;
    return 0;
}

int rgw_policy_decode(const uint8_t* buf, size_t buf_len, rgw_policy_t* policy) {
    if (!buf || !policy) return -EINVAL;

    size_t offset = 0;
    int ret;

    /* 读取版本号 */
    if (offset + sizeof(uint8_t) > buf_len) return -ENOBUFS;
    uint8_t version;
    memcpy(&version, buf + offset, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    if (version != RGW_POLICY_ENCODE_VERSION) {
        return -EINVAL;  /* 不支持的版本 */
    }

    /* 读取 version */
    ret = decode_string(buf, buf_len, &offset, &policy->version);
    if (ret < 0) goto cleanup;

    /* 读取 id */
    ret = decode_string(buf, buf_len, &offset, &policy->id);
    if (ret < 0) goto cleanup;

    /* 读取 statements 数量 */
    if (offset + sizeof(uint32_t) > buf_len) {
        ret = -ENOBUFS;
        goto cleanup;
    }
    uint32_t num_statements;
    memcpy(&num_statements, buf + offset, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    /* 分配 statements 数组 */
    if (num_statements > 0) {
        if (policy->statements) {
            free(policy->statements);
        }
        policy->statements = (rgw_policy_statement_t*)calloc(num_statements,
                                                                sizeof(rgw_policy_statement_t));
        if (!policy->statements) {
            ret = -ENOMEM;
            goto cleanup;
        }
        policy->statements_capacity = num_statements;
    }
    policy->num_statements = num_statements;

    /* 读取每个语句 */
    for (uint32_t i = 0; i < num_statements; i++) {
        rgw_policy_statement_t* stmt = &policy->statements[i];

        /* sid */
        ret = decode_string(buf, buf_len, &offset, &stmt->sid);
        if (ret < 0) goto cleanup;

        /* effect */
        if (offset + sizeof(rgw_policy_effect_t) > buf_len) {
            ret = -ENOBUFS;
            goto cleanup;
        }
        memcpy(&stmt->effect, buf + offset, sizeof(rgw_policy_effect_t));
        offset += sizeof(rgw_policy_effect_t);

        /* actions */
        ret = decode_string_array(buf, buf_len, &offset, &stmt->actions, &stmt->num_actions);
        if (ret < 0) goto cleanup;
        stmt->actions_capacity = stmt->num_actions;

        /* resources */
        ret = decode_string_array(buf, buf_len, &offset, &stmt->resources, &stmt->num_resources);
        if (ret < 0) goto cleanup;
        stmt->resources_capacity = stmt->num_resources;

        /* not_actions */
        ret = decode_string_array(buf, buf_len, &offset, &stmt->not_actions, &stmt->num_not_actions);
        if (ret < 0) goto cleanup;
        stmt->not_actions_capacity = stmt->num_not_actions;

        /* not_resources */
        ret = decode_string_array(buf, buf_len, &offset, &stmt->not_resources, &stmt->num_not_resources);
        if (ret < 0) goto cleanup;
        stmt->not_resources_capacity = stmt->num_not_resources;

        /* conditions */
        if (offset + sizeof(uint32_t) > buf_len) {
            ret = -ENOBUFS;
            goto cleanup;
        }
        uint32_t num_conditions;
        memcpy(&num_conditions, buf + offset, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        if (num_conditions > 0) {
            stmt->conditions = (rgw_policy_condition_block_t*)calloc(
                num_conditions, sizeof(rgw_policy_condition_block_t));
            if (!stmt->conditions) {
                ret = -ENOMEM;
                goto cleanup;
            }

            for (uint32_t j = 0; j < num_conditions; j++) {
                ret = decode_condition_block(buf, buf_len, &offset, &stmt->conditions[j]);
                if (ret < 0) goto cleanup;
            }
            stmt->num_conditions = num_conditions;
            stmt->conditions_capacity = num_conditions;
        }
    }

    return 0;

cleanup:
    /* 清理部分解析的数据 */
    rgw_policy_destroy(policy);
    return ret;
}

/*============================================================================
 * JSON 序列化
 *============================================================================*/

/**
 * @brief 跳过空白字符
 */
static const char* policy_skip_whitespace(const char* p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

/**
 * @brief 提取 JSON 字符串值
 */
static int policy_extract_json_string(const char* json, const char* key, char** out_value) {
    if (!json || !key || !out_value) return -EINVAL;

    char search_key[128];
    snprintf(search_key, sizeof(search_key), "\"%s\"", key);

    const char* p = strstr(json, search_key);
    if (!p) {
        *out_value = NULL;
        return 0;
    }

    p += strlen(search_key);
    p = policy_skip_whitespace(p);
    if (*p != ':') return -EINVAL;
    p++;
    p = policy_skip_whitespace(p);

    if (*p != '"') return -EINVAL;
    p++;

    const char* end = p;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1]) {
            end += 2;
        } else {
            end++;
        }
    }

    if (*end != '"') return -EINVAL;

    size_t len = end - p;
    *out_value = (char*)malloc(len + 1);
    if (!*out_value) return -ENOMEM;

    memcpy(*out_value, p, len);
    (*out_value)[len] = '\0';

    return 0;
}

int rgw_policy_to_json(const rgw_policy_t* policy, char** json_str, size_t* json_len) {
    if (!policy || !json_str) return -EINVAL;

    /* 简单的 JSON 格式输出 */
    size_t buf_size = 4096;
    char* buf = (char*)malloc(buf_size);
    if (!buf) return -ENOMEM;

    size_t offset = 0;
    int written;

    written = snprintf(buf + offset, buf_size - offset, "{\"Version\":\"%s\",\"Id\":\"%s\",\"Statement\":[",
                       policy->version ? policy->version : "",
                       policy->id ? policy->id : "");
    if (written < 0 || (size_t)written >= buf_size - offset) {
        free(buf);
        return -ENOBUFS;
    }
    offset += written;

    for (size_t i = 0; i < policy->num_statements; i++) {
        if (i > 0) {
            written = snprintf(buf + offset, buf_size - offset, ",");
            if (written < 0 || (size_t)written >= buf_size - offset) {
                free(buf);
                return -ENOBUFS;
            }
            offset += written;
        }

        rgw_policy_statement_t* stmt = &policy->statements[i];

        written = snprintf(buf + offset, buf_size - offset,
                          "{\"Sid\":\"%s\",\"Effect\":\"%s\",\"Action\":[\"%s\"],\"Resource\":\"%s\"}",
                          stmt->sid ? stmt->sid : "",
                          stmt->effect == RGW_POLICY_EFFECT_ALLOW ? "Allow" : "Deny",
                          stmt->num_actions > 0 && stmt->actions[0] ? stmt->actions[0] : "",
                          stmt->num_resources > 0 && stmt->resources[0] ? stmt->resources[0] : "");
        if (written < 0 || (size_t)written >= buf_size - offset) {
            free(buf);
            return -ENOBUFS;
        }
        offset += written;
    }

    written = snprintf(buf + offset, buf_size - offset, "]}");
    if (written < 0 || (size_t)written >= buf_size - offset) {
        free(buf);
        return -ENOBUFS;
    }
    offset += written;

    *json_str = buf;
    if (json_len) *json_len = offset;
    return 0;
}

int rgw_policy_from_json(const char* json_str, size_t json_len, rgw_policy_t* policy) {
    if (!json_str || !policy) return -EINVAL;

    /* 复制 JSON 字符串并添加终止符 */
    char* json = (char*)malloc(json_len + 1);
    if (!json) return -ENOMEM;

    memcpy(json, json_str, json_len);
    json[json_len] = '\0';

    int ret = 0;

    /* 提取 version */
    char* version = NULL;
    ret = policy_extract_json_string(json, "Version", &version);
    if (ret < 0) goto cleanup;
    policy->version = version;

    /* 提取 Id */
    char* policy_id = NULL;
    ret = policy_extract_json_string(json, "Id", &policy_id);
    if (ret < 0) goto cleanup;
    policy->id = policy_id;

    /* 提取 Statement 数组 - 简化处理 */
    const char* stmt_start = strstr(json, "\"Statement\"");
    if (!stmt_start) {
        policy->num_statements = 0;
        ret = 0;
        goto cleanup;
    }

    /* 找到 '[' 开始 */
    while (*stmt_start && *stmt_start != '[') stmt_start++;
    if (*stmt_start != '[') {
        policy->num_statements = 0;
        ret = 0;
        goto cleanup;
    }
    stmt_start++;

    /* 简单计数：统计 { } 对的数量 */
    size_t stmt_count = 0;
    int brace_depth = 0;
    bool in_string = false;
    const char* p = stmt_start;

    while (*p) {
        if (*p == '"' && (p == json || p[-1] != '\\')) {
            in_string = !in_string;
        }
        if (!in_string) {
            if (*p == '{') {
                if (brace_depth == 0) stmt_count++;
                brace_depth++;
            } else if (*p == '}') {
                brace_depth--;
            } else if (*p == ']' && brace_depth == 0) {
                break;
            }
        }
        p++;
    }

    policy->num_statements = stmt_count;
    if (stmt_count > 0) {
        policy->statements = (rgw_policy_statement_t*)calloc(stmt_count,
                                                              sizeof(rgw_policy_statement_t));
        if (!policy->statements) {
            ret = -ENOMEM;
            goto cleanup;
        }
        policy->statements_capacity = stmt_count;

        /* 解析每个语句 */
        p = stmt_start;
        for (size_t i = 0; i < stmt_count; i++) {
            /* 找到下一个 { */
            while (*p && *p != '{') p++;
            if (*p != '{') break;

            const char* this_stmt_start = p;
            brace_depth = 0;
            in_string = false;

            /* 找到对应的 } */
            while (*p) {
                if (*p == '"' && (p == json || p[-1] != '\\')) {
                    in_string = !in_string;
                }
                if (!in_string) {
                    if (*p == '{') brace_depth++;
                    else if (*p == '}') {
                        brace_depth--;
                        if (brace_depth == 0) {
                            p++;
                            break;
                        }
                    }
                }
                p++;
            }

            size_t stmt_len = p - this_stmt_start;
            char* stmt_json = (char*)malloc(stmt_len + 1);
            if (!stmt_json) continue;
            memcpy(stmt_json, this_stmt_start, stmt_len);
            stmt_json[stmt_len] = '\0';

            /* 提取语句字段 */
            policy_extract_json_string(stmt_json, "Sid", &policy->statements[i].sid);

            /* 提取 Effect */
            char* effect_str = NULL;
            policy_extract_json_string(stmt_json, "Effect", &effect_str);
            if (effect_str) {
                if (strcmp(effect_str, "Allow") == 0) {
                    policy->statements[i].effect = RGW_POLICY_EFFECT_ALLOW;
                } else if (strcmp(effect_str, "Deny") == 0) {
                    policy->statements[i].effect = RGW_POLICY_EFFECT_DENY;
                }
                free(effect_str);
            }

            /* 简化处理：Action 和 Resource 只提取第一个 */
            char* action = NULL;
            policy_extract_json_string(stmt_json, "Action", &action);
            if (action) {
                policy->statements[i].actions = (char**)malloc(sizeof(char*));
                if (policy->statements[i].actions) {
                    policy->statements[i].actions[0] = action;
                    policy->statements[i].num_actions = 1;
                    policy->statements[i].actions_capacity = 1;
                } else {
                    free(action);
                }
            }

            char* resource = NULL;
            policy_extract_json_string(stmt_json, "Resource", &resource);
            if (resource) {
                policy->statements[i].resources = (char**)malloc(sizeof(char*));
                if (policy->statements[i].resources) {
                    policy->statements[i].resources[0] = resource;
                    policy->statements[i].num_resources = 1;
                    policy->statements[i].resources_capacity = 1;
                } else {
                    free(resource);
                }
            }

            free(stmt_json);
        }
    }

    ret = 0;

cleanup:
    free(json);
    return ret;
}

/*============================================================================
 * OMAP 键
 *============================================================================*/

int rgw_policy_make_bucket_omap_key(const char* bucket_name, char* buf, size_t buf_size) {
    if (!bucket_name || !buf) return -EINVAL;

    int written = snprintf(buf, buf_size, ".bucket.policy.%s", bucket_name);
    if (written < 0 || (size_t)written >= buf_size) {
        return -ENOBUFS;
    }

    return 0;
}

/*============================================================================
 * 工具函数
 *============================================================================*/

bool rgw_policy_is_empty(const rgw_policy_t* policy) {
    if (!policy) return true;
    return policy->num_statements == 0;
}

bool rgw_policy_is_valid(const rgw_policy_t* policy) {
    if (!policy) return false;

    /* 检查版本 */
    if (!policy->version) return false;
    if (strcmp(policy->version, "2012-10-17") != 0 && strcmp(policy->version, "2008-10-17") != 0) {
        return false;
    }

    /* 检查语句 */
    if (policy->num_statements == 0) return true;  /* 空策略是有效的 */

    for (size_t i = 0; i < policy->num_statements; i++) {
        rgw_policy_statement_t* stmt = &policy->statements[i];

        /* 检查效果 */
        if (stmt->effect != RGW_POLICY_EFFECT_ALLOW && stmt->effect != RGW_POLICY_EFFECT_DENY) {
            return false;
        }

        /* 检查操作和资源至少有一个 */
        if (stmt->num_actions == 0 && stmt->num_not_actions == 0) {
            return false;
        }
    }

    return true;
}

bool rgw_policy_equal(const rgw_policy_t* a, const rgw_policy_t* b) {
    if (!a || !b) return false;

    /* 比较 version */
    if ((a->version == NULL) != (b->version == NULL)) return false;
    if (a->version && strcmp(a->version, b->version) != 0) return false;

    /* 比较 id */
    if ((a->id == NULL) != (b->id == NULL)) return false;
    if (a->id && strcmp(a->id, b->id) != 0) return false;

    /* 比较 statements */
    if (a->num_statements != b->num_statements) return false;

    for (size_t i = 0; i < a->num_statements; i++) {
        rgw_policy_statement_t* stmt_a = &a->statements[i];
        rgw_policy_statement_t* stmt_b = &b->statements[i];

        if (stmt_a->effect != stmt_b->effect) return false;

        /* 比较 actions */
        if (stmt_a->num_actions != stmt_b->num_actions) return false;
        for (size_t j = 0; j < stmt_a->num_actions; j++) {
            if (strcmp(stmt_a->actions[j], stmt_b->actions[j]) != 0) return false;
        }

        /* 比较 resources */
        if (stmt_a->num_resources != stmt_b->num_resources) return false;
        for (size_t j = 0; j < stmt_a->num_resources; j++) {
            if (strcmp(stmt_a->resources[j], stmt_b->resources[j]) != 0) return false;
        }
    }

    return true;
}

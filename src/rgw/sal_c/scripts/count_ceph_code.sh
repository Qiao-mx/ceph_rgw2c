#!/bin/bash

# Ceph sal_c 目录代码行数统计脚本
# 目标目录：/mnt/d/nas/ceph-20.1.1/src/rgw/sal_c
# 统计类型：.c/.h（C语言源码），自动过滤空行、单行注释、多行注释
# 输出：总行数、有效代码行、空行、注释行

# 定义目标目录（固定为你的 sal_c 目录）
TARGET_DIR="d:/nas/ceph-20.1.1/src/rgw/sal_c"

# 检查目录是否存在
if [ ! -d "$TARGET_DIR" ]; then
    echo "错误：目标目录 $TARGET_DIR 不存在！"
    exit 1
fi

echo "========================================"
echo "开始统计目录：$TARGET_DIR"
echo "统计文件类型：.c .h"
echo "========================================"

# 初始化统计变量
total_lines=0       # 总行数
code_lines=0        # 有效代码行（非空、非注释）
empty_lines=0       # 空行
comment_lines=0     # 注释行
multi_comment=0     # 多行注释标记（0=未进入，1=进入）

# 遍历所有 .c/.h 文件
find "$TARGET_DIR" -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    echo "正在统计：$file"
    
    # 逐行处理文件（处理多行注释、空行、单行注释）
    while IFS= read -r line; do
        ((total_lines++))  # 总行数+1

        # 去除首尾空格
        trimmed_line=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')

        # 处理空行
        if [ -z "$trimmed_line" ]; then
            ((empty_lines++))
            continue
        fi

        # 处理多行注释（/* ... */）
        if [ $multi_comment -eq 1 ]; then
            ((comment_lines++))
            # 检查是否结束多行注释
            if echo "$trimmed_line" | grep -q '\*/'; then
                multi_comment=0
            fi
            continue
        fi

        # 检查是否开始多行注释
        if echo "$trimmed_line" | grep -q '^/\*'; then
            ((comment_lines++))
            # 检查是否单行多行注释（/* ... */）
            if ! echo "$trimmed_line" | grep -q '\*/'; then
                multi_comment=1
            fi
            continue
        fi

        # 处理单行注释（//）
        if echo "$trimmed_line" | grep -q '^//'; then
            ((comment_lines++))
            continue
        fi

        # 剩余为有效代码行
        ((code_lines++))
    done < "$file"
done

# 输出统计结果
echo "========================================"
echo "统计完成！结果如下："
echo "目标目录：$TARGET_DIR"
echo "----------------------------------------"
echo "总行数（含空行/注释）：$total_lines"
echo "有效代码行：$code_lines"
echo "空行：$empty_lines"
echo "注释行（单行+多行）：$comment_lines"
echo "========================================"

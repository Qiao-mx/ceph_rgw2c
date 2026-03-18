#!/bin/bash
#==============================================================================
# RGW C Common 内存检测脚本
#
# 支持三种内存检测模式:
# 1. AddressSanitizer (ASan) - 检测内存错误
# 2. Valgrind - 检测内存泄漏和错误
# 3. MemorySanitizer (MSan) - 检测未初始化内存读取
#
# 使用方法:
#   ./run_memory_tests.sh [asan|valgrind|msan|all]
#
#==============================================================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 路径配置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
SRC_DIR="$SCRIPT_DIR"

# 测试列表
TESTS=(
    "test_memory"
    "test_cmap"
    "test_cset"
    "test_carray"
    "test_cstring"
    "test_coptional"
    "test_cdeque"
    "test_cstack"
    "test_cqueue"
    "test_cpriority_queue"
    "test_oop"
    "test_errors"
    "test_buffer"
    "test_hex"
    "test_b64"
    "test_xml"
    "test_comprehensive"
)

# 帮助信息
show_help() {
    echo -e "${BLUE}RGW C Common 内存检测脚本${NC}"
    echo ""
    echo "使用方法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  asan       运行 AddressSanitizer 测试"
    echo "  valgrind   运行 Valgrind 内存泄漏检测"
    echo "  msan       运行 MemorySanitizer 测试"
    echo "  ubsan      运行 Undefined Behavior Sanitizer 测试"
    echo "  all        运行所有检测模式"
    echo "  build      仅构建测试 (不带 sanitizer)"
    echo "  help       显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 asan         # 运行 ASan 测试"
    echo "  $0 valgrind     # 运行 Valgrind 检测"
    echo "  $0 all          # 运行所有检测"
}

# 检查依赖
check_dependencies() {
    local mode=$1

    case $mode in
        asan|msan|ubsan)
            if ! command -v gcc &> /dev/null; then
                echo -e "${RED}错误: gcc 未安装${NC}"
                exit 1
            fi
            # 检查 GCC 是否支持 sanitizer
            if ! gcc -v 2>&1 | grep -q "sanitizer"; then
                echo -e "${YELLOW}警告: GCC 可能不支持 $mode${NC}"
            fi
            ;;
        valgrind)
            if ! command -v valgrind &> /dev/null; then
                echo -e "${RED}错误: valgrind 未安装${NC}"
                echo "请运行: sudo apt-get install valgrind"
                exit 1
            fi
            ;;
    esac
}

# 构建测试
build_tests() {
    local mode=$1

    echo -e "${BLUE}=== 构建测试 (mode: $mode) ===${NC}"

    # 创建构建目录
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    # 配置 CMake
    local cmake_opts="-DCMAKE_BUILD_TYPE=Debug"

    case $mode in
        asan)
            cmake_opts="$cmake_opts -DWITH_ASAN=ON"
            ;;
        msan)
            cmake_opts="$cmake_opts -DWITH_MSAN=ON"
            ;;
        ubsan)
            cmake_opts="$cmake_opts -DWITH_UBSAN=ON"
            ;;
        valgrind)
            cmake_opts="$cmake_opts -DWITH_VALGRIND=ON"
            ;;
    esac

    cmake $cmake_opts .. || {
        echo -e "${RED}CMake 配置失败${NC}"
        exit 1
    }

    # 编译
    make -j$(nproc) || {
        echo -e "${RED}编译失败${NC}"
        exit 1
    }

    echo -e "${GREEN}构建完成${NC}"
}

# 运行 AddressSanitizer 测试
run_asan() {
    echo -e "${BLUE}=== 运行 AddressSanitizer 测试 ===${NC}"

    check_dependencies "asan"
    build_tests "asan"

    local failed=0
    local passed=0

    for test in "${TESTS[@]}"; do
        if [ -f "$BUILD_DIR/$test" ]; then
            echo -n "运行 $test ... "
            if timeout 60 "$BUILD_DIR/$test" > /dev/null 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((passed++))
            else
                echo -e "${RED}FAILED${NC}"
                # 显示错误信息
                timeout 60 "$BUILD_DIR/$test" 2>&1 || true
                ((failed++))
            fi
        fi
    done

    echo ""
    echo -e "${BLUE}=== ASan 结果 ===${NC}"
    echo -e "通过: ${GREEN}$passed${NC}"
    echo -e "失败: ${RED}$failed${NC}"

    return $failed
}

# 运行 Valgrind 测试
run_valgrind() {
    echo -e "${BLUE}=== 运行 Valgrind 内存泄漏检测 ===${NC}"

    check_dependencies "valgrind"
    build_tests "valgrind"

    local failed=0
    local passed=0

    for test in "${TESTS[@]}"; do
        if [ -f "$BUILD_DIR/$test" ]; then
            echo -n "运行 $test ... "

            # 使用 valgrind 检测内存泄漏
            local valgrind_opts="--leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --error-exitcode=1"

            if timeout 120 valgrind $valgrind_opts "$BUILD_DIR/$test" > /dev/null 2>&1; then
                echo -e "${GREEN}PASSED (无内存泄漏)${NC}"
                ((passed++))
            else
                echo -e "${RED}FAILED (有内存泄漏)${NC}"
                # 显示详细错误
                echo -e "${YELLOW}--- 详细信息 ---${NC}"
                timeout 120 valgrind $valgrind_opts "$BUILD_DIR/$test" 2>&1 || true
                ((failed++))
            fi
        fi
    done

    echo ""
    echo -e "${BLUE}=== Valgrind 结果 ===${NC}"
    echo -e "通过: ${GREEN}$passed${NC}"
    echo -e "失败: ${RED}$failed${NC}"

    return $failed
}

# 运行 MemorySanitizer 测试
run_msan() {
    echo -e "${BLUE}=== 运行 MemorySanitizer 测试 ===${NC}"

    check_dependencies "msan"
    build_tests "msan"

    local failed=0
    local passed=0

    for test in "${TESTS[@]}"; do
        if [ -f "$BUILD_DIR/$test" ]; then
            echo -n "运行 $test ... "
            if timeout 60 "$BUILD_DIR/$test" > /dev/null 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((passed++))
            else
                echo -e "${RED}FAILED${NC}"
                ((failed++))
            fi
        fi
    done

    echo ""
    echo -e "${BLUE}=== MSan 结果 ===${NC}"
    echo -e "通过: ${GREEN}$passed${NC}"
    echo -e "失败: ${RED}$failed${NC}"

    return $failed
}

# 运行 UBSan 测试
run_ubsan() {
    echo -e "${BLUE}=== 运行 Undefined Behavior Sanitizer 测试 ===${NC}"

    check_dependencies "ubsan"
    build_tests "ubsan"

    local failed=0
    local passed=0

    for test in "${TESTS[@]}"; do
        if [ -f "$BUILD_DIR/$test" ]; then
            echo -n "运行 $test ... "
            if timeout 60 "$BUILD_DIR/$test" > /dev/null 2>&1; then
                echo -e "${GREEN}PASSED${NC}"
                ((passed++))
            else
                echo -e "${RED}FAILED${NC}"
                ((failed++))
            fi
        fi
    done

    echo ""
    echo -e "${BLUE}=== UBSan 结果 ===${NC}"
    echo -e "通过: ${GREEN}$passed${NC}"
    echo -e "失败: ${RED}$failed${NC}"

    return $failed
}

# 主函数
main() {
    local mode=${1:-help}

    case $mode in
        asan)
            run_asan
            ;;
        valgrind)
            run_valgrind
            ;;
        msan)
            run_msan
            ;;
        ubsan)
            run_ubsan
            ;;
        all)
            echo -e "${BLUE}=== 运行所有内存检测 ===${NC}"
            echo ""

            local total_failed=0

            run_asan || ((total_failed++))
            echo ""

            run_valgrind || ((total_failed++))
            echo ""

            run_msan || ((total_failed++))
            echo ""

            run_ubsan || ((total_failed++))
            echo ""

            if [ $total_failed -eq 0 ]; then
                echo -e "${GREEN}=== 所有检测通过! ===${NC}"
            else
                echo -e "${RED}=== 有 $total_failed 项检测失败 ===${NC}"
            fi
            ;;
        build)
            build_tests "normal"
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            echo -e "${RED}未知选项: $mode${NC}"
            show_help
            exit 1
            ;;
    esac
}

main "$@"

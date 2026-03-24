#!/bin/bash
#===============================================================================
# SAL C RADOS 驱动集成测试运行脚本
#===============================================================================

set -e  # 遇到错误时退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAL_C_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
C_COMMON_ROOT="$(cd "${SAL_C_ROOT}/../c_common" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

# 测试配置
ENABLE_ASAN="${ENABLE_ASAN:-OFF}"
TEST_FILTER="${TEST_FILTER:-}"

#===============================================================================
# 辅助函数
#===============================================================================

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo ""
    echo "========================================"
    echo "$1"
    echo "========================================"
}

#===============================================================================
# 环境检查
#===============================================================================

check_environment() {
    print_header "检查测试环境"

    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        log_error "CMake 未安装"
        exit 1
    fi
    log_info "CMake 版本: $(cmake --version | head -1)"

    # 检查 C 编译器
    if ! command -v gcc &> /dev/null; then
        log_error "GCC 未安装"
        exit 1
    fi
    log_info "GCC 版本: $(gcc --version | head -1)"

    # 检查 c_common 库
    if [ -f "${C_COMMON_ROOT}/build/librgw_c_common.a" ]; then
        log_info "找到 c_common 库: ${C_COMMON_ROOT}/build/librgw_c_common.a"
    else
        log_warn "未找到预构建的 c_common 库，将尝试构建"
    fi
}

#===============================================================================
# 构建测试
#===============================================================================

build_tests() {
    print_header "构建 SAL C 测试套件"

    # 创建构建目录
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"

    # 配置 CMake
    log_info "配置 CMake..."

    CMAKE_OPTIONS="-DCMAKE_BUILD_TYPE=Debug"
    
    if [ "${ENABLE_ASAN}" = "ON" ]; then
        CMAKE_OPTIONS="${CMAKE_OPTIONS} -DWITH_ASAN=ON"
        log_info "启用 AddressSanitizer"
    fi

    cmake .. ${CMAKE_OPTIONS}

    # 编译
    log_info "编译测试..."
    make -j$(nproc)

    log_info "构建完成"
}

#===============================================================================
# 运行测试
#===============================================================================

run_tests() {
    print_header "运行测试"

    cd "${BUILD_DIR}"

    if [ -n "${TEST_FILTER}" ]; then
        log_info "运行过滤后的测试: ${TEST_FILTER}"
        ctest --output-on-failure -R "${TEST_FILTER}"
    else
        log_info "运行所有测试"
        ctest --output-on-failure
    fi
}

#===============================================================================
# 运行单个测试
#===============================================================================

run_single_test() {
    local test_name="$1"
    
    print_header "运行测试: ${test_name}"

    cd "${BUILD_DIR}"

    if [ -f "./${test_name}" ]; then
        ./${test_name}
    else
        log_error "测试可执行文件不存在: ${test_name}"
        return 1
    fi
}

#===============================================================================
# 生成报告
#===============================================================================

generate_report() {
    local report_file="${BUILD_DIR}/test_report.txt"
    
    print_header "生成测试报告"

    cd "${BUILD_DIR}"

    {
        echo "SAL C RADOS 驱动集成测试报告"
        echo "========================================"
        echo "日期: $(date)"
        echo "构建类型: Debug"
        echo "平台: $(uname -s)"
        echo ""
        echo "测试环境:"
        echo "  CMake: $(cmake --version | head -1)"
        echo "  GCC: $(gcc --version | head -1)"
        echo ""
        echo "========================================"
        echo "测试结果"
        echo "========================================"
        
        ctest --output-on-failure -V || true
        
    } > "${report_file}"

    log_info "报告已生成: ${report_file}"
}

#===============================================================================
# 清理
#===============================================================================

clean_build() {
    print_header "清理构建目录"

    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        log_info "构建目录已清理"
    else
        log_info "构建目录不存在"
    fi
}

#===============================================================================
# 帮助信息
#===============================================================================

show_help() {
    echo "用法: $0 [选项] [命令]"
    echo ""
    echo "选项:"
    echo "  -h, --help           显示帮助信息"
    echo "  -a, --asan           启用 AddressSanitizer"
    echo "  -f, --filter REGEX   只运行匹配的测试"
    echo ""
    echo "命令:"
    echo "  build                仅构建测试"
    echo "  run                  构建并运行所有测试"
    echo "  run-basic            仅运行 test_basic"
    echo "  run-rados            仅运行 test_rados_driver"
    echo "  run-integration      仅运行 test_integration"
    echo "  report               生成测试报告"
    echo "  clean                清理构建目录"
    echo ""
    echo "环境变量:"
    echo "  ENABLE_ASAN=ON       启用 AddressSanitizer"
    echo "  TEST_FILTER=regex    测试过滤正则表达式"
    echo ""
    echo "示例:"
    echo "  $0 build                    # 仅构建"
    echo "  $0 run                      # 构建并运行"
    echo "  $0 --asan run               # 启用 ASAN 并运行"
    echo "  $0 --filter test_basic run  # 仅运行 test_basic"
}

#===============================================================================
# 主函数
#===============================================================================

main() {
    # 解析参数
    COMMAND="run"

    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help)
                show_help
                exit 0
                ;;
            -a|--asan)
                ENABLE_ASAN="ON"
                shift
                ;;
            -f|--filter)
                TEST_FILTER="$2"
                shift 2
                ;;
            build|run|run-basic|run-rados|run-integration|report|clean)
                COMMAND="$1"
                shift
                ;;
            *)
                log_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done

    # 执行命令
    case "${COMMAND}" in
        build)
            check_environment
            build_tests
            ;;
        run)
            check_environment
            build_tests
            run_tests
            ;;
        run-basic)
            check_environment
            build_tests
            run_single_test "test_basic"
            ;;
        run-rados)
            check_environment
            build_tests
            run_single_test "test_rados_driver"
            ;;
        run-integration)
            check_environment
            build_tests
            run_single_test "test_integration"
            ;;
        report)
            generate_report
            ;;
        clean)
            clean_build
            ;;
        *)
            log_error "未知命令: ${COMMAND}"
            show_help
            exit 1
            ;;
    esac
}

# 运行主函数
main "$@"

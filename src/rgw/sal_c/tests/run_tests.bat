@echo off
REM===============================================================================
REM SAL C RADOS 驱动集成测试运行脚本 (Windows)
REM===============================================================================

setlocal enabledelayedexpansion

REM 颜色定义 (Windows CMD 不原生支持颜色，这里提供文本提示)
set "GREEN=[92m"
set "RED=[91m"
set "YELLOW=[93m"
set "NC=[0m"

REM 脚本目录
set "SCRIPT_DIR=%~dp0"
set "SAL_C_ROOT=%SCRIPT_DIR%.."
set "C_COMMON_ROOT=%SAL_C_ROOT%\..\c_common"
set "BUILD_DIR=%SCRIPT_DIR%build"

REM 测试配置
set "ENABLE_ASAN=OFF"

REM===============================================================================
REM 辅助函数
REM===============================================================================

:log_info
echo [INFO] %~1
goto :eof

:log_warn
echo [WARN] %~1
goto :eof

:log_error
echo [ERROR] %~1
goto :eof

:print_header
echo.
echo ========================================
echo %~1
echo ========================================
goto :eof

REM===============================================================================
REM 环境检查
REM===============================================================================

:check_environment
call :print_header "检查测试环境"

REM 检查 CMake
where cmake >nul 2>&1
if errorlevel 1 (
    call :log_error "CMake 未安装"
    exit /b 1
)
call :log_info "CMake 已安装"
cmake --version | findstr /C:"cmake version"

REM 检查 C 编译器
where gcc >nul 2>&1
if errorlevel 1 (
    call :log_error "GCC 未安装"
    exit /b 1
)
call :log_info "GCC 已安装"
gcc --version | findstr /C:"gcc version"

REM 检查 c_common 库
if exist "%C_COMMON_ROOT%\build\librgw_c_common.a" (
    call :log_info "找到 c_common 库: %C_COMMON_ROOT%\build\librgw_c_common.a"
) else (
    call :log_warn "未找到预构建的 c_common 库"
)

goto :eof

REM===============================================================================
REM 构建测试
REM===============================================================================

:build_tests
call :print_header "构建 SAL C 测试套件"

REM 创建构建目录
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM 配置 CMake
call :log_info "配置 CMake..."

set "CMAKE_OPTIONS=-DCMAKE_BUILD_TYPE=Debug"

if "%ENABLE_ASAN%"=="ON" (
    set "CMAKE_OPTIONS=!CMAKE_OPTIONS! -DWITH_ASAN=ON"
    call :log_info "启用 AddressSanitizer"
)

cmake .. !CMAKE_OPTIONS!

REM 编译
call :log_info "编译测试..."
cmake --build . --parallel

call :log_info "构建完成"
goto :eof

REM===============================================================================
REM 运行测试
REM===============================================================================

:run_tests
call :print_header "运行测试"

cd /d "%BUILD_DIR%"

call :log_info "运行所有测试..."
ctest --output-on-failure
goto :eof

REM===============================================================================
REM 运行单个测试
REM===============================================================================

:run_single_test
set "TEST_NAME=%~1"

call :print_header "运行测试: %TEST_NAME%"

cd /d "%BUILD_DIR%"

if exist "%TEST_NAME%.exe" (
    call %TEST_NAME%.exe
) else (
    call :log_error "测试可执行文件不存在: %TEST_NAME%.exe"
    exit /b 1
)
goto :eof

REM===============================================================================
REM 生成报告
REM===============================================================================

:generate_report
set "REPORT_FILE=%BUILD_DIR%\test_report.txt"

call :print_header "生成测试报告"

cd /d "%BUILD_DIR%"

(
    echo SAL C RADOS 驱动集成测试报告
    echo ========================================
    echo 日期: %date% %time%
    echo 构建类型: Debug
    echo 平台: Windows
    echo.
    echo 测试环境:
    echo   CMake: 
    cmake --version | findstr /C:"cmake version"
    echo   GCC: 
    gcc --version | findstr /C:"gcc version"
    echo.
    echo ========================================
    echo 测试结果
    echo ========================================
    ctest --output-on-failure -V
) > "%REPORT_FILE%"

call :log_info "报告已生成: %REPORT_FILE%"
goto :eof

REM===============================================================================
REM 清理
REM===============================================================================

:clean_build
call :print_header "清理构建目录"

if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    call :log_info "构建目录已清理"
) else (
    call :log_info "构建目录不存在"
)
goto :eof

REM===============================================================================
REM 帮助信息
REM===============================================================================

:show_help
echo 用法: %~nx0 [选项] [命令]
echo.
echo 选项:
echo   -h, --help           显示帮助信息
echo   -a, --asan           启用 AddressSanitizer
echo.
echo 命令:
echo   build               仅构建测试
echo   run                 构建并运行所有测试
echo   run-basic           仅运行 test_basic
echo   run-rados          仅运行 test_rados_driver
echo   run-integration     仅运行 test_integration
echo   report              生成测试报告
echo   clean               清理构建目录
echo.
echo 示例:
echo   %~nx0 build         # 仅构建
echo   %~nx0 run           # 构建并运行
echo   %~nx0 -a run        # 启用 ASAN 并运行
goto :eof

REM===============================================================================
REM 主函数
REM===============================================================================

:main
set "COMMAND=run"

REM 解析参数
:parse_args
if "%~1"=="" goto :execute
if "%~1"=="-h" goto :show_help
if "%~1"=="--help" goto :show_help
if "%~1"=="-a" (
    set "ENABLE_ASAN=ON"
    shift
    goto :parse_args
)
if "%~1"=="--asan" (
    set "ENABLE_ASAN=ON"
    shift
    goto :parse_args
)
if "%~1"=="build" (
    set "COMMAND=build"
    shift
    goto :parse_args
)
if "%~1"=="run" (
    set "COMMAND=run"
    shift
    goto :parse_args
)
if "%~1"=="run-basic" (
    set "COMMAND=run-basic"
    shift
    goto :parse_args
)
if "%~1"=="run-rados" (
    set "COMMAND=run-rados"
    shift
    goto :parse_args
)
if "%~1"=="run-integration" (
    set "COMMAND=run-integration"
    shift
    goto :parse_args
)
if "%~1"=="report" (
    set "COMMAND=report"
    shift
    goto :parse_args
)
if "%~1"=="clean" (
    set "COMMAND=clean"
    shift
    goto :parse_args
)
call :log_error "未知选项: %~1"
goto :show_help

:execute
REM 执行命令
if "%COMMAND%"=="build" (
    call :check_environment
    call :build_tests
    exit /b 0
)
if "%COMMAND%"=="run" (
    call :check_environment
    call :build_tests
    call :run_tests
    exit /b 0
)
if "%COMMAND%"=="run-basic" (
    call :check_environment
    call :build_tests
    call :run_single_test "test_basic"
    exit /b 0
)
if "%COMMAND%"=="run-rados" (
    call :check_environment
    call :build_tests
    call :run_single_test "test_rados_driver"
    exit /b 0
)
if "%COMMAND%"=="run-integration" (
    call :check_environment
    call :build_tests
    call :run_single_test "test_integration"
    exit /b 0
)
if "%COMMAND%"=="report" (
    call :generate_report
    exit /b 0
)
if "%COMMAND%"=="clean" (
    call :clean_build
    exit /b 0
)

call :log_error "未知命令: %COMMAND%"
call :show_help
exit /b 1

REM 运行主函数
call :main %*

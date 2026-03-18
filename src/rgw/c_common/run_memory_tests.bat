@echo off
REM=============================================================================
REM RGW C Common 内存检测脚本 (Windows 版本)
REM
REM 支持三种内存检测模式:
REM 1. AddressSanitizer (ASan) - 检测内存错误
REM 2. Valgrind - 检测内存泄漏和错误
REM 3. MemorySanitizer (MSan) - 检测未初始化内存读取
REM
REM 使用方法:
REM   run_memory_tests.bat [asan|valgrind|msan|all|build|help]
REM
REM 注意: 此脚本需要在 Linux/WSL 环境下运行，或者使用 MinGW/MSYS2
REM=============================================================================

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build"

set TESTS=test_memory test_cmap test_cset test_carray test_cstring test_coptional test_cdeque test_cstack test_cqueue test_cpriority_queue test_oop test_errors test_buffer test_hex test_b64 test_xml test_comprehensive

REM 颜色定义 (ANSI)
set "RED=\033[0;31m"
set "GREEN=\033[0;32m"
set "YELLOW=\033[1;33m"
set "BLUE=\033[0;34m"
set "NC=\033[0m"

:show_help
echo.
echo RGW C Common 内存检测脚本
echo.
echo 使用方法: %0 [选项]
echo.
echo 选项:
echo   asan       运行 AddressSanitizer 测试
echo   valgrind   运行 Valgrind 内存泄漏检测
echo   msan       运行 MemorySanitizer 测试
echo   ubsan      运行 Undefined Behavior Sanitizer 测试
echo   all        运行所有检测模式
echo   build      仅构建测试 ^(不带 sanitizer^)
echo   help       显示此帮助信息
echo.
echo 示例:
echo   %0 asan         运行 ASan 测试
echo   %0 valgrind     运行 Valgrind 检测
echo   %0 all          运行所有检测
echo.
goto :eof

REM 检查依赖
:check_deps
set "mode=%~1"

if "%mode%"=="valgrind" (
    where valgrind >nul 2>&1
    if errorlevel 1 (
        echo 错误: valgrind 未安装
        echo 请运行: sudo apt-get install valgrind
        exit /b 1
    )
)
goto :eof

REM 构建测试
:build_tests
set "mode=%~1"

echo === 构建测试 ^(mode: %mode%^) ===

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

set "cmake_opts=-DCMAKE_BUILD_TYPE=Debug"

if "%mode%"=="asan" (
    set "cmake_opts=!cmake_opts! -DWITH_ASAN=ON"
) else if "%mode%"=="msan" (
    set "cmake_opts=!cmake_opts! -DWITH_MSAN=ON"
) else if "%mode%"=="ubsan" (
    set "cmake_opts=!cmake_opts! -DWITH_UBSAN=ON"
) else if "%mode%"=="valgrind" (
    set "cmake_opts=!cmake_opts! -DWITH_VALGRIND=ON"
)

cmake !cmake_opts! ..
if errorlevel 1 (
    echo CMake 配置失败
    exit /b 1
)

make -j%nproc%
if errorlevel 1 (
    echo 编译失败
    exit /b 1
)

echo 构建完成
goto :eof

REM 运行 AddressSanitizer 测试
:run_asan
echo === 运行 AddressSanitizer 测试 ===

call :check_deps asan
call :build_tests asan

set failed=0
set passed=0

for %%t in (%TESTS%) do (
    if exist "%BUILD_DIR%\%%t.exe" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t.exe" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            "%BUILD_DIR%\%%t.exe" 2>&1 || true
            set /a failed+=1
        )
    ) else if exist "%BUILD_DIR%\%%t" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            "%BUILD_DIR%\%%t" 2>&1 || true
            set /a failed+=1
        )
    )
)

echo.
echo === ASan 结果 ===
echo 通过: !passed!
echo 失败: !failed!

if !failed! gtr 0 exit /b 1
exit /b 0

REM 运行 Valgrind 测试
:run_valgrind
echo === 运行 Valgrind 内存泄漏检测 ===

call :check_deps valgrind
call :build_tests valgrind

set failed=0
set passed=0

for %%t in (%TESTS%) do (
    if exist "%BUILD_DIR%\%%t.exe" (
        echo 运行 %%t ...
        valgrind --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --error-exitcode=1 "%BUILD_DIR%\%%t.exe" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED ^(无内存泄漏^)]
            set /a passed+=1
        ) else (
            echo [FAILED ^(有内存泄漏^)]
            valgrind --leak-check=full "%BUILD_DIR%\%%t.exe" 2>&1 || true
            set /a failed+=1
        )
    ) else if exist "%BUILD_DIR%\%%t" (
        echo 运行 %%t ...
        valgrind --leak-check=full --show-leak-kinds=definite --errors-for-leak-kinds=definite --error-exitcode=1 "%BUILD_DIR%\%%t" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED ^(无内存泄漏^)]
            set /a passed+=1
        ) else (
            echo [FAILED ^(有内存泄漏^)]
            valgrind --leak-check=full "%BUILD_DIR%\%%t" 2>&1 || true
            set /a failed+=1
        )
    )
)

echo.
echo === Valgrind 结果 ===
echo 通过: !passed!
echo 失败: !failed!

if !failed! gtr 0 exit /b 1
exit /b 0

REM 运行 MemorySanitizer 测试
:run_msan
echo === 运行 MemorySanitizer 测试 ===

call :check_deps msan
call :build_tests msan

set failed=0
set passed=0

for %%t in (%TESTS%) do (
    if exist "%BUILD_DIR%\%%t.exe" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t.exe" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            set /a failed+=1
        )
    ) else if exist "%BUILD_DIR%\%%t" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            set /a failed+=1
        )
    )
)

echo.
echo === MSan 结果 ===
echo 通过: !passed!
echo 失败: !failed!

if !failed! gtr 0 exit /b 1
exit /b 0

REM 运行 UBSan 测试
:run_ubsan
echo === 运行 Undefined Behavior Sanitizer 测试 ===

call :check_deps ubsan
call :build_tests ubsan

set failed=0
set passed=0

for %%t in (%TESTS%) do (
    if exist "%BUILD_DIR%\%%t.exe" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t.exe" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            set /a failed+=1
        )
    ) else if exist "%BUILD_DIR%\%%t" (
        echo 运行 %%t ...
        "%BUILD_DIR%\%%t" >nul 2>&1
        if !errorlevel! equ 0 (
            echo [PASSED]
            set /a passed+=1
        ) else (
            echo [FAILED]
            set /a failed+=1
        )
    )
)

echo.
echo === UBSan 结果 ===
echo 通过: !passed!
echo 失败: !failed!

if !failed! gtr 0 exit /b 1
exit /b 0

REM 主函数
:main
set "mode=%~1"

if "%mode%"=="" goto show_help
if "%mode%"=="help" goto show_help
if "%mode%"=="-h" goto show_help
if "%mode%"=="--help" goto show_help

if "%mode%"=="asan" (
    call :run_asan
) else if "%mode%"=="valgrind" (
    call :run_valgrind
) else if "%mode%"=="msan" (
    call :run_msan
) else if "%mode%"=="ubsan" (
    call :run_ubsan
) else if "%mode%"=="all" (
    echo === 运行所有内存检测 ===
    echo.

    set total_failed=0

    call :run_asan
    if errorlevel 1 set /a total_failed+=1
    echo.

    call :run_valgrind
    if errorlevel 1 set /a total_failed+=1
    echo.

    call :run_msan
    if errorlevel 1 set /a total_failed+=1
    echo.

    call :run_ubsan
    if errorlevel 1 set /a total_failed+=1
    echo.

    if !total_failed! equ 0 (
        echo === 所有检测通过! ===
    ) else (
        echo === 有 !total_failed! 项检测失败 ===
    )
) else if "%mode%"=="build" (
    call :build_tests normal
) else (
    echo 未知选项: %mode%
    echo.
    goto show_help
)

endlocal

@echo off
::=============================================================================
:: WSL2 + Ubuntu 22.04 一键安装脚本
:: 右键点击此文件 -> 以管理员身份运行
::=============================================================================

echo ==============================================
echo  WSL2 + Ubuntu 22.04 安装脚本
echo ==============================================
echo.

:: 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [错误] 请右键点击此脚本，选择"以管理员身份运行"
    echo.
    pause
    exit /b 1
)

echo [1/6] 检查系统要求...
echo.

:: 检查 Windows 版本
ver | findstr /i "10\." >nul
if %errorLevel% neq 0 (
    echo [错误] 需要 Windows 10 或更高版本
    pause
    exit /b 1
)
echo [OK] Windows 版本检查通过

echo.
echo [2/6] 启用 WSL 功能...
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
if %errorLevel% neq 0 (
    echo [警告] WSL 功能启用可能失败，继续尝试...
)

echo.
echo [3/6] 启用虚拟机平台...
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
if %errorLevel% neq 0 (
    echo [警告] 虚拟机平台启用可能失败，继续尝试...
)

echo.
echo [4/6] 下载并安装 Ubuntu 22.04...
echo.

:: 检查是否已安装
wsl --list --verbose | findstr "Ubuntu-22.04" >nul
if %errorLevel% equ 0 (
    echo [跳过] Ubuntu 22.04 已安装
) else (
    echo 正在安装 Ubuntu 22.04 (这可能需要几分钟)...
    wsl --install -d Ubuntu-22.04
)

echo.
echo [5/6] 设置 WSL 默认版本为 2...
wsl --set-default-version 2

echo.
echo [6/6] 验证安装...
wsl --list --verbose

echo.
echo ==============================================
echo  安装完成!
echo ==============================================
echo.
echo [重要] 请重启电脑后继续!
echo.
echo 重启后，在开始菜单搜索 "Ubuntu" 并启动
echo 首次启动时设置您的用户名和密码
echo.
echo 重启后，运行依赖安装脚本:
echo   1. 打开 Ubuntu
echo   2. 运行: cp /mnt/c/Users/10070/Desktop/ceph_rgw2c/src/rgw/scripts/install_deps.sh ~/
echo   3. 运行: chmod +x ~/install_deps.sh
echo   4. 运行: sudo ~/install_deps.sh
echo.
pause

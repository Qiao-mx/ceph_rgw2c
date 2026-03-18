# MSYS2 安装后配置脚本
# 使用方法：
# 1. 先下载并安装MSYS2: https://www.msys2.org/
# 2. 安装路径建议使用 C:\msys64
# 3. 右键以管理员身份运行此脚本

$ErrorActionPreference = "Stop"

Write-Host "=== MSYS2 安装后配置脚本 ===" -ForegroundColor Cyan
Write-Host ""

# 检查MSYS2是否已安装
$msys2Path = "C:\msys64"
if (-not (Test-Path $msys2Path)) {
    $msys2Path = "C:\msys32"
}

if (-not (Test-Path "$msys2Path\msys2_shell.cmd")) {
    Write-Host "错误: 未找到MSYS2安装目录" -ForegroundColor Red
    Write-Host "请先下载并安装MSYS2: https://www.msys2.org/" -ForegroundColor Yellow
    Write-Host "安装路径建议使用 C:\msys64" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "MSYS2 安装步骤:" -ForegroundColor Cyan
    Write-Host "1. 访问 https://www.msys2.org/ 下载安装包"
    Write-Host "2. 运行安装包，安装到 C:\msys64"
    Write-Host "3. 安装完成后会打开MSYS2终端"
    Write-Host "4. 在终端中执行: pacman -Syuu"
    Write-Host "5. 关闭终端，重新打开后执行: pacman -S mingw-w64-x86_64-gcc"
    Write-Host "6. 配置环境变量: C:\msys64\mingw64\bin"
    Read-Host "`n按回车键退出"
    exit 1
}

Write-Host "找到MSYS2安装目录: $msys2Path" -ForegroundColor Green
Write-Host ""

# 检查是否已经有gcc
$gccCheck = Test-Path "$msys2Path\mingw64\bin\gcc.exe"
$cmakeCheck = Test-Path "$msys2Path\mingw64\bin\cmake.exe"

if ($gccCheck -and $cmakeCheck) {
    Write-Host "编译工具链已安装!" -ForegroundColor Green
} else {
    Write-Host "编译工具链未安装，开始安装..." -ForegroundColor Yellow
    Write-Host ""
}

# 1. 更新pacman数据库
Write-Host "[1/4] 正在更新pacman数据库..." -ForegroundColor Cyan
Write-Host "      (如果卡住，请手动在MSYS2 MinGW 64-bit终端中执行: pacman -Syuu)" -ForegroundColor Gray
$env:MSYSTEM = "MINGW64"
& "$msys2Path\usr\bin\bash.exe" -lc "pacman -Syuu --noconfirm" 2>&1 | Out-Null
Write-Host "更新完成" -ForegroundColor Green
Write-Host ""

# 2. 安装编译工具链
Write-Host "[2/4] 正在安装编译工具链..." -ForegroundColor Cyan
Write-Host "      需要安装: gcc, make, cmake, gdb" -ForegroundColor Gray
$packages = @(
    "mingw-w64-x86_64-gcc",
    "mingw-w64-x86_64-gcc-libs",
    "mingw-w64-x86_64-make",
    "mingw-w64-x86_64-cmake",
    "mingw-w64-x86_64-gdb"
)
$pkgString = $packages -join " "

# 尝试安装，如果失败则提示手动安装
try {
    & "$msys2Path\usr\bin\bash.exe" -lc "pacman -S --noconfirm $pkgString" 2>&1 | Out-Null
    Write-Host "工具链安装完成" -ForegroundColor Green
} catch {
    Write-Host "自动安装失败，请手动执行以下步骤:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "1. 打开 'MSYS2 MinGW 64-bit' 终端 (不是MSYS2 MSYS)" -ForegroundColor Cyan
    Write-Host "2. 执行: pacman -Syuu" -ForegroundColor Cyan
    Write-Host "3. 如果提示关闭终端，重新打开后再次执行: pacman -Syuu" -ForegroundColor Cyan
    Write-Host "4. 执行: pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake mingw-w64-x86_64-gdb" -ForegroundColor Cyan
}
Write-Host ""

# 3. 配置环境变量
Write-Host "[3/4] 正在配置环境变量..." -ForegroundColor Cyan

# 获取当前PATH
$currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")

# 检查是否已添加
$mingw64Bin = "$msys2Path\mingw64\bin"
$usrBin = "$msys2Path\usr\bin"

if ($currentPath -notlike "*$mingw64Bin*") {
    $newPath = "$mingw64Bin;$usrBin;$currentPath"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "Machine")
    Write-Host "已添加环境变量: $mingw64Bin" -ForegroundColor Green
    Write-Host "已添加环境变量: $usrBin" -ForegroundColor Green
} else {
    Write-Host "环境变量已配置" -ForegroundColor Yellow
}
Write-Host ""

# 4. 验证安装
Write-Host "[4/4] 正在验证安装..." -ForegroundColor Cyan
$env:Path = "$mingw64Bin;$usrBin;[Environment]::GetEnvironmentVariable('Path', 'Machine')"

Start-Sleep -Seconds 2

$gccVersion = & gcc --version 2>$null
if ($gccVersion) {
    Write-Host "=== GCC 安装成功 ===" -ForegroundColor Green
    $gccVersion | Select-Object -First 1
} else {
    Write-Host "警告: GCC未找到" -ForegroundColor Yellow
    Write-Host "请重新打开PowerShell后重试验证" -ForegroundColor Gray
}

$cmakeVersion = & cmake --version 2>$null
if ($cmakeVersion) {
    Write-Host "=== CMake 安装成功 ===" -ForegroundColor Green
    $cmakeVersion | Select-Object -First 1
}

Write-Host ""
Write-Host "=== 配置完成 ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "请重新打开PowerShell/终端以使用新环境" -ForegroundColor Yellow
Write-Host ""
Write-Host "验证命令:" -ForegroundColor Cyan
Write-Host "  gcc --version"
Write-Host "  g++ --version"
Write-Host "  cmake --version"
Write-Host "  make --version"
Write-Host "  gdb --version"

# 刷新当前会话的环境变量
$env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine")

Read-Host "`n按回车键退出"

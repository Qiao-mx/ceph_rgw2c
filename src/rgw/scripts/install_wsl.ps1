# WSL2 + Ubuntu 22.04 安装脚本
# 以管理员身份运行此脚本

Write-Host "==============================================" -ForegroundColor Cyan
Write-Host "WSL2 Ubuntu 22.04 安装脚本" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan
Write-Host ""

# 检查是否以管理员身份运行
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "错误: 请以管理员身份运行此脚本!" -ForegroundColor Red
    Write-Host "右键点击 PowerShell -> 选择 '以管理员身份运行'" -ForegroundColor Yellow
    exit 1
}

Write-Host "[1/5] 启用 WSL 功能..." -ForegroundColor Green
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart

Write-Host "[2/5] 启用虚拟机平台..." -ForegroundColor Green
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart

Write-Host "[3/5] 下载 Ubuntu 22.04..." -ForegroundColor Green
$wslPath = "$env:TEMP\ubuntu2204.zip"
$wslInstaller = "$env:TEMP\ubuntu2204.exe"

# 使用 wsl --install 安装 Ubuntu
Write-Host "正在安装 Ubuntu 22.04 (这可能需要几分钟)..." -ForegroundColor Yellow
wsl --install -d Ubuntu-22.04

Write-Host "[4/5] 设置 WSL 默认版本为 2..." -ForegroundColor Green
wsl --set-default-version 2

Write-Host "[5/5] 配置 WSL 优化..." -ForegroundColor Green

# 创建 WSL 配置文件
$wslConfigPath = "$env:USERPROFILE\.wslconfig"
$wslConfig = @"
[wsl2]
memory=8GB
processors=4
localhostForwarding=true
nestedVirtualization=true
"@

# 检查WSL是否正在运行
Write-Host ""
Write-Host "检查 WSL 状态..." -ForegroundColor Green
Start-Sleep -Seconds 3
wsl --list --verbose

Write-Host ""
Write-Host "==============================================" -ForegroundColor Cyan
Write-Host "安装完成!" -ForegroundColor Cyan
Write-Host "==============================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "请执行以下步骤:" -ForegroundColor Yellow
Write-Host "1. 重启电脑 (重要!)" -ForegroundColor White
Write-Host "2. 首次启动 Ubuntu 时设置用户名和密码" -ForegroundColor White
Write-Host "3. 运行后续的依赖安装脚本" -ForegroundColor White
Write-Host ""
Write-Host "首次启动后，运行以下命令安装依赖:" -ForegroundColor Cyan
Write-Host '  curl -fsSL https://aka.ms/wslubuntu2204/setup-deps.sh | bash' -ForegroundColor White
Write-Host ""

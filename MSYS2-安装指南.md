# MSYS2 C/C++ 编译环境安装指南

## 状态

- MSYS2 核心: ✅ 已安装在 C:\msys64
- GCC/工具链: ❌ 需要手动安装
- 环境变量: ❌ 需要配置

## 手动安装步骤

### 步骤1: 打开MSYS2终端

找到并打开 **"MSYS2 MinGW 64-bit"** (不是 MSYS2 MSYS!)

### 步骤2: 更新包数据库

在终端中执行:
```
pacman -Syuu
```

- 如果提示关闭终端，关闭后重新打开
- 重新执行 `pacman -Syuu` 直到显示 "nothing to do"

### 步骤3: 安装编译工具链

执行:
```
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-cmake mingw-w64-x86_64-gdb
```

输入 `Y` 确认安装。

### 步骤4: 配置环境变量

1. 右键"此电脑" → 属性
2. 点击"高级系统设置"
3. 点击"环境变量"
4. 在"系统变量"中找到 `Path`，双击编辑
5. 新增以下两个路径:
   - `C:\msys64\mingw64\bin`
   - `C:\msys64\usr\bin`
6. 确认保存

### 步骤5: 验证安装

打开新的PowerShell终端，执行:
```powershell
gcc --version
g++ --version
cmake --version
make --version
```

## 自动配置脚本

如果你已完成上述步骤，可以运行项目中的自动配置脚本:
```
.\setup-msys2.ps1
```

右键以管理员身份运行 PowerShell，然后执行:
```powershell
Set-ExecutionPolicy Bypass -File .\setup-msys2.ps1
.\setup-msys2.ps1
```

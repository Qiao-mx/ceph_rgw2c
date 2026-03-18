# RGW C Common 内存检测指南

本文档介绍如何为 RGW C Common 库设置内存泄漏和内存错误检测环境。

## 概述

项目支持多种内存检测工具：

| 工具 | 检测类型 | 适用场景 |
|------|----------|----------|
| **AddressSanitizer (ASan)** | 内存错误：越界访问、释放后使用、双重释放 | 开发阶段快速检测 |
| **Valgrind (memcheck)** | 内存泄漏、未初始化访问、非法内存访问 | 全面内存分析 |
| **MemorySanitizer (MSan)** | 未初始化内存读取 | 检测隐式未初始化问题 |
| **UndefinedBehaviorSanitizer (UBSan)** | 未定义行为：整数溢出、空指针解引用 | 运行时错误检测 |

## 环境要求

### Linux (推荐)

```bash
# Ubuntu/Debian
sudo apt-get install -y build-essential cmake gcc g++ valgrind

# Fedora/RHEL
sudo dnf install -y gcc gcc-c++ cmake valgrind

# Arch Linux
sudo pacman -S base-devel cmake valgrind
```

### Windows (WSL)

```bash
# 在 WSL 中安装
sudo apt-get install -y build-essential cmake gcc g++ valgrind
```

### macOS

```bash
# 安装 Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install cmake gcc valgrind
```

注意：macOS 上 sanitizer 支持有限，建议使用 Linux 环境。

## 使用方法

### 方法 1: 使用脚本 (推荐)

#### Linux/macOS/WSL

```bash
cd /path/to/c_common

# 运行 AddressSanitizer 测试
./run_memory_tests.sh asan

# 运行 Valgrind 内存泄漏检测
./run_memory_tests.sh valgrind

# 运行所有检测
./run_memory_tests.sh all

# 仅构建测试
./run_memory_tests.sh build
```

#### Windows (PowerShell)

```powershell
cd C:\path\to\c_common

# 运行 AddressSanitizer 测试
.\run_memory_tests.bat asan

# 运行 Valgrind 内存泄漏检测
.\run_memory_tests.bat valgrind

# 运行所有检测
.\run_memory_tests.bat all
```

### 方法 2: 手动运行

#### AddressSanitizer

```bash
cd build

# 配置并编译 (启用 ASan)
cmake -DWITH_ASAN=ON ..
make -j4

# 运行测试
./test_memory
```

#### Valgrind

```bash
cd build

# 正常编译
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j4

# 运行 Valgrind
valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./test_memory
```

## CMake 选项说明

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `WITH_ASAN` | 启用 AddressSanitizer | OFF |
| `WITH_VALGRIND` | 启用 Valgrind 支持 | OFF |
| `WITH_MSAN` | 启用 MemorySanitizer | OFF |
| `WITH_UBSAN` | 启用 Undefined Behavior Sanitizer | OFF |

## 检测内容

### AddressSanitizer 检测

- 堆缓冲区溢出
- 栈缓冲区溢出
- 全局缓冲区溢出
- 释放后使用 (use-after-free)
- 双重释放 (double-free)
- 内存泄漏 (可选)

### Valgrind 检测

- 内存泄漏
- 未初始化内存使用
- 非法内存访问
- 使用未释放的内存
- 错误的参数传递给系统调用

### MemorySanitizer 检测

- 从未初始化内存读取
- 从未初始化堆内存读取

### UBSan 检测

- 整数溢出
- 空指针解引用
- 移位运算溢出
- 浮点异常

## 常见问题

### Q: 编译时报错 "undefined reference to '__asan_init'"

A: 确保使用支持 sanitizer 的 GCC 版本 (>= 4.8)，并且同时编译库和测试程序。

### Q: Valgrind 报告大量 "still reachable" 内存

A: 这是正常的，因为很多库会在程序结束时统一释放内存。可以使用 `--show-leak-kinds=definite` 来只报告明确的泄漏。

### Q: Windows 上无法运行

A: Windows 原生不支持这些工具。建议使用 WSL (Windows Subsystem for Linux) 或虚拟机。

## 自动化 CI 集成

可以在 CI 流程中添加内存检测：

```yaml
# .github/workflows/memory-test.yml 示例
name: Memory Tests

on: [push, pull_request]

jobs:
  asan:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build with ASan
        run: |
          cd src/rgw/c_common
          mkdir build && cd build
          cmake -DWITH_ASAN=ON ..
          make -j4
      - name: Run tests
        run: |
          cd src/rgw/c_common/build
          ctest --output-on-failure
```

## 相关文档

- [AddressSanitizer 官方文档](https://clang.llvm.org/docs/AddressSanitizer.html)
- [Valgrind 官方文档](https://valgrind.org/docs/)
- [MemorySanitizer 官方文档](https://clang.llvm.org/docs/MemorySanitizer.html)
- [UBSan 官方文档](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)

# 静态分析工具集成指南

## 概述

本指南介绍如何集成 cppcheck 和 clang-tidy 静态分析工具到 VisionFlowPlatform 项目中，以提高代码质量和安全性。

## 工具介绍

### cppcheck
- **用途**：静态代码分析，检测内存泄漏、空指针解引用、未初始化变量等
- **特点**：轻量级、快速、专注于 C/C++ 代码
- **官网**：http://cppcheck.net

### clang-tidy
- **用途**：基于 LLVM 的 linter 和静态分析工具
- **特点**：功能强大、支持现代 C++ 特性、可扩展
- **官网**：https://clang.llvm.org/extra/clang-tidy/

## 安装指南

### Windows 安装

#### 使用 Chocolatey
```powershell
# 安装 cppcheck
choco install cppcheck

# 安装 LLVM（包含 clang-tidy）
choco install llvm
```

#### 使用 Scoop
```powershell
# 安装 cppcheck
scoop install cppcheck

# 安装 LLVM
scoop install llvm
```

#### 手动安装
1. **cppcheck**：
   - 下载：https://github.com/danmar/cppcheck/releases
   - 安装到 `C:\Program Files\Cppcheck`

2. **LLVM/clang-tidy**：
   - 下载：https://github.com/llvm/llvm-project/releases
   - 安装到 `C:\Program Files\LLVM`

### Linux 安装

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install cppcheck clang-tidy

# CentOS/RHEL
sudo yum install cppcheck clang-tools-extra

# Fedora
sudo dnf install cppcheck clang-tools-extra
```

### macOS 安装

```bash
# 使用 Homebrew
brew install cppcheck llvm
```

## 配置指南

### 1. 项目配置文件

#### .clang-tidy 配置
在项目根目录创建 `.clang-tidy` 文件：

```yaml
---
Checks: >
  -*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers,
  -readability-identifier-length

WarningsAsErrors: ''
HeaderFilterRegex: 'include/.*\.h$'
AnalyzeTemporaryDtors: false
CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
  - key: readability-identifier-naming.VariableCase
    value: camelBack
  - key: readability-identifier-naming.MemberPrefix
    value: 'm_'
```

#### cppcheck 配置
创建 `cppcheck.cfg` 文件：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<project>
    <root>E:\halcon\2\xin1</root>
    <include>
        <dir>include</dir>
        <dir>src</dir>
    </include>
    <exclude>
        <dir>build</dir>
        <dir>thirdparty</dir>
        <dir>tests</dir>
    </exclude>
    <check>
        <enable>all</enable>
        <disable>
            <id>missingInclude</id>
            <id>unusedFunction</id>
        </disable>
    </check>
    <output>
        <format>xml</format>
        <file>cppcheck-results.xml</file>
    </output>
</project>
```

### 2. CMakeLists.txt 集成

在 CMakeLists.txt 中添加静态分析支持：

```cmake
# 静态分析工具选项
option(ENABLE_CPPCHECK "Enable cppcheck static analysis" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)

# cppcheck 集成
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK cppcheck)
    if(CPPCHECK)
        set(CMAKE_CXX_CPPCHECK
            ${CPPCHECK}
            --suppress=missingInclude
            --suppress=unusedFunction
            --enable=all
            --inconclusive
            --force
            --inline-suppr
        )
        message(STATUS "cppcheck enabled: ${CPPCHECK}")
    else()
        message(WARNING "cppcheck not found!")
    endif()
endif()

# clang-tidy 集成
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY clang-tidy)
    if(CLANG_TIDY)
        set(CMAKE_CXX_CLANG_TIDY
            ${CLANG_TIDY}
            -p=${CMAKE_BINARY_DIR}
            --header-filter=include/.*\.h$
        )
        message(STATUS "clang-tidy enabled: ${CLANG_TIDY}")
    else()
        message(WARNING "clang-tidy not found!")
    endif()
endif()
```

### 3. 构建命令

```bash
# 启用 cppcheck
cmake -B build -DENABLE_CPPCHECK=ON
cmake --build build

# 启用 clang-tidy
cmake -B build -DENABLE_CLANG_TIDY=ON
cmake --build build

# 同时启用两者
cmake -B build -DENABLE_CPPCHECK=ON -DENABLE_CLANG_TIDY=ON
cmake --build build
```

## 使用指南

### 1. 命令行使用

#### cppcheck 基本用法
```bash
# 分析整个项目
cppcheck --enable=all --suppress=missingInclude src/ include/

# 分析特定文件
cppcheck --enable=all src/MainWindow.cpp

# 生成 XML 报告
cppcheck --enable=all --xml --xml-version=2 src/ include/ 2> cppcheck-results.xml

# 使用配置文件
cppcheck --project=cppcheck.cfg
```

#### clang-tidy 基本用法
```bash
# 分析整个项目
clang-tidy -p build src/*.cpp

# 分析特定文件
clang-tidy -p build src/MainWindow.cpp

# 自动修复
clang-tidy -p build --fix src/MainWindow.cpp

# 列出所有检查
clang-tidy --list-checks
```

### 2. 集成到开发流程

#### Git 预提交钩子
创建 `.git/hooks/pre-commit`：

```bash
#!/bin/bash

# 获取暂存的文件
files=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|h)$')

if [ -z "$files" ]; then
    exit 0
fi

echo "Running static analysis..."

# 运行 cppcheck
cppcheck --enable=all --suppress=missingInclude --error-exitcode=1 $files

if [ $? -ne 0 ]; then
    echo "cppcheck found errors!"
    exit 1
fi

# 运行 clang-tidy
clang-tidy -p build $files

if [ $? -ne 0 ]; then
    echo "clang-tidy found errors!"
    exit 1
fi

echo "Static analysis passed!"
exit 0
```

#### CI/CD 集成
创建 `.github/workflows/static-analysis.yml`：

```yaml
name: Static Analysis

on: [push, pull_request]

jobs:
  cppcheck:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install cppcheck
        run: sudo apt-get install -y cppcheck
      - name: Run cppcheck
        run: cppcheck --enable=all --suppress=missingInclude src/ include/

  clang-tidy:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install clang-tidy
        run: sudo apt-get install -y clang-tidy
      - name: Generate compile_commands.json
        run: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      - name: Run clang-tidy
        run: clang-tidy -p build src/*.cpp
```

## 常见问题检查

### 1. 内存相关问题
- 内存泄漏
- 空指针解引用
- 未初始化变量
- 缓冲区溢出

### 2. 性能相关问题
- 不必要的拷贝
- 低效的算法
- 内存碎片
- 缓存不友好

### 3. 安全相关问题
- SQL 注入
- 缓冲区溢出
- 整数溢出
- 格式化字符串漏洞

### 4. 代码质量问题
- 重复代码
- 过长函数
- 复杂条件
- 魔法数字

## 集成示例

### 1. 完整的 CMakeLists.txt 配置

```cmake
cmake_minimum_required(VERSION 3.16)
project(VisionFlowPlatform)

# 导出 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 静态分析选项
option(ENABLE_CPPCHECK "Enable cppcheck" OFF)
option(ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)

# 静态分析工具配置
if(ENABLE_CPPCHECK)
    find_program(CPPCHECK cppcheck)
    if(CPPCHECK)
        set(CMAKE_CXX_CPPCHECK
            ${CPPCHECK}
            --suppress=missingInclude
            --suppress=unusedFunction
            --enable=all
            --inconclusive
            --force
            --inline-suppr
        )
    endif()
endif()

if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY clang-tidy)
    if(CLANG_TIDY)
        set(CMAKE_CXX_CLANG_TIDY
            ${CLANG_TIDY}
            -p=${CMAKE_BINARY_DIR}
            --header-filter=include/.*\.h$
        )
    endif()
endif()

# ... 其余配置
```

### 2. PowerShell 脚本

```powershell
# run-static-analysis.ps1

param(
    [string]$SourceDir = "src",
    [string]$IncludeDir = "include",
    [string]$BuildDir = "build"
)

Write-Host "Running static analysis..." -ForegroundColor Green

# 检查工具是否安装
$cppcheck = Get-Command cppcheck -ErrorAction SilentlyContinue
$clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue

if (-not $cppcheck) {
    Write-Host "cppcheck not found! Install it first." -ForegroundColor Red
    exit 1
}

if (-not $clangTidy) {
    Write-Host "clang-tidy not found! Install LLVM first." -ForegroundColor Red
    exit 1
}

# 运行 cppcheck
Write-Host "Running cppcheck..." -ForegroundColor Yellow
cppcheck --enable=all --suppress=missingInclude $SourceDir $IncludeDir

# 运行 clang-tidy
Write-Host "Running clang-tidy..." -ForegroundColor Yellow
clang-tidy -p $BuildDir $SourceDir/*.cpp

Write-Host "Static analysis complete!" -ForegroundColor Green
```

## 报告生成

### 1. HTML 报告

```bash
# 使用 cppcheck 生成 HTML 报告
cppcheck --enable=all --xml --xml-version=2 src/ include/ 2> cppcheck-results.xml
cppcheck-htmlreport --file=cppcheck-results.xml --report-dir=cppcheck-report --source-dir=.
```

### 2. 集成到 IDE

#### Visual Studio Code
安装扩展：
- C/C++ (Microsoft)
- C/C++ Static Analysis (cppcheck)
- Clang-Tidy

#### Qt Creator
1. 打开工具 → 选项 → 分析器
2. 配置 cppcheck 和 clang-tidy 路径
3. 启用静态分析

## 最佳实践

1. **定期运行**：在每次提交前运行静态分析
2. **渐进式修复**：先修复严重问题，再处理警告
3. **自定义规则**：根据项目需求调整检查规则
4. **集成到 CI**：在 CI/CD 流程中自动运行
5. **团队培训**：确保团队了解静态分析结果

## 相关资源

- [cppcheck 文档](http://cppcheck.net/manual.pdf)
- [clang-tidy 文档](https://clang.llvm.org/extra/clang-tidy/)
- [CMake 静态分析](https://cmake.org/cmake/help/latest/variable/CMAKE_CXX_CLANG_TIDY.html)
- [静态分析最佳实践](https://wiki.sei.cmu.edu/confluence/display/c/Static+Analysis)

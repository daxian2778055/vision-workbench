# mcp-cpp 安装与配置指南

## 概述

mcp-cpp 是一个 MCP 服务器，通过 clangd LSP 集成为 C++ 代码分析提供语义理解能力。它使 AI 代理能够像现代 IDE 一样理解 C++ 代码库。

## 系统要求

- **操作系统**: Windows (WSL2)、Linux、macOS
- **Rust**: 2024 版本或更高
- **clangd**: 11 或更高版本（推荐 20+）
- **构建系统**: CMake 或 Meson（需要生成 compile_commands.json）

## 安装步骤

### 方法 1：从 Crates.io 安装（推荐）

```bash
# 安装 Rust（如果尚未安装）
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# 安装 mcp-cpp-server
cargo install mcp-cpp-server

# 验证安装
mcp-cpp-server --help
```

### 方法 2：从源码安装

```bash
# 克隆仓库
git clone https://github.com/mpsm/mcp-cpp.git
cd mcp-cpp

# 安装
cargo install --path .

# 验证安装
mcp-cpp-server --help
```

### 方法 3：Docker 安装

```bash
# 构建 Docker 镜像
docker build -t mcp-cpp-server .

# 运行容器
docker run -i --rm -v /path/to/your/cpp-project:/workspace mcp-cpp-server
```

## Windows 特殊配置

### 安装 clangd

1. **使用 LLVM 安装**：
   - 下载 LLVM 安装程序：https://github.com/llvm/llvm-project/releases
   - 安装时确保包含 clangd

2. **使用 Chocolatey 安装**：
   ```powershell
   choco install llvm
   ```

3. **使用 Scoop 安装**：
   ```powershell
   scoop install llvm
   ```

### 设置环境变量

```powershell
# 设置 clangd 路径
$env:CLANGD_PATH = "C:\Program Files\LLVM\bin\clangd.exe"

# 或者添加到系统 PATH
[Environment]::SetEnvironmentVariable("PATH", $env:PATH + ";C:\Program Files\LLVM\bin", "Machine")
```

## 项目配置

### 1. 生成 compile_commands.json

#### CMake 项目

```bash
# 在 CMakeLists.txt 中添加
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 或者在命令行中指定
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

#### Meson 项目

```bash
# Meson 默认生成 compile_commands.json
meson setup build
```

### 2. 验证 compile_commands.json

```bash
# 检查文件是否存在
ls -la build/compile_commands.json

# 查看文件内容
head -n 20 build/compile_commands.json
```

## Cursor 配置

### 方法 1：通过 Cursor 设置

1. 打开 Cursor 设置（Ctrl+,）
2. 搜索 "MCP"
3. 在 MCP 服务器配置中添加：

```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "mcp-cpp-server",
      "args": ["--root", "E:\\halcon\\2\\xin1"],
      "env": {
        "CLANGD_PATH": "C:\\Program Files\\LLVM\\bin\\clangd.exe"
      }
    }
  }
}
```

### 方法 2：通过配置文件

在项目根目录创建 `.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "mcp-cpp-server",
      "args": ["--root", "E:\\halcon\\2\\xin1"],
      "env": {
        "CLANGD_PATH": "C:\\Program Files\\LLVM\\bin\\clangd.exe"
      }
    }
  }
}
```

### 方法 3：全局配置

在用户目录创建 `~/.cursor/mcp.json`：

```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "mcp-cpp-server",
      "env": {
        "CLANGD_PATH": "C:\\Program Files\\LLVM\\bin\\clangd.exe"
      }
    }
  }
}
```

## 使用方法

### 基本工作流

1. **获取项目详情**：
   ```json
   { "name": "get_project_details" }
   ```

2. **搜索符号**：
   ```json
   {
     "name": "search_symbols",
     "arguments": { "query": "MyClass" }
   }
   ```

3. **分析符号上下文**：
   ```json
   {
     "name": "analyze_symbol_context",
     "arguments": { "symbol": "MyClass::process" }
   }
   ```

### 高级用法

#### 指定构建目录

```json
{
  "name": "search_symbols",
  "arguments": {
    "query": "MyClass",
    "build_directory": "build-debug"
  }
}
```

#### 搜索特定文件

```json
{
  "name": "search_symbols",
  "arguments": {
    "query": "",
    "files": ["include/MyClass.h"]
  }
}
```

#### 分析继承层次

```json
{
  "name": "analyze_symbol_context",
  "arguments": {
    "symbol": "MyClass",
    "max_examples": 5
  }
}
```

## 故障排除

### 问题 1：clangd 找不到

**症状**：服务器启动失败，提示找不到 clangd

**解决方案**：
```powershell
# 检查 clangd 是否安装
clangd --version

# 设置环境变量
$env:CLANGD_PATH = "C:\Program Files\LLVM\bin\clangd.exe"

# 或者在配置中指定完整路径
```

### 问题 2：compile_commands.json 不存在

**症状**：服务器无法找到编译数据库

**解决方案**：
```bash
# 确保 CMakeLists.txt 中包含
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 重新生成
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

### 问题 3：符号搜索无结果

**症状**：搜索符号返回空结果

**解决方案**：
1. 检查 clangd 索引状态
2. 确保 compile_commands.json 包含正确的源文件路径
3. 等待索引完成（首次启动可能需要时间）

### 问题 4：性能问题

**症状**：符号搜索缓慢

**解决方案**：
1. 增加超时时间：
   ```json
   {
     "name": "search_symbols",
     "arguments": {
       "query": "MyClass",
       "wait_timeout": 30
     }
   }
   ```

2. 限制搜索范围：
   ```json
   {
     "name": "search_symbols",
     "arguments": {
       "query": "MyClass",
       "files": ["include/MyClass.h"]
     }
   }
   ```

## 环境变量参考

| 变量名 | 默认值 | 说明 |
|--------|--------|------|
| `CLANGD_PATH` | `clangd` | clangd 可执行文件路径 |
| `RUST_LOG` | `info` | 日志级别 (trace, debug, info, warn, error) |
| `MCP_LOG_FILE` | stderr | 日志文件路径 |
| `MCP_LOG_UNIQUE` | `false` | 是否在日志文件名中添加进程 ID |

## 命令行选项

```bash
mcp-cpp-server [OPTIONS]

选项：
  --root <DIR>             项目根目录（默认为当前目录）
  --clangd-path <PATH>     clangd 可执行文件路径
  --log-level <LEVEL>      日志级别
  --log-file <FILE>        日志文件路径
  --help                   显示帮助信息
  --version                显示版本信息
```

## 集成示例

### 完整的 Cursor 配置示例

```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "mcp-cpp-server",
      "args": [
        "--root", "E:\\halcon\\2\\xin1",
        "--log-level", "info"
      ],
      "env": {
        "CLANGD_PATH": "C:\\Program Files\\LLVM\\bin\\clangd.exe",
        "RUST_LOG": "info"
      }
    }
  }
}
```

### 使用 Docker 的配置示例

```json
{
  "mcpServers": {
    "cpp-tools": {
      "command": "docker",
      "args": [
        "run", "-i", "--rm",
        "-v", "E:\\halcon\\2\\xin1:/workspace",
        "mcp-cpp-server",
        "--root", "/workspace"
      ]
    }
  }
}
```

## 最佳实践

1. **保持 clangd 更新**：使用最新版本的 clangd 以获得最佳性能
2. **定期更新索引**：在代码更改后重新生成 compile_commands.json
3. **使用适当的超时**：根据项目大小调整 wait_timeout 参数
4. **限制搜索范围**：在可能的情况下指定 files 参数以提高性能
5. **监控日志**：启用日志记录以诊断问题

## 相关资源

- [mcp-cpp GitHub 仓库](https://github.com/mpsm/mcp-cpp)
- [Model Context Protocol 规范](https://modelcontextprotocol.io)
- [clangd 文档](https://clangd.llvm.org)
- [Cursor MCP 文档](https://cursor.sh/docs/mcp)

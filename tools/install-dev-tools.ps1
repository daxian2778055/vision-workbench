# VisionFlowPlatform 开发工具安装脚本
# 使用方法：以管理员身份运行 PowerShell，执行此脚本

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VisionFlowPlatform 开发工具安装脚本" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查管理员权限
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "请以管理员身份运行此脚本！" -ForegroundColor Red
    exit 1
}

# 设置错误处理
$ErrorActionPreference = "Stop"

# 安装函数
function Install-Tool {
    param(
        [string]$Name,
        [scriptblock]$InstallCommand,
        [scriptblock]$VerifyCommand
    )
    
    Write-Host "`n安装 $Name..." -ForegroundColor Yellow
    
    try {
        # 检查是否已安装
        $result = & $VerifyCommand
        if ($result) {
            Write-Host "✓ $Name 已安装" -ForegroundColor Green
            return $true
        }
    } catch {
        # 未安装，继续安装
    }
    
    try {
        # 执行安装命令
        & $InstallCommand
        Write-Host "✓ $Name 安装成功" -ForegroundColor Green
        return $true
    } catch {
        Write-Host "✗ $Name 安装失败: $_" -ForegroundColor Red
        return $false
    }
}

# 1. 检查并安装 Chocolatey
Write-Host "`n[1/7] 检查 Chocolatey..." -ForegroundColor Cyan
try {
    choco --version | Out-Null
    Write-Host "✓ Chocolatey 已安装" -ForegroundColor Green
} catch {
    Write-Host "安装 Chocolatey..." -ForegroundColor Yellow
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
}

# 2. 安装 Rust
Write-Host "`n[2/7] 检查 Rust..." -ForegroundColor Cyan
$rustInstalled = Install-Tool -Name "Rust" -InstallCommand {
    choco install rust -y
} -VerifyCommand {
    rustc --version 2>$null
}

# 3. 安装 LLVM (包含 clang-tidy)
Write-Host "`n[3/7] 检查 LLVM/clang-tidy..." -ForegroundColor Cyan
$clangTidyInstalled = Install-Tool -Name "LLVM/clang-tidy" -InstallCommand {
    choco install llvm -y
} -VerifyCommand {
    clang-tidy --version 2>$null
}

# 4. 安装 cppcheck
Write-Host "`n[4/7] 检查 cppcheck..." -ForegroundColor Cyan
$cppcheckInstalled = Install-Tool -Name "cppcheck" -InstallCommand {
    choco install cppcheck -y
} -VerifyCommand {
    cppcheck --version 2>$null
}

# 5. 安装 mcp-cpp-server
Write-Host "`n[5/7] 检查 mcp-cpp-server..." -ForegroundColor Cyan
$mcpCppInstalled = Install-Tool -Name "mcp-cpp-server" -InstallCommand {
    if ($rustInstalled) {
        cargo install mcp-cpp-server
    } else {
        Write-Host "需要先安装 Rust" -ForegroundColor Red
        return $false
    }
} -VerifyCommand {
    mcp-cpp-server --version 2>$null
}

# 6. 验证 compile_commands.json
Write-Host "`n[6/7] 检查 compile_commands.json..." -ForegroundColor Cyan
$compileCommandsPath = "E:\halcon\2\xin1\build\Release\.qtc_clangd\compile_commands.json"
if (Test-Path $compileCommandsPath) {
    Write-Host "✓ compile_commands.json 存在" -ForegroundColor Green
} else {
    Write-Host "⚠ compile_commands.json 不存在，需要重新生成" -ForegroundColor Yellow
    Write-Host "  请运行: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" -ForegroundColor Yellow
}

# 7. 验证配置文件
Write-Host "`n[7/7] 检查配置文件..." -ForegroundColor Cyan
$mcpConfigPath = "E:\halcon\2\xin1\.cursor\mcp.json"
if (Test-Path $mcpConfigPath) {
    Write-Host "✓ MCP 配置文件存在" -ForegroundColor Green
} else {
    Write-Host "⚠ MCP 配置文件不存在" -ForegroundColor Yellow
}

# 显示安装结果
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "安装结果总结" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$results = @(
    @{Name="Rust"; Status=$rustInstalled},
    @{Name="LLVM/clang-tidy"; Status=$clangTidyInstalled},
    @{Name="cppcheck"; Status=$cppcheckInstalled},
    @{Name="mcp-cpp-server"; Status=$mcpCppInstalled}
)

foreach ($result in $results) {
    if ($result.Status) {
        Write-Host "✓ $($result.Name)" -ForegroundColor Green
    } else {
        Write-Host "✗ $($result.Name)" -ForegroundColor Red
    }
}

# 显示环境变量
Write-Host "`n环境变量设置:" -ForegroundColor Cyan
Write-Host "CLANGD_PATH=C:\Program Files\LLVM\bin\clangd.exe" -ForegroundColor Yellow
Write-Host "RUST_LOG=info" -ForegroundColor Yellow

# 显示下一步操作
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "下一步操作" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

Write-Host @"

1. 重新启动终端以加载新的环境变量

2. 验证安装:
   rustc --version
   clang-tidy --version
   cppcheck --version
   mcp-cpp-server --version

3. 重新生成 compile_commands.json (如果需要):
   cd E:\halcon\2\xin1
   cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build

4. 测试 mcp-cpp:
   mcp-cpp-server --root E:\halcon\2\xin1

5. 在 Cursor 中使用:
   - 打开 Cursor
   - 检查 MCP 服务器状态
   - 使用 C++ 代码分析工具

"@ -ForegroundColor Yellow

Write-Host "`n安装完成！" -ForegroundColor Green

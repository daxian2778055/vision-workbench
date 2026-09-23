# VisionFlowPlatform 配置验证脚本
# 使用方法：在项目根目录运行此脚本

param(
    [switch]$Verbose,
    [switch]$Fix
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VisionFlowPlatform 配置验证" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查结果
$checks = @()
$passed = 0
$failed = 0
$warnings = 0

# 辅助函数
function Test-Command {
    param([string]$Command)
    try {
        Invoke-Expression "$Command --version 2>&1" | Out-Null
        return $true
    } catch {
        return $false
    }
}

function Add-Check {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Message,
        [string]$Fix = ""
    )
    
    $script:checks += @{
        Name = $Name
        Status = $Status
        Message = $Message
        Fix = $Fix
    }
    
    switch ($Status) {
        "PASS" { $script:passed++ }
        "FAIL" { $script:failed++ }
        "WARN" { $script:warnings++ }
    }
}

# 1. 检查操作系统
Write-Host "[1/15] 检查操作系统..." -ForegroundColor Yellow
$os = Get-WmiObject -Class Win32_OperatingSystem
if ($os.Version -ge "10.0") {
    Add-Check "操作系统" "PASS" "Windows $($os.Version)"
} else {
    Add-Check "操作系统" "FAIL" "需要 Windows 10 或更高版本" "升级到 Windows 10 或更高版本"
}

# 2. 检查 Visual Studio
Write-Host "[2/15] 检查 Visual Studio..." -ForegroundColor Yellow
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    $vsVersion = & $vsWhere -latest -property catalog_productLineVersion
    Add-Check "Visual Studio" "PASS" "VS $vsVersion at $vsPath"
} else {
    Add-Check "Visual Studio" "FAIL" "未找到 Visual Studio" "安装 Visual Studio 2019 或更高版本"
}

# 3. 检查 Qt
Write-Host "[3/15] 检查 Qt..." -ForegroundColor Yellow
$qtPaths = @(
    "D:\Qt\6.11.0\msvc2022_64",          # 本仓实际使用的版本（与 CMakeLists/部署脚本一致）
    "C:\Qt\6.11.0\msvc2022_64",
    "C:\Qt\6.10.0\msvc2019_64",
    "C:\Qt\6.9.0\msvc2019_64",
    "C:\Qt\6.8.0\msvc2019_64",
    "$env:USERPROFILE\Qt\6.10.0\msvc2019_64"
)

$qtFound = $false
foreach ($qtPath in $qtPaths) {
    if (Test-Path $qtPath) {
        Add-Check "Qt" "PASS" "Qt 6.x at $qtPath"
        $qtFound = $true
        break
    }
}

# 兜底：按“任意盘符 / 任意 6.x / 任意工具链”扫一遍。
# 为什么必须兜底：上面是硬编码路径清单，Qt 一升级（本机是 D:\Qt\6.11.0\msvc2022_64）就会
# 误报"未找到 Qt 6.x"——一个会说谎的检查脚本比没有检查更糟。
if (-not $qtFound) {
    foreach ($drive in @('C:\', 'D:\', 'E:\')) {
        $qtRoot = Join-Path $drive 'Qt'
        if (-not (Test-Path $qtRoot)) { continue }
        $hit = Get-ChildItem $qtRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '6.*' } |
            ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
            Where-Object { Test-Path (Join-Path $_.FullName 'bin\qmake.exe') } |
            Select-Object -First 1
        if ($hit) {
            Add-Check "Qt" "PASS" ("Qt {0} at {1}" -f $hit.Parent.Name, $hit.FullName)
            $qtFound = $true
            break
        }
    }
}

if (-not $qtFound) {
    Add-Check "Qt" "FAIL" "未找到 Qt 6.x" "安装 Qt 6.x 或设置 CMAKE_PREFIX_PATH"
}

# 4. 检查 HALCON
Write-Host "[4/15] 检查 HALCON..." -ForegroundColor Yellow
$halconRoot = $env:HALCON_ROOT
if (-not $halconRoot) {
    $halconRoot = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
}

if (Test-Path $halconRoot) {
    $halconDll = Join-Path $halconRoot "bin\x64-win64\halcon.dll"
    if (Test-Path $halconDll) {
        Add-Check "HALCON" "PASS" "HALCON at $halconRoot"
    } else {
        Add-Check "HALCON" "WARN" "HALCON 目录存在但可能不完整" "重新安装 HALCON"
    }
} else {
    Add-Check "HALCON" "FAIL" "未找到 HALCON" "安装 HALCON 24.11 并设置 HALCON_ROOT 环境变量"
}

# 5. 检查 OpenCV
Write-Host "[5/15] 检查 OpenCV..." -ForegroundColor Yellow
$opencvDir = Join-Path $PSScriptRoot "..\thirdparty\opencv\build"
if (Test-Path $opencvDir) {
    $opencvLib = Join-Path $opencvDir "x64\vc16\lib\opencv_world4130.lib"
    if (Test-Path $opencvLib) {
        Add-Check "OpenCV" "PASS" "OpenCV 4.13.0 at $opencvDir"
    } else {
        Add-Check "OpenCV" "WARN" "OpenCV 目录存在但库文件可能不完整" "重新构建 OpenCV"
    }
} else {
    Add-Check "OpenCV" "FAIL" "未找到 OpenCV" "下载并构建 OpenCV 4.13.0"
}

# 6. 检查 MVS SDK
Write-Host "[6/15] 检查 MVS SDK..." -ForegroundColor Yellow
$mvsRoot = "D:\MVS"
if (Test-Path $mvsRoot) {
    $mvsDll = Join-Path $mvsRoot "Development\Libraries\win64\MvCameraControl.dll"
    if (Test-Path $mvsDll) {
        Add-Check "MVS SDK" "PASS" "MVS SDK at $mvsRoot"
    } else {
        Add-Check "MVS SDK" "WARN" "MVS 目录存在但可能不完整" "重新安装 MVS SDK"
    }
} else {
    Add-Check "MVS SDK" "WARN" "未找到 MVS SDK" "安装海康 MVS SDK（如果需要相机功能）"
}

# 7. 检查 CMake
Write-Host "[7/15] 检查 CMake..." -ForegroundColor Yellow
if (Test-Command "cmake") {
    # 注意：cmake --version 是多行输出，直接 -replace 会得到数组，[int] 转换会抛
    # ConvertToFinalInvalidCastException（本脚本此前每跑必抛）。故先取第一行再正则取主版本号。
    $cmakeMajor = 0
    $cmakeFirstLine = (cmake --version | Select-Object -First 1)
    if ("$cmakeFirstLine" -match 'version\s+(\d+)') { $cmakeMajor = [int]$Matches[1] }
    if ($cmakeMajor -ge 3) {
        Add-Check "CMake" "PASS" "CMake $(cmake --version | Select-String -Pattern '\d+\.\d+\.\d+')"
    } else {
        Add-Check "CMake" "FAIL" "CMake 版本过低" "安装 CMake 3.16 或更高版本"
    }
} else {
    Add-Check "CMake" "FAIL" "未找到 CMake" "安装 CMake 3.16 或更高版本"
}

# 8. 检查 Git
Write-Host "[8/15] 检查 Git..." -ForegroundColor Yellow
if (Test-Command "git") {
    Add-Check "Git" "PASS" "Git $(git --version)"
} else {
    Add-Check "Git" "FAIL" "未找到 Git" "安装 Git"
}

# 9. 检查 Rust (用于 mcp-cpp)
Write-Host "[9/15] 检查 Rust..." -ForegroundColor Yellow
if (Test-Command "rustc") {
    Add-Check "Rust" "PASS" "Rust $(rustc --version)"
} else {
    Add-Check "Rust" "WARN" "未找到 Rust（可选，用于 mcp-cpp）" "安装 Rust: https://www.rust-lang.org/tools/install"
}

# 10. 检查 clang-tidy
Write-Host "[10/15] 检查 clang-tidy..." -ForegroundColor Yellow
if (Test-Command "clang-tidy") {
    Add-Check "clang-tidy" "PASS" "clang-tidy $(clang-tidy --version | Select-String -Pattern 'version')"
} else {
    Add-Check "clang-tidy" "WARN" "未找到 clang-tidy（可选，用于静态分析）" "安装 LLVM: choco install llvm"
}

# 11. 检查 cppcheck
Write-Host "[11/15] 检查 cppcheck..." -ForegroundColor Yellow
if (Test-Command "cppcheck") {
    Add-Check "cppcheck" "PASS" "cppcheck $(cppcheck --version)"
} else {
    Add-Check "cppcheck" "WARN" "未找到 cppcheck（可选，用于静态分析）" "安装 cppcheck: choco install cppcheck"
}

# 12. 检查 compile_commands.json
Write-Host "[12/15] 检查 compile_commands.json..." -ForegroundColor Yellow
$compileCommandsPath = Join-Path $PSScriptRoot "..\build\Release\.qtc_clangd\compile_commands.json"
if (Test-Path $compileCommandsPath) {
    Add-Check "compile_commands.json" "PASS" "存在于 build\Release\.qtc_clangd"
} else {
    $altPath = Join-Path $PSScriptRoot "..\build\compile_commands.json"
    if (Test-Path $altPath) {
        Add-Check "compile_commands.json" "PASS" "存在于 build"
    } else {
        Add-Check "compile_commands.json" "WARN" "未找到" "运行: cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    }
}

# 13. 检查 MCP 配置
Write-Host "[13/15] 检查 MCP 配置..." -ForegroundColor Yellow
$mcpConfigPath = Join-Path $PSScriptRoot "..\.cursor\mcp.json"
if (Test-Path $mcpConfigPath) {
    try {
        $mcpConfig = Get-Content $mcpConfigPath -Raw | ConvertFrom-Json
        if ($mcpConfig.mcpServers.'cpp-tools') {
            Add-Check "MCP 配置" "PASS" "已配置 mcp-cpp 服务器"
        } else {
            Add-Check "MCP 配置" "WARN" "MCP 配置文件存在但未配置 cpp-tools" "检查 .cursor/mcp.json 配置"
        }
    } catch {
        Add-Check "MCP 配置" "FAIL" "MCP 配置文件格式错误" "修复 .cursor/mcp.json 格式"
    }
} else {
    Add-Check "MCP 配置" "WARN" "未找到 MCP 配置文件" "创建 .cursor/mcp.json"
}

# 14. 检查项目规则
Write-Host "[14/15] 检查项目规则..." -ForegroundColor Yellow
$rulesPath = Join-Path $PSScriptRoot "..\.cursor\rules"
if (Test-Path $rulesPath) {
    $ruleFiles = Get-ChildItem $rulesPath -Filter "*.mdc"
    if ($ruleFiles.Count -ge 5) {
        Add-Check "项目规则" "PASS" "$($ruleFiles.Count) 个规则文件"
    } else {
        Add-Check "项目规则" "WARN" "规则文件不完整" "检查 .cursor\rules 目录"
    }
} else {
    Add-Check "项目规则" "WARN" "未找到项目规则目录" "创建 .cursor\rules 目录"
}

# 15. 检查文档
Write-Host "[15/15] 检查文档..." -ForegroundColor Yellow
$docsPath = Join-Path $PSScriptRoot "..\docs"
if (Test-Path $docsPath) {
    $docFiles = Get-ChildItem $docsPath -Filter "*.md"
    if ($docFiles.Count -ge 5) {
        Add-Check "文档" "PASS" "$($docFiles.Count) 个文档文件"
    } else {
        Add-Check "文档" "WARN" "文档不完整" "检查 docs 目录"
    }
} else {
    Add-Check "文档" "WARN" "未找到文档目录" "创建 docs 目录"
}

# 显示结果
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "检查结果" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

foreach ($check in $checks) {
    $icon = switch ($check.Status) {
        "PASS" { "✓"; break }
        "FAIL" { "✗"; break }
        "WARN" { "⚠"; break }
    }
    
    $color = switch ($check.Status) {
        "PASS" { "Green"; break }
        "FAIL" { "Red"; break }
        "WARN" { "Yellow"; break }
    }
    
    Write-Host "$icon $($check.Name): $($check.Message)" -ForegroundColor $color
    
    if ($check.Fix -and ($check.Status -ne "PASS")) {
        Write-Host "  修复: $($check.Fix)" -ForegroundColor Gray
    }
}

# 统计
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "统计" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "通过: $passed" -ForegroundColor Green
Write-Host "失败: $failed" -ForegroundColor Red
Write-Host "警告: $warnings" -ForegroundColor Yellow

# 总结
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "总结" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if ($failed -eq 0) {
    Write-Host "✓ 所有必需检查通过！" -ForegroundColor Green
    
    if ($warnings -gt 0) {
        Write-Host "⚠ 有 $warnings 个警告，建议修复以获得更好的开发体验" -ForegroundColor Yellow
    }
    
    Write-Host "`n下一步：" -ForegroundColor Cyan
    Write-Host "1. 运行 'cmake -B build -DCMAKE_BUILD_TYPE=Release' 生成构建文件" -ForegroundColor White
    Write-Host "2. 运行 'cmake --build build --config Release' 编译项目" -ForegroundColor White
    Write-Host "3. 运行 '.\build\bin\VisionFlowPlatform.exe' 启动程序" -ForegroundColor White
} else {
    Write-Host "✗ 有 $failed 个必需检查失败" -ForegroundColor Red
    Write-Host "请修复这些问题后重新运行验证脚本" -ForegroundColor Yellow
    
    # 显示修复建议
    Write-Host "`n修复建议：" -ForegroundColor Cyan
    foreach ($check in $checks) {
        if ($check.Status -eq "FAIL" -and $check.Fix) {
            Write-Host "- $($check.Name): $($check.Fix)" -ForegroundColor White
        }
    }
}

# 保存报告（写到 logs\ 下：那是运行期产物目录，已随 .gitignore 忽略，不污染仓库根）
$logDir = Join-Path $PSScriptRoot "..\logs"
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir -Force | Out-Null }
$reportPath = Join-Path $logDir "config-check-report.txt"
$report = @"
VisionFlowPlatform 配置验证报告
生成时间: $(Get-Date)

检查结果:
$($checks | ForEach-Object { "$($_.Status) $($_.Name): $($_.Message)" } | Out-String)

统计:
- 通过: $passed
- 失败: $failed
- 警告: $warnings
"@

$report | Out-File -FilePath $reportPath -Encoding UTF8
Write-Host "`n报告已保存到: $reportPath" -ForegroundColor Gray

# 返回状态
if ($failed -eq 0) {
    exit 0
} else {
    exit 1
}

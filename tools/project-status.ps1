# VisionFlowPlatform 项目状态检查脚本
# 快速检查项目配置和健康状态

param(
    [switch]$Quick,
    [switch]$Full
)

# 设置错误处理
$ErrorActionPreference = "Continue"

# 颜色定义
$Green = "Green"
$Yellow = "Yellow"
$Red = "Red"
$Cyan = "Cyan"
$White = "White"

# 计数器
$passCount = 0
$warnCount = 0
$failCount = 0

function Write-Status {
    param(
        [string]$Name,
        [string]$Status,
        [string]$Message,
        [string]$Icon = ""
    )
    
    $color = switch ($Status) {
        "PASS" { $Green; $script:passCount++ }
        "WARN" { $Yellow; $script:warnCount++ }
        "FAIL" { $Red; $script:failCount++ }
        default { $White }
    }
    
    $icon = if ($Icon) { $Icon } else {
        switch ($Status) {
            "PASS" { "✓" }
            "WARN" { "⚠" }
            "FAIL" { "✗" }
            default { "•" }
        }
    }
    
    # ${Name}: must use braces - "$Name:" is parsed as a drive-qualified variable and fails to parse
    Write-Host "$icon ${Name}: $Message" -ForegroundColor $color
}

Write-Host "========================================" -ForegroundColor $Cyan
Write-Host "VisionFlowPlatform 项目状态检查" -ForegroundColor $Cyan
Write-Host "========================================" -ForegroundColor $Cyan
Write-Host ""

# 1. 检查项目结构
Write-Host "[项目结构]" -ForegroundColor $Cyan

$requiredDirs = @("src", "include", "ui", "docs", "tools")
foreach ($dir in $requiredDirs) {
    if (Test-Path $dir) {
        Write-Status $dir "PASS" "存在"
    } else {
        Write-Status $dir "WARN" "不存在"
    }
}

# 2. 检查核心文件
Write-Host "`n[核心文件]" -ForegroundColor $Cyan

$requiredFiles = @(
    "CMakeLists.txt",
    "README.md",
    ".gitignore"
)

foreach ($file in $requiredFiles) {
    if (Test-Path $file) {
        Write-Status $file "PASS" "存在"
    } else {
        Write-Status $file "WARN" "不存在"
    }
}

# 3. 检查 Cursor 配置
Write-Host "`n[Cursor 配置]" -ForegroundColor $Cyan

if (Test-Path ".cursor") {
    Write-Status ".cursor 目录" "PASS" "存在"
    
    # 检查规则文件
    $ruleFiles = Get-ChildItem ".cursor/rules" -Filter "*.mdc" -ErrorAction SilentlyContinue
    if ($ruleFiles.Count -ge 5) {
        Write-Status "规则文件" "PASS" "$($ruleFiles.Count) 个规则文件"
    } else {
        Write-Status "规则文件" "WARN" "规则文件不完整 ($($ruleFiles.Count) 个)"
    }
    
    # 检查 SKILLS
    if (Test-Path ".cursor/skills") {
        $skillDirs = Get-ChildItem ".cursor/skills" -Directory -ErrorAction SilentlyContinue
        Write-Status "SKILLS" "PASS" "$($skillDirs.Count) 个 SKILLS"
    } else {
        Write-Status "SKILLS" "WARN" "SKILLS 目录不存在"
    }
    
    # 检查 MCP 配置
    if (Test-Path ".cursor/mcp.json") {
        Write-Status "MCP 配置" "PASS" "存在"
    } else {
        Write-Status "MCP 配置" "WARN" "不存在"
    }
} else {
    Write-Status ".cursor 目录" "FAIL" "不存在"
}

# 4. 检查文档
Write-Host "`n[文档完整性]" -ForegroundColor $Cyan

$docsPath = "docs"
if (Test-Path $docsPath) {
    $docFiles = Get-ChildItem $docsPath -Filter "*.md" -ErrorAction SilentlyContinue
    Write-Status "文档目录" "PASS" "$($docFiles.Count) 个文档"
    
    # 检查关键文档
    $keyDocs = @(
        "quick-start-guide.md",
        "api-reference.md",
        "troubleshooting-guide.md",
        "development-best-practices.md"
    )
    
    foreach ($doc in $keyDocs) {
        if (Test-Path "$docsPath/$doc") {
            Write-Status $doc "PASS" "存在"
        } else {
            Write-Status $doc "WARN" "不存在"
        }
    }
} else {
    Write-Status "文档目录" "FAIL" "不存在"
}

# 5. 检查工具脚本
Write-Host "`n[工具脚本]" -ForegroundColor $Cyan

$toolsPath = "tools"
if (Test-Path $toolsPath) {
    $toolScripts = @(
        "install-dev-tools.ps1",
        "verify-config.ps1",
        "health-check.ps1"
    )
    
    foreach ($script in $toolScripts) {
        if (Test-Path "$toolsPath/$script") {
            Write-Status $script "PASS" "存在"
        } else {
            Write-Status $script "WARN" "不存在"
        }
    }
} else {
    Write-Status "工具目录" "FAIL" "不存在"
}

# 6. 检查 CI/CD 配置
Write-Host "`n[CI/CD 配置]" -ForegroundColor $Cyan

if (Test-Path ".github/workflows") {
    $workflows = Get-ChildItem ".github/workflows" -Filter "*.yml" -ErrorAction SilentlyContinue
    if ($workflows.Count -gt 0) {
        Write-Status "GitHub Actions" "PASS" "$($workflows.Count) 个工作流"
    } else {
        Write-Status "GitHub Actions" "WARN" "未找到工作流"
    }
} else {
    Write-Status "GitHub Actions" "WARN" "未配置"
}

# 7. 检查依赖库
Write-Host "`n[依赖库]" -ForegroundColor $Cyan

$thirdpartyPath = "thirdparty"
if (Test-Path $thirdpartyPath) {
    $libs = @(
        @{Name="OpenCV"; Path="opencv/build"},
        @{Name="ZXing"; Path="zxing"},
        @{Name="Tesseract"; Path="tesseract"}
    )
    
    foreach ($lib in $libs) {
        if (Test-Path "$thirdpartyPath/$($lib.Path)") {
            Write-Status $lib.Name "PASS" "存在"
        } else {
            Write-Status $lib.Name "WARN" "不存在"
        }
    }
} else {
    Write-Status "第三方库" "WARN" "目录不存在"
}

# 8. 检查构建状态
Write-Host "`n[构建状态]" -ForegroundColor $Cyan

if (Test-Path "build") {
    $buildFiles = Get-ChildItem "build" -Filter "*.sln" -Recurse -ErrorAction SilentlyContinue
    if ($buildFiles.Count -gt 0) {
        Write-Status "构建文件" "PASS" "已生成"
    } else {
        Write-Status "构建文件" "WARN" "未生成"
    }
} else {
    Write-Status "构建目录" "WARN" "不存在"
}

# 9. 检查版本控制
Write-Host "`n[版本控制]" -ForegroundColor $Cyan

if (Test-Path ".git") {
    Write-Status "Git 仓库" "PASS" "已初始化"
    
    # 检查未提交更改
    $gitStatus = git status --porcelain 2>$null
    if ($gitStatus) {
        $changeCount = ($gitStatus | Measure-Object).Count
        Write-Status "未提交更改" "WARN" "$changeCount 个文件"
    } else {
        Write-Status "工作区" "PASS" "干净"
    }
} else {
    Write-Status "Git 仓库" "WARN" "未初始化"
}

# 10. 检查代码统计
if ($Full) {
    Write-Host "`n[代码统计]" -ForegroundColor $Cyan
    
    # 统计源文件
    $srcFiles = Get-ChildItem -Path "src" -Filter "*.cpp" -Recurse -ErrorAction SilentlyContinue
    $headerFiles = Get-ChildItem -Path "include" -Filter "*.h" -Recurse -ErrorAction SilentlyContinue
    
    $totalLines = 0
    foreach ($file in $srcFiles) {
        $totalLines += (Get-Content $file.FullName).Count
    }
    foreach ($file in $headerFiles) {
        $totalLines += (Get-Content $file.FullName).Count
    }
    
    Write-Status "源文件" "INFO" "$($srcFiles.Count) 个 .cpp 文件"
    Write-Status "头文件" "INFO" "$($headerFiles.Count) 个 .h 文件"
    Write-Status "代码行数" "INFO" "$totalLines 行"
    
    # 统计测试文件
    $testFiles = Get-ChildItem -Path "tests" -Filter "*.cpp" -Recurse -ErrorAction SilentlyContinue
    Write-Status "测试文件" "INFO" "$($testFiles.Count) 个测试文件"
}

# 总结
Write-Host "`n========================================" -ForegroundColor $Cyan
Write-Host "检查总结" -ForegroundColor $Cyan
Write-Host "========================================" -ForegroundColor $Cyan

Write-Host "通过: $passCount" -ForegroundColor $Green
Write-Host "警告: $warnCount" -ForegroundColor $Yellow
Write-Host "失败: $failCount" -ForegroundColor $Red

$score = [math]::Round(($passCount / ($passCount + $warnCount + $failCount)) * 100)
$scoreColor = if ($score -ge 80) { $Green } elseif ($score -ge 60) { $Yellow } else { $Red }
Write-Host "健康分数: $score%" -ForegroundColor $scoreColor

# 建议
Write-Host "`n[建议]" -ForegroundColor $Cyan

if ($warnCount -gt 0 -or $failCount -gt 0) {
    Write-Host "1. 运行 .\tools\verify-config.ps1 验证配置" -ForegroundColor $White
    Write-Host "2. 运行 .\tools\health-check.ps1 进行详细检查" -ForegroundColor $White
    Write-Host "3. 查看 docs\quick-start-guide.md 了解快速开始" -ForegroundColor $White
    Write-Host "4. 查看 docs\troubleshooting-guide.md 解决问题" -ForegroundColor $White
} else {
    Write-Host "项目状态良好！" -ForegroundColor $Green
    Write-Host "1. 开始开发新功能" -ForegroundColor $White
    Write-Host "2. 查看 docs\development-best-practices.md 了解最佳实践" -ForegroundColor $White
}

# 返回状态
if ($failCount -eq 0) {
    exit 0
} else {
    exit 1
}

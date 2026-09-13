# VisionFlowPlatform 项目健康检查工具
# 使用方法：在项目根目录运行此脚本

param(
    [switch]$Detailed,
    [switch]$Export
)

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "VisionFlowPlatform 项目健康检查" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# 检查结果
$health = @{
    Score = 100
    Issues = @()
    Recommendations = @()
    Metrics = @{}
}

# 辅助函数
function Add-Issue {
    param(
        [string]$Category,
        [string]$Severity,
        [string]$Message,
        [int]$Deduction = 0
    )
    
    $health.Issues += @{
        Category = $Category
        Severity = $Severity
        Message = $Message
    }
    
    $health.Score -= $Deduction
}

function Add-Recommendation {
    param(
        [string]$Category,
        [string]$Message
    )
    
    $health.Recommendations += @{
        Category = $Category
        Message = $Message
    }
}

function Add-Metric {
    param(
        [string]$Name,
        [string]$Value
    )
    
    $health.Metrics[$Name] = $Value
}

# 1. 检查代码质量
Write-Host "[1/10] 检查代码质量..." -ForegroundColor Yellow

# 统计代码行数
$sourceFiles = Get-ChildItem -Path "src" -Filter "*.cpp" -Recurse
$headerFiles = Get-ChildItem -Path "include" -Filter "*.h" -Recurse
$totalLines = 0

foreach ($file in $sourceFiles) {
    $totalLines += (Get-Content $file.FullName).Count
}

foreach ($file in $headerFiles) {
    $totalLines += (Get-Content $file.FullName).Count
}

Add-Metric "总代码行数" $totalLines
Add-Metric "源文件数" $sourceFiles.Count
Add-Metric "头文件数" $headerFiles.Count

if ($totalLines -gt 100000) {
    Add-Issue "代码质量" "WARN" "代码量较大（$totalLines 行），建议重构" 5
    Add-Recommendation "代码质量" "考虑将大文件拆分为更小的模块"
}

# 2. 检查测试覆盖率
Write-Host "[2/10] 检查测试覆盖率..." -ForegroundColor Yellow

$testFiles = Get-ChildItem -Path "tests" -Filter "*.cpp" -Recurse
$testCount = $testFiles.Count

Add-Metric "测试文件数" $testCount

if ($testCount -eq 0) {
    Add-Issue "测试" "FAIL" "未找到测试文件" 20
    Add-Recommendation "测试" "添加单元测试和集成测试"
} elseif ($testCount -lt 5) {
    Add-Issue "测试" "WARN" "测试文件较少（$testCount 个）" 10
    Add-Recommendation "测试" "增加测试覆盖率"
}

# 3. 检查文档完整性
Write-Host "[3/10] 检查文档完整性..." -ForegroundColor Yellow

$requiredDocs = @(
    "README.md",
    "docs/api-reference.md",
    "docs/quick-start-guide.md",
    "docs/troubleshooting-guide.md"
)

$missingDocs = @()
foreach ($doc in $requiredDocs) {
    if (-not (Test-Path $doc)) {
        $missingDocs += $doc
    }
}

if ($missingDocs.Count -gt 0) {
    Add-Issue "文档" "WARN" "缺少 $($missingDocs.Count) 个必需文档" 5
    foreach ($doc in $missingDocs) {
        Add-Recommendation "文档" "创建 $doc"
    }
}

# 4. 检查依赖管理
Write-Host "[4/10] 检查依赖管理..." -ForegroundColor Yellow

$cmakeLists = "CMakeLists.txt"
if (Test-Path $cmakeLists) {
    $cmakeContent = Get-Content $cmakeLists -Raw
    
    # 检查版本控制
    if ($cmakeContent -match "cmake_minimum_required\(VERSION (\d+\.\d+)") {
        $cmakeVersion = $matches[1]
        Add-Metric "CMake 最低版本" $cmakeVersion
    }
    
    # 检查 C++ 标准
    if ($cmakeContent -match "set\(CMAKE_CXX_STANDARD (\d+)\)") {
        $cppStandard = $matches[1]
        Add-Metric "C++ 标准" $cppStandard
        
        if ([int]$cppStandard -lt 17) {
            Add-Issue "依赖" "WARN" "C++ 标准过低（$cppStandard），建议使用 C++17" 5
        }
    }
} else {
    Add-Issue "依赖" "FAIL" "未找到 CMakeLists.txt" 10
}

# 5. 检查代码规范
Write-Host "[5/10] 检查代码规范..." -ForegroundColor Yellow

$rulesPath = ".cursor\rules"
if (Test-Path $rulesPath) {
    $ruleFiles = Get-ChildItem $rulesPath -Filter "*.mdc"
    Add-Metric "规则文件数" $ruleFiles.Count
    
    if ($ruleFiles.Count -lt 3) {
        Add-Issue "代码规范" "WARN" "规则文件不完整" 5
        Add-Recommendation "代码规范" "添加更多开发规则文件"
    }
} else {
    Add-Issue "代码规范" "WARN" "未找到代码规范配置" 5
    Add-Recommendation "代码规范" "创建 .cursor\rules 目录并添加规范文件"
}

# 6. 检查构建配置
Write-Host "[6/10] 检查构建配置..." -ForegroundColor Yellow

$buildDir = "build"
if (Test-Path $buildDir) {
    $buildFiles = Get-ChildItem $buildDir -Filter "*.sln" -Recurse
    if ($buildFiles.Count -gt 0) {
        Add-Metric "构建状态" "已构建"
    } else {
        Add-Metric "构建状态" "未构建"
        Add-Recommendation "构建" "运行 cmake 生成构建文件"
    }
} else {
    Add-Metric "构建状态" "未构建"
    Add-Recommendation "构建" "运行 cmake -B build 生成构建文件"
}

# 7. 检查版本控制
Write-Host "[7/10] 检查版本控制..." -ForegroundColor Yellow

if (Test-Path ".git") {
    $gitStatus = git status --porcelain 2>$null
    if ($gitStatus) {
        $changedFiles = ($gitStatus | Measure-Object).Count
        Add-Metric "未提交更改" $changedFiles
        
        if ($changedFiles -gt 20) {
            Add-Issue "版本控制" "WARN" "有 $changedFiles 个未提交的更改" 5
            Add-Recommendation "版本控制" "提交或暂存更改"
        }
    } else {
        Add-Metric "未提交更改" "0"
    }
    
    # 检查 .gitignore
    if (Test-Path ".gitignore") {
        Add-Metric "gitignore" "存在"
    } else {
        Add-Issue "版本控制" "WARN" "缺少 .gitignore 文件" 5
        Add-Recommendation "版本控制" "创建 .gitignore 文件"
    }
} else {
    Add-Issue "版本控制" "WARN" "项目未使用 Git 版本控制" 10
    Add-Recommendation "版本控制" "初始化 Git 仓库"
}

# 8. 检查依赖库
Write-Host "[8/10] 检查依赖库..." -ForegroundColor Yellow

$thirdpartyDir = "thirdparty"
if (Test-Path $thirdpartyDir) {
    $libs = Get-ChildItem $thirdpartyDir -Directory
    Add-Metric "第三方库数" $libs.Count
    
    # 检查 OpenCV
    if (Test-Path "$thirdpartyDir\opencv\build") {
        Add-Metric "OpenCV" "已安装"
    } else {
        Add-Issue "依赖库" "WARN" "未找到 OpenCV" 5
    }
    
    # 检查 ZXing
    if (Test-Path "$thirdpartyDir\zxing") {
        Add-Metric "ZXing" "已安装"
    }
    
    # 检查 Tesseract
    if (Test-Path "$thirdpartyDir\tesseract") {
        Add-Metric "Tesseract" "已安装"
    }
} else {
    Add-Issue "依赖库" "WARN" "未找到第三方库目录" 5
    Add-Recommendation "依赖库" "创建 thirdparty 目录并添加依赖库"
}

# 9. 检查 CI/CD 配置
Write-Host "[9/10] 检查 CI/CD 配置..." -ForegroundColor Yellow

$githubActions = ".github\workflows"
if (Test-Path $githubActions) {
    $workflows = Get-ChildItem $githubActions -Filter "*.yml"
    Add-Metric "CI/CD 工作流" $workflows.Count
    
    if ($workflows.Count -eq 0) {
        Add-Issue "CI/CD" "WARN" "未找到 CI/CD 工作流" 5
        Add-Recommendation "CI/CD" "创建 GitHub Actions 工作流"
    }
} else {
    Add-Issue "CI/CD" "WARN" "未配置 CI/CD" 5
    Add-Recommendation "CI/CD" "创建 .github\workflows 目录并添加工作流"
}

# 10. 检查安全性
Write-Host "[10/10] 检查安全性..." -ForegroundColor Yellow

# 检查敏感文件
$sensitiveFiles = @(
    ".env",
    "credentials.json",
    "secrets.json",
    "*.pem",
    "*.key"
)

$foundSensitive = @()
foreach ($pattern in $sensitiveFiles) {
    $files = Get-ChildItem -Path "." -Filter $pattern -Recurse -ErrorAction SilentlyContinue
    if ($files) {
        $foundSensitive += $files
    }
}

if ($foundSensitive.Count -gt 0) {
    Add-Issue "安全性" "FAIL" "发现 $($foundSensitive.Count) 个敏感文件" 15
    Add-Recommendation "安全性" "将敏感文件添加到 .gitignore 并从版本控制中移除"
}

# 检查 .gitignore 是否包含敏感文件模式
if (Test-Path ".gitignore") {
    $gitignoreContent = Get-Content ".gitignore" -Raw
    
    $requiredPatterns = @(
        "*.env",
        "*.key",
        "*.pem",
        "credentials.json"
    )
    
    $missingPatterns = @()
    foreach ($pattern in $requiredPatterns) {
        if ($gitignoreContent -notmatch [regex]::Escape($pattern)) {
            $missingPatterns += $pattern
        }
    }
    
    if ($missingPatterns.Count -gt 0) {
        Add-Issue "安全性" "WARN" ".gitignore 缺少敏感文件模式" 5
        Add-Recommendation "安全性" "将以下模式添加到 .gitignore: $($missingPatterns -join ', ')"
    }
}

# 计算最终分数
$health.Score = [Math]::Max(0, $health.Score)

# 显示结果
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "项目健康分数" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$scoreColor = if ($health.Score -ge 80) { "Green" } 
              elseif ($health.Score -ge 60) { "Yellow" } 
              else { "Red" }

Write-Host "总分: $($health.Score)/100" -ForegroundColor $scoreColor

# 显示指标
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "项目指标" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

foreach ($metric in $health.Metrics.GetEnumerator()) {
    Write-Host "$($metric.Key): $($metric.Value)" -ForegroundColor White
}

# 显示问题
if ($health.Issues.Count -gt 0) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "发现的问题" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    $issuesByCategory = $health.Issues | Group-Object -Property Category
    
    foreach ($category in $issuesByCategory) {
        Write-Host "`n[$($category.Name)]" -ForegroundColor Yellow
        foreach ($issue in $category.Group) {
            $icon = switch ($issue.Severity) {
                "FAIL" { "✗"; break }
                "WARN" { "⚠"; break }
                default { "•"; break }
            }
            $color = switch ($issue.Severity) {
                "FAIL" { "Red"; break }
                "WARN" { "Yellow"; break }
                default { "White"; break }
            }
            Write-Host "  $icon $($issue.Message)" -ForegroundColor $color
        }
    }
}

# 显示建议
if ($health.Recommendations.Count -gt 0) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host "改进建议" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    
    $recsByCategory = $health.Recommendations | Group-Object -Property Category
    
    foreach ($category in $recsByCategory) {
        Write-Host "`n[$($category.Name)]" -ForegroundColor Yellow
        foreach ($rec in $category.Group) {
            Write-Host "  • $($rec.Message)" -ForegroundColor White
        }
    }
}

# 总结
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "总结" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$issueCount = $health.Issues.Count
$failCount = ($health.Issues | Where-Object { $_.Severity -eq "FAIL" }).Count
$warnCount = ($health.Issues | Where-Object { $_.Severity -eq "WARN" }).Count

if ($failCount -eq 0 -and $warnCount -eq 0) {
    Write-Host "✓ 项目健康状况良好！" -ForegroundColor Green
} elseif ($failCount -eq 0) {
    Write-Host "⚠ 项目有 $warnCount 个警告需要关注" -ForegroundColor Yellow
} else {
    Write-Host "✗ 项目有 $failCount 个严重问题需要立即修复" -ForegroundColor Red
}

# 导出报告
if ($Export) {
    $reportPath = "health-check-report.json"
    $report = @{
        Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        Score = $health.Score
        Metrics = $health.Metrics
        Issues = $health.Issues
        Recommendations = $health.Recommendations
    }
    
    $report | ConvertTo-Json -Depth 10 | Out-File -FilePath $reportPath -Encoding UTF8
    Write-Host "`n报告已导出到: $reportPath" -ForegroundColor Gray
}

# 返回状态
if ($health.Score -ge 60) {
    exit 0
} else {
    exit 1
}

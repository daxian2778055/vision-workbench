# ============================================================
# VisionFlowPlatform 长稳（soak）长跑脚本 (Windows / PowerShell)
# ------------------------------------------------------------
# 用途：在目标机/测试机上跑 N 小时连续运行，观察
#       ① 轮次是否持续增长（节拍不漂移、不停摆）
#       ② 有没有失败轮次
#       ③ 进程句柄数 / 工作集是否持续增长（泄漏信号）
#
# 实测基线（本仓 25 秒短跑，640×480 读图→形态学→Blob→公式→计数 链路）：
#   2313 轮 / 92.5 轮每秒 / 0 失败轮次 / 句柄 Δ1 / 工作集 Δ0 MB
#   —— 短跑只能证明"当前无泄漏"，72h 长跑才能覆盖"长时间后是否劣化"。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1                 # 默认 72 小时
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1 -Hours 0.5     # 半小时试跑
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1 -Hours 72 -IntervalMs 0
#
# 长跑期间可另开一个终端观察进度：
#   Get-Content logs\soak_progress.txt -Wait
# ============================================================

param(
    [double]$Hours = 72,             # 长跑时长（小时），支持小数
    [string]$ProgressPath = "",      # 进度文件（默认 logs\soak_progress.txt，逐秒追加）
    [string]$ReportPath = "",        # 报告文件（默认 logs\soak_report_<时间戳>.txt）
    [string]$TestExe = ""            # 测试可执行文件（默认 build\bin\Release\soak_test.exe）
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

if (-not $TestExe) {
    $TestExe = Join-Path $RepoRoot "build\bin\Release\soak_test.exe"
}
if (-not (Test-Path $TestExe)) {
    Write-Error "找不到长稳测试程序: $TestExe`n请先编译 soak_test 目标（cmake --build build --config Release --target soak_test）。"
    exit 1
}

$logDir = Join-Path $RepoRoot "logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
if (-not $ProgressPath) { $ProgressPath = Join-Path $logDir "soak_progress.txt" }
if (-not $ReportPath)   { $ReportPath   = Join-Path $logDir "soak_report_$stamp.txt" }

$seconds = [int][math]::Round($Hours * 3600)
if ($seconds -lt 5) { $seconds = 5 }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  长稳运行" -ForegroundColor Cyan
Write-Host "  时长   : $Hours 小时（$seconds 秒）"
Write-Host "  进度   : $ProgressPath"
Write-Host "  报告   : $ReportPath"
Write-Host "  观察   : Get-Content '$ProgressPath' -Wait"
Write-Host "========================================" -ForegroundColor Cyan

# 每行带时间戳，便于长跑后定位"哪个时间点开始劣化"
"[$(Get-Date -Format 'HH:mm:ss')] soak start: ${Hours}h" | Out-File -FilePath $ProgressPath -Encoding UTF8 -Append

$env:VFP_SOAK_SECONDS  = "$seconds"
$env:VFP_SOAK_PROGRESS = $ProgressPath
$env:VFP_SOAK_REPORT   = $ReportPath
# 与目标机一致地找 Qt 插件（避免"从构建目录直跑"时的 QSQLITE 假告警）
$deployDir = Join-Path $RepoRoot "dist\VisionFlowPlatform"
if (Test-Path $deployDir) { $env:QT_PLUGIN_PATH = $deployDir }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $TestExe
$psi.Arguments = "-o `"$(Join-Path $logDir "soak_test_$stamp.txt"),txt`""
$psi.WorkingDirectory = $RepoRoot
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)

$timeoutMs = ($seconds + 1800) * 1000     # 结束余量 30 分钟（收尾/落报告）
$sw = [Diagnostics.Stopwatch]::StartNew()
$ok = $p.WaitForExit([int][math]::Min($timeoutMs, [int]::MaxValue))
$sw.Stop()

if (-not $ok) {
    $p.Kill()
    Write-Host "长稳超时未退出（超过 $Hours 小时 + 30 分钟余量），已强制结束" -ForegroundColor Red
    exit 2
}

Write-Host ("运行结束：退出码 {0}，耗时 {1} 分钟" -f $p.ExitCode, [math]::Round($sw.Elapsed.TotalMinutes, 1))

if (Test-Path $ReportPath) {
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Get-Content $ReportPath -Encoding UTF8 | Select-Object -First 3 | ForEach-Object { Write-Host $_ }
    Get-Content $ReportPath -Encoding UTF8 | Select-String -Pattern '逐节点' -Context 0,20 |
        ForEach-Object { $_.Line; $_.Context.PostContext } | ForEach-Object { Write-Host $_ }
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Write-Host "完整报告: $ReportPath" -ForegroundColor Green
} else {
    Write-Host "未生成报告文件（$ReportPath）" -ForegroundColor Yellow
}

exit $p.ExitCode

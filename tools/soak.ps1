# ============================================================
# VisionFlowPlatform soak (long-run stability) script - Windows / PowerShell
# ------------------------------------------------------------
# What it does: runs the soak test for N hours and watches
#   1) rounds keep growing (no stalling / no beat drift)
#   2) no failed rounds
#   3) process handle count / working set do NOT keep growing (leak signal)
#
# Measured baseline (this repo, 25 s short run; read -> morphology -> blob -> formula -> counter):
#   2313 rounds / 92.5 rounds per second / 0 failed rounds / handles delta 1 / working set delta 0 MB
#   The short run only proves "no leak right now"; the 72h run covers long-term degradation.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1                # default 72 hours
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1 -Hours 0.5    # half an hour
#   powershell -ExecutionPolicy Bypass -File tools\soak.ps1 -Hours 0.5 -CsvPath logs\soak_samples.csv
#     -> per-sample CSV: timestamp,elapsed_s,handles,working_set_mb (leak trend, easy to graph)
#
# Watch progress from another terminal while it runs:
#   Get-Content logs\soak_progress.txt -Wait
#
# NOTE: this file is intentionally **pure ASCII**. PowerShell 5.1 reads .ps1 as ANSI unless a BOM
# is present, so non-ASCII string literals can be mangled into parser errors (hit twice already).
# ============================================================

param(
    [double]$Hours = 72,             # run length in hours (fractions allowed)
    [string]$ProgressPath = "",      # progress file (default logs\soak_progress.txt, appended per second)
    [string]$ReportPath = "",        # report file (default logs\soak_report_<timestamp>.txt)
    [string]$CsvPath = "",           # optional sample CSV (handles / working set)
    [int]$SampleSeconds = 10,        # sample interval; lower it for a short smoke run
    [string]$TestExe = ""            # test executable (default build\bin\Release\soak_test.exe)
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

if (-not $TestExe) {
    $TestExe = Join-Path $RepoRoot "build\bin\Release\soak_test.exe"
}
if (-not (Test-Path $TestExe)) {
    Write-Error ("soak test executable not found: {0}`nBuild it first: cmake --build build --config Release --target soak_test" -f $TestExe)
    exit 1
}

$logDir = Join-Path $RepoRoot "logs"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
if (-not $ProgressPath) { $ProgressPath = Join-Path $logDir "soak_progress.txt" }
if (-not $ReportPath)   { $ReportPath   = Join-Path $logDir "soak_report_$stamp.txt" }

$seconds = [int][math]::Round($Hours * 3600)
if ($seconds -lt 5) { $seconds = 5 }
if ($SampleSeconds -lt 1) { $SampleSeconds = 1 }

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Soak run" -ForegroundColor Cyan
Write-Host ("  duration : {0} h ({1} s)" -f $Hours, $seconds)
Write-Host ("  progress : {0}" -f $ProgressPath)
Write-Host ("  report   : {0}" -f $ReportPath)
Write-Host ("  csv      : {0}" -f $(if ($CsvPath) { $CsvPath } else { "(disabled)" }))
Write-Host ("  watch    : Get-Content '{0}' -Wait" -f $ProgressPath)
Write-Host "========================================" -ForegroundColor Cyan

("[{0}] soak start: {1}h" -f (Get-Date -Format 'HH:mm:ss'), $Hours) |
    Out-File -FilePath $ProgressPath -Encoding UTF8 -Append

$env:VFP_SOAK_SECONDS  = "$seconds"
$env:VFP_SOAK_PROGRESS = $ProgressPath
$env:VFP_SOAK_REPORT   = $ReportPath
# The test binary needs Qt + HALCON + OpenCV DLLs. Point PATH at the deployed folder (which carries all
# of them) exactly like the CMake test environment does - otherwise the process dies with 0xC0000135
# (STATUS_DLL_NOT_FOUND) and the report is never produced (hit for real in the first version).
# The old default "dist\VisionFlowPlatform" is a stale folder name - release packages
# are now "dist\VisionFlowPlatform-rc-*". Pick the newest one that actually carries
# halconcpp.dll; otherwise the soak process dies with 0xC0000135 before any report.
$deployDir = ""
$latest = Get-ChildItem (Join-Path $RepoRoot "dist") -Directory -Filter "VisionFlowPlatform-rc-*" -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName "halconcpp.dll") } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latest) {
    $deployDir = $latest.FullName
    $env:PATH = $deployDir + ';' + $env:PATH
    $env:QT_PLUGIN_PATH = $deployDir
    Write-Host ("  dlls     : {0}" -f $deployDir)
} else {
    Write-Host "WARNING: no rc-* package with halconcpp.dll found; relying on system PATH" -ForegroundColor Yellow
}
$qtBin = "D:\Qt\6.11.0\msvc2022_64\bin"
if (Test-Path $qtBin) { $env:PATH = $qtBin + ';' + $env:PATH }

$testLog = Join-Path $logDir "soak_test_$stamp.txt"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $TestExe
$psi.Arguments = "-o `"$testLog`,txt`""
$psi.WorkingDirectory = $RepoRoot
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)

$timeoutMs = [int][math]::Min(($seconds + 1800) * 1000, [int]::MaxValue)   # +30 min tail allowance
$sw = [Diagnostics.Stopwatch]::StartNew()
$csvRows = New-Object System.Collections.Generic.List[string]
if ($CsvPath) { $csvRows.Add("timestamp,elapsed_s,handles,working_set_mb") }
$ok = $false
$lastSample = -1
# Poll in 1s slices so we can sample the child process while it runs. The old
# single WaitForExit($timeoutMs) blocked and gave no per-interval leak trend.
while ($sw.Elapsed.TotalMilliseconds -lt $timeoutMs) {
    if ($p.WaitForExit(1000)) { $ok = $true; break }
    if ($CsvPath) {
        $sec = [int]$sw.Elapsed.TotalSeconds
        if ($sec % $SampleSeconds -eq 0 -and $sec -ne $lastSample) {
            $lastSample = $sec
            try {
                $p.Refresh()
                $csvRows.Add(("{0},{1},{2},{3}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'),
                    $sec, $p.HandleCount, [math]::Round($p.WorkingSet64 / 1MB, 1)))
            } catch { }
        }
    }
}
$sw.Stop()

if (-not $ok) {
    $p.Kill()
    Write-Host ("soak did not exit within {0} h + 30 min allowance - killed" -f $Hours) -ForegroundColor Red
    exit 2
}

Write-Host ("finished: exit code {0}, elapsed {1} min" -f $p.ExitCode, [math]::Round($sw.Elapsed.TotalMinutes, 1))

if (Test-Path $ReportPath) {
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Get-Content $ReportPath -Encoding UTF8 | Select-Object -First 3 | ForEach-Object { Write-Host $_ }
    # per-node section header in the report is Chinese; match via \u escapes to keep this file ASCII
    # (\u9010\u8282\u70B9 = "per node")
    Get-Content $ReportPath -Encoding UTF8 | Select-String -Pattern '\u9010\u8282\u70B9' -Context 0,20 |
        ForEach-Object { $_.Line; $_.Context.PostContext } | ForEach-Object { Write-Host $_ }
    Write-Host "----------------------------------------" -ForegroundColor Cyan
    Write-Host ("full report: {0}" -f $ReportPath) -ForegroundColor Green
} else {
    Write-Host ("report file not produced: {0}" -f $ReportPath) -ForegroundColor Yellow
}

if ($CsvPath -and $csvRows.Count -gt 1) {
    $csvRows | Out-File -FilePath $CsvPath -Encoding UTF8
    Write-Host ("sample CSV: {0} ({1} rows)" -f $CsvPath, ($csvRows.Count - 1)) -ForegroundColor Green
}

exit $p.ExitCode

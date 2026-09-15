<#
    VisionFlowPlatform CI: configure (if needed) -> build -> run CTest -> verify results.

    Usage:
      pwsh -File tools/ci.ps1
      pwsh -File tools/ci.ps1 -Config Release -Clean
      pwsh -File tools/ci.ps1 -SkipTests

    Exit codes:
      0 = build and tests OK
      1 = configure/build failed
      2 = tests failed
      3 = environment prerequisite missing

    Note: this script is intentionally ASCII-only. Windows PowerShell 5.1 reads
    .ps1 files as ANSI when they have no BOM, which would garble non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Config = 'Release',
    [string]$BuildDir = 'build',
    [switch]$Clean,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Write-Step { param([string]$Text) Write-Host "`n=== $Text ===" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Text) Write-Host "  [OK]   $Text" -ForegroundColor Green }
function Write-Warn { param([string]$Text) Write-Host "  [WARN] $Text" -ForegroundColor Yellow }
function Write-Err  { param([string]$Text) Write-Host "  [FAIL] $Text" -ForegroundColor Red }

Push-Location $RepoRoot

# ---------------------------------------------------------------- 1. preflight
Write-Step "Preflight"

$hardMissing = @()
if (-not (Test-Path 'CMakeLists.txt')) { $hardMissing += 'CMakeLists.txt not found (run from the repository)' }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $hardMissing += 'cmake not found on PATH' }

if ($hardMissing.Count -gt 0) {
    foreach ($m in $hardMissing) { Write-Err $m }
    Pop-Location
    exit 3
}
Write-Ok "cmake: $((Get-Command cmake).Source)"

# Soft prerequisites: report clearly, let CMake/test produce the real error if wrong.
$halconRoot = if ($env:HALCONROOT) { $env:HALCONROOT } else { 'D:/Program Files/MVTec/HALCON-24.11-Progress-Steady' }
if (Test-Path $halconRoot) {
    Write-Ok "HALCON root: $halconRoot"
} else {
    Write-Warn "HALCON root '$halconRoot' does not exist; set HALCONROOT before configuring"
}

if (Test-Path 'thirdparty/opencv/build') {
    Write-Ok 'OpenCV: thirdparty/opencv/build'
} else {
    Write-Warn 'thirdparty/opencv/build not found; pass -DOpenCV_DIR=... to configure'
}

$deployDir = 'dist/VisionFlowPlatform'
if (Test-Path $deployDir) {
    Write-Ok "Test runtime DLLs: $deployDir"
} elseif (-not $SkipTests) {
    Write-Warn "$deployDir not found; tests may fail to load Qt/HALCON DLLs (0xc0000135)"
}

# ------------------------------------------------------------------ 2. clean
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Clean build directory"
    Remove-Item -Recurse -Force $BuildDir
    Write-Ok "removed $BuildDir"
}

# -------------------------------------------------------------- 3. configure
if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    Write-Step "Configure (Visual Studio 17 2022 / x64)"
    $cfgLog = Join-Path $RepoRoot 'ci-configure.log'
    & cmake -S . -B $BuildDir -G 'Visual Studio 17 2022' -A x64 2>&1 | Tee-Object -FilePath $cfgLog | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Err "configure failed (exit $LASTEXITCODE); see $cfgLog"
        Get-Content $cfgLog -Tail 25 | ForEach-Object { Write-Host "    $_" }
        Pop-Location
        exit 1
    }
    Write-Ok 'configured'
} else {
    Write-Ok "reusing existing configuration in $BuildDir"
}

# ------------------------------------------------------------------ 4. build
Write-Step "Build ($Config)"
$buildLog = Join-Path $RepoRoot 'ci-build.log'
$buildTimer = [System.Diagnostics.Stopwatch]::StartNew()
& cmake --build $BuildDir --config $Config -- /m /v:minimal 2>&1 | Tee-Object -FilePath $buildLog | Out-Null
$buildCode = $LASTEXITCODE
$buildTimer.Stop()

if ($buildCode -ne 0) {
    Write-Err "build failed (exit $buildCode) after $([int]$buildTimer.Elapsed.TotalSeconds)s"
    Get-Content $buildLog | Select-String -Pattern 'error C|error LNK|fatal error|CMake Error' |
        Select-Object -First 30 | ForEach-Object { Write-Host "    $_" }
    Pop-Location
    exit 1
}
Write-Ok "build succeeded in $([int]$buildTimer.Elapsed.TotalSeconds)s"

if ($SkipTests) {
    Write-Step 'Summary'
    Write-Ok 'build only (-SkipTests)'
    Pop-Location
    exit 0
}

# ------------------------------------------------------------------ 5. tests
Write-Step "Tests (CTest / $Config)"

# QtTest 的结果与 qInfo 在进程无控制台时会被投递到调试器输出（OutputDebugString），
# CI 日志里就完全没有内容——测试失败时看不到任何原因（表现为 LastTest.log 里的
# "<end of output>"）。强制 Qt 把日志写到 stderr，ctest 才能捕获到。
$env:QT_ASSUME_STDERR_HAS_CONSOLE = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'

$testLog = Join-Path $RepoRoot 'ci-test.log'
Push-Location $BuildDir
& ctest -C $Config --output-on-failure 2>&1 | Tee-Object -FilePath (Join-Path $RepoRoot 'ci-test.log') | Out-Null
$testCode = $LASTEXITCODE
Pop-Location

$summaryLine = Select-String -Path $testLog -Pattern 'tests passed, .* failed out of' | Select-Object -Last 1
$perTest = Select-String -Path $testLog -Pattern '^\s*\d+/\d+ Test\s+#\d+' |
    ForEach-Object { ($_.Line -replace '\s+$', '').Trim() }

foreach ($line in $perTest) { Write-Host "  $line" }

$total = 0
$failed = 0
if ($summaryLine) {
    Write-Host ''
    Write-Host "  $($summaryLine.Line.Trim())"
    if ($summaryLine.Line -match '(\d+)% tests passed, (\d+) tests failed out of (\d+)') {
        $failed = [int]$Matches[2]
        $total = [int]$Matches[3]
    }
}

if ($testCode -ne 0 -or $failed -gt 0 -or $total -eq 0) {
    Write-Err "tests failed (ctest exit $testCode, failed=$failed, total=$total); see $testLog"
    Write-Host '  --- last 40 lines ---'
    Get-Content $testLog -Tail 40 | ForEach-Object { Write-Host "    $_" }
    # ctest 把每个测试的完整输出写在 LastTest.log；它不依赖流重定向，失败时一并给出
    $lastTestLog = Join-Path $BuildDir 'Testing\Temporary\LastTest.log'
    if (Test-Path $lastTestLog) {
        Write-Host "  --- $lastTestLog (tail 80) ---"
        Get-Content $lastTestLog -Tail 80 | ForEach-Object { Write-Host "    $_" }
    }
    Pop-Location
    exit 2
}
Write-Ok "all $total tests passed"

# ---------------------------------------------------------------- 6. summary
Write-Step 'Summary'
Write-Ok "build: $Config OK"
Write-Ok "tests: $total/$total passed"
Write-Host ''
Pop-Location
exit 0

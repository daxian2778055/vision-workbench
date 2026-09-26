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
      4 = repository hygiene failed (tools/repo_hygiene.py)
      5 = QtTest result-wrapper self-test failed (tools/selftest_run_qtest_gates.py)
      6 = documentation check failed (tools/doc_check.ps1)
      7 = documentation anchor citations drifted (tools/doc_anchors.py)

    Note: this script is intentionally ASCII-only, and therefore carries no UTF-8 BOM
    (PowerShell 5.1 only needs a BOM when a .ps1 contains non-ASCII text). The two-sided
    encoding boundary is defined and enforced by tools/repo_hygiene.py check 6.
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

# ------------------------------------------------------ 1b. repository hygiene
# Why it runs here: tools/repo_hygiene.py encodes incidents that already happened in this
# repository (leaked runner token, unpinned actions, fork-PR guard on the self-hosted runner,
# the .ps1 encoding boundary, .gitignore invariants). It was written but never wired in, so
# on main it could be red while CI stayed green. Running it before configure costs seconds;
# running it only on GitHub-hosted runners left the local/CI path unguarded.
# It must never be a silent skip: no interpreter = exit 3, hygiene problem = exit 4.
Write-Step "Repository hygiene"

$hygieneExe = $null
$hygienePre = @()
foreach ($cand in @('python', 'py')) {
    if ($hygieneExe) { break }
    if (-not (Get-Command $cand -ErrorAction SilentlyContinue)) { continue }
    $pre = if ($cand -eq 'py') { @('-3') } else { @() }
    $probeArgs = $pre + @('--version')
    & $cand @probeArgs 2>&1 | Out-Null
    # 'python' may be the Microsoft Store execution alias: it reports a version nowhere and
    # exits non-zero, so probe it instead of trusting Get-Command.
    if ($LASTEXITCODE -eq 0) { $hygieneExe = $cand; $hygienePre = $pre }
}
if (-not $hygieneExe) {
    Write-Err 'no usable Python interpreter (tried: python, py -3); repo_hygiene.py cannot run'
    Pop-Location
    exit 3
}

$hygieneLog = Join-Path $RepoRoot 'ci-hygiene.log'
$hygieneArgs = $hygienePre + @('tools/repo_hygiene.py')
& $hygieneExe @hygieneArgs 2>&1 | Tee-Object -FilePath $hygieneLog | Out-Null
$hygieneCode = $LASTEXITCODE

$hygieneLines = @(Get-Content $hygieneLog -ErrorAction SilentlyContinue)
if ($hygieneLines.Count -eq 0) { Write-Host '  (repo_hygiene.py produced no output)' }
foreach ($line in $hygieneLines) { Write-Host "  $line" }
if ($hygieneCode -ne 0) {
    Write-Err "repo hygiene failed (exit $hygieneCode) via '$hygieneExe'; see $hygieneLog"
    Pop-Location
    exit 4
}
Write-Ok "repo hygiene OK ($hygieneExe)"

# --------------------------------------------- 1c. QtTest result-wrapper self-test
# Why it runs here: every CTest suite's PASS/FAIL detail, and every red verdict, comes out of
# tools/run_qtest.cmake. A6 found the hole in it: the wrapper deletes and rewrites
# build/Testing/<Suite>.txt at the start of each run, so an intermittent failure that goes green
# on the next run leaves no evidence and the observation item can never be closed. The wrapper now
# keeps a timestamped <Suite>.failed-<UTC>.txt on every red path; this step pins that plus the
# three original gates against six fake result shapes, so the evidence hook cannot rot silently.
# Must never be a silent skip: self-test failure = exit 5.
Write-Step "QtTest gate self-test"
$gatesLog = Join-Path $RepoRoot 'ci-qtest-gates.log'
$gatesArgs = $hygienePre + @('tools/selftest_run_qtest_gates.py', '--cmake', (Get-Command cmake).Source)
& $hygieneExe @gatesArgs 2>&1 | Tee-Object -FilePath $gatesLog | Out-Null
$gatesCode = $LASTEXITCODE

$gatesLines = @(Get-Content $gatesLog -ErrorAction SilentlyContinue)
if ($gatesLines.Count -eq 0) { Write-Host '  (selftest_run_qtest_gates.py produced no output)' }
foreach ($line in $gatesLines) { Write-Host "  $line" }
if ($gatesCode -ne 0) {
    Write-Err "QtTest gate self-test failed (exit $gatesCode) via '$hygieneExe'; see $gatesLog"
    Pop-Location
    exit 5
}
Write-Ok "QtTest gate self-test OK ($hygieneExe)"

# ------------------------------------------------------ 1d. documentation check
# Why it runs here: tools/doc_check.ps1 compares markdown claims against the repository - it
# re-checks "delivered" node claims and requires every backticked in-repo path to exist. A7 found
# the hole: the script existed but was never wired into anything, so a stale or self-contradictory
# doc claim could not fail any gate (and on 2026-09-26 it did report exit 1 for a backticked path
# of a file whose own sentence says it was deleted). Findings = exit 6, never a silent skip.
Write-Step "Documentation check"
$docLog = Join-Path $RepoRoot 'ci-doc-check.log'
& powershell -NoProfile -ExecutionPolicy Bypass -File 'tools/doc_check.ps1' 2>&1 | Tee-Object -FilePath $docLog | Out-Null
$docCode = $LASTEXITCODE

$docLines = @(Get-Content $docLog -ErrorAction SilentlyContinue)
if ($docLines.Count -eq 0) { Write-Host '  (doc_check.ps1 produced no output)' }
foreach ($line in $docLines) { Write-Host "  $line" }
if ($docCode -ne 0) {
    Write-Err "documentation check failed (exit $docCode); see $docLog"
    Pop-Location
    exit 6
}
Write-Ok "documentation check OK"

# --------------------------------------------------- 1e. documentation anchors
# Why it runs here: the gap plan cites the roadmap and the SRS by line number, and every ledger
# row inserted into the roadmap table shifts those numbers - a stale "doc:137" is an unfalsifiable
# claim (stage A / A7 was opened for exactly that). tools/doc_anchors.py pins each cited position
# with a locating regex plus the published line number, so editing a doc without re-syncing the
# numbers turns this step red instead of leaving prose that points at the wrong line.
# Must never be a silent skip: anchor drift / missing target = exit 7.
Write-Step "Documentation anchors"
$anchorLog = Join-Path $RepoRoot 'ci-doc-anchors.log'
$anchorArgs = $hygienePre + @('tools/doc_anchors.py')
& $hygieneExe @anchorArgs 2>&1 | Tee-Object -FilePath $anchorLog | Out-Null
$anchorCode = $LASTEXITCODE

$anchorLines = @(Get-Content $anchorLog -ErrorAction SilentlyContinue)
if ($anchorLines.Count -eq 0) { Write-Host '  (doc_anchors.py produced no output)' }
foreach ($line in $anchorLines) { Write-Host "  $line" }
if ($anchorCode -ne 0) {
    Write-Err "documentation anchors failed (exit $anchorCode) via '$hygieneExe'; see $anchorLog"
    Pop-Location
    exit 7
}
Write-Ok "documentation anchors OK ($hygieneExe)"

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

# ------------------------------------------------- 4b. artifact freshness gate
# Why: an incremental MSBuild can report exit 0 while having (wrongly) skipped
# compiling a project whose sources were edited afterwards ("cached green").
# A stale/absent test executable then makes ctest either skip the test (Not Run)
# or silently run last build's binary. This gate is pure cost-free insurance:
#   1) every test ctest knows about must have its executable in bin/$Config;
#   2) every test executable must be at least as new as the sources it links.
#
# Where the list of tests comes from: ctest itself ('ctest -N'). The CMakeLists.txt
# regex only maps test name -> executable file. The previous version parsed a literal
# "add_test(... COMMAND <exe>)" instead, which stopped existing the moment the QtTest
# wrapper vfp_add_qtest(NAME <test> TARGET <exe>) was introduced - the regex matched
# nothing and this step killed every ci.ps1 run on main at its own "cannot run" check.
# An unrecognised registration form must stay loud, never silently skipped.
Write-Step "Artifact freshness"

$binDir = Join-Path $BuildDir "bin\$Config"

$ctestListLog = Join-Path $RepoRoot 'ci-ctest-list.log'
Push-Location $BuildDir
& ctest -N -C $Config 2>&1 | Tee-Object -FilePath $ctestListLog | Out-Null
Pop-Location
$listed = @(Get-Content $ctestListLog | ForEach-Object {
    if ($_ -match '^\s*Test\s+#\d+:\s+(\S+)') { $Matches[1] }
})
if ($listed.Count -eq 0) {
    Write-Err "ctest -N listed no tests (see $ctestListLog) - freshness gate cannot run"
    Pop-Location
    exit 1
}

$qtestRe = [regex]'vfp_add_qtest\s*\(\s*NAME\s+(\S+)\s+TARGET\s+([A-Za-z0-9_\-\.]+)'
$addTestRe = [regex]'add_test\s*\(\s*NAME\s+(\S+)\s+COMMAND\s+([A-Za-z0-9_\-\.]+)'
$cmakeText = Get-Content 'CMakeLists.txt' -Raw
$exeOf = @{}
$ownSrcOf = @{}
foreach ($m in $qtestRe.Matches($cmakeText)) {
    $exeOf[$m.Groups[1].Value] = "$($m.Groups[2].Value).exe"
    $ownSrcOf[$m.Groups[1].Value] = "tests/$($m.Groups[2].Value).cpp"
}
foreach ($m in $addTestRe.Matches($cmakeText)) {
    if (-not $exeOf.ContainsKey($m.Groups[1].Value)) {
        $exeOf[$m.Groups[1].Value] = "$($m.Groups[2].Value).exe"
    }
}

$unknown = @()
$missing = @()
foreach ($t in $listed) {
    if (-not $exeOf.ContainsKey($t)) {
        $unknown += "$t (registered in a form this step cannot map to an executable - extend the parsers above, do not delete the gate)"
        continue
    }
    if (-not (Test-Path (Join-Path $binDir $exeOf[$t]))) { $missing += "$($exeOf[$t]) (test $t)" }
}
if ($unknown.Count -gt 0) {
    foreach ($u in $unknown) { Write-Err "unparsed test registration: $u" }
    Pop-Location
    exit 1
}
if ($missing.Count -gt 0) {
    foreach ($x in $missing) { Write-Err "registered test executable missing: bin/$Config/$x" }
    Pop-Location
    exit 1
}

# Baseline: everything that goes into vfp_core, which every test binary links
# (src/, include/, headers, CMakeLists). A test's own tests/<target>.cpp is compared
# per executable - a single suite's source must not mark the other 28 binaries stale.
$newestGlobal = @(Get-ChildItem -Recurse -Include '*.cpp','*.h','*.hpp' `
    -Path 'src','include' -ErrorAction SilentlyContinue) +
    @(Get-Item 'CMakeLists.txt' -ErrorAction SilentlyContinue) |
    Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
$stale = @()
foreach ($t in $listed) {
    $ref = $newestGlobal
    $own = if ($ownSrcOf.ContainsKey($t)) { Get-Item $ownSrcOf[$t] -ErrorAction SilentlyContinue } else { $null }
    if ($own -and (-not $ref -or $own.LastWriteTimeUtc -gt $ref.LastWriteTimeUtc)) { $ref = $own }
    if (-not $ref) { continue }
    $exe = Get-Item (Join-Path $binDir $exeOf[$t])
    if ($exe.LastWriteTimeUtc -lt $ref.LastWriteTimeUtc) {
        $stale += "$($exe.Name) (built $(($exe.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC') < $($ref.Name) $(($ref.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC'))"
    }
}
if ($stale.Count -gt 0) {
    foreach ($s in $stale) { Write-Err "stale test binary (older than its sources, build skipped it?): $s" }
    Pop-Location
    exit 1
}
Write-Ok "$($listed.Count) registered tests, executables present and fresh"

if ($SkipTests) {
    Write-Step 'Summary'
    Write-Ok 'build only (-SkipTests)'
    Pop-Location
    exit 0
}

# ------------------------------------------------------------------ 5. tests
Write-Step "Tests (CTest / $Config)"

# QtTest results and qInfo go to OutputDebugString when the process has no console, so the
# CI log shows nothing at all and a failing test gives no reason (LastTest.log only shows
# "<end of output>"). Force Qt to write its logs to stderr so ctest can capture them.
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
    # ctest writes every test's full output to LastTest.log; it does not depend on stream
    # redirection, so dump it on failure as a fallback.
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

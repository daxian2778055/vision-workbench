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
      8 = dangling-timer site found (tools/single_shot_inventory.py)
      9 = duplicated Chinese sentence drifted against its baseline (tools/dup_cn_literal_gate.py)
      10 = source-line citation roster gained a row outside its frozen baseline
           (tools/src_anchor_inventory.py -- that script's own 1 = ADDED, 2 = unreadable docs,
           3 = script crash; the code it returned is echoed in the failure line)
      11 = host exit-code geometry drifted against its frozen registration
           (tools/ci_exit_code_check.py -- the script's own code is echoed: drifted 1,
           unreadable 2, crash 3)
      12 = ledger growth makes the split-the-file project due
           (tools/ledger_size_gate.py -- the script's own code is echoed: due 1,
           unreadable 2, crash 3)

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

# ------------------------------------------------- 1f. dangling-timer site inventory
# Why it runs here: U-20 measured what a QTimer::singleShot(msec, functor) does once the widget it
# captured is deleted - the functor still fires on the next event loop and the process dies with
# SIGSEGV (rc 139). The runtime legs added in the same round (ParamPanelBindingTest T16) only catch
# a site that some test actually instantiates a panel for; a newly added context-less call site in a
# node nobody builds would stay invisible until a operator hit it in the field. This step keeps the
# mechanical rule (second argument starting with '[' = no context object) as a build-time stop.
# Must never be a silent skip: any context-less site = exit 8.
Write-Step "Dangling-timer site inventory"
$timerLog = Join-Path $RepoRoot 'ci-single-shot.log'
$timerArgs = $hygienePre + @('tools/single_shot_inventory.py')
& $hygieneExe @timerArgs 2>&1 | Tee-Object -FilePath $timerLog | Out-Null
$timerCode = $LASTEXITCODE

$timerLines = @(Get-Content $timerLog -ErrorAction SilentlyContinue)
if ($timerLines.Count -eq 0) { Write-Host '  (single_shot_inventory.py produced no output)' }
foreach ($line in $timerLines) { Write-Host "  $line" }
if ($timerCode -ne 0) {
    Write-Err "dangling-timer site found (exit $timerCode) via '$hygieneExe'; see $timerLog"
    Pop-Location
    exit 8
}
Write-Ok "dangling-timer site inventory OK ($hygieneExe)"

# ------------------------------------------- 1g. duplicated Chinese sentence baseline
# Why it runs here: candidate G-4 carried "same Chinese literal twice = red" for four rounds with
# no rule anyone could run. U-35 measured that wording literally: 537 findings, most of them
# one-word labels - a gate like that gets switched off rather than satisfied. The
# rule was then narrowed with the sweep in the ledger (cross-file only, >= 8 non-whitespace
# characters) and today's 58 remaining duplicates were pinned as a baseline set. This step keeps
# that set from moving: a sentence copied into one more file, or a baseline entry quietly dropped,
# stops the build here instead of becoming prose in the next review.
# Must never be a silent skip: any drift = exit 9 (a broken classifier reports as exit 2 from the
# script itself and fails here too, so it cannot read as clean).
Write-Step "Duplicated Chinese sentence baseline"
$dupLog = Join-Path $RepoRoot 'ci-dup-cn.log'
$dupArgs = $hygienePre + @('tools/dup_cn_literal_gate.py')
& $hygieneExe @dupArgs 2>&1 | Tee-Object -FilePath $dupLog | Out-Null
$dupCode = $LASTEXITCODE

$dupLines = @(Get-Content $dupLog -ErrorAction SilentlyContinue)
if ($dupLines.Count -eq 0) { Write-Host '  (dup_cn_literal_gate.py produced no output)' }
foreach ($line in $dupLines) { Write-Host "  $line" }
if ($dupCode -ne 0) {
    Write-Err "duplicated Chinese sentence drift (exit $dupCode) via '$hygieneExe'; see $dupLog"
    Pop-Location
    exit 9
}
Write-Ok "duplicated Chinese sentence baseline OK ($hygieneExe)"

# -------------------------------------- 1h. source-line citation roster baseline
# Why it runs here: the gap plan cites production source lines by number. U-38 turned that
# population into a printed inventory, U-39 added the second citation style to the denominators,
# and the sixth-round review (S-2) asked for the rows that cannot be window-checked at all --
# citations whose prose names no identifier -- to be frozen as a baseline the way 1g froze its 58
# duplicate sentences. tools/src_anchor_inventory.py carries that baseline (31 rows over 30 keys)
# and returns 1 when the roster gains a row. Until now nothing called it, which is G-1/G-2's named
# failure shape: a gate that can go red but is wired to nothing.
# Judgement stays asymmetric on purpose: only ADDED is red; a REMOVED row is printed and counted,
# because naming the identifier the citation rests on is a fix, not a violation.
# Must never be a silent skip: any non-zero from the script = exit 10. Its own code (1 = ADDED,
# 2 = unreadable docs, 3 = crash) is echoed, so a broken input is never read as a roster finding
# and neither is read as clean.
Write-Step "Source-line citation roster baseline"
$nocandLog = Join-Path $RepoRoot 'ci-nocand-baseline.log'
$nocandArgs = $hygienePre + @('tools/src_anchor_inventory.py')
& $hygieneExe @nocandArgs 2>&1 | Tee-Object -FilePath $nocandLog | Out-Null
$nocandCode = $LASTEXITCODE

$nocandAll = @(Get-Content $nocandLog -ErrorAction SilentlyContinue)
if ($nocandAll.Count -eq 0) { Write-Host '  (src_anchor_inventory.py produced no output)' }
$nocandShown = @($nocandAll | Where-Object { $_ -match '^(NOCANDELTA|ERROR|INV nocand|INV verdict)' })
foreach ($line in $nocandShown) { Write-Host "  $line" }
if ($nocandShown.Count -eq 0 -and $nocandAll.Count -gt 0) {
    Write-Host "  (no NOCANDELTA/INV nocand line in $($nocandAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full citation table: $nocandLog ($($nocandAll.Count) line(s))"
if ($nocandCode -ne 0) {
    Write-Err "source-line citation roster check failed (script exit $nocandCode) via '$hygieneExe'; see $nocandLog"
    Pop-Location
    exit 10
}
Write-Ok "source-line citation roster baseline OK ($hygieneExe)"

# ------------------------------------ 1i. host exit-code geometry self-check
# Why it runs here: every step above turns one script's red into one host code, and step 1h's mapping
# was proved only by a hand-run probe (tools/probes/U43_nocand_host_exit_probe.py) that CI never
# calls. Seventh-round review S-1 named the hole: nothing checks the wiring itself, so a later edit
# could turn "exit 10" into a printed warning that keeps walking and no gate would say so. The static
# half of that arm is now this second-level script, pinned to the bytes of this file: header rows and
# executable stops must agree, each gate step must own its own code, and the gate-code counts are
# frozen so no step can quietly take over another step's number.
# The injected half (a red script really producing that code, end to end) stays a hand-run leg at
# close-out, which is what the probe's heavier arms are for.
# Must never be a silent skip: any non-zero from the script = exit 11 (its own code is echoed, so a
# broken input is never read as a geometry finding and neither is read as clean).
Write-Step "Host exit-code geometry self-check"
$geomLog = Join-Path $RepoRoot 'ci-exit-code-geometry.log'
$geomArgs = $hygienePre + @('tools/ci_exit_code_check.py')
& $hygieneExe @geomArgs 2>&1 | Tee-Object -FilePath $geomLog | Out-Null
$geomCode = $LASTEXITCODE

$geomAll = @(Get-Content $geomLog -ErrorAction SilentlyContinue)
if ($geomAll.Count -eq 0) { Write-Host '  (ci_exit_code_check.py produced no output)' }
$geomShown = @($geomAll | Where-Object { $_ -match '^(GEOMETRY|ERROR|INV findings|INV verdict)' })
foreach ($line in $geomShown) { Write-Host "  $line" }
if ($geomShown.Count -eq 0 -and $geomAll.Count -gt 0) {
    Write-Host "  (no GEOMETRY/INV verdict line in $($geomAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full geometry reading: $geomLog ($($geomAll.Count) line(s))"
if ($geomCode -ne 0) {
    Write-Err "host exit-code geometry check failed (script exit $geomCode) via '$hygieneExe'; see $geomLog"
    Pop-Location
    exit 11
}
Write-Ok "host exit-code geometry OK ($hygieneExe)"

# ----------------------------------------- 1j. ledger growth split trigger
# Why it runs here: the gap plan has carried the trigger "a round that grows this file by more than
# 88 lines opens the split-the-file project first" since U-42, where it was only a printed reading
# inside tools/inline_rewrite_check.py, whose published contract is REPORT_ONLY exit=0. Seventh-round
# review S-2 asked for that reading to be able to stop a run, and S-3 for a second condition, because
# per-round deltas of 80/86/50/68 all pass under 88 while the file keeps growing. The script judges
# three conditions (burst, 3-round rate, absolute size) and prints every number it used.
# Must never be a silent skip: any non-zero from the script = exit 12 (its own code is echoed).
Write-Step "Ledger growth split trigger"
$splitLog = Join-Path $RepoRoot 'ci-ledger-split-gate.log'
$splitArgs = $hygienePre + @('tools/ledger_size_gate.py')
& $hygieneExe @splitArgs 2>&1 | Tee-Object -FilePath $splitLog | Out-Null
$splitCode = $LASTEXITCODE

$splitAll = @(Get-Content $splitLog -ErrorAction SilentlyContinue)
if ($splitAll.Count -eq 0) { Write-Host '  (ledger_size_gate.py produced no output)' }
$splitShown = @($splitAll | Where-Object { $_ -match '^(SPLITDUE|ERROR|INV split_project_due|INV verdict)' })
foreach ($line in $splitShown) { Write-Host "  $line" }
if ($splitShown.Count -eq 0 -and $splitAll.Count -gt 0) {
    Write-Host "  (no SPLITDUE/INV verdict line in $($splitAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full growth reading: $splitLog ($($splitAll.Count) line(s))"
if ($splitCode -ne 0) {
    Write-Err "ledger split trigger fired (script exit $splitCode) via '$hygieneExe'; see $splitLog"
    Pop-Location
    exit 12
}
Write-Ok "ledger split trigger not due ($hygieneExe)"

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
$targetOf = @{}
$ownSrcOf = @{}
foreach ($m in $qtestRe.Matches($cmakeText)) {
    $exeOf[$m.Groups[1].Value] = "$($m.Groups[2].Value).exe"
    $targetOf[$m.Groups[1].Value] = $m.Groups[2].Value
    $ownSrcOf[$m.Groups[1].Value] = "tests/$($m.Groups[2].Value).cpp"
}
foreach ($m in $addTestRe.Matches($cmakeText)) {
    if (-not $exeOf.ContainsKey($m.Groups[1].Value)) {
        $exeOf[$m.Groups[1].Value] = "$($m.Groups[2].Value).exe"
        $targetOf[$m.Groups[1].Value] = $m.Groups[2].Value
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

# Three baseline faces, all derived from CMakeLists.txt (the single source of truth for links):
#   newestGlobal - every src/include source and header, plus CMakeLists.txt. Correct for a
#                  product that links vfp_core: a change anywhere in that library relinks it.
#   newestShared - headers and CMakeLists.txt only, i.e. the global face minus the .cpp files.
#                  Used for a product that does NOT link vfp_core: a .cpp outside its own
#                  add_executable() list is compiled into a library it never links, so it cannot
#                  make that binary stale. Only a fallback here - see the closure below.
#   closure      - for a product that does not link vfp_core, the transitive quoted #includes of
#                  the sources its own add_executable() lists, plus CMakeLists.txt. A .h outside
#                  that closure is recompiled into other products but NOT into this one, so MSBuild
#                  correctly leaves this binary alone - and the header-only face would then call a
#                  perfectly-built product stale.
# The previous version applied the global face to every product on the premise that "every test
# binary links vfp_core". Measured false: 1 of 33 registered tests does not (opencv_nodes_test),
# and it was killed by src/ImageReadNode.cpp - a file that product never links or compiles.
# Dropping to "all headers" then hit the same class of bug on the second axis, measured with a
# REAL incremental build: touching include/AlarmHistoryDialog.h relinked all 32 vfp_core products
# (00:51 UTC) but correctly left opencv_nodes_test.exe at 00:04, and the gate called that stale.
# A target whose add_executable() face this parser cannot find keeps the FULL face, and a target
# with a quoted include this scanner cannot resolve keeps the FULL header face; both are reported.
# An unparsable face must never turn into an exemption.
$cmakeBody = ((Get-Content 'CMakeLists.txt') | ForEach-Object { $_ -replace '^\s*#.*', '' }) -join ' '
$faceOf = @{}
$coreOf = @{}
foreach ($m in [regex]::Matches($cmakeBody, 'add_executable\s*\(\s*([A-Za-z0-9_\-.]+)([^)]*)\)')) {
    $faceOf[$m.Groups[1].Value] = @($m.Groups[2].Value -split '\s+' |
        Where-Object { $_ -match '\.(cpp|c|cc)$' })
}
foreach ($m in [regex]::Matches($cmakeBody, 'target_link_libraries\s*\(\s*([A-Za-z0-9_\-.]+)([^)]*)\)')) {
    if ($m.Groups[2].Value -match '(^|\s)vfp_core(\s|$)') { $coreOf[$m.Groups[1].Value] = $true }
}

# Transitive closure of quoted #includes reachable from a target's own sources. Returns the files
# visited plus the tokens it could not locate: the caller must widen back to the full header face
# on a non-empty 'missing', because an include this scanner cannot resolve is a hole in the
# closure, not a licence to ignore that header. Commented-out includes are not followed (^# only).
function Get-VfpIncludeClosure {
    param([string[]]$Sources)
    $seen = @{}
    $missing = @()
    $queue = New-Object System.Collections.Generic.Queue[string]
    foreach ($s in $Sources) { if (Test-Path -LiteralPath $s) { $queue.Enqueue((Resolve-Path -LiteralPath $s).Path) } }
    while ($queue.Count -gt 0) {
        $cur = $queue.Dequeue()
        if ($seen.ContainsKey($cur)) { continue }
        if ($seen.Count -ge 200) { $missing += "$cur (closure cap reached)" ; continue }
        $seen[$cur] = $true
        $body = Get-Content -Raw -LiteralPath $cur -ErrorAction SilentlyContinue
        if (-not $body) { continue }
        $dir = Split-Path -Parent $cur
        foreach ($m in [regex]::Matches($body, '(?m)^\s*#\s*include\s+"([^"]+)"')) {
            $tok = $m.Groups[1].Value
            $hit = $null
            foreach ($c in @((Join-Path $dir $tok), (Join-Path 'src' $tok), (Join-Path 'include' $tok))) {
                if (Test-Path -LiteralPath $c) { $hit = (Resolve-Path -LiteralPath $c).Path; break }
            }
            if ($hit) { $queue.Enqueue($hit) } else { $missing += "$cur -> `"$tok`"" }
        }
    }
    @{ files = @($seen.Keys); missing = @($missing) }
}

$allFace = @(Get-ChildItem -Recurse -Include '*.cpp','*.h','*.hpp' `
    -Path 'src','include' -ErrorAction SilentlyContinue) +
    @(Get-Item 'CMakeLists.txt' -ErrorAction SilentlyContinue)
$sharedFace = @(Get-ChildItem -Recurse -Include '*.h','*.hpp' `
    -Path 'src','include' -ErrorAction SilentlyContinue) +
    @(Get-Item 'CMakeLists.txt' -ErrorAction SilentlyContinue)
$baseGlobal = @( $allFace | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1 )
$baseShared = @( $sharedFace | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1 )
if ($baseGlobal.Count -gt 0) { $baseGlobal = $baseGlobal[0] } else { $baseGlobal = $null }
if ($baseShared.Count -gt 0) { $baseShared = $baseShared[0] } else { $baseShared = $null }
Write-Host "  face: $($allFace.Count) files on the vfp_core baseline, $($sharedFace.Count) on the header-only baseline" -ForegroundColor DarkGray

$stale = @()
$narrow = @()
$faceUnknown = @()
$closureFallback = @()
$closureSizes = @()
foreach ($t in $listed) {
    $target = if ($targetOf.ContainsKey($t)) { $targetOf[$t] } else { $null }
    $faceKnown = ($target -and $faceOf.ContainsKey($target))
    $own = @()
    if ($faceKnown) {
        $own = @(foreach ($s in $faceOf[$target]) { Get-Item $s -ErrorAction SilentlyContinue })
    } else {
        if ($target) { $faceUnknown += "$t ($target)" }
        # Unresolved face: keep the suite's own tests/<target>.cpp as an extra source to check,
        # exactly as this step did before the per-target face existed.
        $ownSrc = if ($ownSrcOf.ContainsKey($t)) { Get-Item $ownSrcOf[$t] -ErrorAction SilentlyContinue } else { $null }
        if ($ownSrc) { $own = @($ownSrc) }
    }
    $newestOwn = @($own | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
    if ($newestOwn.Count -gt 0) { $newestOwn = $newestOwn[0] } else { $newestOwn = $null }

    $narrowed = ($faceKnown -and -not $coreOf.ContainsKey($target))
    $ref = $baseGlobal
    if ($narrowed) {
        $narrow += $t
        $cl = Get-VfpIncludeClosure -Sources @($faceOf[$target])
        if (@($cl.missing).Count -gt 0) {
            $closureFallback += ($target + ' - ' + ((@($cl.missing)) -join '; '))
            $ref = $baseShared
        } else {
            $closureFace = @(foreach ($f in @($cl.files)) { Get-Item -LiteralPath $f -ErrorAction SilentlyContinue }) +
                @(Get-Item 'CMakeLists.txt' -ErrorAction SilentlyContinue)
            $newest = @($closureFace | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
            $ref = if ($newest.Count -gt 0) { $newest[0] } else { $baseShared }
            $closureSizes += "$target -> $(@($cl.files).Count) file(s), baseline $(if ($newest.Count -gt 0) { $newest[0].Name } else { '(none)' })"
        }
    }
    if ($newestOwn -and (-not $ref -or $newestOwn.LastWriteTimeUtc -gt $ref.LastWriteTimeUtc)) { $ref = $newestOwn }
    if (-not $ref) { continue }
    $exe = Get-Item (Join-Path $binDir $exeOf[$t])
    if ($exe.LastWriteTimeUtc -lt $ref.LastWriteTimeUtc) {
        $stale += "$($exe.Name) (built $(($exe.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC') < $($ref.Name) $(($ref.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC'))"
    }
}
foreach ($u in $faceUnknown) {
    Write-Warn "no add_executable() face parsed for $u - kept on the full vfp_core baseline (an unparsable face is never an exemption)"
}
foreach ($c in $closureSizes) { Write-Host "  closure: $c" -ForegroundColor DarkGray }
foreach ($c in $closureFallback) {
    Write-Warn "include closure incomplete for $c - kept on the full header baseline (an unresolved include is never an exemption)"
}
if ($stale.Count -gt 0) {
    foreach ($s in $stale) { Write-Err "stale test binary (older than its sources, build skipped it?): $s" }
    Pop-Location
    exit 1
}
Write-Ok "$($listed.Count) registered tests, executables present and fresh ($($narrow.Count) judged on their own include closure)"

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

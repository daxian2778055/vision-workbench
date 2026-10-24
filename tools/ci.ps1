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
      13 = a log the gap plan cites cannot be opened on this machine and is not in the frozen
           baseline (tools/log_citation_gate.py -- the script's own code is echoed: ADDED 1,
           unreadable 2, crash 3)
      14 = the register appendix broke its append-only promise: a row it had already published was
           altered or dropped, or an in-cell append changed that row's shape
           (tools/register_shape_gate.py -- the script's own code is echoed: RED 1,
           unreadable or no rounds judged 2, crash 3)
      15 = a registered "still open" item that was measured closed has regressed, or the registry
           itself is malformed (tools/open_items_gate.py -- findings 1, gate could not run 2)
      16 = the release package is incoherent with HEAD: tag / manifest / exe md5 mismatch, a missing
           manifest, a dirty-worktree claim, or the gate's own selftest failed
           (tools/release_manifest_check.py + tools/selftest_release_manifest.py)
      17 = Version.h single-source promise broken: a template @TOKEN@ the build-time refresh script
           does not replace, or the two generators produced different bytes
           (tools/version_header_check.py)
      18 = the canvas edit-lock wiring drifted: an execution slot no longer re-evaluates the lock,
           or MainWindow::addNode (toolbox double-click) no longer checks it
           (tools/edit_lock_wiring_check.py)

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

# Decode child-process output as UTF-8. This repo's Python gates (version_header_check,
# release_manifest_check, ...) write Chinese verdict text to stdout (they reconfigure their own
# streams to UTF-8). PowerShell would otherwise decode those bytes with the console code page
# (cp936/GBK on Simplified Chinese Windows), so the Chinese in the Tee'd ci-*.log files comes out
# as mojibake -- verdict codes, finding codes and exit numbers are ASCII and unaffected, but the
# human-readable half of the line is lost (2026-10-24 review: 2 of 32 ci-*.log files had this,
# exactly the two carrying Chinese finding lines). Failure is not fatal: some hosted/redirected
# contexts throw on the setter, and that only restores the old mojibake shape.
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }
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

# ------------------------------------------ 1k. cited-log presence check
# Why it runs here: every row of the gap plan that says "(log X.txt)" promises the reviewer a file
# they can open. Two such promises were broken by this project's own hand in the ninth round, when
# U44_probe_run2.txt and U44_probe_run3.txt were renamed mid-round, and the only thing keeping the
# citations honest was somebody remembering. Tenth-round review item 3 asked for a machine gate for
# that convention; tools/log_citation_gate.py is it: it reads every citation out of the ledger,
# resolves each one on this machine, and compares the unresolved set against its frozen baseline
# (only the ADDED direction is red; REMOVED is report-only, so fixing an old citation can never be
# blocked by the gate that measures it). Its declared skip for a clone without the git-ignored
# scratch trees prints a CITE-SKIP line that this step echoes, so a skip is visible, never silent.
# Must never be a silent skip: any non-zero from the script = exit 13 (its own code is echoed).
Write-Step "Cited-log presence check"
$citeLog = Join-Path $RepoRoot 'ci-citation-gate.log'
$citeArgs = $hygienePre + @('tools/log_citation_gate.py')
& $hygieneExe @citeArgs 2>&1 | Tee-Object -FilePath $citeLog | Out-Null
$citeCode = $LASTEXITCODE

$citeAll = @(Get-Content $citeLog -ErrorAction SilentlyContinue)
if ($citeAll.Count -eq 0) { Write-Host '  (log_citation_gate.py produced no output)' }
$citeShown = @($citeAll | Where-Object { $_ -match '^(CITE|ERROR|INV citation_baseline|INV verdict)' })
foreach ($line in $citeShown) { Write-Host "  $line" }
if ($citeShown.Count -eq 0 -and $citeAll.Count -gt 0) {
    Write-Host "  (no CITE/INV verdict line in $($citeAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full citation reading: $citeLog ($($citeAll.Count) line(s))"
if ($citeCode -ne 0) {
    Write-Err "cited-log presence check failed (script exit $citeCode) via '$hygieneExe'; see $citeLog"
    Pop-Location
    exit 13
}
Write-Ok "cited logs all present or in the frozen baseline ($hygieneExe)"

# ---------------------------------------- 1l. register appendix append-only check
# Why it runs here: the second ledger face (docs/<gap-plan>-<register-appendix>.md, the numbered
# table split out of the gap plan) publishes one row per round, and promises that a published row is
# never rewritten underneath the citations that point at it. tools/register_shape_gate.py turned
# that promise into a red-capable reading in the previous round, but nothing in the host called it,
# so the promise lived in prose again -- the same dead-gate shape as G-1/G-2, and the reason steps
# 1h..1k exist. The step runs the tool's whole-history mode: the committed face is what a reviewer
# replays, and an uncommitted rewrite of an old row is judged by the same pass (the tool prints its
# own pending-worktree pair). Cost, measured not assumed: on this machine, history mode (13 commits
# touching, 12 pairs) took 6.3 s against 0.48 s for one pair, ratio 13 -- both printed by
# build/u44_probe/U67_step_cost_1.txt. A face whose only commit has no parent
# is a NO_ROUNDS exit 2, so a shallow clone cannot read as a silent green.
# Must never be a silent skip: any non-zero from the script = exit 14 (its own code is echoed).
Write-Step "Register appendix append-only check"
$regLog = Join-Path $RepoRoot 'ci-register-shape.log'
$regArgs = $hygienePre + @('tools/register_shape_gate.py')
& $hygieneExe @regArgs 2>&1 | Tee-Object -FilePath $regLog | Out-Null
$regCode = $LASTEXITCODE

$regAll = @(Get-Content $regLog -ErrorAction SilentlyContinue)
if ($regAll.Count -eq 0) { Write-Host '  (register_shape_gate.py produced no output)' }
$regShown = @($regAll | Where-Object { $_ -match '^(\[RG\] face=|\[RG-INV\]|\[RG-FIND\]|\[RG-VERDICT\]|\[RG-ABORT\]|ERROR)' })
foreach ($line in $regShown) { Write-Host "  $line" }
if ($regShown.Count -eq 0 -and $regAll.Count -gt 0) {
    Write-Host "  (no RG-INV/RG-VERDICT line in $($regAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full register-face reading: $regLog ($($regAll.Count) line(s))"
if ($regCode -ne 0) {
    Write-Err "register appendix append-only check failed (script exit $regCode) via '$hygieneExe'; see $regLog"
    Pop-Location
    exit 14
}
Write-Ok "register appendix rows preserved, appends shaped as registered ($hygieneExe)"

# ------------------------------------------------------------------ 1l
# Why it runs here: the appendix carries a "still open" list in prose, and prose does not change on its
# own. U-111 measured every item on it and found 8 of 11 measurably closed, 1 with no object to measure,
# and one that had been open for about thirty rounds describing a tool that has no such option -- so the
# list had rotted without anybody noticing, because re-measuring it depended on somebody remembering.
# This step is that re-measurement, on every host run. Items measured closed are ENFORCED: they are
# re-measured and any mismatch is exit 15, which is what stops a closure from rotting back the way the
# list did. Items still open are printed with a current reading and never move the verdict -- a
# known-red item inside the host is exactly the shape that left the host permanently red in U-109.
# Items whose measurement needs a git-ignored file are printed as unmeasurable_here rather than read as
# green. Must never be a silent skip: any non-zero from the script = exit 15.
Write-Step "Registered open-item re-measurement"
$oigLog = Join-Path $RepoRoot 'ci-open-items.log'
$oigArgs = $hygienePre + @('tools/open_items_gate.py')
& $hygieneExe @oigArgs 2>&1 | Tee-Object -FilePath $oigLog | Out-Null
$oigCode = $LASTEXITCODE

$oigAll = @(Get-Content $oigLog -ErrorAction SilentlyContinue)
$oigShown = @($oigAll | Where-Object { $_ -match '^\[OIG' })
foreach ($line in $oigShown) { Write-Host "  $line" }
if ($oigShown.Count -eq 0 -and $oigAll.Count -gt 0) {
    Write-Host "  (no OIG line in $($oigAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full open-item reading: $oigLog ($($oigAll.Count) line(s))"
if ($oigCode -ne 0) {
    Write-Err "registered open-item re-measurement failed (script exit $oigCode) via '$hygieneExe'; see $oigLog"
    Pop-Location
    exit 15
}
Write-Ok "enforced open items re-measured as closed ($hygieneExe)"

# ------------------------------------------------------------------ 1m-1
# Why: the 2026-10-24 field report ("tools cannot be dragged in; double-click inserts one that then
# cannot be moved") traced to two wiring omissions in MainWindow: onExecutionError did not
# re-evaluate the canvas lock (unlike Started/Stopped/Finished), and the toolbox double-click path
# (MainWindow::addNode) bypassed the edit-lock check that drag/drop performs. The runtime test
# (EditLockReleaseTest) pins the engine half -- "after a failure-stop the executor allows editing";
# this step pins the wiring half, because a deleted call is invisible to any test that drives the
# executor directly. Selftest first (two arms: remove the call, defuse the check) so a dead gate
# cannot read green. Any non-zero = exit 18.
Write-Step "Canvas edit-lock wiring check"
$elwSelfLog = Join-Path $RepoRoot 'ci-edit-lock-selftest.log'
$elwSelfArgs = $hygienePre + @('tools/edit_lock_wiring_check.py', '--repo', $RepoRoot, '--selftest')
& $hygieneExe @elwSelfArgs 2>&1 | Tee-Object -FilePath $elwSelfLog | Out-Null
$elwSelfCode = $LASTEXITCODE
$elwSelfAll = @(Get-Content $elwSelfLog -ErrorAction SilentlyContinue)
$elwSelfShown = @($elwSelfAll | Where-Object { $_ -match '^\[ELW' })
foreach ($line in $elwSelfShown) { Write-Host "  $line" }
if ($elwSelfShown.Count -eq 0 -and $elwSelfAll.Count -gt 0) {
    Write-Host "  (no ELW line in $($elwSelfAll.Count) log line(s) - the selftest did not reach its summary)"
}
if ($elwSelfCode -ne 0) {
    Write-Host "  selftest failed (exit $elwSelfCode) - the real check below is not trusted" -ForegroundColor Yellow
}

$elwLog = Join-Path $RepoRoot 'ci-edit-lock.log'
$elwArgs = $hygienePre + @('tools/edit_lock_wiring_check.py', '--repo', $RepoRoot)
& $hygieneExe @elwArgs 2>&1 | Tee-Object -FilePath $elwLog | Out-Null
$elwCode = $LASTEXITCODE
$elwAll = @(Get-Content $elwLog -ErrorAction SilentlyContinue)
$elwShown = @($elwAll | Where-Object { $_ -match '^\[ELW' })
foreach ($line in $elwShown) { Write-Host "  $line" }
if ($elwShown.Count -eq 0 -and $elwAll.Count -gt 0) {
    Write-Host "  (no ELW line in $($elwAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full edit-lock reading: $elwLog ($($elwAll.Count) line(s))"
if ($elwSelfCode -ne 0 -or $elwCode -ne 0) {
    Write-Err "canvas edit-lock wiring check failed (selftest exit $elwSelfCode, check exit $elwCode) via '$hygieneExe'; see $elwSelfLog / $elwLog"
    Pop-Location
    exit 18
}
Write-Ok "canvas lock re-evaluated in every execution slot and addNode guarded"

# ------------------------------------------------------------------ 1m-0
# Why it runs here: build/generated/Version.h has two producers -- the configure-time
# configure_file bootstrap and the build-time refresh script (vfp_version_header). The template is
# single-source, but "which @TOKEN@ gets replaced" lived in two places, and a future fourth token
# would be replaced by configure_file and NOT by the refresh script -- drift returning through
# another seam. This step makes the single-source promise a command: token coverage plus
# byte-equality of both generators on identical inputs. The selftest arm runs first so a dead gate
# can never read as green. Must never be a silent skip: any non-zero = exit 17.
Write-Step "Version header single-source check"
$vhcSelfLog = Join-Path $RepoRoot 'ci-version-header-selftest.log'
$vhcSelfArgs = $hygienePre + @('tools/version_header_check.py', '--repo', $RepoRoot, '--selftest')
& $hygieneExe @vhcSelfArgs 2>&1 | Tee-Object -FilePath $vhcSelfLog | Out-Null
$vhcSelfCode = $LASTEXITCODE
$vhcSelfAll = @(Get-Content $vhcSelfLog -ErrorAction SilentlyContinue)
$vhcSelfShown = @($vhcSelfAll | Where-Object { $_ -match '^\[VHC' })
foreach ($line in $vhcSelfShown) { Write-Host "  $line" }
if ($vhcSelfShown.Count -eq 0 -and $vhcSelfAll.Count -gt 0) {
    Write-Host "  (no VHC line in $($vhcSelfAll.Count) log line(s) - the selftest did not reach its summary)"
}
if ($vhcSelfCode -ne 0) {
    Write-Host "  selftest failed (exit $vhcSelfCode) - the real check below is not trusted" -ForegroundColor Yellow
}

$vhcLog = Join-Path $RepoRoot 'ci-version-header.log'
$vhcArgs = $hygienePre + @('tools/version_header_check.py', '--repo', $RepoRoot)
& $hygieneExe @vhcArgs 2>&1 | Tee-Object -FilePath $vhcLog | Out-Null
$vhcCode = $LASTEXITCODE
$vhcAll = @(Get-Content $vhcLog -ErrorAction SilentlyContinue)
$vhcShown = @($vhcAll | Where-Object { $_ -match '^\[VHC' })
foreach ($line in $vhcShown) { Write-Host "  $line" }
if ($vhcShown.Count -eq 0 -and $vhcAll.Count -gt 0) {
    Write-Host "  (no VHC line in $($vhcAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full version-header reading: $vhcLog ($($vhcAll.Count) line(s))"
if ($vhcSelfCode -ne 0 -or $vhcCode -ne 0) {
    Write-Err "version header single-source check failed (selftest exit $vhcSelfCode, check exit $vhcCode) via '$hygieneExe'; see $vhcSelfLog / $vhcLog"
    Pop-Location
    exit 17
}
Write-Ok "version header single-source (token coverage + both generators byte-identical)"

# ------------------------------------------------------------------ 1m
# Why it runs here: the 2026-10-23 review found release artifacts were verified by eye and text only.
# The same round measured deploy.ps1 aborting BEFORE writing the manifest in two ways (untagged HEAD's
# `describe --exact-match` non-zero; `Get-FileHash` missing in a restricted PowerShell) -- so a package
# could be missing its self-identification file and nobody would notice. This step makes
# tag / manifest / exe-md5 a command. In auto mode a HEAD without an rc-* tag prints an EXPLICIT skip
# (dist/ is git-ignored, dev builds may legitimately be stale) -- never a silent pass; once HEAD carries
# an rc-* tag the same check is hard. Must never be a silent skip: selftest or check non-zero = exit 16.
Write-Step "Release package coherence (tag / manifest / exe md5)"
$rmSelfLog = Join-Path $RepoRoot 'ci-release-manifest-selftest.log'
$rmSelfArgs = $hygienePre + @('tools/selftest_release_manifest.py')
& $hygieneExe @rmSelfArgs 2>&1 | Tee-Object -FilePath $rmSelfLog | Out-Null
$rmSelfCode = $LASTEXITCODE
$rmSelfAll = @(Get-Content $rmSelfLog -ErrorAction SilentlyContinue)
$rmSelfShown = @($rmSelfAll | Where-Object { $_ -match '^\[RSM' })
foreach ($line in $rmSelfShown) { Write-Host "  $line" }
if ($rmSelfShown.Count -eq 0 -and $rmSelfAll.Count -gt 0) {
    Write-Host "  (no RSM line in $($rmSelfAll.Count) log line(s) - the selftest did not reach its summary)"
}
if ($rmSelfCode -ne 0) {
    # No separate stop here: a gate step may own exactly one exit code (geometry legs C/E), so the
    # selftest failure is folded into the single exit 16 below.
    Write-Host "  selftest failed (exit $rmSelfCode) - the real check below is not trusted" -ForegroundColor Yellow
}

$rmLog = Join-Path $RepoRoot 'ci-release-manifest.log'
$rmArgs = $hygienePre + @('tools/release_manifest_check.py', '--repo', $RepoRoot)
& $hygieneExe @rmArgs 2>&1 | Tee-Object -FilePath $rmLog | Out-Null
$rmCode = $LASTEXITCODE
$rmAll = @(Get-Content $rmLog -ErrorAction SilentlyContinue)
$rmShown = @($rmAll | Where-Object { $_ -match '^\[RM' })
foreach ($line in $rmShown) { Write-Host "  $line" }
if ($rmShown.Count -eq 0 -and $rmAll.Count -gt 0) {
    Write-Host "  (no RM line in $($rmAll.Count) log line(s) - the script did not reach its summary)"
}
Write-Host "  full release reading: $rmLog ($($rmAll.Count) line(s))"
if ($rmSelfCode -ne 0 -or $rmCode -ne 0) {
    Write-Err "release package gate failed (selftest exit $rmSelfCode, check exit $rmCode) via '$hygieneExe'; see $rmSelfLog / $rmLog"
    Pop-Location
    exit 16
}
Write-Ok "release package coherent with HEAD tag (or dev build explicitly skipped)"

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

# Does CMakeLists.txt count as a dependency for ONE target's binary? (U-109)
#
# This step used to put the whole CMakeLists.txt into every face on the premise that "a change
# anywhere in the link file can relink any product". Measured false at 22269a3: ba28be1 added a
# brand-new add_executable(node_search_test ...) block and touched nothing else, and the gate then
# called opencv_nodes_test.exe (built 2026-09-30) stale against CMakeLists.txt's new mtime. A full
# cmake --build leaves that binary alone, because the new block does not name it -- so the red was
# permanent and unbuildable, and it blocked every ci.ps1 run on main.
#
# Three questions decide it, all answerable locally:
#   1 is the worktree dirty?      worktree != HEAD            -> counts (changed, maybe unbuilt)
#   2 is the binary older than the last commit that touched CMakeLists.txt?
#                               newer -> does NOT count (content has not moved since the build)
#   3 did that commit touch THIS target's own registration lines?  yes -> counts; no -> not
# Every step that cannot be answered falls closed: CMakeLists counts. An unreadable answer must
# never turn into an exemption, which is the same rule the include closure above already follows.
function Get-CmakeCountsForTarget {
    param([string]$Target, [datetime]$ExeUtc)
    if (-not $Target) { return @{ counts = $true; why = 'target name unknown - fail closed' } }
    # 1 is the worktree dirty?
    & git diff --quiet HEAD -- CMakeLists.txt 2>$null
    if ($LASTEXITCODE -ne 0) {
        return @{ counts = $true; why = 'worktree CMakeLists.txt differs from HEAD' } }
    $lastRev = (& git log -1 --format=%H -- CMakeLists.txt 2>$null)
    if (-not $lastRev) { return @{ counts = $true; why = 'no git history for CMakeLists.txt - fail closed' } }
    $lastTs = [int](& git log -1 --format=%ct -- CMakeLists.txt 2>$null)
    if ($lastTs -le 0) { return @{ counts = $true; why = 'commit date unreadable - fail closed' } }
    $lastUtc = [datetimeoffset]::FromUnixTimeSeconds($lastTs).UtcDateTime
    # 2 is the binary OLDER than the last commit that touched CMakeLists.txt? if NEWER than skip:
    if ($ExeUtc -ge $lastUtc) { return @{ counts = $false; why = 'binary is newer than the last CMakeLists commit' } }
    # 3 did that commit touch THIS target's own registration lines?
    & git diff --unified=0 "$lastRev^" "$lastRev" -- CMakeLists.txt 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        return @{ counts = $true; why = 'cannot diff the last CMakeLists commit - fail closed' } }
    $diff = (& git diff --unified=0 "$lastRev^" "$lastRev" -- CMakeLists.txt 2>$null)
    $changed = @($diff | Where-Object { $_ -match '^[+-]' -and $_ -notmatch '^(\+\+\+|---)' })
    if (@($changed).Count -eq 0) { return @{ counts = $false; why = 'last CMakeLists commit shows no changed line' } }
    $hits = @($changed | Where-Object { $_ -match [regex]::Escape($Target) })
    if (@($hits).Count -gt 0) {
        return @{ counts = $true; why = "last CMakeLists commit touches this target" } }
    return @{ counts = $false; why = ("last CMakeLists commit " + $lastRev.Substring(0, 7) + " does not name this target") }
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
# U-109: the same two baselines with CMakeLists.txt taken out, for the targets whose
# Get-CmakeCountsForTarget answer is "does not count". The counts stay on one line so a
# reviewer can see how many targets were judged on each side.
$baseGlobalNoCmake = @( $allFace | Where-Object { $_.Name -ne 'CMakeLists.txt' } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1 )
if ($baseGlobalNoCmake.Count -gt 0) { $baseGlobalNoCmake = $baseGlobalNoCmake[0] } else { $baseGlobalNoCmake = $null }
$baseSharedNoCmake = @( $sharedFace | Where-Object { $_.Name -ne 'CMakeLists.txt' } | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1 )
if ($baseSharedNoCmake.Count -gt 0) { $baseSharedNoCmake = $baseSharedNoCmake[0] } else { $baseSharedNoCmake = $null }
Write-Host "  face: $($allFace.Count) files on the vfp_core baseline, $($sharedFace.Count) on the header-only baseline" -ForegroundColor DarkGray

$stale = @()
$narrow = @()
$faceUnknown = @()
$closureFallback = @()
$closureSizes = @()
$cmakeNotes = @()
foreach ($t in $listed) {
    $target = if ($targetOf.ContainsKey($t)) { $targetOf[$t] } else { $null }
    # U-109: decide whether CMakeLists.txt is this binary's dependency at all, before any
    # face is built. The exe is present (checked above) so its mtime is readable here.
    $exe = Get-Item (Join-Path $binDir $exeOf[$t])
    $cmakeRef = Get-CmakeCountsForTarget -Target $target -ExeUtc $exe.LastWriteTimeUtc
    $cmakeNotes += ("{0} -> counts={1} ({2})" -f $t, $cmakeRef.counts, $cmakeRef.why)
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
    $ref = if ($cmakeRef.counts) { $baseGlobal } else { $baseGlobalNoCmake }
    if ($narrowed) {
        $narrow += $t
        $cl = Get-VfpIncludeClosure -Sources @($faceOf[$target])
        if (@($cl.missing).Count -gt 0) {
            $closureFallback += ($target + ' - ' + ((@($cl.missing)) -join '; '))
            $ref = if ($cmakeRef.counts) { $baseShared } else { $baseSharedNoCmake }
        } else {
            $closureFace = @(foreach ($f in @($cl.files)) { Get-Item -LiteralPath $f -ErrorAction SilentlyContinue }) +
                @(if ($cmakeRef.counts) { Get-Item 'CMakeLists.txt' -ErrorAction SilentlyContinue })
            $newest = @($closureFace | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1)
            $ref = if ($newest.Count -gt 0) { $newest[0] } else { $baseShared }
            $closureSizes += "$target -> $(@($cl.files).Count) file(s), baseline $(if ($newest.Count -gt 0) { $newest[0].Name } else { '(none)' })"
        }
    }
    if ($newestOwn -and (-not $ref -or $newestOwn.LastWriteTimeUtc -gt $ref.LastWriteTimeUtc)) { $ref = $newestOwn }
    if (-not $ref) { continue }
    if ($exe.LastWriteTimeUtc -lt $ref.LastWriteTimeUtc) {
        $stale += "$($exe.Name) (built $(($exe.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC') < $($ref.Name) $(($ref.LastWriteTimeUtc.ToString('yyyy-MM-dd HH:mm:ss')) + ' UTC'))"
    }
}
foreach ($u in $faceUnknown) {
    Write-Warn "no add_executable() face parsed for $u - kept on the full vfp_core baseline (an unparsable face is never an exemption)"
}
foreach ($c in $closureSizes) { Write-Host "  closure: $c" -ForegroundColor DarkGray }
# U-109: one line per target saying whether CMakeLists.txt counted for it and why. Printed
# because a rule that silently drops a dependency is exactly the shape this gate exists for.
$cmakeCounted = @($cmakeNotes | Where-Object { $_ -match 'counts=True' }).Count
Write-Host "  CMakeLists.txt counted for $cmakeCounted of $($cmakeNotes.Count) target(s); the rest are judged on their own faces only" -ForegroundColor DarkGray
foreach ($c in $cmakeNotes) { Write-Host "  cmake: $c" -ForegroundColor DarkGray }
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

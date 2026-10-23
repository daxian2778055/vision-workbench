<#
  ci_host_run.ps1 -- run the host CI script and put ITS OWN exit code into the same stdout
  stream the caller redirects to a log.

  Why this exists (U-53, 2026-10-03). ci.ps1 sets an explicit exit code on every failure path
  (exit 2..13) and ends with 'exit 0', but that number only reaches a reader if the invocation
  keeps the child's process status. The sixteenth round's close-out cited a host end-to-end run
  whose log (build/u44_probe/U52_ci_host_1.txt) carries the verdict lines and no exit-code line
  at all, because the command had been piped into tail: a pipeline reports the tail's status, not
  the child's. The claim was therefore about printed text only. This wrapper makes the exit code
  part of the recorded bytes, so citing the log cites the measurement too.

  Run:
    powershell -NoProfile -ExecutionPolicy Bypass -File tools/ci_host_run.ps1
    ... -InnerScript <path>    test arm: any script with the same shape; default tools/ci.ps1

  Last line of stdout on the normal path:
    [HOST-RC] inner=<path> ci_exit_code=<n> wrapper_exit_code=<n> verdict_line=<YES|NO> output_lines=<n>
  Exit codes:
    the child's own code, when the child produced one (and, for 0, printed its pass verdict);
    20 = the child never started (script or interpreter missing, no process status) -- not 0, so
         "could not launch" can never be read as a green host run;
    21 = the child exited 0 without printing the test-passed verdict line -- a silent zero.
         'tests: 0/0 passed' does not count as that line.
#>
param(
    [string]$InnerScript = 'tools/ci.ps1'
)

$ErrorActionPreference = 'Continue'

# Both sides of the fraction must be >= 1: 'tests: 0/0 passed' is a run that never ran anything,
# and it must not satisfy this contract (ci.ps1 has its own total -eq 0 guard, exit 2, but the
# wrapper's rule is checked by an arm of its own and has to hold on its own terms).
$VerdictPattern = '\[OK\]\s+tests: [1-9][0-9]*/[1-9][0-9]* passed'

Write-Host ('=== ci_host_run: inner=' + $InnerScript + ' ===')

if (-not (Test-Path -LiteralPath $InnerScript -PathType Leaf)) {
    Write-Host ('[HOST-RC-ABORT] inner script not found: ' + $InnerScript +
        ' -- refusing to report an exit code for a child that never started')
    exit 20
}

# Run the child with the same interpreter that is running this file, so the two-sided
# pwsh / Windows PowerShell difference cannot make the wrapper launch something the caller
# would not have launched itself.
$exe = $null
try { $exe = (Get-Process -Id $PID -ErrorAction Stop).Path } catch { $exe = $null }
if (-not $exe -or -not (Test-Path -LiteralPath $exe)) {
    foreach ($cand in @('powershell', 'pwsh')) {
        $cmd = Get-Command $cand -ErrorAction SilentlyContinue
        if ($cmd -and $cmd.Source) { $exe = $cmd.Source; break }
    }
}
if (-not $exe) {
    Write-Host '[HOST-RC-ABORT] no PowerShell interpreter could be resolved -- a missing interpreter is not a green host run'
    exit 20
}
Write-Host ('ci_host_run interpreter: ' + $exe)

$global:LASTEXITCODE = $null
# 流式透出：子进程输出一边实时写本进程 stdout（调用方重定向到的日志因此随跑随长），
# 一边落临时文件供结束后扫描判读行与计数。
# 旧写法是 `$raw = & $exe ...` 先把全部输出收进变量再打印，表现为"头几分钟日志只有
# 包装器自己那两行、子进程退出才一次性倾泻"——排障时极易误判为卡死（2026-10-23 复核 P3）。
# 契约不变：末行仍是 [HOST-RC] ...，退出码仍是子进程自己的码（Tee-Object 不覆盖
# $LASTEXITCODE，与 ci.ps1 内部各闸的 `| Tee-Object | Out-Null` 同一用法）。
$rawLog = Join-Path $env:TEMP ('ci_host_run_' + $PID + '.log')
try {
    & $exe -NoProfile -ExecutionPolicy Bypass -File $InnerScript 2>&1 |
        Tee-Object -FilePath $rawLog |
        ForEach-Object { Write-Host ('' + $_); [Console]::Out.Flush() }
    $rc = $LASTEXITCODE
    $raw = @()
    if (Test-Path -LiteralPath $rawLog) { $raw = @(Get-Content -LiteralPath $rawLog) }
} finally {
    if (Test-Path -LiteralPath $rawLog) {
        Remove-Item -LiteralPath $rawLog -Force -ErrorAction SilentlyContinue
    }
}

if ($null -eq $rc) {
    Write-Host '[HOST-RC-ABORT] the child produced no process exit code (it never started) -- not reporting 0'
    exit 20
}

$verdictLine = $null
foreach ($line in @($raw)) {
    if (('' + $line) -match $VerdictPattern) { $verdictLine = ('' + $line).Trim(); break }
}
$verdictSeen = if ($null -ne $verdictLine) { 'YES' } else { 'NO' }

Write-Host ('[HOST-RC] inner=' + $InnerScript + ' ci_exit_code=' + $rc +
    ' wrapper_exit_code=' + $rc + ' verdict_line=' + $verdictSeen +
    ' output_lines=' + @($raw).Count)

if ($rc -eq 0 -and $null -eq $verdictLine) {
    Write-Host '[HOST-RC-ABORT] the child exited 0 without its test-passed verdict line -- treating a silent zero as a failure'
    exit 21
}

exit $rc

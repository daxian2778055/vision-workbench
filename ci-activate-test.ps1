$ErrorActionPreference = 'Continue'
$repo = 'E:\halcon\2\xin1'
$dist = Join-Path $repo 'dist\VisionFlowPlatform'
$tmp  = Join-Path $repo 'ci-tmp'
Set-Location $repo

'=== build ==='
& cmake --build build --config Release --target VisionFlowPlatform -- /m /v:minimal *> (Join-Path $repo 'ci-build-local.log')
$buildExit = $LASTEXITCODE
"build exit=$buildExit"
$errs = Select-String -Path (Join-Path $repo 'ci-build-local.log') -Pattern 'error C|error LNK|fatal error' -Encoding UTF8
if ($errs) { 'BUILD ERRORS:'; $errs | Select-Object -First 8 | ForEach-Object { $_.Line } } else { 'build clean' }
if ($buildExit -ne 0) { 'ABORT: build failed'; exit 1 }

'=== prepare isolated test copy (own TEMP => own single-instance lock) ==='
New-Item -ItemType Directory -Force -Path $tmp | Out-Null
$testExe = Join-Path $dist 'VisionFlowPlatform_t.exe'
Copy-Item (Join-Path $repo 'build\bin\Release\VisionFlowPlatform.exe') $testExe -Force

$env:TEMP = $tmp
$env:TMP  = $tmp
$log = Join-Path $dist 'logs\crash.log'

$a = Start-Process -FilePath $testExe -WorkingDirectory $dist -PassThru
Start-Sleep -Seconds 14
"A alive after 14s: " + (-not $a.HasExited)

$b = Start-Process -FilePath $testExe -WorkingDirectory $dist -PassThru
Start-Sleep -Seconds 8
if ($b.HasExited) { "B exit code = 0x{0:X8}" -f $b.ExitCode } else { 'B STILL RUNNING (unexpected)'; Stop-Process -Id $b.Id -Force }
Start-Sleep -Seconds 2

'=== did the running instance receive the activation request? ==='
$tail = Get-Content $log -Tail 40 -Encoding UTF8
if ($tail | Where-Object { $_ -match 'activation request received' }) { 'ACTIVATION RECEIVED: yes' } else { 'ACTIVATION RECEIVED: no' }

'=== cleanup ==='
if (-not $a.HasExited) { Stop-Process -Id $a.Id -Force }
Start-Sleep -Seconds 1
Remove-Item $testExe -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path $repo 'ci-build-local.log') -Force -ErrorAction SilentlyContinue
'cleanup done (user instance untouched)'

<#
    VisionFlowPlatform: install and register a self-hosted GitHub Actions runner on Windows.

    Usage (elevated PowerShell if you plan to install it as a service):
      powershell -ExecutionPolicy Bypass -File tools/setup-runner.ps1 -Token <REGISTRATION_TOKEN> -Start
      powershell -ExecutionPolicy Bypass -File tools/setup-runner.ps1 -Pat <PERSONAL_ACCESS_TOKEN> -Start

    Credentials (exactly one of them is required):
      -Token  registration token from GitHub: repo -> Settings -> Actions -> Runners -> New runner.
              Valid for about ONE HOUR and single use; prefer -Pat when scripting.
      -Pat    classic personal access token with the "repo" scope; not time limited.

    Parameters:
      -Token       registration token issued by GitHub
      -Pat         personal access token (alternative to -Token)
      -RepoUrl     repository URL            (default: this project)
      -Name        runner name               (default: computer name)
      -Labels      extra custom labels       (default: vfp-win)
      -InstallDir  install directory         (default: C:\actions-runner)
      -Version     runner version            (default: 2.337.0)
      -WorkDir     runner work directory     (default: _work)
      -AsService   install as a Windows service instead of a console process
      -Start       start the listener after configuring and wait until it is online

    Prerequisites on the machine (see .github/workflows/ci.yml header):
      Visual Studio 2022 + CMake, Qt 6.11 discoverable by find_package(Qt6),
      HALCONROOT set, and the repository's thirdparty/ and dist/ assets present.

    Note: this script is intentionally ASCII-only. Windows PowerShell 5.1 reads .ps1
    files as ANSI when they have no BOM, which would garble non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$Token,
    [string]$Pat,
    [string]$RepoUrl = 'https://github.com/daxian2778055/vision-workbench',
    [string]$Name = $env:COMPUTERNAME,
    [string]$Labels = 'vfp-win',
    [string]$InstallDir = 'C:\actions-runner',
    [string]$Version = '2.337.0',
    [string]$WorkDir = '_work',
    [switch]$AsService,
    [switch]$Start
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Text) Write-Host "`n=== $Text ===" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Text) Write-Host "  [OK]   $Text" -ForegroundColor Green }
function Write-Warn { param([string]$Text) Write-Host "  [WARN] $Text" -ForegroundColor Yellow }
function Write-Err  { param([string]$Text) Write-Host "  [FAIL] $Text" -ForegroundColor Red }

Push-Location $InstallDir -ErrorAction SilentlyContinue

try {

# ----------------------------------------------------------------- 0. preflight
Write-Step 'Preflight'
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Set-Location $InstallDir
Write-Ok "install directory: $InstallDir"

if (-not $Token -and -not $Pat) {
    Write-Err 'provide either -Token (registration token, expires in ~1h) or -Pat (personal access token)'
    exit 3
}

if ($AsService) {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $isAdmin) {
        Write-Err 'installing the runner as a service requires an elevated PowerShell'
        exit 3
    }
}

# -------------------------------------------------------------- 1. download
$zipName = "actions-runner-win-x64-$Version.zip"
$zipPath = Join-Path $InstallDir $zipName
$url = "https://github.com/actions/runner/releases/download/v$Version/$zipName"

# Known SHA256 for validated releases; other versions are downloaded with a warning.
$knownHashes = @{
    '2.337.0' = '1150692AFA94E71F872017E254EA55B6EECE1EECE3FE7E3A6D4C93D0A1B85CFC'
}

Write-Step "Download runner v$Version"
if (Test-Path $zipPath) {
    Write-Ok "$zipName already downloaded, reusing it"
} else {
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
    Write-Ok "downloaded $zipName"
}

# -------------------------------------------------------------- 2. checksum
Write-Step 'Verify checksum'
$expected = $knownHashes[$Version]
if ($expected) {
    $actual = (Get-FileHash $zipPath -Algorithm SHA256).Hash.ToUpper()
    if ($actual -ne $expected) {
        Write-Err "checksum mismatch: expected $expected, got $actual"
        exit 4
    }
    Write-Ok "SHA256 matches $expected"
} else {
    Write-Warn "no known checksum for v$Version; skipping verification"
}

# --------------------------------------------------------------- 3. extract
Write-Step 'Extract'
if (Test-Path (Join-Path $InstallDir 'config.cmd')) {
    Write-Ok 'runner already extracted, skipping'
} else {
    Expand-Archive -Path $zipPath -DestinationPath $InstallDir -Force
    Write-Ok "extracted to $InstallDir"
}

# ------------------------------------------------------------- 4. configure
Write-Step 'Configure'
$configArgs = @(
    '--url', $RepoUrl,
    '--name', $Name,
    '--labels', $Labels,
    '--work', $WorkDir,
    '--unattended',
    '--replace'
)
if ($AsService) { $configArgs += '--runasservice' }

# 凭据：注册 token（约 1 小时有效、一次性）或 PAT（用于长期/自动化注册）
if ($Pat) {
    $configArgs = @('--pat', $Pat) + $configArgs
} else {
    $configArgs = @('--token', $Token) + $configArgs
}

& cmd /c "config.cmd $($configArgs -join ' ')" 2>&1 | ForEach-Object { Write-Host "    $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Err "config.cmd failed (exit $LASTEXITCODE)"
    exit 5
}
Write-Ok "runner '$Name' registered with labels: self-hosted, windows, x64, $Labels"

if ($AsService) {
    Write-Ok 'installed as a Windows service (started automatically)'
    Write-Host ''
    exit 0
}

if (-not $Start) {
    Write-Step 'Next step'
    Write-Host '  Start the listener with:'
    Write-Host "    cd $InstallDir; .\run.cmd"
    Write-Host ''
    exit 0
}

# ----------------------------------------------------------------- 5. start
Write-Step 'Start listener'
$proc = Start-Process -FilePath (Join-Path $InstallDir 'run.cmd') -WorkingDirectory $InstallDir `
                      -WindowStyle Hidden -PassThru
Write-Ok "listener started (pid $($proc.Id))"

# ------------------------------------------------------- 6. wait for online
Write-Step 'Wait until online'
$logDir = Join-Path $InstallDir '_diag'
$deadline = (Get-Date).AddSeconds(90)
$online = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    $log = Get-ChildItem $logDir -Filter 'Runner_*.log' -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime | Select-Object -Last 1
    if ($log) {
        $tail = Get-Content $log.FullName -Tail 40 -ErrorAction SilentlyContinue
        if ($tail -match 'Listening for Jobs') { $online = $true; break }
        if ($tail -match 'Runner listener exit|Cannot connect|Authentication') { break }
    }
}
if ($online) {
    Write-Ok 'runner is online and listening for jobs'
} else {
    Write-Warn 'could not confirm the listener came online within 90s; check _diag logs'
}
exit 0

} finally {
    Pop-Location -ErrorAction SilentlyContinue
}

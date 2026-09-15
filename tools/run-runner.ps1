<#
    VisionFlowPlatform: start the self-hosted GitHub Actions runner with a sanitized PATH.

    Why this wrapper exists:
      GitHub's runner resolves the step shell through WhichUtil.Which("pwsh"), which
      enumerates EVERY directory on PATH. A single entry that cannot be enumerated - for
      example a dangling junction left behind by an uninstalled tool, or a stale
      directory - aborts every "run:" step with:

        System.IO.DirectoryNotFoundException: Could not find a part of the path '...'
           at GitHub.Runner.Sdk.WhichUtil.Which(String command, ...)

      This wrapper drops such entries before starting the listener, so job steps run
      normally. Entries are kept only when they can actually be enumerated.

    Usage:
      powershell -ExecutionPolicy Bypass -File tools/run-runner.ps1
      powershell -ExecutionPolicy Bypass -File tools/run-runner.ps1 -InstallDir C:\actions-runner

    Note: this script is intentionally ASCII-only. Windows PowerShell 5.1 reads .ps1
    files as ANSI when they have no BOM, which would garble non-ASCII text.
#>
[CmdletBinding()]
param(
    [string]$InstallDir = 'C:\actions-runner'
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path (Join-Path $InstallDir 'run.cmd'))) {
    Write-Host ("[FAIL] run.cmd not found under " + $InstallDir + "; run tools/setup-runner.ps1 first") -ForegroundColor Red
    exit 3
}

# ----------------------------------- rebuild PATH the way a fresh shell would see it
# A long-running runner keeps the environment it was started with; tools installed later
# (for example PowerShell 7) would otherwise stay invisible until the machine reboots.
$machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
$userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
$env:Path = [Environment]::ExpandEnvironmentVariables(($machinePath + ';' + $userPath))

# ------------------------------------------------- keep only usable PATH entries
$kept = @()
$dropped = @()
foreach ($p in ($env:Path -split ';')) {
    if ([string]::IsNullOrWhiteSpace($p)) { continue }
    try {
        [System.IO.Directory]::EnumerateFileSystemEntries($p) | Out-Null
        $kept += $p
    } catch {
        $dropped += $p
    }
}
$env:Path = ($kept -join ';')
foreach ($d in $dropped) {
    Write-Host ("[drop] PATH entry that cannot be enumerated: " + $d) -ForegroundColor Yellow
}
Write-Host ("[ok]   PATH sanitized: kept " + $kept.Count + ", dropped " + $dropped.Count)

# ------------------------- prefer the real PowerShell 7 over the Store alias stub
# Windows ships an "app execution alias" at
#   %LOCALAPPDATA%\Microsoft\WindowsApps\pwsh.EXE
# which is a zero-byte placeholder. When PowerShell 7 comes from the MSI/Store-less install,
# launching that stub fails with "找不到适用的应用许可证" (no applicable app license) and
# every shell step dies. Prepending the real install directory makes the runner pick it first.
$pwshCandidates = @(
    'C:\Program Files\PowerShell\7',
    'D:\Program Files\PowerShell\7',
    'C:\Program Files (x86)\PowerShell\7'
)
foreach ($dir in $pwshCandidates) {
    if (Test-Path (Join-Path $dir 'pwsh.exe')) {
        if ($env:Path -notlike ("*" + $dir + "*")) {
            $env:Path = $dir + ';' + $env:Path
            Write-Host ("[ok]   prepended real pwsh: " + $dir)
        }
        break
    }
}

# ----------------------------------------------- make sure the step shell exists
$pwsh = Get-Command pwsh -ErrorAction SilentlyContinue
if ($pwsh) {
    Write-Host ("[ok]   step shell pwsh: " + $pwsh.Source)
} else {
    Write-Host "[warn] pwsh (PowerShell 7) not found on the sanitized PATH." -ForegroundColor Yellow
    Write-Host "[warn] the workflow uses 'shell: pwsh'; install PowerShell 7 or switch to 'shell: powershell'." -ForegroundColor Yellow
}

# ----------------------------------------------------------------- start listener
Write-Host ("[ok]   starting runner from " + $InstallDir + " (Ctrl+C to stop)")
Set-Location $InstallDir
& cmd /c "run.cmd"

# download-wheels.ps1 - fetch every wheel the train machine needs, so it can install offline.
#
# Run this on a machine WITH internet. It targets win_amd64 + a specific CPython version,
# because wheels are ABI-specific: a cp312 wheel will not install on 3.10 and vice versa.
param(
    [string]$PythonVersion = "3.10",
    [string]$WheelsDir = "wheels",
    [switch]$Gpu,
    [switch]$WithPythonInstaller
)
$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "[download-wheels] $msg" }

# "3.10" -> "310" / "cp310"
$tag = ($PythonVersion -replace '\.', '')
$abi = "cp$tag"
if (-not (Test-Path $WheelsDir)) { New-Item -ItemType Directory -Path $WheelsDir | Out-Null }

$python = (Get-Command python -ErrorAction SilentlyContinue)
if (-not $python) { Write-Error "python not found in PATH (any 3.8+ works as the downloader)"; exit 2 }

$common = @("-m", "pip", "download", "--only-binary=:all:", "--platform", "win_amd64",
            "--python-version", $tag, "--implementation", "cp", "--abi", $abi,
            "--dest", $WheelsDir)

$torchIndex = if ($Gpu) { "https://download.pytorch.org/whl/cu121" } else { "https://download.pytorch.org/whl/cpu" }
Write-Step "torch/torchvision from $torchIndex (target cp$tag win_amd64)"
& python @common torch torchvision --index-url $torchIndex
if ($LASTEXITCODE -ne 0) { Write-Error "torch download failed"; exit 3 }

Write-Step "remaining deps from PyPI"
& python @common -r (Join-Path $PSScriptRoot "requirements-train.txt")
if ($LASTEXITCODE -ne 0) { Write-Error "dependency download failed"; exit 4 }

if ($WithPythonInstaller) {
    # Python itself is not a wheel: fetch the official installer so the train machine needs no internet.
    $pyUrl = "https://www.python.org/ftp/python/$PythonVersion.11/python-$PythonVersion.11-amd64.exe"
    $out = Join-Path $WheelsDir ("python-$PythonVersion-amd64.exe")
    try {
        Write-Step "python installer: $pyUrl"
        Invoke-WebRequest -Uri $pyUrl -OutFile $out -UseBasicParsing
    } catch {
        Write-Warning "python installer download failed (offline copy it manually): $($_.Exception.Message)"
    }
}

$files = Get-ChildItem $WheelsDir -File
$mb = [math]::Round((($files | Measure-Object Length -Sum).Sum / 1MB), 1)
Write-Step ("done: {0} files, {1} MB in {2}" -f $files.Count, $mb, (Resolve-Path $WheelsDir))
Write-Step "copy this folder to the train machine and run: install-train-env.ps1 -Offline"

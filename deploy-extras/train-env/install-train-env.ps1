# install-train-env.ps1 - create the YOLO training environment (ASCII only on purpose)
#
# Why a script and not "just pip install": the order matters (torch from the CPU index,
# the rest from PyPI), and the offline path must not touch the network at all. Both are
# written down once here instead of in a README nobody follows under pressure.
param(
    [Parameter(Mandatory = $true)][string]$PythonExe,
    [string]$VenvDir = ".venv",
    [switch]$Offline,
    [switch]$Gpu,
    [string]$WheelsDir = "wheels"
)
$ErrorActionPreference = 'Stop'

function Write-Step($msg) { Write-Host "[train-env] $msg" }

if (-not (Test-Path $PythonExe)) {
    Write-Error "Python not found: $PythonExe"
    exit 2
}
$ver = & $PythonExe -c "import sys; print('%d.%d' % sys.version_info[:2])"
Write-Step "python $ver ($PythonExe)"
if ([version]$ver -lt [version]'3.8' -or [version]$ver -gt [version]'3.11') {
    Write-Warning "YOLOX expects Python 3.8-3.10; $ver may not have torch wheels."
}

if (-not (Test-Path $VenvDir)) {
    Write-Step "creating venv: $VenvDir"
    & $PythonExe -m venv $VenvDir
}
$py = Join-Path $VenvDir "Scripts\python.exe"
if (-not (Test-Path $py)) { Write-Error "venv python missing: $py"; exit 2 }

$pip = @($py, "-m", "pip")
$wheels = Resolve-Path $WheelsDir -ErrorAction SilentlyContinue
$hasWheels = $wheels -and (Get-ChildItem $wheels -Filter *.whl -ErrorAction SilentlyContinue)

if ($Offline -and -not $hasWheels) {
    Write-Error "Offline mode but no wheels in '$WheelsDir'. Run download-wheels.ps1 on a networked machine first."
    exit 3
}

& $pip[0] $pip[1] $pip[2] install --upgrade pip | Out-Null

if ($hasWheels) {
    Write-Step "installing torch/torchvision from local wheels"
    & $py -m pip install --no-index --find-links $wheels torch torchvision
    Write-Step "installing remaining deps from local wheels"
    & $py -m pip install --no-index --find-links $wheels -r (Join-Path $PSScriptRoot "requirements-train.txt")
} else {
    $torchIndex = if ($Gpu) { "https://download.pytorch.org/whl/cu121" } else { "https://download.pytorch.org/whl/cpu" }
    Write-Step "installing torch/torchvision from $torchIndex"
    & $py -m pip install torch torchvision --index-url $torchIndex
    Write-Step "installing remaining deps from PyPI"
    & $py -m pip install -r (Join-Path $PSScriptRoot "requirements-train.txt")
}

Write-Step "verifying imports"
& $py -c "import torch, torchvision, numpy, cv2, yaml; print('torch', torch.__version__, '| cuda', torch.cuda.is_available(), '| numpy', numpy.__version__)"
if ($LASTEXITCODE -ne 0) {
    Write-Error "Verification failed - check the output above."
    exit 4
}

Write-Step "done. Use: $py -m yolox.tools.train ... (see README section 5)"

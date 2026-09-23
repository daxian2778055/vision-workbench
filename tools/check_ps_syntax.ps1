# ============================================================
# PowerShell syntax check for a single .ps1 file (Windows / PowerShell)
# ------------------------------------------------------------
# Why a separate script: parsing **several** files inside one process gave inconsistent results
# (the same file reported 0 errors in one run and N errors in the next, measured). One file per
# process is deterministic, which is what a gate needs.
#
# Why this matters at all: PowerShell 5.1 reads .ps1 as ANSI unless the file has a BOM, so a
# non-ASCII string literal can be mangled into a parser error and the script silently becomes
# unusable (hit twice in this repo: tools/soak.ps1 was committed broken; tools/project-status.ps1
# had a "$Name:" variable reference that fails to parse).
#
# Usage:   powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_ps_syntax.ps1 -Path <file.ps1>
# Exit:    0 = ok, 1 = parse errors (first error printed), 2 = usage error
# ============================================================

param(
    [Parameter(Mandatory = $true)][string]$Path
)

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host ("usage error: file not found: {0}" -f $Path)
    exit 2
}

$full = (Resolve-Path -LiteralPath $Path).Path

# UTF-8 BOM is mandatory for .ps1 in this repo: PowerShell 5.1 decodes .ps1 as ANSI when no BOM is
# present, which mangles non-ASCII string literals - that silently made 4 scripts in tools/ unusable
# (they could not even be parsed). Keep the rule enforced, not documented.
$bytes = [System.IO.File]::ReadAllBytes($full)
$hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
if (-not $hasBom) {
    Write-Host ("FAIL {0}: missing UTF-8 BOM (PowerShell 5.1 would read this file as ANSI)" -f (Split-Path -Leaf $full))
    exit 1
}

$tokens = $null
$errs = $null
[void][System.Management.Automation.Language.Parser]::ParseFile($full, [ref]$tokens, [ref]$errs)

$count = if ($errs) { @($errs).Count } else { 0 }
if ($count -eq 0) {
    Write-Host ("ok {0}" -f (Split-Path -Leaf $full))
    exit 0
}

$first = @($errs)[0]
Write-Host ("PARSE FAIL {0}: L{1} {2}" -f (Split-Path -Leaf $full), $first.Extent.StartLineNumber, $first.Message)
exit 1

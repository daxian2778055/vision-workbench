# ============================================================
# PowerShell syntax check for a single .ps1 file (Windows / PowerShell)
# ------------------------------------------------------------
# Why a separate script: parsing **several** files inside one process gave inconsistent results
# (the same file reported 0 errors in one run and N errors in the next, measured). One file per
# process is deterministic, which is what a gate needs.
#
# Why this matters at all: PowerShell 5.1 reads a BOM-less .ps1 as ANSI, so a non-ASCII string
# literal can be mangled into a parser error and the script silently becomes
# unusable (hit twice in this repo: tools/soak.ps1 was committed broken; tools/project-status.ps1
# had a "$Name:" variable reference that fails to parse).
#
# Usage:   powershell -NoProfile -ExecutionPolicy Bypass -File tools\check_ps_syntax.ps1 -Path <file.ps1>
# Exit:    0 = ok, 1 = encoding or parse errors (first one printed), 2 = usage error
# ============================================================

param(
    [Parameter(Mandatory = $true)][string]$Path
)

$Utf8Bom = @(0xEF, 0xBB, 0xBF)

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host ("usage error: file not found: {0}" -f $Path)
    exit 2
}

$full = (Resolve-Path -LiteralPath $Path).Path

# Encoding boundary, identical to tools/repo_hygiene.py check 6 (which runs in CI):
#   non-ASCII bytes  -> UTF-8 BOM is REQUIRED (5.1 decodes BOM-less .ps1 as ANSI and mangles them)
#   pure ASCII       -> BOM is FORBIDDEN (redundant; the old blanket "always BOM" rule could not
#                       tell these two cases apart)
$bytes = [System.IO.File]::ReadAllBytes($full)
$hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq $Utf8Bom[0] -and $bytes[1] -eq $Utf8Bom[1] -and $bytes[2] -eq $Utf8Bom[2])
$nonAscii = 0
$from = if ($hasBom) { 3 } else { 0 }
for ($i = $from; $i -lt $bytes.Length; $i++) {
    if ($bytes[$i] -gt 127) { $nonAscii++ }
}
if ($nonAscii -gt 0 -and -not $hasBom) {
    Write-Host ("FAIL {0}: {1} non-ASCII byte(s) without UTF-8 BOM (PowerShell 5.1 would read this file as ANSI)" -f (Split-Path -Leaf $full), $nonAscii)
    exit 1
}
if ($nonAscii -eq 0 -and $hasBom) {
    Write-Host ("FAIL {0}: carries a UTF-8 BOM but its content is pure ASCII - drop the BOM" -f (Split-Path -Leaf $full))
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

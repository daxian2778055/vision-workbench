# ============================================================
# Documentation reconciliation check (Windows / PowerShell)
# ------------------------------------------------------------
# Why: this repo's roadmap had **4** cases of "doc says missing, code already has it"
# (detection / angle-area / camera IO / golden-template comparison). Manual review does not
# scale, so the check is executable now.
#
# This file is intentionally **pure ASCII**: PowerShell 5.1 reads .ps1 as ANSI unless a BOM is
# present, and non-ASCII string literals get mangled into parser errors. The few Chinese
# markers that must be matched inside docs are written as .NET regex \uXXXX escapes.
#   \u5DF2\u4EA4\u4ED8 = "delivered"   \u5DF2\u5B58\u5728 = "already exists"
#   \u5DF2\u5B9E\u73B0 = "implemented" \u5DF2\u5B8C\u6210 = "done"   \u2705 = white heavy check mark
#   negative markers: \u79FB\u9664 removed, \u7981\u7528 disabled, \u5220\u9664 deleted,
#                     \u5173\u95ED closed, \u4E0D\u505A not doing, \u672A\u505A not done,
#                     \u5F85 pending, \u274C cross mark, \u7F3A missing
#
# Checks (mechanical only, no semantic guessing):
#   A) node classes claimed as delivered/existing in docs must be registered with VFP_REG in the
#      registry file; lines carrying a negative marker are skipped (so "removed" is not a finding).
#   B) repo-relative paths written inside backticks in docs must exist.
#
# Usage:  powershell -ExecutionPolicy Bypass -File tools\doc_check.ps1
# Exit:   0 = ok, 1 = findings (each printed with file:line so it can be fixed directly)
# ============================================================

param(
    [string]$DocsDir = "docs",
    [string]$Registry = "src/NodeRegistry.cpp"
)

$ErrorActionPreference = "Continue"
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$Problems = @()
$claimsChecked = 0
$pathsChecked = 0
$informational = 0

# ---------- A) "delivered" node claims vs registry ----------
$registryText = ""
if (Test-Path $Registry) {
    $registryText = [System.IO.File]::ReadAllText((Join-Path $RepoRoot $Registry))
} else {
    $Problems += "registry file not found: $Registry"
}

$positiveRe = '\u5DF2\u4EA4\u4ED8|\u5DF2\u5B58\u5728|\u5DF2\u5B9E\u73B0|\u5DF2\u5B8C\u6210|\u2705'
$negativeRe = '\u79FB\u9664|\u7981\u7528|\u5220\u9664|\u5173\u95ED|\u4E0B\u7EBF|\u4E0D\u505A|\u672A\u505A|\u5F85|\u274C|\u7F3A'
$nodeRe = '\b([A-Z][A-Za-z0-9_]*Node)\b'

# 豁免名单（每条都要写清"为什么不看注册表"；宁可少报，不要假报）
$nodeSkip = @{
    'HalconNode'     = 'node base class, not a palette entry'
    # 通信节点由 CommunicationManager 按"设备配置"直接实例化（设备管理路径），
    # 不进算子调色板 —— 见 src/CommunicationManager.cpp 的 createNodeForDevice 分支。
    'TcpCommNode'    = 'instantiated by CommunicationManager from device config (not a palette node)'
    'UdpCommNode'    = 'instantiated by CommunicationManager from device config (not a palette node)'
    'SerialCommNode' = 'instantiated by CommunicationManager from device config (not a palette node)'
    'ModbusNode'     = 'instantiated by CommunicationManager from device config (not a palette node)'
    'PlcCommNode'    = 'instantiated by CommunicationManager from device config (not a palette node)'
}

$docFiles = Get-ChildItem (Join-Path $RepoRoot $DocsDir) -Filter *.md -Recurse -ErrorAction SilentlyContinue
foreach ($f in $docFiles) {
    $lines = Get-Content $f.FullName -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        $line = $lines[$i]
        if ($line -notmatch $positiveRe) { continue }
        if ($line -match $negativeRe) { continue }
        foreach ($m in [regex]::Matches($line, $nodeRe)) {
            $token = $m.Groups[1].Value
            if ($nodeSkip.ContainsKey($token)) { continue }
            $claimsChecked++
            if ($registryText -notmatch ("VFP_REG\(\s*" + [regex]::Escape($token) + "\s*,")) {
                $Problems += ("[{0}:{1}] claim says delivered but no VFP_REG for {2} in {3}" -f `
                              $f.Name, ($i + 1), $token, $Registry)
            }
        }
    }
}

# ---------- B) repo-relative paths referenced in docs ----------
# Path chars include CJK (\u4e00-\u9fff) because several docs live under Chinese file names
# (e.g. docs/\u79BB\u7EBF\u53EF\u7528\u6027\u8BF4\u660E.md); an ASCII-only class would silently skip them.
$pathRe = '`([A-Za-z0-9_\u4e00-\u9fff][A-Za-z0-9_./\\\-\u4e00-\u9fff]*\.(?:cpp|h|hpp|ps1|py|md|txt|json|ui|yml|yaml))`'
foreach ($f in $docFiles) {
    $lines = Get-Content $f.FullName -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        foreach ($m in [regex]::Matches($lines[$i], $pathRe)) {
            $rel = $m.Groups[1].Value
            if ($rel -match '[*:<>]') { continue }   # wildcards / absolute paths / placeholders
            # Runtime-output directories: these are produced by the app/deploy script at runtime,
            # never stored in the repo (logs/, data/, dist/, build/, schemes/).
            if ($rel -match '^(logs|data|dist|build|schemes)[\\/]') { continue }
            $pathsChecked++
            # 文档里常省略目录前缀（写 opencv_nodes_test.cpp 而不是 tests/opencv_nodes_test.cpp），
            # 故按"仓库根 + 常见源码目录"多根解析，避免把"没写前缀"误报成"文件不存在"。
            $found = $false
            foreach ($root in @('', 'src', 'include', 'tests', 'tools', 'docs')) {
                $cand = if ($root) { Join-Path (Join-Path $RepoRoot $root) $rel } else { Join-Path $RepoRoot $rel }
                if (Test-Path $cand) { $found = $true; break }
            }
            if (-not $found) {
                # A bare file name (no directory separator) is usually a user/runtime artifact
                # (classes.txt next to the model, 部署说明.txt generated by deploy) -> informational.
                # Only a **directory-prefixed** reference is a claim about a repo path.
                if ($rel -match '[\\/]') {
                    $Problems += ("[{0}:{1}] referenced path does not exist: {2}" -f $f.Name, ($i + 1), $rel)
                } else {
                    $informational++
                }
            }
        }
    }
}

# ---------- result ----------
Write-Host "doc_check: $($docFiles.Count) markdown files under '$DocsDir'"
Write-Host "  A) delivered-node claims checked : $claimsChecked"
Write-Host "  B) path references checked       : $pathsChecked"
Write-Host "     bare names not in repo (info) : $informational  (user/runtime artifacts, not findings)"
if ($Problems.Count -eq 0) {
    Write-Host "  result: PASS (no findings)" -ForegroundColor Green
    exit 0
}
Write-Host "  result: $($Problems.Count) finding(s) - docs and code/filesystem disagree" -ForegroundColor Red
foreach ($p in $Problems) { Write-Host ("   - " + $p) -ForegroundColor Red }
exit 1

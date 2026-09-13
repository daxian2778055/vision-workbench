# ============================================================
# VisionFlowPlatform 部署打包脚本 (Windows / PowerShell)
# ------------------------------------------------------------
# 功能：
#   1. 拷贝 Release exe
#   2. windeployqt 部署 Qt 运行时（DLL + plugins + qml 等）
#   3. 拷贝 HALCON 运行时 DLL（bin/x64-win64）
#   4. 拷贝 MVS 海康相机运行时 DLL
#   5. 创建 data / logs / schemes 目录
#   6. 输出部署说明
#
# 用法（在仓库根目录执行）：
#   powershell -ExecutionPolicy Bypass -File tools\deploy.ps1
#
# 可选参数：
#   -SourceExe  指定源 exe 路径（默认 build\bin\Release\VisionFlowPlatform.exe）
#   -OutDir     指定输出目录（默认 dist\VisionFlowPlatform）
# ============================================================

param(
    [string]$SourceExe = "",
    [string]$OutDir = ""
)

$ErrorActionPreference = "Stop"
$script:step = 0

function Step([string]$msg) {
    $script:step++
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  [$($script:step)] $msg" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Copy-IfExists([string]$src, [string]$dst) {
    if (Test-Path $src) {
        Copy-Item -Path $src -Destination $dst -Recurse -Force
        return $true
    }
    Write-Warning "未找到: $src"
    return $false
}

# ---------- 0. 定位工程根目录 ----------
$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

# ---------- 1. 源 exe ----------
if (-not $SourceExe) {
    $SourceExe = Join-Path $RepoRoot "build\bin\Release\VisionFlowPlatform.exe"
}
if (-not (Test-Path $SourceExe)) {
    Write-Error "找不到 Release exe: $SourceExe`n请先在 VS 中编译 Release 配置，或用 -SourceExe 指定路径。"
    exit 1
}

# ---------- 2. 输出目录 ----------
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "dist\VisionFlowPlatform"
}
$OutDir = (Resolve-Path -LiteralPath $OutDir -ErrorAction SilentlyContinue).Path
if (-not $OutDir) {
    $OutDir = Join-Path $RepoRoot "dist\VisionFlowPlatform"
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "源程序 : $SourceExe" -ForegroundColor Green
Write-Host "输出目录: $OutDir" -ForegroundColor Green

# ---------- 3. Qt 路径 ----------
$QtDir = "D:\Qt\6.11.0\msvc2022_64"
$Windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path $Windeployqt)) {
    Write-Error "找不到 windeployqt: $Windeployqt"
    exit 1
}

# ---------- 4. HALCON 路径 ----------
$HalconRoot = "D:\Program Files\MVTec\HALCON-24.11-Progress-Steady"
$HalconBin = Join-Path $HalconRoot "bin\x64-win64"

# ---------- 5. MVS 运行时路径 ----------
$MvsBin = "C:\Program Files (x86)\Common Files\MVS\Runtime\Win64_x64"

# ---------- 5b. OpenCV 运行时路径 ----------
$OpenCVBin = Join-Path $RepoRoot "thirdparty\opencv\build\x64\vc16\bin"

# ============================================================
# Step 1: 拷贝 exe
# ============================================================
Step "拷贝主程序"
Copy-Item -Path $SourceExe -Destination $OutDir -Force
Write-Host "  -> VisionFlowPlatform.exe" -ForegroundColor Green

# ============================================================
# Step 2: Qt 运行时
# ============================================================
Step "部署 Qt 运行时 (windeployqt)"
& $Windeployqt --release --no-translations --no-system-d3d-compiler `
    --dir $OutDir (Join-Path $OutDir "VisionFlowPlatform.exe")
if ($LASTEXITCODE -ne 0) {
    Write-Warning "windeployqt 返回非零退出码 $LASTEXITCODE（继续处理其它步骤）"
}

# ============================================================
# Step 3: HALCON 运行时
# ============================================================
Step "拷贝 HALCON 运行时 DLL"
if (Test-Path $HalconBin) {
    # 核心运行时 + 算子库全部拷贝，保证 OCR/DataCode2D/DL 等依赖可用
    Copy-Item -Path (Join-Path $HalconBin "*.dll") -Destination $OutDir -Force
    Copy-Item -Path (Join-Path $HalconBin "*.exe") -Destination $OutDir -Force -ErrorAction SilentlyContinue
    $halconDllCount = (Get-ChildItem (Join-Path $HalconBin "*.dll")).Count
    Write-Host "  -> 已拷贝 $halconDllCount 个 HALCON DLL" -ForegroundColor Green
} else {
    Write-Warning "未找到 HALCON 运行时目录: $HalconBin"
}

# HALCON License（若存在则一并拷贝，否则需在目标机配置 License）
Step "拷贝 HALCON License"
$halconLicenseDir = Join-Path $HalconRoot "license"
if (Test-Path $halconLicenseDir) {
    $dstLicense = Join-Path $OutDir "license"
    New-Item -ItemType Directory -Path $dstLicense -Force | Out-Null
    Copy-Item -Path (Join-Path $halconLicenseDir "*") -Destination $dstLicense -Recurse -Force
    Write-Host "  -> license 已拷贝" -ForegroundColor Green
} else {
    Write-Warning "未找到 HALCON license 目录，目标机请配置 HALCON License"
}

# ============================================================
# Step 4: MVS 相机运行时
# ============================================================
Step "拷贝 MVS 相机运行时 DLL"
if (Test-Path $MvsBin) {
    Copy-Item -Path (Join-Path $MvsBin "*.dll") -Destination $OutDir -Force
    $mvsDllCount = (Get-ChildItem $MvsBin -Filter *.dll).Count
    Write-Host "  -> 已拷贝 $mvsDllCount 个 MVS DLL" -ForegroundColor Green
} else {
    Write-Warning "未找到 MVS 运行时目录: $MvsBin（请安装 MVS 客户端或手动配置）"
}

# ============================================================
# Step 5: OpenCV 运行时（OpenCV 节点族依赖）
# ============================================================
Step "拷贝 OpenCV 运行时 DLL"
if (Test-Path $OpenCVBin) {
    Copy-Item -Path (Join-Path $OpenCVBin "opencv_world4130.dll") -Destination $OutDir -Force
    Copy-Item -Path (Join-Path $OpenCVBin "opencv_world4130d.dll") -Destination $OutDir -Force -ErrorAction SilentlyContinue
    Write-Host "  -> opencv_world4130.dll 已拷贝" -ForegroundColor Green
} else {
    Write-Warning "未找到 OpenCV 运行时目录: $OpenCVBin（OpenCV 节点不可用，其余功能不受影响）"
}

# ============================================================
# Step 5.5: 拷贝 tessdata（Tesseract OCR 语言数据）
# ============================================================
$TessdataSrc = Join-Path $RepoRoot "thirdparty\tesseract\tessdata"
if (Test-Path $TessdataSrc) {
    Copy-Item -Path $TessdataSrc -Destination $OutDir -Recurse -Force
    Write-Host "  -> tessdata/ 已拷贝（Tesseract OCR 语言数据）" -ForegroundColor Green
} else {
    Write-Warning "未找到 tessdata 目录（TesseractOCR 节点不可用）"
}

# ============================================================
# Step 6: 拷贝 docs（软件内 F1 查看的操作手册）
# ============================================================
Step "拷贝使用手册"
$docsSrc = Join-Path $RepoRoot "docs"
if (Test-Path $docsSrc) {
    Copy-Item -Path $docsSrc -Destination $OutDir -Recurse -Force
    Write-Host "  -> docs/ 已拷贝（F1 查看手册）" -ForegroundColor Green
} else {
    Write-Warning "未找到 docs 目录（软件内手册不可用）"
}

# ============================================================
# Step 7: 创建数据/日志目录
# ============================================================
Step "创建数据目录结构"
New-Item -ItemType Directory -Path (Join-Path $OutDir "data")  -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutDir "logs")  -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutDir "schemes") -Force | Out-Null
Write-Host "  -> data / logs / schemes 目录已创建" -ForegroundColor Green

# ============================================================
# Step 6: 部署说明
# ============================================================
Step "生成部署说明"
$readme = @"
VisionFlowPlatform 部署说明
===========================

1. 目录结构
   VisionFlowPlatform.exe  主程序
   data/                   数据库 (visionflow.db) 与运行数据
   logs/                   运行日志与崩溃日志
   schemes/                方案文件推荐存放目录
   *.dll                   依赖库 (Qt / HALCON / MVS / OpenCV)

2. 目标机环境要求
   - Windows 10/11 x64
   - 若使用 MVS 相机：目标机需安装"机器视觉工业相机客户端 MVS"，
     或至少具备 MVS 运行时 (MvCameraControl.dll 等，脚本已随包拷贝)。
   - 若使用 HALCON 深度学习/DL 算子：
        a. 目标机需安装 HALCON Runtime 或保证 halconcpp.dll 等与本包一致;
        b. 必须配置 HALCON License（授权码或 License 服务器），
           否则 HALCON 算子（含 DL）无法运行。
   - 相机驱动：GigE 相机建议安装 MVS 附带的网卡驱动优化工具。

3. 首次运行
   - 双击 VisionFlowPlatform.exe 即可。
   - 首次启动需创建初始管理员账号（用户名 + 密码至少 6 位，无默认口令）。

4. 常见问题
   - 提示缺少 DLL: 用 Dependencies / Process Explorer 查看缺失项，确认是 Qt、
     HALCON、OpenCV 还是 MVS 组件。
   - 相机枚举不到: 检查 MVS 客户端能否枚举，确认 GigE 网卡 IP 与相机同网段。
   - OpenCV 节点不可用: 确认 opencv_world451.dll 已随包拷贝（脚本自动处理）。
"@
Set-Content -Path (Join-Path $OutDir "部署说明.txt") -Value $readme -Encoding UTF8
Write-Host "  -> 部署说明.txt" -ForegroundColor Green

# ============================================================
# 完成
# ============================================================
Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "  部署完成!" -ForegroundColor Green
Write-Host "  输出目录: $OutDir" -ForegroundColor Green
Write-Host "  可压缩为 zip 分发至目标机" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

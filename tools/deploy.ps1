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
    [string]$OutDir = "",
    [switch]$SkipVerify      # 跳过部署后自检（默认执行：离线可用性校验）
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
$OutDirRequested = -not [string]::IsNullOrWhiteSpace($OutDir)
if (-not $OutDirRequested) {
    $OutDir = Join-Path $RepoRoot "dist\VisionFlowPlatform"
}
# 原来这里是 $OutDir = (Resolve-Path ...).Path：目录还不存在时 Resolve-Path 返回 Nothing，
# .Path 就是空，紧跟的那句回落于是把调用方指定的落点整个丢掉，包永远落回默认目录（#522，
# 上一次发版就是这么覆盖到旧包上的）。现在存在就取规范化结果，不存在就保留原样给 New-Item 建。
$resolvedOutDir = Resolve-Path -LiteralPath $OutDir -ErrorAction SilentlyContinue
if ($resolvedOutDir) { $OutDir = $resolvedOutDir.Path }
$outDirPreExisted = Test-Path -LiteralPath $OutDir
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "源程序 : $SourceExe" -ForegroundColor Green
Write-Host "输出目录: $OutDir" -ForegroundColor Green
$outDirSourceWord = if ($OutDirRequested) { "调用方指定" } else { "脚本缺省" }
$outDirExistWord = if ($outDirPreExisted) { "是（本次是往已有包里覆盖写入）" } else { "否（本次新建）" }
Write-Host ("落点来源: {0}   目录本来已存在: {1}" -f $outDirSourceWord, $outDirExistWord) -ForegroundColor Green

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

# ---------- 2b. 本仓自己的翻译（windeployqt 那句 --no-translations 只管 Qt 自带的） ----------
# 为什么必须有一步：src/I18n.cpp 里 .qm 缺失的设计是"降级为源文（中文），不阻断启动"，
# 也就是说包里没有翻译文件时界面不会报错，英文现场只会安静地看到中文。
# 上一遍这个文件是人工补进包里的——凡靠人记住的修法都会在下一次回潮，所以落到脚本。
Step "拷贝本仓翻译 (*.qm)"
$QmSrcDir = Join-Path (Split-Path -Parent $SourceExe) "translations"
if (Test-Path -LiteralPath $QmSrcDir) {
    $dstQm = Join-Path $OutDir "translations"
    New-Item -ItemType Directory -Path $dstQm -Force | Out-Null
    Copy-Item -Path (Join-Path $QmSrcDir "*.qm") -Destination $dstQm -Force -ErrorAction SilentlyContinue
    $qmCount = @(Get-ChildItem -Path $dstQm -Filter *.qm -ErrorAction SilentlyContinue).Count
    Write-Host "  -> translations/ 里现有 $qmCount 枚 .qm" -ForegroundColor Green
    if ($qmCount -eq 0) {
        Write-Warning "$QmSrcDir 存在但里面没有 .qm —— 界面会静默退化成中文源文（先跑翻译构建）"
    }
} else {
    Write-Warning "找不到 $QmSrcDir —— 包内没有翻译，英文界面会静默显示中文（I18n 是降级不是报错）"
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
# 单一来源：拷贝、自检清单、部署说明正文都引用这个变量——
# 曾经正文写死 opencv_world451.dll 而实际拷的是 4130，现场照 FAQ 找会扑空。
$OpenCvDllName = "opencv_world4130.dll"
if (Test-Path $OpenCVBin) {
    Copy-Item -Path (Join-Path $OpenCVBin $OpenCvDllName) -Destination $OutDir -Force
    Copy-Item -Path (Join-Path $OpenCVBin "opencv_world4130d.dll") -Destination $OutDir -Force -ErrorAction SilentlyContinue
    Write-Host "  -> $OpenCvDllName 已拷贝" -ForegroundColor Green
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
# Step 5.8: MSVC 运行时（CRT）
# ============================================================
# 为什么必须拷：主程序导入 VCRUNTIME140.dll / VCRUNTIME140_1.dll / MSVCP140.dll，
# 干净目标机（未安装 VC++ 2015-2022 x64 可再发行包）会直接"缺少 DLL"打不开。
# windeployqt 只有在设置了 VCINSTALLDIR（装了 Visual Studio）时才会自动处理，否则它会跳过
# 并以非零码返回（本机实测即如此），所以这里显式兜一层。
Step "拷贝 MSVC 运行时 (CRT)"
$crtNames = @('vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll',
              'msvcp140_2.dll', 'concrt140.dll', 'vccorlib140.dll')
$crtSrcDirs = @()
foreach ($root in @('C:\Program Files\Microsoft Visual Studio', 'C:\Program Files (x86)\Microsoft Visual Studio')) {
    $found = Get-ChildItem (Join-Path $root '*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue |
             Select-Object -ExpandProperty FullName
    if ($found) { $crtSrcDirs += $found }
}
$crtSrcDirs += 'C:\Windows\System32'   # 兜底：本机已安装的运行时（VS 未安装时唯一的本地来源）
$script:crtMissing = @()
$crtCopied = 0
foreach ($n in $crtNames) {
    $copied = $false
    foreach ($dir in $crtSrcDirs) {
        $src = Join-Path $dir $n
        if (Test-Path $src) { Copy-Item $src -Destination $OutDir -Force; $crtCopied++; $copied = $true; break }
    }
    if (-not $copied) { $script:crtMissing += $n }
}
Write-Host "  -> 已拷贝 $crtCopied 个 CRT DLL" -ForegroundColor Green
if ($script:crtMissing.Count -gt 0) {
    Write-Warning ("本机找不到这些 CRT（目标机需安装 VC++ 2015-2022 x64 可再发行包）: " + ($script:crtMissing -join ', '))
}

# ============================================================
# Step 5.9: 补齐 Qt 运行时 DLL（按主程序导入名扫描）
# ============================================================
# 为什么需要：windeployqt 会漏掉部分 Qt 模块 DLL —— 实测主程序已链接 QtCharts（统计报表依赖
# Qt6Charts.dll），但 windeployqt 没有拷贝它，部署自检直接报"缺文件: Qt6Charts.dll"，
# 到目标机就是"报表打不开"。这里按 exe 二进制里**实际引用**的 Qt6*.dll 名字逐个补，
# 未来新增 Qt 模块也不会再漏。
Step "补齐 Qt 运行时 DLL"
$qtBinDir = ""
foreach ($cand in @('D:\Qt\6.11.0\msvc2022_64\bin', 'C:\Qt\6.11.0\msvc2022_64\bin')) {
    if (Test-Path $cand) { $qtBinDir = $cand; break }
}
if (-not $qtBinDir) {
    foreach ($drive in @('C:\', 'D:\', 'E:\')) {
        $root = Join-Path $drive 'Qt'
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like '6.*' } |
            ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
            Where-Object { Test-Path (Join-Path $_.FullName 'bin\Qt6Core.dll') } |
            Select-Object -First 1
        if ($hit) { $qtBinDir = Join-Path $hit.FullName 'bin'; break }
    }
}
$qtDllAdded = 0
if ($qtBinDir) {
    $exeText = [System.Text.Encoding]::ASCII.GetString(
        [System.IO.File]::ReadAllBytes((Join-Path $OutDir 'VisionFlowPlatform.exe')))
    foreach ($m in [regex]::Matches($exeText, 'Qt6[A-Za-z0-9_]+\.dll')) {
        $name = $m.Value
        if (Test-Path (Join-Path $OutDir $name)) { continue }
        $src = Join-Path $qtBinDir $name
        if (Test-Path $src) {
            Copy-Item $src -Destination $OutDir -Force
            $qtDllAdded++
            Write-Host ("  补: " + $name) -ForegroundColor Yellow
        } else {
            Write-Warning ("主程序引用了 $name，但在 $qtBinDir 中找不到")
        }
    }
    Write-Host "  -> 补齐 $qtDllAdded 个 Qt DLL" -ForegroundColor Green
} else {
    Write-Warning "未找到 Qt bin 目录，跳过 Qt DLL 补齐（部署自检会报出缺失项）"
}

# ============================================================
# Step 6: 拷贝 docs（白名单：仅对外文档）
# ============================================================
# 为什么是白名单而不是整目录拷贝：2026-10-23 实测，整目录拷贝把
# 对标差距推进计划（内部台账）、登记附表、客户复核与切片C拍板回执、code-review-process 等
# 内部件一并带进了客户包——现场客户能看到内部审计与复核记录。
# F1 只依赖 用户操作手册.md（ManualPathResolver 候选链 + qrc 兜底），
# 因此其余文档显式列出：不在此表内的一律不随包。
$docsToShip = @('用户操作手册.md', '离线可用性说明.md', '深度学习集成指南.md', '统计报表说明.md')
Step "拷贝对外文档（白名单 $($docsToShip.Count) 份）"
$dstDocs = Join-Path $OutDir "docs"
New-Item -ItemType Directory -Path $dstDocs -Force | Out-Null
# deploy 不清目录：上一次部署遗留的非白名单文档会一直躺在包里，先清后拷
$staleDocs = @(Get-ChildItem -LiteralPath $dstDocs -File -ErrorAction SilentlyContinue |
    Where-Object { $docsToShip -notcontains $_.Name })
foreach ($f in $staleDocs) { Remove-Item -LiteralPath $f.FullName -Force }
$docsCopied = 0
foreach ($docName in $docsToShip) {
    $docSrc = Join-Path $RepoRoot "docs\$docName"
    if (Test-Path -LiteralPath $docSrc) {
        Copy-Item -LiteralPath $docSrc -Destination $dstDocs -Force
        $docsCopied++
    } else {
        Write-Warning "白名单文档缺失: docs/$docName（目标机上看不到这份文档）"
    }
}
Write-Host ("  -> docs/ 现有 {0} 份对外文档；清理上轮遗留 {1} 份" -f $docsCopied, $staleDocs.Count) -ForegroundColor Green

# ---------- train-env folder (new-machine prep for the TRAIN machine) ----------
# Why it ships in the customer package: the train machine is ours to set up, and a
# README + install script + pre-downloaded wheels means it can be prepared offline
# (factory networks usually have no internet). Production machines never need it.
$trainEnvSrc = Join-Path $RepoRoot "deploy-extras\train-env"
$trainEnvDst = Join-Path $OutDir "train-env"
if (Test-Path -LiteralPath $trainEnvSrc) {
    if (Test-Path -LiteralPath $trainEnvDst) { Remove-Item -LiteralPath $trainEnvDst -Recurse -Force }
    Copy-Item -LiteralPath $trainEnvSrc -Destination $trainEnvDst -Recurse -Force
    $wheelCount = @(Get-ChildItem -LiteralPath $trainEnvDst -Filter *.whl -Recurse -ErrorAction SilentlyContinue).Count
    $wheelMb = 0
    $whl = @(Get-ChildItem -LiteralPath $trainEnvDst -Filter *.whl -Recurse -ErrorAction SilentlyContinue)
    if ($whl.Count -gt 0) { $wheelMb = [math]::Round((($whl | Measure-Object Length -Sum).Sum / 1MB), 0) }
    Write-Host ("  -> train-env/ shipped ({0} wheels, {1} MB)" -f $wheelCount, $wheelMb) -ForegroundColor Green
} else {
    Write-Warning "deploy-extras/train-env missing - the train machine will have no offline prep assets"
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
   - 首次启动会播种出厂管理员 admin / admin；登录后请立刻在「系统 → 修改管理员口令」改掉。
     改密前画布编辑与方案保存会被写闸挡住（这是防默认口令被滥用，不是故障）。

4. 常见问题
   - 提示缺少 DLL: 用 Dependencies / Process Explorer 查看缺失项，确认是 Qt、
     HALCON、OpenCV 还是 MVS 组件。
   - 相机枚举不到: 检查 MVS 客户端能否枚举，确认 GigE 网卡 IP 与相机同网段。
   - OpenCV 节点不可用: 确认 $OpenCvDllName 已随包拷贝（脚本自动处理）。
"@
Set-Content -Path (Join-Path $OutDir "部署说明.txt") -Value $readme -Encoding UTF8
Write-Host "  -> 部署说明.txt" -ForegroundColor Green

# ============================================================
# Step 9: 部署自检（离线可用性）
# ============================================================
# 目的：把"包能不能在目标机离线跑起来"变成可执行校验，而不是靠人工双击碰运气。
# 判据三项：关键文件齐 → 主程序导入的 CRT 都在包里 → 从**部署目录本体**跑节点自检 0 失败。
$problems = @()
if (-not $SkipVerify) {
    Step "部署自检（离线可用性）"

    # 9.1 关键文件
    # Qt6Charts.dll: 统计报表（P1-11）用 Qt Charts 画趋势/分布图，缺它则报表打不开
    $mustHave = @('VisionFlowPlatform.exe', 'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll',
                  'Qt6Core.dll', 'Qt6Sql.dll', 'Qt6Widgets.dll', 'Qt6Charts.dll', $OpenCvDllName,
                  'halconcpp.dll', 'MvCameraControl.dll', 'vcruntime140.dll', 'msvcp140.dll',
                  'tessdata', 'license', 'data', 'logs', 'schemes')
    foreach ($n in $mustHave) {
        if (-not (Test-Path (Join-Path $OutDir $n))) { $problems += "缺文件: $n" }
    }
    Write-Host ("  关键文件检查: {0}/{1} 就位" -f ($mustHave.Count - $problems.Count), $mustHave.Count)

    # 9.2 CRT 完备性：按"二进制里是否引用了该 DLL"判断，避免"看起来拷了、其实少一个"
    $scanTargets = @((Join-Path $OutDir 'VisionFlowPlatform.exe'))
    $scanTargets += (Get-ChildItem (Join-Path $OutDir '*.dll') -ErrorAction SilentlyContinue |
                     Where-Object { $_.Length -lt 25MB } | Select-Object -ExpandProperty FullName)
    $referenced = @{}
    foreach ($f in $scanTargets) {
        try {
            # 注意：PE 导入表里的名字大小写不固定（实测是 VCRUNTIME140.dll），Contains 区分大小写，
            # 故统一转小写比较——否则会得到"0 个被引用"这种看着通过、其实没检查的假绿。
            $txt = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($f)).ToLowerInvariant()
        } catch { continue }
        foreach ($n in $crtNames) {
            if ($txt.Contains($n)) { $referenced[$n] = $true }
        }
    }
    $missCrt = @()
    foreach ($n in $referenced.Keys) {
        if (-not (Test-Path (Join-Path $OutDir $n))) { $missCrt += $n }
    }
    if ($missCrt.Count -gt 0) {
        $problems += ("被引用但缺 CRT: " + ($missCrt -join ', '))
    } else {
        Write-Host ("  CRT 检查: 被引用的 {0} 个 CRT 全部在包内" -f $referenced.Count)
    }

    # 9.3 从部署目录本体跑节点自检（不设 QT_PLUGIN_PATH，模拟目标机双击）
    # 用 ProcessStartInfo 而不是 Start-Process -PassThru：后者实测取回 ExitCode 为空值，
    # 会把"自检通过"误报成失败（$null -ne 0 恒真）。
    $savedPluginPath = $env:QT_PLUGIN_PATH
    $env:QT_PLUGIN_PATH = $null
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $OutDir 'VisionFlowPlatform.exe'
    $psi.Arguments = '--selftest-nodes'
    $psi.WorkingDirectory = $OutDir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardOutput = $true
    $p = [System.Diagnostics.Process]::Start($psi)
    $errTask = $p.StandardError.ReadToEndAsync()
    $outTask = $p.StandardOutput.ReadToEndAsync()
    # 10 分钟（原 5 分钟）：2026-10-25 实测在同一台机器上自检耗时 3s / 12s / >5min 都可能出现
    #（杀进程残留、杀毒扫描、MVS 服务状态都会拉长启动），5 分钟会偶发误报"自检超时"。
    if (-not $p.WaitForExit(600000)) {
        $p.Kill()
        $problems += '节点自检超时（>10 分钟）'
        # 超时必须把自检的中间输出打出来（2026-10-25 复核 P3-5）：NodeSelfTest 已逐节点
        # fflush，但这里用 ReadToEndAsync 缓冲，此前只记一句"超时"，现场根本看不到卡在哪个
        # 节点——那条信息正是排障唯一有用的东西。
        # .Result 必须带超时（2026-10-25 复核 P3）：若脚本子进程继承了 stdout 句柄，
        # Kill() 之后流不会关闭，直接读 .Result 会把部署脚本永久挂住——排障代码反倒成了新故障点。
        $partial = ''
        try {
            if ($outTask.Wait(5000)) {
                $partial = $outTask.Result
            } else {
                $partial = '(自检输出读取超时 5 秒：可能有子进程继承了 stdout 句柄)'
            }
        } catch {
            $partial = ''
        }
        if ($partial) {
            $tail = ($partial -split "`r?`n") | Where-Object { $_ -ne '' } | Select-Object -Last 12
            Write-Host '  超时前最后输出（最后一行 START 即卡住的节点；只有构造/初始化阶段卡住时才轮到它下面那个）:'
            foreach ($line in $tail) { Write-Host ("    " + $line) }
        } else {
            Write-Host '  超时前无任何输出（多半卡在启动/单例初始化，不在节点循环里）'
        }
        try {
            if ($errTask.Wait(2000) -and $errTask.Result) {
                $errTail = ($errTask.Result -split "`r?`n") | Where-Object { $_ -ne '' } | Select-Object -Last 6
                Write-Host '  stderr 尾部:'
                foreach ($line in $errTail) { Write-Host ("    " + $line) }
            }
        } catch { }
    } else {
        Write-Host ("  自检退出码: " + $p.ExitCode)
        if ($p.ExitCode -ne 0) { $problems += "节点自检未通过（退出码 $($p.ExitCode)）" }
    }
    $errText = $errTask.Result
    if ($errText -match 'QSQLITE') {
        # 部署目录内应能加载 Qt SQL 插件（数据库落库功能的前提）
        $problems += 'Qt SQL 驱动未加载（stderr 出现 QSQLITE 告警 -> 数据库功能不可用）'
    }
    $env:QT_PLUGIN_PATH = $savedPluginPath
    $rep = Join-Path $OutDir 'selftest_nodes_report.txt'
    if (Test-Path $rep) {
        $sumLine = (Get-Content $rep -Encoding UTF8 | Select-String -Pattern '汇总' | Select-Object -Last 1)
        Write-Host ("  " + ($sumLine.Line))
        if ($sumLine.Line -notmatch '0 FAIL') { $problems += '节点自检存在失败项' }
    } else {
        $problems += '未生成自检报告'
    }

    # 9.4 docs 白名单：内部台账/复核件不得随客户包分发（2026-10-23 实测泄漏，白名单见 Step 6）
    $leakedDocs = @(Get-ChildItem -LiteralPath (Join-Path $OutDir 'docs') -File -ErrorAction SilentlyContinue |
        Where-Object { $docsToShip -notcontains $_.Name } | ForEach-Object { $_.Name })
    if ($leakedDocs.Count -gt 0) {
        $problems += ("docs 白名单外文件（内部件不得随客户包分发）: " + ($leakedDocs -join ', '))
    } else {
        Write-Host ("  docs 白名单: 干净（{0} 份）" -f $docsToShip.Count)
    }
}

# ============================================================
# 完成
# ============================================================
Write-Host ""
if ($problems.Count -gt 0) {
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  部署完成，但自检发现问题（离线可用性存疑）:" -ForegroundColor Red
    foreach ($p in $problems) { Write-Host ("   - " + $p) -ForegroundColor Red }
    Write-Host "  输出目录: $OutDir" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    exit 1
}
# ---------- 包内自识别：这份包对应哪一枚提交、哪份 exe ----------
Step "写 版本说明.txt（提交 / exe 指纹 / 工作树状态）"
$exeInDst = Join-Path $OutDir "VisionFlowPlatform.exe"
$gitRev = "unknown"; $gitTag = "none"; $gitBranch = "unknown"; $worktreeDirty = "unknown"
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitRev = ((& git -C $RepoRoot rev-parse HEAD 2>$null) | Select-Object -First 1)
    $gitBranch = ((& git -C $RepoRoot rev-parse --abbrev-ref HEAD 2>$null) | Select-Object -First 1)
    # 用 `git tag --points-at`（无 tag 时输出空且退出码 0），不要用 `describe --exact-match`：
    # 后者在未打 tag 的 HEAD 上退出非零并把 fatal: 写到 stderr，在 $ErrorActionPreference='Stop'
    # 下会把脚本掀翻——表现恰恰是"非发版构建不生成版本说明.txt"，而那正是最需要自识别的场合。
    $gitTag = ((& git -C $RepoRoot tag --points-at $gitRev 2>$null) | Select-Object -First 1)
    $dirtyLines = @(& git -C $RepoRoot status --porcelain 2>$null)
    $worktreeDirty = $dirtyLines.Count
    if (-not $gitTag) { $gitTag = "none" }
    if ($worktreeDirty -gt 0) {
        Write-Warning ("工作树有 {0} 处未提交改动 —— 这份包不对应任何一枚提交" -f $worktreeDirty)
    }
} else {
    Write-Warning "本机找不到 git —— 包内无法记录提交号，只能靠人记（这是发版最易丢的一格）"
}

# exe 内嵌版本完整性：CMake 把配置期的 git 短哈希编进了 exe（cmake/Version.h.in，关于框可见）。
# 它能抓一类静默错配：提交了工具/文档却没重新构建就部署——此时 manifest 声称的提交与
# 实际编出来的东西不符（现场无法复现问题的经典成因）。短哈希取 exe 二进制里的字面量；
# 找不到（如外部传入的 exe）只警告，不阻断。
$embeddedHash = ""
if ($gitRev -and $gitRev -ne "unknown" -and $gitRev.Length -ge 7) {
    $shortRev = $gitRev.Substring(0, 7)
    try {
        # 版本串经 QStringLiteral 进入二进制，是 UTF-16LE；同时扫 ASCII 兜底（其它引入路径）。
        # 只用"逐字节等值"的 Latin1 视图做 Contains，避免按 UTF-8 解码把二进制搅成乱码。
        # 注意：Windows PowerShell 5.1 跑在 .NET Framework 上没有 Encoding.Latin1（那是 .NET Core 才有），
        # 用代码页 28591（iso-8859-1）显式取，5.1 与 7.x 都可用。
        $latin1 = [System.Text.Encoding]::GetEncoding(28591)
        $bytesAll = [System.IO.File]::ReadAllBytes($exeInDst)
        $latView = $latin1.GetString($bytesAll)
        $needleAscii = $latin1.GetString([System.Text.Encoding]::ASCII.GetBytes($shortRev))
        $needleUtf16 = $latin1.GetString([System.Text.Encoding]::Unicode.GetBytes($shortRev))
        if ($latView.Contains($needleAscii) -or $latView.Contains($needleUtf16)) { $embeddedHash = $shortRev }
    } catch {
        Write-Warning "无法读取 exe 以核对内嵌版本哈希: $_"
    }
}
if ($gitRev -and $gitRev -ne "unknown") {
    if ($embeddedHash) {
        Write-Host ("  exe 内嵌版本哈希 = {0}，与 HEAD 一致" -f $embeddedHash) -ForegroundColor Green
    } else {
        Write-Warning (("exe 内嵌版本哈希未匹配 HEAD {0} —— 这份 exe 很可能不是当前 HEAD 编出来的；" +
                        "若刚提交过未重新构建，请先 cmake --build 再部署，否则 manifest 的提交号不可信") `
                       -f $gitRev.Substring(0, 7))
    }
}
# 不要用 Get-FileHash：受限/精简 PowerShell（如 CI 宿主、PSModulePath 被裁剪的子进程）里
# 该 cmdlet 可能"未识别"，在 $ErrorActionPreference='Stop' 下会把脚本掀翻在写 manifest 之前
# ——包看着部署成功、实际少一件且退出码没人查。这里走 .NET，零 cmdlet 依赖。
function Get-FileMd5Hex([string]$path) {
    $md5 = [System.Security.Cryptography.MD5]::Create()
    try {
        $fs = [System.IO.File]::OpenRead($path)
        try {
            $bytes = $md5.ComputeHash($fs)
            return (($bytes | ForEach-Object { $_.ToString('x2') }) -join '')
        } finally { $fs.Dispose() }
    } finally { $md5.Dispose() }
}
$exeHash = Get-FileMd5Hex $exeInDst
$exeBytes = (Get-Item -LiteralPath $exeInDst).Length

# manifest 的"提交"= 打包时的 HEAD；另记一行"exe 是否内嵌该 HEAD 的版本哈希"。
# 为什么不是反过来记 exe 的提交：从二进制里反解 exe 的真实提交需要额外基础设施（版本旁挂文件），
# 而"提交了却没重新构建就打包"这类错配必须先被看见——所以用"是否匹配"把它变成显式读数：
# 匹配=是，这份 exe 就是 HEAD 编的；匹配=否，manifest 的提交号只说明打包环境，不代表 exe 来源，
# 此时上面的告警已要求先重新构建（发版前必须为"是"）。
$exeEmbedsHead = "unknown"
if ($embeddedHash) { $exeEmbedsHead = "yes" } elseif ($gitRev -and $gitRev -ne "unknown") { $exeEmbedsHead = "no" }

$manifestLines = @(
    "VisionFlowPlatform 包内自识别（本文件由 tools/deploy.ps1 生成，请勿手改）",
    ("打包时间     : {0}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss")),
    ("提交         : {0}" -f $gitRev),
    ("分支         : {0}" -f $gitBranch),
    ("该提交上的标签: {0}" -f $gitTag),
    ("工作树未提交改动条数: {0}" -f $worktreeDirty),
    ("exe 内嵌 HEAD 版本哈希: {0}" -f $exeEmbedsHead),
    ("VisionFlowPlatform.exe md5 : {0}" -f $exeHash),
    ("VisionFlowPlatform.exe 字节 : {0}" -f $exeBytes),
    "",
# (packager note, NOT part of the shipped text) if this package flows outside the customer,
# replace the credential lines above with "initial password: see delivery sheet" so the
# factory password is not printed in a delivered document.
    "默认账号/口令（首次启动自动播种）：admin / admin",
    "  ⚠ 首次登录后请立刻在「系统 → 修改管理员口令」改掉默认口令。",
    "  在改密之前，画布编辑/方案保存/通信配置/用户增删等写操作会被「写操作闸」拦住——",
    "  这不是故障，是防默认口令被滥用；改密后自动解除。",
    "  交付客户时请由实施人员在装机现场改密并记入交付单（默认口令仅供首次装机）。",
    "  角色：Admin（全部功能）；Operator 由 Admin 在「系统 → 用户管理」里创建。",

    "",
    "读法：提交=打包时的 HEAD；exe 内嵌 HEAD 版本哈希=yes 表示这份 exe 就是该提交编出来的",
    "      （no = 打包前没重新构建，manifest 的提交号不代表 exe 来源，发版件必须为 yes）；",
    "      工作树未提交改动条数为 0 时，打包环境本身是干净的。",
    "      要核对包里的 exe 就是刚测过的那份，比对上面这行 md5 与门禁面记录的那行。",
    "      机器校验：python tools/release_manifest_check.py --repo . --expect-tag <tag>"
)
[System.IO.File]::WriteAllText((Join-Path $OutDir "版本说明.txt"), `
    ($manifestLines -join [Environment]::NewLine), (New-Object System.Text.UTF8Encoding($true)))
Write-Host ("  -> 提交 {0}  标签 {1}  exe_md5 {2}  工作树未提交 {3}  exe内嵌HEAD版本 {4}" -f `
    $gitRev, $gitTag, $exeHash, $worktreeDirty, $exeEmbedsHead) -ForegroundColor Green

Write-Host "========================================" -ForegroundColor Green
Write-Host "  部署完成!" -ForegroundColor Green
Write-Host "  输出目录: $OutDir" -ForegroundColor Green
Write-Host "  可压缩为 zip 分发至目标机" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

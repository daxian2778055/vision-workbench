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
                  'Qt6Core.dll', 'Qt6Sql.dll', 'Qt6Widgets.dll', 'Qt6Charts.dll', 'opencv_world4130.dll',
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
    if (-not $p.WaitForExit(300000)) {
        $p.Kill()
        $problems += '节点自检超时（>5 分钟）'
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
Write-Host "========================================" -ForegroundColor Green
Write-Host "  部署完成!" -ForegroundColor Green
Write-Host "  输出目录: $OutDir" -ForegroundColor Green
Write-Host "  可压缩为 zip 分发至目标机" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

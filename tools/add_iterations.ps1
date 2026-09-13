# 批量给 6 个 Halcon 形态学节点加 iterations 参数
$ErrorActionPreference = "Stop"
$ops = @{
  "ErodeNode"       = "GrayErosionRect"
  "DilateNode"      = "GrayDilationRect"
  "OpenNode"        = "GrayOpeningRect"
  "CloseNode"       = "GrayClosingRect"
  "TopHatNode"      = "GrayTophatRect"
  "BottomHatNode"   = "GrayBottomhatRect"
}
$root = "E:\halcon\2\xin1\src"
foreach ($k in $ops.Keys) {
  $p = Join-Path $root "$k.cpp"
  $t = [System.IO.File]::ReadAllText($p)
  $op = $ops[$k]
  # 1) init 加 iterations 参数（在 maskHeight 参数之后）
  $anchor = 'QStringLiteral("maskHeight"), 5, 1, 200,'
  $ins = "`n        makeIntParam(QStringLiteral(`"iterations`"), 1, 1, 20,`n                     QStringLiteral(`"迭代次数`")),"
  if ($t.Contains($anchor) -and -not $t.Contains("iterations")) {
    $t = [System.String]::Replace($t, $anchor, $anchor + $ins)
  }
  # 2) run 调用包 for 循环
  $oldCall = "$op(m_inputImage, &result, mh, mw);"
  $newCall = "HObject tmp = m_inputImage;`n        const int iters = m_params.value(QStringLiteral(`"iterations`"), 1).toInt();`n        HObject result;`n        for (int i = 0; i < iters; ++i) {`n            $op(tmp, &result, mh, mw);`n            tmp = result;`n        }"
  if ($t.Contains($oldCall)) {
    $t = [System.String]::Replace($t, $oldCall, $newCall)
  }
  [System.IO.File]::WriteAllText($p, $t)
  Write-Host "processed $k"
}

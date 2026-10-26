#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""统计 src/include 里"原因键"的写侧站点数（NodeResultFields.h 的注释引用本脚本输出）。

为什么要有这个脚本：NodeResultFields.h 的注释里写着"共 N 处：lastError X / *Note Y /
trainStatus Z"，早先是在临时会话里数的、声明"一次性不入库"——那种数值没人能复现。
现在统计逻辑在仓里，注释里的四个数一律以本脚本输出为准。

用法：
    python tools/reason_key_sites.py          # 打印计数表
    python tools/reason_key_sites.py --check  # 与头文件注释比对四个数（不一致/找不到声明 → 退出 1）

--check 的契约是 **fail-closed**（2026-10-26 复核）：
  * 找不到计数声明 → 退出 1（早先打印"跳过"并返回 0，等于静默放行）；
  * 四个数（合计 / lastError / *Note / trainStatus）逐个比对，不再只看前两个
    （夹具实测过：头文件把 *Note 写成 99 也能 CHECK OK）。
"""
from __future__ import annotations

import glob
import io
import re
import sys

HEADER = "include/NodeResultFields.h"

PAT_LAST = re.compile(r'm_params\[QStringLiteral\("lastError"\)\]\s*=')
PAT_NOTE = re.compile(r'm_params\[QStringLiteral\("(\w*Note)"\)\]\s*=')
PAT_TRAIN = re.compile(r'm_params\[QStringLiteral\("trainStatus"\)\]\s*=')
# 头文件里的四元组：共 N 处）：lastError X / *Note Y / trainStatus Z
PAT_CLAIM = re.compile(
    r"共\s*(\d+)\s*处）：lastError\s*(\d+)\s*/\s*\*Note\s*(\d+)\s*/\s*trainStatus\s*(\d+)")


def count() -> dict:
    last = 0
    note = 0
    train = 0
    per_note: dict[str, int] = {}
    for p in sorted(glob.glob("src/*.cpp") + glob.glob("include/*.h")):
        s = io.open(p, encoding="utf-8").read()
        last += len(PAT_LAST.findall(s))
        train += len(PAT_TRAIN.findall(s))
        for m in PAT_NOTE.finditer(s):
            note += 1
            key = m.group(1)
            per_note[key] = per_note.get(key, 0) + 1
    return {"lastError": last, "note": note, "trainStatus": train,
            "total": last + note + train, "per_note": per_note}


def main() -> int:
    c = count()
    sys.stdout.write("lastError=%d  *Note=%d  trainStatus=%d  合计=%d\n"
                     % (c["lastError"], c["note"], c["trainStatus"], c["total"]))
    for k, v in sorted(c["per_note"].items()):
        sys.stdout.write("  %s=%d\n" % (k, v))

    if "--check" not in sys.argv:
        return 0

    text = io.open(HEADER, encoding="utf-8").read()
    m = PAT_CLAIM.search(text)
    if not m:
        # fail-closed：声明没了就没人知道该对到哪，必须判红
        sys.stdout.write("CHECK FAIL: %s 里找不到计数声明（共 N 处）：lastError X / *Note Y / "
                         "trainStatus Z），无法核对——按 fail-closed 判红\n" % HEADER)
        return 1

    want = {
        "total": int(m.group(1)),
        "lastError": int(m.group(2)),
        "note": int(m.group(3)),
        "trainStatus": int(m.group(4)),
    }
    bad = [k for k in ("total", "lastError", "note", "trainStatus") if want[k] != c[k]]
    if bad:
        for k in bad:
            sys.stdout.write("CHECK FAIL: %s 声明 %d，实测 %d\n" % (k, want[k], c[k]))
        return 1
    sys.stdout.write("CHECK OK: 四个数与声明一致\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

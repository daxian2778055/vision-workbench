#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""统计 src/include 里"原因键"的写侧站点数（NodeResultFields.h 的注释引用本脚本输出）。

为什么要有这个脚本：NodeResultFields.h 的注释里写着"共 N 处：lastError X / ..."，
早先是在临时会话里数的、语句说"一次性不入库"——那种声明的数值没人能复现。现在把统计
逻辑落进仓，注释里的数字一律以本脚本输出为准。

用法：
    python tools/reason_key_sites.py          # 打印计数表
    python tools/reason_key_sites.py --check  # 与 NodeResultFields.h 注释里的数字比对（不一致退出 1）
"""
from __future__ import annotations

import glob
import io
import re
import sys

ROOT = "."
HEADER = "include/NodeResultFields.h"

PAT_LAST = re.compile(r'm_params\[QStringLiteral\("lastError"\)\]\s*=')
PAT_NOTE = re.compile(r'm_params\[QStringLiteral\("(\w*Note)"\)\]\s*=')
PAT_TRAIN = re.compile(r'm_params\[QStringLiteral\("trainStatus"\)\]\s*=')


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

    if "--check" in sys.argv:
        text = io.open(HEADER, encoding="utf-8").read()
        m = re.search(r"共 (\d+) 处）：lastError (\d+)", text)
        if not m:
            sys.stdout.write("CHECK: 头文件里找不到计数声明，跳过\n")
            return 0
        want_total, want_last = int(m.group(1)), int(m.group(2))
        if want_total != c["total"] or want_last != c["lastError"]:
            sys.stdout.write("CHECK FAIL: 注释写的是 %d 处 / lastError %d，实测 %d / %d\n"
                             % (want_total, want_last, c["total"], c["lastError"]))
            return 1
        sys.stdout.write("CHECK OK: 注释与实测一致\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

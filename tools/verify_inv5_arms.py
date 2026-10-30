# -*- coding: utf-8 -*-
"""把 r17/r18 延期的 INV5 三条改坏臂跑成可引用证据（纯 Python 闸，无需编译）。

三种形态（都是 r14/r16 实际踩过的）：
  A. 整行删掉 scale 的 registerParam —— 未登记
  B. 把两条 makeEnumParam 搬出 registerParams（P1-8 原形）
  C. 保留 makeDoubleParam("scale") 文本、去掉 registerParam 包裹（r16 指的旁路）
每臂：改源码 → 跑闸 → 记 GREEN/RED → 还原；最后跑一次基线确认 GREEN。
"""
import io
import os
import subprocess
import sys

GATE = [sys.executable, "tools/wiring_checks.py", "--repo", "."]
TARGET = "src/ObjectCountingNode.cpp"
DETECT = "src/DnnDetectNode.cpp"


def run_gate():
    p = subprocess.run(GATE, capture_output=True, text=True, encoding="utf-8", errors="replace")
    out = (p.stdout or "") + (p.stderr or "")
    verdict = "GREEN" if "GREEN" in out else ("RED" if "RED" in out else "?")
    return verdict, out


def backup(path):
    bak = "build/_arm_" + os.path.basename(path) + ".bak"
    with open(path, "rb") as f:
        data = f.read()
    with open(bak, "wb") as f:
        f.write(data)
    return bak


def restore(path, bak):
    with open(bak, "rb") as f:
        data = f.read()
    with open(path, "wb") as f:
        f.write(data)
    os.remove(bak)


def arm_a():
    s = io.open(TARGET, encoding="utf-8").read()
    start = s.find('    registerParam(makeDoubleParam(QStringLiteral("scale")')
    assert start > 0, "A anchor"
    end = s.find(";\n", start) + 2
    io.open(TARGET, "w", encoding="utf-8").write(s[:start] + s[end:])


def arm_b():
    s = io.open(DETECT, encoding="utf-8").read()
    start = s.find('        makeEnumParam(QStringLiteral("dnnBackend")')
    assert start > 0, "B anchor"
    k = s.find("yoloLayoutLabels(),", start)
    line_end = s.find("\n", k) + 1
    label_end = s.find("\n", line_end) + 1
    block = s[start:label_end]
    s = s[:start] + s[label_end:]
    close = s.find("    });", max(0, start - 300))
    assert close >= 0, "B close"
    close_end = s.find("\n", close) + 1
    io.open(DETECT, "w", encoding="utf-8").write(s[:close_end] + block + s[close_end:])


def arm_c():
    s = io.open(TARGET, encoding="utf-8").read()
    old = ('    registerParam(makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,\n'
           '                                  DnnDetectNode::labelScale()));')
    new = ('    makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,\n'
           '                    DnnDetectNode::labelScale());')
    assert s.count(old) == 1, "C anchor"
    io.open(TARGET, "w", encoding="utf-8").write(s.replace(old, new))


def main():
    base, out = run_gate()
    sys.stdout.write("基线: %s\n" % base)
    assert base == "GREEN", out[-800:]

    arms = [("A 删掉 scale 登记", TARGET, arm_a),
            ("B 枚举搬出 registerParams", DETECT, arm_b),
            ("C 丢掉 registerParam 包裹", TARGET, arm_c)]
    for title, path, fn in arms:
        bak = backup(path)
        try:
            fn()
            v, o = run_gate()
        finally:
            restore(path, bak)
        line = [l for l in o.splitlines() if "findings" in l or "registers" in l]
        sys.stdout.write("%-26s -> %s   %s\n" % (title, v, (line[0][:100] if line else "")))
        assert v == "RED", "臂未判红: " + title

    after, _ = run_gate()
    sys.stdout.write("复原后: %s\n" % after)
    assert after == "GREEN"
    sys.stdout.write("结论: 3/3 RED，复原 GREEN\n")


if __name__ == "__main__":
    main()

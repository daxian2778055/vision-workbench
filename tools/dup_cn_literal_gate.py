"""Executable definition of the G-4 gate: one Chinese sentence, copied into a second file.

Why this exists (U-35, ledger candidate G-4): the ledger had been carrying "重复中文文案静态闸"
for four rounds with a rule nobody could run -- "同一段中文 literal 在 src/ 出现 >=2 次即判红".
Measured, that rule is not a gate, it is 537 findings (build/u35_probe/dup_src.txt), and most of
them are one-character UI words like 「提示」 and 「值」. A gate that red on ordinary work gets
disabled, so the rule needed narrowing with data before anything was pinned. The two cuts the
numbers support are recorded in §3.39; in short:

  * **cross-file only** -- the hazard is "someone wrote this sentence again in another file",
    not "a menu, its tooltip and its status-bar entry name the same action" (161 of the 537
    hits at threshold >=2 are same-file reuse).
  * **at least MIN_CHARS non-whitespace characters** -- below that a "sentence" is a label, and
    a label reused in a new dialog is normal work, not drift. 8 was the cut where what remains
    reads as sentences; the sweep at 4/5/6/7/8/10/12 is in §3.39 table 1.

The literal/text/tier definitions come from tools/scan_i18n_surface.py -- the tokenizer there
(iter_literals) is shared, not copied: a second walk of the same source would be this gate's own
first finding.

Contract (set equality against BASELINE below, like U-8's T14 roster gate):
  * a sentence that is duplicated cross-file today and is NOT in BASELINE      -> ADDED   -> red
  * a sentence in BASELINE whose site count has changed                        -> COUNT   -> red
  * a BASELINE sentence that is no longer duplicated cross-file at all         -> GONE    -> red
  * anything else (same set, same counts)                                      -> green
ADDED/COUNT are what the gate is for. GONE is deliberate too: shrinking the baseline is real
progress and must be an explicit, reviewed edit to this file, never something a red swallows.

BASELINE is *not* a to-do list this gate enforces away -- every entry is a sentence the repo
ships today (see §3.39 table 3 for the families and how many sites each spans). Dedupe is a
separate decision per family; this file only stops the set from growing.

Scope: src/*.cpp, src/*.h, include/*.h -- production text only. tests/ is excluded on purpose and
measured, not assumed: widening the scope to include it adds 37 cross-file duplicates, **all 37**
of which have a tests/ file among their sites (0 purely-production ones), plus 5 extra sites on
sentences already in this baseline. A test that spells out the same expected sentence twice is a
fixture, not drift -- so those 37 would be findings a reviewer cannot act on. Registered in §3.39.

Reproduce:
    python tools/dup_cn_literal_gate.py                 # the gate; 0 = green
    python tools/dup_cn_literal_gate.py --emit-baseline # print a fresh BASELINE block (review it)
    python tools/dup_cn_literal_gate.py --list          # print the current set with site files
Exit codes: 0 = matches BASELINE; 1 = at least one ADDED/COUNT/GONE difference;
            2 = the gate's own teeth self-check failed (the classifier is broken, findings above
                would be meaningless -- reported separately so a broken gate cannot read as clean).
Console output is UTF-8 via reconfigure (the sentences are Chinese; a cp936 console would mangle
them, same handling as tools/scan_i18n_surface.py).
"""

import argparse
import collections
import glob
import hashlib
import importlib.util
import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIN_CHARS = 8          # non-whitespace characters in the decoded sentence
MIN_FILES = 2          # distinct files a sentence must appear in to count as a duplicate

_spec = importlib.util.spec_from_file_location(
    "scan_i18n_surface", os.path.join(REPO_ROOT, "tools", "scan_i18n_surface.py"))
S = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(S)

SPACE = re.compile(r"\s+")


def normalized(text):
    """Comparison key for a sentence: newlines folded back to a visible escape.

    A C++ literal may carry real newlines (``"a\\nb"``); the baseline is one entry per line, so
    the key has to be single-line without losing the difference between "a\\nb" and "anb".
    """
    return text.replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")


def production_files():
    return sorted(glob.glob(os.path.join(REPO_ROOT, "src", "*.cpp"))
                  + glob.glob(os.path.join(REPO_ROOT, "src", "*.h"))
                  + glob.glob(os.path.join(REPO_ROOT, "include", "*.h")))


def duplicated_sentences():
    """{normalized sentence: [(relpath, line), ...]} for the cross-file duplicates in scope.

    Both spellings of a sentence count as that sentence: the text is taken decoded, so an
    escaped ``\\u89d2`` and a literal 角 are the same character and land in the same bucket.
    """
    seen = collections.defaultdict(list)
    for path in production_files():
        rel = os.path.relpath(path, REPO_ROOT).replace("\\", "/")
        cleaned, _raws = S.strip_comments(
            io.open(path, "r", encoding="utf-8", errors="replace").read())
        for lit in S.iter_literals(cleaned):
            if not S.CJK.search(lit.decoded):
                continue
            if len(SPACE.sub("", lit.decoded)) < MIN_CHARS:
                continue
            seen[normalized(lit.decoded)].append((rel, lit.line))
    return {k: v for k, v in seen.items()
            if len({f for f, _ in v}) >= MIN_FILES}


def baseline_map():
    out = {}
    for count, sentence in BASELINE:
        out[sentence] = count
    return out


def diff_sets(actual, expected):
    """(added, changed, gone) between {sentence: count} maps -- the whole judgement, isolated.

    Kept as a pure function of two maps so the teeth self-check below can feed it synthetic
    shapes without touching a single source file (same reasoning as U-8's rosterDiff).
    """
    added = sorted(k for k in actual if k not in expected)
    gone = sorted(k for k in expected if k not in actual)
    changed = sorted("%s: baseline %d -> now %d" % (k, expected[k], actual[k])
                     for k in actual
                     if k in expected and actual[k] != expected[k])
    return added, changed, gone


def teeth_selfcheck():
    """Break the judgement itself, on synthetic ASCII inputs, and report if it stops reacting.

    Without this the gate could rot silently: a comparison weakened to "only look at the
    sentence, ignore how many" still prints OK for the repo as it stands.
    """
    base = {"Alpha sentence": 2, "Beta sentence": 3}
    fails = []

    def want(name, cond):
        if not cond:
            fails.append(name)

    added, changed, gone = diff_sets({"Alpha sentence": 2, "Gamma sentence": 2}, base)
    want("T1 new duplicate is ADDED", added == ["Gamma sentence"]
         and gone == ["Beta sentence"] and not changed)
    added, changed, gone = diff_sets({"Beta sentence": 4}, base)
    want("T2 an extra copy is COUNT", changed == ["Beta sentence: baseline 3 -> now 4"]
         and not added and gone == ["Alpha sentence"])
    added, changed, gone = diff_sets({"Alpha sentence": 1, "Beta sentence": 3}, base)
    want("T3 count below baseline still differs",
         changed == ["Alpha sentence: baseline 2 -> now 1"] and not added and not gone)
    added, changed, gone = diff_sets({"alpha sentence": 2, "Beta sentence": 3}, base)
    want("T4 case-only difference is two findings",
         added == ["alpha sentence"] and gone == ["Alpha sentence"] and not changed)
    added, changed, gone = diff_sets({"Alpha sentence": 2, "Beta sentence": 3}, base)
    want("T5 identical set is clean", not (added or changed or gone))
    added, changed, gone = diff_sets({"Alpha sentence": 3, "Beta sentence": 3}, base)
    want("T6 count drift upward is not treated as a new key",
         changed == ["Alpha sentence: baseline 2 -> now 3"] and not added and not gone)
    return fails


def emit_baseline(actual):
    lines = ["BASELINE = ("]
    for sentence, count in sorted(((k, v) for k, v in actual.items()),
                                  key=lambda kv: (-kv[1], kv[0])):
        # repr() is left alone on purpose: the key carries a visible two-character newline
        # escape (see normalized()), and repr's own escaping is what makes reading this block
        # back produce that same key.
        lines.append("    (%r, %r)," % (count, sentence))
    lines.append(")")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--emit-baseline", action="store_true",
                    help="print the current set as a BASELINE block and exit 0")
    ap.add_argument("--list", action="store_true", help="print every baseline hit with its files")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    sites = duplicated_sentences()
    actual = {k: len(v) for k, v in sites.items()}
    digest = hashlib.sha256(
        "\n".join(sorted("%d\t%s" % (c, k) for k, c in actual.items())).encode("utf-8")).hexdigest()

    if args.emit_baseline:
        print(emit_baseline(actual))
        print("# DUP sentences=%d sites=%d sha256=%s" % (len(actual), sum(actual.values()), digest))
        return 0

    fails = teeth_selfcheck()
    print("TEETH checks=%d failed=%d" % (6, len(fails)))
    for name in fails:
        print("  TEETH-FAIL %s" % name)

    added, changed, gone = diff_sets(actual, baseline_map())
    print("DUPCN scope=src+include min_chars=%d min_files=%d sentences=%d sites=%d sha256=%s"
          % (MIN_CHARS, MIN_FILES, len(actual), sum(actual.values()), digest))
    print("DUPCN baseline=%d verdict=%s added=%d count=%d gone=%d"
          % (len(BASELINE), "RED" if (added or changed or gone) else "GREEN",
             len(added), len(changed), len(gone)))
    for k in added:
        print("  ADDED  x%-3d files=%-2d %s"
              % (actual[k], len({f for f, _ in sites[k]}), k[:70]))
        if args.list:
            for f, ln in sites[k]:
                print("           %s:%d" % (f, ln))
    for row in changed:
        print("  COUNT  %s" % row[:110])
    for k in gone:
        print("  GONE   %s" % k[:70])
    if args.list:
        print("-- current set (sentence / count / files) --")
        for k in sorted(actual, key=lambda s: (-actual[s], s)):
            print("  x%-3d files=%-2d %s" % (actual[k], len({f for f, _ in sites[k]}), k[:70]))

    if fails:
        print("FAIL: the gate's own judgement is broken (%s) -- findings above are not trustable"
              % ", ".join(fails))
        return 2
    if added or changed or gone:
        print("FAIL: duplicated Chinese sentences drifted against BASELINE "
              "(%d ADDED / %d COUNT / %d GONE)" % (len(added), len(changed), len(gone)))
        print("      Adding a copy: move the sentence to one shared definition instead of "
              "re-baselining.")
        return 1
    print("OK: cross-file Chinese duplicates match BASELINE (%d sentences)" % len(BASELINE))
    return 0


# Today's cross-file duplicate sentences (count, normalized text), produced by
# `python tools/dup_cn_literal_gate.py --emit-baseline` on the commit this gate landed.
# Review before editing; see the module docstring for what each direction of drift means.
BASELINE = (
    (4, 'ROI 宽度（0=全图）'),
    (4, 'ROI 高度（0=全图）'),
    (4, '无法写入 %1：%2'),
    (4, '枚举设备失败，错误码:'),
    (3, '<i>提示：点击后在图像视图上拖拽绘制搜索线/圆，'),
    (3, 'ONNX 模型加载失败'),
    (3, 'ONNX 模型文件 (.onnx)'),
    (3, 'VisionFlowPlatform 统计报表'),
    (3, '匹配模式需要设置模板文件路径'),
    (3, '在画布上设置搜索区域'),
    (3, '均值（R,G,B）'),
    (3, '字节匹配-协议组装'),
    (3, '已加载模板: %1'),
    (3, '松手后自动写回坐标参数。</i>'),
    (3, '模板保存失败: %1'),
    (3, '模板加载失败: %1'),
    (3, '请设置 ONNX 模型路径'),
    (2, 'BGR/RGB 交换'),
    (2, 'ONNX深度学习推理'),
    (2, 'ONNX目标检测'),
    (2, 'PNG (*.png);;所有文件 (*)'),
    (2, 'ROI 宽（0=整图）'),
    (2, 'ZXing条码解码'),
    (2, '保存名称（供坐标系换算复用）'),
    (2, '写入寄存器 - 地址%1'),
    (2, '创建相机句柄失败，错误码:'),
    (2, '匹配位姿写入 Fixture 名（空=不写）'),
    (2, '参与拟合的最大轮廓数'),
    (2, '参与拟合的最小点数'),
    (2, '回写校验（写后回读比对）'),
    (2, '寄存器地址%1 写入失败。'),
    (2, '寄存器地址%1 写入成功：%2'),
    (2, '已训练并保存: %1'),
    (2, '平移对齐最大允许偏移（超出视为对齐失败）'),
    (2, '开启后：每次写寄存器都回读同地址并比对，不一致才报警（默认关闭）'),
    (2, '开始采集失败，错误码:'),
    (2, '当参数为默认值 (0,0)-(100,100) 时，单次运行将改用图像对角线两端点估算距离。'),
    (2, '打开相机时发生异常:'),
    (2, '打开相机时发生未知异常'),
    (2, '打开设备失败，错误码:'),
    (2, '旋转搜索范围 ±（仅“平移+旋转”模式，步长 0.5°）'),
    (2, '曝光时间 (μs):'),
    (2, '未找到匹配的设备:'),
    (2, '标定成功\\n标定误差: %1\\n内参: [%2]'),
    (2, '标定板描述文件 (.descr)'),
    (2, '标定结果无法存入：键 cam_params 上已有 %1 项的另一种载荷，本次 HALCON 内参是 %2 项'),
    (2, '模板图像文件 (png/bmp/jpg)'),
    (2, '流程处于编辑锁定状态（运行中），无法插入片段'),
    (2, '第 %1 项不是数值'),
    (2, '第 %1 项不是有限值'),
    (2, '类别名文件 classes.txt（空则模型同目录）'),
    (2, '该寄存器为只读模式，无法写入。'),
    (2, '请先选择标定板描述文件 (.descr)'),
    (2, '输入值 (数据类型: %1):'),
    (2, '边缘极性：全部/暗到亮/亮到暗'),
    (2, '边缘选择：全部/第一个/最后一个'),
    (2, '重连间隔(ms):'),
    (2, '锁定中，无法删除'),
)
# DUP sentences=58 sites=137 sha256=527b81fb43e0fbcdc49395224fc4933c1b6b19be989e49ae2b8a51ef471255ce


if __name__ == "__main__":
    sys.exit(main())

"""Executable definition of the `doc:line` citations used by the gap plan.

Why this exists (stage A / A7): docs/对标差距推进计划.md quotes other documents by
line number (`路线图:137`, `SRS:454-459`, ...). Ledger rows get inserted into the
roadmap table, so every such number drifts, and a stale number is an unfalsifiable
claim - the exact failure A7 was opened for. Here each cited position is pinned by a
locating regex plus the line number the docs currently publish. Editing either doc
without re-syncing turns this check red, so the numbers in prose can never rot
silently.

Second check (also A7): the plan doc publishes a table of these same anchors and calls it
"generated from the script's definitions". That claim is only falsifiable if something
compares the two - so this script re-reads the table and fails when a row disagrees with
ANCHORS in either direction.

Reproduce:
    python tools/doc_anchors.py
Exit codes: 0 = every anchor matched exactly once at its published line;
            1 = at least one DRIFT / MISSING / AMBIGUOUS anchor.
Output is ASCII-only on purpose (the console code page is not guaranteed UTF-8).
"""

import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RM = os.path.join(REPO_ROOT, "docs", "VisionMaster4.4对标分析与路线图.md")
SRS = os.path.join(REPO_ROOT, "docs", "需求规格说明书.md")
PLAN = os.path.join(REPO_ROOT, "docs", "对标差距推进计划.md")

# (anchor name, file, locating regex, line the docs currently publish)
ANCHORS = [
    ("rm-ledger-a2-bom-rule", RM, r"不能直接删", 69),
    ("rm-cand-2-s7", RM, r"^\| ② \| \*\*G-P1-4", 97),
    ("rm-cand-3-i18n", RM, r"^\| ③ \| \*\*G-P1-5", 98),
    ("rm-cand-4-sdk", RM, r"^\| ④ \| G-P1-7", 99),
    ("rm-cand-5-mes", RM, r"^\| ⑤ \| G-P1-12", 100),
    ("rm-cand-dl-training", RM, r"^\| —— \| \*\*G-P1-3 DL", 101),
    ("rm-cand-p2-bundle", RM, r"^\| —— \| \*\*P2 全部", 102),
    ("rm-real4-heading", RM, r"^> ### .*「真机四项」定义", 104),
    ("rm-real4-unconfirmed", RM, r"定义提议，尚未由需求方确认", 106),
    ("rm-real4-row-camera", RM, r"^> \| 1 \| 相机真机联调", 110),
    ("rm-real4-row-plc", RM, r"^> \| 2 \| PLC 真机联动", 111),
    ("rm-real4-row-soak", RM, r"^> \| 3 \| 现场 7×24 长稳", 112),
    ("rm-real4-row-ac", RM, r"^> \| 4 \| AC1–AC8 现场验收", 113),
    ("rm-observation-item", RM, r"^\*\*当前观察项", 123),
    ("rm-cmp-row-locate", RM, r"^\| 定位 \| 模板匹配", 141),
    ("rm-cmp-row-measure", RM, r"^\| 测量 \| 卡尺", 142),
    ("rm-cmp-row-dl", RM, r"^\| 深度学习 \| 分类/检测", 145),
    ("rm-conclusion-line", RM, r"^\*\*结论（2026-09-24 复核更新）", 153),
    ("rm-gp2-1-3d", RM, r"^\| G-P2-1 \| 3D 视觉整体缺失", 205),
    ("rm-gp2-4-accel", RM, r"^\| G-P2-4 \| 推理加速", 208),
    ("rm-sec6-3d", RM, r"^1\. \*\*3D 视觉\*\*（G-P2-1）", 238),
    ("rm-sec6-accel", RM, r"^4\. \*\*推理加速\*\*（G-P2-4）", 241),
    ("rm-smoke-diag-note", RM, r"该文件取完数据后即删除", 268),
    ("srs-e1-row", SRS, r"^\| E1 \|", 456),
    ("srs-e6-row", SRS, r"^\| E6 \|", 461),
    ("srs-w2-heading", SRS, r"审核建议中本轮未实施的一条（W-2）", 500),
    ("srs-w2-unclosed", SRS, r"本项未收口的部分（不要读成", 512),
    ("srs-ac1-row", SRS, r"^\| AC1 \|", 662),
    ("srs-ac6-row", SRS, r"^\| AC6 \|", 667),
    ("srs-ac7-row", SRS, r"^\| AC7 \|", 668),
    ("srs-ac8-row", SRS, r"^\| AC8 \|", 669),
    ("srs-ac9-row", SRS, r"^\| AC9 \|", 670),
    ("srs-ac10-row", SRS, r"^\| AC10 \|", 671),
]


def read_lines(path):
    if not os.path.isfile(path):
        return None
    with io.open(path, "r", encoding="utf-8") as handle:
        return handle.read().split("\n")


def check_plan_table():
    """The plan's anchor table claims to mirror ANCHORS; a hand edit would break that."""
    lines = read_lines(PLAN)
    if lines is None:
        print("doc-table=absent (plan doc not in tree; table not checked)")
        return 0
    row = re.compile(r"^\| `(rm-[a-z0-9\-]+|srs-[a-z0-9\-]+)` \|[^|]*\| \*\*(\d+)\*\* \|$")
    table = {}
    for line in lines:
        m = row.match(line)
        if m:
            table[m.group(1)] = int(m.group(2))
    bad = 0
    for name, _, _, expect in ANCHORS:
        got = table.get(name)
        if got is None:
            print("table-row=%s status=MISSING_FROM_TABLE script=%d" % (name, expect))
            bad += 1
        elif got != expect:
            print("table-row=%s status=MISMATCH table=%d script=%d" % (name, got, expect))
            bad += 1
    for name in sorted(set(table) - {a[0] for a in ANCHORS}):
        print("table-row=%s status=NOT_IN_SCRIPT" % name)
        bad += 1
    print("DOC-TABLE rows=%d anchors=%d bad=%d" % (len(table), len(ANCHORS), bad))
    return bad


def main():
    cache = {}
    bad = 0
    for name, path, pattern, expect in ANCHORS:
        if path not in cache:
            cache[path] = read_lines(path)
        lines = cache[path]
        if lines is None:
            print("anchor=%s file=%s status=MISSING_FILE"
                  % (name, "SRS" if path == SRS else "RM"))
            bad += 1
            continue
        rx = re.compile(pattern)
        hits = [i + 1 for i, line in enumerate(lines) if rx.search(line)]
        if not hits:
            status = "MISSING"
        elif len(hits) > 1:
            status = "AMBIGUOUS"
        elif hits[0] == expect:
            status = "OK"
        else:
            status = "DRIFT"
        if status != "OK":
            bad += 1
            print("anchor=%s file=%s expect=%d hits=%s status=%s"
                  % (name, "SRS" if path == SRS else "RM", expect,
                     hits or "[]", status))
    table_bad = check_plan_table()
    print("DOC-ANCHORS anchors=%d bad=%d" % (len(ANCHORS), bad))
    return 1 if (bad + table_bad) else 0


if __name__ == "__main__":
    sys.exit(main())

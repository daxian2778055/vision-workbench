"""U-35 取证探针（只读）：把 G-4 那条"同一句中文出现两次就判红"的原始口径量成一张表。

为什么要这张表：推进计划里 G-4 挂着的那句口径（§3.34 表 4 第 5 条转述的"在 src/ 出现 >=2 次即
判红"）从没有人跑过。跑出来的结果是 537 条重复句子，其中大头是「提示」(31 处) 「图像处理」(25 处)
这种一两字的标签——这种闸门会被关掉，不会被满足。所以 U-35 动手之前先把两个候选切法量清：
  · 只看**跨文件**重复（同一句话出现在 2 个以上不同文件）——同文件复用不是"抄了第二份"；
  · 只看在**长度下限**以上的句子——短于下限的是标签，不是句子。

本探针不另写一份口径：它装载 tools/dup_cn_literal_gate.py 这个闸门本体，只换它的全局常量
（MIN_CHARS / MIN_FILES / 是否含 tests/），所以这里读到的每一条数都走的是闸门那条代码路径。

用法：
    python tools/probes/U35_dup_cn_sweep_probe.py            # 表 1：长度下限扫描
    python tools/probes/U35_dup_cn_sweep_probe.py --scope-all # 表 3：把 tests/ 拉进来会多出什么
"""
import collections
import glob
import importlib.util
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

_spec = importlib.util.spec_from_file_location(
    "dup_cn_literal_gate", os.path.join(ROOT, "tools", "dup_cn_literal_gate.py"))
G = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(G)
S = G.S

FLOORS = [0, 4, 5, 6, 7, 8, 10, 12]


def gather(paths):
    """[(relpath, normalized decoded sentence)] for every CJK literal in these files.

    No length or file-count filter here on purpose: the walk happens once and the sweep below
    only re-filters in memory, so a row of the table cannot be an artifact of re-parsing.
    """
    out = []
    for path in paths:
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        cleaned, _raws = S.strip_comments(
            io.open(path, "r", encoding="utf-8", errors="replace").read())
        for lit in S.iter_literals(cleaned):
            if S.CJK.search(lit.decoded):
                out.append((rel, G.normalized(lit.decoded)))
    return out


def _group(records, floor):
    """{sentence: [relpath, ...]} for cross-file duplicates at one length floor -- one code path
    for both the table rows and the ONLY_LEN7_CROSS listing below."""
    by = collections.defaultdict(list)
    for rel, sentence in records:
        if len(G.SPACE.sub("", sentence)) >= floor:
            by[sentence].append(rel)
    dup = {k: v for k, v in by.items() if len(v) >= 2}
    return {k: v for k, v in dup.items() if len(set(v)) >= 2}


def split(records, floor):
    """(dup_total, dup_sites, cross_sentences, same_file_only) at one length floor."""
    by = collections.defaultdict(list)
    for rel, sentence in records:
        if len(G.SPACE.sub("", sentence)) >= floor:
            by[sentence].append(rel)
    dup = {k: v for k, v in by.items() if len(v) >= 2}
    cross = _group(records, floor)
    return len(dup), sum(len(v) for v in dup.values()), len(cross), len(dup) - len(cross)


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    prod = G.production_files()
    tests = sorted(glob.glob(os.path.join(ROOT, "tests", "*.cpp")))
    if "--scope-all" in sys.argv:
        rec_prod = gather(prod)
        rec_all = rec_prod + gather(tests)
        base = G.baseline_map()
        cnt_all = collections.Counter(s for _, s in rec_all)
        files_of = collections.defaultdict(set)
        for rel, sentence in rec_all:
            files_of[sentence].add(rel)
        cross_all = {s for s in files_of
                     if len(files_of[s]) >= 2
                     and len(G.SPACE.sub("", s)) >= G.MIN_CHARS}
        fresh = sorted(cross_all - set(base))
        touching = [s for s in fresh if any(f.startswith("tests/") for f in files_of[s])]
        gained = sum(max(cnt_all[s] - base[s], 0) for s in base)
        print("SCOPE_PROD files=%d baseline_entries=%d src_sites=%d"
              % (len(prod), len(base), sum(base.values())))
        print("SCOPE_ALL files=%d min_chars=%d cross=%d"
              % (len(prod) + len(tests), G.MIN_CHARS, len(cross_all)))
        print("NEW_NOT_IN_BASELINE total=%d touching_tests=%d purely_production=%d"
              % (len(fresh), len(touching), len(fresh) - len(touching)))
        print("BASELINE_SENTENCES extra_sites_if_tests_in_scope=%d" % gained)
        for s in fresh[:12]:
            print("  NEW  x%-3d %s  [%s]" % (cnt_all[s], s[:52],
                                             ",".join(sorted(files_of[s])[:3])))
        return 0
    records = gather(prod)
    print("FLOOR  dup_sentences  dup_sites  cross_file  same_file_only   (scope=src+include)")
    for floor in FLOORS:
        dup, sites, cross, same = split(records, floor)
        print("%-6d %-13d %-10d %-11d %d" % (floor, dup, sites, cross, same))
    short = collections.Counter(s for _, s in records if len(G.SPACE.sub("", s)) <= 3)
    print("SHORT_SENTENCES <=3 chars: distinct=%d sites=%d" % (len(short), sum(short.values())))
    print("TOP_SHORT %s" % " ".join("%s=%d" % (s, n) for s, n in short.most_common(8)))
    # The 7-vs-8 difference is exactly the cross-file sentences whose non-whitespace length is 7:
    # the ledger names them instead of asserting from memory which ones "already stop being a
    # sentence" at that width.
    seven = {}
    for sentence, files in _group(records, 7).items():
        if len(G.SPACE.sub("", sentence)) == 7:
            seven[sentence] = files
    print("ONLY_LEN7_CROSS count=%d (进入下限 7、不进入下限 8 的那批)" % len(seven))
    for sentence in sorted(seven):
        print("  x%-3d files=%d %s  [%s]"
              % (len(seven[sentence]), len(set(seven[sentence])), sentence[:40],
                 ",".join(sorted(set(seven[sentence])))[:80]))
    return 0


if __name__ == "__main__":
    sys.exit(main())

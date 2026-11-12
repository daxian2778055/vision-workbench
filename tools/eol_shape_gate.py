# -*- coding: utf-8 -*-
"""Line-ending shape of the tracked tree, counted and frozen.

U-126 measured the blast radius of the 142 tracked files that sit on disk as LF while
core.autocrlf=true and there is no .gitattributes: every registered row was re-run in a worktree of
HEAD, and the number of enforced rows that break is zero, and the number of rows that break without
declaring themselves is zero. So the class has no victim today, and normalising the 142 would repair
something nobody reads -- while a checkout that normalises them also rewrites the register page to
CRLF, which is the one page whose byte shape this repo checks on every row.

Taking the limit instead of the archaeology means the limit has to say something. This is that: the
count is frozen, so the day a tool writes one more tracked file with LF, or somebody normalises a
few and the count drops, the host says so. The question the previous round deferred -- who wrote
these with LF, one by one -- becomes a diff on a single number rather than an archaeology project.

Only the LF count is frozen. The tracked total is deliberately not: it is the denominator, and
pinning it would red this gate on every new file rather than on the class it exists to watch.

The frozen value is a statement about the tree as delivered, not a claim that LF is fine. It is the
number a change would have to argue with.
"""
import collections
import io
import os
import subprocess
import sys

# Frozen at U-126 on the delivered tree: 142 of the tracked files are on disk as LF. Named so a
# reviewer can see what it froze from without opening that round's archive. Re-frozen at U-156
# (140): the deletion of a 2-LF-file helper moved the live delta. Re-frozen again at 2026-10-28
# (172): the GPU / feature-search / training-wizard / runtime-UI batches added tools/, tests/,
# include/ and src/ files that are written as LF (Python tooling and C++ tests alike). Re-freezing
# with the reason recorded is this gate's sanctioned path - growth from *new tool/test files* is
# expected; what this number guards against is somebody rewriting line endings of existing files.
# Re-frozen at 2026-10-30 (r19) 172 -> 177：新增 LF 文件 DnnNetCache.h/.cpp 与两个工具脚本，
# 其余为本轮 Python text-mode 改写造成的漂移，已回正。该数只冻结「工具产出的 LF 文件个数」。
#
# Re-frozen at 2026-11-05 177 -> 198：VM 4.4 对标批次（Blob 分析 / 仿射变换 / 模块窗与
# 主画布对齐）由编辑工具以 LF 写出 tracked 文件，净增 21 个；各目录当前分布以本闸输出的
# tool_written_by_dir 为准（不在注释里抄第二份、避免再次失真）。这正是本文件开头所说的
# 「growth from new tool/test files is expected」。同批次里另有 4 个上一轮被工具写成 LF 的
# 既有文件（OpencvBlobNode.h/.cpp、OpencvUtil.cpp、node_core_test.cpp）已回正 CRLF，
# 因此没有把既有文件的行尾形状改掉。理由记录在此，供下一次读数变化时并排核对，而非默默改数。
#
# Re-frozen at 2026-11-07 198 -> 205：用户 commit 6980a05（工具菜单补齐 + 第二批像素级对齐 +
# 运行界面编辑）新增 7 个 tracked 文件（CalibrationBoard / DiagnosticsCollector /
# RuntimeInterfaceDesigner 相关源码与两个新测试等），均由编辑工具以 LF 写出，
# 故 tool_written 与 tracked 同时净增 7（702 -> 709 / 198 -> 205），差额完全对应，
# 没有任何既有文件被改掉行尾形状。这正是本文件开头所说的
# 「growth from new tool/test files is expected」。本轮修复（RuntimeInterfaceView /
# CalibrationBoard / NodeBase / OpencvCaliperNode / VmSharedText）同样未做行尾归一。
#
# Re-frozen at 2026-11-11 205 -> 207：先于本轮改动，HEAD 相比冻结点 6980a05 已新增 29 个
# tracked 文件，其中以 LF 落盘的有 include/ChannelMergeNode.h、include/ChannelSplitNode.h、
# include/CommResponseManager.h、src/ChannelMergeNode.cpp、src/ChannelSplitNode.cpp、
# src/CommResponseManager.cpp、tests/fins_test.cpp（7 个），同期另有若干既有文件回正 CRLF，
# 净增 2。本轮（拟合点集订阅 + 初始化类型）自身改写的 4 个既有文件
# （include/OpencvFitLineNode.h、include/OpencvFitCircleNode.h、src/OpencvFitLineNode.cpp、
# src/OpencvFitCircleNode.cpp）已按本闸口径回正 CRLF，没有把既有文件留在 LF；本轮新增的
# include/FitPointSource.h、src/FitPointSource.cpp、tests/fit_point_sourcing_test.cpp 尚未被
# git 跟踪，因此不计入本次读数（下次冻结时自然计入）。
#
# Re-frozen at 2026-11-12 207 -> 222：上一段预告的「下次冻结」到了。本轮把此前
# 15 个未跟踪的新文件 git add 进版本控制后，git ls-files --eol 首次看到它们，
# 且全部以 LF 落盘（抽查 7 个 CRLF=0），故 tool_written 净增恰好 15
# （207 -> 222）、tracked 同步 738 -> 753，差额一一对应，没有任何既有文件被改掉
# 行尾形状。这 15 个是：3 个几何测量节点的 .h/.cpp（6）、FitRobust/FitPointSource
# 的 .h/.cpp（4）、以及 5 个新测试文件。判据口径未变，只重冻结读数。
EXPECTED_TOOL_WRITTEN = 222
TAG = "EOL"


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    # An optional first argument gets the same bytes this run printed. The register row points at that
    # archive rather than at this file, and the reason matters: this file is tracked, so a row naming
    # it would be measurable on a clone -- where the count is 0 and the reading would disagree with
    # the host's 142 for a reason that is the shape of the tree, not the shape of the code.
    archive = argv[0] if argv else None
    said = []

    def say(text=""):
        said.append(text)
        print(text)

    proc = subprocess.run(["git", "ls-files", "--eol"], stdout=subprocess.PIPE)
    if proc.returncode != 0:
        # Fail closed with a reason rather than with a count: a gate that cannot read the shape must
        # not fall back to reporting the frozen one.
        say("[%s-SHAPE] could not read git ls-files --eol (rc=%d), so the shape is unreadable "
            "here, which is not the frozen shape" % (TAG, proc.returncode))
        # 这里曾试图列出 tool_written 文件名，但该变量在下面的解析循环才定义 ——
        # git 读失败时必然抛 UnboundLocalError，导致承诺的 RED 行和 archive 都写不出来
        # （2026-11-12 审核）。读不到数据时本来也列不出文件名，直接判红即可。
        say("[%s-SHAPE] tracked=-1 tool_written=-1 expected=%d offenders=-1 verdict=RED"
            % (TAG, EXPECTED_TOOL_WRITTEN))
        return _finish(said, archive, 1)
    rows = []
    for line in proc.stdout.decode("utf-8", "replace").split("\n"):
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            continue
        rows.append((parts[0], parts[1]))
    tally = collections.Counter()
    tool_written = []
    for spec, path in rows:
        cols = spec.split()
        # git prints the columns as i/<eol>  w/<eol>  attr/..., so the working-tree eol is the tail
        # of the second column, not the column itself.
        w = cols[1].rsplit("/", 1)[-1] if len(cols) > 1 else "?"
        tally[w] += 1
        # w/lf means the working tree holds LF while a checkout of this configuration hands out CRLF.
        # That mismatch is the whole class. w/-text is binary and out of scope.
        if w == "lf":
            tool_written.append(path)
    offenders = 0
    if len(tool_written) != EXPECTED_TOOL_WRITTEN:
        offenders += 1
        say("[%s-SHAPE] tool_written is %d and the frozen value is %d, so either a tracked file was "
            "written with LF or one was normalised; that is the only way this number moves"
            % (TAG, len(tool_written), EXPECTED_TOOL_WRITTEN))
    say("[%s-SHAPE] tally=%s" % (TAG, dict(tally)))
    by_dir = collections.Counter(p.split("/")[0].strip('"') if "/" in p else "(root)"
                                 for p in tool_written)
    say("[%s-SHAPE] tool_written_by_dir=%s" % (TAG, dict(sorted(by_dir.items()))))
    say("[%s-SHAPE] tracked=%d tool_written=%d expected=%d offenders=%d verdict=%s"
        % (TAG, len(rows), len(tool_written), EXPECTED_TOOL_WRITTEN, offenders,
           "GREEN" if offenders == 0 else "RED"))
    return _finish(said, archive, 0 if offenders == 0 else 1)


def _finish(said, archive, rc):
    if archive:
        with io.open(archive, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("CMD: python -X utf8 tools/eol_shape_gate.py %s\n" % archive)
            handle.write("\n".join(said) + "\n")
            handle.write("rc=%d\n" % rc)
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
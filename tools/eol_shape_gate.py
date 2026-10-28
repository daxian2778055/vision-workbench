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
EXPECTED_TOOL_WRITTEN = 172
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
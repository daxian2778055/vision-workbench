"""Cited-log presence gate -- turns "a log the ledger cites must be openable" into something that
can actually stop a run (tenth-round review, item 3, which this repo itself had already listed as a
candidate: "日志名不复用加机器闸").

Why this exists: every row of the ledger that says "(日志 X.txt)" is promising the reviewer a file
they can open. Two of those promises were broken by this project's own hand in the ninth round --
U44_probe_run2.txt and U44_probe_run3.txt were renamed mid-round (U-46 W-3) and the only thing that
kept the citations honest was somebody remembering to. A convention with no machine behind it is a
convention the next round can silently break, so this script is the machine.

Definition of a citation (this file is the only place it is written): the text 日志 followed by
whitespace and a path-like token ending in .txt or .log. The captured token is judged as written:

  1 EXACT    ROOT/<token> exists -- a citation that spelled out its own path.
  2 SCRATCH  <token> is a bare file name and some file of that name exists under a build/*_probe/
             scratch tree (searched recursively; u44's wt/ subtree is excluded -- it is a transient
             copy of the repository, so its .txt files are the repo's own tracked files, not logs).
             That directory-suffix convention is where every round's probe logs are written.
  3 MISSING  neither. This is what the gate judges.

Baseline口径, copied from NOCAND_BASELINE in src_anchor_inventory.py (sixth-round review S-2):
MISSING_BASELINE freezes what was already missing when the gate was registered. Only ADDED is red --
a citation this round introduced (or revived) that resolves to nothing. REMOVED is report-only, so
cleaning up an old broken citation can never be blocked by the gate that measures it.

Why the baseline is machine-local, stated rather than hidden: build/u44_probe/ is git-ignored, so
"present" describes this working machine, not the repository. On a fresh clone every cited log is
missing and every clone would exit 13 before configure -- a gate nobody can live with gets disabled,
which is worse than a weaker gate (same reasoning as the 1g/1h pre-existing-state baseline in
ledger_size_gate.py). So an absent scratch tree produces a DECLARED, VISIBLE skip: CITE-SKIP is
printed, is in the host's display filter, and exits 0. It is not a pass and not a silence.

What this gate does NOT prove, so the boundary stays visible: a name that is reused by OVERWRITING
an older log still resolves, so this gate reads it as present. Catching that would need the file
contents to be pinned per round, which is a different (much bigger) promise; it is recorded as the
unproven half in the ledger rather than claimed here.

Reproduce:
    python tools/log_citation_gate.py
    python tools/log_citation_gate.py --ledger <path>     (judge a copy)
    python tools/log_citation_gate.py --emit-baseline     (print a fresh paste block; still judges)

Exit: 0 = green or declared skip | 1 = ADDED citations (each printed as CITEADD) |
2 = ledger unreadable | 3 = script crash. Output is ASCII-only (cp936 console); paths go through esc.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inline_rewrite_check import LEDGER_REL  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The recorded scratch convention: every round's probes write into build/<something>_probe/.
SCRATCH_PARENT_REL = "build"
SCRATCH_SUFFIX = "_probe"
# The copy worktree inside u44's scratch: excluded from the SCRATCH resolution, see docstring.
SCRATCH_EXCLUDE_REL = os.path.join(SCRATCH_PARENT_REL, "u44_probe", "wt")

CITE = re.compile(u"日志\\s+([A-Za-z0-9_./\\\\-]+\\.(?:txt|log))")

# Frozen at ledger baseline commit fc12294 by this script's own --emit-baseline run
# (build/u44_probe/U47_citation_baseline.txt). Replace only by re-running that command and pasting
# its output; the counts on the INV citation_baseline= line are what a reviewer can replay.
# The one item below is not a broken promise: §3.42's table-3 header (docs line 4510 at fc12294 --
# `git show fc12294:docs/对标差距推进计划.md | sed -n '4510p'`) writes that citation as a RANGE
# (u38_A1..A10.raw.txt) standing for ten individually present files, so no single file has that name.
# It is frozen rather than judged because rewriting a table header is not what this gate is for.
MISSING_BASELINE = (
    "build/u38_probe/u38_A1..A10.raw.txt",
)


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def scratch_basenames():
    """Bare file names of every .txt/.log under build/*_probe/, minus the copy worktree.
    Returns None when there is no scratch tree at all (a fresh clone) -- the declared skip."""
    parent = os.path.join(ROOT, SCRATCH_PARENT_REL)
    if not os.path.isdir(parent):
        return None
    exclude_abs = os.path.normpath(os.path.join(ROOT, SCRATCH_EXCLUDE_REL))
    names, trees = set(), 0
    for entry in sorted(os.listdir(parent)):
        tree = os.path.join(parent, entry)
        if not (os.path.isdir(tree) and entry.endswith(SCRATCH_SUFFIX)):
            continue
        trees += 1
        for dirpath, dirnames, files in os.walk(tree):
            if os.path.normpath(dirpath) == exclude_abs:
                del dirnames[:]
                continue
            for name in files:
                if name.endswith((".txt", ".log")):
                    names.add(name)
    if trees == 0:
        return None
    return names, trees


def classify(text, names):
    """Split every citation in the ledger into EXACT / SCRATCH / MISSING, keyed as written."""
    exact, scratch, missing = set(), set(), set()
    for m in CITE.finditer(text):
        token = m.group(1).replace("\\", "/")
        if os.path.isfile(os.path.join(ROOT, token.replace("/", os.sep))):
            exact.add(token)
        elif names is not None and os.path.basename(token) in names:
            scratch.add(token)
        else:
            missing.add(token)
    return exact, scratch, missing


def main(argv):
    ledger_arg = None
    emit_baseline = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--emit-baseline":
            emit_baseline = True
        elif a == "--ledger" and i + 1 < len(argv):
            ledger_arg = argv[i + 1]
            i += 1
        else:
            print("ERROR unknown or incomplete option: %s" % esc(a))
            return 2
        i += 1

    rel = ledger_arg or LEDGER_REL
    abs_path = rel if os.path.isabs(rel) else os.path.join(ROOT, rel)
    try:
        with open(abs_path, encoding="utf-8", newline="") as handle:
            text = handle.read()
    except OSError as exc:
        print("ERROR ledger not readable: %s (%s)" % (esc(abs_path), esc(str(exc))))
        return 2

    print("CITE ledger=%s bytes=%d" % (esc(abs_path), len(text.encode("utf-8"))))
    state = scratch_basenames()
    if state is None:
        print("CITE-SKIP scratch tree absent (no %s/*%s directory exists here, and those trees are "
              "git-ignored, so a clone that has never built the self-proofs has none) -- every cited "
              "log would read missing, and judging that would stop a clean clone. DECLARED SKIP, not "
              "a pass: no citation was checked."
              % (SCRATCH_PARENT_REL, SCRATCH_SUFFIX))
        print("INV verdict=SKIP exit=0")
        return 0

    names, trees = state
    exact, scratch, missing = classify(text, names)
    citations = exact | scratch | missing
    print("CITE scratch_trees=%d scratch_files=%d citations_distinct=%d present_exact=%d "
          "present_scratch=%d missing=%d"
          % (trees, len(names), len(citations), len(exact), len(scratch), len(missing)))

    baseline = set(MISSING_BASELINE)
    added = sorted(missing - baseline)
    removed = sorted(baseline - missing)
    for token in added:
        print("CITEADD dir=ADDED cite=%s" % esc(token))
    for token in removed:
        print("CITEREMOVED dir=REMOVED cite=%s (report only)" % esc(token))
    print("INV citation_baseline=%d observed_missing=%d added=%d removed=%d (added=%d removed=%d "
          "cites)" % (len(baseline), len(missing), len(added), len(removed),
                      len(added), len(removed)))

    if emit_baseline:
        print("== fresh MISSING_BASELINE block (review before pasting) ==")
        for token in sorted(missing):
            print('    "%s",' % token)

    if added:
        print("INV findings=%d" % len(added))
        print("INV verdict=RED exit=1 (each CITEADD above is a 日志 citation the ledger makes and "
              "this machine cannot open)")
        return 1
    print("INV findings=0")
    print("INV verdict=GREEN exit=0 (the only assertion is missing-vs-MISSING_BASELINE, ADDED "
          "direction; REMOVED is report-only)")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    try:
        sys.exit(main(sys.argv))
    except Exception as exc:
        print("ERROR script failed: %s %s" % (type(exc).__name__,
                                              esc(str(exc))[:200]))
        sys.exit(3)

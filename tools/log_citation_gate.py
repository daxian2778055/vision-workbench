r"""Cited-log presence gate -- turns "a log the ledger cites must be openable" into something that
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

The second rule (U-54): a citation that resolves to a HOST RUN must carry its own exit code. Why it
is here: the seventeenth round found the ledger citing a host end-to-end run whose log had the verdict
lines and no exit code at all, because the command had been piped into tail -- a pipeline reports the
tail's status. That round delivered tools/ci_host_run.ps1, which writes the child's code into the same
bytes, but left the rule as a convention ("only write an rc for a log when the log itself carries that
line") with no machine behind it, and said plainly that a next round piping the host would hit nothing.
This is that machine.

  4 HOST-SHAPED is judged from the file's own bytes, not from prose: a line matching
    ^\s*\[OK\]\s+tests:\s*\d+/\d+\s+passed\s*$   (tools/ci.ps1:677) or
    ^\s*\[FAIL\]\s+tests failed \(                (tools/ci.ps1:659)
    -- the host's two verdict banners, so a log is host evidence because the host wrote into it.
  5 SELF-CODED is one line matching [HOST-RC] inner=<path> ci_exit_code=N wrapper_exit_code=N, the
    wrapper's contract line. A host-shaped citation that is not self-coded is a CITEHOSTADD finding.
    Only the token is asked for, not a location: a log may hold many host runs, and requiring the
    contract line at all -- rather than at line N -- is what the rule can actually check.

    Not one definition with the wrapper, on purpose: rule 4's banner count is \d+/\d+, while the
    wrapper's own verdict pattern (tools/ci_host_run.ps1:35, '\[OK\]\s+tests: [1-9][0-9]*/[1-9][0-9]*
    passed') refuses 'tests: 0/0 passed' as a run that never ran anything. The gap runs in the
    demanding direction -- every shape the wrapper calls a verdict is host-shaped here, plus one the
    wrapper throws away, and a 0/0 banner written by hand is precisely the kind of evidence worth
    asking for a code. So the two patterns are related as superset, not as equal, and arm T5 of
    build/u44_probe/u54_host_rc_teeth.py measures that relation on both sides' own text instead of
    asserting it here.

HOSTRC_BASELINE is the set of host-shaped citations that were already written before this rule
existed, frozen from this script's own --emit-baseline run on the delivered ledger. Same ADDED-only
direction as the presence rule: old evidence stays readable, a new host run must carry its code.

Why the baseline is machine-local, stated rather than hidden: build/u44_probe/ is git-ignored, so
"present" describes this working machine, not the repository. On a fresh clone every cited log is
missing and every clone would exit 13 before configure -- a gate nobody can live with gets disabled,
which is worse than a weaker gate (same reasoning as the 1g/1h pre-existing-state baseline in
ledger_size_gate.py). So an absent scratch tree produces a DECLARED, VISIBLE skip: CITE-SKIP is
printed, is in the host's display filter, and exits 0. It is not a pass and not a silence. The host
rule reads file contents, so it shares the same skip: with no scratch there is nothing to sniff, and
CITEHOST prints host_shaped=0 with the skip rather than pretending a clean judgement.

What this gate does NOT prove, so the boundary stays visible: a name that is reused by OVERWRITING
an older log still resolves, so this gate reads it as present. Catching that would need the file
contents to be pinned per round, which is a different (much bigger) promise; it is recorded as the
unproven half in the ledger rather than claimed here. The same declined promise has a second edge for
rule 5: a baselined token whose file is REPLACED by new bytes that are host-shaped and uncoded still
matches the baseline and is excused. Judging that would need the baseline pinned by content, and it is
registered the same way -- stated, not claimed.

Reproduce:
    python tools/log_citation_gate.py
    python tools/log_citation_gate.py --ledger <path>     (judge a copy)
    python tools/log_citation_gate.py --emit-baseline     (print both fresh paste blocks; still judges)

Exit: 0 = green or declared skip | 1 = ADDED citations (CITEADD) or host-shaped citations without the
contract line (CITEHOSTADD) | 2 = ledger unreadable | 3 = script crash. Output is ASCII-only (cp936
console); paths go through esc.
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

# Rule 4's patterns, transcribed from the host's own Write-Ok / Write-Err call sites (tools/ci.ps1:677
# and :659). A log is host evidence because the host wrote its verdict into it, so the judgement does
# not depend on any sentence around the citation.
HOST_BANNERS = (
    re.compile(r"^\s*\[OK\]\s+tests:\s*\d+/\d+\s+passed\s*$"),
    re.compile(r"^\s*\[FAIL\]\s+tests failed \("),
)
# Rule 5: the one line tools/ci_host_run.ps1 writes next to the child's own output.
HOST_RC_LINE = re.compile(r"\[HOST-RC\] inner=\S+ ci_exit_code=-?\d+ wrapper_exit_code=-?\d+")

# Host-shaped citations that were already written before rule 5 existed. Frozen by this script's own
# --emit-baseline run on the ledger delivered as bdeea2d (output in build/u44_probe/
# U54_cite_gate_prebaseline_2.txt: host_shaped=12 coded=1 uncoded=11, and the same three numbers from
# an independent reader -- build/u44_probe/u54_host_evidence_census.py, U54_host_evidence_census_3.txt).
# Replace only by re-running that command; the direction judged is ADDED, so this list can only shrink.
HOSTRC_BASELINE = (
    "build/g3_probe/ci_e2e_328.txt",
    "build/g3_probe/ci_e2e_final_328.txt",
    "build/u21_probe/ci_final.txt",
    "build/u28_probe/ci_run1.log",
    "build/u29_ci_run2.log",
    "build/u31_probe/ci_u31_run1.log",
    "build/u32_probe/ci_u36_run1.log",
    "build/u35_probe/ci_run1.log",
    "build/u35_probe/ci_run2.log",
    "build/w1_probe/ci_final.txt",
    "ci_e2e_green.txt",
)


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def scratch_basenames():
    """Bare file names of every .txt/.log under build/*_probe/, minus the copy worktree, plus the
    basename -> paths map those names came from (rule 5 has to open the file, so names alone are not
    enough). Returns None when there is no scratch tree at all (a fresh clone) -- the declared skip."""
    parent = os.path.join(ROOT, SCRATCH_PARENT_REL)
    if not os.path.isdir(parent):
        return None
    exclude_abs = os.path.normpath(os.path.join(ROOT, SCRATCH_EXCLUDE_REL))
    names, trees, by_name = set(), 0, {}
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
                    by_name.setdefault(name, []).append(os.path.join(dirpath, name))
    if trees == 0:
        return None
    return names, trees, {k: sorted(set(v)) for k, v in by_name.items()}


def resolve_paths(token, by_name):
    """Every file a citation could mean, under classify's own two rules. A bare name that lives under
    two scratch trees resolves to both -- rule 5 judges all of them, so an ambiguous name can only
    make the gate stricter, never excuse a finding by picking one."""
    paths = []
    if os.path.isfile(os.path.join(ROOT, token.replace("/", os.sep))):
        paths.append(os.path.normpath(os.path.join(ROOT, token.replace("/", os.sep))))
    paths.extend(by_name.get(os.path.basename(token.replace("\\", "/")), []))
    return sorted(set(paths))


def host_state(path):
    """(banner_hits, rc_line_hits) for one resolved file, read once. Undecodable bytes go through
    errors=replace: a log that cannot be decoded is not evidence of anything, and making it
    unclassifiable would let a corrupt file dodge the rule by being unreadable."""
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8", errors="replace")
    lines = text.split("\n")
    banners = sum(1 for ln in lines for pat in HOST_BANNERS if pat.match(ln))
    return banners, sum(1 for ln in lines if HOST_RC_LINE.search(ln))


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

    names, trees, by_name = state
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

    # Rule 5: of the citations that resolve, which ones the host itself wrote into -- and do they
    # carry the wrapper's contract line. Keyed by the token as the ledger wrote it, same as rule 3.
    host_shaped, coded, uncoded, ambiguous = set(), set(), set(), set()
    for token in sorted(exact | scratch):
        paths = resolve_paths(token, by_name)
        if len(paths) > 1:
            ambiguous.add(os.path.basename(token.replace("\\", "/")))
        banners = rc_lines = 0
        for path in paths:
            hits, coded_hits = host_state(path)
            banners += hits
            rc_lines += coded_hits
        if banners:
            host_shaped.add(token)
            (coded if rc_lines else uncoded).add(token)
    host_baseline = set(HOSTRC_BASELINE)
    host_added = sorted(uncoded - host_baseline)
    host_removed = sorted(host_baseline - uncoded)
    for token in host_added:
        print("CITEHOSTADD dir=ADDED cite=%s (host verdict banner(s) in the file, [HOST-RC] contract "
              "line absent)" % esc(token))
    for token in host_removed:
        print("CITEHOSTREMOVED dir=REMOVED cite=%s (report only)" % esc(token))
    print("CITEHOST host_shaped=%d coded=%d uncoded=%d host_baseline=%d host_added=%d host_removed=%d "
          "ambiguous_basenames=%d"
          % (len(host_shaped), len(coded), len(uncoded), len(host_baseline), len(host_added),
             len(host_removed), len(ambiguous)))

    if emit_baseline:
        print("== fresh MISSING_BASELINE block (review before pasting) ==")
        for token in sorted(missing):
            print('    "%s",' % token)
        print("== fresh HOSTRC_BASELINE block (review before pasting) ==")
        for token in sorted(uncoded):
            print('    "%s",' % token)

    findings = len(added) + len(host_added)
    if findings:
        print("INV findings=%d" % findings)
        print("INV verdict=RED exit=1 (a CITEADD is a 日志 citation the ledger makes and this machine "
              "cannot open; a CITEHOSTADD is a citation whose file the host itself wrote a verdict "
              "into and which carries no [HOST-RC] line, so its exit code was never measured)")
        return 1
    print("INV findings=0")
    print("INV verdict=GREEN exit=0 (two assertions: missing-vs-MISSING_BASELINE and "
          "host-shaped-without-contract-line-vs-HOSTRC_BASELINE, both ADDED direction; REMOVED is "
          "report-only)")
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

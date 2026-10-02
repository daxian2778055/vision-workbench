"""Thirteenth-round review S-3's control face: "读数相同" and "整份日志相同" are two rulers.

The reviewer measured the _1-vs-_2 pairs as whole files and found h3／ledger／abort NOT byte
identical, while the prose quoted them as if the whole log repeated. Both are true of different
objects: the judged lines (the ones a ledger cell quotes as its reading) held, the difference sat
on state lines -- the copy's sync labels, base_nl / cur_nl / delta / headroom, an md5, a detached
HEAD hash -- which are supposed to move with the state around the run. This script answers, per
pair, with two separate columns, and prints every differing line together with the token that lets
it be called state-bearing. A differing line that matches no token is a reading that moved: the
pair fails.

The second face answers the reviewer's first suggestion's parenthetical: how many fields the
[U47-OK] LK line printed, per citation log. "before the narrowing" is a field count, so a reader
following a commit message must be able to tell which logs carry 8 and which carry 10.

The state classifier is deliberately two-tier, and the tiers are not equally strong. Tier 1 is a
named token (the STATE list). Tier 2 is a bare 8-40 digit hex string, which excuses a differing line
even when no token matches. Which tier carried a line is not left implicit: the [LP-PAIR] line
prints excused_by= as one "line:token" entry per excused difference. Measured this round, all five
split pairs were carried by tier-1 tokens -- tokens like synced docs, copy baseline, baseline=[,
added=[, copy cannot be reset -- so tier 2 carried nothing in the default face at all. Tier 2's own
reach, and the fail-closed direction, were measured by construction in
build/u44_probe/u50_state_teeth.py: hex_only (two synthetic logs differing only in a 40-digit
string, no token) must pass, no_token (a differing line with neither a token nor hex) must red with
unexplained_diffs=1, reading_moved (a differing marker line that a token excuses from the diff
count) must red on reading_lines_identical=NO. Tier 2 stays a boundary, not a proof: a hash can also
sit on a line that carries a reading.

Two columns are printed because they license different sentences. A pair may be called byte
identical as a whole file only when it reports whole_identical=yes; pairs that report
reading_lines_identical=yes with whole_identical=no differ on state lines and may only be described
as carrying the same judged lines.

Run: python tools/probes/U50_logpair_identical_probe.py                     (both faces, defaults)
     python tools/probes/U50_logpair_identical_probe.py --pair <name|pathA|pathB> [...]
     python tools/probes/U50_logpair_identical_probe.py --lineage <log> [...]
Exit: 0 = every pair has identical verdict lines and every whole-file difference is state-only
      1 = a verdict line moved, a pair carries no verdict marker, or a differing line is unexplained
      2 = usage / an unreadable file. Reads only, writes nothing, takes no scratch lock.
"""
import io
import os
import re
import sys

MARKERS = (
    "[HC-READING]", "[HC-SUMMARY]",
    "[LT-READING]", "[LT-SUMMARY]",
    "[SW-READING]", "[AB-READING]", "[NEGB-READING]", "[PIN-READING]",
    "[U44-SUMMARY]", "[U47-SUMMARY]",
)

# Lines whose content is a function of the state the run happened to see, not of the thing judged.
STATE = (
    "synced docs", "copy baseline", "before injection", "injected", "still differs",
    "restored reading differs", "reusing existing copy", "copy cannot be reset",
    "copy reset fails", "main before", "copy footprint", "copy advanced",
    "headroom=", "sum=", "delta=", "cur_nl=", "base_nl=", "added=[", "citations=[",
    "baseline=[", "window=", "at E:", "detached ",
)
HEXISH = re.compile(r"\b[0-9a-f]{8,40}\b")
LK_PREFIX = "[U47-OK] LK "
FIELD = re.compile(r"\b[A-Za-z_][A-Za-z_0-9]*=")

PROBE_DIR = "build/u44_probe"

DEFAULT_PAIRS = (
    ("h3_teeth", "U49_h3_teeth_1.txt", "U49_h3_teeth_2.txt"),
    ("ledger_teeth", "U49_ledger_teeth_1.txt", "U49_ledger_teeth_2.txt"),
    ("sweep_legs", "U49_sweep_legs_1.txt", "U49_sweep_legs_2.txt"),
    ("abort_check", "U49_abort_check_1.txt", "U49_abort_check_2.txt"),
    ("banner_negative", "U49_banner_negative_1.txt", "U49_banner_negative_2.txt"),
    ("pin_check", "U49_pin_check_1.txt", "U49_pin_check_2.txt"),
    ("probe_u44_12", "U49_probe_u44_1.txt", "U49_probe_u44_2.txt"),
    ("probe_u44_67", "U49_probe_u44_6.txt", "U49_probe_u44_7.txt"),
    ("citation_34", "U49_citation_probe_3.txt", "U49_citation_probe_4.txt"),
    ("citation_56", "U49_citation_probe_5.txt", "U49_citation_probe_6.txt"),
    ("citation_78", "U49_citation_probe_7.txt", "U49_citation_probe_8.txt"),
    ("citation_35_cross", "U49_citation_probe_3.txt", "U49_citation_probe_5.txt"),
    ("citation_46_cross", "U49_citation_probe_4.txt", "U49_citation_probe_6.txt"),
)

DEFAULT_LINEAGE = (
    "U48_citation_probe_10.txt", "U48_citation_probe_11.txt",
    "U49_citation_probe_1.txt", "U49_citation_probe_2.txt",
    "U49_citation_probe_3.txt", "U49_citation_probe_4.txt",
    "U49_citation_probe_5.txt", "U49_citation_probe_6.txt",
    "U49_citation_probe_7.txt", "U49_citation_probe_8.txt",
    "U50_citation_probe_1.txt",
)


def read_lines(path):
    return io.open(path, encoding="utf-8", errors="replace").read().splitlines()


def verdict_lines(lines):
    return [ln for ln in lines if ln.startswith(MARKERS)]


def state_token(line):
    """Which token makes this line state-bearing, or None when nothing does."""
    for tok in STATE:
        if tok in line:
            return tok
    hit = HEXISH.search(line)
    return "hex %s" % hit.group(0)[:12] if hit else None


def check(name, path_a, path_b):
    a, b = read_lines(path_a), read_lines(path_b)
    va, vb = verdict_lines(a), verdict_lines(b)
    if not va or not vb:
        print("[LP-HOLD] %s: one member carries none of the verdict markers (%s=%d %s=%d)"
              % (name, os.path.basename(path_a), len(va), os.path.basename(path_b), len(vb)))
        return 1, False, False
    bad = []
    excused = []
    for n in range(min(len(a), len(b))):
        if a[n] == b[n]:
            continue
        ta, tb = state_token(a[n]), state_token(b[n])
        if ta is None or tb is None:
            bad.append((n + 1, ta, tb, a[n], b[n]))
        else:
            excused.append("%d:%s" % (n + 1, ta))
    if len(a) != len(b):
        bad.append((min(len(a), len(b)) + 1, None, None, "<len %d>" % len(a), "<len %d>" % len(b)))
    diff_lines = len(bad) + len(excused)
    excused_txt = ",".join(excused[:8]) + (" +%d" % (len(excused) - 8) if len(excused) > 8 else "")
    reading_same = va == vb
    whole = a == b
    verdict = "OK" if reading_same and not bad else "HOLD"
    print("[LP-PAIR] %s a=%s b=%s lines=%d/%d whole_identical=%s diff_lines=%d verdict_lines=%d "
          "reading_lines_identical=%s unexplained_diffs=%d excused_by=%s verdict=%s"
          % (name, os.path.basename(path_a), os.path.basename(path_b), len(a), len(b),
             "yes" if whole else "no", diff_lines, len(va), "yes" if reading_same else "NO",
             len(bad), excused_txt or "-", verdict))
    for n, ta, tb, la, lb in bad[:8]:
        print("   [LP-DIFF-UNEXPLAINED] line %d: %r vs %r  (state tokens: %r / %r)"
              % (n, la[:110], lb[:110], ta, tb))
    if not reading_same:
        moved = [(x, y) for x, y in zip(va, vb) if x != y]
        for la, lb in moved[:8]:
            print("   [LP-READING-MOVED] %r vs %r" % (la[:110], lb[:110]))
    return (0 if verdict == "OK" else 1), reading_same, whole


def lineage(path):
    lk = [ln for ln in read_lines(path) if ln.startswith(LK_PREFIX)]
    if not lk:
        print("[LP-LINEAGE] %s carries no %sline" % (os.path.basename(path), LK_PREFIX))
        return 1
    line = lk[-1]
    # Two honest counts of the same line. The ledger's "eight fields" is the word count of the
    # reading itself -- it drops the "[U47-OK]" label but keeps the leg name, giving 8 before the
    # narrowing and 10 after. A key=value field count is one lower (7 and 9). Print both.
    fields = len(FIELD.findall(line[len(LK_PREFIX):]))
    words = len(line.split()[1:])
    kv = dict(re.findall(r"\b([A-Za-z_][A-Za-z_0-9]*)=([^\s]+)", line))
    print("[LP-LINEAGE] %s lk_fields=%d lk_words=%d drivers=%s unlocked=%s "
          "unlock_only_bites=%s wide_face_lets_unlock=%s"
          % (os.path.basename(path), fields, words,
             kv.get("drivers", "-"), kv.get("unlocked", "-"),
             "yes" if "unlock_only_bites=" in line else "no",
             "yes" if "wide_face_lets_unlock=" in line else "no"))
    return 0


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))).replace("\\", "/")
    argv = sys.argv[1:]
    mode = "both"
    if argv and argv[0] in ("--pair", "--lineage"):
        mode = argv[0][2:]
        argv = argv[1:]
    elif argv:
        print("[LP-USE] bare run takes no arguments, pass each face through --pair / --lineage")
        return 2

    rc, pairs, reading_same_n, whole_n = 0, 0, 0, 0
    if mode in ("both", "pair"):
        items = []
        for arg in argv:
            parts = arg.split("|")
            if len(parts) != 3:
                print("[LP-USE] expected name|pathA|pathB, got %r" % arg)
                return 2
            items.append((parts[0], parts[1], parts[2]))
        if not items:
            items = [(n, os.path.join(root, PROBE_DIR, pa), os.path.join(root, PROBE_DIR, pb))
                     for n, pa, pb in DEFAULT_PAIRS]
        for name, pa, pb in items:
            for p in (pa, pb):
                if not os.path.isfile(p):
                    print("[LP-USE] unreadable pair member: %s" % p)
                    return 2
            r, same, whole = check(name, pa, pb)
            rc |= r
            pairs += 1
            reading_same_n += 1 if same else 0
            whole_n += 1 if whole else 0

    if mode in ("both", "lineage"):
        logs = argv or [os.path.join(root, PROBE_DIR, f) for f in DEFAULT_LINEAGE]
        for p in logs:
            if not os.path.isfile(p):
                print("[LP-USE] unreadable lineage log: %s" % p)
                return 2
            r = lineage(p)
            rc |= r

    print("[LP-SUMMARY] pairs=%d reading_lines_identical=%d whole_file_identical=%d "
          "state_only_diff_pairs=%d verdict=%s"
          % (pairs, reading_same_n, whole_n, pairs - whole_n,
             "SPLIT CONFIRMED" if rc == 0 else "A READING MOVED OR AN UNEXPLAINED DIFF"))
    return rc


if __name__ == "__main__":
    sys.exit(main())

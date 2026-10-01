"""Ledger growth split-trigger gate -- turns the recorded "more than +88 lines in one round must
open the split-the-file project first" trigger into something that can actually stop a run
(seventh-round review S-2 and S-3).

Why this exists: tools/inline_rewrite_check.py already computes the ledger's own line growth and
prints "split_project_due=YES/NO", but that script's published contract is REPORT_ONLY exit=0 and
the ledger quotes those readings verbatim, so giving it a red switch would make registered readings
unreplayable. This script is the judgement half: same constants (imported, not re-typed), and it
exits non-zero when the split is due. Seventh-round review S-2 named exactly this gap:
"split_project_due only prints, it never judges -- give it its own host code so 该拆没拆 can stop
one end-to-end run."

Three conditions, all measured from git output, none carrying state a later round could reset:

  1 BURST   this round's net line growth of the ledger (worktree vs HEAD) > SPLIT_TRIGGER_DELTA.
            The recorded trigger, unchanged; the shape the ledger already agreed to.
  2 RATE    the newest up to 3 counted rounds, summed, > 200 lines. Seventh-round review S-3's
            parallel condition, added because a burst rule alone stops an explosion but not a
            steady crawl -- it measured 80/86/50/68 passing one under 88 every round.
  3 SIZE    the ledger's absolute line count > LINES_CEILING. The one quantity that no windowing,
            anchoring or re-phrasing can move: a file that is too big is too big.

Why condition 2 counts rounds from REGISTERED_ANCHOR and not over all of history. Measured on the
committed history in this round: the last three rounds sum to 204 net lines, so a history-wide
version of the rule is ALREADY violated today, before this gate exists. Making that a hard red would
exit 12 before configure on every later run, including a plain local build -- and a gate nobody can
live with gets disabled, which is worse than a weaker gate. The repo's established shape for
"pre-existing state violates a new rule" is the 1g/1h baseline: freeze what is there at registration
and make growth red. So the window starts after a pinned commit -- the threshold itself stays exactly
the reviewer's 200, it is the *start* that is registered -- and the history-wide sum is printed every
run as INFORMATIONAL so the debt stays visible and nobody can read this as "the steady growth was
fine".

The ceiling is a decision, and its basis is printed rather than hidden: measured mean net growth over
the last eight committed rounds is 78.1 lines/round, so 6000 leaves about 13 rounds at that rate
before the file must be split for size.

Sizes are newline counts of the bytes (nl), matching inline_rewrite_check's LEDGER_GROWTH line; that
is one less than the split-count line numbers the FILE lines print, and the difference is stated here
rather than in the ledger.

Reproduce:
    python tools/ledger_size_gate.py
    python tools/ledger_size_gate.py --ledger <path>        (judge a copy)
    python tools/ledger_size_gate.py --base-lines <n>       (what the probe injects)
Overrides are printed on the line INV overrides_used=, so a bypass can never be silent.

Exit: 0 = not due | 1 = due (each fired condition printed as SPLITDUE) | 2 = git or the ledger
unreadable | 3 = script crash. Output is ASCII-only (cp936 console); paths go through esc().
"""
import io
import os
import subprocess
import sys

# Keep the constants in one place: this gate and the report-only inventory must never drift apart.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inline_rewrite_check import LEDGER_REL, SPLIT_TRIGGER_DELTA  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Rounds counted for condition 2 start after this commit -- the last commit before this gate existed.
REGISTERED_ANCHOR = "5ce4449"
# Seventh-round review S-3's suggested parallel condition, adopted as written: 3 rounds > 200 lines.
RATE_WINDOW_ROUNDS = 3
RATE_LIMIT_LINES = 200
# Condition 3: a decision, with the measured basis stated in the docstring above.
LINES_CEILING = 6000
# How many recent commits are scanned for per-round numstat (generously more than the window).
SCAN_COMMITS = 12


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def git(args, stdin_bytes=None):
    proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    out, err = proc.communicate(stdin_bytes)
    return proc.returncode, out, err


def unquote(path_bytes):
    """Git's numstat prints non-ASCII paths as a C-style quoted, octal-escaped string."""
    s = path_bytes.strip()
    if len(s) >= 2 and s.startswith(b'"') and s.endswith(b'"'):
        body = s[1:-1]
        out, i = bytearray(), 0
        while i < len(body):
            if body[i:i + 1] == b"\\" and body[i + 1:i + 4].isdigit():
                out.append(int(body[i + 1:i + 4], 8))
                i += 4
            else:
                out.append(body[i])
                i += 1
        return bytes(out)
    return s


def blob_nl(rev, rel_path):
    """Newline count of a file at a revision, or None when it is not readable."""
    spec = ("%s:%s" % (rev, rel_path)).encode("utf-8")
    rc, out, _err = git(["git", "cat-file", "--batch-check"], spec + b"\n")
    if rc != 0:
        return None
    parts = out.decode("ascii", "replace").split()
    if len(parts) < 2 or parts[1] != "blob":
        return None
    rc2, data, _err2 = git(["git", "cat-file", "blob", parts[0]])
    if rc2 != 0:
        return None
    return data.count(b"\n")


def rounds_touching(rel_bytes, rev_range=None):
    """[(sha7, net_lines)], newest first, for commits that changed this path.

    The revision range travels as an ASCII argv; the Chinese path is never put on the command line,
    it is matched here after unquoting git's own numstat output.
    """
    args = ["git", "log", "--format=COMMIT %h", "-%d" % SCAN_COMMITS]
    if rev_range:
        args.append(rev_range)
    args.append("--numstat")
    rc, out, _err = git(args)
    if rc != 0:
        return None
    sha, rows = None, []
    for raw in out.split(b"\n"):
        if raw.startswith(b"COMMIT "):
            sha = raw[7:].strip().decode("ascii", "replace")
            continue
        if b"\t" not in raw:
            continue
        parts = raw.split(b"\t")
        if len(parts) < 3 or unquote(parts[2]) != rel_bytes:
            continue
        try:
            added, deleted = int(parts[0]), int(parts[1])
        except ValueError:
            continue  # a binary diff prints "-" for both counts
        rows.append((sha, added - deleted))
    return rows


def rate_window(delta, head_sha, after_anchor):
    """Condition 2's arithmetic, kept pure so a probe can drive synthetic round sequences through it
    (a real repository only ever offers the rounds it actually has).

    The round in progress counts first when it moved the ledger; a clean worktree contributes nothing.
    Needs two rounds to say anything: one round is already covered by the burst rule, and reading a
    single round through the rate window would double-report the same finding.
    """
    seq = ([(head_sha, delta)] if delta != 0 else []) + list(after_anchor)
    window = seq[:RATE_WINDOW_ROUNDS]
    total = sum(net for _s, net in window)
    return window, total, len(window) >= 2 and total > RATE_LIMIT_LINES


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    argv = sys.argv[1:]
    ledger_arg, base_lines_arg = None, None
    overrides = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--ledger", "--base-lines") and i + 1 < len(argv):
            value = argv[i + 1]
            if a == "--ledger":
                ledger_arg = value
            else:
                try:
                    base_lines_arg = int(value)
                except ValueError:
                    print("ERROR --base-lines wants a number, got: %s" % esc(value))
                    return 2
            overrides.append("%s=%s" % (a[2:], value))
            i += 2
        else:
            print("ERROR unknown or incomplete option: %s" % esc(a))
            return 2

    # The judged file. --ledger is the read side of the same switch: without it a probe copy could
    # inject a break that the gate never looks at.
    abs_path = os.path.join(ROOT, (ledger_arg or LEDGER_REL).replace("/", os.sep))
    if not os.path.isfile(abs_path):
        print("ERROR ledger not readable: %s" % esc(abs_path))
        return 2
    with io.open(abs_path, "rb") as handle:
        cur_nl = handle.read().count(b"\n")

    rc, out, _err = git(["git", "rev-parse", "--verify", "HEAD^{commit}"])
    if rc != 0:
        print("ERROR HEAD unreadable")
        return 2
    head_sha = out.decode("ascii").strip()[:12]

    if base_lines_arg is not None:
        base_nl = base_lines_arg
    else:
        base_nl = blob_nl("HEAD", ledger_arg or LEDGER_REL)
        if base_nl is None:
            print("ERROR ledger not readable at HEAD: %s" % esc(ledger_arg or LEDGER_REL))
            return 2

    delta = cur_nl - base_nl
    burst_due = delta > SPLIT_TRIGGER_DELTA

    # Condition 2: the newest rounds, counting the round in progress first, from the pinned anchor on.
    rel_bytes = (ledger_arg or LEDGER_REL).encode("utf-8")
    after_anchor = rounds_touching(rel_bytes, "%s..HEAD" % REGISTERED_ANCHOR)
    committed = rounds_touching(rel_bytes)
    if after_anchor is None or committed is None:
        print("ERROR git log --numstat unreadable (anchor=%s)" % REGISTERED_ANCHOR)
        return 2
    window, rate_sum, rate_due = rate_window(delta, head_sha, after_anchor)

    history3 = sum(net for _s, net in committed[:RATE_WINDOW_ROUNDS])
    size_due = cur_nl > LINES_CEILING

    fired = []
    if burst_due:
        fired.append("burst")
    if rate_due:
        fired.append("rate")
    if size_due:
        fired.append("size")

    print("INV ledger=%s" % esc(rel_bytes.decode("utf-8")))
    print("INV head=%s base_nl=%d cur_nl=%d delta=%d burst_limit=%d burst_due=%s"
          % (head_sha, base_nl, cur_nl, delta, SPLIT_TRIGGER_DELTA, "YES" if burst_due else "NO"))
    print("INV rounds_counted=%d window=%s sum=%d rate_limit=%d rate_due=%s"
          % (len(window), esc(",".join("%s:%+d" % (s, n) for s, n in window) or "none"),
             rate_sum, RATE_LIMIT_LINES, "YES" if rate_due else "NO"))
    print("INV anchor=%s committed_after_anchor=%d scanned_commits=%d"
          % (REGISTERED_ANCHOR, len(after_anchor), len(committed)))
    print("INV history_only_last3=%d rate_limit=%d (INFORMATIONAL, not judged: that growth predates "
          "this gate -- see the docstring)" % (history3, RATE_LIMIT_LINES))
    print("INV ceiling=%d size_due=%s headroom=%d"
          % (LINES_CEILING, "YES" if size_due else "NO", LINES_CEILING - cur_nl))
    if overrides:
        print("INV overrides_used=%s" % esc(" ".join(overrides)))
    else:
        print("INV overrides_used=none")
    for name in fired:
        print("SPLITDUE condition=%s -- the split-the-file project is due; per the recorded trigger "
              "it must open before another round appends to this ledger" % name)
    print("INV split_project_due=%s fired=%s"
          % ("YES" if fired else "NO", esc(",".join(fired) or "none")))
    print("INV verdict=%s" % ("RED" if fired else "GREEN"))
    return 1 if fired else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # a crash must not read as "not due"
        print("ERROR script failed: %s %s" % (type(exc).__name__, esc(str(exc))[:200]))
        sys.exit(3)

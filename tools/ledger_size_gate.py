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

Why all three conditions read the FAMILY from U-60 on (the split-the-file project's first cut).
U-60 stopped re-carrying the cumulative register into every new ledger block and moved it to
docs/对标差距推进计划-登记附表.md. Measured before that cut (build/u44_probe/u60_split_census.py,
log U60_split_census_2.txt): the last four blocks held 13/18/23/31 register rows, and the newest
edge re-emitted 21 of them byte-identically while rewriting 2 in place -- so a block's size was
mostly a copy of the previous block's copy. If the gate kept judging one path, moving those rows
to a sibling file would have halved every reading without writing one line less. So burst, rate and
size all read the sum over the family, which is the only quantity a cut cannot vote on: +d on one
member and -d on the other leave the family sum, and therefore this judgement, exactly as hard as
before. LINES_CEILING still means "the whole plan is too big", now measured on the family total,
and each member's own line count is printed on its INV member= line so the per-file half of the
old rationale stays visible rather than being traded away. --no-register runs a declared
single-member family (the boundary arms of tools/probes/U44_geometry_split_probe.py use it) and
says so on INV overrides_used=.

Why there is a FOURTH judgement, and why it is a refusal rather than a fourth SPLITDUE. The U-60
review's W-2 measured what the family sum does NOT close: summing the members makes "move d rows from
one file into the other" vote nothing, but nothing ever looked at a round that DELETES rows. Before
this leg the only deletion-shaped answer was the vanished-member refusal in main() ("a member is only
ever left out by name"), and the tool that does count deletions -- tools/inline_rewrite_check.py's
delete= reading -- publishes REPORT_ONLY exit=0 and the ledger quotes its lines verbatim, so it has no
red switch either. A family net of +2 built from ledger +5 and register -3 therefore passed every
condition while quietly dropping registered rows. So now: any judged member whose worktree line count
is BELOW its own HEAD blob is refused (exit 2, one ERROR line, no INV reading and no verdict printed)
unless the run declares the cut with --allow-shrink=<reason>, echoed on INV overrides_used= like every
other override. Per member and not on the family sum is the whole point -- a member-level floor is
exactly what the family sum cannot see. A member with no base blob (the round that creates it) has
nothing to lose: it is printed as new and can never read as a shrink.

Why the refusal channel instead of the growth one: a shrink is not "the split is due", and exit 2
already means "this gate declines to judge" for an unreadable ledger and for a vanished member. It is
also why nothing in tools/ci.ps1 was touched -- step 1j maps ANY non-zero from this script onto host
exit 12 and echoes the script's own code, U44's M2 arm already proves that mapping for code 2, and
editing that step's comment would move CITEHOSTDEF's marker_digest, which twelve ledger rows quote
verbatim (measured before choosing: 12 hits for b57315efc59f in the ledger).

Reproduce:
    python tools/ledger_size_gate.py
    python tools/ledger_size_gate.py --ledger <path>        (judge a copy as the ledger member)
    python tools/ledger_size_gate.py --register <path>      (judge a copy as the register member)
    python tools/ledger_size_gate.py --no-register          (declared single-member family)
    python tools/ledger_size_gate.py --base-lines <n>       (what the probe injects: the family base)
    python tools/ledger_size_gate.py --allow-shrink <text>  (declare a deliberate family cut)
Overrides are printed on the line INV overrides_used=, so a bypass can never be silent.

Exit: 0 = not due | 1 = due (each fired condition printed as SPLITDUE) | 2 = git or the ledger
unreadable, a tracked member missing, or a judged member that lost lines against HEAD without a
declaration | 3 = script crash. Output is ASCII-only (cp936 console); paths go through esc().
"""
import io
import os
import subprocess
import sys

# Keep the constants in one place: this gate and the report-only inventory must never drift apart.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inline_rewrite_check import LEDGER_REL, REGISTER_REL, SPLIT_TRIGGER_DELTA  # noqa: E402

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
    """[(sha7, net_lines)], newest first, for commits that changed this path."""
    return _rounds({rel_bytes}, rev_range)


def family_rounds(rel_bytes_list, rev_range=None):
    """Same list, but each commit's net is the SUM over every family member it changed.

    This is what the split trigger is judged on: a round that moves rows out of one member into
    another nets the same on the family, so no cut can vote on the window by itself.
    """
    return _rounds(set(rel_bytes_list), rev_range)


def _rounds(rel_set, rev_range=None):
    """[(sha7, net_lines)] over rel_set, newest first, from git's own commit order.

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
    order, acc, touched, sha = [], {}, set(), None
    for raw in out.split(b"\n"):
        if raw.startswith(b"COMMIT "):
            sha = raw[7:].strip().decode("ascii", "replace")
            if sha not in acc:
                acc[sha] = 0
                order.append(sha)
            continue
        if b"\t" not in raw or sha is None:
            continue
        parts = raw.split(b"\t")
        if len(parts) < 3 or unquote(parts[2]) not in rel_set:
            continue
        try:
            added, deleted = int(parts[0]), int(parts[1])
        except ValueError:
            continue  # a binary diff prints "-" for both counts
        acc[sha] += added - deleted
        touched.add(sha)
    return [(s, acc[s]) for s in order if s in touched]


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
    ledger_arg, register_arg, base_lines_arg = None, None, None
    allow_shrink = None
    drop_register = False
    overrides = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--ledger", "--register", "--base-lines", "--allow-shrink") and i + 1 < len(argv):
            value = argv[i + 1]
            if a == "--ledger":
                ledger_arg = value
            elif a == "--register":
                register_arg = value
            elif a == "--allow-shrink":
                if not value.strip():
                    print("ERROR --allow-shrink wants a non-empty reason")
                    return 2
                allow_shrink = value
            else:
                try:
                    base_lines_arg = int(value)
                except ValueError:
                    print("ERROR --base-lines wants a number, got: %s" % esc(value))
                    return 2
            overrides.append("%s=%s" % (a[2:], value))
            i += 2
        elif a == "--no-register":
            drop_register = True
            overrides.append("no_register=declared")
            i += 1
        else:
            print("ERROR unknown or incomplete option: %s" % esc(a))
            return 2

    # The judged family. --ledger and --register are the read sides of the same switches: without
    # them a probe copy could inject a break the gate never looks at. --no-register is DECLARED,
    # printed on the overrides line, and is how a single-member family is run on purpose.
    members = [("ledger", ledger_arg or LEDGER_REL)]
    if not drop_register:
        members.append(("register", register_arg or REGISTER_REL))

    cur_total = 0
    member_read = []
    for label, rel in members:
        abs_path = os.path.join(ROOT, rel.replace("/", os.sep))
        if not os.path.isfile(abs_path):
            if label == "ledger":
                print("ERROR ledger not readable: %s" % esc(abs_path))
                return 2
            # The one absence a member may still have before the round that creates it lands: not on
            # disk AND not in HEAD, so there was nothing yet to sum and nobody can have taken it.
            # Once HEAD carries it, absence on disk is a deletion -- the run would then sum one file
            # while still printing family_members_judged=2 and go GREEN, which is exactly the escape
            # the family sum exists to close, and unlike --no-register nothing declared it. Refuse;
            # --no-register stays the only way to judge a smaller family on purpose.
            if blob_nl("HEAD", rel) is not None:
                print("ERROR family member not readable although HEAD tracks it: label=%s path=%s "
                      "(a member is only ever left out by name, on --no-register)"
                      % (label, esc(rel)))
                return 2
            cur_total += 0
            member_read.append((label, rel, None, "absent"))
            continue
        with io.open(abs_path, "rb") as handle:
            nl = handle.read().count(b"\n")
        cur_total += nl
        member_read.append((label, rel, nl, "present"))

    rc, out, _err = git(["git", "rev-parse", "--verify", "HEAD^{commit}"])
    if rc != 0:
        print("ERROR HEAD unreadable")
        return 2
    head_sha = out.decode("ascii").strip()[:12]

    # Every judged member's own base, read whether or not the family base below is injected: the
    # shrink leg has to answer for the bytes in front of it, and an injected family base must not be
    # able to hide a member that lost lines.
    base_by_label = dict((label, blob_nl("HEAD", rel)) for label, rel, _c, _s in member_read)

    if base_lines_arg is not None:
        base_total = base_lines_arg
        base_mode = "injected"
    else:
        base_total, base_mode = 0, "measured"
        for label, rel, _cur, _state in member_read:
            nl = base_by_label[label]
            if nl is None:
                # a family member that does not exist at HEAD yet is a creation, not a read failure
                continue
            base_total += nl

    delta = cur_total - base_total
    burst_due = delta > SPLIT_TRIGGER_DELTA

    # The fourth judgement: a judged member that lost lines against its own HEAD blob. Per member, on
    # purpose -- see the docstring: the family sum is what a move hides behind, and it hides a deletion
    # just as well when another member grew more than the deletion took away.
    shrink_parts, shrunk, member_net = [], [], 0
    for label, rel, cur, state in member_read:
        if state == "absent":
            shrink_parts.append("%s:absent" % label)
            continue
        base_nl = base_by_label[label]
        if base_nl is None:
            shrink_parts.append("%s:new" % label)
            continue
        shrink_parts.append("%s:%+d" % (label, cur - base_nl))
        member_net += cur - base_nl
        if cur < base_nl:
            shrunk.append((label, base_nl, cur))
    if shrunk and not allow_shrink:
        print("ERROR undeclared family shrink: %s -- a judged member holds fewer lines than its own "
              "HEAD blob. The family sum cannot see this on its own (another member can have grown "
              "more), and the plan's own rule is that old rows stay on the page with an in-place note, "
              "so a cut has to be declared with --allow-shrink=<reason>, which prints on "
              "overrides_used=. Nothing was judged here and no verdict was printed."
              % esc(" ; ".join("%s base_nl=%d cur_nl=%d delta=%+d"
                               % (l, b0, c0, c0 - b0) for l, b0, c0 in shrunk)))
        return 2

    # Condition 2: the newest rounds, counting the round in progress first, from the pinned anchor on.
    rel_bytes = [rel.encode("utf-8") for _label, rel, _c, _s in member_read]
    after_anchor = family_rounds(rel_bytes, "%s..HEAD" % REGISTERED_ANCHOR)
    committed = family_rounds(rel_bytes)
    if after_anchor is None or committed is None:
        print("ERROR git log --numstat unreadable (anchor=%s)" % REGISTERED_ANCHOR)
        return 2
    window, rate_sum, rate_due = rate_window(delta, head_sha, after_anchor)

    history3 = sum(net for _s, net in committed[:RATE_WINDOW_ROUNDS])
    size_due = cur_total > LINES_CEILING

    fired = []
    if burst_due:
        fired.append("burst")
    if rate_due:
        fired.append("rate")
    if size_due:
        fired.append("size")

    print("INV ledger=%s" % esc(ledger_arg or LEDGER_REL))
    print("INV register=%s family_members_judged=%d%s"
          % (esc(register_arg or REGISTER_REL),
             len(member_read), " (--no-register: the register member was DECLARED out)"
             if drop_register else ""))
    for label, rel, nl, state in member_read:
        print("INV member=%s path=%s cur_nl=%s state=%s"
              % (label, esc(rel), "none" if nl is None else nl, state))
    print("INV shrink=%s member_net=%+d due=%s declared=%s"
          % (",".join(shrink_parts) or "none", member_net,
             "DECLARED" if shrunk else "NO",
             esc(allow_shrink) if allow_shrink else "none"))
    print("INV head=%s base_nl=%d cur_nl=%d delta=%d base_mode=%s burst_limit=%d burst_due=%s"
          % (head_sha, base_total, cur_total, delta, base_mode, SPLIT_TRIGGER_DELTA,
             "YES" if burst_due else "NO"))
    print("INV rounds_counted=%d window=%s sum=%d rate_limit=%d rate_due=%s"
          % (len(window), esc(",".join("%s:%+d" % (s, n) for s, n in window) or "none"),
             rate_sum, RATE_LIMIT_LINES, "YES" if rate_due else "NO"))
    print("INV anchor=%s committed_after_anchor=%d scanned_commits=%d"
          % (REGISTERED_ANCHOR, len(after_anchor), len(committed)))
    print("INV history_only_last3=%d rate_limit=%d (INFORMATIONAL, not judged: that growth predates "
          "this gate -- see the docstring)" % (history3, RATE_LIMIT_LINES))
    print("INV ceiling=%d size_due=%s headroom=%d"
          % (LINES_CEILING, "YES" if size_due else "NO", LINES_CEILING - cur_total))
    if overrides:
        print("INV overrides_used=%s" % esc(" ".join(overrides)))
    else:
        print("INV overrides_used=none")
    for name in fired:
        print("SPLITDUE condition=%s -- the split-the-file project is due; per the recorded trigger "
              "it must open before another round appends to this ledger family" % name)
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

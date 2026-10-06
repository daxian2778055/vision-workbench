"""U-44 replayable probe: the two gates wired in by the seventh-round review must be able to stop
the host, and the wiring itself must be checked.

What it proves (advance plan 3.48):

  S-1  ci.ps1 step 1i calls tools/ci_exit_code_check.py, whose job is to keep "this script's red is
       mapped onto exactly one documented host code" from rotting. U-43 proved that mapping once by
       hand; nothing ran the proof again, which is the same dead-gate shape one layer down. So this
       probe checks the checker (legs G-A..G-F, each injecting ONE defect into a temp copy of ci.ps1
       and asserting the finding names the expected leg), and it checks the mapping end to end (H1).
  S-2  step 1j calls tools/ledger_size_gate.py, which judges the recorded split trigger instead of
       only printing it. H2 grows the copy's ledger past the burst limit and expects the host to stop
       with exit 12 before configure. From U-60 that trigger is judged on the ledger FAMILY -- the plan
       plus the cumulative register that moved out of it in the first cut of the split-the-file project
       -- and L5 is the leg that keeps the family sum honest rather than a way to halve a reading.
       The family sum cannot see a member that LOST lines while another gained more, which is what the
       U-60 review's W-2 named; L6 judges the per-member shrink refusal added for it, on exit 2 -- the
       same channel M2 proves maps to host code 12, so no new host code and no ci.ps1 byte moved.
  S-3  the same script carries the rate condition (3 rounds > 200 lines) and the absolute size
       ceiling; their arithmetic is driven through rate_window() with synthetic round sequences (L3),
       because a real repository only offers the rounds it actually has.

Arms:

  G0  checker baseline      delivered ci.ps1 -> rc=0, findings=0, header codes 0..12, and the frozen
                            per-code counts printed exactly as pinned here
  G-A stop without a row    delete the header row for 11 -> exactly one finding, and it is leg A
  G-B row without a stop    add a header row for a code nothing returns -> exactly one finding, leg B
  G-C code stolen           a second exit 11 inside step 1j's region -> leg C counts AND leg E location
  G-D heading renamed       rename step 1i's Write-Step text -> exactly one finding, leg D
  G-E stop removed          step 1h's exit 10 replaced by a printed warning (the silent-skip shape
                            S-1 named) -> leg C reads "code 10 fires 0", leg B reads row 10 unwired
  G-F code repurposed       step 1h's exit 10 changed to exit 11 -> legs B/C/E all fire on one defect
  G-M unreadable file       --path <missing> -> rc=2, no GEOMETRY line (a broken input is not drift
                            and not clean)
  L0  gate baseline         delivered bytes -> rc=0, and its history_only_last3 reading agrees with the
                            SAME window git selects itself (the newest rounds that touched either
                            member, inside the gate's own scan bound) -- triangular over three
                            arithmetic paths: the gate's printed reading, one range numstat naming BOTH
                            members over that span, and the sum of each selected round's own numstat
  L1  burst boundary        --base-lines derived from the gate's own cur_nl (delta 88 / delta 89):
                            burst off at 88, on at 89; fired list, SPLITDUE lines and exit code must
                            all follow from the three condition flags of that same run, and that run
                            must still be judging two members
  L3  rate arithmetic       rate_window() on synthetic sequences: one round not judged, 190 green,
                            200 green (strict >), 210 red, a 4th round drops out of the window -- plus
                            the probe's two family paths pinned onto the gate's own constants
  L4  size ceiling          synthetic 6000-line file -> rc=0; synthetic 6001-line file -> rc=1 size
                            only, each as a DECLARED single-member family (--no-register) so the real
                            register's line count cannot sit inside the boundary being measured
  L-M unreadable ledger     --ledger <missing> -> rc=2
  L5  family sum            the delivered run's judged cur_nl equals the two INV member= lines that
                            same run prints, and --no-register drops exactly the register's own count
                            while naming itself on overrides_used -- the leg that turns "the rows moved
                            to a sibling file" into a reading instead of an escape hatch
  L6  shrink refusal        per-member line loss against each member's OWN HEAD blob, refused on exit
                            2 (W-2: the family sum cannot see a member that lost lines while another
                            gained more). A6 truncate the copy's register only -> rc=2, one ERROR line
                            naming the member and its numbers, and no INV/verdict/SPLITDUE printed;
                            A6b same truncation + --allow-shrink=<reason> -> rc=0, due=DECLARED, the
                            reason on overrides_used; A6c is W-2 exactly -- ledger +1 AND register -1 so
                            the family nets zero and all three growth conditions stay not due, yet the
                            refusal still fires on the member. All three run against the COPY with
                            --register pointing at it: blob_nl resolves a path against HEAD, so a temp
                            file off HEAD reads as "new" and could never fire the leg.
  H1  host maps 11          copy-only defect (delete the header row for 11, same shape as G-A, so the
                            geometry step is the FIRST step that sees it) -> ci.ps1 exit 11, echo shows
                            the GEOMETRY finding, no "=== Configure"
  H2  host maps 12          copy-only growth (89 citation-free filler lines appended to the copy's
                            ledger, so no earlier step judges it -- one member's growth, which the
                            family sum must still read as a burst) -> ci.ps1 exit 12, step 1i's OK line
                            is present (proof the stop came from 1j), no "=== Configure"
  M2  host maps 2           copy-only temporary edit: step 1j passes --ledger <missing> -> ci.ps1 still
                            exit 12 AND the failure line reads "script exit 2" (the host test is
                            -ne 0, it does not special-case 1). Reverted right after.
  H3  restored              injections removed -> both scripts rc=0 in the copy and their summary
                            lines byte-identical to CB's readings for THIS copy, not to G0/L0's:
                            those read the main worktree, whose growth baseline moves every time a
                            round lands, so a copy-vs-main comparison is a cross-tree check, not a
                            control. Its reading line counts matched lines over all ten compared
                            keys (a yes/no derived from one tool only once printed "identical=yes"
                            on the same line as three red lines from the other tool).
  CB  copy baseline         (not an arm) the copy's own two tool readings, taken after make_copy's
                            sync and before the first injection; H3 and the copy's git status both
                            anchor on it. A copy that is not green at that point raises SystemExit
                            instead of recording a failure -- with no baseline to restore to, every
                            arm below would measure against readings that were never taken.
  Z0  snapshots             main worktree: md5 + porcelain of the watched files, re-checked last. Copy:
                            its footprint md5s AND its git status must both equal the baselines taken
                            right after the sync, before any injection. Not "git status is clean" -- the
                            sync alone leaves docs marked M, because the copy checks the ledger out with
                            CRLF (core.autocrlf=true) while the delivered bytes are LF.

Not proven here, and said so: the host GREEN path through 1i and 1j (a green host run needs
configure+build+ctest, which the copy does not carry) is taken from this round's end-to-end close-out
run on the main worktree, exactly as U-43 表 3 末行 states for step 1h. The checker's own exit 3 is the
wrapper's except branch (code shape only); no leg manufactures a crash. L6 is refused on a LINE count,
so a rewrite that deletes a whole row while adding one row's worth of prose is invisible to it by
construction -- no leg manufactures that either, because nothing can tell it apart from an ordinary
round. And L6 never runs the host: the shrink path maps to exit 12 only through M2's proof that any
non-zero from step 1j's script becomes host code 12, not through a host run that actually shrinks.
L0's cross-check has no synthetic negative arm either -- the only proof it bites is the red it printed
on the post-landing replay of the retired HEAD~3..HEAD form, which was a real divergence between two
window definitions rather than a manufactured one.

One host shape the H legs have to account for: ci.ps1 prints every script finding line through
Write-Host "  $line", so on the console "GEOMETRY ..." and "SPLITDUE ..." carry a two-space indent.
echo_lines() strips it before matching; matching raw lines reads a real red as a missing finding.

Run:  python tools/probes/U44_geometry_split_probe.py         (all arms)
      python tools/probes/U44_geometry_split_probe.py --keep  (leave the copy + logs for reading)
      ONE AT A TIME: this probe, u45_abort_check.py, u45_pin_check.py and u46_sweep_legs_arm.py all
      write the same build/u44_probe copy worktree and legs directory, so main() takes that shared
      scratch lock before it touches anything (eleventh-round review W-1 -- before this round the
      probe was one of the unlocked entrances).
Exit: 0 = every arm behaved as pinned; 1 = at least one arm diverged; 4 = another run holds the
      shared scratch ([LOCK-BUSY] printed, no judgement made, no scratch touched). ASCII-only.
"""
import hashlib
import msvcrt
import os
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOGDIR = os.path.join(ROOT, "build", "u44_probe")
WT = os.path.join(LOGDIR, "wt")
LEGS = os.path.join(LOGDIR, "legs")
# Same file the git-ignored helper locks (build/u44_probe/u46_scratch_clean.py LOCK_FILE); this probe
# cannot import that helper -- a fresh clone has no build/u44_probe tree at all -- so the acquisition
# is carried twice on purpose, and the U47 probe's LK arm pins the two names onto one literal.
SCRATCH_LOCK = os.path.join(LOGDIR, "scratch.lock")
# --keep is read once, at module scope, because both exits need it: the normal tail and abort().
KEEP = "--keep" in sys.argv[1:]
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

CI_PS1 = os.path.join("tools", "ci.ps1")
GEOM_TOOL = "tools/ci_exit_code_check.py"
SPLIT_TOOL = "tools/ledger_size_gate.py"
# The gap plan, spelled the way the tools spell it (the console here is cp936).
LEDGER_REL = u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212.md"
# From U-60 on the gate judges a FAMILY of two: the ledger plus the cumulative register that moved out
# of it. This probe has to spell the second path itself because it loads the gate as a module in one
# leg only; the two spellings are pinned against the gate's own constants in leg_l3, and leg_l5 reads
# the member lines the delivered gate prints.
REGISTER_REL = (u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212"
                u"-\u767b\u8bb0\u9644\u8868.md")

GEOM_STEP = "=== Host exit-code geometry self-check ==="
SPLIT_STEP = "=== Ledger growth split trigger ==="
GEOM_OK = "host exit-code geometry OK"
SPLIT_OK = "ledger split trigger not due"
CONFIGURE_MARKER = "=== Configure"

# Pinned from the delivered bytes (G0 asserts them, so a drift in the file cannot pass here).
EXPECTED_COUNTS = "0:2 1:6 2:1 3:2 4:1 5:1 6:1 7:1 8:1 9:1 10:1 11:1 12:1 13:1"
EXPECTED_HEADER = "0 1 2 3 4 5 6 7 8 9 10 11 12 13"
EXPECTED_STEPS = "14"
# NOT pinned as an absolute value: the gate's history_only_last3 takes the last three commits that
# touched the ledger, so the window moves one commit forward with every commit that lands -- an
# absolute pin here would go red for a reviewer replaying this probe at the delivered commit for a
# reason that has nothing to do with the gate. L0 pins the AGREEMENT instead (see leg_l0).
HEADER_ROW_11 = u"      11 = host exit-code geometry drifted against its frozen registration\n"
STEP_1I_HEADING = u'Write-Step "Host exit-code geometry self-check"'
STEP_1J_HEADING = u'Write-Step "Ledger growth split trigger"'
STEP_1H_STOP = u"    exit 10\n"

FAILURES = []
# L0's reading of the MAIN tree, consumed by leg_l1's cross-check (both judge the delivered bytes).
# There used to be a BASELINE_G0 next to it: the only reader was H3, and H3 now compares against the
# copy's own pre-injection readings below, so a write-only baseline global is one reader short of the
# cross-tree comparison this probe keeps failing for elsewhere.
BASELINE_L0 = {}
# The COPY's own readings, taken right after make_copy's sync and before the first injection. H3 is the
# undo-everything control, so it must compare the copy against the copy: G0/L0 read the main tree, whose
# ledger growth is measured against a different HEAD as soon as a round lands.
BASELINE_G0_COPY = {}
BASELINE_L0_COPY = {}
# Taken before the first injection; Z0 re-checks them last.
MAIN_BEFORE = {}
STATUS_BEFORE = b""
# The copy's git status right after make_copy's sync, before any injection. Z0 compares against THIS,
# not against "clean" -- see leg_z0's comment for why the sync itself can never leave a clean copy.
COPY_STATUS_BEFORE = b""


def fail(text):
    FAILURES.append(text)
    print("[U44-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def yn(cond):
    return "yes" if cond else "NO"


def arm_open():
    return len(FAILURES)


ARMS = []


def reading(since, text):
    """The reading line tells on itself: an arm that already appended a failure must not print OK."""
    ARMS.append(text.split(" ")[0])
    print("[%s] %s" % ("U44-OK" if len(FAILURES) == since else "U44-FAIL", text))


def sweep_legs():
    """Drop the injected leg scripts. The tail has always done this unless --keep; the fast-abort
    paths used to skip it, which left the previous run's injections sitting next to the copy."""
    if KEEP or not os.path.isdir(LEGS):
        return
    shutil.rmtree(LEGS, ignore_errors=True)
    if os.path.isdir(LEGS):
        print("[U44-LEGS] left in place, rmtree did not remove %s" % LEGS)


def abort(reason):
    """Leave the run because a precondition broke and every arm below would measure wrong bytes.

    A bare `raise SystemExit` jumps past main()'s tail, where the LEGS cleanup and the [U44-SUMMARY]
    block live -- so a failure already collected by an EARLIER arm went unreported (ninth-round
    review W-4). The abort stays fast and keeps exit code 1, but it says who is leaving, why, and
    what was already red before it leaves.
    """
    print("[U44-ABORT] %s" % reason)
    print("[U44-ABORT] arms opened so far=%d, failures left unsummarised=%d"
          % (len(ARMS), len(FAILURES)))
    for f in FAILURES:
        print("[U44-ABORT]   pending failure: " + f)
    sweep_legs()
    raise SystemExit(reason)


def md5(data):
    return hashlib.md5(data).hexdigest()


def decode(raw):
    return raw.decode("utf-8", errors="replace").replace("\r\n", "\n")


def sh(args, cwd=None, timeout=3600):
    return subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)


def rel(*parts):
    return os.path.join(WT, *parts)


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def read_bytes_or_none(path):
    if not os.path.isfile(path):
        return None
    return read_bytes(path)


def head_bytes(rel_posix):
    """HEAD's blob for one tracked path in the copy, or None if it has none.

    This leg spells it git show HEAD:<path>. The gate's own base goes through two commands instead:
    blob_nl pipes the same rev:path spec into git cat-file --batch-check, then fetches the object it
    names with git cat-file blob, both with cwd the gate's own ROOT. U-62 measured the two faces
    byte-identical for both family members (build/u44_probe/U62R_review_check_1.txt, the RV-BASE
    lines read show_md5=catfile_md5=disk_md5), so this is still the same number the refusal will be
    compared against, not a second opinion about what the base is.
    """
    p = sh(["git", "show", "HEAD:%s" % rel_posix], cwd=WT)
    if p.returncode != 0:
        return None
    return p.stdout


def inv_lines(text):
    """{key: whole line} for the script's own INV readings, plus the GEOMETRY findings."""
    out = {}
    for line in text.split("\n"):
        m = re.match(r"^INV (\w+)=", line)
        if m:
            out[m.group(1)] = line
    return out


def findings(text):
    return [ln[len("GEOMETRY "):] for ln in text.split("\n") if ln.startswith("GEOMETRY ")]


def run_geom(path):
    proc = sh([sys.executable, GEOM_TOOL, "--path", path])
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def run_gate(args_extra=None, cwd=None):
    proc = sh([sys.executable, SPLIT_TOOL] + list(args_extra or []), cwd=cwd or ROOT)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def run_ci():
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1], cwd=WT)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def patch_once(text, old, new, leg):
    n = text.count(old)
    if n != 1:
        fail("%s: patch anchor hit %d time(s), expected exactly 1: %r" % (leg, n, old[:60]))
        return None
    if not os.path.isdir(LEGS):
        os.makedirs(LEGS)
    path = os.path.join(LEGS, "%s.ps1" % leg)
    with open(path, "wb") as handle:
        handle.write(text.replace(old, new).encode("utf-8"))
    return path


# ---------------------------------------------------------------- G legs: the checker itself
def leg_g0():
    since = arm_open()
    rc, text = run_geom(os.path.join(ROOT, CI_PS1))
    inv = inv_lines(text)
    check(rc == 0, "G0: delivered ci.ps1 not green, rc=%s" % rc)
    check(inv.get("findings", "").endswith("0"), "G0: findings line: %r" % inv.get("findings"))
    check(inv.get("header_codes", "") == "INV header_codes=" + EXPECTED_HEADER,
          "G0: header codes drifted: %r" % inv.get("header_codes"))
    check(EXPECTED_COUNTS in inv.get("executable_stops", ""),
          "G0: executable-stop counts drifted: %r" % inv.get("executable_stops"))
    check(inv.get("step_count", "").startswith("INV step_count=%s " % EXPECTED_STEPS),
          "G0: step count: %r" % inv.get("step_count"))
    check(inv.get("verdict", "").endswith("GREEN"), "G0: verdict: %r" % inv.get("verdict"))
    reading(since, "G0 rc=%d | %s | %s" % (rc, inv.get("header_codes"), inv.get("executable_stops")))
    return text


def leg_g(leg, old, new, expect_letters, expect_substrings):
    """Inject ONE defect, run the checker, and require exactly the named legs to answer for it.

    The comparison is a multiset of leg letters, not a finding count: one defect can legitimately
    light up several legs at once (a step that lost its stop loses its header row too), and pinning
    the letters is what keeps a leg from passing on an unrelated finding.
    """
    since = arm_open()
    base = decode(read_bytes(os.path.join(ROOT, CI_PS1)))
    path = patch_once(base, old, new, leg)
    if path is None:
        reading(since, "%s: patch did not apply" % leg)
        return
    rc, text = run_geom(path)
    fs = findings(text)
    letters = tuple(sorted(f.split(" ")[0] for f in fs))
    check(rc == 1, "%s: expected rc=1, got %s | %s" % (leg, rc, text[-200:]))
    check(letters == expect_letters, "%s: legs answering: %r, expected %r | %r"
          % (leg, letters, expect_letters, fs))
    for sub in expect_substrings:
        check(any(sub in f for f in fs), "%s: no finding contains %r: %r" % (leg, sub, fs))
    reading(since, "%s rc=%d letters=%s" % (leg, rc, ",".join(letters) or "none"))


def legs_g():
    leg_g0()
    leg_g("GA", HEADER_ROW_11, u"", ("A",), ("A stop code 11 fires 1 time(s) but has no header row",))
    leg_g("GB", HEADER_ROW_11, HEADER_ROW_11 + u"      14 = reserved for a gate that is not wired\n",
          ("B",), ("B header row 14 has no executable stop",))
    leg_g("GC", STEP_1J_HEADING, STEP_1J_HEADING + u"\n    exit 11",
          ("C", "E"), ("C gate code 11 fires 2 time(s), frozen 1", "also fires inside"))
    leg_g("GD", STEP_1I_HEADING, u'Write-Step "Host exit-code geometry self-check RENAMED"',
          ("D",), ("missing Host exit-code geometry self-check",
                   "extra Host exit-code geometry self-check RENAMED"))
    leg_g("GE", STEP_1H_STOP, u'    Write-Warn "citation roster drifted"\n',
          ("B", "C", "E"), ("C gate code 10 fires 0 time(s)", "B header row 10 has no executable stop"))
    leg_g("GF", STEP_1H_STOP, u"    exit 11\n",
          ("B", "C", "C", "E", "E"),
          ("C gate code 10 fires 0 time(s)", "C gate code 11 fires 2 time(s)", "also fires inside"))
    leg_gm()


def leg_gm():
    """Mirrors L-M on the other tool: an input the checker cannot read is neither drift (rc=1) nor
    clean (rc=0), and must not print a verdict at all."""
    since = arm_open()
    rc, text = run_geom(os.path.join(LEGS, "u44_no_such_ci.ps1"))
    check(rc == 2, "G-M: missing --path must read rc=2, got %s | %s" % (rc, text[-160:]))
    check(text.startswith("ERROR"), "G-M: expected an ERROR line, got %r" % text[:120])
    check("GEOMETRY" not in text, "G-M: a broken input must not print a finding: %r" % text[:160])
    check("verdict" not in text,
          "G-M: a broken input must not print a verdict (green or red): %r" % text[:160])
    reading(since, "G-M rc=%d | %s" % (rc, text.split("\n")[0]))


# ---------------------------------------------------------------- L legs: the split gate
def leg_l0():
    since = arm_open()
    rc, text = run_gate()
    inv = inv_lines(text)
    BASELINE_L0.clear()
    BASELINE_L0.update(dict((k, inv.get(k)) for k in
                            ("head", "rounds_counted", "history_only_last3", "ceiling",
                             "split_project_due", "verdict")))
    check(rc == 0, "L0: delivered bytes not green, rc=%s" % rc)
    check(inv.get("verdict", "").endswith("GREEN"), "L0: verdict %r" % inv.get("verdict"))
    check("burst_due=NO" in inv.get("head", ""), "L0: burst: %r" % inv.get("head"))
    check("rate_due=NO" in inv.get("rounds_counted", ""), "L0: rate: %r" % inv.get("rounds_counted"))
    check("size_due=NO" in inv.get("ceiling", ""), "L0: size: %r" % inv.get("ceiling"))
    check("overrides_used=none" in inv.get("overrides_used", ""),
          "L0: a plain run must use no override: %r" % inv.get("overrides_used"))
    # Cross-check the gate's own history reading with git doing the arithmetic, on the SAME window
    # definition: history_only_last3 sums the newest RATE_WINDOW_ROUNDS commits that TOUCH either
    # family member, inside the gate's own SCAN_COMMITS scan. So the control selects those commits with
    # git's own pathspec and then nets the whole span with one range numstat naming BOTH members --
    # reading only the ledger row would compare the gate's sum against half of it.
    # Why this was re-cut in U-61: the previous form compared against HEAD~3..HEAD (the last three
    # commits full stop) and stood on the assumption that every recent commit updates the family. That
    # assumption was load-bearing and it broke one commit later -- the tools-only commit that landed W-2
    # touched neither member, and the post-landing replay read "gate reads 134 but git diff
    # HEAD~3..HEAD reads 65", where BOTH numbers were correct about two different windows. A leg that
    # reds on a healthy repository is a leg people route around, so the control moved to the gate's
    # definition instead of the repository bending to the leg.
    # What is no longer assumed: that recent commits touch either member. What still is: a linear
    # history. git rev-list's path simplification and the gate's per-commit numstat parse can disagree
    # across a merge, so the merge count inside the scan is printed on both the reading line and the
    # failure line -- the leg still reds on the numbers rather than adjudicating which selection git
    # meant.
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ledger_size_gate as gate  # (constants only: the window size and the scan bound)
    sel = sh(["git", "rev-list", "-%d" % gate.RATE_WINDOW_ROUNDS,
              "HEAD~%d..HEAD" % gate.SCAN_COMMITS, "--", LEDGER_REL, REGISTER_REL])
    rounds = [s for s in decode(sel.stdout).split("\n") if s.strip()]
    if not check(sel.returncode == 0 and len(rounds) == gate.RATE_WINDOW_ROUNDS,
                 "L0: cannot select the gate's %d-round window from git: rc=%s got=%d %r"
                 % (gate.RATE_WINDOW_ROUNDS, sel.returncode, len(rounds),
                    [s[:7] for s in rounds])):
        reading(since, "L0: window not selectable, no cross-check measured")
        return text
    proc = sh(["git", "diff", "--numstat", "%s^" % rounds[-1], rounds[0], "--",
               LEDGER_REL, REGISTER_REL])
    rows = [ln for ln in decode(proc.stdout).split("\n") if ln.strip()]
    pairs = [re.match(r"^(\d+)\t(\d+)\t", ln) for ln in rows]
    if not check(proc.returncode == 0 and bool(rows) and all(pairs),
                 "L0: cross-check git diff unreadable: rc=%s rows=%d %r"
                 % (proc.returncode, len(rows), rows[:3])):
        reading(since, "L0: cross-check unavailable")
        return text
    cross = sum(int(m.group(1)) - int(m.group(2)) for m in pairs)
    # A third, independent arithmetic path: each selected round's OWN numstat. The range net telescopes
    # only if the history between the endpoints is what git's selection said it was, so requiring
    # range == per-round sum == the gate's printed reading makes the agreement triangular instead of a
    # two-way coincidence. There is deliberately no "at least one round must be non-zero" guard here:
    # three consecutive in-place-only rounds is a legitimate repository shape (72b287c itself nets 0 on
    # the family), and demanding growth is exactly the fragility this re-cut retired. The per-round nets
    # therefore go on the reading line, so a thin agreement is at least visible to a reviewer.
    nets = []
    for sha in rounds:
        one = sh(["git", "diff", "--numstat", "%s^" % sha, sha, "--", LEDGER_REL, REGISTER_REL])
        ones = [re.match(r"^(\d+)\t(\d+)\t", ln) for ln in decode(one.stdout).split("\n") if ln.strip()]
        if not check(one.returncode == 0 and all(ones),
                     "L0: round %s's own numstat is unreadable: rc=%s %r"
                     % (sha[:7], one.returncode, decode(one.stdout)[:120])):
            reading(since, "L0: per-round leg unavailable, no cross-check measured")
            return text
        nets.append((sha[:7], sum(int(m.group(1)) - int(m.group(2)) for m in ones)))
    check(sum(n for _s, n in nets) == cross,
          "L0: the range net %d is not the sum of the selected rounds' own nets %s"
          % (cross, ",".join("%s:%+d" % n for n in nets)))
    printed = re.search(r"history_only_last3=(\d+)", text)
    check(printed is not None, "L0: gate printed no history_only_last3 reading")
    # The two algorithms agreeing is the assertion; the value itself is not pinned -- see the note
    # above EXPECTED constants. Guard against a vacuous agreement (an empty window reads 0 on both sides).
    scanned = re.search(r"scanned_commits=(\d+)", inv.get("anchor", ""))
    check(scanned is not None and int(scanned.group(1)) >= gate.RATE_WINDOW_ROUNDS,
          "L0: history window has fewer than %d commits to sum: %r"
          % (gate.RATE_WINDOW_ROUNDS, inv.get("anchor")))
    merges = len([s for s in decode(sh(["git", "rev-list", "--merges",
                                        "-%d" % gate.SCAN_COMMITS, "HEAD"]).stdout).split("\n")
                  if s.strip()])
    check(printed is not None and int(printed.group(1)) == cross,
          "L0: gate reads %s but git rev-list/diff over the same %d selected round(s) reads %d across "
          "%d family row(s), merges_in_scan=%d"
          % (printed.group(1) if printed else "?", len(rounds), cross, len(rows), merges))
    reading(since, "L0 rc=%d history3=%s family_cross=%d rows=%d window=%s merges_in_scan=%d ceiling=%s"
            % (rc, printed.group(1) if printed else "?", cross, len(rows),
               ",".join("%s:%+d" % p for p in nets), merges, inv.get("ceiling")))
    return text


def leg_l1():
    """The burst condition's boundary: delta 88 leaves burst off, delta 89 turns it on (the recorded
    trigger is strictly greater). Both bases come from the gate's own cur_nl reading -- which from
    U-60 is the FAMILY sum of both members, so what --base-lines injects here is a family base; the
    two runs still differ in exactly one injected number whatever the family measures today.

    The exit code is NOT hard-coded here. Condition 2 counts the rounds that really landed after the
    pinned anchor, so a replay two rounds from can legitimately fire rate as well -- pinning "delta 88
    exits 0 with nothing fired" would then go red and blame the wrong thing. This leg reads the gate's
    own three flags from the SAME run, derives the fired list, the SPLITDUE lines and the exit code
    from them, and asserts that burst membership is exactly what the delta boundary decides."""
    cur = re.search(r"cur_nl=(\d+)", BASELINE_L0.get("head", ""))
    if not check(cur is not None, "L1: no cur_nl in the L0 baseline reading: %r" % BASELINE_L0.get("head")):
        return
    cur_nl = int(cur.group(1))
    for delta, want_burst in ((88, False), (89, True)):
        base_lines = str(cur_nl - delta)
        since = arm_open()
        rc, text = run_gate(["--base-lines", base_lines])
        inv = inv_lines(text)
        head = inv.get("head", "")
        got_delta = re.search(r"delta=(-?\d+)", head)
        got_cur = re.search(r"cur_nl=(\d+)", head)
        check(got_delta is not None and got_cur is not None
              and int(got_delta.group(1)) == int(got_cur.group(1)) - int(base_lines) == delta,
              "L1 delta=%s: head line reads %r" % (delta, head))
        due = inv.get("split_project_due", "")
        fired = re.search(r"fired=(\S+)", due)
        if not check(fired is not None, "L1 delta=%s: no fired= reading: %r" % (delta, due)):
            reading(since, "L1 delta=%s: fired line unreadable" % delta)
            continue
        burst_on = "burst_due=YES" in head
        rate_on = "rate_due=YES" in inv.get("rounds_counted", "")
        size_on = "size_due=YES" in inv.get("ceiling", "")
        want = [n for n, on in (("burst", burst_on), ("rate", rate_on), ("size", size_on)) if on]
        # the boundary itself: burst is on exactly for the round over 88
        check(burst_on == want_burst, "L1 delta=%s: burst flag %r, want burst=%s" % (delta, head, want_burst))
        # everything the run reports must follow from its own three flags
        check(fired.group(1) == (",".join(want) or "none"),
              "L1 delta=%s: fired=%s but its flags read burst=%s rate=%s size=%s"
              % (delta, fired.group(1), burst_on, rate_on, size_on))
        check(("split_project_due=YES" in due) == bool(want),
              "L1 delta=%s: due line %r vs fired=%s" % (delta, due, fired.group(1)))
        check(re.findall(r"^SPLITDUE condition=(\w+)", text, re.M) == want,
              "L1 delta=%s: SPLITDUE lines %s vs fired=%s"
              % (delta, re.findall(r"^SPLITDUE condition=(\w+)", text, re.M), fired.group(1)))
        check(rc == (1 if want else 0), "L1 delta=%s: rc=%s with fired=%s" % (delta, rc, fired.group(1)))
        check("base-lines=%s" % base_lines in inv.get("overrides_used", ""),
              "L1 %s: the override must be printed: %r" % (delta, inv.get("overrides_used")))
        # The base goes in as a FAMILY base, so the run that answers for the boundary must still be
        # judging both members -- a one-member run would read the same delta off different bytes.
        check("family_members_judged=2" in inv.get("register", ""),
              "L1 delta=%s: the burst boundary must be judged on the whole family: %r"
              % (delta, inv.get("register")))
        reading(since, "L1 delta=%s base=%s rc=%d | %s | rate_due=%s size_due=%s"
                % (delta, base_lines, rc, due,
                   "YES" if rate_on else "NO", "YES" if size_on else "NO"))


def leg_l3():
    """Condition 2's arithmetic, driven through the same function the gate calls (a real repository
    only offers the rounds it actually has, and this round has none after the anchor)."""
    since = arm_open()
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import ledger_size_gate as gate  # (imported for rate_window and the two constants it judges by)

    check(gate.RATE_WINDOW_ROUNDS == 3 and gate.RATE_LIMIT_LINES == 200,
          "L3: the rule under test is no longer 3 rounds over 200 lines: %s / %s"
          % (gate.RATE_WINDOW_ROUNDS, gate.RATE_LIMIT_LINES))
    # The legs that pass --ledger/--register an explicit path, and the ones that read the member lines
    # back, are only meaningful while the probe and the gate name the same two files. This is the leg
    # that already has the module imported, so the check lives here rather than duplicating an import.
    paths_agree = gate.LEDGER_REL == LEDGER_REL and gate.REGISTER_REL == REGISTER_REL
    check(paths_agree,
          "L3: probe and gate no longer name the same family: probe=(%s, %s) gate=(%s, %s)"
          % tuple([s.encode("unicode_escape").decode("ascii")
                   for s in (LEDGER_REL, REGISTER_REL, gate.LEDGER_REL, gate.REGISTER_REL)]))

    rounds = [("r%d" % i, net) for i, net in enumerate((70, 60, 50, 40), start=1)]
    cases = [
        ("one round is not judged (burst covers it)", 300, [], (1, 300, False)),
        ("two rounds under the limit", 0, rounds[:2], (2, 130, False)),
        ("two rounds over the limit", 0, [("a", 110), ("b", 110)], (2, 220, True)),
        ("sum exactly at the limit stays green (strict >)", 100, [("x", 100)], (2, 200, False)),
        ("one line over the limit fires", 101, [("x", 100)], (2, 201, True)),
        ("a 4th round drops out of the window", 0, rounds, (3, 180, False)),
    ]
    for label, delta, after, (want_n, want_sum, want_due) in cases:
        window, total, due = gate.rate_window(delta, "HEAD", list(after))
        check(len(window) == want_n and total == want_sum and due == want_due,
              "L3 %s: got n=%d sum=%d due=%s, expected n=%d sum=%d due=%s"
              % (label, len(window), total, due, want_n, want_sum, want_due))
    # The window must keep the NEWEST rounds, not the first ones it sees.
    window, total, _due = gate.rate_window(1, "HEAD", list(rounds))
    check([s for s, _n in window] == ["HEAD", "r1", "r2"],
          "L3 window order: %r" % ([s for s, _n in window],))
    check(total == 1 + 70 + 60, "L3 window sum: %d" % total)
    reading(since, "L3 cases=%d boundary=%s window=%s sum=%d family_paths_agree=%s"
            % (len(cases), "200/201", ",".join(s for s, _n in window), total, yn(paths_agree)))


def synthetic_ledger(name, lines):
    if not os.path.isdir(LEGS):
        os.makedirs(LEGS)
    path = os.path.join(LEGS, name)
    with open(path, "wb") as handle:
        handle.write(b"\n".join(b"- filler line %05d" % i for i in range(lines)) + b"\n")
    return path


def leg_l4():
    """Condition 3: the absolute size ceiling, one line either side of it, with the burst condition
    neutralised by --base-lines so only size can answer.

    These two arms are why --no-register exists: the ceiling is judged on the family sum, so a synthetic
    file would otherwise be measured with the real register's line count added to it and the 6000/6001
    boundary would land on a different pair of files every round. Dropping a member is DECLARED, and
    the leg below requires the gate to print that declaration -- a bypass nobody can see is the shape
    this whole probe exists to catch."""
    for size, want_rc in ((6000, 0), (6001, 1)):
        since = arm_open()
        path = synthetic_ledger("synthetic_%d.md" % size, size)
        rel_path = os.path.relpath(path, ROOT).replace(os.sep, "/")
        rc, text = run_gate(["--ledger", rel_path, "--base-lines", str(size), "--no-register"])
        inv = inv_lines(text)
        check(rc == want_rc, "L4 size=%d: rc=%s expected %s | %s" % (size, rc, want_rc, text[-160:]))
        check("cur_nl=%d" % size in inv.get("head", ""), "L4 size=%d head line: %r"
              % (size, inv.get("head")))
        check("family_members_judged=1" in inv.get("register", ""),
              "L4 size=%d: the synthetic file must be the only member judged: %r"
              % (size, inv.get("register")))
        check("no_register=declared" in inv.get("overrides_used", ""),
              "L4 size=%d: a one-member family must say so: %r"
              % (size, inv.get("overrides_used")))
        check(("size_due=YES" if want_rc else "size_due=NO") in inv.get("ceiling", ""),
              "L4 size=%d ceiling line: %r" % (size, inv.get("ceiling")))
        if want_rc:
            check("fired=size" in inv.get("split_project_due", ""),
                  "L4 size=6001 fired line: %r" % inv.get("split_project_due"))
        check("INV ledger=%s" % rel_path in text,
              "L4 size=%d: the judged file must be printed: %r" % (size, inv.get("ledger")))
        # The refusal leg's one exception, on the synthetic member this leg already carries: a file git
        # has no HEAD blob for is a creation, and a creation has nothing to lose -- so it must print as
        # new, never as a shrink. Without this the L6 arms would be the only evidence that the gate can
        # tell "no base yet" apart from "base unreadable".
        check("shrink=ledger:new " in inv.get("shrink", ""),
              "L4 size=%d: an untracked member must read as new, not as a shrink: %r"
              % (size, inv.get("shrink")))
        reading(since, "L4 size=%d rc=%d | %s" % (size, rc, inv.get("split_project_due")))


def leg_lm():
    since = arm_open()
    rc, text = run_gate(["--ledger", "u44_no_such_ledger.md"])
    check(rc == 2, "L-M: missing ledger must read rc=2, got %s | %s" % (rc, text[-160:]))
    check(text.startswith("ERROR"), "L-M: expected an ERROR line, got %r" % text[:120])
    check("SPLITDUE" not in text and "verdict=GREEN" not in text,
          "L-M: a broken input must not read as a finding or as clean: %r" % text[:160])
    reading(since, "L-M rc=%d | %s" % (rc, text.split("\n")[0]))


MEMBER_RE = re.compile(r"^INV member=(\w+) path=\S+ cur_nl=(\d+|none) state=(\w+)", re.M)


def leg_l5(l0_text):
    """The family half of the split-the-file promise, measured on the delivered bytes.

    U-60 moved the re-carried cumulative register out of the ledger into a file of its own, and the
    gate sums burst, rate and size over BOTH. This arm judges the one property that makes that cut an
    honest one instead of an escape hatch: the number the trigger is read off must be the sum of the
    members the very same run prints -- a gate that lists two files and adds up one of them goes red
    here. Nothing absolute is pinned: both runs' numbers come from readings taken in this arm, so a
    reviewer replaying it two rounds from now measures the same relation (see the note above the
    EXPECTED constants).

    The second half is the DECLARATION. --no-register has to show up on overrides_used and has to drop
    exactly the register member's own line count, so a single-member family can only ever be run on
    purpose -- which is what leg_l4 needs for its synthetic boundary. This arm does not pin the
    declared run's verdict: dropping a member is precisely the move that can legitimately change a rate
    reading, and what is under test here is whether anyone can see it happen.
    """
    since = arm_open()
    inv = inv_lines(l0_text)
    rows = dict((m.group(1), (m.group(2), m.group(3))) for m in MEMBER_RE.finditer(l0_text))
    if not check(sorted(rows) == ["ledger", "register"],
                 "L5: the bare run printed member lines %s, want ledger+register" % sorted(rows)):
        reading(since, "L5: family membership unreadable: %r" % (rows,))
        return
    if not check(rows["ledger"][1] == "present" and rows["register"][1] == "present",
                 "L5: a close-out that points at a companion file must land it: %r" % (rows,)):
        reading(since, "L5: a family member is absent: %r" % (rows,))
        return
    check("family_members_judged=2" in inv.get("register", ""),
          "L5: the bare run must judge both members: %r" % inv.get("register"))
    ledger_nl, register_nl = int(rows["ledger"][0]), int(rows["register"][0])
    printed = re.search(r"cur_nl=(\d+)", inv.get("head", ""))
    check(printed is not None and int(printed.group(1)) == ledger_nl + register_nl,
          "L5: judged cur_nl %s is not the sum of the members the same run prints (%d+%d=%d)"
          % (printed.group(1) if printed else "?", ledger_nl, register_nl, ledger_nl + register_nl))
    family_nl = int(printed.group(1)) if printed else -1
    rc2, text2 = run_gate(["--no-register"])
    inv2 = inv_lines(text2)
    rows2 = [m.group(1) for m in MEMBER_RE.finditer(text2)]
    check(rows2 == ["ledger"], "L5: --no-register still printed members %s, want the ledger alone"
          % rows2)
    check("family_members_judged=1" in inv2.get("register", ""),
          "L5: the declared run must report one member: %r" % inv2.get("register"))
    check("no_register=declared" in inv2.get("overrides_used", ""),
          "L5: dropping a member must be printed, never silent: %r" % inv2.get("overrides_used"))
    cur2 = re.search(r"cur_nl=(\d+)", inv2.get("head", ""))
    check(cur2 is not None and int(cur2.group(1)) == ledger_nl,
          "L5: the declared run reads cur_nl %s, want the ledger member's own %d (it dropped %s)"
          % (cur2.group(1) if cur2 else "?", ledger_nl,
             "nothing" if cur2 is None else ledger_nl + register_nl - int(cur2.group(1))))
    reading(since, "L5 family=%d+%d=%d declared_out=%d judged=2->1 rc2=%d verdict2=%s"
            % (ledger_nl, register_nl, family_nl,
               int(cur2.group(1)) if cur2 else -1, rc2,
               inv2.get("verdict", "").rsplit("=", 1)[-1]))


def drop_last_line(data):
    """The bytes of the same file with its LAST line taken away (one fewer newline, nothing else moved).

    Written as a byte cut rather than a line count on purpose: the leg under test compares a member's
    worktree line count against its OWN HEAD blob, so the injection has to be a real deletion from the
    delivered bytes -- not a shorter synthetic file, which would also change the content the round
    claims to have kept.
    """
    if not data.endswith(b"\n"):
        return None
    cut = data[:-1].rfind(b"\n")
    if cut < 0:
        return None
    return data[:cut + 1]


SHRINK_REASON = "u61-w2-declared-cut"


def leg_l6():
    """W-2's half: the family sum can hide a member that LOST lines, so the loss is refused by itself.

    L5 judges the sum, which is what stops a move from voting. A deletion is the other vote, and the
    sum is blind to it: one row out of the register plus two rows into the ledger nets positive on the
    family while the register shrank -- and the plan's own rule is that old rows stay on the page with
    an in-place note, so a shrinking row count is the shape a rewrite that broke that rule leaves
    behind. The gate answers it per member, on the exit 2 channel M2 already proves maps to host code
    12, which is why nothing here touches ci.ps1.

    Three arms, all against the COPY with the copy's own HEAD (blob_nl resolves a path against HEAD, so
    a synthetic temp file reads as "new" and can never fire):
      A6   register minus its last line -> rc=2, ONE ERROR line naming register and its own two counts
           with delta=-1, and neither an INV reading nor a verdict printed (a refusal is not a result).
      A6b  the same bytes + --allow-shrink=<reason> -> rc=0, due=DECLARED, the reason echoed on
           overrides_used -- the declaration is the only way through and it cannot be silent.
      A6c  W-2 exactly: ledger +1 line AND register -1 line, so the family delta is 0 and burst, rate
           and size are all not due. The refusal still fires, and the DECLARED run prints all three
           growth flags NO in the same stdout -- the evidence that the growth channel could not have
           said a word about this round.
    Nothing absolute is pinned: every count compared comes from this copy's own pre-injection run.

    Baseline: the two members are reset to this copy's own HEAD blobs before the first run, and the
    bytes the probe synced out of main's worktree are put back in the finally below. A6's delta=-1
    only means anything with the member sitting AT its base, and the probe syncs main into the copy
    first -- so an uncommitted family edit in main used to arrive here as register:+3 and red the leg
    before any arm ran, which is a leg that only worked when the round happened to be committed.
    """
    since = arm_open()
    ledger = rel(LEDGER_REL.replace("/", os.sep))
    register = rel(REGISTER_REL.replace("/", os.sep))
    # Restoration bytes: what the probe synced out of main, returned to in the finally below.
    led_before = read_bytes(ledger)
    reg_before = read_bytes(register)
    led_head = head_bytes(LEDGER_REL)
    reg_head = head_bytes(REGISTER_REL)
    if not check(led_head is not None and reg_head is not None,
                 "L6: the copy's HEAD blobs are unreadable"):
        reading(since, "L6: HEAD baseline unreadable, no arm measured")
        return
    try:
        with open(ledger, "wb") as handle:
            handle.write(led_head)
        with open(register, "wb") as handle:
            handle.write(reg_head)
        rc0, text0 = run_gate(cwd=WT)
        inv0 = inv_lines(text0)
        rows0 = dict((m.group(1), m.group(2)) for m in MEMBER_RE.finditer(text0))
        if not check(rc0 == 0 and "ledger:+0,register:+0" in inv0.get("shrink", ""),
                     "L6: the copy is not on a zero-shrink baseline, rc=%s shrink=%r"
                     % (rc0, inv0.get("shrink"))):
            reading(since, "L6: copy shrink baseline unreadable, no arm measured")
            return
        if not check(sorted(rows0) == ["ledger", "register"]
                     and rows0["ledger"] not in ("none", None)
                     and rows0["register"] not in ("none", None),
                     "L6: no member counts to inject against: %r" % (rows0,)):
            reading(since, "L6: member counts unreadable, no arm measured")
            return
        reg_nl = int(rows0["register"])
        led_nl = int(rows0["ledger"])

        cut_reg = drop_last_line(reg_head)
        if not check(cut_reg is not None and cut_reg.count(b"\n") == reg_nl - 1,
                     "L6: cannot take exactly one line off the register (HEAD blob has %d newline(s), "
                     "the cut has %s)" % (reg_nl, cut_reg.count(b"\n") if cut_reg else "unreadable")):
            reading(since, "L6: injection shape not available, no arm measured")
            return
        with open(register, "wb") as handle:
            handle.write(cut_reg)
        expect = "register base_nl=%d cur_nl=%d delta=-1" % (reg_nl, reg_nl - 1)
        rc, text = run_gate(cwd=WT)
        err = [ln for ln in text.split("\n") if ln.startswith("ERROR")]
        check(rc == 2, "A6: undeclared shrink must refuse on rc=2, got %s | %s" % (rc, tail(text)))
        check(len(err) == 1, "A6: expected exactly one ERROR line, got %r" % err)
        check(any("undeclared family shrink" in ln for ln in err),
              "A6: the ERROR line must name the refusal: %r" % err)
        check(any(expect in ln for ln in err),
              "A6: the ERROR line must carry the member's own numbers %r: %r" % (expect, err))
        check("INV" not in text, "A6: a refusal must print no reading at all: %r" % text[:200])
        # verdict=/split_project_due= as PRINTED keys, not the bare word: the refusal's own wording says
        # "no verdict was printed", so a match on "verdict" would red a run that behaved correctly.
        check("verdict=" not in text and "split_project_due=" not in text,
              "A6: a refusal must print neither its due reading nor a verdict: %r" % text[:200])
        check("SPLITDUE" not in text, "A6: a refusal is not a growth finding: %r" % text[:200])

        rc_b, text_b = run_gate(["--allow-shrink", SHRINK_REASON], cwd=WT)
        inv_b = inv_lines(text_b)
        check(rc_b == 0, "A6b: the declared cut must be allowed through, got %s | %s"
              % (rc_b, tail(text_b)))
        check("due=DECLARED" in inv_b.get("shrink", ""),
              "A6b: the declared run must say the loss was declared: %r" % inv_b.get("shrink"))
        check("register:-1" in inv_b.get("shrink", ""),
              "A6b: the per-member reading must still show the loss: %r" % inv_b.get("shrink"))
        check("declared=%s" % SHRINK_REASON in inv_b.get("shrink", ""),
              "A6b: the reason belongs on the shrink line: %r" % inv_b.get("shrink"))
        check(SHRINK_REASON in inv_b.get("overrides_used", ""),
              "A6b: a bypass has to be printed on overrides_used: %r" % inv_b.get("overrides_used"))

        # The second spelling, and the one the delivered ledger block prints: --allow-shrink=<reason>.
        # U-62 caught this parser rejecting that spelling with exit 2 while both arms here (and the
        # archive the ledger block quotes) used the space form -- so a run proving the feature proved
        # nothing about the command a reader was actually handed. Same bytes, same reason: both
        # spellings have to reach the same two lines. The reading line below is deliberately not
        # extended; the ledger quotes its prefix verbatim.
        rc_eq, text_eq = run_gate(["--allow-shrink=%s" % SHRINK_REASON], cwd=WT)
        inv_eq = inv_lines(text_eq)
        check(rc_eq == 0,
              "A6b: the --allow-shrink=<reason> spelling must parse too, got %s | %s"
              % (rc_eq, tail(text_eq)))
        check(inv_eq.get("overrides_used", "") == inv_b.get("overrides_used", "")
              and inv_eq.get("shrink", "") == inv_b.get("shrink", ""),
              "A6b: the two spellings must print the same readings: %r/%r vs %r/%r"
              % (inv_eq.get("overrides_used"), inv_eq.get("shrink"),
                 inv_b.get("overrides_used"), inv_b.get("shrink")))

        # A6c -- the shape the sum cannot see: the ledger gains a line as the register loses one.
        filler = (u"- 探针注入占位行（不含引用、不含路径、不含结论标记）\n").encode("utf-8")
        check(led_head.endswith(b"\n"), "A6c: the ledger does not end with a newline")
        with open(ledger, "wb") as handle:
            handle.write(led_head + filler)
        rc_c, text_c = run_gate(cwd=WT)
        err_c = [ln for ln in text_c.split("\n") if ln.startswith("ERROR")]
        check(rc_c == 2, "A6c: family net 0 with a shrinking member must still refuse, got %s | %s"
              % (rc_c, tail(text_c)))
        check(any(expect in ln for ln in err_c),
              "A6c: the refusal must name the member that lost lines, not the family sum: %r" % err_c)

        rc_d, text_d = run_gate(["--allow-shrink", SHRINK_REASON], cwd=WT)
        inv_d = inv_lines(text_d)
        check(rc_d == 0, "A6c: the declared run must reach a verdict, got %s | %s"
              % (rc_d, tail(text_d)))
        check(inv_d.get("verdict", "").endswith("GREEN"),
              "A6c: the declared run's verdict: %r" % inv_d.get("verdict"))
        check("shrink=ledger:+1,register:-1 member_net=+0" in inv_d.get("shrink", ""),
              "A6c: the per-member line must show the offsetting pair: %r" % inv_d.get("shrink"))
        head = inv_d.get("head", "")
        check("delta=0" in head and "cur_nl=%d" % (led_nl + reg_nl) in head,
              "A6c: the family sum reads no growth at all here: %r" % head)
        for key, want in (("head", "burst_due=NO"), ("rounds_counted", "rate_due=NO"),
                          ("ceiling", "size_due=NO")):
            check(want in inv_d.get(key, ""),
                  "A6c: all three growth conditions must be silent in the same run, %s missing: %r"
                  % (want, inv_d.get(key)))
        reading(since, "L6 refuse rc=%d/%d declared rc=%d/%d | %s | %s | growth_all_NO=%s"
                % (rc, rc_c, rc_b, rc_d,
                   inv_b.get("shrink", "?"), inv_d.get("shrink", "?"),
                   yn(all(w in inv_d.get(k, "") for k, w in
                          (("head", "burst_due=NO"), ("rounds_counted", "rate_due=NO"),
                           ("ceiling", "size_due=NO"))))))
    finally:
        with open(ledger, "wb") as handle:
            handle.write(led_before)
        with open(register, "wb") as handle:
            handle.write(reg_before)


# ---------------------------------------------------------------- H legs: the host maps them
def make_copy():
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            abort("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        print("[U44-INFO] copy at %s (detached %s)"
              % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
    else:
        head_proc = sh(["git", "rev-parse", "HEAD"], cwd=WT)
        if head_proc.returncode != 0:
            abort("copy at %s has no HEAD, so it is not a git worktree any more: %s"
                  % (WT, decode(head_proc.stdout + head_proc.stderr)[:200]))
        head_at_entry = decode(head_proc.stdout).strip()
        print("[U44-INFO] reusing existing copy at %s (detached %s)" % (WT, head_at_entry))
        # A copy left behind by a crashed run can still carry an injection. Undo tracked-file edits in
        # this probe-owned tree first, so "the copy's bytes" means HEAD's bytes plus this round's sync.
        proc = sh(["git", "checkout", "--", "."], cwd=WT)
        if proc.returncode != 0:
            abort("copy cannot be reset to its own HEAD %s, so every arm below would measure "
                  "polluted bytes: %s" % (head_at_entry[:9],
                                          decode(proc.stdout + proc.stderr)[:200]))
        # Both copy baselines (H3's footprint md5s and Z0's status snapshot) are taken AFTER the sync
        # below, which means anything already sitting in this tree when the run starts is absorbed into
        # the baseline and can never show up later as a leftover. Name it instead of hiding it: after a
        # successful reset the tracked side should be empty, so what this line lists is untracked --
        # either one of this probe's own sync targets or something this probe did not write.
        left = decode(sh(["git", "-c", "core.quotePath=false", "status", "--porcelain",
                          "--untracked-files=all"], cwd=WT).stdout).splitlines()
        print("[U44-INFO] copy after reset: %d status line(s)%s"
              % (len(left), "" if not left else
                 " absorbed into this run's baseline: " + " | ".join(left[:6])))
    # The copy must sit on the SAME commit the main tree is on. A worktree created in an earlier round
    # stays detached at that round's HEAD, so its "growth since HEAD" reading silently measures against
    # an outdated baseline once new rounds land -- and a baseline that moves under the arms is exactly
    # what this probe keeps failing for elsewhere (see leg_h3's own baseline).
    head_main = decode(sh(["git", "rev-parse", "HEAD"], cwd=ROOT).stdout).strip()
    head_copy = decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()
    if head_main and head_copy != head_main:
        # git refuses a checkout that would overwrite an UNTRACKED file with the target commit's
        # tracked one. A previous round's sync wrote exactly that shape: this round's two new tools did
        # not exist at the old HEAD, so in the copy they sat untracked while the new HEAD tracks them.
        # What may be dropped is therefore bounded to this probe's own sync targets that the copy does
        # not track -- every one of them is rewritten from the main tree a few lines below, so the
        # removal loses no state and no file the copy itself ever owned.
        untracked = set(decode(sh(["git", "-c", "core.quotePath=false", "ls-files", "--others",
                                   "--exclude-standard"], cwd=WT).stdout).splitlines())
        dropped = []
        for p in watched_copy_files():
            if p.replace(os.sep, "/") in untracked and os.path.isfile(rel(p)):
                os.remove(rel(p))
                dropped.append(p.replace(os.sep, "/"))
        if dropped:
            print("[U44-INFO] dropped untracked sync targets from the copy: %s" % ", ".join(dropped))
        proc = sh(["git", "checkout", "--quiet", "--detach", head_main], cwd=WT)
        if proc.returncode != 0:
            abort("copy cannot follow main HEAD %s (copy at %s): %s"
                  % (head_main[:9], head_copy[:9],
                     decode(proc.stdout + proc.stderr)[:200]))
        print("[U44-INFO] copy advanced %s -> %s (main HEAD)" % (head_copy[:9], head_main[:9]))
    for p in watched_copy_files():
        main_bytes = read_bytes(os.path.join(ROOT, p))
        checked_out = read_bytes_or_none(rel(p))
        if checked_out is None or md5(checked_out) != md5(main_bytes):
            with open(rel(p), "wb") as handle:
                handle.write(main_bytes)
            print("[U44-INFO] synced %s into the copy (%s -> %s)%s"
                  % (p, md5(checked_out)[:8] if checked_out is not None else "absent",
                     md5(main_bytes)[:8], eol_verdict(checked_out, main_bytes)))
    return {p: md5(read_bytes(rel(p))) for p in watched_copy_files()}


def watched_copy_files():
    # The appendix is in this list because the gate the copy runs judges the FAMILY: a copy that kept
    # yesterday's companion file (or none at all, before the first close-out commits it) would read a
    # different cur_nl from the main tree, and CB/H3 would then be comparing two different sums.
    # inline_rewrite_check.py is in it one level down for the same reason: the gate imports
    # LEDGER_REL, REGISTER_REL and SPLIT_TRIGGER_DELTA from that module, and the copy is checked out
    # at HEAD -- before this round's commit that checkout still holds yesterday's module, so the gate
    # would die on an ImportError in copy_baseline instead of judging anything.
    return [CI_PS1, os.path.join("tools", "ci_exit_code_check.py"),
            os.path.join("tools", "ledger_size_gate.py"),
            os.path.join("tools", "inline_rewrite_check.py"),
            LEDGER_REL.replace("/", os.sep),
            REGISTER_REL.replace("/", os.sep)]


def eol_verdict(before, after):
    """Label for the hash pair make_copy prints when it re-syncs a file into the copy.

    core.autocrlf=true keeps text blobs LF in the index and writes them CRLF into a worktree, so a
    copy freshly reset to main HEAD can carry the SAME committed content under a different per-line
    terminator than the main worktree does. The md5 pair then changes every round with no content
    having changed, and reads as a drift to whoever meets it first. This says which of the two it
    was, measured on the two buffers the sync already holds. It judges nothing -- neither the sync
    above nor the run's exit code consults it; it only stops an honest hash from being misread.
    """
    if before is None:
        return " [no previous bytes: the copy did not have this file]"
    if before.replace(b"\r\n", b"\n") == after.replace(b"\r\n", b"\n"):
        return " [same modulo EOL -- line-ending decode of the same content, not a content drift]"
    return " [different BEYOND line endings -- a real content sync]"


def copy_baseline():
    """Take the COPY's own pre-injection readings, once, right after make_copy's sync.

    H3 is the undo-everything control, so its baseline must be the state the arms started from in THIS
    tree. G0/L0 read the main worktree: as soon as a round lands, that tree's growth is measured
    against the new HEAD while a copy created an earlier round was still on the old one -- so
    "restored == G0/L0" was a cross-tree comparison, not a control (and it went red on the first
    replay after the round landed). Not an arm: nothing about the delivered files is judged here, only
    the nothing-left-behind baseline is captured. A copy that is not green before the first injection
    aborts the run instead: there is then no baseline to restore to, and every H3 comparison after it
    would be measuring against None while its reading line kept printing a match count.
    """
    rc_g, out_g = run_geom(rel(CI_PS1))
    rc_s, out_s = run_gate(cwd=WT)
    if rc_g != 0:
        abort("CB: geometry tool not green in the copy before injection, rc=%s" % rc_g)
    if rc_s != 0:
        abort("CB: split gate not green in the copy before injection, rc=%s" % rc_s)
    geom = inv_lines(out_g)
    split = inv_lines(out_s)
    BASELINE_G0_COPY.clear()
    BASELINE_G0_COPY.update(dict((k, geom.get(k)) for k in
                                 ("header_codes", "executable_stops", "findings", "step_count")))
    BASELINE_L0_COPY.clear()
    BASELINE_L0_COPY.update(dict((k, split.get(k)) for k in
                                 ("head", "rounds_counted", "history_only_last3", "ceiling",
                                  "shrink", "verdict")))
    print("[U44-INFO] copy baseline: geom=%s split=%s | %s | %s"
          % (geom.get("verdict"), split.get("verdict"), split.get("head"),
             split.get("history_only_last3")))


def host_marker_present(text, marker):
    return any(marker in ln for ln in text.split("\n"))


def echo_lines(text):
    """The scripts' own finding lines as the host console shows them. ci.ps1 echoes every shown line
    through Write-Host "  $line", so the indent has to come off before a line can be recognised."""
    return [ln.strip() for ln in text.split("\n") if ln.strip()]


def leg_h1():
    since = arm_open()
    ci = rel(CI_PS1)
    original = read_bytes(ci)
    try:
        text = decode(original)
        n = text.count(HEADER_ROW_11)
        if not check(n == 1, "H1: header row 11 hit %d time(s) in the copy" % n):
            return
        with open(ci, "wb") as handle:
            handle.write(text.replace(HEADER_ROW_11, u"", 1).encode("utf-8"))
        rc, out = run_ci()
        check(rc == 11, "H1: expected host exit 11, got %s" % rc)
        check(host_marker_present(out, GEOM_STEP), "H1: step 1i heading absent from the host echo")
        check(any(ln.startswith("GEOMETRY A stop code 11") for ln in echo_lines(out)),
              "H1: the host did not echo the leg-A finding: %s" % tail(out))
        check("host exit-code geometry check failed (script exit 1)" in out,
              "H1: failure line missing: %s" % tail(out))
        check(not host_marker_present(out, CONFIGURE_MARKER),
              "H1: the host reached configure - a red gate must stop it first")
        check(SPLIT_STEP not in out, "H1: step 1j ran although 1i already stopped the host")
        reading(since, "H1 host rc=%d geom_step=%s no_configure=%s"
                % (rc, yn(SPLIT_STEP not in out and host_marker_present(out, GEOM_STEP)),
                   yn(not host_marker_present(out, CONFIGURE_MARKER))))
    finally:
        with open(ci, "wb") as handle:
            handle.write(original)


def leg_h2():
    since = arm_open()
    ledger = rel(LEDGER_REL.replace("/", os.sep))
    original = read_bytes(ledger)
    try:
        # 89 citation-free, marker-free filler lines: strictly above the recorded 88-line burst
        # trigger, and written so no earlier step (doc_check, doc_anchors, src_anchor_inventory,
        # dup_cn) has anything to judge -- otherwise the arm would prove the wrong step's mapping.
        filler = "".join(u"- 探针注入占位行 %04d（不含引用、不含路径、不含结论标记）\n" % i
                         for i in range(89)).encode("utf-8")
        if not check(original.endswith(b"\n"),
                     "H2: the ledger does not end with a newline, appending would merge the first "
                     "injected line into a real row"):
            return
        with open(ledger, "wb") as handle:
            handle.write(original + filler)
        rc, out = run_ci()
        check(rc == 12, "H2: expected host exit 12, got %s" % rc)
        check(host_marker_present(out, SPLIT_STEP), "H2: step 1j heading absent from the host echo")
        check(GEOM_OK in out, "H2: step 1i did not pass first, so 12 proves nothing about 1j: %s" % tail(out))
        check(any(ln.startswith("SPLITDUE condition=burst") for ln in echo_lines(out)),
              "H2: the host did not echo the burst finding: %s" % tail(out))
        check("ledger split trigger fired (script exit 1)" in out,
              "H2: failure line missing: %s" % tail(out))
        check(not host_marker_present(out, CONFIGURE_MARKER),
              "H2: the host reached configure - a red gate must stop it first")
        reading(since, "H2 host rc=%d geom_ok=%s split_step=%s no_configure=%s"
                % (rc, yn(GEOM_OK in out), yn(host_marker_present(out, SPLIT_STEP)),
                   yn(not host_marker_present(out, CONFIGURE_MARKER))))
    finally:
        with open(ledger, "wb") as handle:
            handle.write(original)


def tail(text, n=3):
    return " / ".join([ln for ln in text.split("\n") if ln.strip()][-n:])[-260:]


def leg_m2():
    """The host test is -ne 0, so a script that cannot read its input must also stop the host -- with
    its own code echoed, which is what keeps a broken input from being mistaken for a growth finding."""
    since = arm_open()
    ci = rel(CI_PS1)
    original = read_bytes(ci)
    old = u"$splitArgs = $hygienePre + @('tools/ledger_size_gate.py')"
    new = u"$splitArgs = $hygienePre + @('tools/ledger_size_gate.py', '--ledger', 'u44_no_such_ledger')"
    try:
        text = decode(original)
        if not check(text.count(old) == 1, "M2: step 1j's args line hit %d time(s)" % text.count(old)):
            return
        with open(ci, "wb") as handle:
            handle.write(text.replace(old, new, 1).encode("utf-8"))
        rc, out = run_ci()
        check(rc == 12, "M2: expected host exit 12, got %s" % rc)
        check("ledger split trigger fired (script exit 2)" in out,
              "M2: the failure line must echo the script's own code 2: %s" % tail(out))
        check(not host_marker_present(out, CONFIGURE_MARKER), "M2: the host reached configure")
        reading(since, "M2 host rc=%d echoes_script_code_2=%s"
                % (rc, yn("script exit 2" in out)))
    finally:
        with open(ci, "wb") as handle:
            handle.write(original)


def leg_h3(copy_before):
    """Undo-everything control: with the injections gone, both tools are green in the copy and their
    summary lines are byte-identical to the readings THIS copy printed before the first injection."""
    since = arm_open()
    for p, digest in copy_before.items():
        now = md5(read_bytes(rel(p)))
        check(now == digest, "H3: %s still differs from the post-sync bytes (%s vs %s)"
              % (p, now[:8], digest[:8]))
    rc_g, out_g = run_geom(rel(CI_PS1))
    rc_s, out_s = run_gate(cwd=WT)
    check(rc_g == 0, "H3: geometry tool in the copy rc=%s" % rc_g)
    check(rc_s == 0, "H3: split gate in the copy rc=%s" % rc_s)
    geom_inv = inv_lines(out_g)
    split_inv = inv_lines(out_s)
    pairs = (("geometry", geom_inv, BASELINE_G0_COPY,
              ("header_codes", "executable_stops", "findings", "step_count")),
             ("split gate", split_inv, BASELINE_L0_COPY,
              ("head", "rounds_counted", "history_only_last3", "ceiling", "shrink", "verdict")))
    total = sum(len(keys) for _l, _i, _b, keys in pairs)
    # The denominator of the reading line is itself part of what this arm judges: without this check a
    # key added to or dropped from the two tuples above would quietly move "restored lines=N/M" instead
    # of reddening the arm.
    check(total == 10, "H3: comparison surface drifted, expected 10 keys (4 geometry + 6 split), got %d"
          % total)
    diffs = []
    for label, inv, base, keys in pairs:
        for key in keys:
            if inv.get(key) != base.get(key):
                diffs.append("%s %s: %r vs %r" % (label, key, inv.get(key), base.get(key)))
    for d in diffs:
        check(False, "H3: restored reading differs from this copy's pre-injection reading -- " + d)
    # The reading line counts BOTH tools: run8's version derived its yes/no from the geometry keys
    # only, so it printed "identical=yes" on the same line the three red split-gate keys were named.
    reading(since, "H3 geom_rc=%s split_rc=%s restored lines=%d/%d identical to the copy baseline"
            % (rc_g, rc_s, total - len(diffs), total))


def leg_z0(copy_before):
    """Nothing this probe does may touch the main worktree, and nothing may be left in the copy."""
    since = arm_open()
    for p, digest in MAIN_BEFORE.items():
        now = md5(read_bytes(os.path.join(ROOT, p)))
        check(now == digest, "Z0: main worktree %s changed under the probe (%s vs %s)"
              % (p, now[:8], digest[:8]))
    status_now = sh(["git", "status", "--porcelain"], cwd=ROOT).stdout
    check(status_now == STATUS_BEFORE,
          "Z0: main porcelain changed: %r" % decode(status_now)[:200])
    # Not "the copy's docs are clean" -- measured, that can never hold while the sync runs: the copy
    # checks the ledger out with CRLF (core.autocrlf=true) and the delivered bytes are LF, so the sync
    # alone makes git report " M" for docs even though the content equals HEAD's blob. Expecting clean
    # here would be U-43's W-1 bug again -- a premise about a state this probe itself forbids. What
    # proves "nothing left injected" is the footprint md5 below plus THIS equality: any leftover byte
    # change, or any file left behind, moves the copy's status off its own post-sync baseline.
    copy_status_now = sh(["git", "status", "--porcelain"], cwd=WT).stdout
    check(copy_status_now == COPY_STATUS_BEFORE,
          "Z0: copy porcelain moved off its post-sync baseline: %r" % decode(copy_status_now)[:200])
    for p, digest in copy_before.items():
        now = md5(read_bytes(rel(p)))
        check(now == digest, "Z0: copy %s moved during the run (%s vs %s)" % (p, now[:8], digest[:8]))
    reading(since, "Z0 main md5=%d unchanged, porcelain unchanged=%s, copy status unchanged=%s "
                   "(copy baseline: %d line(s))"
            % (len(MAIN_BEFORE), yn(status_now == STATUS_BEFORE),
               yn(copy_status_now == COPY_STATUS_BEFORE),
               len([ln for ln in decode(COPY_STATUS_BEFORE).split("\n") if ln])))


def require_scratch_lock(name):
    """Refuse to start while another run holds the shared scratch (build/u44_probe: one copy
    worktree, one legs directory). msvcrt advisory locks belong to a process and the kernel drops
    them when it dies, which is why this is not a pid-in-a-file check -- measured on this box,
    os.kill(dead_pid, 0) raises nothing, so a liveness test would treat every crashed run as still
    holding the scratch and wedge the probe. Called from main(), never at import: the self-proof
    scripts load this file as a module while they hold the lock themselves."""
    fd = os.open(SCRATCH_LOCK, os.O_CREAT | os.O_RDWR)
    try:
        msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
    except OSError as exc:
        os.close(fd)
        print("[LOCK-BUSY] %s: another run holds %s (%s). The self-proof scripts and this probe "
              "share one copy worktree and one legs directory, so a second one started now would "
              "measure the first one's teardown and print a false OPEN. Run them one at a time. "
              "(exit code 4 is this lock; no judgement was made and no scratch was touched)"
              % (name, SCRATCH_LOCK, exc))
        raise SystemExit(4)
    os.write(fd, ("owner=%s pid=%d\n" % (name, os.getpid())).encode("ascii", "replace"))
    # No pid on this line: the replay convention is byte-identical output from the same command run
    # twice, and a pid differs by construction. The lock file itself still records owner+pid.
    print("[U44-LOCK] acquired=%s owner=%s" % (SCRATCH_LOCK, name))
    return fd


def main():
    global STATUS_BEFORE, COPY_STATUS_BEFORE
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        # A traceback that echoes one of THIS file's Chinese source lines dies on the cp936 stderr
        # halfway through, which hides the exception it was printing.
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    if not os.path.isdir(LOGDIR):
        os.makedirs(LOGDIR)
    require_scratch_lock("U44_geometry_split_probe")

    # Every watched file gets snapshotted below and re-read in Z0, and read_bytes() has no answer for a
    # path that is not there -- so a missing family member is named and exits here rather than as a
    # traceback that never reaches the [U44-SUMMARY] block.
    missing = [p for p in (LEDGER_REL, REGISTER_REL)
               if not os.path.isfile(os.path.join(ROOT, p.replace("/", os.sep)))]
    if missing:
        abort("a watched family member is not on disk, so no baseline can be taken: "
              + ", ".join(m.encode("unicode_escape").decode("ascii") for m in missing))

    MAIN_BEFORE.update(dict((p, md5(read_bytes(os.path.join(ROOT, p))))
                            for p in watched_copy_files()))
    STATUS_BEFORE = sh(["git", "status", "--porcelain"], cwd=ROOT).stdout
    print("[U44-INFO] main before: " + " ".join("%s=%s" % (os.path.basename(p), d[:8])
                                                for p, d in MAIN_BEFORE.items()))

    copy_before = make_copy()
    print("[U44-INFO] copy footprint: " + " ".join(
        "%s=%s" % (os.path.basename(p), md5(read_bytes(rel(p)))[:8]) for p in watched_copy_files()))
    COPY_STATUS_BEFORE = sh(["git", "status", "--porcelain"], cwd=WT).stdout
    copy_baseline()

    legs_g()
    l0_text = leg_l0()
    leg_l1()
    leg_l3()
    leg_l4()
    leg_lm()
    leg_l5(l0_text)
    leg_l6()
    leg_h1()
    leg_h2()
    leg_m2()
    leg_h3(copy_before)
    leg_z0(copy_before)

    sweep_legs()
    print("\n[U44-SUMMARY] arms=%d (%s) failures=%d"
          % (len(ARMS), ",".join(ARMS), len(FAILURES)))
    for f in FAILURES:
        print("[U44-SUMMARY]   " + f)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())

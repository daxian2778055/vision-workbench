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
       with exit 12 before configure.
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
  L0  gate baseline         delivered bytes -> rc=0, and its history_only_last3 reading equals the
                            sum this probe computes independently with one git diff --numstat range
  L1  burst boundary        --base-lines derived from the gate's own cur_nl (delta 88 / delta 89):
                            burst off at 88, on at 89; fired list, SPLITDUE lines and exit code must
                            all follow from the three condition flags of that same run
  L3  rate arithmetic       rate_window() on synthetic sequences: one round not judged, 190 green,
                            200 green (strict >), 210 red, a 4th round drops out of the window
  L4  size ceiling          synthetic 6000-line file -> rc=0; synthetic 6001-line file -> rc=1 size only
  L-M unreadable ledger     --ledger <missing> -> rc=2
  H1  host maps 11          copy-only defect (delete the header row for 11, same shape as G-A, so the
                            geometry step is the FIRST step that sees it) -> ci.ps1 exit 11, echo shows
                            the GEOMETRY finding, no "=== Configure"
  H2  host maps 12          copy-only growth (89 citation-free filler lines appended to the copy's
                            ledger, so no earlier step judges it) -> ci.ps1 exit 12, step 1i's OK line
                            is present (proof the stop came from 1j), no "=== Configure"
  M2  host maps 2           copy-only temporary edit: step 1j passes --ledger <missing> -> ci.ps1 still
                            exit 12 AND the failure line reads "script exit 2" (the host test is
                            -ne 0, it does not special-case 1). Reverted right after.
  H3  restored              injections removed -> both scripts rc=0 in the copy and their summary
                            lines byte-identical to CB's readings for THIS copy, not to G0/L0's:
                            those read the main worktree, whose growth baseline moves every time a
                            round lands, so a copy-vs-main comparison is a cross-tree check, not a
                            control. Its reading line counts matched lines over all nine compared
                            keys (a yes/no derived from one tool only once printed "identical=yes"
                            on the same line as three red lines from the other tool).
  CB  copy baseline         (not an arm) the copy's own two tool readings, taken after make_copy's
                            sync and before the first injection; H3 and the copy's git status both
                            anchor on it.
  Z0  snapshots             main worktree: md5 + porcelain of the watched files, re-checked last. Copy:
                            its footprint md5s AND its git status must both equal the baselines taken
                            right after the sync, before any injection. Not "git status is clean" -- the
                            sync alone leaves docs marked M, because the copy checks the ledger out with
                            CRLF (core.autocrlf=true) while the delivered bytes are LF.

Not proven here, and said so: the host GREEN path through 1i and 1j (a green host run needs
configure+build+ctest, which the copy does not carry) is taken from this round's end-to-end close-out
run on the main worktree, exactly as U-43 表 3 末行 states for step 1h. The checker's own exit 3 is the
wrapper's except branch (code shape only); no leg manufactures a crash.

One host shape the H legs have to account for: ci.ps1 prints every script finding line through
Write-Host "  $line", so on the console "GEOMETRY ..." and "SPLITDUE ..." carry a two-space indent.
echo_lines() strips it before matching; matching raw lines reads a real red as a missing finding.

Run:  python tools/probes/U44_geometry_split_probe.py         (all arms)
      python tools/probes/U44_geometry_split_probe.py --keep  (leave the copy + logs for reading)
Exit: 0 = every arm behaved as pinned; 1 = at least one arm diverged. Output is ASCII-only.
"""
import hashlib
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
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

CI_PS1 = os.path.join("tools", "ci.ps1")
GEOM_TOOL = "tools/ci_exit_code_check.py"
SPLIT_TOOL = "tools/ledger_size_gate.py"
# The gap plan, spelled the way the tools spell it (the console here is cp936).
LEDGER_REL = u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212.md"

GEOM_STEP = "=== Host exit-code geometry self-check ==="
SPLIT_STEP = "=== Ledger growth split trigger ==="
GEOM_OK = "host exit-code geometry OK"
SPLIT_OK = "ledger split trigger not due"
CONFIGURE_MARKER = "=== Configure"

# Pinned from the delivered bytes (G0 asserts them, so a drift in the file cannot pass here).
EXPECTED_COUNTS = "0:2 1:6 2:1 3:2 4:1 5:1 6:1 7:1 8:1 9:1 10:1 11:1 12:1"
EXPECTED_HEADER = "0 1 2 3 4 5 6 7 8 9 10 11 12"
EXPECTED_STEPS = "13"
# NOT pinned as an absolute value: the gate's history_only_last3 takes the last three commits that
# touched the ledger, so the window moves one commit forward with every commit that lands -- an
# absolute pin here would go red for a reviewer replaying this probe at the delivered commit for a
# reason that has nothing to do with the gate. L0 pins the AGREEMENT instead (see leg_l0).
HEADER_ROW_11 = u"      11 = host exit-code geometry drifted against its frozen registration\n"
STEP_1I_HEADING = u'Write-Step "Host exit-code geometry self-check"'
STEP_1J_HEADING = u'Write-Step "Ledger growth split trigger"'
STEP_1H_STOP = u"    exit 10\n"

FAILURES = []
# The main-tree readings, filled by the two baseline legs G0 / L0 (those legs judge the delivered bytes).
BASELINE_G0 = {}
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
    BASELINE_G0.clear()
    BASELINE_G0.update(dict((k, inv.get(k)) for k in
                            ("header_codes", "executable_stops", "findings", "step_count")))
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
    leg_g("GB", HEADER_ROW_11, HEADER_ROW_11 + u"      13 = reserved for a gate that is not wired\n",
          ("B",), ("B header row 13 has no executable stop",))
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
    # Cross-check the gate's own history reading with ONE git command that does the arithmetic
    # itself, instead of re-running the per-commit parser: the range numstat of the last three
    # commits must equal the sum of their individual nets.
    # Standing assumption: the gate sums the last three commits that TOUCHED the ledger, HEAD~3..HEAD
    # takes the last three commits full stop. They can only be expected to agree while every recent
    # commit updates the ledger (true for this repo's close-out flow). If a commit ever lands that
    # does not touch the ledger, this leg prints the gap as a failure -- it does not adjudicate
    # which of the two definitions is right.
    proc = sh(["git", "diff", "--numstat", "HEAD~3", "HEAD", "--", LEDGER_REL])
    line = decode(proc.stdout).strip()
    m = re.match(r"^(\d+)\t(\d+)\t", line)
    if not check(proc.returncode == 0 and m is not None,
                 "L0: cross-check git diff unreadable: rc=%s %r" % (proc.returncode, line[:120])):
        reading(since, "L0: cross-check unavailable")
        return text
    cross = int(m.group(1)) - int(m.group(2))
    printed = re.search(r"history_only_last3=(\d+)", text)
    check(printed is not None, "L0: gate printed no history_only_last3 reading")
    # The two algorithms agreeing is the assertion; the value itself is not pinned -- see the note
    # above EXPECTED constants. Guard against a vacuous agreement (an empty window reads 0 on both sides).
    scanned = re.search(r"scanned_commits=(\d+)", inv.get("anchor", ""))
    check(scanned is not None and int(scanned.group(1)) >= 3,
          "L0: history window has fewer than 3 commits to sum: %r" % inv.get("anchor"))
    check(printed is not None and int(printed.group(1)) == cross,
          "L0: gate reads %s but git diff HEAD~3..HEAD reads %d"
          % (printed.group(1) if printed else "?", cross))
    reading(since, "L0 rc=%d history3=%s cross_check=%d ceiling=%s"
            % (rc, printed.group(1) if printed else "?", cross, inv.get("ceiling")))
    return text


def leg_l1():
    """The burst condition's boundary: delta 88 leaves burst off, delta 89 turns it on (the recorded
    trigger is strictly greater). Both bases come from the gate's own cur_nl reading, so the two runs
    differ in exactly one injected number whatever the ledger measures today.

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
    reading(since, "L3 cases=%d boundary=%s window=%s sum=%d"
            % (len(cases), "200/201", ",".join(s for s, _n in window), total))


def synthetic_ledger(name, lines):
    if not os.path.isdir(LEGS):
        os.makedirs(LEGS)
    path = os.path.join(LEGS, name)
    with open(path, "wb") as handle:
        handle.write(b"\n".join(b"- filler line %05d" % i for i in range(lines)) + b"\n")
    return path


def leg_l4():
    """Condition 3: the absolute size ceiling, one line either side of it, with the burst condition
    neutralised by --base-lines so only size can answer."""
    for size, want_rc in ((6000, 0), (6001, 1)):
        since = arm_open()
        path = synthetic_ledger("synthetic_%d.md" % size, size)
        rel_path = os.path.relpath(path, ROOT).replace(os.sep, "/")
        rc, text = run_gate(["--ledger", rel_path, "--base-lines", str(size)])
        inv = inv_lines(text)
        check(rc == want_rc, "L4 size=%d: rc=%s expected %s | %s" % (size, rc, want_rc, text[-160:]))
        check("cur_nl=%d" % size in inv.get("head", ""), "L4 size=%d head line: %r"
              % (size, inv.get("head")))
        check(("size_due=YES" if want_rc else "size_due=NO") in inv.get("ceiling", ""),
              "L4 size=%d ceiling line: %r" % (size, inv.get("ceiling")))
        if want_rc:
            check("fired=size" in inv.get("split_project_due", ""),
                  "L4 size=6001 fired line: %r" % inv.get("split_project_due"))
        check("INV ledger=%s" % rel_path in text,
              "L4 size=%d: the judged file must be printed: %r" % (size, inv.get("ledger")))
        reading(since, "L4 size=%d rc=%d | %s" % (size, rc, inv.get("split_project_due")))


def leg_lm():
    since = arm_open()
    rc, text = run_gate(["--ledger", "u44_no_such_ledger.md"])
    check(rc == 2, "L-M: missing ledger must read rc=2, got %s | %s" % (rc, text[-160:]))
    check(text.startswith("ERROR"), "L-M: expected an ERROR line, got %r" % text[:120])
    check("SPLITDUE" not in text and "verdict=GREEN" not in text,
          "L-M: a broken input must not read as a finding or as clean: %r" % text[:160])
    reading(since, "L-M rc=%d | %s" % (rc, text.split("\n")[0]))


# ---------------------------------------------------------------- H legs: the host maps them
def make_copy():
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        print("[U44-INFO] copy at %s (detached %s)"
              % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
    else:
        print("[U44-INFO] reusing existing copy at %s (detached %s)"
              % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
        # A copy left behind by a crashed run can still carry an injection. Undo tracked-file edits in
        # this probe-owned tree first, so "the copy's bytes" means HEAD's bytes plus this round's sync.
        sh(["git", "checkout", "--", "."], cwd=WT)
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
                                   "--exclude-standard"], cwd=WT).stdout).split("\n"))
        dropped = []
        for p in watched_copy_files():
            if p.replace(os.sep, "/") in untracked and os.path.isfile(rel(p)):
                os.remove(rel(p))
                dropped.append(p.replace(os.sep, "/"))
        if dropped:
            print("[U44-INFO] dropped untracked sync targets from the copy: %s" % ", ".join(dropped))
        proc = sh(["git", "checkout", "--quiet", "--detach", head_main], cwd=WT)
        if proc.returncode != 0:
            raise SystemExit("copy cannot follow main HEAD %s (copy at %s): %s"
                             % (head_main[:9], head_copy[:9],
                                decode(proc.stdout + proc.stderr)[:200]))
        print("[U44-INFO] copy advanced %s -> %s (main HEAD)" % (head_copy[:9], head_main[:9]))
    for p in watched_copy_files():
        main_bytes = read_bytes(os.path.join(ROOT, p))
        checked_out = read_bytes_or_none(rel(p))
        if checked_out is None or md5(checked_out) != md5(main_bytes):
            with open(rel(p), "wb") as handle:
                handle.write(main_bytes)
            print("[U44-INFO] synced %s into the copy (%s -> %s)"
                  % (p, md5(checked_out)[:8] if checked_out is not None else "absent",
                     md5(main_bytes)[:8]))
    return {p: md5(read_bytes(rel(p))) for p in watched_copy_files()}


def watched_copy_files():
    return [CI_PS1, os.path.join("tools", "ci_exit_code_check.py"),
            os.path.join("tools", "ledger_size_gate.py"), LEDGER_REL.replace("/", os.sep)]


def copy_baseline():
    """Take the COPY's own pre-injection readings, once, right after make_copy's sync.

    H3 is the undo-everything control, so its baseline must be the state the arms started from in THIS
    tree. G0/L0 read the main worktree: as soon as a round lands, that tree's growth is measured
    against the new HEAD while a copy created an earlier round was still on the old one -- so
    "restored == G0/L0" was a cross-tree comparison, not a control (and it went red on the first
    replay after the round landed). Not an arm: nothing about the delivered files is judged here, only
    the nothing-left-behind baseline is captured. A copy that is not green before the first injection
    is still reported, because every H3 comparison after it would then be meaningless.
    """
    rc_g, out_g = run_geom(rel(CI_PS1))
    rc_s, out_s = run_gate(cwd=WT)
    check(rc_g == 0, "CB: geometry tool not green in the copy before injection, rc=%s" % rc_g)
    check(rc_s == 0, "CB: split gate not green in the copy before injection, rc=%s" % rc_s)
    geom = inv_lines(out_g)
    split = inv_lines(out_s)
    BASELINE_G0_COPY.clear()
    BASELINE_G0_COPY.update(dict((k, geom.get(k)) for k in
                                 ("header_codes", "executable_stops", "findings", "step_count")))
    BASELINE_L0_COPY.clear()
    BASELINE_L0_COPY.update(dict((k, split.get(k)) for k in
                                 ("head", "rounds_counted", "history_only_last3", "ceiling",
                                  "verdict")))
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
              ("head", "rounds_counted", "history_only_last3", "ceiling", "verdict")))
    total = sum(len(keys) for _l, _i, _b, keys in pairs)
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
    keep = "--keep" in sys.argv[1:]

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
    leg_l0()
    leg_l1()
    leg_l3()
    leg_l4()
    leg_lm()
    leg_h1()
    leg_h2()
    leg_m2()
    leg_h3(copy_before)
    leg_z0(copy_before)

    if not keep:
        for path in (LEGS,):
            if os.path.isdir(path):
                shutil.rmtree(path, ignore_errors=True)
    print("\n[U44-SUMMARY] arms=%d (%s) failures=%d"
          % (len(ARMS), ",".join(ARMS), len(FAILURES)))
    for f in FAILURES:
        print("[U44-SUMMARY]   " + f)
    return 0 if not FAILURES else 1


if __name__ == "__main__":
    sys.exit(main())

"""U-47 replayable probe: the cited-log presence gate must bite on its own, and a red verdict must
actually stop the host (tenth-round review, item 3 -- "the log-name non-reuse convention needs a
machine gate", which this repo had already listed as a candidate itself).

What it proves (advance plan 3.51): tools/log_citation_gate.py resolves every 日志 citation in the
gap plan against this machine and compares the unresolved set against MISSING_BASELINE. G-1/G-2's
lesson applies to any new script: **a gate that can go red but is called by nothing is a dead gate**,
so the half that matters is the wiring -- step 1k of ci.ps1 and host code 13 -- and this probe is the
only place that chain is walked.

Arms (defects are injected either in a --ledger copy under build/u47_probe/ on the main worktree, or
inside a one-shot `git worktree add --detach` copy under build/; no tracked file of the main worktree
is ever written to):

  C1  code occupancy + registration
                      ci.ps1 at the pinned revision BASE_REVISION has 0 executable `exit 13`; the
                      delivered one has exactly 1, carries step 1k's Write-Step line and the header
                      row for 13, leaves the per-code counts of 3..12 unmoved, and the delivered
                      tools/ci_exit_code_check.py still reads that geometry GREEN (i.e. the new code
                      was registered on purpose, not smuggled in)
  G1  main baseline   delivered gate on the real ledger -> rc=0, added=0, verdict GREEN
  G2  injected red    ledger copy + exactly one appended line citing U47_no_such_log.txt -> rc=1,
                      findings=1, and the ADDED line is pinned byte-for-byte
  G3  restore         the injected line removed (copy byte-equal to the ledger again) -> rc=0 and
                      every citation count equals G1's, so the only variable that moved was that line
  G4  REMOVED is not red
                      copy where the one frozen baseline citation is rewritten onto a file that does
                      exist -> removed=1, added=0, rc=0: repairing an old citation can never be
                      blocked by the gate that measures it
  M1  script code 2   gate with an unreadable --ledger -> rc=2 + the input-error line (2 is not a
                      finding)
  S1  declared skip   gate in the copy, which has no build/*_probe tree -> CITE-SKIP printed,
                      verdict=SKIP, rc=0 (a fresh clone is not stopped, and is not silently passed)
  H1  host red        copy: step 1k pointed at the injected ledger -> gate rc=1 with added=baseline+1
                      AND ci.ps1 exit 13, echo stops at step 1k (no === Configure), the ADDED line is
                      echoed to the console as the script printed it; then restored
  M2  host maps 2     copy-only temporary edit: step 1k passes --ledger <missing> -> ci.ps1 still
                      exit 13 AND the failure line reads "script exit 2". Reverted right after.
  Z0  snapshots       main worktree: md5 + porcelain of the watched files, re-checked last. Copy: its
                      footprint must not move during the run, and no injected file may be left in it.

Not proven here, and said so (advance plan 3.51 没证到的那半): the gate's own exit 3 is the wrapper's
`except` branch (its code shape only); no leg manufactures a crash. The host GREEN path through step
1k is not run inside this probe -- a green host run needs configure+build+ctest, which the copy does
not carry; that leg is taken from this round's end-to-end close-out run on the main worktree. Also:
the copy cannot have the machine-local scratch the gate resolves against, so H1's absolute `added`
count is the copy's own dirt -- what is pinned there is the +1 the injected citation causes.

Run:  python tools/probes/U47_citation_host_exit_probe.py        (C1 G1 G2 G3 G4 M1 S1 H1 M2 Z0)
      python tools/probes/U47_citation_host_exit_probe.py --keep (leave the copy + logs for reading)
Exit: 0 = every arm behaved as pinned; 1 = at least one arm diverged. Output is ASCII-only.
"""
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from inline_rewrite_check import LEDGER_REL  # noqa: E402

LOGDIR = os.path.join(ROOT, "build", "u47_probe")
WT = os.path.join(LOGDIR, "wt")
COPY_SCRATCH_REL = os.path.join("build", "u47_probe")
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

CI_PS1 = os.path.join("tools", "ci.ps1")
GATE = os.path.join("tools", "log_citation_gate.py")
CI_CHECK = os.path.join("tools", "ci_exit_code_check.py")

# The revision before step 1k was wired -- pinned, not HEAD (seventh-round review W-1: a premise
# about "the code was free before" read from HEAD goes red for every later replay, for a reason that
# has nothing to do with the wiring). fc1229477... is U-46's close-out commit, ci.ps1's parent for 13.
BASE_REVISION = "fc12294775f14d8032e877c6f4f55aaba783ad20"

STEP_HEADING = "=== Cited-log presence check ==="
STEP_WRITE = 'Write-Step "Cited-log presence check"'
HEADER_ROW = "13 = a log the gap plan cites cannot be opened on this machine"
ECHO_RX = re.compile(r"^(CITE|ERROR|INV citation_baseline|INV verdict)")

CITE_ARGS_NEEDLE = b"$citeArgs = $hygienePre + @('tools/log_citation_gate.py')"

# One citation the machine cannot resolve, appended as exactly one line. 日志 written as escapes so
# this file stays ASCII (the console is cp936 and repo_hygiene wants .py files ASCII-decodable).
INJECT_MARK = u"\u65e5\u5fd7 "
INJECT_NAME = "U47_no_such_log.txt"
INJECT_LINE = u"%s%s" % (INJECT_MARK, INJECT_NAME)
ADDED_INJECT = "CITEADD dir=ADDED cite=%s" % INJECT_NAME

# The one item in MISSING_BASELINE, and a real file it can be rewritten onto (G4).
BASELINE_CITE = "build/u38_probe/u38_A1..A10.raw.txt"
BASELINE_CITE_FIX = "build/u38_probe/u38_A1.raw.txt"

MISSING_LEDGER = "u47_no_such_ledger.md"

ADDED_RX = re.compile(r"INV citation_baseline=(\d+) observed_missing=(\d+) added=(\d+) removed=(\d+)")
CITE_LINE_RX = re.compile(r"citations_distinct=(\d+) present_exact=(\d+) present_scratch=(\d+) "
                          r"missing=(\d+)")

FAILURES = []


def fail(text):
    FAILURES.append(text)
    print("[U47-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def yn(cond):
    return "yes" if cond else "NO"


def arm_open():
    return len(FAILURES)


def reading(since, text):
    print("[%s] %s" % ("U47-OK" if len(FAILURES) == since else "U47-FAIL", text))


def md5(data):
    return hashlib.md5(data).hexdigest()


def read_bytes(path):
    with io.open(path, "rb") as handle:
        return handle.read()


def sh(args, cwd=None, timeout=1800):
    return subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)


def decode(raw):
    """One place that kills the trailing CR: print() on this console emits \\r\\n, and the exact-
    equality legs below (pinned ADDED line, echoed-line comparison) otherwise fail on an invisible
    carriage return."""
    return raw.decode("utf-8", errors="replace").replace("\r\n", "\n")


def run_gate(args_extra=None, cwd=None, log_name=None):
    cmd = [sys.executable, GATE] + list(args_extra or [])
    proc = sh(cmd, cwd=cwd or ROOT)
    text = decode(proc.stdout) + decode(proc.stderr)
    if log_name:
        with io.open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    return proc.returncode, text


def run_ci(log_name, cwd=None):
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1],
              cwd=cwd or WT)
    text = decode(proc.stdout) + decode(proc.stderr)
    with io.open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return proc.returncode, text


def echoed(text):
    return [ln for ln in text.split("\n") if ECHO_RX.match(ln)]


def added_counts(text):
    for ln in text.split("\n"):
        m = ADDED_RX.match(ln)
        if m:
            return [int(x) for x in m.groups()]
    return None


def citation_counts(text):
    for ln in text.split("\n"):
        if ln.startswith("CITE scratch_trees="):
            m = CITE_LINE_RX.search(ln)
            if m:
                return [int(x) for x in m.groups()]
    return None


def citeadd(text):
    return [ln for ln in text.split("\n") if ln.startswith("CITEADD ")]


def citeremoved(text):
    return [ln for ln in text.split("\n") if ln.startswith("CITEREMOVED ")]


def ledger_bytes():
    return read_bytes(os.path.join(ROOT, LEDGER_REL))


def write_copy_ledger(name, body):
    """A doctored ledger copy under the main worktree's own scratch (never a tracked path)."""
    path = os.path.join(LOGDIR, name)
    check(not os.path.exists(path), "setup: %s already exists" % name)
    with io.open(path, "wb") as handle:
        handle.write(body)
    return path


# ------------------------------------------------------------------ arms on the main worktree

def arm_c1_occupancy():
    since = arm_open()
    base_proc = sh(["git", "show", "%s:%s" % (BASE_REVISION, CI_PS1.replace(os.sep, "/"))], cwd=ROOT)
    check(base_proc.returncode == 0,
          "C1: cannot read the pinned baseline revision %s's ci.ps1" % BASE_REVISION[:12])
    base_txt = decode(base_proc.stdout)
    delivered_txt = decode(read_bytes(os.path.join(ROOT, CI_PS1)))

    def exec_exits(text):
        return [m.group(1) for m in re.finditer(r"(?m)^\s*exit\s+(\d+)\s*$", text)]

    base_codes = exec_exits(base_txt)
    now_codes = exec_exits(delivered_txt)
    check(base_codes.count("13") == 0,
          "C1: baseline revision %s already had an executable exit 13 (%d) -- the code would not "
          "have been free" % (BASE_REVISION[:12], base_codes.count("13")))
    check(now_codes.count("13") == 1,
          "C1: delivered ci.ps1 has %d executable `exit 13`, expected exactly 1"
          % now_codes.count("13"))
    for code in ("3", "4", "5", "6", "7", "8", "9", "10", "11", "12"):
        check(now_codes.count(code) == base_codes.count(code),
              "C1: code %s count moved (%d -> %d); step 1k must not repurpose another step's code"
              % (code, base_codes.count(code), now_codes.count(code)))
    step_write = check(STEP_WRITE in delivered_txt, "C1: step 1k's Write-Step line is missing")
    header_row = check(HEADER_ROW in delivered_txt,
                       "C1: the header exit-code table was not updated for 13 -- a host code that is "
                       "not in the table is a code nobody can look up")
    check_proc = sh([sys.executable, CI_CHECK], cwd=ROOT)
    text_check = decode(check_proc.stdout) + decode(check_proc.stderr)
    check(check_proc.returncode == 0,
          "C1: tools/ci_exit_code_check.py reads the delivered geometry as drifted (rc=%s) -- the "
          "new step was not registered in its frozen tables: %s"
          % (check_proc.returncode, [ln for ln in text_check.split("\n")
                                     if ln.startswith("GEOMETRY")][:3]))
    reading(since, "C1 base=%s exit13=%d delivered_exit13=%d codes_3_to_12=%s step_write=%s "
                   "header_row=%s geometry_rc=%d"
            % (BASE_REVISION[:7], base_codes.count("13"), now_codes.count("13"),
               " ".join("%s:%d" % (c, now_codes.count(c)) for c in
                        ("3", "4", "5", "6", "7", "8", "9", "10", "11", "12")),
               yn(step_write), yn(header_row), check_proc.returncode))


def arm_g1_baseline():
    since = arm_open()
    rc, text = run_gate(log_name="U47_G1_gate.txt")
    counts = added_counts(text)
    check(rc == 0, "G1: delivered gate on the real ledger rc=%s, expected 0" % rc)
    check(counts is not None, "G1: the gate did not print its INV citation_baseline= line")
    if counts:
        check(counts[2] == 0, "G1: the real ledger already has %d ADDED citation(s): %r"
              % (counts[2], citeadd(text)))
    check("INV verdict=GREEN" in text, "G1: baseline verdict is not GREEN: %r" % echoed(text))
    reading(since, "G1 rc=%d baseline=%s | %s" % (rc, counts, citation_counts(text)))
    return citation_counts(text)


def arm_g2_g3(g1_counts):
    """Inject one citation -> red with the ADDED line pinned; restore -> counts back to G1's."""
    since = arm_open()
    original = ledger_bytes()
    tail = INJECT_LINE.encode("utf-8") + b"\n"
    if not original.endswith(b"\n"):
        tail = b"\n" + tail
    path = write_copy_ledger("U47_G2_ledger.md", original + tail)
    try:
        grew_by = len(read_bytes(path)) - len(original)
        check(grew_by == len(tail),
              "G2 setup: the copy grew by %d bytes, expected exactly %d (one appended line)"
              % (grew_by, len(tail)))
        check(read_bytes(path).startswith(original),
              "G2 setup: the copy is not the ledger plus a tail -- variables not controlled")
        rc_red, text_red = run_gate(["--ledger", path], log_name="U47_G2_gate.txt")
        counts_red = added_counts(text_red)
        check(rc_red == 1, "G2: gate on the injected copy rc=%s, expected 1" % rc_red)
        check(counts_red is not None and counts_red[2] == 1 and counts_red[3] == 0,
              "G2: the injected citation did not move exactly one ADDED and no REMOVED: %r"
              % counts_red)
        check(citeadd(text_red) == [ADDED_INJECT],
              "G2: the ADDED set is not exactly the injected citation: %r" % citeadd(text_red))
        check("INV findings=1" in text_red,
              "G2: findings count does not name the one injected citation: %r" % echoed(text_red))
        check("INV verdict=RED" in text_red, "G2: verdict line is not RED")
    finally:
        os.remove(path)

    since3 = arm_open()
    path3 = write_copy_ledger("U47_G3_ledger.md", original)
    try:
        rc_back, text_back = run_gate(["--ledger", path3], log_name="U47_G3_gate.txt")
        check(rc_back == 0, "G3: gate on the restored copy rc=%s, expected 0" % rc_back)
        check(not citeadd(text_back), "G3: a restored copy still reports ADDED: %r"
              % citeadd(text_back))
        counts_back = citation_counts(text_back)
        check(counts_back == g1_counts,
              "G3: the citation counts moved against G1 (%r -> %r) -- the red did not come from the "
              "single injected line alone" % (g1_counts, counts_back))
        reading(since3, "G3 restore rc=%d citations=%s equal_to_G1=%s added=%s"
                % (rc_back, counts_back, yn(counts_back == g1_counts), added_counts(text_back)))
    finally:
        os.remove(path3)
    reading(since, "G2 inject rc=%d added_line_pinned=%s findings=1 | tail_bytes=%d"
            % (rc_red, yn(citeadd(text_red) == [ADDED_INJECT]), len(tail)))


def arm_g4_removed_report_only():
    """Repairing the one frozen baseline citation must not be blocked by the gate that measures it."""
    since = arm_open()
    original = ledger_bytes()
    body = original.replace(BASELINE_CITE.encode("utf-8"), BASELINE_CITE_FIX.encode("utf-8"))
    if not check(body != original and body.count(BASELINE_CITE_FIX.encode("utf-8"))
                 == original.count(BASELINE_CITE.encode("utf-8")),
                 "G4 setup: the baseline citation is not the single string this leg rewrites "
                 "(hits=%d)" % original.count(BASELINE_CITE.encode("utf-8"))):
        return
    path = write_copy_ledger("U47_G4_ledger.md", body)
    try:
        rc, text = run_gate(["--ledger", path], log_name="U47_G4_gate.txt")
        check(rc == 0, "G4: fixing an old citation went red (rc=%s) -- REMOVED must be report-only"
              % rc)
        check(not citeadd(text), "G4: unexpected ADDED set while repairing: %r" % citeadd(text))
        removed = citeremoved(text)
        check(removed == ["CITEREMOVED dir=REMOVED cite=%s (report only)" % BASELINE_CITE],
              "G4: the REMOVED line is not pinned: %r" % removed)
        counts = added_counts(text)
        check(counts is not None and counts[2] == 0 and counts[3] == 1,
              "G4: INV line does not read added=0 removed=1: %r" % counts)
        reading(since, "G4 rc=%d added=0 removed=1 report_only=%s | %s"
                % (rc, yn(removed), counts))
    finally:
        os.remove(path)


def arm_m1_script_code2():
    since = arm_open()
    rc, text = run_gate(["--ledger", MISSING_LEDGER], log_name="U47_M1_gate.txt")
    check(rc == 2, "M1: gate with an unreadable --ledger rc=%s, expected 2" % rc)
    check("ERROR ledger not readable" in text,
          "M1: rc=2 did not come with the input-error line: %r" % text[:200])
    check(not citeadd(text), "M1: an input error printed findings: %r" % citeadd(text))
    reading(since, "M1 script rc=%d input_error_echoed=yes findings_printed=%d" % (rc, len(citeadd(text))))


# ------------------------------------------------------------------ copy machinery (as U-43's)

def purge_tree(path):
    """git writes loose objects read-only, so a plain rmtree can raise PermissionError on Windows;
    clear the write bit first and never swallow what is left (U-46 W-1's lesson, applied here)."""
    if not os.path.isdir(path):
        return True
    for root, dirs, files in os.walk(path):
        for entry in dirs + files:
            try:
                os.chmod(os.path.join(root, entry), 0o700)
            except OSError:
                pass
    try:
        shutil.rmtree(path)
    except OSError as exc:
        print("[U47-FAIL] teardown: %s: %s" % (path, exc))
        return not os.path.isdir(path)
    return not os.path.isdir(path)


def make_copy():
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        print("[U47-INFO] copy at %s (detached %s)"
              % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
    else:
        print("[U47-INFO] reusing existing copy at %s" % WT)
    for p in (CI_PS1, GATE, CI_CHECK):
        main_bytes = read_bytes(os.path.join(ROOT, p))
        with io.open(os.path.join(WT, p), "wb") as handle:
            handle.write(main_bytes)
        check(md5(read_bytes(os.path.join(WT, p))) == md5(main_bytes),
              "copy setup: %s did not sync" % p)
    return porcelain(WT)


def remove_copy():
    proc = sh(["git", "worktree", "remove", "--force", WT], cwd=ROOT)
    if proc.returncode != 0:
        print("[U47-WARN] git worktree remove said: " + decode(proc.stdout + proc.stderr).strip())
    sh(["git", "worktree", "prune"], cwd=ROOT)


def porcelain(cwd):
    return tuple(sorted(l.strip() for l in decode(
        sh(["git", "status", "--porcelain", "--", "src", "include", "tools", "docs"],
           cwd=cwd).stdout).splitlines() if l.strip()))


def copy_has_scratch():
    parent = os.path.join(WT, "build")
    if not os.path.isdir(parent):
        return False
    return any(d.endswith("_probe") and os.path.isdir(os.path.join(parent, d))
               for d in os.listdir(parent))


def arm_s1_declared_skip():
    """The copy has no build/*_probe tree, exactly like a fresh clone: the skip must be visible."""
    since = arm_open()
    check(not copy_has_scratch(),
          "S1: the copy already has a build/*_probe tree -- the skip leg would not be testing a "
          "clone-shaped machine")
    rc, text = run_gate(cwd=WT, log_name="U47_S1_gate.txt")
    skip = [ln for ln in text.split("\n") if ln.startswith("CITE-SKIP")]
    check(rc == 0, "S1: gate in a clone-shaped copy rc=%s, expected 0 (a skip must not stop a run)"
          % rc)
    check(len(skip) == 1, "S1: no visible CITE-SKIP line -- a silent skip is what W-3 was about: %r"
          % echoed(text))
    check("INV verdict=SKIP" in text, "S1: the skip is not labelled as such in the verdict: %r"
          % echoed(text))
    check(not citeadd(text) and not citeremoved(text),
          "S1: the skip leg still judged something: %r" % (citeadd(text) + citeremoved(text)))
    reading(since, "S1 rc=%d skip_lines=%d labelled=%s judged=%d"
            % (rc, len(skip), yn("INV verdict=SKIP" in text), len(citeadd(text))))


def arm_h1_host_red():
    """Point step 1k at a doctored ledger inside the copy -> gate red -> host exit 13 -> restore."""
    since = arm_open()
    scratch = os.path.join(WT, COPY_SCRATCH_REL)
    if not purge_tree(scratch):
        fail("H1: could not clear a leftover %s inside the copy" % COPY_SCRATCH_REL)
        return
    os.makedirs(scratch, exist_ok=True)
    with io.open(os.path.join(scratch, "U47_H1_anchor.txt"), "wb") as handle:
        handle.write(b"H1 anchor: its only job is to make build/*_probe exist in the copy, so the "
                     b"gate judges instead of skipping.\n")
    ci_path = os.path.join(WT, CI_PS1)
    original_ci = read_bytes(ci_path)
    if not check(original_ci.count(CITE_ARGS_NEEDLE) == 1,
                 "H1: cannot locate step 1k's argument line in the copy (%d hit(s))"
                 % original_ci.count(CITE_ARGS_NEEDLE)):
        purge_tree(scratch)
        return
    # One path, spelled the way the host command line and the gate both want it.
    ledger_posix = "build/u47_probe/U47_H1_ledger.md"
    ledger_abs = os.path.join(WT, "build", "u47_probe", "U47_H1_ledger.md")

    def patch_host():
        args = (b"$citeArgs = $hygienePre + @('tools/log_citation_gate.py', '--ledger', '%s')"
                % ledger_posix.encode("ascii"))
        with io.open(ci_path, "wb") as handle:
            handle.write(original_ci.replace(CITE_ARGS_NEEDLE, args, 1))

    def write_ledger(with_injection):
        body = read_bytes(os.path.join(WT, LEDGER_REL))
        tail = INJECT_LINE.encode("utf-8") + b"\n"
        if not body.endswith(b"\n"):
            tail = b"\n" + tail
        with io.open(ledger_abs, "wb") as handle:
            handle.write(body if not with_injection else body + tail)

    try:
        patch_host()
        write_ledger(False)
        rc_pre, text_pre = run_gate(cwd=WT, args_extra=["--ledger", ledger_posix],
                                    log_name="U47_H1_pre.txt")
        pre_counts = added_counts(text_pre)
        pre_added = len(citeadd(text_pre))
        check(rc_pre == 1 and pre_counts is not None and pre_counts[2] == pre_added,
              "H1: the uninjected copy did not read red as expected (rc=%s INV=%r CITEADD=%d) -- "
              "the +1 leg would be measuring nothing" % (rc_pre, pre_counts, pre_added))
        check(not any(INJECT_NAME in ln for ln in citeadd(text_pre)),
              "H1: the injected citation is already present before injection")

        write_ledger(True)
        rc_tool, text_tool = run_gate(cwd=WT, args_extra=["--ledger", ledger_posix],
                                      log_name="U47_H1_gate.txt")
        post_counts = added_counts(text_tool)
        check(rc_tool == 1, "H1: gate on the injected copy rc=%s, expected 1" % rc_tool)
        check(post_counts is not None and pre_counts is not None
              and post_counts[2] == pre_counts[2] + 1,
              "H1: one injected citation moved added from %r to %r -- expected +1"
              % (pre_counts, post_counts))
        check(any(ln.endswith(INJECT_NAME) for ln in citeadd(text_tool)),
              "H1: the injected ADDED line is missing from the script output")

        rc_ci, text_ci = run_ci("H1_ci.txt")
        check(STEP_HEADING in text_ci,
              "H1: host echo never reached step 1k: %r"
              % [ln for ln in text_ci.split("\n") if ln.startswith("=== ")][:8])
        check("=== Configure" not in text_ci,
              "H1: host walked past the red into configure/build -- the step does not stop it")
        check(rc_ci == 13, "H1: ci.ps1 rc=%s, expected 13" % rc_ci)
        check("script exit 1" in text_ci,
              "H1: the failure line does not echo the script's own code 1: %r"
              % [ln for ln in text_ci.split("\n") if "cited-log presence check failed" in ln])
        shown = [ln.strip() for ln in text_ci.split("\n") if INJECT_NAME in ln]
        check(len(shown) >= 1,
              "H1: the ADDED line was not echoed to the console (a red must name its cite): %r"
              % shown)
        reading(since, "H1 pre added=%d -> post added=%d | ci rc=%d step_reached=%s "
                       "configure_reached=%s echoed_inject=%d"
                   % (pre_counts[2] if pre_counts else -1,
                      post_counts[2] if post_counts else -1, rc_ci,
                      yn(STEP_HEADING in text_ci), yn("=== Configure" in text_ci), len(shown)))
    finally:
        since_restore = arm_open()
        with io.open(ci_path, "wb") as handle:
            handle.write(original_ci)
        check(md5(read_bytes(ci_path)) == md5(original_ci), "H1: ci.ps1 revert is not byte-for-byte")
        # The restore claim is about THIS ledger, so the file has to still exist for the run: rewrite
        # it without the injected line and judge it again. Deleting it here would only prove that a
        # missing input reads as code 2, which is M1's leg, not this one.
        write_ledger(False)
        rc_after, text_after = run_gate(cwd=WT, args_extra=["--ledger", ledger_posix],
                                        log_name="U47_H1_after.txt")
        after_counts = added_counts(text_after)
        check(rc_after == 1 and after_counts == pre_counts,
              "H1: after restore the copy reads %r (rc=%s), expected %r (rc=1)"
              % (after_counts, rc_after, pre_counts))
        check(not any(INJECT_NAME in ln for ln in citeadd(text_after)),
              "H1: the injected citation is still in the roster after restore")
        removed_ok = purge_tree(scratch)
        check(removed_ok and not os.path.isdir(scratch),
              "H1: leftover %s in the copy" % COPY_SCRATCH_REL)
        reading(since_restore, "H1 restore rc=%d added_back_to_pre=%s scratch_removed=%s"
                % (rc_after, yn(after_counts == pre_counts), yn(removed_ok)))


def arm_m2_host_maps_code2():
    """Step 1k pointed at an unreadable ledger, inside the copy only: the host's test is -ne 0."""
    since = arm_open()
    ci_path = os.path.join(WT, CI_PS1)
    original = read_bytes(ci_path)
    replacement = (b"$citeArgs = $hygienePre + @('tools/log_citation_gate.py', "
                   b"'--ledger', '%s')" % MISSING_LEDGER.encode("ascii"))
    if not check(original.count(CITE_ARGS_NEEDLE) == 1,
                 "M2: cannot locate step 1k's argument line in the copy (%d hit(s))"
                 % original.count(CITE_ARGS_NEEDLE)):
        return
    with io.open(ci_path, "wb") as handle:
        handle.write(original.replace(CITE_ARGS_NEEDLE, replacement, 1))
    try:
        rc_ci, text_ci = run_ci("M2_ci.txt")
        check(rc_ci == 13, "M2: ci.ps1 rc=%s with the script returning 2, expected 13" % rc_ci)
        labelled = check("script exit 2" in text_ci,
                         "M2: the echo does not carry the script's own code 2 -- 2 would read as a "
                         "finding: %r"
                         % [ln for ln in text_ci.split("\n") if "cited-log presence check failed" in ln])
        stopped = check("=== Configure" not in text_ci, "M2: host walked past an input error into configure")
        reading(since, "M2 ci rc=%d echo_labels_code2=%s configure_reached=%s"
                % (rc_ci, yn(labelled), yn(not stopped)))
    finally:
        with io.open(ci_path, "wb") as handle:
            handle.write(original)
        check(md5(read_bytes(ci_path)) == md5(original), "M2: revert is not byte-for-byte")


def main():
    keep = "--keep" in sys.argv[1:]
    if not os.path.isdir(LOGDIR):
        os.makedirs(LOGDIR, exist_ok=True)

    watch = [CI_PS1, GATE, CI_CHECK, LEDGER_REL]
    main_md5 = dict((p, md5(read_bytes(os.path.join(ROOT, p)))) for p in watch)
    main_pre = porcelain(ROOT)

    arm_c1_occupancy()
    g1_counts = arm_g1_baseline()
    arm_g2_g3(g1_counts)
    arm_g4_removed_report_only()
    arm_m1_script_code2()

    dirty = make_copy()
    try:
        arm_s1_declared_skip()
        arm_h1_host_red()
        arm_m2_host_maps_code2()

        since_z0 = arm_open()
        post = porcelain(WT)
        check(post == dirty, "Z0: copy footprint not back to its sync set: %r != %r" % (post, dirty))
        left = [n for n in os.listdir(os.path.join(WT, "docs")) if n.startswith("U47_")] \
            if os.path.isdir(os.path.join(WT, "docs")) else []
        check(not left, "Z0: injected files left in the copy's docs/: %r" % left)
        check(not copy_has_scratch(), "Z0: a build/*_probe tree was left inside the copy")
        reading(since_z0, "Z0 copy footprint unchanged=%s (lines=%d) injected_left=%d scratch_left=%s"
                % (yn(post == dirty), len(post), len(left), yn(copy_has_scratch())))
    finally:
        if keep:
            print("[U47-INFO] --keep: copy left at %s, logs in %s" % (WT, LOGDIR))
        else:
            remove_copy()

    since_main = arm_open()
    md5_ok = 0
    for p, digest in main_md5.items():
        if check(md5(read_bytes(os.path.join(ROOT, p))) == digest,
                 "Z0: main worktree %s changed" % p):
            md5_ok += 1
    check(porcelain(ROOT) == main_pre, "Z0: main worktree porcelain moved during the run")
    reading(since_main, "Z0 main worktree md5_matched=%d/%d porcelain_unchanged=%s"
            % (md5_ok, len(main_md5), yn(porcelain(ROOT) == main_pre)))

    print("[U47-SUMMARY] legs=10 (C1 G1 G2 G3 G4 M1 S1 H1 M2 Z0; G3 runs inside the G2 arm, Z0 has "
          "a copy half and a main-worktree half) | failures=%d" % len(FAILURES))
    if FAILURES:
        print("U-47 probe: %d problem(s)" % len(FAILURES))
        return 1
    print("U-47 probe: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())

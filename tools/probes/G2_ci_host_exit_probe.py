"""G-2 replayable probe for ci.ps1 step 1f (dangling-timer site inventory -> host exit 8).

What it proves, and what was unmeasured before (advance plan 3.26, "registered unfixed" item 2):
the static inventory tool existed and T16 caught the two panel sites at runtime, but the tool was
not wired into the host, so a newly added context-less QTimer::singleShot in a node no test builds
would not turn any default gate red until someone ran the tool by hand.

  A0   copy baseline      python tools/single_shot_inventory.py            -> rc=0, no_context=0
  A1   inventory red      drop the context argument of the singleShot at
         -> host exit 8    src/ColorConversionNode.cpp:300 inside a one-shot worktree copy
                            -> tools/ci.ps1 rc=8, echo contains "=== Dangling-timer site inventory ===",
                               "NOCTX ColorConversionNode.cpp:300 argc=2", "FAIL: 1 singleShot site(s)",
                               "[FAIL] dangling-timer site found (exit 1)", 1b-1e all [OK], and stops
                               before "=== Configure ==="
  A2   restore            git checkout -- that file -> inventory rc=0 again + copy working tree clean
  A3   second file        the same drop at src/HalconNode.cpp:645 (the base-class site) -> host rc=8,
                               with the NOCTX needle naming HalconNode.cpp instead -> the gate is not
                               pinned to one file
  A4   restore            byte-for-byte + inventory rc=0
  A5   blind spot (U-23)  drop the *receiver context* of connect(this, &ImageReadNode::thumbnailUpdated,
                               thumbnailLayout, ...) at src/ImageReadNode.cpp:513 -> inventory stays
                               rc=0 (it greps QTimer::singleShot only). Reported as a measured blind
                               spot, not as coverage: this gate does not see stale connect lambdas.
  A6   scope reading      the tool's scan root is src/; the same mechanical rule applied to tracked
                               .cpp/.h files outside src/ counts the context-less sites living there
                               (T16's mechanism leg + its comment), so widening the root would make
                               the gate red by design.
  A7   exit-code map      every "exit N" literal in tools/ci.ps1, to show 8 is claimed by one step
                               plus the main-worktree md5 / porcelain snapshot
  ST   needle self-test   the red verifier rejects a green sample, an exit-4-shaped sample and a bare
                               1f fragment (missing the upstream steps), and accepts its own sample
  ST2  real-log cross     A1's log must fail the A3 expectation and vice versa (the file:line needle
                               discriminates which site the host blamed)

Every defect is injected in a one-shot `git worktree add --detach` copy under build/, which the probe
removes again. The main worktree's tracked files are never written to, and tools/ci.ps1 is never
modified -- it is the script that produces the exit code.

Note on copy bytes (advance plan 3.18): core.autocrlf=true means a fresh worktree checkout is NOT
md5-identical to the main worktree for files the index stores as LF. All comparisons below use the
copy's own baseline, never main's bytes.

Note on copy contents: step 1f is uncommitted while this probe runs, so the copy is HEAD **plus**
this round's tools/ci.ps1 re-synced from the main worktree (md5-checked against main, listed in
SYNC_FROM_MAIN). That synced file is the only dirt the copy is allowed to carry, and the A2
assertion compares against exactly that footprint instead of against "clean".

Run:  python tools/probes/G2_ci_host_exit_probe.py        (arms A0..A7 + ST + ST2; two host runs)
      python tools/probes/G2_ci_host_exit_probe.py --keep (leave the copy + logs for reading)
"""
import hashlib
import importlib.util
import io
import os
import re
import subprocess
import sys

# A6 exec_modules the inventory tool; without this it drops a __pycache__ directory into tools/,
# which then shows up as untracked dirt in the A7 main-worktree snapshot.
sys.dont_write_bytecode = True

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WT = os.path.join(ROOT, "build", "g2_probe", "wt")
LOGDIR = os.path.join(ROOT, "build", "g2_probe")

CI_PS1 = os.path.join("tools", "ci.ps1")
INVENTORY = os.path.join("tools", "single_shot_inventory.py")
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

# The step under test is uncommitted while this probe runs, and `git worktree add ... HEAD` would
# check out HEAD's pre-1f ci.ps1. Those files are therefore re-synced from the main worktree into
# the copy (md5 pinned below), which also means the copy is expected to report exactly these paths
# as modified for its whole life.
SYNC_FROM_MAIN = [CI_PS1]

SITE_NEEDLE = b"QTimer::singleShot(100, inputImageCombo, [=]() {"
# label -> (repo-relative file, expected 1-based line, text dropped from that line)
INJECTIONS = {
    "A1": ("src/ColorConversionNode.cpp", 300, b", inputImageCombo"),
    "A3": ("src/HalconNode.cpp", 645, b", inputImageCombo"),
}
# A5: the U-23 shape -- the third argument of connect() is its receiver context object.
A5_FILE = "src/ImageReadNode.cpp"
A5_LINE = 513
A5_DROP = b"thumbnailLayout, "          # occurs exactly once on that line (verified by the arm)

UPSTREAM_OK = ("[OK]   repo hygiene OK (",
               "[OK]   QtTest gate self-test OK (",
               "[OK]   documentation check OK",
               "[OK]   documentation anchors OK (")

FAILURES = []


def md5(data):
    return hashlib.md5(data).hexdigest()


def read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read()


def note(text):
    print(text)


def fail(text):
    FAILURES.append(text)
    print("[G2-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def sh(args, cwd=None, timeout=900):
    return subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)


def decode(raw):
    # ci.ps1 writes through the OEM console code page; every needle asserted below is ASCII,
    # so a lossy decode cannot turn a missing needle into a present one.
    return raw.decode("utf-8", errors="replace") + "\n"


def rel_path(rel, base=None):
    return os.path.join(base or WT, rel.replace("/", os.sep))


def run_ci(log_name):
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1], cwd=WT)
    text = decode(proc.stdout) + decode(proc.stderr)
    with open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return proc.returncode, text


def run_inventory(cwd=None):
    proc = sh([sys.executable, INVENTORY], cwd=cwd or WT)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def find_unique_line(raw, needle):
    """1-based line number of the only line containing `needle`; None if zero or several."""
    hits = [i + 1 for i, line in enumerate(raw.splitlines()) if needle in line]
    return hits[0] if len(hits) == 1 else None


def drop_on_line(raw, line_no, drop):
    """Remove `drop` from line `line_no` only. Returns None if that line lacks it exactly once."""
    parts = raw.splitlines(keepends=True)
    idx = line_no - 1
    if idx < 0 or idx >= len(parts):
        return None
    if parts[idx].count(drop) != 1:
        return None
    parts[idx] = parts[idx].replace(drop, b"", 1)
    return b"".join(parts)


def inject(label):
    rel, expected_line, drop = INJECTIONS[label]
    target = rel_path(rel)
    original = read_bytes(target)
    measured = find_unique_line(original, SITE_NEEDLE)
    check(measured == expected_line,
          "%s: %s context-less-timer candidate is at line %s, probe expects %d"
          % (label, rel, measured, expected_line))
    line_no = measured or expected_line
    new = drop_on_line(original, line_no, drop)
    if new is None:
        raise SystemExit("cannot inject %s: %s:%d does not contain %r exactly once"
                         % (label, rel, line_no, drop.decode()))
    with open(target, "wb") as handle:
        handle.write(new)
    return rel, line_no, original, new


def restore(rel):
    sh(["git", "checkout", "--", rel], cwd=WT)


# The inventory prints its verdict label with "%-6s" plus one space, so a red line reads
# "NOCTX  File.cpp:123 ..." -- two spaces, which the needles below must reproduce exactly.
NOCTX_NEEDLE = "NOCTX  %s:%d argc=2"


def verify_red(rc, text, rel, line_no):
    problems = []
    if rc != 8:
        problems.append("expected host rc=8, got %s" % rc)
    for needle in ("=== Dangling-timer site inventory ===",
                   NOCTX_NEEDLE % (rel.split("/")[-1], line_no),
                   "FAIL: 1 singleShot site(s) still lack a context object",
                   "[FAIL] dangling-timer site found (exit 1)",
                   "=== Repository hygiene ===") + UPSTREAM_OK:
        if needle not in text:
            problems.append("missing echo needle: %r" % needle)
    for absent in ("=== Configure", "=== Build", "=== Tests (CTest",
                   "every singleShot call site is bound",
                   "[OK]   dangling-timer site inventory OK"):
        if absent in text:
            problems.append("run should have stopped at 1f, but echo contains %r" % absent)
    return problems


def verify_inventory_green(rc, text):
    problems = []
    if rc != 0:
        problems.append("expected inventory rc=0, got %s" % rc)
    if "OK: every singleShot call site is bound to a context object" not in text:
        problems.append("missing inventory OK line")
    if "no_context=0" not in text:
        problems.append("inventory did not report no_context=0")
    if "NOCTX" in text:
        problems.append("inventory listed a NOCTX site while claiming green")
    return problems


def make_copy():
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        head = decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()
        note("[G2-INFO] copy created at %s (detached HEAD %s)" % (WT, head))
    else:
        note("[G2-INFO] reusing existing copy at %s" % WT)
    for rel in SYNC_FROM_MAIN:
        main_bytes = read_bytes(os.path.join(ROOT, rel))
        with open(rel_path(rel), "wb") as handle:
            handle.write(main_bytes)
        check(md5(read_bytes(rel_path(rel))) == md5(main_bytes),
              "copy setup: %s in the copy does not match the main worktree bytes" % rel)
        note("[G2-INFO] copy synced %s from the main worktree (md5 %s) -- HEAD's version predates step 1f"
             % (rel, md5(main_bytes)[:12]))
    return copy_dirty_expectation()


def copy_dirty_expectation():
    """Porcelain entries the copy carries for its whole life, purely because of the sync above."""
    return tuple(sorted("M %s" % rel.replace(os.sep, "/") for rel in SYNC_FROM_MAIN))


def porcelain_entries(cwd, paths):
    """Same normalization as copy_dirty_expectation, so the two are directly comparable."""
    return tuple(sorted(l.strip() for l in git_clean(cwd, paths).splitlines() if l.strip()))


def remove_copy():
    proc = sh(["git", "worktree", "remove", "--force", WT], cwd=ROOT)
    if proc.returncode != 0:
        note("[G2-WARN] git worktree remove said: " + decode(proc.stdout + proc.stderr).strip())
    sh(["git", "worktree", "prune"], cwd=ROOT)


def git_clean(cwd, paths):
    out = sh(["git", "status", "--porcelain", "--"] + paths, cwd=cwd)
    return decode(out.stdout).strip()


def inventory_sites_for(path):
    """Apply the tool's own mechanical rule to an arbitrary file (used by the A6 scope reading)."""
    spec = importlib.util.spec_from_file_location("g2_ss_tool", os.path.join(ROOT, INVENTORY))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    lines = io.open(path, encoding="utf-8", errors="replace").read().split("\n")
    hits = []
    for i, line in enumerate(lines):
        for m in mod.CALL_RE.finditer(line):
            args = mod.top_level_args(line[m.end() - 1:])
            second = args[1] if len(args) > 1 else ""
            if second.startswith("["):
                kind = "comment" if line.strip().startswith(("//", "*", "/*")) else "code"
                # keep the probe's own stdout ASCII-only: the console code page is not guaranteed UTF-8
                hits.append((i + 1, kind, second[:20].encode("ascii", "replace").decode("ascii")))
    return hits


def main():
    argv = sys.argv[1:]
    keep = "--keep" in argv
    os.makedirs(LOGDIR, exist_ok=True)

    # ---- ST: the verifiers must be discriminative before any copy exists
    red_sample = ("\n=== Dangling-timer site inventory ===\n"
                  "  SINGLESHOT total=18 with_context=17 no_context=1\n"
                  "    NOCTX  ColorConversionNode.cpp:300 argc=2 second='[=]() {'\n"
                  "  FAIL: 1 singleShot site(s) still lack a context object\n"
                  "  [FAIL] dangling-timer site found (exit 1) via 'python'; see log\n")
    green_sample = ("\n=== Dangling-timer site inventory ===\n"
                    "  SINGLESHOT total=18 with_context=18 no_context=0\n"
                    "  OK: every singleShot call site is bound to a context object\n"
                    "  [OK]   dangling-timer site inventory OK (python)\n")
    hygiene_red_sample = ("\n=== Repository hygiene ===\n"
                          "  [FAIL] ps1-encoding: tools/soak.ps1 carries a UTF-8 BOM\n"
                          "  [FAIL] repo hygiene failed (exit 1) via 'python'; see log\n")
    full_red = ("=== Repository hygiene ===\n  checked 536 tracked files\n  [OK]   repo hygiene OK (python)\n"
                "  [OK]   QtTest gate self-test OK (python)\n  [OK]   documentation check OK\n"
                "  [OK]   documentation anchors OK (python)\n" + red_sample)
    check(len(verify_red(8, red_sample, "src/ColorConversionNode.cpp", 300)) >= 5,
          "ST: verify_red accepted a bare 1f fragment that never shows the upstream steps")
    check(verify_red(8, full_red, "src/ColorConversionNode.cpp", 300) == [],
          "ST: verify_red rejects a full-shaped red log (problems=%d)"
          % len(verify_red(8, full_red, "src/ColorConversionNode.cpp", 300)))
    check(len(verify_red(4, hygiene_red_sample, "src/ColorConversionNode.cpp", 300)) >= 5,
          "ST: verify_red accepted an exit-4 hygiene-red log")
    check(len(verify_red(8, green_sample, "src/ColorConversionNode.cpp", 300)) >= 3,
          "ST: verify_red accepted a green 1f log")
    check(verify_inventory_green(0, green_sample) == [], "ST: green verifier rejects its own good sample")
    check(len(verify_inventory_green(1, red_sample)) >= 2, "ST: green verifier accepted a red inventory")
    note("[G2-INFO] ST discriminativity (problems reported): bare-fragment=%d full-red=%d exit-4=%d green=%d"
         % (len(verify_red(8, red_sample, "src/ColorConversionNode.cpp", 300)),
            len(verify_red(8, full_red, "src/ColorConversionNode.cpp", 300)),
            len(verify_red(4, hygiene_red_sample, "src/ColorConversionNode.cpp", 300)),
            len(verify_red(8, green_sample, "src/ColorConversionNode.cpp", 300))))

    # ---- A7 (front half): snapshot the main worktree before anything is touched
    main_pre = git_clean(ROOT, ["src", "include", "tools", "docs"])
    main_watch = ("tools/ci.ps1", "tools/single_shot_inventory.py",
                  "tools/probes/G1_ci_host_exit_probe.py",
                  "src/ColorConversionNode.cpp", "src/HalconNode.cpp", "src/ImageReadNode.cpp")
    main_baseline = {p: md5(read_bytes(os.path.join(ROOT, p))) for p in main_watch}

    expect_dirty = make_copy()

    # ---- A0: copy baseline, green
    ci_md5 = md5(read_bytes(rel_path(CI_PS1)))
    rc0, text0 = run_inventory()
    for problem in verify_inventory_green(rc0, text0):
        fail("A0: " + problem)
    total0 = re.search(r"SINGLESHOT total=\d+ with_context=\d+ no_context=\d+", text0)
    note("[G2-INFO] A0 copy baseline inventory rc=%s %s | executed tools/ci.ps1 md5=%s"
         % (rc0, total0.group(0) if total0 else "(no SINGLESHOT line)", ci_md5))
    copy_baseline = {rel: md5(read_bytes(rel_path(rel)))
                     for rel in ("src/ColorConversionNode.cpp", "src/HalconNode.cpp", A5_FILE)}

    # ---- A1 + A3: context-less singleShot in two different files -> host exit 8
    host_logs = {}
    for label in ("A1", "A3"):
        rel, line_no, original, injected = inject(label)
        check(md5(read_bytes(rel_path(rel))) == md5(injected),
              "%s: injected bytes do not match the expected drop" % label)
        check(md5(read_bytes(rel_path(rel))) != copy_baseline[rel],
              "%s: injection left %s unchanged" % (label, rel))
        check(md5(read_bytes(rel_path(CI_PS1))) == ci_md5,
              "%s: the executed tools/ci.ps1 changed during the run" % label)

        rci, texti = run_inventory()
        check(rci == 1, "%s: inventory in the copy returned %s, expected 1" % (label, rci))
        check(NOCTX_NEEDLE % (rel.split("/")[-1], line_no) in texti,
              "%s: inventory did not name %s:%d as NOCTX" % (label, rel, line_no))

        rc, text = run_ci("probe_%s_inventory_red.txt" % label)
        for problem in verify_red(rc, text, rel, line_no):
            fail("%s: %s" % (label, problem))
        host_logs[label] = (rc, text, rel, line_no)
        note("[G2-INFO] %s %s:%d dropped %r -> inventory rc=%s -> host rc=%s "
             "(1f red, stopped before === Configure ===)"
             % (label, rel, line_no, INJECTIONS[label][2].decode(), rci, rc))

        restore(rel)
        check(md5(read_bytes(rel_path(rel))) == copy_baseline[rel],
              "%s: %s not restored byte-for-byte" % (label, rel))
        rcg, textg = run_inventory()
        check(rcg == 0, "%s: inventory after restore rc=%s" % (label, rcg))

    # ---- A2: the copy is clean again after both injections
    copy_dirt = porcelain_entries(WT, ["src", "tools", "docs"])
    check(copy_dirt == expect_dirty,
          "A2: copy dirty beyond the synced ci.ps1: expected=%r got=%r" % (expect_dirty, copy_dirt))
    rc2, text2 = run_inventory()
    for problem in verify_inventory_green(rc2, text2):
        fail("A2: " + problem)

    # ---- ST2: cross-rejection on the two real host logs
    a1_as_a3 = verify_red(host_logs["A1"][0], host_logs["A1"][1], "src/HalconNode.cpp", 645)
    a3_as_a1 = verify_red(host_logs["A3"][0], host_logs["A3"][1], "src/ColorConversionNode.cpp", 300)
    check(len(a1_as_a3) >= 1, "ST2: verify_red accepted A1's real log when asked about HalconNode.cpp:645")
    check(len(a3_as_a1) >= 1, "ST2: verify_red accepted A3's real log when asked about ColorConversionNode.cpp:300")
    check(verify_red(host_logs["A1"][0], host_logs["A1"][1], "src/ColorConversionNode.cpp", 300) == [],
          "ST2: verify_red rejects A1's own real log")
    note("[G2-INFO] ST2 cross-rejection on real logs: A1-as-A3 problems=%d, A3-as-A1 problems=%d"
         % (len(a1_as_a3), len(a3_as_a1)))

    # ---- A5: measured blind spot -- the U-23 connect() shape stays green through this gate
    a5_target = rel_path(A5_FILE)
    a5_original = read_bytes(a5_target)
    a5_measured = [i + 1 for i, l in enumerate(a5_original.splitlines())
                   if b"connect(this, &ImageReadNode::thumbnailUpdated, thumbnailLayout," in l]
    check(A5_LINE in a5_measured,
          "A5: expected the receiver-context connect at %s:%d, found %s"
          % (A5_FILE, A5_LINE, a5_measured))
    a5_new = drop_on_line(a5_original, A5_LINE, A5_DROP)
    if a5_new is None:
        fail("A5: %s:%d does not contain %r exactly once, blind-spot arm has no object"
             % (A5_FILE, A5_LINE, A5_DROP.decode()))
        rc5 = -1
    else:
        with open(a5_target, "wb") as handle:
            handle.write(a5_new)
        rc5, text5 = run_inventory()
        check(rc5 == 0, "A5: inventory rc=%s after dropping the connect receiver context" % rc5)
        check("no_context=0" in text5, "A5: inventory no longer reports no_context=0")
        check(NOCTX_NEEDLE % ("ImageReadNode.cpp", A5_LINE) not in text5,
              "A5: inventory blamed an ImageReadNode site")
        check("NOCTX" not in text5, "A5: inventory reported a NOCTX site for the connect-shape injection")
        total5 = re.search(r"SINGLESHOT total=\d+ with_context=\d+ no_context=\d+", text5)
        note("[G2-INFO] A5 blind spot measured: receiver context removed at %s:%d "
             "(md5 %s -> %s, %d byte(s) shorter), inventory still rc=%s %s -- this gate greps "
             "QTimer::singleShot only, stale connect lambdas are covered by T16, not here"
             % (A5_FILE, A5_LINE, md5(a5_original)[:8], md5(a5_new)[:8],
                len(a5_original) - len(a5_new), rc5,
                total5.group(0) if total5 else "?"))
        def line_of(blob):
            return blob.splitlines()[A5_LINE - 1].decode("ascii", "replace").strip()
        restore(A5_FILE)
        note("[G2-INFO] A5 line %d before: %s" % (A5_LINE, line_of(a5_original)))
        note("[G2-INFO] A5 line %d after : %s" % (A5_LINE, line_of(a5_new)))
        check(md5(read_bytes(a5_target)) == copy_baseline[A5_FILE],
              "A5: %s not restored byte-for-byte" % A5_FILE)
        rc5b, _ = run_inventory()
        check(rc5b == 0, "A5: inventory still red after restore (rc=%s)" % rc5b)

    # ---- A6: the gate's scan root, and what the same rule sees outside it
    tool_src = read_bytes(os.path.join(ROOT, INVENTORY)).decode("utf-8", "replace")
    root_line = [l for l in tool_src.split("\n") if l.startswith("SRC = ")]
    outside = []
    out = sh(["git", "ls-files", "*.cpp", "*.h", "*.hpp"], cwd=ROOT)
    for rel in sorted(decode(out.stdout).split()):
        if rel.startswith("src/") or rel.startswith("build/"):
            continue
        for ln, kind, second in inventory_sites_for(os.path.join(ROOT, rel.replace("/", os.sep))):
            outside.append("%s:%d %s %s" % (rel, ln, kind, second))
    note("[G2-INFO] A6 tool scan root: %r | context-less sites judged by the same rule outside src/: %d"
         % (root_line[0] if root_line else "?", len(outside)))
    for item in outside:
        note("[G2-INFO] A6 outside-root site: %s" % item)
    check(len(outside) > 0, "A6: expected T16's mechanism legs outside src/ so the reading is not vacuous")

    # ---- A7: exit-code map in the delivered ci.ps1, and the main worktree untouched
    ci_text = read_bytes(os.path.join(ROOT, CI_PS1)).decode("utf-8", "replace")
    codes = sorted(set(int(m) for m in re.findall(r"^\s*exit (\d+)\s*$", ci_text, re.M)))
    eight = len(re.findall(r"^\s*exit 8\s*$", ci_text, re.M))
    note("[G2-INFO] A7 ci.ps1 exit codes present: %s | occurrences of 'exit 8': %d" % (codes, eight))
    check(eight == 1, "A7: 'exit 8' appears %d times in ci.ps1, expected exactly 1" % eight)

    for p, digest in main_baseline.items():
        check(md5(read_bytes(os.path.join(ROOT, p))) == digest,
              "A7: main worktree %s changed (was %s)" % (p, digest))
    main_post = git_clean(ROOT, ["src", "include", "tools", "docs"])
    check(main_post == main_pre,
          "A7: main worktree changed during the run: before=%r after=%r" % (main_pre, main_post))

    if keep:
        note("[G2-INFO] --keep: copy left at %s, logs in %s" % (WT, LOGDIR))
    else:
        remove_copy()
        note("[G2-INFO] copy removed; %r"
             % decode(sh(["git", "worktree", "list"], cwd=ROOT).stdout).strip())

    print("[G2-SUMMARY] A1 host rc=%s | A3 host rc=%s | A5 inventory rc=%s (blind spot, green by design) | "
          "outside-root sites=%d | failures=%d"
          % (host_logs["A1"][0], host_logs["A3"][0], rc5, len(outside), len(FAILURES)))
    if FAILURES:
        print("G-2 probe: %d problem(s)" % len(FAILURES))
        return 1
    print("G-2 probe: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

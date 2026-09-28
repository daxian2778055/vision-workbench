"""G-1 replayable probe for the ci.ps1 host exit codes (repository-hygiene side).

What it proves, and what was unmeasured before:

  A0  copy baseline         python tools/repo_hygiene.py            -> rc=0, "checked N tracked files"
  A1  hygiene red -> host   prepend BOM to the copy's tools/soak.ps1
                            -> powershell tools/ci.ps1 rc=4, echo names step 1b +
                               "ps1-encoding: tools/soak.ps1 ... pure ASCII", and stops
                               before "=== Configure ==="
  A2  restore               git checkout -- tools/soak.ps1 in the copy
                            -> hygiene rc=0 again + copy working tree clean
  A3  no interpreter -> host PATH keeps cmake but hides python/py    -> rc=3, preflight [OK] cmake
                               + "no usable Python interpreter", i.e. the exit 3 at the 1b branch
  A3b control               same PATH minus cmake                   -> rc=3, "cmake not found on PATH",
                               1b never reached -- proves A3's rc=3 is not the preflight branch
  A4  two-sidedness         strip the BOM from a .ps1 that has non-ASCII content
                            -> hygiene rc=1 with "non-ASCII ... no UTF-8 BOM"
                               (check 6 is not "any BOM is red"; the opposite combination is red)
  A5  main worktree         md5 of tools/ci.ps1 / tools/soak.ps1 / tools/repo_hygiene.py unchanged
     untouched             + `git status --porcelain -- src include tools docs` identical to the
                               snapshot taken before any injection (the probe's own file is
                               untracked on its first run, so "empty" would be the wrong ask)
  ST  needle self-test      A1's verifier run against A3's log must report problems
                            (the assertions are discriminative, not always-true)
  ST2 real-log rejection    the three captured logs are fed to each other's verifiers: A1's verifier
                            rejects both exit-3 logs, and both exit-3 verifiers reject the exit-4
                            log -- the rc mapping is three distinct shapes, not a tautology

Every defect is injected in a one-shot `git worktree add --detach` copy under build/, which the probe
removes again. The main worktree's tracked files are never written to, and tools/ci.ps1 itself is
never modified -- the injected file is a different .ps1, so the script under test stays the thing
that produces the exit code.

Note on copy bytes: because core.autocrlf=true, files the index stores as LF come out of a fresh
checkout as CRLF, so a worktree copy's tools/ci.ps1 is NOT md5-identical to the main worktree's.
The earlier ledger claim of byte-identity across both copy shapes is corrected in
docs/对标差距推进计划.md §3.18. The probe therefore compares the copy against the copy's own
baseline, never against main.

Run:  python tools/probes/G1_ci_host_exit_probe.py            (arms A0..A5 + ST + ST2)
      python tools/probes/G1_ci_host_exit_probe.py --keep     (leave the copy + logs for reading)
"""
import codecs
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WT = os.path.join(ROOT, "build", "g1_probe", "wt")
LOGDIR = os.path.join(ROOT, "build", "g1_probe")
CI_PS1 = os.path.join("tools", "ci.ps1")
HYGIENE = os.path.join("tools", "repo_hygiene.py")
ASCII_TARGET = "tools/soak.ps1"           # pure-ASCII, BOM-less, not the executed script
CMAKE_BIN = r"D:\Program Files\Microsoft Visual Studio\2022\Professional\Common7" \
            r"\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

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
    print("[G1-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def sh(args, cwd=None, env=None, timeout=600):
    return subprocess.run(args, cwd=cwd, env=env, capture_output=True, timeout=timeout)


def decode(raw):
    # ci.ps1 writes through the OEM console code page; every needle asserted below is ASCII,
    # so a lossy decode cannot turn a missing needle into a present one.
    return raw.decode("utf-8", errors="replace") + "\n"


def run_ci(cwd, path_env=None, log_name=None):
    env = dict(os.environ)
    if path_env is not None:
        env["PATH"] = path_env
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1],
              cwd=cwd, env=env, timeout=900)
    text = decode(proc.stdout) + decode(proc.stderr)
    if log_name:
        with open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
    return proc.returncode, text


def script_path_env(with_cmake):
    sys32 = r"C:\Windows\System32"
    parts = [sys32, os.path.join(sys32, "Wbem"), os.path.join(sys32, "WindowsPowerShell", "v1.0")]
    if with_cmake:
        parts.append(CMAKE_BIN)
    return ";".join(parts)


def run_hygiene(cwd):
    proc = sh([sys.executable, HYGIENE], cwd=cwd)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def pick_nonascii_ps1(cwd):
    """First tracked .ps1 whose content is non-ASCII (sorted by path -- deterministic)."""
    out = sh(["git", "ls-files", "tools/*.ps1"], cwd=cwd)
    for rel in sorted(decode(out.stdout).split()):
        raw = read_bytes(os.path.join(cwd, rel.replace("/", os.sep)))
        body = raw[len(codecs.BOM_UTF8):] if raw.startswith(codecs.BOM_UTF8) else raw
        if any(b > 127 for b in body):
            return rel, raw
    return None, None


def verify_a1(rc, text):
    problems = []
    if rc != 4:
        problems.append("A1 expected host rc=4, got %s" % rc)
    for needle in ("=== Repository hygiene ===",
                   "[FAIL] ps1-encoding: tools/soak.ps1 carries a UTF-8 BOM while its content is pure ASCII",
                   "[FAIL] repo hygiene failed (exit 1) via",
                   "=== Preflight ===",
                   "[OK]   cmake:"):
        if needle not in text:
            problems.append("A1 missing echo needle: %r" % needle)
    for absent in ("=== Configure ===", "=== Build ===", "=== Tests ==="):
        if absent in text:
            problems.append("A1 should have stopped before 1b, but echo contains %r" % absent)
    return problems


def verify_a3(rc, text, expect_preflight):
    problems = []
    if rc != 3:
        problems.append("A3 expected host rc=3, got %s" % rc)
    if expect_preflight:
        if "[FAIL] cmake not found on PATH" not in text:
            problems.append("A3b missing '[FAIL] cmake not found on PATH'")
        if "=== Repository hygiene ===" in text:
            problems.append("A3b should die in preflight, but echo reached 1b")
    else:
        for needle in ("=== Repository hygiene ===",
                       "[FAIL] no usable Python interpreter (tried: python, py -3)",
                       "[OK]   cmake:"):
            if needle not in text:
                problems.append("A3 missing echo needle: %r" % needle)
        for absent in ("=== Configure ===", "=== Build ==="):
            if absent in text:
                problems.append("A3 should have stopped before 1b, but echo contains %r" % absent)
    return problems


def make_copy(reuse):
    if os.path.isdir(WT):
        if not reuse:
            remove_copy()
        else:
            return
    os.makedirs(os.path.dirname(WT), exist_ok=True)
    proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
    head = decode(sh(["git", "rev-parse", "HEAD"], cwd=ROOT).stdout).strip()
    note("[G1-INFO] copy created at %s (detached HEAD %s)" % (WT, head))


def remove_copy():
    proc = sh(["git", "worktree", "remove", "--force", WT], cwd=ROOT)
    if proc.returncode != 0:
        note("[G1-WARN] git worktree remove said: " + decode(proc.stdout + proc.stderr).strip())
    sh(["git", "worktree", "prune"], cwd=ROOT)


def git_clean(cwd, paths):
    out = sh(["git", "status", "--porcelain", "--"] + paths, cwd=cwd)
    return decode(out.stdout).strip()


def main():
    argv = sys.argv[1:]
    keep = "--keep" in argv
    reuse = "--reuse" in argv
    os.makedirs(LOGDIR, exist_ok=True)

    # ---- needle discriminativity (ST), before touching any copy
    a1_sample = ("=== Preflight ===\n  [OK]   cmake: X\n\n=== Repository hygiene ===\n"
                 "  [FAIL] ps1-encoding: tools/soak.ps1 carries a UTF-8 BOM while its content is pure ASCII\n"
                 "  [FAIL] repo hygiene failed (exit 1) via 'python'; see log\n")
    a3_sample = ("=== Preflight ===\n  [OK]   cmake: X\n\n=== Repository hygiene ===\n"
                 "  [FAIL] no usable Python interpreter (tried: python, py -3); repo_hygiene.py cannot run\n")
    check(len(verify_a1(3, a3_sample)) >= 2, "ST: verify_a1 accepted a log that is not hygiene-red")
    check(verify_a1(4, a1_sample) == [], "ST: verify_a1 rejects its own good sample")
    check(len(verify_a3(3, a1_sample, True)) >= 1, "ST: verify_a3(1b) accepted a hygiene-red log")
    check(verify_a3(3, a3_sample, False) == [], "ST: verify_a3(1b) rejects its own good sample")
    check(len(verify_a3(4, a3_sample, True)) >= 1, "ST: verify_a3(preflight) accepted the 1b log")

    # ---- baseline: snapshot the main worktree, then create the copy from it
    main_pre = git_clean(ROOT, ["src", "include", "tools", "docs"])
    note("[G1-INFO] A5 baseline `git status --porcelain -- src include tools docs` = %r" % main_pre)
    main_baseline = {p: md5(read_bytes(os.path.join(ROOT, p)))
                     for p in ("tools/ci.ps1", "tools/soak.ps1", "tools/repo_hygiene.py")}

    make_copy(reuse)

    # ---- A0 copy baseline
    wt_ci_md5 = md5(read_bytes(os.path.join(WT, CI_PS1)))
    tracked_line = ""
    rc, text = run_hygiene(WT)
    for line in text.splitlines():
        if line.startswith("checked "):
            tracked_line = line
    check(rc == 0, "A0: hygiene in the fresh copy returned %s, not 0" % rc)
    check("repo hygiene: OK" in text, "A0: hygiene in the copy did not print OK")
    note("[G1-INFO] A0 copy hygiene rc=%s %s | executed tools/ci.ps1 md5=%s"
         % (rc, tracked_line, wt_ci_md5))
    wt_baseline = {p: md5(read_bytes(os.path.join(WT, p)))
                   for p in (CI_PS1, "tools/soak.ps1")}

    # ---- A1 pure-ASCII .ps1 gains a BOM -> host exit 4
    target = os.path.join(WT, ASCII_TARGET.replace("/", os.sep))
    raw = read_bytes(target)
    check(not raw.startswith(codecs.BOM_UTF8), "A1: %s already has a BOM, injection would be a no-op" % ASCII_TARGET)
    with open(target, "wb") as handle:
        handle.write(codecs.BOM_UTF8 + raw)
    rc1, text1 = run_ci(WT, log_name="probe_A1_hygiene_red.txt")
    for problem in verify_a1(rc1, text1):
        fail(problem)
    check(md5(read_bytes(os.path.join(WT, CI_PS1))) == wt_baseline[CI_PS1],
          "A1: the executed tools/ci.ps1 changed during the run")
    check(md5(read_bytes(os.path.join(WT, "tools/soak.ps1"))) == md5(codecs.BOM_UTF8 + raw),
          "A1: injected bytes do not match BOM + original")

    # ---- A2 restore through git, then hygiene green again in the same copy
    sh(["git", "checkout", "--", ASCII_TARGET], cwd=WT)
    check(md5(read_bytes(target)) == wt_baseline["tools/soak.ps1"],
          "A2: restored soak.ps1 md5 differs from the copy baseline")
    check(git_clean(WT, ["tools"]) == "", "A2: copy still dirty after restore: %r" % git_clean(WT, ["tools"]))
    rc2, text2 = run_hygiene(WT)
    check(rc2 == 0 and "repo hygiene: OK" in text2, "A2: hygiene after restore rc=%s" % rc2)

    # ---- A3 PATH with cmake but without python/py -> host exit 3 at the 1b branch
    rc3, text3 = run_ci(WT, path_env=script_path_env(True), log_name="probe_A3_no_interpreter.txt")
    for problem in verify_a3(rc3, text3, False):
        fail(problem)

    # ---- A3b control: same PATH minus cmake -> exit 3 at preflight
    rc3b, text3b = run_ci(WT, path_env=script_path_env(False), log_name="probe_A3b_no_cmake.txt")
    for problem in verify_a3(rc3b, text3b, True):
        fail(problem)

    # ---- ST2 cross-rejection on the real logs (not the synthetic ST samples)
    check(len(verify_a1(rc3, text3)) >= 2, "ST2: verify_a1 did not reject the real A3 log")
    check(len(verify_a1(rc3b, text3b)) >= 2, "ST2: verify_a1 did not reject the real A3b log")
    check(len(verify_a3(rc1, text1, False)) >= 2, "ST2: verify_a3(1b) did not reject the real A1 log")
    check(len(verify_a3(rc1, text1, True)) >= 1, "ST2: verify_a3(preflight) did not reject the real A1 log")
    check(len(verify_a3(rc3, text3, True)) >= 1, "ST2: verify_a3(preflight) accepted the 1b-interpreter log")
    note("[G1-INFO] ST2 cross-rejection: A1-on-A3 problems=%d, A3-on-A1 problems=%d, "
         "A1-on-A3b problems=%d, A3b-on-A1 problems=%d"
         % (len(verify_a1(rc3, text3)), len(verify_a3(rc1, text1, False)),
            len(verify_a1(rc3b, text3b)), len(verify_a3(rc1, text1, True))))

    # ---- A4 opposite combination: strip the BOM from a non-ASCII .ps1
    rel, original = pick_nonascii_ps1(WT)
    check(rel is not None, "A4: no non-ASCII .ps1 found in the copy, control arm has no object")
    if rel:
        stripped = original[len(codecs.BOM_UTF8):]
        with open(os.path.join(WT, rel.replace("/", os.sep)), "wb") as handle:
            handle.write(stripped)
        rc4, text4 = run_hygiene(WT)
        check(rc4 == 1, "A4: stripping the BOM of %s gave hygiene rc=%s" % (rel, rc4))
        check("non-ASCII byte" in text4 and "no UTF-8 BOM" in text4,
              "A4: hygiene did not report the 'non-ASCII without BOM' side for %s" % rel)
        note("[G1-INFO] A4 used %s (%d non-ASCII bytes, BOM removed) -> rc=%s"
             % (rel, sum(1 for b in stripped if b > 127), rc4))
        sh(["git", "checkout", "--", rel], cwd=WT)
        check(md5(read_bytes(os.path.join(WT, rel.replace("/", os.sep)))) == md5(original),
              "A4: %s not restored byte-for-byte" % rel)
        rc4b, text4b = run_hygiene(WT)
        check(rc4b == 0, "A4: hygiene still red after restoring %s (rc=%s)" % (rel, rc4b))
    check(git_clean(WT, ["tools", "docs", "src", "include"]) == "",
          "A4 tail: copy left dirty: %r" % git_clean(WT, ["tools", "docs", "src", "include"]))

    # ---- A5 main worktree untouched
    for p, digest in main_baseline.items():
        check(md5(read_bytes(os.path.join(ROOT, p))) == digest,
              "A5: main worktree %s changed (was %s)" % (p, digest))
    main_post = git_clean(ROOT, ["src", "include", "tools", "docs"])
    check(main_post == main_pre,
          "A5: main worktree changed during the run: before=%r after=%r" % (main_pre, main_post))

    if keep:
        note("[G1-INFO] --keep: copy left at %s, logs in %s" % (WT, LOGDIR))
    else:
        remove_copy()
        note("[G1-INFO] copy removed; %r" % sh(["git", "worktree", "list"], cwd=ROOT).stdout.decode("utf-8", "replace").strip())

    print("[G1-SUMMARY] A1 rc=%s | A3 rc=%s | A3b rc=%s | A2/A0 hygiene rc=%s/%s | failures=%d"
          % (rc1, rc3, rc3b, rc2, rc4b if rel else -1, len(FAILURES)))
    if FAILURES:
        print("G-1 probe: %d problem(s)" % len(FAILURES))
        return 1
    print("G-1 probe: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

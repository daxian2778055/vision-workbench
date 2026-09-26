#!/usr/bin/env python3
"""Self-test for tools/run_qtest.cmake: the four red gates, and the failure-preservation step.

Why this exists: A6 (docs/对标差距推进计划.md) hit a gate-availability hole — the wrapper deletes
and rewrites build/Testing/<Suite>.txt at the start of every run, so an intermittent failure that
goes green on the next run leaves *no evidence at all*, and an observation item can never be
closed. The fix (vfp_preserve_failure) only matters if it provably fires on exactly the red paths
and stays out of the way on the green path. Asserting that against the real ctest gate needs a
rebuild per attempt, so this script drives the wrapper directly with a fake QtTest that emits one
shape per case and checks three things: the exit code, whether a timestamped keep-file was left
next to the log, and whether the "preserved" line was printed.

Run:  python tools/selftest_run_qtest_gates.py [--cmake <path-to-cmake>]
      (tools/ci.ps1 step 1c passes --cmake explicitly; otherwise cmake is looked up on PATH)

Shapes (mode -> emitted result, expected exit code, expected keep-file):
  green       Totals 13 passed/0 failed, rc 0            -> rc 0, no keep
  failed      Totals 12/1 failed, rc 1                   -> rc 1, keep
  failedrc0   Totals 12/1 failed, rc 0                   -> rc 1, keep
  zeropassed  Totals 0 passed/13 skipped, rc 0           -> rc 1, keep
  nototals    log written but has no Totals line, rc 0    -> rc 1, keep
  nocfile     writes nothing at all, rc 0                  -> rc 1, NO keep (nothing to preserve;
                                                            the guard must return, not crash)

The keep-file is named "<mode>.failed-<UTC>.txt", so ".failed-" is an ASCII-only marker that is
unique per case — matched on raw bytes because CMake's console encoding for the Chinese reason
text varies with the caller's code page.

Exit code: 0 when all six shapes match, 1 otherwise.
"""
import os
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WRAPPER = os.path.join(REPO, "tools", "run_qtest.cmake")

FAKE = '''import sys
mode = sys.argv[1]
log = None
for i, a in enumerate(sys.argv):
    if a == "-o":
        log = sys.argv[i + 1].rsplit(",", 1)[0]

HEADER = "********* Start testing of FakeTest *********\\n"


def write(text):
    with open(log, "w", newline="") as f:
        f.write(text)


if mode == "green":
    write(HEADER + "PASS   : FakeTest::initTestCase()\\n"
          + "Totals: 13 passed, 0 failed, 0 skipped, 0 blacklisted, 24ms\\n")
elif mode == "failed":
    write(HEADER
          + "FAIL!  : FakeTest::injected() Compared values are not the same\\n"
          + "Totals: 12 passed, 1 failed, 0 skipped, 0 blacklisted, 25ms\\n")
    sys.exit(1)
elif mode == "failedrc0":
    write(HEADER
          + "FAIL!  : FakeTest::injected() exit code lied\\n"
          + "Totals: 12 passed, 1 failed, 0 skipped, 0 blacklisted, 25ms\\n")
elif mode == "zeropassed":
    write(HEADER + "SKIP   : FakeTest::everything() not enabled\\n"
          + "Totals: 0 passed, 0 failed, 13 skipped, 0 blacklisted, 1ms\\n")
elif mode == "nototals":
    write(HEADER + "PASS   : FakeTest::initTestCase()\\n")
sys.exit(0)
'''

CASES = [
    # (mode, expected rc, expected keep-file)
    ("green", 0, False),
    ("failed", 1, True),
    ("failedrc0", 1, True),
    ("zeropassed", 1, True),
    ("nototals", 1, True),
    ("nocfile", 1, False),
]

MARKER = b".failed-"


def main():
    argv = sys.argv[1:]
    cmake = None
    if "--cmake" in argv:
        i = argv.index("--cmake")
        if i + 1 >= len(argv):
            print("usage: --cmake <path-to-cmake>")
            return 1
        cmake = argv[i + 1]
    else:
        cmake = shutil.which("cmake") or shutil.which("cmake.exe")
    if not cmake or not os.path.isfile(cmake):
        print("FAIL: cmake not found (pass --cmake <path>); the wrapper cannot be driven")
        return 1
    if not os.path.isfile(WRAPPER):
        print("FAIL: wrapper not found: %s" % WRAPPER)
        return 1

    tmp = tempfile.mkdtemp(prefix="vfp-qtest-gates-")
    try:
        fake = os.path.join(tmp, "fake_qtest.py")
        with open(fake, "w", newline="") as f:
            f.write(FAKE)

        bad = 0
        for mode, want_rc, want_keep in CASES:
            log = os.path.join(tmp, "%s.txt" % mode)
            proc = subprocess.run(
                [cmake, "-DVFP_EXE=%s" % sys.executable, "-DVFP_LOG=%s" % log,
                 "-DVFP_ARGS=%s;%s" % (fake, mode), "-P", WRAPPER],
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            out = proc.stdout or b""
            kept = sorted(n for n in os.listdir(tmp) if n.startswith("%s.failed-" % mode))
            printed = out.count(MARKER)
            ok = (proc.returncode == want_rc
                  and bool(kept) == want_keep
                  and printed == (1 if want_keep else 0))
            if not ok:
                bad += 1
            print("%-11s rc=%-3s keep=%d printed=%d  want rc=%s keep=%s  %s"
                  % (mode, proc.returncode, len(kept), printed, want_rc, want_keep,
                     "OK" if ok else "MISMATCH"))
            if not ok:
                print(out.decode("utf-8", "replace")[-800:])

        print("QTEST-GATES cases=%d mismatch=%d" % (len(CASES), bad))
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())

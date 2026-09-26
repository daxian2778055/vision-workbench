#!/usr/bin/env python3
# -*- coding: ascii -*-
"""O-1 baseline: how much does the E6 loop-interval default actually cost / save.

Open item O-1 (docs/yellow) asks whether m_loopIntervalMs should stay 0 (no extra
throttle). The decision trades two numbers off against each other:
  * CPU burned by an unthrottled continuous flow (the "empty spinning" risk), and
  * throughput lost if a nonzero default is pinned instead.
This script produces both, per interval, from one real flow (the SoakTest chain),
so the choice is made on measured values rather than on reasoning.

Reading rule: this file is the executable definition of the measurement. Docs must
cite the numbers this script prints; they must not restate the method in prose only.

Usage:
    python tools/measure_loop_cadence.py                      # intervals 0,50,100 @ 8s
    python tools/measure_loop_cadence.py --seconds 20 --intervals 0,5
    python tools/measure_loop_cadence.py --json build/cadence.json
    python tools/measure_loop_cadence.py --selfcheck          # negative self-test, no exe

Exit codes:
    0 all intervals measured and every consistency check held
    1 measurement missing / exe failed / a consistency check broke (see printed reason)
    2 soak_test.exe not found (build it first)
    (--selfcheck uses 0 = every gate provably red-capable, 1 = one of them is not.)

Console output is ASCII-only: this repo's Windows console is GBK and a Chinese
label here would raise UnicodeEncodeError instead of reporting the reading.
"""

import argparse
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINE_RE = re.compile(r"CADENCE-BASELINE\s+(.*\S)\s*$")
KV_RE = re.compile(r"(\w+)=(-?\d+(?:\.\d+)?)")

# Same runtime-dir recipe ctest uses for these suites: deployed Qt/HALCON/SDK DLLs
# first, then OpenCV, then the build output dir. Without it the child dies at load
# time with 0xc0000135, which would read as "no measurement" rather than "no DLLs".
RUNTIME_DIRS = (
    os.path.join(REPO, "dist", "VisionFlowPlatform"),
    os.path.join(REPO, "thirdparty", "opencv", "build", "x64", "vc16", "bin"),
)


def parse_reading(text):
    """Return the last CADENCE-BASELINE reading found in text, or None."""
    found = None
    for line in text.splitlines():
        m = LINE_RE.search(line)
        if m:
            found = {k: float(v) for k, v in KV_RE.findall(m.group(1))}
    return found


def run_one(exe, seconds, interval, log_dir):
    """Run soak_test once at the given interval; return (reading, error)."""
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(list(RUNTIME_DIRS) + [os.path.dirname(exe), env.get("PATH", "")])
    env["VFP_SOAK_SECONDS"] = str(seconds)
    env["VFP_SOAK_INTERVAL_MS"] = str(interval)
    # Qt turns its plain-text log channel off when it decides there is no console
    # (W-A); the reading line is emitted through that channel.
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    txt = os.path.join(log_dir, "cadence_%dms.txt" % interval)
    report = os.path.join(log_dir, "cadence_%dms_report.txt" % interval)
    env["VFP_SOAK_REPORT"] = report
    # "-o <file>,<format>" is ONE argument (see tools/run_qtest.cmake); splitting it
    # makes QTest read "txt" as a test-function name and fail the whole suite.
    cmd = [exe, "-o", "%s,txt" % txt]
    try:
        p = subprocess.run(cmd, cwd=log_dir, env=env, timeout=seconds * 4 + 240,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except subprocess.TimeoutExpired:
        return None, "timeout after %ds" % (seconds * 4 + 240)
    if p.returncode != 0:
        return None, "soak_test exit=%d (see %s)" % (p.returncode, txt)
    text = ""
    for src in (txt, report):
        if os.path.isfile(src):
            with open(src, "r", encoding="utf-8", errors="replace") as fh:
                text += fh.read()
    reading = parse_reading(text)
    if reading is None:
        return None, "no CADENCE-BASELINE line in %s / %s" % (txt, report)
    return reading, None


SAMPLE_LINE = ("QINFO : SoakTest::testContinuousSoak() CADENCE-BASELINE interval_ms=50 "
               "window_ms=10004 rounds=162 rounds_per_s=16.19 ms_per_round=61.79 "
               "cpu_ms=2125 cpu_ms_per_round=13.117 cpu_core_pct=21.2")


def selfcheck():
    """Prove each gate below can go red on its own (and that green data stays green).

    Without this the checks are just another pair of eyes nobody ever tested --
    the failure mode this repo has been bitten by repeatedly ("wrote it, never
    wired it / never proved it can fail").
    """
    r = parse_reading(SAMPLE_LINE)
    results = []
    results.append(("parser_all_8_fields", r is not None and len(r) == 8
                    and r["interval_ms"] == 50 and r["cpu_ms"] == 2125.0,
                    "parsed=%s" % ("none" if r is None else len(r))))

    good = {"interval_ms": 0.0, "window_ms": 10000.0, "rounds": 1600.0, "rounds_per_s": 160.0,
            "ms_per_round": 6.25, "cpu_ms": 24000.0, "cpu_ms_per_round": 15.0, "cpu_core_pct": 240.0}

    scenarios = [
        ("cpu_never_samples_goes_red",
         [(0, dict(good, cpu_ms=0.0, cpu_ms_per_round=0.0), None)], True),
        ("interval_not_honored_goes_red",
         [(0, good, None), (50, dict(good, rounds_per_s=158.0, ms_per_round=6.3), None)], True),
        ("throughput_not_ordered_goes_red",
         [(0, dict(good, rounds_per_s=100.0), None),
          (50, dict(good, rounds_per_s=100.0, ms_per_round=62.0), None)], True),
        ("missing_row_goes_red",
         [(0, None, "soak_test exit=1 (simulated)")], True),
        ("healthy_set_stays_green",
         [(0, good, None),
          (50, dict(good, rounds=160.0, rounds_per_s=16.0, ms_per_round=62.5,
                    cpu_ms_per_round=14.0), None)], False),
    ]
    for name, rows, expect_red in scenarios:
        checks = []
        check(rows, checks)
        red = [c[0] for c in checks if not c[1]]
        ok = bool(red) if expect_red else not red
        results.append((name, ok, "red=%s" % (",".join(red) if red else "-")))

    bad = [n for n, ok, _ in results if not ok]
    for n, ok, detail in results:
        print("%-32s %-4s %s" % (n, "OK" if ok else "RED", detail))
    print("CADENCE-SELFCHK scenarios=%d mismatch=%d" % (len(results), len(bad)))
    if bad:
        for n in bad:
            print("FAIL: selfcheck scenario did not behave as designed: %s" % n)
        return 1
    print("PASS")
    return 0


def check(rows, checks):
    """Append (name, ok, detail) tuples; every check must be able to go red on its own."""
    def add(name, ok, detail):
        checks.append((name, bool(ok), detail))

    for interval, reading, err in rows:
        if err:
            add("measured@%d" % interval, False, err)
            continue
        add("measured@%d" % interval, True, "rounds=%d" % reading["rounds"])
        add("cpu_sampled@%d" % interval, reading["cpu_ms"] > 0,
            "cpu_ms=%d (0 or -1 means GetProcessTimes is not advancing)" % reading["cpu_ms"])
        add("cpu_per_round@%d" % interval, reading["cpu_ms_per_round"] > 0,
            "cpu_ms_per_round=%.3f" % reading["cpu_ms_per_round"])
        if interval > 0:
            ok = reading["ms_per_round"] >= 0.8 * interval
            add("interval_honored@%d" % interval, ok,
                "ms_per_round=%.2f must be >= 0.8 x %d" % (reading["ms_per_round"], interval))

    got = [(i, r) for i, r, e in rows if r]
    for (i1, r1), (i2, r2) in zip(got, got[1:]):
        add("throughput_ordering@%d<%d" % (i1, i2), r1["rounds_per_s"] > r2["rounds_per_s"],
            "%.2f rounds/s must exceed %.2f rounds/s" % (r1["rounds_per_s"], r2["rounds_per_s"]))


def main():
    ap = argparse.ArgumentParser(description="Measure loop-cadence / CPU baseline per interval (O-1).")
    ap.add_argument("--intervals", default="0,50,100", help="comma list of ms, ascending")
    ap.add_argument("--seconds", type=int, default=8, help="per-interval window (default 8)")
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "bin", "Release", "soak_test.exe"))
    ap.add_argument("--log-dir", default=os.path.join(REPO, "build"))
    ap.add_argument("--json", default=None, help="also dump readings here")
    ap.add_argument("--selfcheck", action="store_true",
                    help="prove each check below can go red; needs no executable")
    args = ap.parse_args()

    if args.selfcheck:
        return selfcheck()

    intervals = [int(x) for x in args.intervals.split(",") if x.strip() != ""]
    if sorted(intervals) != intervals or not intervals:
        print("FAIL: --intervals must be ascending non-empty comma list of ms")
        return 1
    if not os.path.isfile(args.exe):
        print("FAIL: exe not found: %s" % args.exe)
        print("      build it first: cmake --build build --config Release --target soak_test")
        return 2
    if not os.path.isdir(args.log_dir):
        os.makedirs(args.log_dir)

    rows = []
    for interval in intervals:
        print("-- run: interval=%dms window=%ds" % (interval, args.seconds))
        reading, err = run_one(args.exe, args.seconds, interval, args.log_dir)
        rows.append((interval, reading, err))

    hdr = "%9s %9s %11s %11s %11s %11s" % (
        "interval", "rounds", "rounds/s", "ms/round", "cpu ms/r", "cpu %core")
    print("")
    print(hdr)
    print("-" * len(hdr))
    for interval, reading, err in rows:
        if err:
            print("%9d  no reading: %s" % (interval, err))
            continue
        print("%8dms %9d %11.2f %11.2f %11.3f %11.1f" % (
            interval, reading["rounds"], reading["rounds_per_s"], reading["ms_per_round"],
            reading["cpu_ms_per_round"], reading["cpu_core_pct"]))

    print("")
    print("note: acquisition node is file-read (soak.png), not a real camera;")
    print("note: cpu is process-wide (all threads) over a %ds window, single run --" % args.seconds)
    print("      not a 72h soak, and not calibrated against other window lengths.")

    checks = []
    check(rows, checks)
    bad = [c for c in checks if not c[1]]
    print("")
    for name, ok, detail in checks:
        print("%-32s %-4s %s" % (name, "OK" if ok else "RED", detail))
    print("CADENCE-BASELINE checks=%d mismatch=%d" % (len(checks), len(bad)))

    if args.json:
        with open(args.json, "w", encoding="ascii") as fh:
            json.dump({"seconds": args.seconds,
                       "rows": [{"interval_ms": i, "reading": r, "error": e} for i, r, e in rows],
                       "checks": [{"name": n, "ok": o, "detail": d} for n, o, d in checks]},
                      fh, indent=2, sort_keys=True)
            fh.write("\n")

    if bad:
        for name, _ok, detail in bad:
            print("FAIL: %s :: %s" % (name, detail))
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""端到端流水线性能基线与相对回归门禁（AG1）。

判据刻意不是绝对毫秒：同一台机器冻结基线，只拦显著退化，避免不同机型/负载误判。
只有"读数缺失、零轮次、失败轮、CPU 未采样"和"相对基线显著退化"才判红；
性能数字一律打印并可选落 JSON 供人复核。

用法：
  python tools/measure_pipeline_bench.py                     # 跑两臂并对照冻结基线
  python tools/measure_pipeline_bench.py --json out.json
  python tools/measure_pipeline_bench.py --selfcheck         # 自证每条闸能单独变红
  python tools/measure_pipeline_bench.py --emit-baseline     # 显式重冻（需人工审阅）

退出码：0=GREEN，1=RED，2=工具自身异常。
"""
import argparse
import json
import os
import re
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINE_RE = re.compile(r"BENCH-RESULT\s+(.*\S)\s*$")
KV_RE = re.compile(r"(\w+)=(-?\d+(?:\.\d+)?)")
FIELDS = ("db", "seconds", "window_ms", "rounds", "failed", "rounds_per_s",
          "ms_per_round", "cpu_ms", "cpu_ms_per_round", "cpu_core_pct",
          "handles", "working_set_mb")

# Re-frozen 2026-11-27 AG1：同机同口径（640x480 合成图、6 节点链、满速、预热 1s、
# 窗口 8s），DB 两臂分别取一次实测。重冻必须人工审阅并保持日期/原因注释。
BASELINE = {
    "date": "2026-11-27",
    "reason": "AG1 首次冻结：端到端流水线基准（无相机/含 DB 两臂），只拦相对回归",
    "arms": {
        "0": {"rounds_per_s": 144.65, "ms_per_round": 6.390,
              "cpu_ms_per_round": 14.751},
        "1": {"rounds_per_s": 134.82, "ms_per_round": 6.891,
              "cpu_ms_per_round": 15.133},
    },
}

# 相对回归容差：机器负载波动留足余量，只抓量级明显的退化
MAX_MS_PER_ROUND_RATIO = 1.30
MIN_ROUNDS_PER_S_RATIO = 0.75
MAX_CPU_PER_ROUND_RATIO = 1.50


def say(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def parse_reading(text):
    hits = LINE_RE.findall(text)
    if not hits:
        return None
    kv = {k: (float(v) if "." in v else int(v)) for k, v in KV_RE.findall(hits[-1])}
    if not all(f in kv for f in FIELDS):
        return None
    return kv


def run_arm(exe, seconds, with_db, log_dir):
    os.makedirs(log_dir, exist_ok=True)
    report = os.path.join(log_dir, "pipeline_bench_db%d.txt" % with_db)
    env = os.environ.copy()
    env["VFP_BENCH_SECONDS"] = str(seconds)
    env["VFP_BENCH_DB"] = "1" if with_db else "0"
    env["VFP_BENCH_REPORT"] = report
    env["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    deploy = os.path.join(REPO, "dist", "VisionFlowPlatform")
    opencv = os.path.join(REPO, "thirdparty", "opencv", "build", "x64", "vc16", "bin")
    env["QT_PLUGIN_PATH"] = deploy
    env["PATH"] = os.pathsep.join([deploy, opencv,
                                   os.path.dirname(os.path.abspath(exe)), env.get("PATH", "")])
    out = os.path.join(log_dir, "pipeline_bench_db%d.log" % with_db)
    cmd = [exe, "-maxwarnings", "10000", "-o", out + ",txt", "testPipelineBench"]
    try:
        p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=seconds * 4 + 240)
    except FileNotFoundError:
        return None, "bench exe not found: %s" % exe
    except subprocess.TimeoutExpired:
        return None, "bench timed out (db=%d)" % with_db
    text = p.stdout.decode("utf-8", "replace")
    if os.path.isfile(report):
        with open(report, "r", encoding="utf-8", errors="replace") as fh:
            text += "\n" + fh.read()
    if p.returncode != 0:
        return None, "bench exit %d (db=%d)" % (p.returncode, with_db)
    row = parse_reading(text)
    if row is None:
        return None, "BENCH-RESULT missing/incomplete (db=%d)" % with_db
    return row, ""


def check(row, baseline):
    findings = []
    if row is None:
        return ["reading_missing"]
    if row["rounds"] < 3:
        findings.append("rounds_too_few")
    if row["failed"] != 0:
        findings.append("failed_rounds")
    if row["cpu_ms"] <= 0 or row["cpu_ms_per_round"] <= 0:
        findings.append("cpu_not_sampled")
    if baseline:
        if row["ms_per_round"] > baseline["ms_per_round"] * MAX_MS_PER_ROUND_RATIO:
            findings.append("ms_per_round_regressed")
        if row["rounds_per_s"] < baseline["rounds_per_s"] * MIN_ROUNDS_PER_S_RATIO:
            findings.append("throughput_regressed")
        if row["cpu_ms_per_round"] > baseline["cpu_ms_per_round"] * MAX_CPU_PER_ROUND_RATIO:
            findings.append("cpu_per_round_regressed")
    return findings


def selfcheck():
    good = {"rounds": 100, "failed": 0, "cpu_ms": 1000, "cpu_ms_per_round": 10,
            "ms_per_round": 6.0, "rounds_per_s": 150.0}
    base = BASELINE["arms"]["0"]
    problems = []
    if check(good, base):
        problems.append("healthy_set_red")
    cases = [
        ("reading_missing", None, base),
        ("rounds_too_few", dict(good, rounds=1), base),
        ("failed_rounds", dict(good, failed=1), base),
        ("cpu_not_sampled", dict(good, cpu_ms=0, cpu_ms_per_round=0), base),
        ("ms_per_round_regressed", dict(good, ms_per_round=base["ms_per_round"] * 2), base),
        ("throughput_regressed", dict(good, rounds_per_s=base["rounds_per_s"] * 0.5), base),
        ("cpu_per_round_regressed", dict(good, cpu_ms_per_round=base["cpu_ms_per_round"] * 2), base),
    ]
    for name, row, baseline in cases:
        if name not in check(row, baseline):
            problems.append(name + "_dead")
            say("[SELFCHECK] case=%s FAIL (gate did not fire)" % name)
        else:
            say("[SELFCHECK] case=%s ok" % name)
    say("[SELFCHECK-INV] problems=%d verdict=%s" % (len(problems), "RED" if problems else "GREEN"))
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=8)
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "bin", "Release", "soak_test.exe"))
    ap.add_argument("--log-dir", default=os.path.join(REPO, "build", "u44_probe", "pipeline_bench"))
    ap.add_argument("--json", default="")
    ap.add_argument("--selfcheck", action="store_true")
    ap.add_argument("--emit-baseline", action="store_true")
    args = ap.parse_args()

    if args.selfcheck:
        return selfcheck()
    if args.emit_baseline:
        say("[PB] --emit-baseline is intentional: edit BASELINE in this file with a dated reason.")
        return 2

    all_findings = []
    rows = {}
    for arm in ("0", "1"):
        row, err = run_arm(args.exe, args.seconds, arm == "1", args.log_dir)
        if err:
            say("[PB] arm=%s error=%s" % (arm, err))
        if row:
            rows[arm] = row
            say("[PB] arm=%s db=%s rounds=%d rounds_per_s=%.2f ms_per_round=%.3f "
                "cpu_ms_per_round=%.3f" % (arm, row["db"], row["rounds"],
                                           row["rounds_per_s"], row["ms_per_round"],
                                           row["cpu_ms_per_round"]))
        findings = check(row, BASELINE["arms"].get(arm))
        for f in findings:
            all_findings.append("db%s:%s" % (arm, f))
            say("[PB] finding=db%s:%s" % (arm, f))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as fh:
            json.dump({"baseline": BASELINE, "rows": rows, "findings": all_findings},
                      fh, ensure_ascii=False, indent=2)

    say("[PB-INV] findings=%d verdict=%s" % (len(all_findings), "RED" if all_findings else "GREEN"))
    return 1 if all_findings else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        say("[PB] tool error: %r" % exc)
        sys.exit(2)

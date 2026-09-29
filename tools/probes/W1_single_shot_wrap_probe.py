"""W-1 replayable probe: the dangling-timer inventory must not be sensitive to source formatting.

What it proves (advance plan 3.30): the external review's warning W-1 said the inventory tool read a
call's arguments **from the single line** holding `QTimer::singleShot(`, so whenever the second
argument landed on a later line the tool saw an empty second argument, and
`"".startswith("[")` is false -- a genuinely dangling timer was classified as "has a context object"
and the 1f gate stayed green on exactly the shape U-20 crashed on.

The probe therefore pins a **pair** of verdicts per source shape: the pre-fix tool (pinned by
commit, not by HEAD -- HEAD moves) versus the delivered tool.

  S1  one line, no context        red    -> red     (negative control: the pair is not "new is always red")
  S2  2nd argument on next line   green  -> red     (W-1, the crash shape)
  S3  '(' ends the first line     green  -> red     (W-1)
  S4  '(' on the line after name  green  -> red     (W-1; the pre-fix scan never saw the site at all: total=0)
  S5  call never closes by EOF    green  -> red     (fail-closed: reported as UNRES, not assumed safe)
  S6  fewer than two arguments    green  -> red     (fail-closed UNRES)
  G1  the repo's current shape    green  -> green   (no false alarm)
  G2  context argument wrapped    green  -> green
  G3  comma inside a capture list green  -> green   (argc 4 -> 3: body/capture commas are no longer
                                                     counted as separators)
  D1  verifier self-test          the red verifier must reject a green sample and vice versa
  H1  real site, same-line drop   -> copy inventory rc=1 AND tools/ci.ps1 exit 8
  H2  real site, drop + wrap      -> pre-fix rc=0 / delivered rc=1 AND tools/ci.ps1 exit 8  (host level)
  Z0  main worktree untouched     md5 snapshot + porcelain snapshot around every copy write

Every defect is injected inside a one-shot `git worktree add --detach` copy under build/, which the
probe removes again; the main worktree's tracked files are never written to. The synthetic shapes
run in throwaway scratch trees that carry a copy of the tool, because the tool derives its scan
root from its own location.

Note on copy bytes (advance plan 3.18): core.autocrlf=true means a fresh checkout is not
md5-identical to the main worktree for index-LF files, so all comparisons use the copy's own bytes.

Run:  python tools/probes/W1_single_shot_wrap_probe.py        (S1..S6 + G1..G3 + D1 + H1 + H2 + Z0)
      python tools/probes/W1_single_shot_wrap_probe.py --keep (leave the copy + scratch for reading)
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
LOGDIR = os.path.join(ROOT, "build", "w1_probe")
SCRATCH = os.path.join(LOGDIR, "shapes")
WT = os.path.join(LOGDIR, "wt")

CI_PS1 = os.path.join("tools", "ci.ps1")
INVENTORY = os.path.join("tools", "single_shot_inventory.py")
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

# Commit whose tools/single_shot_inventory.py still has the line-local argument read. Pinned by sha
# on purpose: `HEAD` would name the *fixed* tool as soon as this round lands, which would turn every
# "green -> red" pair into a false assertion.
PRE_FIX_SHA = "a423ddc"

# The tool's green-path summary must not move while the classification is hardened: sections
# 3.26/3.27/3.29 quote it and the G2 probe greps it.
BASELINE_SUMMARY = "SINGLESHOT total=18 with_context=18 no_context=0"
OK_LINE = "OK: every singleShot call site is bound to a context object"

SITE_NEEDLE = b"QTimer::singleShot(100, inputImageCombo, [=]() {"
SITE_FILE = "src/ColorConversionNode.cpp"
SITE_LINE = 300

# name -> (source, expected_after_verdict, expected_before_verdict, expected_after_argc, note)
SHAPES = {
    "S1_oneline_noctx": (
        'void f() {\n    QTimer::singleShot(100, [=]() { foo(); });\n}\n',
        "red", "red", 2, "negative control: already red before the fix"),
    "S2_second_arg_wrapped": (
        'void f() {\n    QTimer::singleShot(100,\n        [=]() { foo(); });\n}\n',
        "red", "green", 2, "W-1 itself: U-20's crash shape read as 'has context'"),
    "S3_paren_ends_first_line": (
        'void f() {\n    QTimer::singleShot(\n        0, [=]() { foo(); });\n}\n',
        "red", "green", 2, "W-1 variant, nothing after '(' on the first line"),
    "S4_paren_after_name_line": (
        'void f() {\n    QTimer::singleShot\n        (100, [=]() { foo(); });\n}\n',
        "red", "green", 2, "W-1 variant the old scan could not even count"),
    "S5_unbalanced_to_eof": (
        'void f() {\n    QTimer::singleShot(100, this, [=]() { foo();\n',
        "red", "green", 0, "fail closed: an unterminated call is UNRES, not CTX"),
    "S6_one_argument_only": (
        'void f() {\n    QTimer::singleShot(0);\n}\n',
        "red", "green", 1, "fail closed: no second argument to classify"),
    "G1_repo_shape": (
        'void f() {\n    QTimer::singleShot(100, someWidget, [=]() { foo(); });\n}\n',
        "green", "green", 3, "the shape all 18 repo sites use"),
    "G2_ctx_arg_wrapped": (
        'void f() {\n    QTimer::singleShot(100, someWidget,\n        [=]() { foo(); });\n}\n',
        "green", "green", 3, "wrapped but genuinely bound -> must stay green"),
    "G3_capture_list_comma": (
        'void f() {\n    QTimer::singleShot(0, thumbnailLayout, [this, thumbnailLayout]() {\n'
        '        update(thumbnailLayout);\n    });\n}\n',
        "green", "green", 3, "capture-list and body commas are not separators"),
}

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
    print("[W1-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def sh(args, cwd=None, timeout=1800):
    return subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)


def decode(raw):
    return raw.decode("utf-8", errors="replace") + "\n"


def rel_path(rel, base=None):
    return os.path.join(base or WT, rel.replace("/", os.sep))


def run_tool(tool_path, cwd):
    proc = sh([sys.executable, tool_path], cwd=cwd)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def verdict_of(rc, text):
    """'red' / 'green' plus the label the tool used, so an arm can fail for the right reason."""
    label = "UNRES" if "SINGLESHOT-UNRESOLVED" in text else (
        "NOCTX" if "NOCTX " in text else ("CTX" if "CTX " in text else "?"))
    return ("red" if rc != 0 else "green"), label


def materialize_shape(tool_bytes, tag, source):
    """A throwaway repo: the tool derives its scan root from its own path, so it must be copied in."""
    tree = os.path.join(SCRATCH, tag)
    shutil.rmtree(tree, ignore_errors=True)
    os.makedirs(os.path.join(tree, "tools"))
    os.makedirs(os.path.join(tree, "src"))
    with open(os.path.join(tree, "tools", "single_shot_inventory.py"), "wb") as handle:
        handle.write(tool_bytes)
    io.open(os.path.join(tree, "src", "F.cpp"), "w", encoding="utf-8", newline="\n").write(source)
    io.open(os.path.join(tree, "src", "NodeRegistry.cpp"), "w", encoding="utf-8",
            newline="\n").write("// stub: no VFP_REG here\n")
    return tree


def run_shape(tool_bytes, tag, source):
    tree = materialize_shape(tool_bytes, tag, source)
    rc, text = run_tool(os.path.join(tree, "tools", "single_shot_inventory.py"), tree)
    return rc, text


def shape_arms(pre_fix_bytes, delivered_bytes):
    print("=== shape arms (before = tool at %s, after = delivered tool) ===" % PRE_FIX_SHA)
    for tag in sorted(SHAPES):
        source, want_after, want_before, want_argc, why = SHAPES[tag]
        rc_b, text_b = run_shape(pre_fix_bytes, tag + "__before", source)
        rc_a, text_a = run_shape(delivered_bytes, tag + "__after", source)
        vb, lb = verdict_of(rc_b, text_b)
        va, la = verdict_of(rc_a, text_a)
        argc = re.search(r"argc=(\d+)", text_a)
        got_argc = int(argc.group(1)) if argc else None
        ok = check(vb == want_before and va == want_after and got_argc == want_argc,
                   "%s: before=%s/%s after=%s/%s argc=%s, expected %s/%s argc=%d"
                   % (tag, vb, lb, va, la, got_argc, want_before, want_after, want_argc))
        note("[W1-%s] %-26s before rc=%s %-5s | after rc=%s %-5s argc=%s | %s"
             % ("OK" if ok else "FAIL", tag, rc_b, lb, rc_a, la, got_argc, why))
        if "S4" in tag and vb == "green":
            check("total=0" in text_b, "S4: pre-fix scan was expected to miss the site (total=0), got %r"
                  % [l for l in text_b.split("\n") if l.startswith("SINGLESHOT")])
        if tag == "G3_capture_list_comma":
            argc_b = re.search(r"argc=(\d+)", text_b)
            check(argc_b and argc_b.group(1) == "4",
                  "G3: pre-fix argc was expected to be the polluted 4, got %s" % text_b)


def verifier_self_test():
    """D1: the needles used by H1/H2 must be able to tell a red log from a green one."""
    red = ("SINGLESHOT total=18 with_context=17 no_context=1\n"
           "  NOCTX  ColorConversionNode.cpp:300 argc=2 second='[=]() {'\n"
           "  FAIL: 1 singleShot site(s) still lack a context object\n")
    green = (BASELINE_SUMMARY + "\n  " + OK_LINE + "\n")
    problems_red = red_log_problems(red.split("\n"), SITE_FILE, SITE_LINE)
    problems_green = red_log_problems(green.split("\n"), SITE_FILE, SITE_LINE)
    wrapped_green_moved = red_log_problems(green.split("\n"), SITE_FILE, SITE_LINE + 1)
    check(problems_red == [], "D1: red sample rejected by its own verifier: %r" % problems_red)
    check(len(problems_green) >= 2, "D1: red verifier accepted a GREEN sample (not discriminative): %r"
          % problems_green)
    check(len(wrapped_green_moved) >= 1, "D1: verifier accepted a log blaming the wrong line: %r"
          % wrapped_green_moved)
    note("[W1-INFO] D1 discriminative: red sample problems=%d, green sample problems=%d"
         % (len(problems_red), len(problems_green)))


def red_log_problems(lines, rel, line_no):
    """Why a log is NOT a red at rel:line_no with argc=2 -- empty list means it is exactly that."""
    text = "\n".join(lines)
    needle = "NOCTX  %s:%d argc=2 second='[=]() {'" % (rel.split("/")[-1], line_no)
    problems = []
    if needle not in text:
        problems.append("missing needle %r" % needle)
    if "FAIL: 1 singleShot site(s) still lack a context object" not in text:
        problems.append("missing FAIL line")
    if OK_LINE in text:
        problems.append("log also carries the OK line")
    if "no_context=1" not in text:
        problems.append("summary does not report no_context=1")
    return problems


def pre_fix_bytes_or_die():
    proc = sh(["git", "show", "%s:%s" % (PRE_FIX_SHA, INVENTORY.replace(os.sep, "/"))], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit("cannot read the pinned pre-fix tool at %s: %s"
                         % (PRE_FIX_SHA, decode(proc.stderr).strip()))
    blob = proc.stdout
    if b"SINGLESHOT-UNRESOLVED" in blob:
        raise SystemExit("pinned tool at %s already has the fix -- the pair assertions would be vacuous"
                         % PRE_FIX_SHA)
    if b"if len(args) > 1 else" not in blob:
        raise SystemExit("pinned tool at %s is not the line-local reader this probe describes"
                         % PRE_FIX_SHA)
    return blob


# ---------------------------------------------------------------- copy (host) arms

def make_copy(sync_paths):
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        note("[W1-INFO] copy created at %s (detached HEAD %s)"
             % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
    else:
        note("[W1-INFO] reusing existing copy at %s" % WT)
    dirty = []
    for rel in sync_paths:
        main_bytes = read_bytes(os.path.join(ROOT, rel))
        with open(rel_path(rel), "wb") as handle:
            handle.write(main_bytes)
        check(md5(read_bytes(rel_path(rel))) == md5(main_bytes),
              "copy setup: %s in the copy does not match the main worktree" % rel)
        head_proc = sh(["git", "show", "HEAD:%s" % rel.replace(os.sep, "/")], cwd=WT)
        differs = md5(head_proc.stdout) != md5(main_bytes)
        if differs:
            dirty.append("M %s" % rel.replace(os.sep, "/"))
        note("[W1-INFO] copy synced %s from the main worktree (md5 %s, differs from HEAD: %s)"
             % (rel, md5(main_bytes)[:12], differs))
    return tuple(sorted(dirty))


def remove_copy():
    proc = sh(["git", "worktree", "remove", "--force", WT], cwd=ROOT)
    if proc.returncode != 0:
        note("[W1-WARN] git worktree remove said: " + decode(proc.stdout + proc.stderr).strip())
    sh(["git", "worktree", "prune"], cwd=ROOT)


def git_clean(cwd, paths):
    return decode(sh(["git", "status", "--porcelain", "--"] + paths, cwd=cwd).stdout).strip()


def rewrite_site(copy_bytes, replacement):
    """Replace SITE_NEEDLE in the copy exactly once; returns (new_bytes) or None."""
    if copy_bytes.count(SITE_NEEDLE) != 1:
        return None
    return copy_bytes.replace(SITE_NEEDLE, replacement, 1)


def eol_of(raw):
    return b"\r\n" if b"\r\n" in raw else b"\n"


def write_copy(raw):
    with open(rel_path(SITE_FILE), "wb") as handle:
        handle.write(raw)


def host_arms(pre_fix_bytes):
    """H1 same-line drop, H2 drop + wrap, each: tool verdict + ci.ps1 exit code, then restore."""
    original = read_bytes(rel_path(SITE_FILE))
    baseline_rc, baseline_text = run_tool(rel_path(INVENTORY), WT)
    check(baseline_rc == 0 and BASELINE_SUMMARY in baseline_text,
          "H0: copy baseline inventory rc=%s text=%r" % (baseline_rc, baseline_text[:200]))
    note("[W1-INFO] H0 copy baseline rc=%s | %s"
         % (baseline_rc, [l for l in baseline_text.split("\n") if l.startswith("SINGLESHOT")][0]))

    logs = {}
    cases = [
        ("H1_same_line_drop", SITE_NEEDLE.replace(b", inputImageCombo", b"", 1)),
        ("H2_drop_and_wrap", b"QTimer::singleShot(100," + eol_of(original)
         + b"            [=]() {"),
    ]
    for tag, replacement in cases:
        broken = rewrite_site(original, replacement)
        if broken is None:
            fail("%s: cannot inject -- %s does not contain the site needle exactly once" % (tag, SITE_FILE))
            continue
        write_copy(broken)
        rc_tool, text_tool = run_tool(rel_path(INVENTORY), WT)
        # the same broken bytes, judged by the pinned pre-fix tool: run it as a sibling copy of the
        # tool only, so the scanned sources stay the copy's own.
        tmp_tool = os.path.join(WT, "tools", "single_shot_inventory_prefix.py")
        with open(tmp_tool, "wb") as handle:
            handle.write(pre_fix_bytes)
        rc_pre, text_pre = run_tool(tmp_tool, WT)
        os.remove(tmp_tool)
        problems = red_log_problems(text_tool.split("\n"), SITE_FILE, SITE_LINE)
        rc_ci, text_ci = run_ci("%s_ci.txt" % tag)
        check(rc_tool == 1, "%s: delivered inventory rc=%s, expected 1" % (tag, rc_tool))
        check(problems == [], "%s: red log does not carry the pinned needles: %r" % (tag, problems))
        check(rc_ci == 8, "%s: ci.ps1 rc=%s, expected 8" % (tag, rc_ci))
        check("=== Dangling-timer site inventory ===" in text_ci and "=== Configure" not in text_ci,
              "%s: host echo did not stop at step 1f" % tag)
        note("[W1-INFO] %s: pre-fix tool rc=%s (%s) | delivered tool rc=%s | ci.ps1 rc=%s"
             % (tag, rc_pre, verdict_of(rc_pre, text_pre)[1], rc_tool, rc_ci))
        logs[tag] = (rc_pre, rc_tool, rc_ci)
        if tag == "H2_drop_and_wrap":
            check(rc_pre == 0, "%s: the pre-fix tool was expected to stay GREEN on these very bytes "
                               "(that is W-1); rc=%s text=%r" % (tag, rc_pre, text_pre[:220]))

        write_copy(original)
        rc_back, text_back = run_tool(rel_path(INVENTORY), WT)
        check(rc_back == 0 and BASELINE_SUMMARY in text_back,
              "%s: inventory did not return to baseline after restore (rc=%s)" % (tag, rc_back))
        check(md5(read_bytes(rel_path(SITE_FILE))) == md5(original),
              "%s: restore is not byte-for-byte" % tag)
    return logs


def run_ci(log_name):
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1], cwd=WT)
    text = decode(proc.stdout) + decode(proc.stderr)
    with open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return proc.returncode, text


def main():
    argv = sys.argv[1:]
    keep = "--keep" in argv
    os.makedirs(LOGDIR, exist_ok=True)
    shutil.rmtree(SCRATCH, ignore_errors=True)

    pre_fix = pre_fix_bytes_or_die()
    delivered = read_bytes(os.path.join(ROOT, INVENTORY))
    check(md5(pre_fix) != md5(delivered), "setup: the pinned pre-fix tool is byte-identical to the delivered one")

    # Z0: what the main worktree looks like before anything runs, checked again at the end.
    watch = [INVENTORY, os.path.join("src", "ColorConversionNode.cpp")]
    main_baseline = dict((p, md5(read_bytes(os.path.join(ROOT, p)))) for p in watch)
    main_pre = git_clean(ROOT, ["src", "include", "tools", "docs"])

    shape_arms(pre_fix, delivered)
    verifier_self_test()

    # the tool's own reading of the real repo, from the main work tree
    rc0, text0 = run_tool(os.path.join(ROOT, INVENTORY), ROOT)
    check(rc0 == 0 and BASELINE_SUMMARY in text0 and "UNRES" not in text0,
          "A0: main work tree inventory rc=%s text=%r" % (rc0, text0[:200]))
    note("[W1-INFO] A0 real src: rc=%s | %s" % (rc0, BASELINE_SUMMARY if BASELINE_SUMMARY in text0 else "?"))

    dirty = make_copy([INVENTORY])
    try:
        host_logs = host_arms(pre_fix)
        post = tuple(sorted(l.strip() for l in git_clean(WT, ["src", "include", "tools", "docs"]).splitlines()
                            if l.strip()))
        check(post == dirty, "H2: copy is not back to its sync footprint: %r != %r" % (post, dirty))
    finally:
        if keep:
            note("[W1-INFO] --keep: copy left at %s, logs in %s" % (WT, LOGDIR))
        else:
            remove_copy()

    for p, digest in main_baseline.items():
        check(md5(read_bytes(os.path.join(ROOT, p))) == digest, "Z0: main work tree %s changed" % p)
    check(git_clean(ROOT, ["src", "include", "tools", "docs"]) == main_pre,
          "Z0: main work tree porcelain moved during the run")

    print("[W1-SUMMARY] shapes=%d | H1 pre/deliv/ci=%s | H2 pre/deliv/ci=%s | failures=%d"
          % (len(SHAPES), host_logs.get("H1_same_line_drop"), host_logs.get("H2_drop_and_wrap"),
             len(FAILURES)))
    if FAILURES:
        print("W-1 probe: %d problem(s)" % len(FAILURES))
        return 1
    print("W-1 probe: OK")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())

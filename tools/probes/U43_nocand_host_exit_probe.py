"""U-43 replayable probe: the frozen nocand citation roster must be wired into the host, and a red
verdict must actually stop it.

What it proves (advance plan 3.47): the sixth-round review (S-2) got the 31-row no-candidate roster
frozen as NOCAND_BASELINE inside tools/src_anchor_inventory.py, so the script can now return 1. G-1
and G-2 named the failure shape this round closes: **a gate that can go red but is called by nothing
is a dead gate** -- until this round no caller existed (grep over *.ps1/*.py/*.yml found only prose
mentions), so that red could only ever be seen by whoever ran the script by hand.

Arms (all defects injected inside a one-shot `git worktree add --detach` copy under build/, which the
probe deletes again; the main worktree's tracked files are never written to):

  C1  code occupancy   HEAD's ci.ps1 has 0 executable `exit 10`; the delivered one has exactly 1,
                       step 1h's Write-Step line and the header code-table row are both present,
                       and the per-code counts of 3..9 are unmoved (no other step's code repurposed)
  B0  copy baseline    delivered tool on the copy's own docs -> rc=0, added=0
  H1  tight style      one more row on a key the baseline already counts once -> tool rc=1
                       AND ci.ps1 exit 10, echo stops at step 1h (no === Configure)
  H2  spaced style     a new (spaced, path, n, n) key -> tool rc=1 AND ci.ps1 exit 10
  N1  negative control the same citation, prose naming a backticked identifier -> tool rc=0; the row
                       is pinned as cands=1(loadprojectforu43control) AND win0=miss, because that
                       token exists nowhere in the file -- so the exemption can only come from the
                       prose NAMING an identifier, never from it MATCHING
  N2  restore          injected files removed -> tool rc=0 and the four echoed baseline lines are
                       byte-identical to B0's
  M1  script code 2    delivered tool with --docs-root <missing> -> rc=2 (input problem, not a
                       roster finding)
  M2  host maps 2      copy-only temporary edit: step 1h passes --docs-root <missing> -> ci.ps1 still
                       exit 10 AND the failure line reads "script exit 2" (the host test is -ne 0, it
                       does not special-case 1). Reverted right after.
  Z0  snapshots        main worktree: md5 + porcelain of the watched files, re-checked last. Copy: its
                       footprint is MEASURED right after the sync and must not move during the run,
                       and no injected file may be left in the copy's docs/

Not proven here, and said so (advance plan 3.47 表 3 末行): the script's own exit 3 is the wrapper's
`except` branch (its code shape only); no leg manufactures a crash. The host GREEN path through step 1h is not
run inside this probe either -- a green host run needs configure+build+ctest, which the copy does not
carry; that leg is taken from this round's end-to-end close-out run on the main worktree.

Run:  python tools/probes/U43_nocand_host_exit_probe.py         (C1 + B0 + H1 + H2 + N1 + N2 + M1 + M2 + Z0)
      python tools/probes/U43_nocand_host_exit_probe.py --keep  (leave the copy + logs for reading)
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
LOGDIR = os.path.join(ROOT, "build", "u43_probe")
WT = os.path.join(LOGDIR, "wt")
POWERSHELL = r"C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe"

CI_PS1 = os.path.join("tools", "ci.ps1")
TOOL = os.path.join("tools", "src_anchor_inventory.py")

# Two different forms of the same step, and the arms must not confuse them: ci.ps1's own bytes
# carry the Write-Step call, the host's console carries the "=== ... ===" that function prints.
STEP_HEADING = "=== Source-line citation roster baseline ==="
STEP_WRITE = 'Write-Step "Source-line citation roster baseline"'
ECHO_RX = re.compile(r"^(NOCANDELTA|ERROR|INV nocand|INV verdict)")

# The citation this round's legs point at. Both are real, in-range lines of a tracked source file, so
# the injected row reaches the roster instead of being printed as file=ABSENT.
TIGHT_CITE = "src/ProjectManager.cpp:334"      # already counted once in the baseline -> multiset leg
SPACED_CITE = "include/ProjectManager.h :20"   # no (spaced, include/ProjectManager.h, 20) key exists

# The delta line each injection must produce, exactly as the script prints it.
ADDED_H1 = "NOCANDELTA dir=ADDED rows=1 kind=tight cite=%s-334" % TIGHT_CITE
ADDED_H2 = "NOCANDELTA dir=ADDED rows=1 kind=spaced cite=include/ProjectManager.h:20-20"

MISSING_DOCS = "u43_no_such_docs_root"

FAILURES = []


def fail(text):
    FAILURES.append(text)
    print("[U43-FAIL] " + text)


def check(cond, text):
    if not cond:
        fail(text)
    return cond


def yn(cond):
    return "yes" if cond else "NO"


def md5(data):
    return hashlib.md5(data).hexdigest()


def read_bytes(path):
    with io.open(path, "rb") as handle:
        return handle.read()


def sh(args, cwd=None, timeout=1800):
    return subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout)


def decode(raw):
    """One place that kills the trailing CR: print() on this console emits \\r\\n, and every
    exact-equality leg below (pinned ADDED line, DOCID name match, baseline echo comparison)
    otherwise fails on an invisible carriage return."""
    return raw.decode("utf-8", errors="replace").replace("\r\n", "\n")


def rel(*parts):
    return os.path.join(WT, *parts)


def run_tool(args_extra=None, cwd=None):
    cmd = [sys.executable, TOOL] + list(args_extra or [])
    proc = sh(cmd, cwd=cwd or WT)
    return proc.returncode, decode(proc.stdout) + decode(proc.stderr)


def echoed(text):
    return [ln for ln in text.split("\n") if ECHO_RX.match(ln)]


def run_ci(log_name, cwd=None):
    proc = sh([POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", CI_PS1],
              cwd=cwd or WT)
    text = decode(proc.stdout) + decode(proc.stderr)
    with io.open(os.path.join(LOGDIR, log_name), "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return proc.returncode, text


# ------------------------------------------------------------------ injection helpers

def inject(name, body, expect_line):
    """Create ONE untracked markdown file inside the copy's docs/ (never the main worktree)."""
    path = rel("docs", name)
    check(not os.path.exists(path), "setup: %s already exists in the copy" % name)
    with io.open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(body)
    got = [ln for ln in io.open(path, encoding="utf-8").read().split("\n") if TIGHT_CITE in ln or SPACED_CITE in ln]
    check(len(got) == 1 and got[0] == expect_line,
          "setup: injected file %s does not carry its citation alone: %r" % (name, got))
    return path


def drop(path):
    if os.path.exists(path):
        os.remove(path)


def delta_lines(text, direction):
    return [ln for ln in text.split("\n") if ln.startswith("NOCANDELTA dir=%s" % direction)]


def did_of(text, doc_name):
    """The D-index the tool assigned to docs/<doc_name> (names are Chinese, so rows cite ids)."""
    for ln in text.split("\n"):
        if ln.startswith("DOCID "):
            did, printed = ln[6:].split(" ", 1)
            if printed == doc_name:
                return did
    return None


# ------------------------------------------------------------------ arms

def arm_c1_code_occupancy():
    """The new host code must be free at HEAD and used by exactly one thing now."""
    head_proc = sh(["git", "show", "HEAD:" + CI_PS1.replace(os.sep, "/")], cwd=ROOT)
    check(head_proc.returncode == 0, "C1: cannot read HEAD's ci.ps1")
    head_txt = decode(head_proc.stdout)
    delivered_txt = decode(read_bytes(os.path.join(ROOT, CI_PS1)))

    def exec_exits(text):
        return [m.group(1) for m in re.finditer(r"(?m)^\s*exit\s+(\d+)\s*$", text)]

    head_codes = exec_exits(head_txt)
    now_codes = exec_exits(delivered_txt)
    check(head_codes.count("10") == 0,
          "C1: HEAD already had an executable `exit 10` (%d) -- the code would not be free"
          % head_codes.count("10"))
    check(now_codes.count("10") == 1,
          "C1: delivered ci.ps1 has %d executable `exit 10`, expected exactly 1" % now_codes.count("10"))
    for code in ("3", "4", "5", "6", "7", "8", "9"):
        check(now_codes.count(code) == head_codes.count(code),
              "C1: code %s count moved (%d -> %d); this step must not repurpose another step's code"
              % (code, head_codes.count(code), now_codes.count(code)))
    step_write = check(STEP_WRITE in delivered_txt,
                       "C1: step 1h's Write-Step line missing from the delivered file")
    header_table = check("10 = source-line citation roster" in delivered_txt,
                         "C1: the header exit-code table was not updated (A2's lesson: a host code "
                         "that is not in the table is a code nobody can look up)")
    print("[U43-OK] C1 head_exit10=%d delivered_exit10=%d codes_3_to_9=%s step_write=%s header_table=%s"
          % (head_codes.count("10"), now_codes.count("10"),
             " ".join("%s:%d" % (c, now_codes.count(c)) for c in "3456789"),
             yn(step_write), yn(header_table)))


def arm_b0_baseline():
    rc, text = run_tool()
    lines = echoed(text)
    check(rc == 0, "B0: delivered tool on the copy's own docs rc=%s, expected 0" % rc)
    check(any(ln.startswith("INV nocand_added=0 nocand_removed=0") for ln in lines),
          "B0: baseline copy is not already clean: %r" % lines)
    print("[U43-OK] B0 copy baseline rc=%d | %s" % (rc, " / ".join(lines)))
    return lines


def arm_host_red(tag, path, doc_name, expect_delta, pre_echo):
    """Inject -> tool red -> host exit 10 -> restore, checking every step of that chain."""
    rc_tool, text_tool = run_tool()
    check(rc_tool == 1, "%s: tool rc=%s after injection, expected 1" % (tag, rc_tool))
    added = delta_lines(text_tool, "ADDED")
    check(added == [expect_delta],
          "%s: ADDED delta not pinned: got %r, expected exactly %r" % (tag, added, [expect_delta]))
    check(not delta_lines(text_tool, "REMOVED"),
          "%s: injection also produced a REMOVED delta -- controlled variables broken: %r"
          % (tag, delta_lines(text_tool, "REMOVED")))
    did = did_of(text_tool, doc_name)
    check(did is not None, "%s: the injected doc is not in the tool's own DOCID map" % tag)
    rows = [ln for ln in text_tool.split("\n")
            if ln.startswith("NOCANDROW ") and (" doc=%s " % did) in ln]
    check(len(rows) == 1, "%s: the ADDED row does not trace back to the injected file (did=%s rows=%r)"
          % (tag, did, rows))
    rc_ci, text_ci = run_ci("%s_ci.txt" % tag)
    step_lines = [ln for ln in text_ci.split("\n") if ln.startswith("=== ")]
    step_reached = check(STEP_HEADING in text_ci, "%s: host echo never reached step 1h: %r"
                         % (tag, step_lines[:6]))
    stopped_early = check("=== Configure" not in text_ci,
                          "%s: host walked past the red into configure/build -- the step does not "
                          "stop it" % tag)
    check(rc_ci == 10, "%s: ci.ps1 rc=%s, expected 10" % (tag, rc_ci))
    check("script exit 1" in text_ci,
          "%s: failure line does not echo the script's own code 1: %r"
          % (tag, [ln for ln in text_ci.split("\n") if "roster check failed" in ln]))
    shown = [ln.strip() for ln in text_ci.split("\n") if ln.strip().startswith("NOCANDELTA dir=ADDED")]
    check(shown == [expect_delta],
          "%s: the ADDED row is not echoed to the console as the script printed it "
          "(a red must name its key): %r" % (tag, shown))
    drop(path)
    rc_back, text_back = run_tool()
    back_clean = rc_back == 0 and echoed(text_back) == pre_echo
    check(back_clean,
          "%s: after restore tool rc=%s echo_moved=%s" % (tag, rc_back,
                                                          echoed(text_back) != pre_echo))
    print("[U43-OK] %s tool rc=%d added=%d | ci rc=%d step_reached=%s configure_reached=%s "
          "echoed_ADDED=%d | restore rc=%d echo_identical=%s"
          % (tag, rc_tool, len(added), rc_ci, yn(step_reached), yn(not stopped_early),
             len(shown), rc_back, yn(back_clean)))


def arm_n1_negative():
    """A citation whose prose names an identifier is not a roster row: the red is the roster, not the raw count."""
    body = (u"# U-43 \u6ce8\u5165\u6837\u672c\uff08N1 \u8d1f\u5bf9\u7167\uff09\n\n"
            u"\u5bf9\u7167\u8bf4\u660e\uff1a" + TIGHT_CITE +
            u" \u5904\u7684 `loadProjectForU43Control` \u624d\u6709\u53ef\u70b9\u540d\u7684\u6807\u8bc6\u7b26\n")
    path = inject("U43_N1.md", body, u"\u5bf9\u7167\u8bf4\u660e\uff1a" + TIGHT_CITE +
                  u" \u5904\u7684 `loadProjectForU43Control` \u624d\u6709\u53ef\u70b9\u540d\u7684\u6807\u8bc6\u7b26")
    try:
        rc, text = run_tool()
        check(rc == 0, "N1: naming an identifier on the cited line still went red (rc=%s)" % rc)
        check(not delta_lines(text, "ADDED"),
              "N1: unexpected ADDED delta with cands named: %r" % delta_lines(text, "ADDED"))
        did = did_of(text, "U43_N1.md")
        check(did is not None, "N1: injected doc missing from the tool's own DOCID map")
        rows = [ln for ln in text.split("\n")
                if ln.startswith("ROW doc=%s " % did) and TIGHT_CITE in ln]
        row = rows[0] if len(rows) == 1 else ""
        # win0=miss is asserted, not tolerated: `loadProjectForU43Control` does not exist anywhere in
        # the file, so the exemption below can only come from the prose NAMING an identifier, never
        # from the identifier MATCHING. Whether a miss should itself be red is the open step 2.
        check(len(rows) == 1
              and "cands=1(loadprojectforu43control)" in row.replace(" ", "")
              and "win0=miss" in row,
              "N1: the injected row is not the named-but-unmatched one this leg describes: %r" % rows)
        nocand = [ln for ln in text.split("\n")
                  if ln.startswith("NOCANDROW ") and (" doc=%s " % did) in ln]
        check(not nocand, "N1: the named-identifier row landed in the roster anyway: %r" % nocand)
        print("[U43-OK] N1 tool rc=%d no_added_delta=%s nocandrow_for_injected_doc=%d | %s"
              % (rc, yn(not delta_lines(text, "ADDED")), len(nocand), row[:150]))
    finally:
        drop(path)


def arm_m1_script_code2():
    rc, text = run_tool(["--docs-root", MISSING_DOCS], cwd=WT)
    check(rc == 2, "M1: tool with an unreadable docs root rc=%s, expected 2" % rc)
    check("ERROR docs directory not found" in text,
          "M1: rc=2 did not come with the input-error line: %r" % text[:200])
    print("[U43-OK] M1 script rc=%d input_error_echoed=yes" % rc)


def arm_m2_host_maps_code2():
    """Temporarily point step 1h at a missing docs root, inside the copy only.

    This is the leg that shows the host's test is `-ne 0` rather than `if 1`: the script's code 2
    (unreadable input) must still stop the build, and must still be labelled as code 2 in the echo so
    a broken input is never read as a roster finding. The edit is reverted in the same arm.
    """
    ci_path = rel(CI_PS1)
    original = read_bytes(ci_path)
    needle = b"$nocandArgs = $hygienePre + @('tools/src_anchor_inventory.py')"
    replacement = (b"$nocandArgs = $hygienePre + @('tools/src_anchor_inventory.py', "
                   b"'--docs-root', '%s')" % MISSING_DOCS.encode("ascii"))
    if not check(original.count(needle) == 1,
                 "M2: cannot locate step 1h's argument line in the copy (%d hit(s))" % original.count(needle)):
        return
    with io.open(ci_path, "wb") as handle:
        handle.write(original.replace(needle, replacement, 1))
    try:
        rc_ci, text_ci = run_ci("M2_ci.txt")
        check(rc_ci == 10, "M2: ci.ps1 rc=%s with the script returning 2, expected 10" % rc_ci)
        labelled = check("script exit 2" in text_ci,
                         "M2: the echo does not carry the script's own code 2 -- 2 would read as a "
                         "roster red: %r"
                         % [ln for ln in text_ci.split("\n") if "roster check failed" in ln])
        stopped = check("=== Configure" not in text_ci,
                        "M2: host walked past an input error into configure")
        print("[U43-OK] M2 ci rc=%d echo_labels_code2=%s configure_reached=%s"
              % (rc_ci, yn(labelled), yn(not stopped)))
    finally:
        with io.open(ci_path, "wb") as handle:
            handle.write(original)
        check(md5(read_bytes(ci_path)) == md5(original), "M2: revert is not byte-for-byte")


# ------------------------------------------------------------------ copy

def make_copy():
    if not os.path.isdir(WT):
        os.makedirs(os.path.dirname(WT), exist_ok=True)
        proc = sh(["git", "worktree", "add", "--detach", WT, "HEAD"], cwd=ROOT)
        if proc.returncode != 0:
            raise SystemExit("cannot create worktree copy: " + decode(proc.stdout + proc.stderr))
        print("[U43-INFO] copy at %s (detached %s)"
              % (WT, decode(sh(["git", "rev-parse", "HEAD"], cwd=WT).stdout).strip()))
    else:
        print("[U43-INFO] reusing existing copy at %s" % WT)
    for p in (CI_PS1, TOOL):
        main_bytes = read_bytes(os.path.join(ROOT, p))
        checked_out = read_bytes(rel(p))
        with open(rel(p), "wb") as handle:
            handle.write(main_bytes)
        head_blob = sh(["git", "show", "HEAD:" + p.replace(os.sep, "/")], cwd=WT).stdout
        check(md5(read_bytes(rel(p))) == md5(main_bytes), "copy setup: %s did not sync" % p)
        # Why the footprint is MEASURED below and not predicted from these md5s: core.autocrlf=true
        # checked this file out as CRLF while its blob is LF, so syncing the LF bytes makes
        # `git status` report M even though they are md5-identical to the blob (both readings
        # printed here). The leg's claim is "the footprint does not MOVE during the run".
        print("[U43-INFO] copy synced %s | checkout %dB/crlf=%d, main %dB/crlf=%d, HEAD blob md5==main: %s"
              % (p, len(checked_out), checked_out.count(b"\r\n"), len(main_bytes),
                 main_bytes.count(b"\r\n"), yn(md5(head_blob) == md5(main_bytes))))
    return porcelain(WT)


def remove_copy():
    proc = sh(["git", "worktree", "remove", "--force", WT], cwd=ROOT)
    if proc.returncode != 0:
        print("[U43-WARN] git worktree remove said: " + decode(proc.stdout + proc.stderr).strip())
    sh(["git", "worktree", "prune"], cwd=ROOT)


def porcelain(cwd):
    return tuple(sorted(l.strip() for l in decode(
        sh(["git", "status", "--porcelain", "--", "src", "include", "tools", "docs"], cwd=cwd).stdout
    ).splitlines() if l.strip()))


def main():
    keep = "--keep" in sys.argv[1:]
    os.makedirs(LOGDIR, exist_ok=True)

    watch = [CI_PS1, TOOL, os.path.join("docs", u"\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212.md")]
    main_md5 = dict((p, md5(read_bytes(os.path.join(ROOT, p)))) for p in watch)
    main_pre = porcelain(ROOT)

    arm_c1_code_occupancy()

    dirty = make_copy()
    try:
        pre_echo = arm_b0_baseline()
        p1 = inject("U43_H1.md",
                    u"# U-43 \u6ce8\u5165\u6837\u672c\uff08H1 \u7d27\u8d34\u6837\u5f0f\uff09\n\n"
                    u"\u6ce8\u5165\u8bf4\u660e\uff1a" + TIGHT_CITE +
                    u"\uff0c\u6563\u6587\u4e0d\u70b9\u540d\u4efb\u4f55\u6807\u8bc6\u7b26\n",
                    u"\u6ce8\u5165\u8bf4\u660e\uff1a" + TIGHT_CITE +
                    u"\uff0c\u6563\u6587\u4e0d\u70b9\u540d\u4efb\u4f55\u6807\u8bc6\u7b26")
        arm_host_red("H1", p1, "U43_H1.md", ADDED_H1, pre_echo)

        p2 = inject("U43_H2.md",
                    u"# U-43 \u6ce8\u5165\u6837\u672c\uff08H2 \u5e26\u7a7a\u683c\u6837\u5f0f\uff09\n\n"
                    u"\u5e26\u7a7a\u683c\u8bf4\u660e\uff1a" + SPACED_CITE +
                    u"\uff0c\u6563\u6587\u4e5f\u4e0d\u70b9\u540d\u6807\u8bc6\u7b26\n",
                    u"\u5e26\u7a7a\u683c\u8bf4\u660e\uff1a" + SPACED_CITE +
                    u"\uff0c\u6563\u6587\u4e5f\u4e0d\u70b9\u540d\u6807\u8bc6\u7b26")
        arm_host_red("H2", p2, "U43_H2.md", ADDED_H2, pre_echo)

        arm_n1_negative()
        arm_m1_script_code2()
        arm_m2_host_maps_code2()

        post = porcelain(WT)
        footprint_ok = check(post == dirty,
                             "Z0: copy footprint not back to its sync set: %r != %r" % (post, dirty))
        left = [n for n in os.listdir(rel("docs")) if n.startswith("U43_")]
        check(not left, "Z0: injected files left in the copy: %r" % left)
        print("[U43-OK] Z0 copy footprint unchanged=%s (lines=%d) injected_left=%d"
              % (yn(footprint_ok), len(post), len(left)))
    finally:
        if keep:
            print("[U43-INFO] --keep: copy left at %s, logs in %s" % (WT, LOGDIR))
        else:
            remove_copy()

    md5_ok = 0
    for p, digest in main_md5.items():
        if check(md5(read_bytes(os.path.join(ROOT, p))) == digest,
                 "Z0: main worktree %s changed" % p):
            md5_ok += 1
    porcelain_ok = check(porcelain(ROOT) == main_pre, "Z0: main worktree porcelain moved during the run")
    print("[U43-OK] Z0 main worktree md5_matched=%d/%d porcelain_unchanged=%s"
          % (md5_ok, len(main_md5), yn(porcelain_ok)))

    print("[U43-SUMMARY] legs=9 (C1 B0 H1 H2 N1 N2 M1 M2 Z0; N2 is the restore check that runs "
          "inside each host arm) | failures=%d" % len(FAILURES))
    if FAILURES:
        print("U-43 probe: %d problem(s)" % len(FAILURES))
        return 1
    print("U-43 probe: OK")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    sys.exit(main())

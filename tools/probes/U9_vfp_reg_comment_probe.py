"""U-9 replayable break-proof matrix for `tools/doc_check.ps1` gate A.

What it proves (all in TEMP copies -- the worktree, src/NodeRegistry.cpp and tools/doc_check.ps1 are
never written to):

  control  new gate + real registry + real docs                     -> rc=0, 0 findings
  B1       new gate + one LIVE VFP_REG line commented out           -> rc=1, 1 finding   (gate bites)
  B1o      PRE-FIX gate (blob 23c1379) + the SAME broken registry   -> rc=0, 0 findings  (blind spot)
  B2a      new gate + HEAD snapshot of the ledger, which really claimed CropNode  -> rc=1, 1 finding
  B2b      PRE-FIX gate + the SAME docs                              -> rc=0  (U-9 itself, reproduced)
  B2c      new gate + a registry where only that line's `// ` was taken off -> rc=0 (no collateral)
  B3       new gate with the comment-stripping line neutralised + B1's registry -> rc=0 (decisive step)
  B4       trailing comment on a live registration line             -> rc=0 (must NOT be stripped)
  B5       two live registrations commented out                     -> findings == per-claim counts

Why the two temp gate copies sit in build/ and not next to this file: doc_check derives RepoRoot as the
PARENT of its own directory, so from build/ RepoRoot is the repo root and gate B resolves exactly like
a real run -- otherwise the exit code would mean something else than "A found nothing".

The PRE-FIX anchor is a pinned commit (23c1379 = the commit before the U-9 fix), never HEAD: after the
fix is committed, HEAD *is* the fixed version and the two "old gate" arms would silently invert.

Run:  python tools/probes/U9_vfp_reg_comment_probe.py
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCR = os.path.join(ROOT, "build", "u9_probe")
PRE_FIX_SHA = "23c1379"          # doc_check.ps1 before the U-9 fix -- immutable coordinate
NEW_PS = os.path.join(ROOT, "tools", "doc_check.ps1")
LEDGER = "docs/对标差距推进计划.md"


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)
    return path


def main():
    os.makedirs(SCR, exist_ok=True)
    raw = read_bytes(os.path.join(ROOT, "src", "NodeRegistry.cpp")).decode("utf-8")
    sep = "\r\n" if "\r\n" in raw else "\n"
    lines = raw.split(sep)

    def live_idx(tok):
        idx = [i for i, l in enumerate(lines) if re.match(r"^\s*VFP_REG\(\s*" + tok + r"\s*,", l)]
        assert len(idx) == 1, ("expected exactly one live registration", tok, idx)
        return idx[0]

    def comment_out(tok):
        out = list(lines)
        i = live_idx(tok)
        out[i] = re.sub(r"^(\s*)", r"\1// ", out[i])
        return sep.join(out).encode("utf-8")

    def two_commented(a, b):
        out = list(lines)
        for tok in (a, b):
            i = live_idx(tok)
            out[i] = re.sub(r"^(\s*)", r"\1// ", out[i])
        return sep.join(out).encode("utf-8")

    def uncomment_cropnode():
        out = list(lines)
        idx = [i for i, l in enumerate(out) if re.match(r"^\s*//\s*VFP_REG\(\s*CropNode\s*,", l)]
        assert len(idx) == 1, idx
        out[idx[0]] = re.sub(r"^(\s*)// ", r"\1", out[idx[0]])
        return sep.join(out).encode("utf-8")

    def trailing_note(tok):
        out = list(lines)
        i = live_idx(tok)
        out[i] = out[i] + "   // trailing note, the registration stays live"
        return sep.join(out).encode("utf-8")

    write(os.path.join(SCR, "reg_b1_cropnode_commented.cpp"), comment_out("OpencvCropNode"))
    write(os.path.join(SCR, "reg_b2_cropnode_live.cpp"), uncomment_cropnode())
    write(os.path.join(SCR, "reg_b4_trailing.cpp"), trailing_note("OpencvCropNode"))
    write(os.path.join(SCR, "reg_b5_two.cpp"), two_commented("DelayNode", "OpencvBlobNode"))

    old = subprocess.run(["git", "show", PRE_FIX_SHA + ":tools/doc_check.ps1"],
                         cwd=ROOT, capture_output=True)
    assert old.returncode == 0 and old.stdout[:3] == b"\xef\xbb\xbf", old.returncode
    OLD_PS = write(os.path.join(ROOT, "build", "doc_check_pre_fix_u9.ps1"), old.stdout)

    fixed = read_bytes(NEW_PS).decode("utf-8-sig")
    strip_line = "$registryLive = (($registryText -split \"`n\") | Where-Object { $_ -notmatch '^\\s*//' }) -join \"`n\""
    assert strip_line in fixed, "the comment-stripping line is no longer verbatim -- update this probe"
    nostrip = fixed.replace(strip_line, "$registryLive = $registryText   # NEUTRALISED for B3")
    NOSTRIP_PS = write(os.path.join(ROOT, "build", "doc_check_nostrip_u9.ps1"),
                       b"\xef\xbb\xbf" + nostrip.encode("utf-8"))

    old_ledger = subprocess.run(["git", "show", PRE_FIX_SHA + ":" + LEDGER], cwd=ROOT, capture_output=True)
    assert old_ledger.returncode == 0, old_ledger.returncode
    docs_old = os.path.join(SCR, "docs_head")
    if not os.path.isdir(docs_old):
        shutil.copytree(os.path.join(ROOT, "docs"), docs_old)
    write(os.path.join(docs_old, os.path.basename(LEDGER)), old_ledger.stdout)

    def run(script, docsdir, registry, note):
        args = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
                "-DocsDir", docsdir, "-Registry", registry]
        p = subprocess.run(args, cwd=ROOT, capture_output=True)
        txt = p.stdout.decode("gbk", "replace") + p.stderr.decode("gbk", "replace")
        a = re.search(r"claims checked\s*:\s*(\d+)", txt)
        fnd = re.findall(r"no VFP_REG for (\w+)", txt)
        print("%-56s rc=%s A=%-4s findings=%d %s" % (
            note, p.returncode, a.group(1) if a else "?", len(fnd),
            dict((t, fnd.count(t)) for t in sorted(set(fnd)))))
        return p.returncode, len(fnd), fnd

    rel = lambda name: "build/u9_probe/" + name          # noqa: E731  (paths are repo-root relative)

    rc_c, _, _ = run(NEW_PS, "docs", "src/NodeRegistry.cpp", "control  new gate + real registry + real docs")
    rc_b1, n_b1, _ = run(NEW_PS, "docs", rel("reg_b1_cropnode_commented.cpp"),
                         "B1     new gate, OpencvCropNode registration commented out")
    rc_b1o, n_b1o, _ = run(OLD_PS, "docs", rel("reg_b1_cropnode_commented.cpp"),
                           "B1o    PRE-FIX gate, same broken registry")
    rc_b2a, n_b2a, f_b2a = run(NEW_PS, rel("docs_head"), "src/NodeRegistry.cpp",
                               "B2a    new gate + HEAD-snapshot ledger (claims CropNode)")
    rc_b2b, n_b2b, _ = run(OLD_PS, rel("docs_head"), "src/NodeRegistry.cpp",
                           "B2b    PRE-FIX gate + same docs  (= U-9 itself)")
    rc_b2c, n_b2c, _ = run(NEW_PS, rel("docs_head"), rel("reg_b2_cropnode_live.cpp"),
                           "B2c    new gate + only that // marker taken off")
    rc_b3, n_b3, _ = run(NOSTRIP_PS, "docs", rel("reg_b1_cropnode_commented.cpp"),
                         "B3     strip step neutralised + B1 registry")
    rc_b4, n_b4, _ = run(NEW_PS, "docs", rel("reg_b4_trailing.cpp"),
                         "B4     trailing comment on a live line must survive")
    rc_b5, n_b5, f_b5 = run(NEW_PS, "docs", rel("reg_b5_two.cpp"),
                            "B5     two live registrations commented (claims 4 + 3)")

    checks = [
        ("control green", rc_c == 0),
        ("B1 red with exactly 1 finding", rc_b1 == 1 and n_b1 == 1),
        ("B1o stays green (old blind spot)", rc_b1o == 0 and n_b1o == 0),
        ("B2a red on CropNode", rc_b2a == 1 and f_b2a == ["CropNode"]),
        ("B2b stays green (fix, not docs, is decisive)", rc_b2b == 0 and n_b2b == 0),
        ("B2c green again once the marker is gone", rc_b2c == 0 and n_b2c == 0),
        ("B3 green once the strip step is removed", rc_b3 == 0 and n_b3 == 0),
        ("B4 no false red from a trailing comment", rc_b4 == 0 and n_b4 == 0),
        ("B5 findings == per-claim counts (4 + 3)", rc_b5 == 1 and n_b5 == 7
         and f_b5.count("DelayNode") == 4 and f_b5.count("OpencvBlobNode") == 3),
    ]
    print("\nassertions:")
    bad = 0
    for name, passed in checks:
        print("   %-52s %s" % (name, "OK" if passed else "VIOLATED"))
        bad += 0 if passed else 1
    print("matrix_rc=%d" % (1 if bad else 0))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())

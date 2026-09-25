#!/usr/bin/env python3
"""Count the "silent green light" shapes that HalconNode's success contract has to cover.

Why this exists: docs/VisionFlowPlatform SRS section 3.21 (records P0-1 and S-1) quoted
numbers from an ad-hoc scan whose definition was never written down. Two reviewers then
reproduced different counts (44 / 12 / 41 vs 44 / 22 / 13) -- all "correct" for their own
regex. This script is that definition, so every number in the docs has one reproducible
source:

    python tools/scan_silent_green_paths.py [--files]

Definitions (scope: src/*.cpp):

  P1  clear-then-return  `m_outputImage.Clear();` immediately followed by `return`
                         (comment-only lines in between are allowed).
  P2  catch-clear-only   a `catch (...) { ... }` body that clears m_outputImage and does
                         NOT write moduleStatus inside the same body.
  N   union              files matching P1 or P2.
  NOSTATUS               union files that never assign `moduleStatus = false` anywhere,
                         i.e. every failure path in them is invisible to the contract.

P1 and P2 are different sets that happen to have the same size -- the overlap is printed
so nobody reads "44 files" twice as "88 places in 44 files".

Informational only: exit code is always 0. Gating CI on a source-shape count would just
freeze the numbers instead of fixing the code.
"""
import glob
import io
import os
import re
import sys

CLEAR_RETURN = re.compile(
    r"m_outputImage\s*\.\s*Clear\s*\(\s*\)\s*;\s*(?:(?://[^\n]*\n)\s*)*return\s*[;0-9a-zA-Z_(]"
)
CLEARED_INSIDE = re.compile(r"m_outputImage[^;]*\.\s*Clear\s*\(")
WRITES_FALSE = re.compile(r"moduleStatus[^=\n]*=\s*false")
CATCH_HEAD = re.compile(r"catch\s*\([^{]*\)\s*\{")


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def catch_bodies(text):
    """Yield the inner text of every catch block (brace matched, no nesting tricks)."""
    for match in CATCH_HEAD.finditer(text):
        i = match.end()
        depth = 1
        j = i
        while j < len(text) and depth:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
            j += 1
        yield text[i:j - 1]


def scan(root):
    p1, p2 = {}, {}
    for path in sorted(glob.glob(os.path.join(root, "src", "*.cpp"))):
        with io.open(path, "r", encoding="utf-8", errors="replace") as handle:
            text = handle.read()
        n1 = len(CLEAR_RETURN.findall(text))
        if n1:
            p1[path] = n1
        n2 = sum(1 for body in catch_bodies(text)
                 if CLEARED_INSIDE.search(body) and "moduleStatus" not in body)
        if n2:
            p2[path] = n2
    return p1, p2


def main():
    root = repo_root()
    p1, p2 = scan(root)
    show_files = "--files" in sys.argv[1:]
    union = sorted(set(p1) | set(p2))
    overlap = sorted(set(p1) & set(p2))
    no_status = [p for p in union
                 if not WRITES_FALSE.search(io.open(p, encoding="utf-8",
                                                    errors="replace").read())]

    def line(label, value, extra=""):
        print("%-34s %4d %s" % (label, value, extra))

    line("P1 files (clear-then-return)", len(p1), "occurrences=%d" % sum(p1.values()))
    line("P2 files (catch-clear-only)", len(p2), "occurrences=%d" % sum(p2.values()))
    line("P1 and P2 overlap", len(overlap))
    line("union files (P1 or P2)", len(union))
    line("union w/o moduleStatus=false", len(no_status))

    if show_files:
        print("\n-- union without any explicit failure write --")
        for path in no_status:
            print("  %s" % os.path.relpath(path, root).replace("\\", "/"))


if __name__ == "__main__":
    main()
    sys.exit(0)

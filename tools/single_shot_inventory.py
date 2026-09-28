"""Executable definition of the "QTimer::singleShot without a context object" inventory.

Why this exists (U-20): `QTimer::singleShot(msec, functor)` is not tied to any receiver, so the
functor still runs after the widget/node it captured has been destroyed -- U-20 measured that as a
segmentation fault (RC=139) once a swept panel was deleted and the event loop spun again. The
three-argument overload `singleShot(msec, context, functor)` cancels the timer when `context` dies,
which is the shape every other call site in this repo already uses.

Classification rule (mechanical, no semantic guessing): the second argument of the call is compared
against the lambda introducer. If it starts with '[' the call has **no** context object. Everything
else (`this`, a widget pointer, `&a`) is treated as a context object. A `SLOT(...)` string second
argument would be a false negative for that rule, so the script also prints every accepted site for
eyeball verification.

Second inventory: which registered node classes reach `HalconNode::createParamPanel()`'s hand-written
body at all. That function returns early via `createAutoParamPanel()` when `m_paramSpecs` is not empty,
so the dangling timer at HalconNode.cpp:642 is only scheduled for operators that declare no ParamSpec.

Reproduce:
    python tools/single_shot_inventory.py
Exit codes: 0 = zero sites without a context object;
            1 = at least one such site remains (the U-20 fix is incomplete or has regressed).
Output is ASCII-only on purpose (the console code page is not guaranteed UTF-8).
"""

import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "src")

CALL_RE = re.compile(r"QTimer::singleShot\s*\(")
CLASS_NAME_RE = re.compile(r"VFP_REG\s*\(\s*([A-Za-z][A-Za-z0-9_]*Node)\b")


def top_level_args(text):
    """Arguments of a call whose text starts at its opening '(' (paren balanced, ignores nesting)."""
    depth = 0
    args = []
    cur = []
    for ch in text:
        if ch == "(":
            depth += 1
            if depth == 1:
                continue
        elif ch == ")":
            depth -= 1
            if depth == 0:
                break
        if depth == 1 and ch == ",":
            args.append("".join(cur).strip())
            cur = []
            continue
        cur.append(ch)
    if cur:
        args.append("".join(cur).strip())
    return args


def scan_single_shot():
    sites = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith((".cpp", ".h")):
            continue
        lines = io.open(os.path.join(SRC, name), encoding="utf-8", errors="replace").read().split("\n")
        for i, line in enumerate(lines):
            for m in CALL_RE.finditer(line):
                args = top_level_args(line[m.end() - 1:])
                second = args[1] if len(args) > 1 else ""
                no_context = second.startswith("[")
                sites.append((name, i + 1, len(args), second[:26], no_context))
    return sites


def live_registry_classes():
    lines = io.open(os.path.join(SRC, "NodeRegistry.cpp"), encoding="utf-8", errors="replace").read().split("\n")
    # Same rule as doc_check.ps1 (U-9): a commented-out VFP_REG line is not a registration.
    live = "\n".join(l for l in lines if not l.strip().startswith("//"))
    return sorted(set(CLASS_NAME_RE.findall(live)))


def classes_overriding_panel():
    defined = set()
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".cpp"):
            continue
        txt = io.open(os.path.join(SRC, name), encoding="utf-8", errors="replace").read()
        for cls in live_registry_classes():
            if re.search(r"\b%s::createParamPanel\s*\(" % cls, txt):
                defined.add(cls)
    return defined


def parent_of(cls):
    for root in ("include", "src"):
        p = os.path.join(REPO_ROOT, root, cls + ".h")
        if os.path.exists(p):
            txt = io.open(p, encoding="utf-8", errors="replace").read()
            m = re.search(r"class\s+\w*\s*%s\s*:\s*public\s+([A-Za-z0-9_]+)" % cls, txt)
            return m.group(1) if m else "?"
    return "?"


def main():
    sites = scan_single_shot()
    no_ctx = [s for s in sites if s[4]]
    print("SINGLESHOT total=%d with_context=%d no_context=%d" % (len(sites), len(sites) - len(no_ctx), len(no_ctx)))
    for name, ln, argc, second, is_no_ctx in sites:
        print("  %-6s %s:%d argc=%d second=%r" % ("NOCTX" if is_no_ctx else "CTX", name, ln, argc, second))

    classes = live_registry_classes()
    defined = classes_overriding_panel()
    base_only = [c for c in classes if c not in defined]
    non_halcon = [c for c in base_only if parent_of(c) != "HalconNode"]
    print("REGISTRY live=%d override_createParamPanel=%d base_halcon_only=%d (parent!=HalconNode: %d)"
          % (len(classes), len(defined), len(base_only) - len(non_halcon), len(non_halcon)))
    print("  BASE_ONLY %s" % ",".join(sorted(set(base_only) - set(non_halcon))))

    if no_ctx:
        print("FAIL: %d singleShot site(s) still lack a context object" % len(no_ctx))
        return 1
    print("OK: every singleShot call site is bound to a context object")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())

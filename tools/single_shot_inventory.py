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

The call text is gathered by balancing parentheses **forward from the '(' across lines** (W-1). A
one-line-only read was the bug: `singleShot(` at the end of a line yields no second argument, and
`"".startswith("[")` is false, so a genuinely dangling timer was classified as having a context --
the gate went green on exactly the shape that crashes. So a site is never defaulted to "has a
context object": when the parentheses do not balance before end of file, or fewer than two
arguments can be read, the site is reported as UNRES and judged red, which is the fail-closed side.
String literals and comments are not tokenized (same limitation as before); a stray unbalanced paren
inside them can only produce an UNRES, i.e. a red the reviewer then eyeballs.

Second inventory: which registered node classes reach `HalconNode::createParamPanel()`'s hand-written
body at all. That function returns early via `createAutoParamPanel()` when `m_paramSpecs` is not empty,
so the dangling timer at HalconNode.cpp:645 is only scheduled for operators that declare no ParamSpec.

Reproduce:
    python tools/single_shot_inventory.py
Exit codes: 0 = zero sites without a context object and zero unresolved sites;
            1 = at least one site lacks a context object, or at least one site could not be
              classified (unbalanced parentheses / fewer than two arguments) -- fail closed.
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
OPEN = "(["
CLOSE = ")]}"


def top_level_args(text):
    """Arguments of a call whose text starts at its opening '('.

    Parentheses, brackets and braces share one depth counter, so a comma inside a lambda capture
    list (`[this, w]`) or inside a lambda body is not an argument separator. Counting only
    parentheses made an argument count depend on how the source was wrapped (W-1).
    """
    depth = 0
    args = []
    cur = []
    for ch in text:
        if ch in OPEN:
            depth += 1
            if depth == 1:
                continue
        elif ch in CLOSE:
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


def preview(arg):
    """How an argument is printed: same text whether the source put the call on one line or ten.

    Whitespace runs collapse, and anything past the start of an argument body is dropped -- the
    classifier reads the raw argument, the preview only needs the introducer (`[=]() {`).
    """
    flat = " ".join(arg.split())
    cut = flat.find("{")
    if cut >= 0:
        flat = flat[:cut + 1]
    return flat[:26]


def call_text_from(lines, index, col):
    """Text of the call whose '(' is at lines[index][col], balanced forward across lines.

    Returns (text, balanced). ``balanced`` is False when the file ends before the call closes --
    the caller must treat that as "unknown", never as "has a context object".
    """
    depth = 0
    out = []
    for k in range(index, len(lines)):
        for ch in (lines[k][col:] if k == index else lines[k]):
            if ch in OPEN:
                depth += 1
            elif ch in CLOSE:
                depth -= 1
                if depth == 0:
                    out.append(ch)
                    return "".join(out), True
            out.append(ch)
    return "".join(out), False


def scan_single_shot():
    """One record per singleShot call: (file, line, argc, second_arg, no_context, unresolved)."""
    sites = []
    for name in sorted(os.listdir(SRC)):
        if not name.endswith((".cpp", ".h")):
            continue
        text = io.open(os.path.join(SRC, name), encoding="utf-8", errors="replace").read()
        lines = text.split("\n")
        # Match over the whole file, not line by line: '\s*' in CALL_RE can then span the newline of
        # a "singleShot\n(" wrap, which a per-line scan silently would not see at all (W-1).
        for m in CALL_RE.finditer(text):
            open_at = m.end() - 1
            line_no = text.count("\n", 0, m.start()) + 1
            paren_line = text.count("\n", 0, open_at)
            call_text, balanced = call_text_from(lines, paren_line,
                                                 open_at - (text.rfind("\n", 0, open_at) + 1))
            args = top_level_args(call_text) if balanced else []
            if not balanced or len(args) < 2:
                # Fail closed: an unreadable second argument is reported, not assumed safe.
                sites.append((name, line_no, len(args), "", False, True))
                continue
            second = args[1]
            sites.append((name, line_no, len(args), preview(second), second.startswith("["), False))
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
    unresolved = [s for s in sites if s[5]]
    print("SINGLESHOT total=%d with_context=%d no_context=%d"
          % (len(sites), len(sites) - len(no_ctx) - len(unresolved), len(no_ctx)))
    if unresolved:
        print("SINGLESHOT-UNRESOLVED k=%d" % len(unresolved))
    for name, ln, argc, second, is_no_ctx, is_unresolved in sites:
        label = "UNRES" if is_unresolved else ("NOCTX" if is_no_ctx else "CTX")
        print("  %-6s %s:%d argc=%d second=%r" % (label, name, ln, argc, second))

    classes = live_registry_classes()
    defined = classes_overriding_panel()
    base_only = [c for c in classes if c not in defined]
    non_halcon = [c for c in base_only if parent_of(c) != "HalconNode"]
    print("REGISTRY live=%d override_createParamPanel=%d base_halcon_only=%d (parent!=HalconNode: %d)"
          % (len(classes), len(defined), len(base_only) - len(non_halcon), len(non_halcon)))
    print("  BASE_ONLY %s" % ",".join(sorted(set(base_only) - set(non_halcon))))

    if no_ctx:
        print("FAIL: %d singleShot site(s) still lack a context object" % len(no_ctx))
    if unresolved:
        print("FAIL: %d singleShot site(s) could not be classified "
              "(parentheses unbalanced by EOF or fewer than 2 arguments)" % len(unresolved))
    if no_ctx or unresolved:
        return 1
    print("OK: every singleShot call site is bound to a context object")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())

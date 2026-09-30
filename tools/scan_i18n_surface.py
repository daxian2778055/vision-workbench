#!/usr/bin/env python3
"""Measure how much Chinese UI text still bypasses the tr() -> .ts -> .qm pipeline.

Why this exists: docs/对标差距推进计划.md candidate row C3 quotes "MainWindow 单文件 70+ 处"
-- a read-off estimate with no executable definition, and the whole-repo surface was never
counted at all. Every number the ledger now quotes for C3 comes from this script:

    python tools/scan_i18n_surface.py [--files] [--top N] [--samples N]

Definitions (scope: src/*.cpp, src/*.h, include/*.h; ui/*.ui counted separately):

  literal   a C/C++ string literal ("...") that contains at least one CJK code point
            (U+4E00..U+9FFF). Comments (line and block) are stripped first, so Chinese in
            comments never counts. Raw string bodies (R"(...)") are masked out and measured
            on their own lines: lupdate cannot extract a phrase from inside a raw body, so
            that text is a blind spot no bucket below counts.
  wrapped   some enclosing call in the parenthesis stack is named tr / translate, i.e.
            lupdate can extract it. Determined with a parenthesis stack, so a multi-line
            tr(  "..."  ) counts as wrapped.
  bare      a literal with no wrapped call around it. This is the C3 backlog.
  key       what a bare literal sits in: the NEAREST enclosing call name, skipping
            STRING_CALLS -- QStringLiteral & friends only build the string and would
            otherwise swallow 80% of the sites (setText(QStringLiteral("...")) must read
            as setText, not as QStringLiteral). With no enclosing call, the current
            statement decides: assign:<receiver> (`err = "..."`, `s += "..."`, incl. `=`
            inside a ternary), return (`case X: return QStringLiteral("...")`),
            stream:<lhs> (`VFP_DEBUG << "..."`), else (no target).
  tier      what kind of text that key holds -- see TIERS_BY_KEY below. This is the split
            the ledger quotes: widget_text (Qt setters/ctors/dialogs) and param_label
            (the label/tooltip argument of make*Param; the serialization key is the ASCII
            `name` field) are tr()-safe. data_identity (node display names, registry
            aliases, VFP_REG arguments, page names, and literals that sit in a comparison)
            must NOT be wrapped blind. runtime_message
            and export_content need a decision per text, not_ui_text is diagnostic or
            compile-time only, and unresolved needs a human read.
  bare_ui / bare_log  same counts grouped by UI_CALLS / LOG_CALLS below. Call-name based,
            so bare_ui is a candidate list, not a runtime proof the text reaches a screen.
  escaped_cjk  a CJK code point written as an escape (\\uXXXX) instead of as a character.
            Invisible to `literal` above -- that rule looks at code points -- yet a C++
            compiler decodes \\uXXXX into the same text, so it reaches the screen while
            lupdate sees only ASCII and cannot extract it. Counted on the comment-stripped,
            raw-masked text: inside a raw body a backslash-u stays literal text, so it is
            not an escape and does not count here. This is a blind-spot measure, not a
            bucket: it says the `bare` total is a lower bound.

ui/*.ui strings are excluded from every bucket: uic emits them inside retranslateUi()
wrapped in tr(), so they are already extractable. Their count is printed for context only.

Informational only: exit code is always 0. Gating CI on a literal count would just freeze
the numbers instead of translating them.
"""
import argparse
import bisect
import collections
import glob
import io
import os
import re
import sys
import xml.etree.ElementTree as ET

CJK = re.compile(r"[\u4e00-\u9fff]")
# Chinese written as an escape: "\u89d2" is 角 to the compiler but ASCII to lupdate.
ESCAPED_HEX = re.compile(r"\\u([0-9A-Fa-f]{4})")
IDENT_TAIL = re.compile(r"([A-Za-z_~][\w:~]*)\s*$")
# Statement heads that tell us who receives a bare literal (see target_key).
ASSIGN_HEAD = re.compile(r"^\s*(?:const\s+)?[\w:<>,\s\*&\[\]\.\(\)\?\+\|!-]*?"
                         r"([A-Za-z_]\w*(?:->\w+)?(?:\[[^\]\n]*\])?)\s*[-+*/]?=(?!=)")
CASE_RETURN_HEAD = re.compile(r"^\s*(?:(?:case\b[^;{}]*?|default)\s*:\s*)?\breturn\b"
                              r"\s*(?:[A-Za-z_]\w*\s*\(\s*)?$")
# `VFP_DEBUG << "..."` / `os << "..."` -- no call, no assignment: a stream receives it.
STREAM_HEAD = re.compile(r"^\s*([A-Za-z_][\w:]*)\s*<<")
# Streams that are diagnostics, i.e. text nobody on screen has to read.
DEBUG_STREAMS = {"VFP_DEBUG", "qDebug", "qInfo", "qWarning", "qCritical", "cerr", "wcerr"}

# Calls that only build a string: they hide the real destination, so the key skips them.
STRING_CALLS = {
    "QStringLiteral", "QLatin1String", "QLatin1Char", "QString", "QStringView",
    "fromUtf8", "fromLocal8Bit", "fromStdString", "fromWCharArray", "toUtf8",
    "number", "arg", "argView",
}

# Nearest-call names that put text on a widget (Qt setters, dialogs, widget ctors).
UI_CALLS = {
    "setText", "setPlainText", "setWindowTitle", "setPlaceholderText", "setToolTip",
    "setStatusTip", "setWhatsThis", "setAccessibleName", "setAccessibleDescription",
    "setLabelText", "setTabText", "addItem", "addItems", "insertItem", "setItemText",
    "setHorizontalHeaderLabels", "setVerticalHeaderLabels",
    "setHeaderData", "setStringList", "setButtonText", "addButton", "setMessage",
    "setInformativeText", "setDetailedText", "setTitle", "setLabel", "append",
    "appendRow", "setHeaderLabelText", "setPrefix", "setSuffix", "setDescription",
    "setErrorMessage", "setStatus", "setName", "setDisplayName", "setLabelFor",
    "information", "warning", "critical", "question", "setMenuText", "setTextData",
    "QLabel", "QPushButton", "QCheckBox", "QRadioButton", "QGroupBox",
    "getSaveFileName", "getOpenFileName", "getExistingDirectory",
    "addAction", "addRow", "addMenu", "addTab", "setHeaderLabels", "QTableWidgetItem",
    "QDockWidget", "QMenu", "QAction", "getMultiLineText", "setTitleText",
    "setSpecialValueText", "box", "about",
}

# Nearest-call names that write to a log / audit sink rather than to a widget.
LOG_CALLS = {
    "qDebug", "qInfo", "qWarning", "qCritical", "qFatal", "trace", "log", "audit",
    "appendLog", "addLog", "writeLog", "pushLog", "vfpLog", "logEvent", "logAudit",
    "logMessage", "recordEvent",
}

# Which kind of text a key holds; "unresolved" is what this cannot read.
#
# data_identity is the reason C3 is not a find-and-wrap job: these strings are stored in
# the .vfp scheme and looked up again on load -- src/NodeFactory.cpp:102 feeds the saved
# name to createByName(), which matches reg.displayName/englishName/aliases
# (src/NodeRegistry.cpp:527-537), and src/FlowSnippet.cpp persists node names too.
# Wrap them in tr() and an English-locale run stops loading schemes built in Chinese.
# Three keys used to sit here and were moved out after their consumers were read:
# addInputPort/addOutputPort (ProjectManager.cpp:387/397 writes port names into the scheme
# and nothing anywhere reads "inputPorts"/"outputPorts" back -> write-only, so unresolved
# rather than a proven key), setDescription (reg->description only reaches a tooltip and
# the help page) and assign:valueName (MeasureResult.valueName is rendered by
# ImageDisplayController.cpp:126 and never serialized).
TIERS_BY_KEY = {
    "setName": "data_identity",
    "addInputPort": "unresolved",
    "addOutputPort": "unresolved",
    "addAliases": "data_identity",
    "VFP_REG": "data_identity",
    "setDescription": "widget_text",
    "if": "data_identity",
    "contains": "data_identity",
    "startsWith": "data_identity",
    "assign:valueName": "runtime_message",
    "assign:pageName": "data_identity",
    "fail": "runtime_message",
    "failWith": "runtime_message",
    "failDetail": "runtime_message",
    "setData": "runtime_message",
    "communicationError": "runtime_message",
    "executionError": "runtime_message",
    "connectionRejected": "runtime_message",
    "recordNodeSkipped": "runtime_message",
    "logMessage": "runtime_message",
    "logOperation": "runtime_message",
    "audit": "runtime_message",
    "showMessage": "runtime_message",
    "static_assert": "not_ui_text",
    "csvLine": "export_content",
    "esc": "export_content",
    "drawText": "export_content",
    "QBarSet": "export_content",
    "makeStatusLabel": "widget_text",
    "addForm": "widget_text",
    "addMenu": "widget_text",
    "addTab": "widget_text",
    "addComment": "widget_text",
    "NodeGroupItem": "widget_text",
    "runWithBusyFeedback": "widget_text",
    "MessageBoxW": "widget_text",
    "getColor": "widget_text",
    "setImage": "widget_text",
    "setSourceInfo": "widget_text",
    "makeIntParam": "param_label",
    "makeDoubleParam": "param_label",
    "makeBoolParam": "param_label",
    "makeStringParam": "param_label",
    "makeFilePathParam": "param_label",
    "makeEnumParam": "param_label",
}

# assign:<receiver> tiers by the receiver's name; anything else stays unresolved.
MESSAGE_RECEIVERS = {
    "error", "errout", "m_lasterror", "lasterror", "msg", "message", "reason", "why",
    "hint", "detail", "status", "info", "desc", "description", "text", "m_text",
    "title", "m_title", "label", "tagline", "modename", "statustext",
}


def tier_of(key):
    """Which kind of text this key holds (see TIERS_BY_KEY above)."""
    if key in TIERS_BY_KEY:
        return TIERS_BY_KEY[key]
    if key in UI_CALLS:
        return "widget_text"
    if key.startswith("stream:"):
        return "not_ui_text" if key[len("stream:"):] in DEBUG_STREAMS else "unresolved"
    if key.startswith("assign:"):
        receiver = key[len("assign:"):].lower()
        if receiver in MESSAGE_RECEIVERS:
            return "runtime_message"
        if receiver.startswith("m_params"):
            return "runtime_message"
        return "unresolved"
    return "unresolved"


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def strip_comments(text):
    """Blank out comments and raw-string bodies; keep offsets and line numbering.

    Returns (cleaned_text, raw_bodies). Ordinary string literals pass through
    byte-for-byte; raw bodies become spaces (newlines preserved) so their content is
    counted by the caller as a blind spot instead of being misread as ordinary literals.
    """
    out = []
    raws = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            out.append(text[i:j + 1])
            i = j + 1
            continue
        if ch == "/" and text[i:i + 2] == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
            continue
        if ch == "/" and text[i:i + 2] == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            seg = text[i:j]
            out.append("".join(c if c == "\n" else " " for c in seg))
            i = j
            continue
        if ch == "R" and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] in "_:")) \
                and i + 1 < n and text[i + 1] == '"':
            k = i + 2
            while k < n and (text[k].isalnum() or text[k] == "_"):
                k += 1
            delim = text[i + 2:k]
            if k < n and text[k] == "(":
                closer = ")%s\"" % delim
                j = text.find(closer, k + 1)
                if j > 0:
                    raws.append(text[k + 1:j])
                    seg = text[i:j + len(closer)]
                    out.append("".join(c if c == "\n" else " " for c in seg))
                    i = j + len(closer)
                    continue
        out.append(ch)
        i += 1
    return "".join(out), raws


def nearest_key(stack):
    """Nearest enclosing call that actually says where the text goes."""
    for name in reversed(stack):
        if name and name not in STRING_CALLS:
            return name
    return "(no call)"


def target_key(text, start):
    """Key for a literal no call encloses: what receives the value.

    Reads the whole current statement, so `err = cond ? "A" : cond` and
    `case X: return QStringLiteral("A")` key correctly; a literal in a braced list
    or an argument position has no receiver this can name and stays "(no target)".
    """
    prefix = text[max(0, start - 200):start]
    cut = max(prefix.rfind(";"), prefix.rfind("{"), prefix.rfind("}"))
    seg = prefix[cut + 1:]
    m = ASSIGN_HEAD.match(seg)
    if m:
        ident = m.group(1)
        if "[" in ident:
            ident = ident.split("[")[0] + "[...]"
        return "assign:%s" % ident
    m = CASE_RETURN_HEAD.match(seg)
    if m:
        return "return"
    m = STREAM_HEAD.match(seg)
    if m:
        return "stream:%s" % m.group(1)
    return "(no target)"


ESCAPE_MAP = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "'": "'", "\\": "\\"}
ANY_ESCAPE = re.compile(r"\\u([0-9A-Fa-f]{4})|\\([nrt\"'\\])")


def decode_escapes(body):
    """The text a literal actually carries, with \\uXXXX and the ordinary escapes decoded.

    The `literal` rule keys on code points, so an escaped 「\\u89d2」 is not a literal here --
    but the compiler turns it into that character all the same. Any rule that compares two
    literals *as text* has to decode first, or the escaped and the direct spelling of one
    sentence read as two different sentences.
    """
    def one(m):
        if m.group(1):
            return chr(int(m.group(1), 16))
        return ESCAPE_MAP[m.group(2)]
    return ANY_ESCAPE.sub(one, body)


Literal = collections.namedtuple("Literal", "line raw decoded key tier wrapped")


def iter_literals(text):
    """Every string literal in already comment-stripped / raw-masked source text.

    One tokenizer for two consumers (U-35): scan_file() derives the wrapped/bare buckets this
    file measures, tools/dup_cn_literal_gate.py derives the duplicated-sentence baseline. A
    second copy of this walk would be the thing the gate exists to stop.

    Yields Literal(line, raw, decoded, key, tier, wrapped) for **every** literal, CJK or not:
    which subset counts as Chinese text is the caller's rule, not the tokenizer's. `key`/`tier`
    follow the definitions at the top of this file.
    """
    newl = [m.start() for m in re.finditer("\n", text)]
    stack = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "(":
            m = IDENT_TAIL.search(text[max(0, i - 60):i])
            stack.append(m.group(1).split("::")[-1] if m else "")
            i += 1
            continue
        if ch == ")":
            if stack:
                stack.pop()
            i += 1
            continue
        if ch == '"':
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            raw = text[i + 1:j]
            key = nearest_key(stack)
            if key == "(no call)":
                key = target_key(text, i)
            yield Literal(bisect.bisect_left(newl, i) + 1, raw, decode_escapes(raw),
                          key, tier_of(key),
                          any(s in ("tr", "translate") for s in stack))
            i = j + 1
            continue
        i += 1


def scan_file(path):
    """Return (wrapped_count, [(key, line_no, preview), ...] of bare literals,
    raw_string_count, raw_cjk_lines, escaped_cjk_count)."""
    cleaned, raws = strip_comments(
        io.open(path, "r", encoding="utf-8", errors="replace").read())
    text = cleaned
    raw_cjk_lines = sum(1 for body in raws for ln in body.split("\n") if CJK.search(ln))
    # Chinese carried as an escape is invisible to the literal rule above but is real text
    # on screen, so it is reported as a blind spot (see the escaped_cjk definition).
    escaped_cjk = sum(1 for m in ESCAPED_HEX.finditer(text)
                      if 0x4E00 <= int(m.group(1), 16) <= 0x9FFF)
    wrapped = 0
    bare = []
    for lit in iter_literals(text):
        if not CJK.search(lit.raw):
            continue
        if lit.wrapped:
            wrapped += 1
        else:
            bare.append((lit.key, lit.line, lit.raw[:24]))
    return wrapped, bare, len(raws), raw_cjk_lines, escaped_cjk


def count_ui_strings(root):
    total = 0
    for path in sorted(glob.glob(os.path.join(root, "ui", "*.ui"))):
        try:
            tree = ET.parse(path)
        except ET.ParseError:
            continue
        for node in tree.iter():
            if node.tag in ("string",) and node.text and CJK.search(node.text):
                total += 1
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--files", action="store_true", help="per-file breakdown")
    ap.add_argument("--top", type=int, default=25, help="how many keys to list")
    ap.add_argument("--samples", type=int, default=0, help="sites printed per listed key")
    args = ap.parse_args()
    # Previews are Chinese; a cp936 console mangles them without this.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    root = repo_root()
    paths = sorted(glob.glob(os.path.join(root, "src", "*.cpp"))
                   + glob.glob(os.path.join(root, "src", "*.h"))
                   + glob.glob(os.path.join(root, "include", "*.h")))
    per_file = {path: scan_file(path) for path in paths}

    wrapped_total = sum(w for w, _, _, _, _ in per_file.values())
    bare_total = sum(len(b) for _, b, _, _, _ in per_file.values())
    raw_total = sum(r for _, _, r, _, _ in per_file.values())
    raw_cjk_total = sum(c for _, _, _, c, _ in per_file.values())
    escaped_total = sum(e for _, _, _, _, e in per_file.values())
    escaped_files = sum(1 for _, _, _, _, e in per_file.values() if e)
    by_key = collections.Counter()
    sites = collections.defaultdict(list)
    for path, (_, bare, _, _, _) in per_file.items():
        for key, line_no, preview in bare:
            by_key[key] += 1
            sites[key].append((path, line_no, preview))
    bare_ui = sum(v for k, v in by_key.items() if k in UI_CALLS)
    bare_log = sum(v for k, v in by_key.items() if k in LOG_CALLS)
    bare_other = bare_total - bare_ui - bare_log
    by_tier = collections.Counter()
    for k, v in by_key.items():
        by_tier[tier_of(k)] += v

    def line(label, value, extra=""):
        print("%-38s %5d %s" % (label, value, extra))

    line("scanned files", len(paths))
    line("CJK literals wrapped (tr/translate)", wrapped_total)
    line("CJK literals bare", bare_total)
    line("  bare, key in UI_CALLS (candidate)", bare_ui)
    line("  bare, key in LOG_CALLS", bare_log)
    line("  bare, key other/unclassified", bare_other)
    line('raw strings R"(...) in scope', raw_total, "(masked, not classified)")
    line("  CJK lines inside raw bodies", raw_cjk_total, "(unextractable by lupdate)")
    line("escaped CJK \\uXXXX (blind spot)", escaped_total,
         "in %d file(s), not counted as literals above" % escaped_files)
    line("ui/*.ui CJK strings (auto tr-wrapped)", count_ui_strings(root))

    tiers = ["widget_text", "param_label", "runtime_message",
             "data_identity", "export_content", "not_ui_text", "unresolved"]
    print("\n-- bare literals by tier (sum=%d) --" % sum(by_tier.values()))
    for tier in tiers:
        print("  %-16s %5d" % (tier, by_tier.get(tier, 0)))

    print("\n-- bare literals by nearest call (top %d of %d distinct) --"
          % (args.top, len(by_key)))
    for key, value in by_key.most_common(args.top):
        print("  %5d  %-42s %s" % (value, key, tier_of(key)))
        for path, line_no, preview in sites[key][:args.samples]:
            print("          %s:%d  \"%s\""
                  % (os.path.relpath(path, root).replace("\\", "/"), line_no, preview))

    if args.files:
        print("\n-- per file (bare desc) --")
        rows = sorted(per_file.items(), key=lambda kv: (-len(kv[1][1]), kv[0]))
        for path, (wrapped, bare, _, _, _) in rows:
            if bare:
                print("  bare=%4d wrapped=%4d  %s"
                      % (len(bare), wrapped, os.path.relpath(path, root).replace("\\", "/")))
        print("\n-- top files by escaped CJK (blind spot) --")
        erows = sorted(((e, p) for p, (_, _, _, _, e) in per_file.items() if e), reverse=True)
        for count, path in erows[:10]:
            print("  esc=%4d  %s" % (count, os.path.relpath(path, root).replace("\\", "/")))

    print("\nfiles_with_bare=%d files_with_wrapped=%d files_both=%d"
          % (sum(1 for _, b, _, _, _ in per_file.values() if b),
             sum(1 for w, _, _, _, _ in per_file.values() if w),
             sum(1 for w, b, _, _, _ in per_file.values() if w and b)))


if __name__ == "__main__":
    main()
    sys.exit(0)

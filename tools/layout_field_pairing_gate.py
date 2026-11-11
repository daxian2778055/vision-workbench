"""Executable definition of the "layout field pairing" check.

Why this exists (2026-11-11): the运行界面 layout is persisted by hand-written
toJson/fromJson pairs in src/RuntimeInterface.cpp. There is no schema, so a field
can be written on save but never read back -- the value silently disappears the
next time the layout is loaded. Two real defects of that shape were already fixed
by hand: `cornerRadius` was written only inside the RunStatus branch although the
designer lets SubInterface set it (saved as 0), and `subPassword` was persisted in
plaintext while the reader compared it with `!=`.

This gate compares the key sets of each writer/reader pair and fails when a key is
read but never written (a value that cannot survive a save/load round trip).

Classification rule (mechanical, no semantic guessing):
  * keys are collected from `o["k"]`, `root["k"]`, `json["k"]`, `cv/bars/t/tabs["k"]`
    (the local aliases used in that file) and from `.value(QStringLiteral("k"))`
  * read_only  = read by fromJson, never written by toJson  -> RED (data loss)
  * write_only = written by toJson, never read by fromJson  -> report only

Deliberately NOT treated as asymmetry:
  * `type` at page level: the page reader reads each control's type and hands the
    parsed enum to controlFromJson, which then also reads its own `type` key.
    Listed in READ_ALLOW so the gate stays about real data loss.
  * nested sub-objects whose keys are read through a different alias are collected
    by including every alias above; if a new alias appears, the gate will report it
    as read_only, which is the fail-closed side (a human then adds the alias).

Reproduce:
    python tools/layout_field_pairing_gate.py
Exit codes: 0 = no read-only key; 1 = at least one read-only key or an unparsable
            function body (missing writer/reader is a red too, not a skip).
Output is ASCII-only on purpose (the console code page is not guaranteed UTF-8).
"""

import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO_ROOT, "src", "RuntimeInterface.cpp")

ALIASES = ("o", "root", "json", "cv", "bars", "t", "tabs")
KEY_RE = re.compile(r'(?:%s)\["([A-Za-z0-9_]+)"\]' % "|".join(ALIASES))
VALUE_RE = re.compile(r'\.value\(QStringLiteral\("([A-Za-z0-9_]+)"\)\)')

PAIRS = [
    # label, writer signature, reader signature, read-only allowlist
    ("RuntimeControl",
     "QJsonObject controlToJson",
     "RuntimeControl controlFromJson",
     {"type"}),
    ("RuntimeInterfacePage",
     "QJsonObject RuntimeInterfacePage::toJson",
     "bool RuntimeInterfacePage::fromJson",
     {"type"}),
    ("RuntimeInterface",
     "QJsonObject RuntimeInterface::toJson",
     "bool RuntimeInterface::fromJson",
     set()),
]


def body_of(source, signature):
    """Text of the function body whose signature appears at `signature`.

    Braces are balanced forward from the first '{'; a signature that is not found
    returns None so the caller can fail closed instead of skipping the pair.
    """
    i = source.find(signature)
    if i < 0:
        return None
    j = source.find("{", i)
    if j < 0:
        return None
    depth = 0
    k = j
    while k < len(source):
        if source[k] == "{":
            depth += 1
        elif source[k] == "}":
            depth -= 1
            if depth == 0:
                return source[j:k]
        k += 1
    return None


def keys_of(text):
    if not text:
        return set()
    return set(KEY_RE.findall(text)) | set(VALUE_RE.findall(text))


def main():
    if not os.path.exists(SRC):
        print("FAIL: source not found: %s" % SRC)
        return 1
    source = io.open(SRC, encoding="utf-8", errors="replace").read()

    red = 0
    for label, writer, reader, read_allow in PAIRS:
        w_text = body_of(source, writer)
        r_text = body_of(source, reader)
        if w_text is None or r_text is None:
            print("FAIL: %s: cannot locate %s / %s"
                  % (label, "writer" if w_text is None else "",
                     "reader" if r_text is None else ""))
            red += 1
            continue
        written = keys_of(w_text)
        read = keys_of(r_text)
        read_only = sorted((read - written) - read_allow)
        write_only = sorted(written - read)
        print("PAIR %-22s writer=%d reader=%d read_only=%d write_only=%d"
              % (label, len(written), len(read), len(read_only), len(write_only)))
        for k in read_only:
            print("  READ_ONLY  %s: '%s' is read back but never written" % (label, k))
        for k in write_only:
            print("  WRITE_ONLY %s: '%s' is written but never read" % (label, k))
        if read_only:
            red += 1

    if red:
        print("VERDICT RED: %d pair(s) lose data across save/load" % red)
        return 1
    print("VERDICT GREEN: every persisted key is both written and read back")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())

"""Executable definition of the "persisted field pairing" check.

Why this exists (2026-11-11): persisted state in this repo is written by hand-written
toJson/fromJson pairs with no schema, so a field can be written on save and never read
back -- the value then disappears the next time it is loaded. Two defects of that shape
were already fixed by hand in the runtime-interface layout: `cornerRadius` was written
only inside the RunStatus branch although the designer lets SubInterface set it (saved
as 0), and `subPassword` was stored in plaintext while the reader compared it with `!=`.

This gate compares the key sets of each writer/reader pair and fails when a key is read
but never written (a value that cannot survive a save/load round trip). It covers two
groups:

  layout  RuntimeInterface.cpp        -- control / page / whole-interface layout
  comm    CommunicationManager.cpp    -- devices / receive+send events / response config
          ReceiveEvent.cpp            -- base + text-protocol + byte-match readers
          SendEvent.cpp               -- base + text-direct + byte-pack senders
          HeartbeatManager.cpp
          GlobalTriggerManager.cpp

Classification rule (mechanical, no semantic guessing):
  * keys are collected from both spellings used in these files -- `obj["k"]` and
    `obj[QStringLiteral("k")]` -- plus the read form `.value(QStringLiteral("k"))`
  * read_only  = read by fromJson, never written by toJson  -> RED (data loss)
  * write_only = written by toJson, never read by fromJson  -> report only

Deliberately NOT treated as asymmetry (per-group allowlists, each with the reason):
  * layout: `type` -- the control type is parsed separately by runtimeControlTypeFromName()
    and passed into controlFromJson, so the reader body does not mention the key.
  * comm:   `id`, `deviceName` -- CommunicationManager::fromJson reads them off each event
    object only to construct the event instance; the keys are written by
    ReceiveEvent::toJson / SendEvent::toJson (a different function in a different file).

Reproduce:
    python tools/layout_field_pairing_gate.py
Exit codes: 0 = no read-only key; 1 = at least one read-only key or an unparsable
            function body (a missing writer/reader is a red too, not a skip).
Output is ASCII-only on purpose (the console code page is not guaranteed UTF-8).
"""

import io
import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

KEY_RE = re.compile(r'\[\s*(?:QStringLiteral\()?"([A-Za-z0-9_]+)"\)?\s*\]')
VALUE_RE = re.compile(r'\.value\(\s*(?:QStringLiteral\()?"([A-Za-z0-9_]+)"\)?\s*\)')

# (label, source file, writer signature, reader signature, read-only allowlist)
LAYOUT_PAIRS = [
    ("RuntimeControl", "src/RuntimeInterface.cpp",
     "QJsonObject controlToJson", "RuntimeControl controlFromJson", {"type"}),
    ("RuntimeInterfacePage", "src/RuntimeInterface.cpp",
     "QJsonObject RuntimeInterfacePage::toJson", "bool RuntimeInterfacePage::fromJson",
     {"type"}),
    ("RuntimeInterface", "src/RuntimeInterface.cpp",
     "QJsonObject RuntimeInterface::toJson", "bool RuntimeInterface::fromJson", set()),
]

COMM_PAIRS = [
    ("CommunicationManager", "src/CommunicationManager.cpp",
     "QJsonObject CommunicationManager::toJson", "void CommunicationManager::fromJson",
     {"id", "deviceName"}),
    ("ReceiveEvent", "src/ReceiveEvent.cpp",
     "QJsonObject ReceiveEvent::toJson", "void ReceiveEvent::fromJson", set()),
    ("TextProtocolReceiveEvent", "src/ReceiveEvent.cpp",
     "QJsonObject TextProtocolReceiveEvent::toJson",
     "void TextProtocolReceiveEvent::fromJson", set()),
    ("ByteMatchReceiveEvent", "src/ReceiveEvent.cpp",
     "QJsonObject ByteMatchReceiveEvent::toJson",
     "void ByteMatchReceiveEvent::fromJson", set()),
    ("SendEvent", "src/SendEvent.cpp",
     "QJsonObject SendEvent::toJson", "void SendEvent::fromJson", set()),
    ("TextDirectSendEvent", "src/SendEvent.cpp",
     "QJsonObject TextDirectSendEvent::toJson",
     "void TextDirectSendEvent::fromJson", set()),
    ("BytePackSendEvent", "src/SendEvent.cpp",
     "QJsonObject BytePackSendEvent::toJson",
     "void BytePackSendEvent::fromJson", set()),
    ("HeartbeatManager", "src/HeartbeatManager.cpp",
     "QJsonObject HeartbeatManager::toJson", "void HeartbeatManager::fromJson", set()),
    ("GlobalTriggerManager", "src/GlobalTriggerManager.cpp",
     "QJsonObject GlobalTriggerManager::toJson", "void GlobalTriggerManager::fromJson",
     set()),
]

GROUPS = [("layout", LAYOUT_PAIRS), ("comm", COMM_PAIRS)]


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
    cache = {}
    red = 0
    for group, pairs in GROUPS:
        print("GROUP %s pairs=%d" % (group, len(pairs)))
        for label, rel, writer, reader, read_allow in pairs:
            path = os.path.join(REPO_ROOT, rel)
            if path not in cache:
                if not os.path.exists(path):
                    print("FAIL: source not found: %s" % rel)
                    cache[path] = None
                else:
                    cache[path] = io.open(path, encoding="utf-8",
                                          errors="replace").read()
            source = cache[path]
            if source is None:
                red += 1
                continue
            w_text = body_of(source, writer)
            r_text = body_of(source, reader)
            if w_text is None or r_text is None:
                print("  FAIL %-26s cannot locate %s"
                      % (label, "writer" if w_text is None else "reader"))
                red += 1
                continue
            written = keys_of(w_text)
            read = keys_of(r_text)
            read_only = sorted((read - written) - read_allow)
            write_only = sorted(written - read)
            print("  PAIR %-26s writer=%2d reader=%2d read_only=%d write_only=%d"
                  % (label, len(written), len(read), len(read_only), len(write_only)))
            for k in read_only:
                print("    READ_ONLY  %s: '%s' is read back but never written"
                      % (label, k))
            for k in write_only:
                print("    WRITE_ONLY %s: '%s' is written but never read" % (label, k))
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

"""Report-only inventory of the `src/file.cpp:NNN` citations scattered through docs/.

Why this exists (U-38 / third-round review comment S-1 step 1): the gap plan quotes
production source lines by number. Source lines drift (measured: 3 lines, :566 -> :569),
and the weakest possible check "line number <= file length" cannot see that drift - it
passes on any in-range number, right or wrong. Before deciding what a real gate should
assert, the population has to be measured: how many citations exist, where they point,
whether the file is still there, and whether the identifier named in the prose actually
shows up on the cited line or only a few lines away.

This script therefore NEVER goes red. Exit code is 0 for any finding; a non-zero exit
means the script itself failed (unreadable input, crash). That is deliberate, matching
how step 1g (tools/dup_cn_literal_gate.py) started life as a measurement before it
became a gate. Step 2 - window size and where the identifier is taken from - is still
open, and the printed per-window hit rates (win0/win1/win3/win5) are the data that
decision should be made on. The three win0_miss* lines below them are that decision's
distribution table (the gap plan's S-1 row cites them by name), so they stay in the output.

Reproduce:
    python tools/src_anchor_inventory.py            # table + summary
    python tools/src_anchor_inventory.py --full     # also print the cited source line
Output is ASCII-only (the console code page is not guaranteed UTF-8); Chinese doc file
names are printed as stable D-index ids, which the DOCID lines at the top decode.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC_DIR = os.path.join(ROOT, "docs")

# src/ProjectManager.cpp:495  |  include/ProjectManager.h:12-14  |  tools/ci.ps1:88
CITE = re.compile(
    r"(?P<path>(?:src|include|tests|tools)/[A-Za-z0-9_.\-/]+\.(?:cpp|cc|h|hpp|py|ps1|cmake|txt|md))"
    r":(?P<start>\d{1,5})(?:\s*[-\u2013]\s*(?P<end>\d{1,5}))?"
)
# Blind spot of CITE: the same citation written with a space before the colon
# ("src/ProjectManager.cpp :336"). Step 2 has to merge these in first, so the count is
# printed instead of being left to a session-side calculation.
SPACED = re.compile(
    r"(?:src|include|tests|tools)/[A-Za-z0-9_.\-/]+\.(?:cpp|cc|h|hpp|py|ps1|cmake|txt|md)"
    r"\s+:\s*\d{1,5}"
)
IDENT = re.compile(r"[A-Za-z_][A-Za-z0-9_]{3,}")
BACKTICK = re.compile(r"`([^`\n]+)`")
WINDOWS = (0, 1, 3, 5)

# Words that look like identifiers but say nothing about the cited line.
STOP = {
    "src", "include", "tests", "tools", "cpp", "hpp", "py", "ps1", "md", "txt",
    "http", "https", "www", "com", "the", "and", "for", "with", "this", "that",
    "from", "else", "true", "false", "null", "todo", "note", "code", "line",
    "docs", "file", "name", "test", "case", "json", "jsonobject", "qstring",
    "qjsonarray", "qmap", "qset", "list", "void", "int", "auto", "return",
}


def read_text(path):
    with io.open(path, "rb") as handle:
        data = handle.read()
    return data.decode("utf-8", errors="replace")


def split_lines(text):
    return text.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def ascii_only(text):
    out = []
    for ch in text:
        if ch == "\t":
            out.append(" ")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
    return "".join(out)


def token_set(lines, lo, hi):
    """Identifier tokens found in lines[lo:hi] (1-based, inclusive, clipped)."""
    if not lines:
        return set()
    lo = max(1, lo)
    hi = min(len(lines), hi)
    if lo > hi:
        return set()
    blob = " ".join(ascii_only(x) for x in lines[lo - 1:hi])
    return {t.lower() for t in IDENT.findall(blob) if t.lower() not in STOP}


def doc_candidates(doc_line, cite_text):
    """Identifiers the prose names: backticked spans first, then bare tokens.

    The citation itself is removed first - 'ProjectManager' in the path would
    otherwise count as evidence that the line was checked.
    """
    stripped = ascii_only(doc_line).replace(cite_text, " ")
    ticked = set()
    for span in BACKTICK.findall(stripped):
        for t in IDENT.findall(span):
            ticked.add(t.lower())
    bare = {t.lower() for t in IDENT.findall(stripped)}
    bare -= ticked
    ticked = {t for t in ticked if t not in STOP}
    bare = {t for t in bare if t not in STOP}
    return ticked, bare


def main():
    full = "--full" in sys.argv[1:]
    if not os.path.isdir(DOC_DIR):
        print("ERROR docs directory not found:", DOC_DIR)
        return 2

    doc_names = sorted(n for n in os.listdir(DOC_DIR) if n.endswith(".md"))
    doc_id = {}
    rows = []
    src_cache = {}
    spaced = 0

    for name in doc_names:
        doc_id[name] = "D%02d" % (len(doc_id) + 1)
        lines = split_lines(read_text(os.path.join(DOC_DIR, name)))
        for no, line in enumerate(lines, start=1):
            spaced += len(SPACED.findall(line))
            for m in CITE.finditer(line):
                path = m.group("path")
                if path not in src_cache:
                    abs_path = os.path.join(ROOT, path.replace("/", os.sep))
                    if os.path.isfile(abs_path):
                        src_cache[path] = split_lines(read_text(abs_path))
                    else:
                        src_cache[path] = None
                rows.append((doc_id[name], name, no, m.group(0), path,
                             int(m.group("start")),
                             int(m.group("end")) if m.group("end") else 0, line))

    print("== doc id map ==")
    for name in doc_names:
        print("DOCID %s %s" % (doc_id[name], ascii_only(name) or "(non-ascii name)"))

    print("== citations ==")
    stats = {w: 0 for w in WINDOWS}
    tick_stats = {w: 0 for w in WINDOWS}
    tick_checked = [0]
    checked = 0
    no_candidate = 0
    missing_file = 0
    out_of_range = 0
    files = set()
    for did, name, dno, cite_text, path, start, end, raw_line in rows:
        files.add(path)
        lines = src_cache[path]
        if lines is None:
            missing_file += 1
            print("ROW doc=%s line=%d cite=%s file=ABSENT" % (did, dno, ascii_only(cite_text)))
            continue
        hi = end if end else start
        in_range = 1 <= start <= hi <= len(lines)
        if not in_range:
            out_of_range += 1
        ticked, bare = doc_candidates(raw_line, cite_text)
        cands = ticked | bare
        if not cands:
            no_candidate += 1
        flags = []
        any_hit = {w: False for w in WINDOWS}
        for w in WINDOWS:
            window = token_set(lines, start - w, hi + w)
            hit = bool(cands & window)
            any_hit[w] = hit
            flags.append("win%d=%s" % (w, "hit" if hit else ("-" if not cands else "miss")))
        if cands:
            checked += 1
            for w in WINDOWS:
                if any_hit[w]:
                    stats[w] += 1
        detail = ""
        if full and in_range:
            detail = " |src=%s" % ascii_only(lines[start - 1]).strip()[:120]
        if ticked:
            tick_checked[0] += 1
            for w in WINDOWS:
                if any_hit[w] and (ticked & token_set(lines, start - w, hi + w)):
                    tick_stats[w] += 1
        print("ROW doc=%s line=%d cite=%s file_lines=%d in_range=%s cands=%d(%s) %s%s"
              % (did, dno, ascii_only(cite_text), len(lines), "yes" if in_range else "NO",
                 len(cands), ",".join(sorted(list(ticked)[:3])) or "none",
                 " ".join(flags), detail))

    print("== summary ==")
    print("INV total=%d docs=%d cited_files=%d" % (len(rows), len(doc_names), len(files)))
    print("INV missing_file=%d out_of_range=%d" % (missing_file, out_of_range))
    print("INV with_candidates=%d no_candidates=%d" % (checked, no_candidate))
    for w in WINDOWS:
        print("INV window%d_hits=%d/%d" % (w, stats[w], checked))
    # Step-2 calibration table: how many rows need a wider window before the named
    # identifier appears at all. Derived from the counters above, not counted twice.
    win0_miss = checked - stats[0]
    never = checked - stats[5]
    print("INV win0_miss=%d" % win0_miss)
    print("INV win0_miss_first_hit w1=%d w3=%d w5=%d never=%d"
          % (stats[1] - stats[0], stats[3] - stats[1], stats[5] - stats[3], never))
    print("INV win0_miss_hit_by_w5=%d" % (win0_miss - never))
    print("INV spaced_style_not_in_denominator=%d (path<space>:NNN, CITE cannot extract these)"
          % spaced)
    print("INV (identifier source = backticked spans only)")
    for w in WINDOWS:
        print("INV tickwindow%d_hits=%d/%d" % (w, tick_stats[w], tick_checked[0]))
    print("INV verdict=REPORT_ONLY exit=0 (no assertion made here; step 2 is the gate)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:   # noqa: BLE001 - a crash must be visible, findings must not
        print("ERROR script failed:", type(exc).__name__, ascii_only(str(exc))[:200])
        sys.exit(3)

"""Report-only inventory of the `src/file.cpp:NNN` citations scattered through docs/.

Why this exists (U-38 / third-round review comment S-1 step 1): the gap plan quotes
production source lines by number. Source lines drift (measured: 3 lines, :566 -> :569),
and the weakest possible check "line number <= file length" cannot see that drift - it
passes on any in-range number, right or wrong. Before deciding what a real gate should
assert, the population has to be measured: how many citations exist, where they point,
whether the file is still there, and whether the identifier named in the prose actually
shows up on the cited line or only a few lines away.

Three citation styles are present and only the first one was in the denominator, which is
what the fourth-round review (S-1) asked to be listed as a pre-condition of step 2:
  1) tight   "src/Foo.cpp:495", range "src/Foo.cpp:12-14"  -> CITE, printed as INV total
  2) spaced  "src/Foo.cpp :495" (a space before the colon)  -> SPACED, printed as INV spaced_*
  3) bare ":NNN" continuation on a line that already carries a path, e.g.
     "src/Foo.cpp:297 / :300 / :301" -> ANY_COLON_NUM, printed as INV continuation_like
Style 3 carries no path of its own, so no path-anchored regex can locate it; it is counted,
never resolved. Styles 2 and 3 are measured with the SAME window definition as style 1
(window_hits below is the only place a "window hit" is defined) and printed as their own
denominators rather than silently merged into INV total - merging is step 2's decision.

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
# Second extraction style, invisible to CITE: the same citation written with a space
# before the colon ("src/ProjectManager.cpp :336"). Step 2 has to decide how to merge
# these, so they are extracted and measured as their own denominator here.
SPACED = re.compile(
    r"(?P<path>(?:src|include|tests|tools)/[A-Za-z0-9_.\-/]+\.(?:cpp|cc|h|hpp|py|ps1|cmake|txt|md))"
    r"\s+:\s*(?P<start>\d{1,5})"
)
# Third style, unattachable by any path-anchored regex: a bare ":NNN" written after an
# already-anchored citation on the same line (":297 / :300 / :301"). Only counted, not located.
ANY_COLON_NUM = re.compile(r"[:\uFF1A]\d{1,5}")
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


def window_hits(lines, start, hi, cands):
    """Does an identifier named in the prose appear within +/- w of the cited line?

    Shared by both extraction styles so 'window hit' has exactly one definition here.
    """
    return {w: bool(cands & token_set(lines, start - w, hi + w)) for w in WINDOWS}


def flag_text(hits, has_cands):
    return " ".join("win%d=%s" % (w, "hit" if hits[w] else ("miss" if has_cands else "-"))
                    for w in WINDOWS)


def main():
    full = "--full" in sys.argv[1:]
    if not os.path.isdir(DOC_DIR):
        print("ERROR docs directory not found:", DOC_DIR)
        return 2

    doc_names = sorted(n for n in os.listdir(DOC_DIR) if n.endswith(".md"))
    doc_id = {}
    rows = []
    srows = []
    src_cache = {}
    continuation = 0
    noise_colons = 0

    def cache_for(path):
        if path not in src_cache:
            abs_path = os.path.join(ROOT, path.replace("/", os.sep))
            src_cache[path] = split_lines(read_text(abs_path)) if os.path.isfile(abs_path) else None
        return src_cache[path]

    for name in doc_names:
        doc_id[name] = "D%02d" % (len(doc_id) + 1)
        lines = split_lines(read_text(os.path.join(DOC_DIR, name)))
        for no, line in enumerate(lines, start=1):
            tight_hits = list(CITE.finditer(line))
            spaced_hits = list(SPACED.finditer(line))
            for m in tight_hits:
                path = m.group("path")
                cache_for(path)
                rows.append((doc_id[name], name, no, m.group(0), path,
                             int(m.group("start")),
                             int(m.group("end")) if m.group("end") else 0, line))
            for m in spaced_hits:
                path = m.group("path")
                cache_for(path)
                srows.append((doc_id[name], name, no, m.group(0), path, int(m.group("start")), line))
            # bare ":NNN" that no path-anchored regex can attribute to a file
            if tight_hits or spaced_hits:
                anchored = [(cm.start("start") - 1) for cm in tight_hits]
                anchored += [(sm.start("start") - 1) for sm in spaced_hits]
                for m in ANY_COLON_NUM.finditer(line):
                    if m.start() not in anchored:
                        continuation += 1
            else:
                noise_colons += len(ANY_COLON_NUM.findall(line))

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
        hits = window_hits(lines, start, hi, cands)
        if cands:
            checked += 1
            for w in WINDOWS:
                if hits[w]:
                    stats[w] += 1
        detail = ""
        if full and in_range:
            detail = " |src=%s" % ascii_only(lines[start - 1]).strip()[:120]
        if ticked:
            tick_checked[0] += 1
            for w in WINDOWS:
                if hits[w] and (ticked & token_set(lines, start - w, hi + w)):
                    tick_stats[w] += 1
        # sorted() before the slice: set order of strings is salted per process, so an
        # unsorted sample would make replayers see different rows on the same bytes.
        print("ROW doc=%s line=%d cite=%s file_lines=%d in_range=%s cands=%d(%s) %s%s"
              % (did, dno, ascii_only(cite_text), len(lines), "yes" if in_range else "NO",
                 len(cands), ",".join(sorted(ticked)[:3]) or "none",
                 flag_text(hits, bool(cands)), detail))

    print("== spaced citations (path<space>:NNN - outside the INV total denominator) ==")
    s_stats = {w: 0 for w in WINDOWS}
    s_checked = 0
    s_no_candidate = 0
    s_missing_file = 0
    s_out_of_range = 0
    s_files = set()
    for did, name, dno, cite_text, path, start, raw_line in srows:
        s_files.add(path)
        lines = src_cache[path]
        if lines is None:
            s_missing_file += 1
            print("SPACEDROW doc=%s line=%d cite=%s file=ABSENT" % (did, dno, ascii_only(cite_text)))
            continue
        in_range = 1 <= start <= len(lines)
        if not in_range:
            s_out_of_range += 1
        ticked, bare = doc_candidates(raw_line, cite_text)
        cands = ticked | bare
        hits = window_hits(lines, start, start, cands)
        if not cands:
            s_no_candidate += 1
        else:
            s_checked += 1
            for w in WINDOWS:
                if hits[w]:
                    s_stats[w] += 1
        print("SPACEDROW doc=%s line=%d cite=%s file_lines=%d in_range=%s cands=%d(%s) %s"
              % (did, dno, ascii_only(cite_text), len(lines), "yes" if in_range else "NO",
                 len(cands), ",".join(sorted(ticked)[:3]) or "none",
                 flag_text(hits, bool(cands))))

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
    spaced = len(srows)
    print("INV spaced_style_not_in_denominator=%d (path<space>:NNN, CITE cannot extract these)"
          % spaced)
    print("INV spaced_total=%d spaced_files=%d spaced_missing_file=%d spaced_out_of_range=%d"
          % (spaced, len(s_files), s_missing_file, s_out_of_range))
    print("INV spaced_with_candidates=%d spaced_no_candidates=%d" % (s_checked, s_no_candidate))
    for w in WINDOWS:
        print("INV spaced_window%d_hits=%d/%d" % (w, s_stats[w], s_checked))
    print("INV spaced_win0_miss=%d" % (s_checked - s_stats[0]))
    # Style 3: bare ":NNN" on a line that already carries a path - no path-anchored regex
    # can attribute it to a file, so step 2 must either reject this style or rewrite it.
    print("INV continuation_like=%d (bare :NNN on an already-cited line, unattachable)"
          % continuation)
    print("INV noise_colons=%d (upper bound: :NNN on lines with no path at all)" % noise_colons)
    print("INV citation_like_total=%d (tight %d + spaced %d + continuation %d)"
          % (len(rows) + spaced + continuation, len(rows), spaced, continuation))
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

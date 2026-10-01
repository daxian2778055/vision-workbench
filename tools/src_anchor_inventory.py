"""Inventory of the `src/file.cpp:NNN` citations scattered through docs/, with one assertion.

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

Fifth-round review S-2 asked what happens to rows that cannot be window-checked at all, i.e.
citations whose prose names no identifier (cands=0; measured: 15 tight + 16 spaced). Their
fallback is defined HERE rather than left for step 2 to invent: no window judgement, only the
weak check (cited number within the cited file's length) plus a printed roster, one NOCANDROW
per row and INV nocand_rows / nocand_out_of_range as the totals. Step 2 inherits that as a
pre-condition instead of discovering a hole in the denominator after the gate exists.

Sixth-round review S-2 asked for that printed roster to be frozen as a baseline, the way
1g froze its 58 duplicate sentences (set equality), so that a NEW citation whose prose names
no identifier shows up as ADDED instead of just moving a total. That baseline is
NOCAND_BASELINE below and the comparison is a multiset equality on the key

    (kind, path, start, hi)          # kind = "tight" | "spaced"

deliberately WITHOUT the doc id and the doc line number: both of those move when unrelated
text is inserted above (doc ids are assigned by sorted filename, doc lines by insertion), so
keying on them would report drift as an added row. hi is the end of a cited range, or start
when the citation names one line. The key is a function of the doc text plus one thing the
doc cannot control: rows whose cited file is ABSENT never reach the roster (they are counted
by missing_file and printed as file=ABSENT), so renaming or deleting a cited source file
shows up here as REMOVED.

Multiset, not set: measured on the current docs, 31 rows share 30 distinct keys, because
"src/ProjectManager.cpp :336" is cited with no identifier in the prose from two different
doc lines. A set would let one of those two be deleted unnoticed.

Judgement is asymmetric on purpose, following this round's other review point ("a gate that
goes red once gets switched off"): only ADDED is red (exit 1). A REMOVED row is printed and
counted but does not fail the run, because a row can leave the roster by the prose being
improved - naming the identifier the citation rests on - and punishing that would make the
gate something people want to disable. Shrinking the baseline stays a visible, named number
that a round has to account for, it just is not a red condition.

Everything else here is still measurement: no window/denominator assertion is made, step 2
(window size, and where the identifier is taken from) is still open, and the printed
per-window hit rates (win0/win1/win3/win5) are the data that decision should be made on. The
three win0_miss* lines below them are that decision's distribution table (the gap plan's S-1
row cites them by name), so they stay in the output.

Exit codes: 0 = no ADDED row, 1 = the roster has a row that is not in NOCAND_BASELINE,
2 = unreadable input (no docs), 3 = the script itself crashed.

Reproduce:
    python tools/src_anchor_inventory.py            # table + summary
    python tools/src_anchor_inventory.py --full     # also print the cited source line
    python tools/src_anchor_inventory.py --docs-root DIR   # read docs from DIR instead of
                                             # docs/ - input only, nothing is ever written
                                             # (used to feed the gate synthetic rosters)
Output is ASCII-only (the console code page is not guaranteed UTF-8); Chinese doc file
names are printed as stable D-index ids, which the DOCID lines at the top decode.
"""

import collections
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

# Frozen NOCANDROW roster (sixth-round review S-2), recorded with
#     python tools/src_anchor_inventory.py
# on the commit this baseline landed on: 31 rows over 30 distinct keys, counts shown for
# keys a set would have collapsed. Regenerate with --emit-nocand-baseline and review the
# block before pasting it here; a row may only be added to it together with the prose that
# named no identifier being fixed or the citation being re-anchored.
NOCAND_BASELINE = (
    ("spaced", "include/ProjectManager.h", 74, 74, 1),
    ("spaced", "include/ProjectManager.h", 77, 77, 1),
    ("spaced", "src/CalibrationManager.cpp", 97, 97, 1),
    ("spaced", "src/OpencvFeatureMatchNode.cpp", 86, 86, 1),
    ("spaced", "src/OpencvTemplateMatchNode.cpp", 75, 75, 1),
    ("spaced", "src/ProjectManager.cpp", 297, 297, 1),
    ("spaced", "src/ProjectManager.cpp", 336, 336, 2),
    ("spaced", "src/ProjectManager.cpp", 361, 361, 1),
    ("spaced", "src/ProjectManager.cpp", 363, 363, 1),
    ("spaced", "src/ProjectManager.cpp", 522, 522, 1),
    ("spaced", "src/ProjectManager.cpp", 566, 566, 1),
    ("spaced", "src/TemplateMatchNode.cpp", 41, 41, 1),
    ("spaced", "tests/integration_test.cpp", 4420, 4420, 1),
    ("spaced", "tests/integration_test.cpp", 4426, 4426, 1),
    ("spaced", "tests/integration_test.cpp", 4613, 4613, 1),
    ("tight", "src/HelpViewer.cpp", 139, 139, 1),
    ("tight", "src/ModuleEditorDialog.cpp", 392, 392, 1),
    ("tight", "src/OpencvTemplateMatchNode.cpp", 74, 74, 1),
    ("tight", "src/ProjectManager.cpp", 334, 334, 1),
    ("tight", "src/ProjectManager.cpp", 480, 480, 1),
    ("tight", "src/ProjectManager.cpp", 485, 485, 1),
    ("tight", "src/ProjectManager.cpp", 501, 501, 1),
    ("tight", "src/ProjectManager.cpp", 510, 510, 1),
    ("tight", "src/ProjectManager.cpp", 553, 553, 1),
    ("tight", "src/VariablePanel.cpp", 115, 115, 1),
    ("tight", "tests/integration_test.cpp", 4344, 4344, 1),
    ("tight", "tests/param_panel_binding_test.cpp", 205, 205, 1),
    ("tight", "tests/param_panel_binding_test.cpp", 2117, 2117, 1),
    ("tight", "tests/param_panel_binding_test.cpp", 2136, 2136, 1),
    ("tight", "tests/param_panel_binding_test.cpp", 2166, 2166, 1),
)


def nocand_key_counts(nocand):
    """Multiset of the roster keys, from the (kind, did, line, path, start, hi, ...) rows."""
    return collections.Counter((row[0], row[3], row[4], row[5]) for row in nocand)


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
    argv = sys.argv[1:]
    full = "--full" in argv
    emit_baseline = "--emit-nocand-baseline" in argv
    docs_dir = DOC_DIR
    if "--docs-root" in argv:
        i = argv.index("--docs-root") + 1
        docs_dir = argv[i] if i < len(argv) else ""
    if not os.path.isdir(docs_dir):
        print("ERROR docs directory not found: %s" % ascii_only(docs_dir))
        return 2

    doc_names = sorted(n for n in os.listdir(docs_dir) if n.endswith(".md"))
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
        lines = split_lines(read_text(os.path.join(docs_dir, name)))
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
    nocand = []
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
            nocand.append(("tight", did, dno, path, start, hi, len(lines), in_range))
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
            nocand.append(("spaced", did, dno, path, start, start, len(lines), in_range))
        else:
            s_checked += 1
            for w in WINDOWS:
                if hits[w]:
                    s_stats[w] += 1
        print("SPACEDROW doc=%s line=%d cite=%s file_lines=%d in_range=%s cands=%d(%s) %s"
              % (did, dno, ascii_only(cite_text), len(lines), "yes" if in_range else "NO",
                 len(cands), ",".join(sorted(ticked)[:3]) or "none",
                 flag_text(hits, bool(cands))))

    print("== no-candidate rows (prose names no matchable identifier -> weak check only) ==")
    # Fifth-round review S-2: rows whose prose carries no identifier to match cannot be
    # window-checked at all, so the fallback for them is the weak check only -- the cited
    # number must not exceed the cited file's length -- plus this printed roster. Rows whose
    # file is ABSENT are not here (they are counted by missing_file and printed as file=ABSENT).
    nocand_sorted = sorted(nocand, key=lambda r: (r[0], r[1], r[2], r[3], r[4]))
    nocand_out_of_range = 0
    for kind, did, dno, path, start, hi, file_lines, in_range in nocand_sorted:
        if not in_range:
            nocand_out_of_range += 1
        print("NOCANDROW kind=%s doc=%s line=%d cite=%s file_lines=%d in_range=%s"
              % (kind, did, dno, ascii_only("%s:%d-%d" % (path, start, hi)),
                 file_lines, "yes" if in_range else "NO"))
    print("INV nocand_rows=%d (tight %d + spaced %d) nocand_out_of_range=%d"
          % (len(nocand_sorted), no_candidate, s_no_candidate, nocand_out_of_range))

    print("== nocand baseline (set equality; ADDED is the only red condition) ==")
    # Sixth-round review S-2: the roster above is now frozen as NOCAND_BASELINE, so a new
    # "cited a source line, prose named no identifier" writing shows up as ADDED instead of
    # only moving nocand_rows. Comparison is on counts per key, because two doc rows can
    # share one key (measured: 31 rows / 30 keys).
    observed = nocand_key_counts(nocand_sorted)
    expected = collections.Counter()
    for kind, path, start, hi, n in NOCAND_BASELINE:
        expected[(kind, path, start, hi)] = n
    added = sorted((k, observed[k] - expected.get(k, 0)) for k in observed
                   if observed[k] > expected.get(k, 0))
    removed = sorted((k, expected[k] - observed.get(k, 0)) for k in expected
                     if expected[k] > observed.get(k, 0))
    for (kind, path, start, hi), n in added:
        print("NOCANDELTA dir=ADDED rows=%d kind=%s cite=%s:%d-%d" % (n, kind, path, start, hi))
    for (kind, path, start, hi), n in removed:
        print("NOCANDELTA dir=REMOVED rows=%d kind=%s cite=%s:%d-%d"
              % (n, kind, path, start, hi))
    print("INV nocand_baseline_rows=%d keys=%d | observed_rows=%d keys=%d"
          % (sum(expected.values()), len(expected), sum(observed.values()), len(observed)))
    print("INV nocand_added=%d nocand_removed=%d (added=%d removed=%d rows)"
          % (len(added), len(removed),
             sum(n for _, n in added), sum(n for _, n in removed)))
    if emit_baseline:
        print("== fresh NOCAND_BASELINE block (review before pasting) ==")
        for key in sorted(observed):
            kind, path, start, hi = key
            print('    ("%s", "%s", %d, %d, %d),' % (kind, path, start, hi, observed[key]))

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
    print("INV docs_root=%s" % ascii_only(os.path.relpath(docs_dir, ROOT).replace(os.sep, "/")))
    if emit_baseline:
        print("INV verdict=EMIT exit=0 (fresh block printed above; no judgement made)")
        return 0
    print("INV verdict=%s exit=%d (the only assertion is nocand ADDED vs NOCAND_BASELINE; "
          "window rates and the other denominators stay report-only)"
          % ("RED" if added else "GREEN", 1 if added else 0))
    return 1 if added else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:   # noqa: BLE001 - a crash must be visible, findings must not
        print("ERROR script failed:", type(exc).__name__, ascii_only(str(exc))[:200])
        sys.exit(3)

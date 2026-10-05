"""Report-only inventory of *in-place line rewrites* between two readable states.

Why this exists (U-41 / fifth-round review comment S-3): the ledger's standing rule for
new sections is "insert a whole block, keep every pre-existing line untouched", and the
prefix gate that enforces it can only see whole-block insertions. A line that is EDITED
in place (same line count, content changed) is invisible to that gate - the file still
grows by 0 lines, so nothing is contradicted. The fifth-round review pointed at exactly
one such edit in the previous round and asked for a standing check of its own shape:
"diff against the previous commit shows ONE line, and the content is either pure
addition or pure replacement".

This script is that measurement. It compares two states (a base revision and either
another revision or the current worktree), walks the line-level diff, and prints every
hunk with its shape:

  insert        base has no line there, current adds N lines        -> HUNK kind=insert
  delete        current removes N lines                             -> HUNK kind=delete
  rewrite_1x1   exactly one line replaced by exactly one line        -> HUNK kind=rewrite_1x1
                (this is the "in-place rewrite" S-3 is about)
  rewrite_nxn   N lines replaced by N lines, N >= 2                  -> HUNK kind=rewrite_nxn
  mixed         N lines replaced by M lines, N != M                  -> HUNK kind=mixed

For rewrite_1x1 (and per pair inside rewrite_nxn) the *content* is then classified at
character level, which is the half of S-3 that asks "pure addition or pure replacement":

  pure_insert_tail  the old text is a prefix of the new one - old kept verbatim,
                    text only appended at the end
  pure_insert_mid   old kept verbatim but text inserted in the middle (every old
                    character still present, in order)
  altered           some old characters are GONE - the pre-existing wording was
                    edited, not just extended
  replaced          barely related: similarity below --similar (default 0.34),
                    i.e. the line was rewritten with different content

"altered" is the reading a reviewer needs: the ledger convention says a corrected line
keeps its original wording verbatim and appends the correction, so any altered line is
supposed to be listed and explained rather than silently shipped.

Like tools/src_anchor_inventory.py this script NEVER goes red: exit 0 for any finding,
non-zero only if the script itself fails (git unreadable, crash). Turning "the ledger
must contain no altered rewrite outside its declared section" into a red gate is a
separate decision; the numbers printed here are what that decision should be made on.

Sixth-round review S-1 asked that this decision be scoped by file category instead of
applied to every changed file, because "any altered pair is red" would have gone red on
its very first run - on two production files whose comments legitimately changed together
with the behaviour they describe. The script now buckets every changed file
(ledger / other_doc / test / tool / code / other) and prints the altered_or_replaced pair
count per bucket plus the would-be verdict of both scopings, so the choice is measured:

  - scoped to all files        -> red this round (code comments),
  - scoped to every .md file   -> red on the U-40 hand-off: docs/用户操作手册.md line 152
                                  was rewritten in place (sub=altered, removed=2,
                                  added=38), and that is the manual being updated to match
                                  shipped behaviour, not a ledger convention violation,
  - scoped to the LEDGER only  -> green on both, because "keep the old wording verbatim
                                  and append the correction" is a ledger convention and
                                  only the ledger lives under it.

The third scoping is the criterion printed as the would-be verdict. The ledger's own line
growth is printed next to it together with the recorded split-the-file trigger (more than
+88 lines in one round), so that trigger is computed rather than eyeballed.

From U-60 the "ledger" bucket is a FAMILY of two members, not one path: the plan itself and
the companion register file that its first cut moved the cumulative "not proven" table into.
Both live under the same convention (append new rows, annotate carried rows in place, never
delete), so both belong in the same judged population - and the family's SUM is what the
split trigger is judged on, because a cut that moves rows from one member to the other has
to leave the judgement as hard as it was. The single-file LEDGER_GROWTH line is still printed
in its old format (the ledger quotes it verbatim, so reformatting it would make registered
readings unreplayable); FAMILY_GROWTH is printed next to it, with the source of each member's
number labelled.

Reproduce (two immutable commits - worktree-independent, this is the anchor to cite):
    python tools/inline_rewrite_check.py --base 46b1559 --head ff7b8b0
Reproduce (base commit vs the bytes about to be delivered - moves with the worktree):
    python tools/inline_rewrite_check.py --base ff7b8b0

Binary-safe by construction: file lists come from `git diff -z` and blob contents come
from `git cat-file --batch-check`/`blob`, so Chinese path names never travel through the
console code page. Output is ASCII-only (non-ASCII is printed as \\uXXXX escapes with a
stable F-id so lines stay identifiable).
"""

import difflib
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only these suffixes are line-diffed; anything else is reported as skipped-binary-like.
TEXT_SUFFIX = (".md", ".cpp", ".cc", ".h", ".hpp", ".py", ".ps1", ".txt", ".cmake",
               ".json", ".qml", ".js", ".bat")

# The gap plan is the one file under the ledger convention ("keep the old wording
# verbatim, append the correction"), so it is the only file a red gate may judge.
LEDGER_REL = u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212.md"
# U-60 opened the split-the-file project with its first cut: from section 3.64 on, the
# cumulative register ("the half not proven") is appended to this companion file instead of
# being re-carried into every new ledger block. It is under the SAME convention (append new
# rows, annotate carried rows in place, never delete), so it is in the same judged population
# as the ledger -- and it is in the same measured FAMILY, because moving rows out of one
# member must not lower the family's total growth (see tools/ledger_size_gate.py).
REGISTER_REL = (u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212"
                u"-\u767b\u8bb0\u9644\u8868.md")
LEDGER_FAMILY = (LEDGER_REL, REGISTER_REL)
# Recorded trigger (ledger section 3.42 table row "观察"): a round that adds MORE than
# this many lines to the ledger must open the split-the-file project first.
SPLIT_TRIGGER_DELTA = 88
BUCKETS = ("ledger", "other_doc", "test", "tool", "code", "other")


def bucket_of(path):
    if path in LEDGER_FAMILY:
        return "ledger"
    if path.endswith(".md"):
        return "other_doc"
    if path.startswith("tests/"):
        return "test"
    if path.startswith("tools/"):
        return "tool"
    if path.startswith(("src/", "include/")):
        return "code"
    return "other"


def git(args, stdin_bytes=None):
    proc = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, stdin=subprocess.PIPE)
    out, err = proc.communicate(stdin_bytes)
    return proc.returncode, out, err


def esc(text):
    """ASCII-safe rendering that a reader can decode back (F-ids stay attached anyway)."""
    return text.encode("unicode_escape").decode("ascii")


def read_worktree(rel_path):
    abs_path = os.path.join(ROOT, rel_path.replace("/", os.sep))
    if not os.path.isfile(abs_path):
        return None
    with io.open(abs_path, "rb") as handle:
        return handle.read()


def blob_of(rev, rel_path):
    """Return (raw_bytes, None) or (None, reason). Path travels as UTF-8 on stdin."""
    spec = ("%s:%s" % (rev, rel_path)).encode("utf-8")
    rc, out, err = git(["git", "cat-file", "--batch-check"], spec + b"\n")
    if rc != 0:
        return None, "batch-check-failed:" + esc(err.decode("utf-8", "replace")[:80])
    line = out.decode("ascii", "replace").strip()
    parts = line.split()
    if len(parts) < 2 or parts[1] != "blob":
        return None, "missing"
    rc2, data, err2 = git(["git", "cat-file", "blob", parts[0]])
    if rc2 != 0:
        return None, "blob-failed:" + esc(err2.decode("utf-8", "replace")[:80])
    return data, None


def to_lines(data):
    if data is None:
        return []
    text = data.decode("utf-8", errors="replace")
    return text.replace("\r\n", "\n").replace("\r", "\n").split("\n")


def count_eol(data, kind):
    if data is None:
        return 0
    if kind == "crlf":
        return data.count(b"\r\n")
    if kind == "lf":
        return data.count(b"\n") - data.count(b"\r\n")
    return data.count(b"\n")


def classify_pair(old_line, new_line, similar):
    """Character-level shape of a 1->1 line change. Returns (sub, removed, added)."""
    old = old_line.strip()
    new = new_line.strip()
    if not old or not new:
        return "empty_side", 0, 0
    opcodes = difflib.SequenceMatcher(None, old, new, autojunk=False).get_opcodes()
    removed = sum(e - s for tag, s, e, _i, _j in opcodes if tag in ("delete", "replace"))
    added = sum(j - i for tag, _s, _e, i, j in opcodes if tag in ("insert", "replace"))
    has_loss = any(tag in ("delete", "replace") for tag, _s, _e, _i, _j in opcodes)
    if not has_loss:
        return ("pure_insert_tail" if new.startswith(old) else "pure_insert_mid",
                removed, added)
    ratio = difflib.SequenceMatcher(None, old, new, autojunk=False).ratio()
    if ratio < similar:
        return "replaced", removed, added
    return "altered", removed, added


def main():
    argv = sys.argv[1:]
    base, head, limit, paths = "HEAD", None, 0.34, []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--base" and i + 1 < len(argv):
            base = argv[i + 1]
            i += 2
        elif a == "--head" and i + 1 < len(argv):
            head = argv[i + 1]
            i += 2
        elif a == "--similar" and i + 1 < len(argv):
            limit = float(argv[i + 1])
            i += 2
        elif a.startswith("-"):
            print("ERROR unknown option:", esc(a))
            return 2
        else:
            paths.append(a)
            i += 1

    rc, out, err = git(["git", "rev-parse", "--verify", "%s^{commit}" % base])
    if rc != 0:
        print("ERROR base revision unreadable:", esc(base),
              esc(err.decode("utf-8", "replace")[:120]))
        return 2
    base_sha = out.decode("ascii").strip()[:12]
    if head is not None:
        rc2, out2, err2 = git(["git", "rev-parse", "--verify", "%s^{commit}" % head])
        if rc2 != 0:
            print("ERROR head revision unreadable:", esc(head),
                  esc(err2.decode("utf-8", "replace")[:120]))
            return 2
        head_sha = out2.decode("ascii").strip()[:12]
    else:
        head_sha = "WORKTREE"

    list_args = ["git", "diff", "--name-only", "-z", ("%s" % base)]
    if head is not None:
        list_args.append(head)
    rc3, listing, err3 = git(list_args)
    if rc3 != 0:
        print("ERROR git diff listing failed:", esc(err3.decode("utf-8", "replace")[:160]))
        return 2
    listed = [p.decode("utf-8", "replace") for p in listing.split(b"\0") if p]
    if paths:
        wanted = set(paths)
        listed = [p for p in listed if p in wanted]
    if not listed:
        print("INV changed_files=0 (nothing differs between %s and %s)" % (base_sha, head_sha))
        print("INV verdict=REPORT_ONLY exit=0")
        return 0

    # Stable ids: the file names are Chinese in this repo and the console is cp936.
    fid = {}
    for p in listed:
        fid[p] = "F%02d" % (len(fid) + 1)

    totals = dict(insert=0, delete=0, rewrite_1x1=0, rewrite_nxn=0, mixed=0)
    subs = dict(pure_insert_tail=0, pure_insert_mid=0, altered=0, replaced=0,
                empty_side=0)
    per_file = {}
    by_bucket = {b: dict(files=0, rewrite=0, altered_or_replaced=0) for b in BUCKETS}
    growth = {}
    # Where each family member's number came from. "not-in-diff" is the only honest label for a
    # member this diff does not touch at all; the other four are set while walking the listing.
    member_state = dict((m, "not-in-diff") for m in LEDGER_FAMILY)

    print("== file id map (base=%s head=%s) ==" % (base_sha, head_sha))
    for p in listed:
        print("FILEID %s path=%s" % (fid[p], esc(p)))

    for p in listed:
        if not p.endswith(TEXT_SUFFIX):
            print("SKIP file=%s path=%s reason=not-a-text-suffix" % (fid[p], esc(p)))
            continue
        if head is None:
            cur_data = read_worktree(p)
            if cur_data is None:
                if p in LEDGER_FAMILY:
                    member_state[p] = "listed-but-absent-in-worktree"
                print("SKIP file=%s path=%s reason=absent-in-worktree" % (fid[p], esc(p)))
                continue
        else:
            cur_data, why = blob_of(head, p)
            if cur_data is None:
                if p in LEDGER_FAMILY:
                    member_state[p] = "listed-but-absent-in-head"
                print("SKIP file=%s path=%s reason=%s" % (fid[p], esc(p), why))
                continue
        base_data, why = blob_of(base, p)
        if base_data is None:
            # A newly added file: every line is an insertion, no in-place rewrite possible.
            if p in LEDGER_FAMILY:
                # Its base is 0 because git says the blob does not exist at base, not because this
                # script guessed; the whole file counts as growth of the family.
                growth[p] = (0, cur_data.count(b"\n"))
                member_state[p] = "created"
            print("NEWFILE file=%s path=%s base_state=%s cur_lines=%d"
                  % (fid[p], esc(p), why, len(to_lines(cur_data))))
            continue

        b_lines = to_lines(base_data)
        c_lines = to_lines(cur_data)
        # get_opcodes() walks the two files in order, so bi/ci below can be printed
        # as 1-based line numbers in the base and the current state.
        matcher = difflib.SequenceMatcher(None, b_lines, c_lines, autojunk=False)
        opcodes = matcher.get_opcodes()
        counts = dict(insert=0, delete=0, rewrite_1x1=0, rewrite_nxn=0, mixed=0)
        altered_here = 0
        for tag, bi, bj, ci, cj in opcodes:
            if tag == "equal":
                continue
            n_old, n_new = bj - bi, cj - ci
            if n_old and n_new and n_old == n_new:
                kind = "rewrite_1x1" if n_old == 1 else "rewrite_nxn"
            elif n_old and n_new:
                kind = "mixed"
            elif n_new:
                kind = "insert"
            else:
                kind = "delete"
            counts[kind] += 1
            totals[kind] += 1
            if kind in ("rewrite_1x1", "rewrite_nxn"):
                for off in range(n_old):
                    sub, removed, added = classify_pair(
                        b_lines[bi + off], c_lines[ci + off], limit)
                    subs[sub] += 1
                    if sub in ("altered", "replaced"):
                        altered_here += 1
                    print("HUNK file=%s kind=%s base_line=%d cur_line=%d sub=%s "
                          "old_chars=%d new_chars=%d removed=%d added=%d"
                          % (fid[p], kind, bi + off + 1, ci + off + 1, sub,
                             len(b_lines[bi + off].strip()), len(c_lines[ci + off].strip()),
                             removed, added))
            else:
                print("HUNK file=%s kind=%s base_line=%d cur_line=%d old_lines=%d new_lines=%d"
                      % (fid[p], kind, bi + 1, ci + 1, n_old, n_new))
        per_file[p] = counts
        b = bucket_of(p)
        by_bucket[b]["files"] += 1
        by_bucket[b]["rewrite"] += counts["rewrite_1x1"] + counts["rewrite_nxn"]
        by_bucket[b]["altered_or_replaced"] += altered_here
        if p in LEDGER_FAMILY:
            growth[p] = (base_data.count(b"\n"), cur_data.count(b"\n"))
            member_state[p] = "measured"
        print("FILE file=%s path=%s base_lines=%d cur_lines=%d base_cr=%d cur_cr=%d "
              "rewrite=%d altered_or_replaced=%d"
              % (fid[p], esc(p), len(b_lines), len(c_lines),
                 count_eol(base_data, "crlf"), count_eol(cur_data, "crlf"),
                 counts["rewrite_1x1"] + counts["rewrite_nxn"], altered_here))
        # its own line on purpose: the FILE line's format is quoted verbatim in the ledger,
        # so extending it here would make those registered readings unreplayable
        print("BUCKET file=%s bucket=%s" % (fid[p], b))

    rewrite_total = totals["rewrite_1x1"] + totals["rewrite_nxn"]
    print("== summary ==")
    print("INV changed_files=%d text_files=%d" % (len(listed), len(per_file)))
    print("INV insert=%d delete=%d rewrite_1x1=%d rewrite_nxn=%d mixed=%d"
          % (totals["insert"], totals["delete"], totals["rewrite_1x1"],
             totals["rewrite_nxn"], totals["mixed"]))
    print("INV rewrite_total=%d" % rewrite_total)
    print("INV pair_shapes: pure_insert_tail=%d pure_insert_mid=%d altered=%d replaced=%d "
          "empty_side=%d" % (subs["pure_insert_tail"], subs["pure_insert_mid"],
                             subs["altered"], subs["replaced"], subs["empty_side"]))
    print("INV kept_verbatim=%d edited_existing_text=%d"
          % (subs["pure_insert_tail"] + subs["pure_insert_mid"],
             subs["altered"] + subs["replaced"]))
    # Sixth-round review S-1: which population a red gate may judge is a per-category
    # decision, so print the buckets and the would-be verdict of every candidate scoping.
    print("== buckets (would-be verdicts; nothing here changes the exit code) ==")
    for name in BUCKETS:
        v = by_bucket[name]
        print("INV bucket=%s files=%d rewrite=%d altered_or_replaced=%d"
              % (name, v["files"], v["rewrite"], v["altered_or_replaced"]))
    md_altered = (by_bucket["ledger"]["altered_or_replaced"]
                  + by_bucket["other_doc"]["altered_or_replaced"])
    all_altered = sum(by_bucket[n]["altered_or_replaced"] for n in BUCKETS)
    ledger_altered = by_bucket["ledger"]["altered_or_replaced"]
    print("INV scoping=all_files altered_or_replaced=%d verdict_if_enabled=%s"
          % (all_altered, "RED" if all_altered else "GREEN"))
    print("INV scoping=all_md altered_or_replaced=%d verdict_if_enabled=%s"
          % (md_altered, "RED" if md_altered else "GREEN"))
    print("INV scoping=ledger_only altered_or_replaced=%d verdict_if_enabled=%s"
          % (ledger_altered, "RED" if ledger_altered else "GREEN"))
    if LEDGER_REL in growth:
        g_base, g_cur = growth[LEDGER_REL]
        delta = g_cur - g_base
        print("INV LEDGER_GROWTH base_nl=%d cur_nl=%d delta=%d trigger_more_than=%d "
              "split_project_due=%s (nl = newline count of the bytes; the FILE lines above "
              "print the split count, which is nl + 1)"
              % (g_base, g_cur, delta, SPLIT_TRIGGER_DELTA,
                 "YES" if delta > SPLIT_TRIGGER_DELTA else "NO"))
    else:
        print("INV LEDGER_GROWTH not_measured (ledger file not in this diff; delta unknown)")
    # The family line is what the split trigger is judged on (tools/ledger_size_gate.py): moving
    # rows from one member to the other moves +d on one side and -d on the other, so the SUM is
    # the only reading a cut cannot vote on. Every member prints with the source of its number.
    parts, fam = [], 0
    for member in LEDGER_FAMILY:
        d = (growth[member][1] - growth[member][0]) if member in growth else 0
        fam += d
        parts.append("%s=%+d[%s]" % (esc(member), d, member_state[member]))
    print("INV FAMILY_GROWTH %s sum_delta=%d trigger_more_than=%d split_project_due_if_family=%s "
          "(brackets say where each number came from: measured=two readable blobs, created=no base "
          "blob at all so the whole file counts, not-in-diff=0 by definition, listed-but-absent=the "
          "diff names it but one side has no bytes and this script does not invent a count for it)"
          % (" ".join(parts), fam, SPLIT_TRIGGER_DELTA,
             "YES" if fam > SPLIT_TRIGGER_DELTA else "NO"))
    print("INV note=rewrite_1x1 is what the whole-block prefix gate cannot see; "
          "sizing gate reads (file_lines, not line_numbers) stay unchanged by it")
    print("INV verdict=REPORT_ONLY exit=0 (no assertion made here)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:   # noqa: BLE001 - a crash must be visible, findings must not
        print("ERROR script failed:", type(exc).__name__, esc(str(exc))[:200])
        sys.exit(3)

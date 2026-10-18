# -*- coding: utf-8 -*-
"""Register-face shape gate: 附页的旧行只许追加，不许删改 -- and this says so in red.

Why this exists: the ledger has a sibling shape ruler (tools/inline_rewrite_check.py) but that ruler
is report-only by contract (INV verdict=REPORT_ONLY exit=0), and its per-edit classification (sub=...)
only runs inside rewrite blocks. 附页第四十五条 registered the consequence: when an in-cell append
lands next to newly appended rows, diff merges them into one replace block, and the annotated row
then appears in neither the ruler's HUNK lines nor its INV pair_shapes. 附页第七十一行 picked the
denominator (逐处, not 整枚 hunk). This gate is the red-capable half that row left open.

Its judgements are taken from the pre-change measurement, not from memory:
build/u44_probe/U66_appendix_history_census_3.txt (rc=0) read, over the whole history of this face,
rounds_measured=10 rounds_created=1 rounds_pure_append=6 rounds_hidden_appends=2
appends_total_opened_basis=5 appends_total_ruler_basis=3 violations_total=0
would_be_red_revlist=none appends_pipe_changed_total=0 appends_label_changed_total=0
appends_on_non_row_total=1 rounds_pure_append_at_tail=6.
Because violations_total=0, this gate installs with NO frozen baseline: every finding below is a
regression against the face's own history.

Judgements (each is one finding, any finding = RED):
  F1 old_line_destroyed   an old line is altered/replaced, or drops out of the pairing entirely
  F2 empty_side_pair      a paired old/new where one side is blank -- the pairing went onto filler
  F3 row_pipe_changed     an in-cell append onto a table row changed that row's cell count
  F4 row_label_changed    an in-cell append changed the row's first cell (its 编号/名称)
  F5 pure_round_not_at_tail  a round that touches no old line must add its rows at the table tail
  F6 pure_flag_disagrees  my per-edit "touches no old line" and git's own numstat removed==0 differ
  F7 kinds_disagree       my re-typed n_old/n_new kind ladder differs from the ruler's own HUNK kinds
  F8 unreadable_input   a pair could not be read, or git could not count it. In history mode that is
     one more finding (RED); in the explicit --base / --base-file modes the run returns 2 instead of
     printing a green verdict over bytes nobody read. Reading a face is never a silent skip.

F1 has two halves since U-135, and the split is the whole point of this section. A line that leaves
the active 附页 is destroyed UNLESS the archived volume holds that exact line in the same pair:

    relocated  the line's stripped bytes appear in the archive face at the pair's head side, and its
       first cell (its 条号) is NOT on the active face's new side. Consumed one-for-one from a
       Counter, so one archived copy can never excuse sixty-eight deletions.
    destroyed  everything else. Same finding text as before, same exit code.

Why an exact-byte match is enough to call it a move rather than a deletion: the plan's own rule is
that old rows stay on the page, so the only sanctioned way for a row to stop being on the active page
is for its bytes to be somewhere else. Checking the bytes is what makes "somewhere else" mean
"still registered", not "somewhere I claim". A row that was moved AND edited on the way out stays
F1 -- which is the correct reading, because the edit is what violated the rule, not the move.

Why the archive is judged as its own face and not just consulted: U-135 moved 68 rows (第三十二条 to
第九十九条) out of the active face into docs/对标差距推进计划-登记附表-归档一.md. If this gate only
learned to excuse them, the moved rows would land in a file nobody watches -- the exemption would be
a hole, not a cut. So every pair the archive exists in gets judged by the same ladder: its rows still
have to hold their cell count (F3) and their 条号 (F4), and lines that leave it are destroyed (F1).
Its own first line is not 第一条 and it has no 表头 lineage to defend, so a base side that does not
exist reads as the face's creation -- that is printed (archive_base=absent) and excused, never judged.

Why the pair list now covers both faces: face_commits() walks git log over the active face AND the
archive, so a round that only appends to the archive is still judged rather than invisible.

--no-archive turns the relocation half off (every moved row reads destroyed again). It exists because
an exemption nobody can switch off is an exemption nobody can trust; the teeth drive it as the arm
that proves the channel is load-bearing.

The non-row excuse is not a judgement and has no F number: this face also takes annotations on its
leading blockquote, which has no cell separator at all. Such an append counts as append_non_row and
is excused, so F3/F4 stay claims about table rows only (the census measured that shape once, as
appends_on_non_row_total=1).

Reads are face-scoped on purpose: the same census read rounds_pair_shapes_scope_mismatch=2, i.e. in
2 of 10 rounds the ruler's global INV pair_shapes tallies were not the appendix's numbers. This gate
never asserts against those global tallies; F7 compares the ruler's per-face HUNK lines only.

One definition is re-typed rather than imported: the n_old/n_new ladder lives inline in the ruler's
main(), and the ruler's bytes are pinned by other gates' baselines, so it stays untouched. F7 is
what keeps that re-typing honest.

Reproduce:
    python tools/register_shape_gate.py                              # whole history + pending worktree
    python tools/register_shape_gate.py --base REV [--head REV|WORKTREE]
    python tools/register_shape_gate.py --base-file A --head-file B   # what the teeth inject
    python tools/register_shape_gate.py --archive <rel path>          # name another archived volume
    python tools/register_shape_gate.py --base-file A --head-file B --archive-file C
    python tools/register_shape_gate.py --no-archive                  # DECLARED off: moved rows read
                                                                       # destroyed again
Exit: 0 GREEN, 1 RED (findings printed), 2 an input was unreadable (never a green), 3 crash.
"""
import difflib
import os
import subprocess
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

from inline_rewrite_check import (REGISTER_REL, blob_of, read_worktree, to_lines, esc,  # noqa: E402
                                   classify_pair)

RULER = os.path.join(HERE, "inline_rewrite_check.py")
SIMILAR = 0.34
KIND_KEYS = ("insert", "delete", "rewrite_1x1", "rewrite_nxn", "mixed")
APPEND_KINDS = ("pure_insert_tail", "pure_insert_mid")
# U-135: the volume the active 附页's oldest rows were moved into. Spelled here, not imported from
# tools/ledger_size_gate.py -- a gate must not depend on a sibling gate -- and the two spellings are
# compared by build/u44_probe/u135_cut_teeth.py, so renaming one cannot drift unnoticed.
ARCHIVE_REL = (u"docs/\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212-\u767b\u8bb0\u9644\u8868"
               u"-\u5f52\u6863\u4e00.md")


def arg_value(argv, i, name):
    """Option values may not start with '--': a switch eaten as a value is a silent mis-config."""
    if i + 1 >= len(argv):
        return None, "%s needs a value" % name
    value = argv[i + 1]
    if value.startswith("--"):
        return None, "%s got a switch-looking value (%s); pass the value as its own word" % (name, value)
    return value, None


def face_commits(paths=None):
    """Every commit that touched the active face or the archived volume, newest first.

    Both paths go to one git log because a round that only appends to the archive must still be
    judged -- judging the archive face only in pairs where the active face also moved would leave a
    hole exactly the size of the file the rows were moved into.
    """
    args = ["git", "log", "--format=%H", "--"]
    args.extend(paths if paths else [REGISTER_REL, ARCHIVE_REL])
    proc = subprocess.run(args,
                          cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return None, proc.stderr.decode("utf-8", "replace").strip()[:120]
    return [r for r in proc.stdout.decode("utf-8", "replace").replace("\r", "").split("\n")
            if r.strip()], None


def numstat_for(base, head, rel):
    """git's own line counts for one face -- the second half of F6, per face."""
    args = ["git", "diff", "--numstat"]
    if head in (None, "WORKTREE"):
        args.append(base)
    else:
        args.extend([base, head])
    args.extend(["--", rel])
    proc = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return None, None, proc.stderr.decode("utf-8", "replace").strip()[:100]
    out = proc.stdout.decode("utf-8", "replace").strip()
    if not out:
        return 0, 0, None
    parts = out.split(chr(9))
    try:
        return int(parts[0]), int(parts[1]), None
    except ValueError:
        return None, None, "unparsable numstat: %s" % esc(parts[0] if parts else "")


def parent_of(rev):
    proc = subprocess.run(["git", "rev-parse", "--verify", "%s^" % rev],
                          cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return None
    return proc.stdout.decode("utf-8", "replace").strip()


def kind_of(n_old, n_new):
    """The ruler's own kind rule (its n_old/n_new ladder), re-typed here -- F7 pins the agreement."""
    if n_old and n_new:
        if n_old == n_new:
            return "rewrite_1x1" if n_old == 1 else "rewrite_nxn"
        return "mixed"
    return "insert" if n_new else "delete"


def first_cell(line):
    return line.split(" | ")[0]


def record_lost(st, old_lines, oi_local, abs_oi, abs_ni, how):
    """One old-side line that no longer sits where it sat -- relocated, or destroyed.

    The pool is a Counter of the archive face's lines at this pair's head side. Consuming one copy
    per excused line is the point: without it a single archived line would excuse every deletion in
    the repository. A line whose 条号 still appears on the new side is NOT excused -- that shape is
    "the row was copied and rewritten", and the copy in the archive proves nobody moved anything out.
    """
    line = old_lines[oi_local]
    pool = st.get("pool")
    if pool is not None and pool.get(line, 0) > 0 and first_cell(line) not in st["new_cells"]:
        pool[line] -= 1
        if not pool[line]:
            del pool[line]
        st["relocated"] += 1
        st["relocated_rows"].append((abs_oi, abs_ni, first_cell(line)))
        return
    st["old_lost"] += 1
    st["destroyed"].append((abs_oi, abs_ni, how))


def pair_and_classify(old_lines, new_lines, by_position, st, offset, new_offset):
    """Pair old->new lines inside one changed block and run the ruler's predicate on each pair.

    by_position reproduces the ruler's rule (offset against offset, legal only at equal sizes).
    Otherwise the pairing is by least loss, because a changed block in this table is normally one
    annotated row plus several new rows; a positional pairing would call the annotated row a
    replacement and report a destroyed old line that is still on disk.

    offset and new_offset shift the block-relative positions up to file positions, which is what a
    reviewer greps. They are two arguments because a block has two different bases: the old half
    starts at i0 and the new half at j0, and any earlier inserted line makes j0 > i0. Sharing one
    base would print a new-side coordinate pointing at a different line than the one reported --
    harmless for every counter here, fatal for the person told to go look at that line.
    """
    pairs = []
    if by_position and len(old_lines) == len(new_lines):
        pairs = [(off, off) for off in range(len(old_lines))]
    else:
        cands = []
        for oi, ol in enumerate(old_lines):
            for ni, nl in enumerate(new_lines):
                sub, removed, added = classify_pair(ol, nl, SIMILAR)
                cands.append(((sub not in APPEND_KINDS, removed, added, removed + added, oi, ni),
                              oi, ni))
        cands.sort()
        used_old, used_new = set(), set()
        for _rank, oi, ni in cands:
            if oi in used_old or ni in used_new:
                continue
            used_old.add(oi)
            used_new.add(ni)
            pairs.append((oi, ni))
        pairs.sort()
    for oi, ni in pairs:
        sub, _removed, _added = classify_pair(old_lines[oi], new_lines[ni], SIMILAR)
        st["paired"] += 1
        if sub in APPEND_KINDS:
            st["append"] += 1
            if not old_lines[oi].startswith("|"):
                st["append_non_row"] += 1
                continue
            if new_lines[ni].count("|") != old_lines[oi].count("|"):
                st["row_pipe_changed"] += 1
                st["pipe_rows"].append((oi + offset, ni + new_offset))
            if first_cell(new_lines[ni]) != first_cell(old_lines[oi]):
                st["row_label_changed"] += 1
                st["label_rows"].append((oi + offset, ni + new_offset))
        elif sub == "empty_side":
            st["empty_side"] += 1
            st["empty_pairs"].append((oi + offset, ni + new_offset))
        else:
            record_lost(st, old_lines, oi, oi + offset, ni + new_offset, sub)
    matched_old = set(oi for oi, _ni in pairs)
    matched_new = set(ni for _oi, ni in pairs)
    for oi in range(len(old_lines)):
        if oi not in matched_old:
            record_lost(st, old_lines, oi, oi + offset, None, "unpaired")
    for ni in range(len(new_lines)):
        if ni not in matched_new:
            st["added"] += 1


def new_state():
    return dict(preserved=0, append=0, append_non_row=0, row_pipe_changed=0,
                row_label_changed=0, empty_side=0, old_lost=0, added=0, paired=0,
                relocated=0, blocks=0, no_change=False, pipe_rows=[], label_rows=[],
                empty_pairs=[], destroyed=[], relocated_rows=[],
                pool=None, new_cells=set())


def judge(old_text, new_text, pool=None):
    """One commit pair (or file pair) of the face -> tallies plus the kinds the ruler printed.

    pool: a Counter of the archived volume's lines at this pair's head side, or None to judge with no
    relocation channel. Only the active face is ever handed one -- rows leaving the archive have
    nowhere further sanctioned to go, so they stay plain deletions.
    """
    st = new_state()
    kinds = dict((k, 0) for k in KIND_KEYS)
    old_lines = [l.strip() for l in to_lines(old_text)]
    new_lines = [l.strip() for l in to_lines(new_text)]
    st["pool"] = pool
    st["new_cells"] = set(first_cell(l) for l in new_lines)
    first_change = None
    for tag, i0, i1, j0, j1 in difflib.SequenceMatcher(None, old_lines, new_lines,
                                                       autojunk=False).get_opcodes():
        if tag == "equal":
            st["preserved"] += i1 - i0
            continue
        if first_change is None:
            first_change = i0
        kind = kind_of(i1 - i0, j1 - j0)
        kinds[kind] += 1
        st["blocks"] += 1
        if kind == "insert":
            st["added"] += j1 - j0
        elif kind == "delete":
            for oi in range(i0, i1):
                record_lost(st, old_lines, oi, oi, None, "delete")
        else:
            pair_and_classify(old_lines[i0:i1], new_lines[j0:j1], (i1 - i0) == (j1 - j0), st, i0, j0)
    last_content = -1
    for idx, line in enumerate(old_lines):
        if line:
            last_content = idx
    st["first_change"] = first_change
    st["last_content"] = last_content
    st["at_tail"] = first_change is not None and first_change > last_content
    st["kinds"] = kinds
    st["no_change"] = (old_lines == new_lines)
    st["pure_new_rows"] = (st["old_lost"] == 0 and st["empty_side"] == 0 and st["append"] == 0
                           and st["relocated"] == 0)
    return st


def ruler_kinds(base, head, rel=REGISTER_REL):
    """The ruler's own per-face HUNK kind tallies for this pair, or (None, reason)."""
    if rel != REGISTER_REL:
        # The ruler judges LEDGER_FAMILY (the plan and its active appendix). The archived volume is
        # not in that family, so its HUNK tallies do not exist to be compared -- reporting the
        # absence is the honest reading, comparing against the active face's tallies would be a
        # fabricated F7.
        return None, "face_not_in_ruler_family"
    args = [sys.executable, RULER, "--base", base]
    if head not in (None, "WORKTREE"):
        args.extend(["--head", head])
    proc = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    want = esc(rel)
    fid = None
    kinds = dict((k, 0) for k in KIND_KEYS)
    seen = False
    for raw in proc.stdout.decode("utf-8", "replace").split(chr(10)):
        line = raw.strip()
        if line.startswith("FILEID ") and line.endswith("path=" + want):
            fid = line.split()[1]
            seen = True
        elif fid and line.startswith("HUNK file=" + fid + " "):
            body = {}
            for chunk in line.split()[1:]:
                if "=" in chunk:
                    k, v = chunk.split("=", 1)
                    body[k] = v
            kinds[body["kind"]] += 1
    if not seen:
        return None, "face_not_in_ruler_output rc=%d" % proc.returncode
    return kinds, None


def report(label, base, head, st, ns_add, ns_del, findings):
    print("[RG] pair=%s base=%s head=%s | blocks=%d preserved=%d append=%d append_non_row=%d "
          "old_lost=%d relocated=%d added=%d empty_side=%d row_pipe_changed=%d "
          "row_label_changed=%d"
          % (label, base[:9] if base else "-", (head or "WORKTREE")[:9], st["blocks"],
             st["preserved"], st["append"], st["append_non_row"], st["old_lost"],
             st["relocated"], st["added"],
             st["empty_side"], st["row_pipe_changed"], st["row_label_changed"]))
    if st["relocated"]:
        sample = [cell for _oi, _ni, cell in st["relocated_rows"][:4]]
        print("[RG] pair=%s relocated_names=%s shown=%d of=%d note=each_excused_line_matched_"
              "byte_identical_in_the_archive"
              % (label, esc(",".join(sample)), len(sample), st["relocated"]))
    print("[RG] pair=%s position changed=%s first_change_base_index=%s base_last_content_index=%d "
          "at_tail=%s | git numstat_added=%s numstat_removed=%s pure_new_rows=%s"
          % (label, "no" if st["no_change"] else "yes", st["first_change"], st["last_content"],
             "yes" if st["at_tail"] else "no",
             ns_add, ns_del, "yes" if st["pure_new_rows"] else "no"))
    for oi, ni, sub in st["destroyed"][:8]:
        findings.append("F1 old_line_destroyed pair=%s base_line_index=%d new_line_index=%s how=%s"
                        % (label, oi + 1, "-" if ni is None else ni + 1, sub))
    if len(st["destroyed"]) > 8:
        findings.append("F1 old_line_destroyed pair=%s more_destroyed=%d"
                        % (label, len(st["destroyed"]) - 8))
    for oi, ni in st["empty_pairs"][:8]:
        findings.append("F2 empty_side_pair pair=%s base_line_index=%d new_line_index=%d"
                        % (label, oi + 1, ni + 1))
    for oi, ni in st["pipe_rows"][:8]:
        findings.append("F3 row_pipe_changed pair=%s base_line_index=%d new_line_index=%d"
                        % (label, oi + 1, ni + 1))
    for oi, ni in st["label_rows"][:8]:
        findings.append("F4 row_label_changed pair=%s base_line_index=%d new_line_index=%d"
                        % (label, oi + 1, ni + 1))
    if st["pure_new_rows"] and not st["at_tail"] and not st["no_change"]:
        findings.append("F5 pure_round_not_at_tail pair=%s first_change_base_index=%s "
                        "base_last_content_index=%d"
                        % (label, st["first_change"], st["last_content"]))
    if ns_del is not None and not st["no_change"]:
        git_pure = (ns_del == 0)
        if git_pure != st["pure_new_rows"]:
            findings.append("F6 pure_flag_disagrees pair=%s numstat_removed=%s per_edit_pure=%s"
                            % (label, ns_del, "yes" if st["pure_new_rows"] else "no"))


FACE_CACHE = {}


def read_face(rev, rel):
    """One face's bytes at a rev (or in the worktree), through a cache.

    The archive is read for every pair the gate walks, and the history walk shows each rev twice
    (as one pair's head and the next pair's base), so an uncached read would be a needless
    subprocess per pair. WORKTREE is not cached: a driver can write the file between reads.
    """
    if rev in (None, "WORKTREE"):
        text = read_worktree(rel)
        return text, (None if text is not None else "worktree-file-absent")
    key = (rev, rel)
    if key not in FACE_CACHE:
        FACE_CACHE[key] = blob_of(rev, rel)
    return FACE_CACHE[key]


def archive_pool(head, rel):
    """The Counter of lines the archived volume holds at this pair's head side, or (None, reason).

    An archive that does not exist on this side is not an error: it is the state before the first
    cut, and every row that leaves the active face is then read as destroyed -- which is the
    fail-closed direction, not a silent pass.
    """
    text, err = read_face(head, rel)
    if text is None:
        return None, err or "absent"
    return Counter(l.strip() for l in to_lines(text)), None


def judge_rev_pair(label, base, head, findings, archive_rel=ARCHIVE_REL, use_archive=True):
    old, err_old = read_face(base, REGISTER_REL)
    new, err_new = read_face(head, REGISTER_REL)
    if old is None or new is None:
        findings.append("F8 unreadable pair=%s base=%s head=%s reasons=%s,%s"
                        % (label, base[:9] if base else "-", str(head)[:9], err_old, err_new))
        return None
    pool = None
    pool_total = None
    pool_note = "off"
    if use_archive:
        pool, pool_err = archive_pool(head, archive_rel)
        if pool is not None:
            pool_total = sum(pool.values())
            pool_note = "present"
        else:
            pool_note = "absent reason=%s" % esc(str(pool_err))
    st = judge(old, new, pool)
    if use_archive:
        print("[RG] pair=%s archive=%s archive_side=%s pool=%s pool_before=%s pool_left=%s"
              % (label, esc(archive_rel), str(head or "WORKTREE")[:9], pool_note,
                 pool_total if pool_total is not None else "none",
                 sum(pool.values()) if pool is not None else "none"))
    ns_add, ns_del, ns_err = numstat_for(base, head, REGISTER_REL)
    if ns_err:
        findings.append("F8 unreadable pair=%s numstat=%s" % (label, esc(ns_err)))
    report(label, base, head, st, ns_add, ns_del, findings)
    rk, rk_err = ruler_kinds(base, head, REGISTER_REL)
    if rk_err:
        print("[RG] pair=%s ruler_cross_check=skipped reason=%s" % (label, esc(rk_err)))
    elif rk != st["kinds"]:
        findings.append("F7 kinds_disagree pair=%s ruler=%s gate=%s" % (label, rk, st["kinds"]))
    else:
        print("[RG] pair=%s ruler_cross_check=agree kinds=%s" % (label, st["kinds"]))
    return st


def judge_archive_pair(label, base, head, findings, archive_rel=ARCHIVE_REL):
    """The archived volume judged by the same ladder, so a moved row does not land in a file nobody
    watches. Returns None when neither side of the pair holds the archive (nothing to judge there).
    """
    old, err_old = read_face(base, archive_rel)
    new, err_new = read_face(head, archive_rel)
    if old is None and new is None:
        return None
    if new is None:
        findings.append("F8 unreadable pair=%s face=archive head=%s reason=%s"
                        % (label, str(head)[:9], esc(str(err_new))))
        return None
    created = old is None
    if created:
        old = b""
        print("[RG] pair=%s face=archive archive_base=absent creation=excused "
              "reason=no_old_line_on_the_archive_side_yet" % label)
    st = judge(old, new)
    if st["no_change"] and not created:
        return None
    ns_add, ns_del, ns_err = numstat_for(base, head, archive_rel)
    if ns_err:
        findings.append("F8 unreadable pair=%s face=archive numstat=%s" % (label, esc(ns_err)))
    report("archive#" + label, base, head, st, ns_add, ns_del, findings)
    rk, rk_err = ruler_kinds(base, head, archive_rel)
    if rk_err:
        print("[RG] pair=%s face=archive ruler_cross_check=skipped reason=%s"
              % (label, esc(rk_err)))
    elif rk != st["kinds"]:
        findings.append("F7 kinds_disagree pair=%s face=archive ruler=%s gate=%s"
                        % (label, rk, st["kinds"]))
    return st


def judge_file_pair(base_path, head_path, findings, archive_path=None):
    try:
        with open(base_path, "rb") as handle:
            old = handle.read()
        with open(head_path, "rb") as handle:
            new = handle.read()
    except IOError as exc:
        findings.append("F8 unreadable file_pair=%s" % esc(str(exc)[:120]))
        return None
    pool = None
    pool_total = None
    if archive_path:
        try:
            with open(archive_path, "rb") as handle:
                pool = Counter(l.strip() for l in to_lines(handle.read()))
            pool_total = sum(pool.values())
        except IOError as exc:
            findings.append("F8 unreadable archive_file=%s" % esc(str(exc)[:120]))
            return None
    st = judge(old, new, pool)
    # The Counter is consumed as rows are excused, so the size BEFORE the judgement and the residue
    # AFTER it are two different numbers and both are printed -- one name for both would be the
    # same mistake as 第八十八行 registered for signals= versus any_signal.
    print("[RG] pair=file base=%s head=%s archive=%s pool_before=%s pool_left=%s "
          "ruler_cross_check=skipped_reason=pair_is_not_two_revs"
          % (os.path.basename(base_path), os.path.basename(head_path),
             os.path.basename(archive_path) if archive_path else "none",
             pool_total if pool is not None else "off",
             sum(pool.values()) if pool is not None else "off")
          )
    report("file", "", "", st, None, None, findings)
    return st


def main(argv):
    base = head = base_file = head_file = None
    archive_file = None
    archive_rel = ARCHIVE_REL
    use_archive = True
    overrides = []
    mode = "history"
    i = 0
    while i < len(argv):
        a = argv[i]
        # --opt=value and "--opt value" are the same switch (tools/ledger_size_gate.py established the
        # rule: a command printed in a delivered document has to be a command the parser accepts).
        name, sep, inline = a.partition("=") if a.startswith("--") else (a, "", "")
        if name in ("--base", "--head", "--base-file", "--head-file", "--archive", "--archive-file"):
            value = inline if sep else None
            if not sep:
                value, err = arg_value(argv, i, name)
                if err:
                    print("[RG-ABORT] %s" % esc(err))
                    return 2
                i += 2
            else:
                i += 1
            if name == "--base":
                base, mode = value, "pair"
            elif name == "--head":
                head = value
            elif name == "--base-file":
                base_file, mode = value, "files"
            elif name == "--head-file":
                head_file = value
            elif name == "--archive":
                archive_rel = value
                overrides.append("archive=%s" % value)
            else:
                archive_file = value
                overrides.append("archive_file=%s" % value)
            continue
        if a == "--no-archive":
            use_archive = False
            overrides.append("no_archive=declared")
            i += 1
            continue
        if a in ("--help", "-h"):
            print(__doc__)
            return 0
        print("[RG-ABORT] unknown argument %s" % esc(a))
        return 2
    if not use_archive and (archive_file or archive_rel != ARCHIVE_REL):
        print("[RG-ABORT] --no-archive cannot also name an archive; pick one")
        return 2
    revs, err = face_commits([REGISTER_REL, archive_rel] if use_archive else [REGISTER_REL])
    if err:
        print("[RG-ABORT] git log failed: %s" % esc(err))
        return 2
    print("[RG] face=%s archive=%s use_archive=%s commits_touching=%d mode=%s"
          % (esc(REGISTER_REL), esc(archive_rel if use_archive else "off"),
             "yes" if use_archive else "no", len(revs), mode))
    findings = []
    states = []
    archive_states = []
    created = []
    if mode == "files":
        if not (base_file and head_file):
            print("[RG-ABORT] --base-file needs --head-file")
            return 2
        one = judge_file_pair(base_file, head_file, findings,
                              archive_file if use_archive else None)
        if one is None:
            return 2
        states = [one]
    elif mode == "pair":
        if base is None:
            print("[RG-ABORT] mode=pair needs --base")
            return 2
        one = judge_rev_pair("explicit", base, head, findings, archive_rel, use_archive)
        if one is None:
            return 2
        states = [one]
        if use_archive:
            two = judge_archive_pair("explicit", base, head, findings, archive_rel)
            if two is not None:
                archive_states.append(two)
    else:
        head_sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                  stdout=subprocess.PIPE).stdout.decode("ascii", "replace").strip()
        worktree = read_worktree(REGISTER_REL)
        blob_head, _err = read_face(head_sha, REGISTER_REL)
        lines_differ = (worktree is not None and blob_head is not None
                        and to_lines(worktree) != to_lines(blob_head))
        bytes_differ = (worktree is not None and worktree != blob_head)
        pending = lines_differ
        for rev in reversed(revs):
            par = parent_of(rev)
            if par is None:
                created.append(rev[:9])
                print("[RG] rev=%s parent=NONE face_created_here excused=no_old_line_to_preserve"
                      % rev[:9])
                continue
            _base_blob, base_err = read_face(par, REGISTER_REL)
            if base_err == "missing":
                created.append(rev[:9])
                print("[RG] rev=%s parent=%s face_created_here base_blob=missing "
                      "excused=no_old_line_to_preserve" % (rev[:9], par[:9]))
                continue
            one = judge_rev_pair(rev[:9], par, rev, findings, archive_rel, use_archive)
            if one is not None:
                states.append(one)
            if use_archive:
                two = judge_archive_pair(rev[:9], par, rev, findings, archive_rel)
                if two is not None:
                    archive_states.append(two)
        if pending:
            one = judge_rev_pair("worktree", head_sha, "WORKTREE", findings,
                                 archive_rel, use_archive)
            if one is not None:
                states.append(one)
            if use_archive:
                two = judge_archive_pair("worktree", head_sha, "WORKTREE", findings, archive_rel)
                if two is not None:
                    archive_states.append(two)
        print("[RG] pending_worktree_pair=%s worktree_lines_vs_head=%s worktree_bytes_ne_head=%s "
              "(an eol-only difference is not a round; byte health is repo_hygiene's job)"
              % ("yes" if pending else "no", "differ" if lines_differ else "equal",
                 "yes" if bytes_differ else "no"))
    if not states:
        print("[RG-VERDICT] rounds_judged=0 verdict=NO_ROUNDS")
        return 2
    print("[RG-INV] rounds_judged=%d rounds_no_change=%d rounds_created_excused=%d "
          "destroyed_total=%d relocated_total=%d empty_side_total=%d "
          "appends_total=%d append_non_row_total=%d row_pipe_changed_total=%d "
          "row_label_changed_total=%d pure_rounds=%d pure_rounds_at_tail=%d"
          % (len(states), sum(1 for s in states if s["no_change"]), len(created),
             sum(s["old_lost"] for s in states), sum(s["relocated"] for s in states),
             sum(s["empty_side"] for s in states),
             sum(s["append"] for s in states),
             sum(s["append_non_row"] for s in states), sum(s["row_pipe_changed"] for s in states),
             sum(s["row_label_changed"] for s in states),
             sum(1 for s in states if s["pure_new_rows"]),
             sum(1 for s in states if s["pure_new_rows"] and s["at_tail"])))
    # The archived volume is its OWN population. Folding its tallies into the lines above would give
    # one name two denominators -- the exact shape 第八十八行 registered about signals= versus
    # any_signal -- so it prints beside them, never inside them.
    print("[RG-INV] archive_rounds_judged=%d archive_destroyed_total=%d archive_relocated_total=%d "
          "archive_appends_total=%d archive_row_pipe_changed_total=%d "
          "archive_row_label_changed_total=%d archive_empty_side_total=%d"
          % (len(archive_states),
             sum(s["old_lost"] for s in archive_states),
             sum(s["relocated"] for s in archive_states),
             sum(s["append"] for s in archive_states),
             sum(s["row_pipe_changed"] for s in archive_states),
             sum(s["row_label_changed"] for s in archive_states),
             sum(s["empty_side"] for s in archive_states)))
    print("[RG] overrides_used=%s" % (esc(" ".join(overrides)) if overrides else "none"))
    for text in findings:
        print("[RG-FIND] %s" % esc(text))
    verdict = "GREEN" if not findings else "RED"
    print("[RG-VERDICT] findings=%d verdict=%s" % (len(findings), verdict))
    return 0 if not findings else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as exc:
        print("ERROR script failed: %s %s" % (type(exc).__name__, str(exc)[:200]))
        sys.exit(3)

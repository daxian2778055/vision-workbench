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
Exit: 0 GREEN, 1 RED (findings printed), 2 an input was unreadable (never a green), 3 crash.
"""
import difflib
import os
import subprocess
import sys

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


def arg_value(argv, i, name):
    """Option values may not start with '--': a switch eaten as a value is a silent mis-config."""
    if i + 1 >= len(argv):
        return None, "%s needs a value" % name
    value = argv[i + 1]
    if value.startswith("--"):
        return None, "%s got a switch-looking value (%s); pass the value as its own word" % (name, value)
    return value, None


def face_commits():
    proc = subprocess.run(["git", "log", "--format=%H", "--", REGISTER_REL],
                          cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return None, proc.stderr.decode("utf-8", "replace").strip()[:120]
    return [r for r in proc.stdout.decode("utf-8", "replace").replace("\r", "").split("\n")
            if r.strip()], None


def parent_of(rev):
    proc = subprocess.run(["git", "rev-parse", "--verify", "%s^" % rev],
                          cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0:
        return None
    return proc.stdout.decode("utf-8", "replace").strip()


def numstat(base, head):
    """git's own line counts for this face only -- independent of the pairing below."""
    args = ["git", "diff", "--numstat"]
    if head in (None, "WORKTREE"):
        args.append(base)
    else:
        args.extend([base, head])
    args.extend(["--", REGISTER_REL])
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


def kind_of(n_old, n_new):
    """The ruler's own kind rule (its n_old/n_new ladder), re-typed here -- F7 pins the agreement."""
    if n_old and n_new:
        if n_old == n_new:
            return "rewrite_1x1" if n_old == 1 else "rewrite_nxn"
        return "mixed"
    return "insert" if n_new else "delete"


def first_cell(line):
    return line.split(" | ")[0]


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
            st["old_lost"] += 1
            st["destroyed"].append((oi + offset, ni + new_offset, sub))
    matched_old = set(oi for oi, _ni in pairs)
    matched_new = set(ni for _oi, ni in pairs)
    for oi in range(len(old_lines)):
        if oi not in matched_old:
            st["old_lost"] += 1
            st["destroyed"].append((oi + offset, None, "unpaired"))
    for ni in range(len(new_lines)):
        if ni not in matched_new:
            st["added"] += 1


def new_state():
    return dict(preserved=0, append=0, append_non_row=0, row_pipe_changed=0,
                row_label_changed=0, empty_side=0, old_lost=0, added=0, paired=0,
                blocks=0, no_change=False, pipe_rows=[], label_rows=[], empty_pairs=[],
                destroyed=[])


def judge(old_text, new_text):
    """One commit pair (or file pair) of the face -> tallies plus the kinds the ruler printed."""
    st = new_state()
    kinds = dict((k, 0) for k in KIND_KEYS)
    old_lines = [l.strip() for l in to_lines(old_text)]
    new_lines = [l.strip() for l in to_lines(new_text)]
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
            st["old_lost"] += i1 - i0
            for oi in range(i0, i1):
                st["destroyed"].append((oi, None, "delete"))
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
    st["pure_new_rows"] = (st["old_lost"] == 0 and st["empty_side"] == 0 and st["append"] == 0)
    return st


def ruler_kinds(base, head):
    """The ruler's own per-face HUNK kind tallies for this pair, or (None, reason)."""
    args = [sys.executable, RULER, "--base", base]
    if head not in (None, "WORKTREE"):
        args.extend(["--head", head])
    proc = subprocess.run(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    want = esc(REGISTER_REL)
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
          "old_lost=%d added=%d empty_side=%d row_pipe_changed=%d row_label_changed=%d"
          % (label, base[:9] if base else "-", (head or "WORKTREE")[:9], st["blocks"],
             st["preserved"], st["append"], st["append_non_row"], st["old_lost"], st["added"],
             st["empty_side"], st["row_pipe_changed"], st["row_label_changed"]))
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


def judge_rev_pair(label, base, head, findings):
    old, err_old = blob_of(base, REGISTER_REL)
    if head in (None, "WORKTREE"):
        new = read_worktree(REGISTER_REL)
        err_new = None if new is not None else "worktree-file-absent"
    else:
        new, err_new = blob_of(head, REGISTER_REL)
    if old is None or new is None:
        findings.append("F8 unreadable pair=%s base=%s head=%s reasons=%s,%s"
                        % (label, base[:9], str(head)[:9], err_old, err_new))
        return None
    st = judge(old, new)
    ns_add, ns_del, ns_err = numstat(base, head)
    if ns_err:
        findings.append("F8 unreadable pair=%s numstat=%s" % (label, esc(ns_err)))
    report(label, base, head, st, ns_add, ns_del, findings)
    rk, rk_err = ruler_kinds(base, head)
    if rk_err:
        print("[RG] pair=%s ruler_cross_check=skipped reason=%s" % (label, esc(rk_err)))
    elif rk != st["kinds"]:
        findings.append("F7 kinds_disagree pair=%s ruler=%s gate=%s" % (label, rk, st["kinds"]))
    else:
        print("[RG] pair=%s ruler_cross_check=agree kinds=%s" % (label, st["kinds"]))
    return st


def judge_file_pair(base_path, head_path, findings):
    try:
        with open(base_path, "rb") as handle:
            old = handle.read()
        with open(head_path, "rb") as handle:
            new = handle.read()
    except IOError as exc:
        findings.append("F8 unreadable file_pair=%s" % esc(str(exc)[:120]))
        return None
    st = judge(old, new)
    print("[RG] pair=file base=%s head=%s ruler_cross_check=skipped_reason=pair_is_not_two_revs"
          % (os.path.basename(base_path), os.path.basename(head_path)))
    report("file", "", "", st, None, None, findings)
    return st


def main(argv):
    base = head = base_file = head_file = None
    mode = "history"
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--base", "--head", "--base-file", "--head-file"):
            name = a
            value, err = arg_value(argv, i, name)
            if err:
                print("[RG-ABORT] %s" % esc(err))
                return 2
            if name == "--base":
                base, mode = value, "pair"
            elif name == "--head":
                head = value
            elif name == "--base-file":
                base_file, mode = value, "files"
            else:
                head_file = value
            i += 2
            continue
        if a in ("--help", "-h"):
            print(__doc__)
            return 0
        print("[RG-ABORT] unknown argument %s" % esc(a))
        return 2
    revs, err = face_commits()
    if err:
        print("[RG-ABORT] git log failed: %s" % esc(err))
        return 2
    print("[RG] face=%s commits_touching=%d mode=%s" % (esc(REGISTER_REL), len(revs), mode))
    findings = []
    states = []
    created = []
    if mode == "files":
        if not (base_file and head_file):
            print("[RG-ABORT] --base-file needs --head-file")
            return 2
        one = judge_file_pair(base_file, head_file, findings)
        if one is None:
            return 2
        states = [one]
    elif mode == "pair":
        if base is None:
            print("[RG-ABORT] mode=pair needs --base")
            return 2
        one = judge_rev_pair("explicit", base, head, findings)
        if one is None:
            return 2
        states = [one]
    else:
        head_sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                  stdout=subprocess.PIPE).stdout.decode("ascii", "replace").strip()
        worktree = read_worktree(REGISTER_REL)
        blob_head, _err = blob_of(head_sha, REGISTER_REL)
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
            _base_blob, base_err = blob_of(par, REGISTER_REL)
            if base_err == "missing":
                created.append(rev[:9])
                print("[RG] rev=%s parent=%s face_created_here base_blob=missing "
                      "excused=no_old_line_to_preserve" % (rev[:9], par[:9]))
                continue
            one = judge_rev_pair(rev[:9], par, rev, findings)
            if one is not None:
                states.append(one)
        if pending:
            one = judge_rev_pair("worktree", head_sha, "WORKTREE", findings)
            if one is not None:
                states.append(one)
        print("[RG] pending_worktree_pair=%s worktree_lines_vs_head=%s worktree_bytes_ne_head=%s "
              "(an eol-only difference is not a round; byte health is repo_hygiene's job)"
              % ("yes" if pending else "no", "differ" if lines_differ else "equal",
                 "yes" if bytes_differ else "no"))
    if not states:
        print("[RG-VERDICT] rounds_judged=0 verdict=NO_ROUNDS")
        return 2
    print("[RG-INV] rounds_judged=%d rounds_no_change=%d rounds_created_excused=%d "
          "destroyed_total=%d empty_side_total=%d "
          "appends_total=%d append_non_row_total=%d row_pipe_changed_total=%d row_label_changed_total=%d "
          "pure_rounds=%d pure_rounds_at_tail=%d"
          % (len(states), sum(1 for s in states if s["no_change"]), len(created),
             sum(s["old_lost"] for s in states), sum(s["empty_side"] for s in states),
             sum(s["append"] for s in states),
             sum(s["append_non_row"] for s in states), sum(s["row_pipe_changed"] for s in states),
             sum(s["row_label_changed"] for s in states),
             sum(1 for s in states if s["pure_new_rows"]),
             sum(1 for s in states if s["pure_new_rows"] and s["at_tail"])))
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

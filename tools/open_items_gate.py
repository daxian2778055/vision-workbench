# -*- coding: utf-8 -*-
"""open_items_gate.py -- the "still open" list as a reading the host takes, not prose in a cell.

Why this exists
---------------
附页 carries a "still open" list in its last row, in prose. U-111 measured every item on it and found
8 of 11 measurably closed, 1 with no object to measure at all, and #368 -- open for about thirty
rounds -- described as belonging to tools/ledger_size_gate.py, which has no --face option to describe.
Prose does not change on its own, so the list rots, and the rot is invisible until somebody
re-measures it by hand. This file makes the re-measurement something the host does on every run.

Three states, and the difference matters
----------------------------------------
  enforced  the item is closed and must stay closed. Re-measured every run; any mismatch is a
            finding and the host goes red. This is the part that stops a closure from quietly
            rotting back the way the list did.
  open      the item is genuinely still open (or is a reading, not a defect). Re-measured and
            PRINTED every run so the register carries a current number, but it never moves the
            verdict: a known-red item in the host is exactly the shape that made U-109's host
            permanently red.
  local     the measurement needs a file that is git-ignored (build/u44_probe/...), so a fresh
            clone has nothing to measure. Printed as unmeasurable_here rather than read as green.

Fail-closed, deliberately
-------------------------
A gate that cannot run must never read as clean. If the registry is malformed (duplicate id, unknown
state, an enforced entry whose command cannot run), that is a finding, not a pass. The first version of
the row-text lint shipped with an unguarded recursion and a CLI that printed nothing on the crash; the
teeth read that as "0 findings", which is fail-open and worse than a miss.

Deliberately out of scope
------------------------
This gate does not decide what the open items ARE -- that stays prose in 附页. It re-measures the
items this file names and reports. An item nobody registered here is invisible to it; the way to add
one is to add a line to ITEMS below, next to the measurement that closes or keeps it open.

Usage:
    python tools/open_items_gate.py             # measure everything, print readings, judge
    python tools/open_items_gate.py --list      # print the registry and exit, measure nothing
    python tools/open_items_gate.py --registry <path>   # measure a registry copy (the teeth do this)

Exit codes: 0 = every enforced item re-measured as closed; 1 = a finding; 2 = the gate itself could
not run.
"""
import inspect
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
PY = sys.executable

STATES = ("enforced", "open", "local")
PATH_LITERAL_RX = re.compile(r"[\"']((?:tools|src|include|tests|docs)/[A-Za-z0-9_./\-]+\.[A-Za-z0-9]+)[\"']")
IDENT_RX = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


class Item(object):
    """One registered item.

    id       stable handle; it is what a finding names and what --list prints
    state    one of STATES, see the module docstring
    argv     the command that re-measures it, relative to the repo root
    want_rc  for enforced: the rc that means "still closed"
    needle   for enforced: a substring that must appear in the output (None = rc alone)
    file     for local: the git-ignored path whose presence makes it measurable
    note     one line, ASCII, saying what the reading means
    """

    def __init__(self, id, state, argv, want_rc, needle, file, note, read=None, subjects=()):
        self.id = id
        self.state = state
        self.argv = argv
        self.want_rc = want_rc
        self.needle = needle
        self.file = file
        self.note = note
        # for open/local items: which output line is the reading worth printing. Without it the
        # reading is whichever line came last, which for the size gate is its own "verdict=GREEN" --
        # true, and says nothing about the number the item is about.
        self.read = read
        # N4 (U-114): the tracked files this row claims to guard. An enforced row must declare at
        # least one and the gate checks that its measurement actually reads each one. This is the only
        # thing that catches "the needle is perfectly its own, and the row still measures the wrong
        # subject" -- which is the one failure N1 is blind to by construction.
        #
        # Declared as a field rather than mined out of `note` on purpose. An earlier attempt read the
        # note and compared: it reported zero mismatches on the whole table, and the reason was that
        # no note names a tracked file at all -- the check was vacuously green, i.e. it could not have
        # caught anything. Prose is for readers; a declaration is the machine contract.
        self.subjects = tuple(subjects)

    def validate(self):
        if self.state not in STATES:
            return "unknown state %r (want one of %s)" % (self.state, ",".join(STATES))
        if not re.match(r"^[a-z0-9_]+$", self.id or ""):
            return "id %r is not lowercase-with-underscores" % (self.id,)
        if self.state == "local":
            if not self.file:
                return "state=local needs file"
            if not self.argv:
                return "state=local needs an argv to run once the file is there"
            if self.needle is not None:
                # N3 (U-113): a needle on a non-enforced item is dead config. It reads like the item is
                # checked against something; it is never consulted, because only enforced items consult
                # their needle. Someone who meant to enforce it typed the wrong state, or pasted a row
                # and forgot to move it -- either way the honest thing is to refuse the row, not to
                # carry a needle that looks armed and is not.
                return "state=local with a needle: nothing reads it; enforce it or drop the needle"
            return None
        if self.state == "open" and self.needle is not None:
            return "state=open with a needle: nothing reads it; enforce it or drop the needle"
        if not self.argv:
            return "argv is empty"
        if not isinstance(self.want_rc, int):
            return "want_rc is not an int"
        if self.state == "enforced" and self.needle is None and self.want_rc == 0:
            return "enforced with want_rc=0 and no needle would pass on any output at all"
        if self.state == "enforced" and not self.subjects:
            # N4: an enforced row with no declared subject cannot be checked for measuring the wrong
            # one at all. That is exactly the row N1 cannot catch, so refusing it is the whole point.
            return ("enforced with no declared subject: nothing can check that this row measures "
                    "what it claims to guard")
        for s in self.subjects:
            if not isinstance(s, str) or not s:
                return "subject %r is not a non-empty string" % (s,)
            if s.startswith("build/"):
                return ("subject %r is under build/ (git-ignored): an enforced row must guard a "
                        "tracked file, or a fresh clone has nothing to guard" % s)
        return None


GATE = "tools/log_citation_gate.py"

# NOTE: the three probe argvs below name this file by its repo-relative path, never through
# os.path.join(HERE, ...). HERE comes from __file__, and the teeth load this registry as a COPY
# (--registry), so the copys own __file__ makes HERE build/u44_probe -- a self-referential probe
# then points at a file that is not there and two enforced items read REGRESSED for a reason that
# had nothing to do with their subject. run() uses cwd=ROOT, so a repo-relative path is
# independent of __file__ and the copy and the shipped file read the same string.
CI = "tools/ci.ps1"

ITEMS = [
    Item("wording_slot_holds_switch", "enforced",
         [PY, "-X", "utf8", GATE, "--face=--no-register"], 2,
         "its slot holds switch-shaped text", None,
         "closed in U-111: the equals spelling says what actually happened instead of looking the "
         "token up in the face list. The ERROR unknown face: prefix is byte-identical and pinned.",
         subjects=("tools/log_citation_gate.py",)),
    Item("wording_space_form_untouched", "enforced",
         [PY, "-X", "utf8", GATE, "--face", "--no-register"], 2,
         "wants a value, its slot holds a switch", None,
         "closed before U-111 and must stay: the space form is the one that has always said it.",
         subjects=("tools/log_citation_gate.py",)),
    Item("emit_baseline_face_named", "enforced",
         [PY, "-X", "utf8", GATE, "--emit-baseline", "--face", "register"], 0,
         "fresh MISSING_BASELINE block for face=register", None,
         "closed in U-111: the block header carries the face on the single-face run too.",
         subjects=("tools/log_citation_gate.py",)),
    Item("appendix_faces_in_host", "enforced",
         [PY, "-X", "utf8", "tools/open_items_gate.py", "--probe-host"], 0,
         "host_gates=4", None,
         "the appendix is not named by ci.ps1 itself; four gates judge it and this holds them there. "
         "Its subject is ci.ps1 and its measurement reaches it through --probe-host, not through the "
         "argv -- which is the case N4 has to resolve rather than skip.",
         subjects=("tools/ci.ps1",)),
    Item("ci_fail_closed_paths", "enforced",
         [PY, "-X", "utf8", "tools/open_items_gate.py", "--probe-failclosed"], 0,
         "fail_closed_returns=4", None,
         "U-109 added fail-closed exits in the freshness helper (unreadable worktree, unreadable git "
         "log, unreadable commit date, a diff that will not run). They still have no fixture; this "
         "holds that the four returns stay. 附页 row 116 called them three; there are four, and the "
         "fourth (an unknown target name) is counted here rather than argued about.",
         subjects=("tools/ci.ps1",)),
    Item("family_headroom", "open",
         [PY, "-X", "utf8", "tools/ledger_size_gate.py"], 0, None, None,
         "ceiling 6000 lines over the two faces; one appendix row costs one line. This is a dated "
         "number, not a defect: size_due goes YES on the row that takes headroom to zero. Its real "
         "subjects are the two documents the size gate opens, which this gate cannot see from here; "
         "the script is what is declared, because the script is what is run.",
         read=r"^INV ceiling=",
         subjects=("tools/ledger_size_gate.py",)),
    Item("merge_first_parent", "open",
         [PY, "-X", "utf8", "tools/open_items_gate.py", "--probe-merges"], 0,
         None, None,
         "the rule compares against the first parent only; this repo has no merge commit, so the "
         "uncovered path has no object here. Printed, never judged: forcing merges to stay at zero "
         "would constrain the repository, not the rule. This row declares no subject on purpose -- its "
         "subject is the commit graph, which is not a tracked file, and an enforced row in that "
         "position would have to be refused. It is open, so no declaration is required of it."),
    Item("scratch_roster_exposure", "open",
         [PY, "-X", "utf8", "tools/repo_hygiene.py"], 0, None, None,
         "measured as the hygiene gate's own verdict plus its tracked-file count.",
         subjects=("tools/repo_hygiene.py",)),
    Item("row_text_lint_in_tools", "local",
         [PY, "-X", "utf8", "build/u44_probe/u110_row_text_lint.py",
          "--line-endings", "build/u44_probe/u111_appendix_row118.py"], 0, None,
         "build/u44_probe/u111_appendix_row118.py",
         "the lint judges the row writers, which live under git-ignored build/. A fresh clone has "
         "none, so this cannot be a host step until there is a tracked object for it to judge."),
    Item("anchor_says_its_own_hit_count", "local",
         [PY, "-X", "utf8", "build/u44_probe/u115_dead_anchor_census.py"], 0, None,
         "build/u44_probe/u115_dead_anchor_census.py",
         "teeth that rewrite a mother line can only see a dead anchor if they print how many needles "
         "landed; the census names the planting ones that do not. Both the tool and its population "
         "live under git-ignored build/, so this is local by the same reason the lint row is.",
         read=r"^\[U115-DA-ACCOUNTING\]"),
    Item("docstring_teeth_confined_stale", "open",
         [PY, "-X", "utf8", "build/u44_probe/u63_docstring_teeth.py"], 1, None, None,
         "the piece that judged 'this round only touched docstrings' reads a pinned base revision "
         "against the work tree, so its RED is a reading about one diff, not a defect with a fixed "
         "end state -- enforced would go green the moment that commit stops being the base. Open, so "
         "the host step re-measures it every round and prints the verdict line.",
         read=r"^\[U63-DT-VERDICT\]"),
    Item("u63_face_option", "local",
         [PY, "-X", "utf8", "build/u44_probe/u63_cited_pointer_check.py",
          "--face", "docs/" + u"\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212"
                    u"-\u767b\u8bb0\u9644\u8868.md"], 0, None,
         "build/u44_probe/u63_cited_pointer_check.py",
         "the --face switch is wired and both spellings run; the file that carries it is ignored."),
]

def _read(path):
    with open(os.path.join(ROOT, path), "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def _probe_host():
    text = _read(CI)
    names = ["register_shape_gate", "log_citation_gate", "ledger_size_gate",
             "dup_cn_literal_gate"]
    present = [n for n in names if n in text]
    print("[OIG-PROBE] host_gates=%d names=%s" % (len(present), ",".join(present)))
    return 0 if len(present) == len(names) else 1


def _probe_failclosed():
    text = _read(CI)
    # The U-109 freshness helper fails closed by RETURNING a "why" instead of a count, so the caller
    # treats an unreadable worktree, an unreadable git log, an unreadable commit date and a diff that
    # will not run as stale rather than as fresh. Counted by the phrase those returns carry, not by
    # line number, so a refactor that moves them does not read as a removal.
    marks = [l.strip() for l in text.split("\n") if "- fail closed" in l]
    reasons = []
    for l in marks:
        m = re.search(r"why\s*=\s*'([^']+)'", l)
        reasons.append(m.group(1) if m else l[:60])
    print("[OIG-PROBE] fail_closed_returns=%d" % len(marks))
    for i, r in enumerate(reasons, 1):
        print("[OIG-PROBE]   path%d %s" % (i, r))
    return 0 if len(marks) >= 3 else 1


def _probe_merges():
    proc = subprocess.run(["git", "log", "--merges", "--oneline"], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out = proc.stdout.decode("utf-8", "replace")
    merges = [l for l in out.split("\n") if l.strip()]
    total = subprocess.run(["git", "rev-list", "--count", "HEAD"], cwd=ROOT,
                           stdout=subprocess.PIPE).stdout.decode("ascii", "replace").strip()
    print("[OIG-PROBE] merges=%d commits=%s" % (len(merges), total))
    return 0


# U-114: PROBES maps a flag to the function object itself, not to a lambda wrapping it. It used to map
# to `lambda: _probe_host()` and sat ABOVE the definitions, so inspect.getsource on the lambda returned
# the PROBES dict literal rather than the probe body -- and N4 then reported that two enforced rows
# "never read" the file their probe plainly reads. The finding was right about the resolution and wrong
# about the rows. Mapping to the function, after the definitions, is what makes the read set mean what
# it says; the alternative (unwrapping a lambda at inspection time) would have been cleverer and
# harder to read.
PROBES = {
    "--probe-host": _probe_host,
    "--probe-failclosed": _probe_failclosed,
    "--probe-merges": _probe_merges,
}


def _probe_reads(func):
    """Tracked files a probe function reaches: literals in its own source, plus module-level string
    constants it names (the probe reads CI, which is a constant, not a literal inside the function)."""
    src = inspect.getsource(func)
    out = set(m.group(1) for m in PATH_LITERAL_RX.finditer(src))
    for name in set(IDENT_RX.findall(src)):
        val = globals().get(name)
        if isinstance(val, str):
            norm = val.replace("\\", "/")
            if norm:
                out.add(norm)
    return out


def reads_of(item):
    """Everything this row's measurement actually reads, as repo-relative strings.

    Two sources, and only two, because those are the only ones the gate can see: paths passed on the
    command line, and the body of a probe the command line names. What the measured *process* opens
    internally is invisible from here -- a row may legitimately guard a script whose real subject is a
    document that script opens, and the script is then the subject, not the document.
    """
    out = set()
    for a in item.argv:
        s = str(a).replace("\\", "/")
        out.add(s)
        if s.startswith("--probe-") and s in PROBES:
            out |= _probe_reads(PROBES[s])
    return out


def subject_is_tracked(path):
    proc = subprocess.run(["git", "ls-files", "--error-unmatch", "--", path], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return proc.returncode == 0


def run(argv):
    proc = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n")


def load_registry(path):
    """ITEMS from this module, or from a copy the caller points at.

    The teeth copy this file and edit one line of the table. A copied RULE is not the shipped rule, so
    the teeth point --registry at a copy of the registry and keep running THIS module's code -- the
    same arrangement the row-text lint's --skip uses.
    """
    if path is None:
        return ITEMS
    ns = {"__name__": "open_items_registry_copy", "__file__": path}
    with open(path, "r", encoding="utf-8") as fh:
        exec(compile(fh.read(), path, "exec"), ns)
    return ns["ITEMS"]


def main(argv):
    if "--list" in argv:
        for it in ITEMS:
            print("[OIG-LIST] id=%-32s state=%-9s file=%s"
                  % (it.id, it.state, it.file or "-"))
            print("           %s" % it.note)
        print("[OIG-LIST] items=%d enforced=%d open=%d local=%d"
              % (len(ITEMS), sum(1 for i in ITEMS if i.state == "enforced"),
                 sum(1 for i in ITEMS if i.state == "open"),
                 sum(1 for i in ITEMS if i.state == "local")))
        return 0

    if argv and argv[0] in PROBES:
        return PROBES[argv[0]]()

    registry = None
    if "--registry" in argv:
        registry = argv[argv.index("--registry") + 1]

    try:
        items = load_registry(registry)
    except Exception as exc:
        print("[OIG-FIND] registry unreadable: %s %s" % (type(exc).__name__, str(exc)[:120]))
        print("[OIG-VERDICT] items=0 enforced=0 findings=1 verdict=RED")
        return 2

    findings = 0
    seen = set()
    enforced_outputs = []
    enforced_argvs = {}
    n_enforced = n_open = n_local = 0
    for it in items:
        if it.id in seen:
            print("[OIG-FIND] duplicate id %s -- a second entry for one handle judges it twice and"
                  % it.id)
            print("           makes the reading ambiguous")
            findings += 1
            continue
        seen.add(it.id)
        bad = it.validate()
        if bad:
            print("[OIG-FIND] malformed entry %s: %s" % (it.id, bad))
            findings += 1
            continue

        if it.state == "local":
            n_local += 1
            if not os.path.exists(os.path.join(ROOT, it.file.replace("/", os.sep))):
                print("[OIG] id=%-32s state=local        reading=unmeasurable_here file=%s"
                      % (it.id, it.file))
                continue

        rc, out = run(it.argv)
        last = ""
        picked = None
        if it.read:
            for l in out.split("\n"):
                if re.search(it.read, l.strip()):
                    picked = l.strip()
                    break
        for l in reversed(out.split("\n")):
            if l.strip():
                last = l.strip()
                break
        reading = (picked or last)[:150]

        if it.state == "enforced":
            n_enforced += 1
            needle_ok = it.needle is None or it.needle in out
            ok = rc == it.want_rc and needle_ok
            print("[OIG] id=%-32s state=enforced     rc=%d want_rc=%d needle=%s verdict=%s"
                  % (it.id, rc, it.want_rc, "yes" if needle_ok else "NO",
                     "CLOSED" if ok else "REGRESSED"))
            if not ok:
                print("[OIG-FIND] enforced item regressed: %s" % it.id)
                print("           want rc=%d needle=%r" % (it.want_rc, it.needle))
                print("           first output line: %s" % reading)
                findings += 1
            else:
                enforced_outputs.append((it.id, it.needle, out))
            enforced_argvs.setdefault(tuple(it.argv), []).append(it.id)
        else:
            if it.state == "open":
                n_open += 1
            print("[OIG] id=%-32s state=%-12s rc=%d reading=%s"
                  % (it.id, it.state, rc, reading))

    # ---- N2 (U-113): two enforced items must not run the same command ----
    # A copy-pasted row measures the same thing twice under two handles, and the second one looks like
    # coverage. Nothing else in this file notices: each row judges fine on its own.
    for argv_key, owners in sorted(enforced_argvs.items(), key=lambda kv: kv[1][0]):
        if len(owners) > 1:
            print("[OIG-FIND] two enforced items run the same command: %s" % ",".join(owners))
            print("           argv=%s" % " ".join(argv_key[1:]))
            print("           one of them measures nothing of its own; split it or drop it")
            findings += 1

    # ---- N1 (U-113): an enforced needle must belong to its own run and no other ----
    # If the needle also shows up in a sibling's output, this row can read CLOSED because of what the
    # sibling printed, while measuring nothing about its own subject -- and it would keep reading CLOSED
    # after its own subject broke, which is the exact failure this gate exists to prevent. Checked only
    # over rows that matched their own needle: a row that already regressed has said so once, and
    # repeating it here would bury the one line that names the subject.
    for id_, needle, out in enforced_outputs:
        if needle is None:
            continue
        owners = [oid for oid, _n, o in enforced_outputs if needle in o]
        if owners != [id_]:
            print("[OIG-FIND] needle of %s is not unique to its own run: seen in %s"
                  % (id_, ",".join(owners) if owners else "none"))
            print("           needle=%r -- a row that can be satisfied by a sibling's output is not"
                  % needle)
            print("           measuring its own subject; give it a needle only its own run prints")
            findings += 1

    # ---- N4 (U-114): a declared subject must be a tracked file the measurement actually reads ----
    # N1 checks that the needle belongs to this row's own run. It cannot see the other failure: a row
    # whose needle is entirely its own while its measurement reads something else. That row guards
    # nothing and stays green forever. Each row therefore declares what it guards, and this checks the
    # declaration against the command -- including the case where the subject is reached through a
    # probe the command names rather than through the command line itself.
    for it in items:
        if it.state != "enforced" or not it.subjects:
            continue
        reads = reads_of(it)
        for s in it.subjects:
            if s not in reads:
                print("[OIG-FIND] %s declares subject %s but its measurement never reads it"
                      % (it.id, s))
                print("           argv=%s" % " ".join(str(a) for a in it.argv))
                print("           reads=%s" % ",".join(sorted(reads)) or "(nothing)")
                print("           a row that measures the wrong thing keeps reading CLOSED forever;")
                print("           either fix the command or declare what it really guards")
                findings += 1
            elif not subject_is_tracked(s):
                print("[OIG-FIND] %s declares subject %s which git does not track"
                      % (it.id, s))
                print("           a fresh clone would have nothing to guard")
                findings += 1

    print("[OIG-VERDICT] items=%d enforced=%d open=%d local=%d findings=%d verdict=%s"
          % (len(items), n_enforced, n_open, n_local, findings,
             "GREEN" if findings == 0 else "RED"))
    return 0 if findings == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Exception as exc:
        print("[OIG-VERDICT] the gate itself failed: %s %s" % (type(exc).__name__, str(exc)[:160]))
        sys.exit(2)

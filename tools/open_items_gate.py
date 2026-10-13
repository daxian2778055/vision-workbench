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

    def __init__(self, id, state, argv, want_rc, needle, file, note, read=None, subjects=(),
                 clone_expect=None):
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
        # U-124: does this row expect to be measurable on a clean clone? None means "not
        # declared", and an undeclared row that turns out unmeasurable is a FINDING -- which is
        # the whole point. U-122 found a permanently-red enforced row by hand, because nothing
        # recorded that an enforced row must be measurable on a clone at all. This field is what
        # turns that from a discovery into a reading.
        self.clone_expect = clone_expect

    def validate(self):

        # U-124, and this check sits BEFORE the state branches on purpose: the first version put it
        # after the `local` early return, so a local row with a bogus clone_expect was never judged
        # at all -- the E4 teeth arm caught that, because the arm reported GREEN on a value the
        # contract does not allow.
        if self.clone_expect not in (None, "yes", "no"):
            return ("clone_expect %r must be yes, no, or absent" % (self.clone_expect,))
        if self.state == "enforced" and self.clone_expect == "no":
            return ("enforced with clone_expect=no: an enforced row must be measurable on a "
                    "clean clone; if it cannot be, it is local, not enforced")
        if self.state not in STATES:
            return "unknown state %r (want one of %s)" % (self.state, ",".join(STATES))
        if not re.match(r"^[a-z0-9_]+$", self.id or ""):
            return "id %r is not lowercase-with-underscores" % (self.id,)
        if self.state == "local":
            if not self.file:
                return "state=local needs file"
            if not self.argv:
                return "state=local needs an argv to run once the file is there"
            return None
        # N3 (U-113) used to refuse a needle on a local row as well. U-122 showed that rule was too
        # strong: a local row that IS measurable on this machine has something to measure, and its
        # needle is what says whether it still reads closed here. The main loop consults it and prints
        # CLOSED_LOCAL / REGRESSED_LOCAL -- a reading, never a verdict, because a fresh clone cannot
        # measure the row at all and a verdict that moves with the machine is the U-109 shape again.
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
    # emit_baseline_face_named
    Item("emit_baseline_face_named", "local",
         [PY, "-X", "utf8", GATE, "--emit-baseline", "--face", "register"], 0,
         "fresh MISSING_BASELINE block for face=register", "build/u44_probe/u111_appendix_row118.py",
         "closed in U-111 and stayed closed on the machine that closed it -- but the citation gate "
         "declares CITE-SKIP ... verdict=SKIP when the git-ignored scratch tree is absent, and its own "
         "docstring says that is neither a pass nor a silence. So on a clean clone this item is not "
         "verified at all, and the honest state is local, not enforced: U-122's clone probe reads it "
         "UNMEASURABLE_IN_CLONE, and that is a finding for an enforced row.",
         subjects=("tools/log_citation_gate.py",),
         # U-124 measured kind A: first stage ran it in a HEAD worktree and it printed CITE-SKIP and verdict=SKIP: the citation gate declares its own skip when the git-ignored scratch tree is absent. Moving any file cannot change that, because what it measures on a clone IS the skip.
         clone_expect="no"),
    Item("enforced_survives_a_clone", "enforced",
         [PY, "-X", "utf8", "tools/open_items_gate.py", "--probe-clone"], 0,
         "all_survive=yes", None,
         "the shipping criterion, measured instead of found by hand: every enforced row must still "
         "read CLOSED in a worktree of HEAD with nothing git-ignored present. U-122 found this by "
         "review -- an enforced item went RED on a clean clone, which would have put step 1l on exit 15 "
         "for anyone who cloned the repo (the same class as U-109's permanently-red host). A declared "
         "skip from the measured tool counts as UNMEASURABLE_IN_CLONE and is a finding, because an "
         "item nobody measures on a clone is not closed on a clone. Its subject is this module itself:",
         "the probe reads the registry out of it, and the registry is what decides which rows it runs.",
         subjects=("tools/open_items_gate.py",)),
    Item("clone_readings_match_host", "enforced",
         [PY, "-X", "utf8", "tools/open_items_gate.py", "--probe-clone-readings"], 0,
         "all_agree=yes", None,
         "the second half of the U-122 item: not whether a row can be measured on a clone, but whether "
         "it reads the same there. A row that measures something machine-local would agree nowhere "
         "else, and before this row nothing compared the two readings. Rows that cannot be measured on "
         "the clone report unmeasurable and are excluded from the comparison rather than counted as "
         "agreeing -- reading a row nobody measured as agreeing is the fail-open this module refuses. "
         "Its subject is this module for the same reason the row above it is.",
         subjects=("tools/open_items_gate.py",)),
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
    # row_text_lint_in_tools
    Item("row_text_lint_in_tools", "local",
         [PY, "-X", "utf8", "build/u44_probe/u110_row_text_lint.py",
          "--line-endings", "build/u44_probe/u111_appendix_row118.py"], 0, None,
         "build/u44_probe/u111_appendix_row118.py",
         "the lint judges the row writers, which live under git-ignored build/. A fresh clone has "
         "none, so this cannot be a host step until there is a tracked object for it to judge.",
         # U-124 measured kind B1: the command cannot even start on a clone. Its closure reaches past one file: the lint itself lives under build/ and its argv names the per-round row writers, so relocating the lint leaves the population where it was.
         clone_expect="no"),
    # anchor_says_its_own_hit_count
    Item("anchor_says_its_own_hit_count", "local",
         [PY, "-X", "utf8", "build/u44_probe/u115_dead_anchor_census.py"], 0, None,
         "build/u44_probe/u115_dead_anchor_census.py",
         "teeth that rewrite a mother line can only see a dead anchor if they print how many needles "
         "landed; the census names the planting ones that do not. Both the tool and its population "
         "live under git-ignored build/, so this is local by the same reason the lint row is.",
         read=r"^\[U115-DA-ACCOUNTING\]",
         # U-124 measured kind B1: the command cannot even start on a clone, and the census scans the tree rather than one file: its population IS the git-ignored tree, so there is nothing to relocate.
         clone_expect="no"),
    # docstring_teeth_confined_stale
    Item("docstring_teeth_confined_stale", "open",
         [PY, "-X", "utf8", "build/u44_probe/u63_docstring_teeth.py"], 1, None,
         "build/u44_probe/u63_docstring_teeth.py",
         "the piece that judged 'this round only touched docstrings' reads a pinned base revision "
         "against the work tree, so its RED is a reading about one diff, not a defect with a fixed "
         "end state -- enforced would go green the moment that commit stops being the base. Open, so "
         "the host step re-measures it every round and prints the verdict line. It names its file "
         "because the piece is git-ignored: on a clean clone the honest reading is unmeasurable_here, "
         "not 'can't open file' (U-122 measured the latter and it says the measurement broke).",
         read=r"^\[U63-DT-VERDICT\]",
         # U-124 measured kind B2: the command cannot even start on a clone, and this is the one of the five whose closure is a single file: no build/ literal, no tree scan, nothing else to bring along. Moving it would work, and this round still decided not to, because it buys one row out of five against 71 old references plus a permanent drift guard.
         clone_expect="no"),
    # u63_face_option
    Item("u63_face_option", "local",
         [PY, "-X", "utf8", "build/u44_probe/u63_cited_pointer_check.py",
          "--face", "docs/" + u"\u5bf9\u6807\u5dee\u8ddd\u63a8\u8fdb\u8ba1\u5212"
                    u"-\u767b\u8bb0\u9644\u8868.md"], 0, None,
         "build/u44_probe/u63_cited_pointer_check.py",
         "the --face switch is wired and both spellings run; the file that carries it is ignored.",
         # U-124 measured kind B1: the command cannot even start on a clone. Measured as B1, not B2: the piece writes a scratch face under build/, so even after relocating it still reaches into the ignored tree. An earlier draft of this note said B2 on the strength of argument; the reading disagreed and the reading won.
         clone_expect="no"),
    # U-125: the third kind of blind, measured this round and unlike the other two.
    Item("eol_shape_is_host_only", "open",
         [PY, "-X", "utf8", "build/u44_probe/u125_eol_census.py",
          "build/u44_probe/U125_eol_census_1.txt"], 0, None,
         "build/u44_probe/U125_eol_census_1.txt",
         "a row that reads bytes is host-only by construction, and the census says how many files that is: 142 of 558 tracked files sit on disk as LF while core.autocrlf=true and there is no .gitattributes, so a fresh checkout of any of them hands the reader CRLF. U-125 measured the consequence rather than asserting it: the piece docstring_teeth_confined_stale carries was copied verbatim into tools/ of a HEAD worktree, ROOT line and nothing else, and there it dies with a ValueError and prints no verdict line at all -- it looks for a byte anchor def head_bytes(rel_posix): -- a byte anchor it finds after splitting the file on newlines, and that anchor sits on the LF copy only. So moving the piece into the repo does NOT make this family comparable, which is the premise the previous round deferred on. This kind cannot be fixed by relocating anything; it is fixed either by not reading bytes or by knowing the worktree shape differs from the clone shape.",
         read=r"^tracked files=",
         # U-124 kind, and this one is its own kind: not the tool declaring a skip, and not a closure wider than one file. Locating the file changes nothing.
         clone_expect="no"),
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


def run(argv, cwd=None):
    proc = subprocess.run([str(a) for a in argv], cwd=cwd or ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout.decode("utf-8", "replace").replace("\r\n", "\n")


# ---- the clone probe (U-122) ----
# Reading: an enforced item must still read CLOSED in a worktree of HEAD -- no build/, no ci-*.log,
# nothing git-ignored. This is the shipping criterion, and before U-122 it was not measured at all: it
# was found by hand in a review round, and what it found was an enforced item that goes RED on a clean
# clone, which would have put ci.ps1's step 1l on exit 15 for anyone who cloned the repo. That is the
# same class as the permanently-red host of U-109's P1, brought in when these items were registered.
#
# A declared skip is not an excuse. tools/log_citation_gate.py prints CITE-SKIP ... verdict=SKIP when
# the git-ignored scratch tree is absent, and its own docstring says that is "not a pass and not a
# silence". An item whose measurement skips on a clean clone is not verified there, so it is reported as
# UNMEASURABLE_IN_CLONE and it IS a finding: the honest classification for such an item is local, with
# a git-ignored file named as its `file`, and that is what this rule pushes the author into.
CLONE_MARKER = "build/u44_probe"
CLONE_DIR = None
CLONE_SEQ = [0]
CLONE_PROBE_FLAGS = ("--probe-clone", "--probe-clone-readings")
# The ref a clone worktree is taken from. HEAD by default; the environment variable is a legitimate
# configuration point (a reviewer may want the clone taken from a tag or a branch tip), and it is what
# the teeth use to drive a REAL git failure into the fail-closed leg -- a ref that does not exist makes
# `git worktree add` fail for the ordinary reason, rather than the gate being patched to pretend to.


def _make_clone():
    """A detached worktree of HEAD. Fail-closed: a clone we cannot make is a finding, never a pass."""
    global CLONE_DIR
    CLONE_SEQ[0] += 1
    # A path unique to this call, not one fixed path. The first version used one fixed path, so the
    # two clone probes nested inside each other (the readings probe runs every enforced row, and one of
    # them runs the clone probe) and the inner one's cleanup removed the directory the outer one was
    # still using -- the outer's remove then failed with 128, leaving the worktree behind and reading
    # REGRESSED for a reason that had nothing to do with any item.
    tmp = os.path.join(os.environ.get("TEMP", "/tmp"),
                       "oig_clone_wt_%d_%d" % (os.getpid(), CLONE_SEQ[0]))
    if os.path.isdir(tmp):
        subprocess.run(["git", "worktree", "remove", "--force", tmp], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    elif os.path.exists(tmp):
        # A plain file left at that path would make every future probe fail with "already exists, but
        # is not a directory" -- which reads as a broken gate rather than as leftover state. Removing
        # it is what keeps one interrupted run from turning into a permanent red.
        try:
            os.remove(tmp)
        except OSError as exc:
            return None, "cannot clear %s: %s" % (tmp, exc)
    proc = subprocess.run(["git", "worktree", "add", "--detach", "--force", tmp,
                           os.environ.get("OIG_CLONE_REF", "HEAD")], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if proc.returncode != 0 or not os.path.isdir(tmp):
        return None, proc.stdout.decode("utf-8", "replace")[-400:]
    CLONE_DIR = tmp
    return tmp, None


def _drop_clone():
    global CLONE_DIR
    if not CLONE_DIR:
        return "no worktree was made"
    proc = subprocess.run(["git", "worktree", "remove", "--force", CLONE_DIR], cwd=ROOT,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    left = os.path.isdir(CLONE_DIR)
    CLONE_DIR = None
    return "removed rc=%d left=%s" % (proc.returncode, left)


SKIP_NEEDLES = ("CITE-SKIP", "verdict=SKIP")


def _probe_clone():
    wt, err = _make_clone()
    if wt is None:
        print("[OIG-PROBE] could not make a clone worktree: %s" % err.replace("\n", " ")[:220])
        print("[OIG-PROBE] clone_measured=0 clone_survivable=0 unmeasurable=0 regressed=0 "
              "all_survive=no")
        print("[OIG-PROBE] worktree cleanup: %s" % _drop_clone())
        # Code 2, not 1: "a finding" and "this probe could not run at all" are different claims, and
        # collapsing them is how a broken gate starts reading as a measured one. The module's own
        # contract says 2 = the gate itself could not run.
        print("[OIG-PROBE] verdict=INCOMPLETE exit=2")
        return 2
    print("[OIG-PROBE] clone_worktree=%s" % wt)
    survivable = 0
    unmeasurable = []
    regressed = []
    for it in ACTIVE_ITEMS:
        if it.state != "enforced":
            continue
        if _is_self_probe(it):
            # Self-reference: running a probe of this gate inside its own clone probe recurses, and
            # the nested copy would report whatever the outer one reports -- a reading that cannot
            # move. Judged by shape (argv names this module and a --probe- flag), not by a list of
            # names: the first version listed --probe-clone by hand, so the readings probe added in
            # U-123 was not skipped and nested one level deep.
            print("[OIG-PROBE]   %-32s SKIPPED (self-referential: this probe runs itself)"
                  % it.id)
            continue
        rc, out = run(it.argv, cwd=wt)
        needle_ok = it.needle is None or it.needle in out
        skipped = any(s in out for s in SKIP_NEEDLES)
        if rc == it.want_rc and needle_ok:
            survivable += 1
            print("[OIG-PROBE]   %-32s CLOSED_IN_CLONE" % it.id)
        elif skipped:
            unmeasurable.append(it.id)
            print("[OIG-PROBE]   %-32s UNMEASURABLE_IN_CLONE (the tool declares a skip)"
                  % it.id)
            print("[OIG-PROBE]   %s" % [l for l in out.split("\n") if l.strip()][-1][:110])
        else:
            regressed.append(it.id)
            print("[OIG-PROBE]   %-32s REGRESSED_IN_CLONE" % it.id)
            print("[OIG-PROBE]   %s" % [l for l in out.split("\n") if l.strip()][-1][:110])
    drop = _drop_clone()
    measured = survivable + len(unmeasurable) + len(regressed)
    all_survive = "yes" if (measured and survivable == measured) else "no"
    print("[OIG-PROBE] clone_measured=%d clone_survivable=%d unmeasurable=%d regressed=%d "
          "all_survive=%s"
          % (measured, survivable, len(unmeasurable), len(regressed), all_survive))
    print("[OIG-PROBE] worktree cleanup: %s" % drop)
    return 0 if all_survive == "yes" else 1


# PROBES sits here, after every probe function it names -- including _probe_clone, which is what the
# U-122 shipping criterion runs. Putting it above them means a probe added later is referenced before
# it exists, which is a NameError at import time rather than at the step that would have caught it.
def _is_self_probe(item):
    """True when this row measures THIS module's clone machinery: its argv names it and passes one of
    the two clone flags.

    Such a row is the gate examining itself, and it must never run inside the other clone probe: the
    nested copy would report whatever the outer one reports, a reading that cannot move. The first
    version asked for any --probe- flag, which also matched --probe-host and --probe-failclosed --
    two rows that are ordinary measurements, so the clone probe silently measured two fewer rows than
    it claimed to. R5 caught it by printing the skip list. Narrowed to the exact set that must not
    nest.
    """
    argv = [str(a) for a in item.argv]
    if "tools/open_items_gate.py" not in argv:
        return False
    return any(flag.startswith(arg.strip("=")[:len(flag)]) or arg.startswith(flag)
               for arg in argv for flag in CLONE_PROBE_FLAGS)

def _verdict_line(out):
    for l in out.split("\n"):
        if "VERDICT" in l or "verdict=" in l:
            return l.strip()[:120]
    for l in out.split("\n"):
        if l.strip():
            return l.strip()[:120]
    return "(no output)"


def _probe_clone_readings():
    """Same-string agreement: for every row measurable on both sides, the clone's reading must equal
    this machine's.

    U-122's clone probe asks the first question -- can this row be measured in a clone at all. This one
    asks the second, which is what "replayable" actually means: the row a reviewer replays must read
    the same thing it reads here. A row that measures something machine-local (a build path, an
    on-disk artefact, a clock) would agree nowhere else, and before this probe nothing measured that.

    What it deliberately does not do: compare rows that cannot be measured on the clone. Those report
    unmeasurable and move neither the count nor the verdict -- the honest reading for them is "not
    measured here", and pretending they agree would be the fail-open this module keeps refusing.
    """
    wt, err = _make_clone()
    if wt is None:
        print("[OIG-PROBE] clone_readings_comparable=0 agree=0 disagree=0 unmeasurable=0 agree=no")
        print("[OIG-PROBE] could not make a clone worktree: %s" % err.replace("\n", " ")[:200])
        print("[OIG-PROBE] worktree cleanup: %s" % _drop_clone())
        print("[OIG-PROBE] verdict=INCOMPLETE exit=2")
        return 2

    comparable = agree = disagree = unmeas = 0
    undeclared = []
    # A digest, not the verdict text. The first version printed each side's verdict line, and a verdict
    # line is where the needles live -- so this probe's own output contained every other row's needle,
    # and N1 reported five rows whose needle was no longer unique to its own run. That finding was
    # right: a probe that quotes the population verbatim makes the population un-comparable. Printing
    # a digest keeps the equality check (same text -> same digest) without reproducing anybody's text.
    import hashlib
    for it in ACTIVE_ITEMS:
        if _is_self_probe(it):
            print("[OIG-PROBE]   %-32s SKIPPED (self-referential)" % it.id)
            continue
        if it.file and not os.path.exists(os.path.join(wt, it.file.replace("/", os.sep))) \
                and it.state in ("local", "open"):
            if it.clone_expect is None:
                # A row nobody declared is a finding. Counting it inside a number and moving on
                # is exactly how the U-122 permanently-red row sat in the register for seven
                # rounds with nothing measuring it.
                undeclared.append(it.id)
                print("[OIG-PROBE]   %-32s CLONE-BLIND-UNDECLARED (needs %s)"
                      % (it.id, it.file))
            else:
                unmeas += 1
                print("[OIG-PROBE]   %-32s UNMEASURABLE_IN_CLONE (declared no; needs %s)"
                      % (it.id, it.file))
            continue
        rc_h, out_h = run(it.argv, cwd=ROOT)
        rc_c, out_c = run(it.argv, cwd=wt)
        comparable += 1
        d_h = hashlib.sha256(_verdict_line(out_h).encode("utf-8")).hexdigest()[:12]
        d_c = hashlib.sha256(_verdict_line(out_c).encode("utf-8")).hexdigest()[:12]
        same = rc_h == rc_c and d_h == d_c
        if same:
            agree += 1
            print("[OIG-PROBE]   %-32s AGREE rc=%d digest=%s" % (it.id, rc_h, d_h))
        else:
            disagree += 1
            print("[OIG-PROBE]   %-32s DISAGREE rc=%d/%d digest_host=%s digest_clone=%s"
                  % (it.id, rc_h, rc_c, d_h, d_c))

    all_agree = "yes" if (comparable and disagree == 0) else "no"
    print("[OIG-PROBE] clone_readings_comparable=%d agree=%d disagree=%d unmeasurable=%d "
          "undeclared_blind=%d all_agree=%s"
          % (comparable, agree, disagree, unmeas, len(undeclared), all_agree))
    for rid in undeclared:
        print("[OIG-PROBE] clone-blind-undeclared %s -- declare clone_expect or fix it" % rid)
    print("[OIG-PROBE] worktree cleanup: %s" % _drop_clone())
    if undeclared:
        return 1
    return 0 if all_agree == "yes" else 1


PROBES = {
    "--probe-host": _probe_host,
    "--probe-failclosed": _probe_failclosed,
    "--probe-merges": _probe_merges,
    "--probe-clone": _probe_clone,
    "--probe-clone-readings": _probe_clone_readings,
}


ACTIVE_ITEMS = ITEMS


def _resolve_registry(argv):
    """The registry a probe must judge: the shipped one, or the copy --registry names.

    The probes used to iterate the module-level ITEMS, so --registry had no effect on them -- a teeth
    arm that pointed --registry at a copy was quietly measuring the delivered table instead of the
    fixture it had just built, and it read GREEN for the wrong reason. U-122's first C1 arm is where
    that surfaced. Resolving before dispatch is the whole fix.
    """
    global ACTIVE_ITEMS
    if "--registry" in argv:
        try:
            ACTIVE_ITEMS = load_registry(argv[argv.index("--registry") + 1])
        except Exception as exc:
            print("[OIG-FIND] registry unreadable: %s %s" % (type(exc).__name__, str(exc)[:120]))
            ACTIVE_ITEMS = []
            return False
    else:
        ACTIVE_ITEMS = ITEMS
    return True


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

    # A probe flag is dispatched wherever it appears. Gating on argv[0] meant the teeth' own
    # "--registry <copy> --probe-clone" never reached the probe at all: it fell through to the full-gate
    # path, measured the copy LOCALLY (which is green for the very row the arm is about), and the arm
    # read GREEN for a reason that had nothing to do with the clone. U-122's C1 is where that surfaced.
    probe = next((k for k in PROBES if k in argv), None)
    if probe:
        if not _resolve_registry(argv):
            return 2
        return PROBES[probe]()

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
        # Counted exactly once per item, here, before any branch. The U-122 version counted an open
        # row with a `file` twice -- once in the unmeasurable branch that also serves local rows, once
        # again at the bottom -- so the run read open=5 against --list's open=4 on the same bytes, and
        # the teeth arm that compares the two caught it. Counting in one place is what makes that arm
        # mean something: it still tests that the loop visits every row exactly once.
        if it.state == "enforced":
            n_enforced += 1
        elif it.state == "open":
            n_open += 1
        else:
            n_local += 1
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

        # An item that names a git-ignored file it needs is reported as unmeasurable when that file is
        # absent, whatever its state. U-122: a `open` row whose command needs build/u44_probe used to
        # print "can't open file" on a clean clone -- a reading that says the measurement broke, when
        # the truth is there is nothing to measure. Distinguishing those two is the whole point.
        if it.state in ("local", "open") and it.file:
            if not os.path.exists(os.path.join(ROOT, it.file.replace("/", os.sep))):
                print("[OIG] id=%-32s state=%-12s reading=unmeasurable_here file=%s"
                      % (it.id, it.state, it.file))
                continue

        if it.state == "local":
            pass  # already counted in the unmeasurable / measurable branch above

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
            print("[OIG] id=%-32s state=%-12s rc=%d reading=%s"
                  % (it.id, it.state, rc, reading))
            # A local row that CAN be measured here reports whether it still holds, so the reading is
            # more than a line of stdout. It never moves the verdict: a fresh clone cannot measure the
            # row at all, and a verdict that follows the machine is the shape U-109 spent a round
            # digging out of the host.
            if it.state == "local" and it.needle is not None:
                ok_local = rc == it.want_rc and it.needle in out
                print("[OIG]   -> %s reads %s here (rc=%d want_rc=%d needle=%s)"
                      % (it.id, "CLOSED_LOCAL" if ok_local else "REGRESSED_LOCAL",
                         rc, it.want_rc, "yes" if it.needle in out else "NO"))

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

r"""Cited-log presence gate -- turns "a log the ledger cites must be openable" into something that
can actually stop a run (tenth-round review, item 3, which this repo itself had already listed as a
candidate: "日志名不复用加机器闸").

Why this exists: every row of the ledger that says "(日志 X.txt)" is promising the reviewer a file
they can open. Two of those promises were broken by this project's own hand in the ninth round --
U44_probe_run2.txt and U44_probe_run3.txt were renamed mid-round (U-46 W-3) and the only thing that
kept the citations honest was somebody remembering to. A convention with no machine behind it is a
convention the next round can silently break, so this script is the machine.

Definition of a citation (this file is the only place it is written): the text 日志 followed by
whitespace and a path-like token ending in .txt or .log. The captured token is judged as written:

  1 EXACT    ROOT/<token> exists -- a citation that spelled out its own path.
  2 SCRATCH  <token> is a bare file name and some file of that name exists under a build/*_probe/
             scratch tree (searched recursively; u44's wt/ subtree is excluded -- it is a transient
             copy of the repository, so its .txt files are the repo's own tracked files, not logs).
             That directory-suffix convention is where every round's probe logs are written.
  3 MISSING  neither. This is what the gate judges.

Baseline口径, copied from NOCAND_BASELINE in src_anchor_inventory.py (sixth-round review S-2):
MISSING_BASELINE freezes what was already missing when the gate was registered. Only ADDED is red --
a citation this round introduced (or revived) that resolves to nothing. REMOVED is report-only, so
cleaning up an old broken citation can never be blocked by the gate that measures it.

The second rule (U-54): a citation that resolves to a HOST RUN must carry its own exit code. Why it
is here: the seventeenth round found the ledger citing a host end-to-end run whose log had the verdict
lines and no exit code at all, because the command had been piped into tail -- a pipeline reports the
tail's status. That round delivered tools/ci_host_run.ps1, which writes the child's code into the same
bytes, but left the rule as a convention ("only write an rc for a log when the log itself carries that
line") with no machine behind it, and said plainly that a next round piping the host would hit nothing.
This is that machine.

  4 HOST-SHAPED is judged from the file's own bytes, not from prose, under either of two readings:
      narrow -- a line matching
        ^\s*\[OK\]\s+tests:\s*\d+/\d+\s+passed\s*$   (tools/ci.ps1:715, :677 before U-67's step 1l) or
        ^\s*\[FAIL\]\s+tests failed \(                (tools/ci.ps1:697, :659 before the same step)
      wide (U-55) -- a line matching ^\s*\[FAIL\]\s+ whose rest starts with a static step's own
        verdict sentence, read out of tools/ci.ps1 at run time (WRITE_ERR_RX up to the first '$',
        plus Preflight's $hardMissing strings). A host that stopped at step 1b..1l never prints a
        tests banner, so without this second reading its log is not evidence to this gate at all --
        which is how a piped static-red run could be cited without ever being asked for its code.
    Either way a log is host evidence because the host wrote into it, not because prose says so.
    The CITEHOSTDEF line carries two readings of that definition so a rewording in ci.ps1 cannot
    hide inside it: wide_markers= (how many step sentences were read) and marker_digest= (sha256 of
    those literals sorted and joined by LF, first 12 hex). The count alone cannot see one literal
    swapped for another; the digest can. It is printed for the replay, not judged -- see the comment
    at CI_PS1_REL for the exact boundary of that promise.
  5 SELF-CODED is one line matching [HOST-RC] inner=<path> ci_exit_code=N wrapper_exit_code=N, the
    wrapper's contract line. A host-shaped citation that is not self-coded is a CITEHOSTADD finding.
    Only the token is asked for, not a location: a log may hold many host runs, and requiring the
    contract line at all -- rather than at line N -- is what the rule can actually check.

    Not one definition with the wrapper, on purpose: rule 4's banner count is \d+/\d+, while the
    wrapper's own verdict pattern (tools/ci_host_run.ps1:35, '\[OK\]\s+tests: [1-9][0-9]*/[1-9][0-9]*
    passed') refuses 'tests: 0/0 passed' as a run that never ran anything. The gap runs in the
    demanding direction -- every shape the wrapper calls a verdict is host-shaped here, plus one the
    wrapper throws away, and a 0/0 banner written by hand is precisely the kind of evidence worth
    asking for a code. So the two patterns are related as superset, not as equal, and arm T5 of
    build/u44_probe/u54_host_rc_teeth.py measures that relation on both sides' own text instead of
    asserting it here.

HOSTRC_BASELINE is the set of host-shaped citations that were already written before this rule
existed, frozen from this script's own --emit-baseline run on the delivered ledger. Same ADDED-only
direction as the presence rule: old evidence stays readable, a new host run must carry its code.

Why the baseline is machine-local, stated rather than hidden: build/u44_probe/ is git-ignored, so
"present" describes this working machine, not the repository. On a fresh clone every cited log is
missing and every clone would exit 13 before configure -- a gate nobody can live with gets disabled,
which is worse than a weaker gate (same reasoning as the 1g/1h pre-existing-state baseline in
ledger_size_gate.py). So an absent scratch tree produces a DECLARED, VISIBLE skip: CITE-SKIP is
printed, is in the host's display filter, and exits 0. It is not a pass and not a silence. The host
rule reads file contents, so it shares the same skip: with no scratch there is nothing to sniff, and
CITEHOST prints host_shaped=0 with the skip rather than pretending a clean judgement.

What this gate does NOT prove, so the boundary stays visible: a name that is reused by OVERWRITING
an older log still resolves, so this gate reads it as present. Catching that would need the file
contents to be pinned per round, which is a different (much bigger) promise; it is recorded as the
unproven half in the ledger rather than claimed here. The same declined promise has a second edge for
rule 5: a baselined token whose file is REPLACED by new bytes that are host-shaped and uncoded still
matches the baseline and is excused. Judging that would need the baseline pinned by content, and it is
registered the same way -- stated, not claimed.

Two faces, two baselines (U-65, closing the cell 附页第三十八行 left open). The repository has two
ledgers that cite logs: LEDGER_REL (the gap plan) and REGISTER_REL (its registration appendix, split
out of that file in U-60). Both were frozen against the LEDGER's history only, so pointing this gate
at the appendix compared face B's citations against face A's past. Measured before the split, on the
delivered appendix bytes: that run reads missing=0 and uncoded=0 -- nothing in the appendix fails
either rule -- while the same run prints removed=1 and host_removed=14, because the ledger's one
baselined range and its fourteen baselined host runs are simply not citations of this face. Those
were artifacts of the comparison, not cleanings-up, and they made the appendix unusable as a host
scan face: the numbers could not be read as anything. So each face now carries its own pair of
frozen sets, keyed by the label it is judged under (FACES below), and a run judges both faces and
aggregates. The ledger's two sets keep their module names, lengths and contents unchanged -- a
reviewer who counts them by importing this module still gets 1 and 14, the reading 第三十八行
registers.

What a two-face run prints, and what stayed put. Which faces get judged: no option = the whole
family (ledger first, then appendix); --face ledger|register = that one; a --ledger or --register path
without --face = only the face whose copy you named, which is what keeps the pre-split meaning of bare
--ledger intact (one face, the ledger's two frozen sets, one set of lines). New lines, all starting
with CITE so tools/ci.ps1 step 1k's display filter shows them: CITEFACES (which faces this run
judges), CITEFACE (one face's path and its own two baseline sizes), CITEFIND (that face's finding
count), plus the INV faces= aggregate before INV findings=. Unchanged, deliberately: every per-face
line keeps its old prefix and wording, because the host filter and
tools/probes/U47_citation_host_exit_probe.py read those lines by regex and the ledger registers them
as exact numbers. Two consequences a reviewer should not have to guess: the INV citation_baseline=
line now appears once per face (the U47 probe reads the first, which is the ledger's, since FACES is
in ledger-then-appendix order), and CITEHOSTDEF is printed once per run rather than once per face,
because that reading comes from tools/ci.ps1 and is the same for both. One ordering change comes with
judging more than one file: every face is opened before the scratch decision, so an unreadable face is
still exit 2 even on a machine with no scratch trees.

Reproduce:
    python tools/log_citation_gate.py
    python tools/log_citation_gate.py --ledger <path>     (judge a copy, ledger's baselines as before)
    python tools/log_citation_gate.py --register <path>   (same, for the appendix face)
    python tools/log_citation_gate.py --face register     (one face, that face's own baselines)
    python tools/log_citation_gate.py --emit-baseline     (print both fresh paste blocks; still judges)

Exit: 0 = green or declared skip | 1 = ADDED citations (CITEADD) or host-shaped citations without the
contract line (CITEHOSTADD) | 2 = ledger unreadable | 3 = script crash. Output is ASCII-only (cp936
console); paths go through esc.
"""
import hashlib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from inline_rewrite_check import LEDGER_REL, REGISTER_REL  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The recorded scratch convention: every round's probes write into build/<something>_probe/.
SCRATCH_PARENT_REL = "build"
SCRATCH_SUFFIX = "_probe"
# The copy worktree inside u44's scratch: excluded from the SCRATCH resolution, see docstring.
SCRATCH_EXCLUDE_REL = os.path.join(SCRATCH_PARENT_REL, "u44_probe", "wt")

CITE = re.compile(u"日志\\s+([A-Za-z0-9_./\\\\-]+\\.(?:txt|log))")

# Frozen at ledger baseline commit fc12294 by this script's own --emit-baseline run
# (build/u44_probe/U47_citation_baseline.txt). Replace only by re-running that command and pasting
# its output; the counts on the INV citation_baseline= line are what a reviewer can replay.
# The one item below is not a broken promise: §3.42's table-3 header (docs line 4510 at fc12294 --
# `git show fc12294:docs/对标差距推进计划.md | sed -n '4510p'`) writes that citation as a RANGE
# (u38_A1..A10.raw.txt) standing for ten individually present files, so no single file has that name.
# It is frozen rather than judged because rewriting a table header is not what this gate is for.
MISSING_BASELINE = (
    "build/u38_probe/u38_A1..A10.raw.txt",
)

# The appendix face's own frozen set (U-65). Empty by measurement, not by default: 附页第三十八行
# stores that judging this face against the ledger's two sets reads missing=0 and uncoded=0 -- the
# appendix has no broken citation and no uncoded host run -- while the same run prints removed=1 and
# host_removed=14, which are only the ledger's history missing from this face. So there is nothing to
# freeze here. An empty set is the strict direction: every unresolved citation on this face is ADDED,
# with nothing to excuse it. Re-emitted by --emit-baseline, which prints both blocks.
REGISTER_MISSING_BASELINE = ()

# Rule 4's patterns, transcribed from the host's own Write-Ok / Write-Err call sites (tools/ci.ps1:715
# and :697 after U-67's step 1l; :677 and :659 in the bytes that round replaced). A log is host
# evidence because the host wrote its verdict into it, so the judgement does not depend on any
# sentence around the citation.
HOST_BANNERS = (
    re.compile(r"^\s*\[OK\]\s+tests:\s*\d+/\d+\s+passed\s*$"),
    re.compile(r"^\s*\[FAIL\]\s+tests failed \("),
)
# Rule 5's wide side (U-55): the two banners above only describe a host that reached the tests step.
# A host that went red at a STATIC step prints that step's own Write-Err sentence instead, and its
# wrapper line then reads verdict_line=NO -- under the narrow rule such a log is not host evidence at
# all, so its exit code is never asked for. The eleven arms of §3.59 are exactly that shape.
# The sentences are READ FROM tools/ci.ps1 rather than transcribed here, in the same direction as
# that file's own docstring: a transcription would be a second copy that can drift, and a drifted copy
# silently un-shapes a log (the weak direction).
# What live reading actually buys, stated at the strength it holds (the first wording here claimed
# more -- "the only way to escape by wording is to delete the Write-Err" -- and a review measured the
# hole: a one-for-one REWORDING does escape, for every log already on disk). Two halves:
#   future  -- a log written after a rewording is host-shaped under whatever wording ci.ps1 carries
#              when that log is read, so no evidence going missing from here on;
#   past    -- a log written under the OLD wording stops matching the moment ci.ps1 is reworded, and
#              wide_markers= below does NOT move, because a rewording swaps one literal for another
#              and the size of the set is unchanged.
# marker_digest= is the reading that closes that blind spot: sha256 over the sorted literals joined
# by LF, first 12 hex, printed on every run, so an add, a drop or a rewording changes a number the
# same replay already prints. It is a reading, not a judgement -- a moved digest does not turn this
# gate red; what remains report-only is which already-written logs stopped being shaped (the
# CITEHOSTREMOVED line, and shaped_wide_only= going down). Deleting a step's Write-Err moves both
# readings, and ci_exit_code_check.py (step 1i) pins the step roster on top of that.
CI_PS1_REL = os.path.join("tools", "ci.ps1")
WRITE_ERR_RX = re.compile(r"""Write-Err\s+(["'])(.*?)\1""")
HARDMISSING_RX = re.compile(r"\$hardMissing\s*\+=\s*'([^']+)'")
FAIL_PREFIX_RX = re.compile(r"^\s*\[FAIL\]\s+")
# Rule 5: the one line tools/ci_host_run.ps1 writes next to the child's own output.
HOST_RC_LINE = re.compile(r"\[HOST-RC\] inner=\S+ ci_exit_code=-?\d+ wrapper_exit_code=-?\d+")

# Host-shaped citations that were already written before rule 5 existed. Frozen by this script's own
# --emit-baseline run on the ledger delivered as bdeea2d (output in build/u44_probe/
# U54_cite_gate_prebaseline_2.txt: host_shaped=12 coded=1 uncoded=11, and the same three numbers from
# an independent reader -- build/u44_probe/u54_host_evidence_census.py, U54_host_evidence_census_3.txt).
# Re-emitted the same way in U-55 after the wide reading above was added, on the ledger delivered as
# dc4a17e (build/u44_probe/U55_cite_gate_wide_1.txt): 11 -> 14, the three additions being host runs
# that went red at a STATIC step before tools/ci_host_run.ps1 existed, so their code was never written.
# Replace only by re-running that command; the direction judged is ADDED, so every replacement after
# this one can only shrink the list. It has grown exactly once -- the 11 -> 14 re-emission above.
HOSTRC_BASELINE = (
    "build/g2_probe/probe_A1_inventory_red.txt",
    "build/g3_probe/ci_e2e_328.txt",
    "build/g3_probe/ci_e2e_final_328.txt",
    "build/g3_probe/res_ci_red.txt",
    "build/u21_probe/ci_final.txt",
    "build/u28_probe/ci_run1.log",
    "build/u29_ci_run2.log",
    "build/u31_probe/ci_u31_run1.log",
    "build/u32_probe/ci_u36_run1.log",
    "build/u35_probe/ci_run1.log",
    "build/u35_probe/ci_run2.log",
    "build/w1_probe/ci_final.txt",
    "ci_e2e.txt",
    "ci_e2e_green.txt",
)

# The appendix face's host-rule set (U-65). Empty for the same measured reason as
# REGISTER_MISSING_BASELINE: this face reads host_shaped=0 (the pass registered in 附页第三十八行), so
# no citation on it is host evidence at all, and there is nothing uncoded to freeze. Growing it means
# re-running --emit-baseline with --face register and pasting THIS block, not the ledger's.
REGISTER_HOSTRC_BASELINE = ()

# The two faces a run can judge, each bound to its own path and its own pair of frozen sets. The paths
# come from inline_rewrite_check -- the same single source of truth tools/ledger_size_gate.py imports
# and the U44 probe asserts against -- so moving a family file cannot leave this gate reading a path
# that no longer exists while the size gate reads another.
FACES = (
    ("ledger", LEDGER_REL, MISSING_BASELINE, HOSTRC_BASELINE),
    ("register", REGISTER_REL, REGISTER_MISSING_BASELINE, REGISTER_HOSTRC_BASELINE),
)


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def scratch_basenames():
    """Bare file names of every .txt/.log under build/*_probe/, minus the copy worktree, plus the
    basename -> paths map those names came from (rule 5 has to open the file, so names alone are not
    enough). Returns None when there is no scratch tree at all (a fresh clone) -- the declared skip."""
    parent = os.path.join(ROOT, SCRATCH_PARENT_REL)
    if not os.path.isdir(parent):
        return None
    exclude_abs = os.path.normpath(os.path.join(ROOT, SCRATCH_EXCLUDE_REL))
    names, trees, by_name = set(), 0, {}
    for entry in sorted(os.listdir(parent)):
        tree = os.path.join(parent, entry)
        if not (os.path.isdir(tree) and entry.endswith(SCRATCH_SUFFIX)):
            continue
        trees += 1
        for dirpath, dirnames, files in os.walk(tree):
            if os.path.normpath(dirpath) == exclude_abs:
                del dirnames[:]
                continue
            for name in files:
                if name.endswith((".txt", ".log")):
                    names.add(name)
                    by_name.setdefault(name, []).append(os.path.join(dirpath, name))
    if trees == 0:
        return None
    return names, trees, {k: sorted(set(v)) for k, v in by_name.items()}


def resolve_paths(token, by_name):
    """Every file a citation could mean, under classify's own two rules. A bare name that lives under
    two scratch trees resolves to both -- rule 5 judges all of them, so an ambiguous name can only
    make the gate stricter, never excuse a finding by picking one."""
    paths = []
    if os.path.isfile(os.path.join(ROOT, token.replace("/", os.sep))):
        paths.append(os.path.normpath(os.path.join(ROOT, token.replace("/", os.sep))))
    paths.extend(by_name.get(os.path.basename(token.replace("\\", "/")), []))
    return sorted(set(paths))


def step_marker_literals(path=None):
    """The host's own static-step verdict sentences, read from tools/ci.ps1 bytes.

    Each is the part of a Write-Err string before its first '$' (the exit code and the log path are
    interpolated after it), plus the literal lines Preflight collects into $hardMissing. Returns
    (literals, error_string): a ci.ps1 that cannot be read is reported, not swallowed -- callers that
    keep judging narrow banners must say they did.
    """
    try:
        with open(path or os.path.join(ROOT, CI_PS1_REL), encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError as exc:
        return set(), "ci.ps1 not readable: %s" % str(exc)
    literals = set()
    for match in WRITE_ERR_RX.finditer(text):
        literals.add(match.group(2).split("$", 1)[0].rstrip())
    for match in HARDMISSING_RX.finditer(text):
        literals.add(match.group(1))
    literals.discard("")
    return literals, None


def marker_digest(markers):
    """One number for the whole marker set: sha256 over the sorted literals joined by LF, 12 hex.

    Why a digest and not the count: a rewording swaps one literal for another, so wide_markers= is
    unchanged by it and the drift is invisible in the count alone (sixteenth-round review W-1, the
    hole the old comment claimed not to have). An add, a drop or a rewording all move this. It is
    printed, not judged -- see the block comment at CI_PS1_REL for what it does and does not catch.
    """
    return hashlib.sha256("\n".join(sorted(markers)).encode("utf-8")).hexdigest()[:12]


def host_state(path, markers=frozenset()):
    """(narrow_banner_hits, wide_marker_hits, rc_line_hits) for one resolved file, read once.

    Undecodable bytes go through errors=replace: a log that cannot be decoded is not evidence of
    anything, and making it unclassifiable would let a corrupt file dodge the rule by being
    unreadable. The two shapes are counted apart, never merged into one number, because the ledger
    has to show which definition made a citation host evidence.
    """
    with open(path, "rb") as handle:
        text = handle.read().decode("utf-8", errors="replace")
    lines = text.split("\n")
    narrow = sum(1 for ln in lines for pat in HOST_BANNERS if pat.match(ln))
    wide = 0
    for ln in lines:
        hit = FAIL_PREFIX_RX.match(ln)
        if hit and any(ln[hit.end():].startswith(literal) for literal in markers):
            wide += 1
    return narrow, wide, sum(1 for ln in lines if HOST_RC_LINE.search(ln))


def classify(text, names):
    """Split every citation in the ledger into EXACT / SCRATCH / MISSING, keyed as written."""
    exact, scratch, missing = set(), set(), set()
    for m in CITE.finditer(text):
        token = m.group(1).replace("\\", "/")
        if os.path.isfile(os.path.join(ROOT, token.replace("/", os.sep))):
            exact.add(token)
        elif names is not None and os.path.basename(token) in names:
            scratch.add(token)
        else:
            missing.add(token)
    return exact, scratch, missing


def face_abs_path(rel):
    return rel if os.path.isabs(rel) else os.path.join(ROOT, rel)


def read_face(label, rel):
    """One face's bytes, or None when it cannot be opened.

    Read before the scratch decision, exactly as the single-face gate always did: an unreadable input
    is an input error (exit 2) whether or not this machine has the scratch trees, so the declared skip
    can never mask a missing ledger.

    The label is printed because a face name in an error is the reader's only clue which of the two
    members the command could not open. It stays "ledger" for that member, whose wording both the U47
    probe's M1 arm and the readings registered in the ledger match on literally."""
    abs_path = face_abs_path(rel)
    try:
        with open(abs_path, encoding="utf-8", newline="") as handle:
            return handle.read()
    except OSError as exc:
        print("ERROR %s not readable: %s (%s)" % (esc(label), esc(abs_path), esc(str(exc))))
        return None


def print_host_def_lines(marker_error, markers):
    """The rule-4 definition reading, printed once per run: it comes out of tools/ci.ps1, which is the
    same file for every face, so a two-face run must not print the same number twice and leave a
    reviewer to guess which face made it. Called by judge_face when only one face is judged (the
    single-face output stays byte-identical to this gate's own registered readings), and by main once
    before the faces when both are."""
    if marker_error:
        print("CITEHOSTDEF %s -- the wide side cannot be read, so ONLY the two narrow banners are "
              "judged this run. DECLARED, not silent: the narrow rule still fires, and every citation "
              "that is host-shaped just by a static marker would go unjudged." % esc(marker_error))
    print("CITEHOSTDEF ci_ps1=%s narrow_patterns=%d wide_markers=%d readable=%s marker_digest=%s"
          % (esc(CI_PS1_REL.replace("\\", "/")), len(HOST_BANNERS), len(markers),
             "no" if marker_error else "yes",
             "none" if marker_error else marker_digest(markers)))


def judge_face(face, text, state, emit_baseline, print_host_def, multi, markers, marker_error):
    """Judge ONE face against ITS OWN two frozen sets and return its finding count.

    Every line prefix and sentence below keeps the wording this gate had before the family split, on
    purpose: tools/ci.ps1 step 1k shows only lines starting with CITE / ERROR / INV citation_baseline /
    INV verdict, and tools/probes/U47_citation_host_exit_probe.py reads INV citation_baseline= and
    CITE scratch_trees= out of that same text by regex, and the ledger registers those readings as
    exact numbers. So the per-face lines keep their shape -- including the CITE ledger= path line,
    which is why a face's own name does not go there -- and the name rides on the two new lines this
    round adds (CITEFACE / CITEFIND), which start with CITE so the host displays them as well.
    """
    label, rel, missing_baseline, hostrc_baseline = face
    names, trees, by_name = state
    if multi:
        print("CITEFACE face=%s rel=%s missing_baseline=%d hostrc_baseline=%d"
              % (esc(label), esc(rel.replace("\\", "/")), len(missing_baseline),
                 len(hostrc_baseline)))
    print("CITE ledger=%s bytes=%d" % (esc(face_abs_path(rel)), len(text.encode("utf-8"))))

    exact, scratch, missing = classify(text, names)
    citations = exact | scratch | missing
    print("CITE scratch_trees=%d scratch_files=%d citations_distinct=%d present_exact=%d "
          "present_scratch=%d missing=%d"
          % (trees, len(names), len(citations), len(exact), len(scratch), len(missing)))

    baseline = set(missing_baseline)
    added = sorted(missing - baseline)
    removed = sorted(baseline - missing)
    for token in added:
        print("CITEADD dir=ADDED cite=%s" % esc(token))
    for token in removed:
        print("CITEREMOVED dir=REMOVED cite=%s (report only)" % esc(token))
    print("INV citation_baseline=%d observed_missing=%d added=%d removed=%d (added=%d removed=%d "
          "cites)" % (len(baseline), len(missing), len(added), len(removed),
                      len(added), len(removed)))

    # Rule 5: of the citations that resolve, which ones the host itself wrote into -- and do they
    # carry the wrapper's contract line. Keyed by the token as the ledger wrote it, same as rule 3.
    # "Host wrote into it" is read two ways (see HOST_BANNERS / step_marker_literals): the tests
    # banners, and any static step's own verdict sentence as ci.ps1 spells it today.
    if print_host_def:
        print_host_def_lines(marker_error, markers)
    host_shaped, coded, uncoded, ambiguous = set(), set(), set(), set()
    narrow_shaped, wide_only_shaped = set(), set()
    for token in sorted(exact | scratch):
        paths = resolve_paths(token, by_name)
        if len(paths) > 1:
            ambiguous.add(os.path.basename(token.replace("\\", "/")))
        narrow = wide = rc_lines = 0
        for path in paths:
            hits_n, hits_w, coded_hits = host_state(path, markers)
            narrow += hits_n
            wide += hits_w
            rc_lines += coded_hits
        if narrow:
            narrow_shaped.add(token)
        if wide and not narrow:
            wide_only_shaped.add(token)
        if narrow or wide:
            host_shaped.add(token)
            (coded if rc_lines else uncoded).add(token)
    host_baseline = set(hostrc_baseline)
    host_added = sorted(uncoded - host_baseline)
    host_removed = sorted(host_baseline - uncoded)
    for token in host_added:
        print("CITEHOSTADD dir=ADDED cite=%s (a host verdict sentence is in the file -- tests banner "
              "or a static step's own marker -- and the [HOST-RC] contract line is absent)" % esc(token))
    for token in host_removed:
        print("CITEHOSTREMOVED dir=REMOVED cite=%s (report only)" % esc(token))
    print("CITEHOST host_shaped=%d coded=%d uncoded=%d host_baseline=%d host_added=%d host_removed=%d "
          "ambiguous_basenames=%d shaped_narrow=%d shaped_wide_only=%d"
          % (len(host_shaped), len(coded), len(uncoded), len(host_baseline), len(host_added),
             len(host_removed), len(ambiguous), len(narrow_shaped), len(wide_only_shaped)))

    if emit_baseline:
        tag = " for face=%s" % esc(label) if multi else ""
        print("== fresh MISSING_BASELINE block%s (review before pasting) ==" % tag)
        for token in sorted(missing):
            print('    "%s",' % token)
        print("== fresh HOSTRC_BASELINE block%s (review before pasting) ==" % tag)
        for token in sorted(uncoded):
            print('    "%s",' % token)

    findings = len(added) + len(host_added)
    if multi:
        print("CITEFIND face=%s findings=%d added=%d host_added=%d"
              % (esc(label), findings, len(added), len(host_added)))
    return findings


def main(argv):
    ledger_arg = None
    register_arg = None
    face_arg = None
    emit_baseline = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == "--emit-baseline":
            emit_baseline = True
        elif a == "--ledger" and i + 1 < len(argv):
            ledger_arg = argv[i + 1]
            i += 1
        elif a == "--register" and i + 1 < len(argv):
            register_arg = argv[i + 1]
            i += 1
        elif a == "--face" and i + 1 < len(argv):
            face_arg = argv[i + 1]
            i += 1
        else:
            print("ERROR unknown or incomplete option: %s" % esc(a))
            return 2
        i += 1

    # Which faces this run judges: no option = the whole family, in ledger-then-appendix order; --face
    # = one of them. Bare --ledger keeps its pre-split meaning (a copy judged with the LEDGER's two
    # frozen sets, no --face needed), so every copy-judging arm of the U47 probe and the artifact
    # reading 第三十八行 stores both stay reproducible exactly as written.
    faces = list(FACES)
    if face_arg is not None:
        faces = [f for f in FACES if f[0] == face_arg]
        if not faces:
            print("ERROR unknown face: %s (known: %s)"
                  % (esc(face_arg), esc(", ".join(f[0] for f in FACES))))
            return 2

    # One path override per face. A teeth run has to be able to put a doctored copy in place of one
    # face and a clean copy in place of its sibling, so that "only the appendix arm grew" is decided
    # inside ONE judgement rather than inferred from two. Overriding a face this run is not judging is
    # an error, not a silence: the alternative is a run that quietly ignores the copy you handed it.
    overrides = dict((label, path) for label, path in (("ledger", ledger_arg),
                                                       ("register", register_arg))
                     if path is not None)
    if overrides and face_arg is None:
        # A path override without --face narrows the run to the faces it names. This is what keeps the
        # pre-split meaning of bare --ledger intact: one face, the ledger's own two frozen sets, one
        # set of lines -- the shape every registered reading of that command has, and the shape the
        # U47 probe's G2/G3/M1/H1 arms write into step 1k's argument line.
        faces = [f for f in faces if f[0] in overrides]
    for label, path in sorted(overrides.items()):
        if not [f for f in faces if f[0] == label]:
            print("ERROR --%s names a face this run does not judge (faces here: %s)"
                  % (label, esc(",".join(f[0] for f in faces))))
            return 2
        faces = [(f[0], path, f[2], f[3]) if f[0] == label else f for f in faces]
    multi = len(faces) > 1

    texts = []
    for face in faces:
        text = read_face(face[0], face[1])
        if text is None:
            return 2
        texts.append(text)

    state = scratch_basenames()
    if state is None:
        if multi:
            print("CITEFACES faces=%d labels=%s"
                  % (len(faces), esc(",".join(f[0] for f in faces))))
        print("CITE-SKIP scratch tree absent (no %s/*%s directory exists here, and those trees are "
              "git-ignored, so a clone that has never built the self-proofs has none) -- every cited "
              "log would read missing, and judging that would stop a clean clone. DECLARED SKIP, not "
              "a pass: no citation was checked."
              % (SCRATCH_PARENT_REL, SCRATCH_SUFFIX))
        print("INV verdict=SKIP exit=0")
        return 0

    markers, marker_error = step_marker_literals()
    if multi:
        print("CITEFACES faces=%d labels=%s"
              % (len(faces), esc(",".join(f[0] for f in faces))))
        print_host_def_lines(marker_error, markers)

    per_face = []
    for face, text in zip(faces, texts):
        findings = judge_face(face, text, state, emit_baseline, not multi, multi, markers,
                              marker_error)
        per_face.append((face[0], findings))

    total = sum(count for _, count in per_face)
    if multi:
        print("INV faces=%d per_face_findings=%s"
              % (len(per_face),
                 esc(",".join("%s:%d" % (label, count) for label, count in per_face))))
    if total:
        print("INV findings=%d" % total)
        print("INV verdict=RED exit=1 (a CITEADD is a 日志 citation the ledger makes and this machine "
              "cannot open; a CITEHOSTADD is a citation whose file the host itself wrote a verdict "
              "into and which carries no [HOST-RC] line, so its exit code was never measured)")
        return 1
    print("INV findings=0")
    print("INV verdict=GREEN exit=0 (two assertions: missing-vs-MISSING_BASELINE and "
          "host-shaped-without-contract-line-vs-HOSTRC_BASELINE, both ADDED direction; REMOVED is "
          "report-only)")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    try:
        sys.exit(main(sys.argv))
    except Exception as exc:
        print("ERROR script failed: %s %s" % (type(exc).__name__,
                                              esc(str(exc))[:200]))
        sys.exit(3)

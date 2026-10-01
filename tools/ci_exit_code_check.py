"""ci.ps1 exit-code geometry check -- the static half of what arm C1 of the U-43 probe measured by
hand, turned into something the host can call every run (seventh-round review S-1).

Why this exists: step 1h maps "the citation roster went red" onto host exit code 10. The U-43 probe
proved that mapping once, by hand, and nothing calls the probe -- not CI, not ctest -- so a later
round could edit ci.ps1's `if ($nocandCode -ne 0) { ... exit 10 }` into something that prints a
failure and keeps walking, and no gate would say so. That is G-1/G-2's dead-gate shape one layer
down: a gate whose *wiring* is nobody's business. This script makes it the host's business.

What is pinned, all of it read from the bytes of the file under test:
  A  every executable stop has a row in the header exit-code table (an undocumented stop is a code
     nobody can look up -- A2's lesson, quoted in the probe too)
  B  every header row has an executable stop (a row for a code that no longer fires advertises a
     gate that is not there)
  C  the per-code counts of the pre-configure gate codes equal the frozen table, so no step can
     quietly take over another step's code (this is C1's "counts unmoved" leg, made runnable)
  D  the ordered list of Write-Step headings equals the frozen list -- adding, renaming or dropping
     a step is a deliberate registration, not a side effect of the next edit
  E  each gate step's own stop lives inside that step's region and in no other step's region
     (C1 could only say "step 1h's line is somewhere in the file"; this leg says WHERE)

Deliberately not pinned: host codes 0/1/2 (success, build, tests). Those branches multiply for
legitimate reasons when the build or test stage is restructured, and A/B still require them to stay
documented. Preflight shares code 3 with the hygiene step's interpreter check, so 3 is frozen by
count (C) and not by ownership (E).

Exit: 0 = geometry as pinned | 1 = drifted (each finding printed as a GEOMETRY line)
      | 2 = the file or its comment block is unreadable | 3 = script crash.
Output is ASCII-only; names and paths go through esc() because the console is cp936.

Run:  python tools/ci_exit_code_check.py                  (checks tools/ci.ps1 of this repo)
      python tools/ci_exit_code_check.py --path <file>    (check a copy -- what the probe injects)
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CI_REL = os.path.join("tools", "ci.ps1")

STOP_RX = re.compile(r"^[ \t]*exit[ \t]+(\d+)[ \t]*$")
STEP_RX = re.compile(r'Write-Step "([^"]+)"')

# The Write-Step headings of ci.ps1, in file order, frozen from the bytes this round delivers: the
# eleven that 5ce4449 already had plus the two this round wires in (1i = this script, 1j =
# tools/ledger_size_gate.py). Leg D compares against exactly this list.
EXPECTED_STEPS = [
    "Preflight",
    "Repository hygiene",
    "QtTest gate self-test",
    "Documentation check",
    "Documentation anchors",
    "Dangling-timer site inventory",
    "Duplicated Chinese sentence baseline",
    "Source-line citation roster baseline",
    "Host exit-code geometry self-check",
    "Ledger growth split trigger",
    "Build ($Config)",
    "Artifact freshness",
    "Tests (CTest / $Config)",
]

# gate step -> the one host code that step's red must produce. Codes 0/1/2 and the shared 3 are
# explained at the top of the file.
GATE_CODE_OF_STEP = [
    ("Repository hygiene", "4"),
    ("QtTest gate self-test", "5"),
    ("Documentation check", "6"),
    ("Documentation anchors", "7"),
    ("Dangling-timer site inventory", "8"),
    ("Duplicated Chinese sentence baseline", "9"),
    ("Source-line citation roster baseline", "10"),
    ("Host exit-code geometry self-check", "11"),
    ("Ledger growth split trigger", "12"),
]

# Frozen executable-stop counts of the gate codes. 3 reads 2 because the interpreter is required
# twice before configure (preflight, then again where the hygiene step needs it); every other gate
# code fires from exactly one place.
FROZEN_GATE_COUNTS = {
    "3": 2, "4": 1, "5": 1, "6": 1, "7": 1, "8": 1, "9": 1, "10": 1, "11": 1, "12": 1,
}
SHARED_CODES = ("0", "1", "2", "3")


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def parse(text):
    """Return (header row codes, [(step heading, [codes in its region])], [all executable codes]).

    The header rows are taken at the indent of the first row (code 0), so the continuation lines of
    a wrapped row are not counted as rows -- measured on the delivered bytes, the wrapped row for 10
    otherwise reads a phantom code 3 out of "3 = script crash".

    A step heading is defined as a Write-Step call written at column 0 with a double-quoted string,
    which is how every gate step is written. That excludes three headings -- the indented "Clean build
    directory" and "Configure (Visual Studio 17 2022 / x64)", plus the single-quoted Summary lines.
    Not a blind spot: none of them owns a gate code, legs A/B/C read every stop inside them anyway, and
    a stray gate stop still lands in the region of the step that precedes it, where leg E sees it.
    """
    block = re.search(r"<#(.*?)\n#>", text, re.S)
    if block is None:
        return None, [], []
    header, body = block.group(1), text[block.end():]

    rows, indent = [], None
    for line in header.split("\n"):
        m = re.match(r"^(\s*)(\d+) = ", line)
        if m is None:
            continue
        if indent is None:
            indent = len(m.group(1))
        if len(m.group(1)) == indent:
            rows.append(m.group(2))

    lines = body.split("\n")
    stops = [(i, STOP_RX.match(ln).group(1)) for i, ln in enumerate(lines) if STOP_RX.match(ln)]
    heads = [(i, STEP_RX.match(ln).group(1)) for i, ln in enumerate(lines) if STEP_RX.match(ln)]
    steps = []
    for pos, (start, name) in enumerate(heads):
        end = heads[pos + 1][0] if pos + 1 < len(heads) else len(lines)
        steps.append((name, [c for (i, c) in stops if start <= i < end]))
    return rows, steps, [c for _i, c in stops]


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    argv = sys.argv[1:]
    path = os.path.join(ROOT, CI_REL)
    i = 0
    while i < len(argv):
        if argv[i] == "--path" and i + 1 < len(argv):
            path = argv[i + 1]
            i += 2
        else:
            print("ERROR unknown or incomplete option: %s" % esc(argv[i]))
            return 2
    if not os.path.isfile(path):
        print("ERROR file not found: %s" % esc(path))
        return 2

    header, steps, stops = parse(io.open(path, encoding="utf-8", errors="replace").read())
    if header is None:
        print("ERROR no <# ... #> comment block in %s" % esc(path))
        return 2
    if not steps:
        print("ERROR no Write-Step heading found in %s -- the parser is out of date with the host"
              % esc(path))
        return 2

    counts = {}
    for c in stops:
        counts[c] = counts.get(c, 0) + 1
    findings = []

    for c in sorted(set(stops), key=int):
        if c not in header:
            findings.append("A stop code %s fires %d time(s) but has no header row" % (c, counts[c]))
    for c in header:
        if c not in counts:
            findings.append("B header row %s has no executable stop -- the table advertises a gate "
                            "that is not wired" % c)

    for c in sorted(FROZEN_GATE_COUNTS, key=int):
        if counts.get(c, 0) != FROZEN_GATE_COUNTS[c]:
            findings.append("C gate code %s fires %d time(s), frozen %d"
                            % (c, counts.get(c, 0), FROZEN_GATE_COUNTS[c]))
    for c in sorted(set(counts) - set(FROZEN_GATE_COUNTS) - set(SHARED_CODES), key=int):
        findings.append("C gate code %s fires %d time(s) but is not in the frozen gate counts -- a "
                        "new stop must be registered here on purpose" % (c, counts[c]))

    names = [n for n, _c in steps]
    if names != EXPECTED_STEPS:
        findings.append("D step list drifted: read %d step(s), frozen %d -- missing %s, extra %s"
                        % (len(names), len(EXPECTED_STEPS),
                           esc(",".join(sorted(set(EXPECTED_STEPS) - set(names))) or "none"),
                           esc(",".join(sorted(set(names) - set(EXPECTED_STEPS))) or "none")))
    else:
        by_name = dict(steps)
        for step, code in GATE_CODE_OF_STEP:
            inside = by_name.get(step, [])
            if inside.count(code) != 1:
                findings.append("E step %s owns %d copy(ies) of exit %s, expected exactly 1 (its "
                                "region reads %s)"
                                % (esc(step), inside.count(code), code,
                                   esc(" ".join(sorted(set(inside), key=int)) or "none")))
            elsewhere = [n for (n, cs) in steps
                         if n != step and code in cs and code not in SHARED_CODES]
            if elsewhere:
                findings.append("E exit %s also fires inside %s -- a gate code belongs to one step"
                                % (code, esc(",".join(elsewhere))))

    print("INV target=%s" % esc(path))
    print("INV header_codes=%s" % " ".join(header))
    print("INV executable_stops=%d counts=%s"
          % (len(stops), " ".join("%s:%d" % (c, counts[c]) for c in sorted(counts, key=int))))
    print("INV step_count=%d frozen_steps=%d frozen_gate_codes=%d"
          % (len(steps), len(EXPECTED_STEPS), len(FROZEN_GATE_COUNTS)))
    for step, code in GATE_CODE_OF_STEP:
        print("INV gate_step=%s code=%s" % (esc(step), code))
    print("INV findings=%d" % len(findings))
    for f in findings:
        print("GEOMETRY %s" % esc(f))
    print("INV verdict=%s" % ("GREEN" if not findings else "RED"))
    return 1 if findings else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # a crash must not read as a clean geometry
        print("ERROR script failed: %s %s" % (type(exc).__name__, esc(str(exc))[:200]))
        sys.exit(3)

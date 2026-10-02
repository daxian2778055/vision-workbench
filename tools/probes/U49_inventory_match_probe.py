"""Promoted copy of the twelfth-round W-2 control face, now called by the ledger writer itself.

Thirteenth-round review suggestion 2: the writer used to carry ledger_named= as a hand-filled
constant. That constant was an EXISTENCE check (it holds, so the token gets substituted), not a
VALUE check -- a later round could edit it to any number and no gate would go red. From this round
the writer runs this script against the bytes it is about to deliver and reads the number off this
script's own stdout; a non-zero rc, a missing ledger_named= field, or a verdict other than MATCH all
hold the write. The scratch original (build/u44_probe/u49_inventory_match.py) is left untouched as
round twelve's frozen face; this tracked copy is the one judged from now on, so the definition a
reviewer can check out of the commit is the definition the writer uses.

Three counts, against [U47-INFO] LK inventory (the LK leg's own stdout, not a list copied by hand):
every driver the leg prints has to appear in the ledger's per-driver row, the row may not silently
grow a name the leg does not know (only the two declared non-drivers are allowed, because that row
has to spell out which script runs on whose behalf), and no inventory entry may carry the leg's own
(NO-LOCK) tag.

Run: python tools/probes/U49_inventory_match_probe.py <log carrying the inventory line> <ledger path>
Exit: 0 = the row covers the printed set; 1 = missing / undeclared extra / a NO-LOCK entry / the
anchor row not found exactly once / no inventory line; 2 = usage.

Two things a reader should not have to guess. (a) When the log carries more than one inventory line
-- a log accumulates legs, and a re-run inside one file is legal -- the LAST one is the one judged;
the earlier lines are ignored, so a stale list cannot satisfy the row. (b) This script is not in the
LK leg's driver set, and the reason is NOT that it is read-only: the leg decides the tools/probes
side by looking for the quoted scratch directory name in the file text, and this file does not
contain that token (grep it: 0 hits, same for the S-3 script). The invariant worth stating out loud,
because it is the one that will bite: any future probe that adds the quoted token joins the set, and
the leg then requires it against TRACKED_SCRATCH_DRIVERS -- an unlisted new driver goes red on the
equality check, it does not slide in.
"""
import io
import os
import re
import sys

PREFIX = u"| 上锁之后每个驱动各跑一遍，没有一家被锁挡死 |"
INVENTORY = "[U47-INFO] LK inventory: "
PY = re.compile(r"[A-Za-z0-9_]+\.py")
DECLARED_NON_DRIVERS = ("u45_pin_check.py", "u47_lock_teeth.py")


def inventory_of(log):
    lines = io.open(log, encoding="utf-8", errors="replace").read().split("\n")
    inv_lines = [ln for ln in lines if INVENTORY in ln]
    if not inv_lines:
        return None
    items = inv_lines[-1].split(INVENTORY, 1)[1].split()
    drivers, unlocked = [], []
    for item in items:
        if item.endswith("(NO-LOCK)"):
            item = item[: -len("(NO-LOCK)")]
            unlocked.append(item)
        drivers.append(os.path.basename(item.replace("\\", "/")))
    return drivers, unlocked


def main():
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) != 3:
        print("[W2-USE] python tools/probes/U49_inventory_match_probe.py <log> <ledger>")
        return 2
    log, ledger = sys.argv[1], sys.argv[2]
    for p in (log, ledger):
        if not os.path.isfile(p):
            print("[W2-HOLD] unreadable face: %s" % p)
            return 1
    print("[W2-FACE] log=%s ledger=%s anchor=%r declared_non_drivers=%s"
          % (os.path.basename(log), os.path.abspath(ledger), PREFIX[:20],
             ",".join(sorted(DECLARED_NON_DRIVERS))))

    got = inventory_of(log)
    if got is None:
        print("[W2-HOLD] %s carries no %s line" % (os.path.basename(log), INVENTORY.strip()))
        return 1
    drivers, unlocked = got
    names = set(drivers)
    if len(names) != len(drivers):
        print("[W2-HOLD] the inventory line repeats a driver: %s" % (drivers,))
        return 1

    text = io.open(ledger, encoding="utf-8", errors="replace").read()
    rows = [(n, ln) for n, ln in enumerate(text.split("\n"), 1) if ln.startswith(PREFIX)]
    if len(rows) != 1:
        print("[W2-HOLD] anchor row found %d time(s), expected exactly 1" % len(rows))
        return 1
    row_no, row = rows[0]
    named = set(PY.findall(row))
    missing = sorted(names - named)
    extra = sorted(named - names - set(DECLARED_NON_DRIVERS))
    verdict = ("MATCH" if not missing and not extra and not unlocked else "MISMATCH")
    # The field names and their order are the same as round twelve's scratch script printed, so a
    # reader can compare the two readings line for line.
    print("[W2-READING] inventory=%d ledger_named=%d missing=%d(%s) undeclared_extra=%d(%s) "
          "declared_non_drivers=%s unlocked_in_inventory=%d(%s) verdict=%s"
          % (len(names), len(named), len(missing), ",".join(missing) or "-", len(extra),
             ",".join(extra) or "-", ",".join(sorted(DECLARED_NON_DRIVERS)), len(unlocked),
             ",".join(os.path.basename(u.replace("\\", "/")) for u in unlocked) or "-", verdict))
    print("[W2-NAMES] anchor_line=%d named=%s" % (row_no, ",".join(sorted(named))))
    return 0 if verdict == "MATCH" else 1


if __name__ == "__main__":
    sys.exit(main())

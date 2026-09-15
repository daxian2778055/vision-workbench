#!/usr/bin/env python3
"""Repository hygiene and workflow-policy checks.

Runs both in CI (.github/workflows/repo-hygiene.yml, GitHub-hosted) and locally:

    python tools/repo_hygiene.py

Every check below encodes an incident that actually happened in this repository, so the
mistake cannot come back unnoticed:

  1. secret scan            - a runner registration token was pasted into a chat/screenshot
                              during setup; never let one reach the tree.
  2. tracked large files    - thirdparty/ and dist/ are gitignored on purpose.
  3. workflow YAML parses    - a broken workflow fails at dispatch with no useful message.
  4. actions pinned to SHA   - this repo enforces "pin to full-length commit SHA"; a plain
                              @v5 reference makes the job die in Job initialize.
  5. self-hosted + PR guard  - the self-hosted runner executes the workspace, so a
                              pull_request trigger without a same-repo guard would let fork
                              PRs run arbitrary code on the build machine.
  6. tools/ci.ps1 stays ASCII- Windows PowerShell 5.1 reads BOM-less .ps1 as ANSI, which
                              garbles non-ASCII text and can corrupt script logic.
  7. .gitignore invariants   - build/, thirdparty/, dist/ and ci-*.log must stay ignored.

Messages are ASCII-only so they render correctly in any console.
"""
import glob
import os
import re
import subprocess
import sys

FAILURES = []
NOTES = []


def tracked_files():
    try:
        out = subprocess.run(["git", "ls-files"], capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        FAILURES.append("cannot run 'git ls-files': %s" % exc)
        return []
    return [line.strip() for line in out.stdout.splitlines() if line.strip()]


def read_text(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as handle:
            return handle.read()
    except OSError:
        return ""


def check_secrets(files):
    patterns = [
        (re.compile(r"CAXT[0-9A-Z]{20,}"), "GitHub runner registration token"),
        (re.compile(r"gh[pousr]_[0-9A-Za-z]{20,}"), "GitHub access token"),
        (re.compile(r"AKIA[0-9A-Z]{16}"), "AWS access key id"),
        (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key block"),
    ]
    for path in files:
        text = read_text(path)
        if not text:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            for pattern, label in patterns:
                if pattern.search(line):
                    FAILURES.append("secret scan: %s:%d looks like a %s" % (path, number, label))


def check_large_files(files, limit_mb=5):
    limit = limit_mb * 1024 * 1024
    for path in files:
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        if size > limit:
            FAILURES.append(
                "large file: %s is %.1f MB (> %d MB); keep binaries out of the repository"
                % (path, size / 1048576.0, limit_mb))


def workflow_files():
    return sorted(glob.glob(".github/workflows/*.yml") + glob.glob(".github/workflows/*.yaml"))


def check_workflows():
    try:
        import yaml
    except ImportError:
        NOTES.append("pyyaml missing: workflow syntax check skipped (install it: pip install pyyaml)")
        yaml = None

    workflows = workflow_files()
    if not workflows:
        NOTES.append("no workflow files found")

    sha_ref = re.compile(r"@[0-9a-f]{40}$")
    uses_line = re.compile(r"^\s*-?\s*uses:\s*(\S+)", re.M)
    pr_trigger = re.compile(r"^\s*pull_request(_target)?\s*:", re.M)
    # match on the runs-on declaration, not on any mention of the word in comments
    self_hosted = re.compile(r"^\s*runs-on:.*self-hosted", re.M)

    for path in workflows:
        text = read_text(path)

        if yaml is not None:
            try:
                yaml.safe_load(text)
            except Exception as exc:                      # noqa: BLE001 - report whatever it is
                FAILURES.append("workflow yaml: %s does not parse: %s" % (path, exc))

        # every action must be pinned to a full-length commit SHA (repository policy)
        for ref in uses_line.findall(text):
            if ref.startswith("./"):
                continue
            if not sha_ref.search(ref):
                FAILURES.append(
                    "workflow policy: %s uses '%s' - actions must be pinned to a 40-hex commit SHA"
                    % (path, ref))

        # a self-hosted runner under pull_request must carry the same-repo guard
        if self_hosted.search(text) and pr_trigger.search(text):
            if "github.event.pull_request.head.repo.full_name == github.repository" not in text:
                FAILURES.append(
                    "workflow policy: %s runs on a self-hosted runner under pull_request without "
                    "the same-repo guard; fork PRs would execute arbitrary code on the build machine"
                    % path)


def check_ascii_scripts():
    for path in ("tools/ci.ps1",):
        text = read_text(path)
        if not text:
            continue
        for number, line in enumerate(text.splitlines(), 1):
            bad = [ch for ch in line if ord(ch) > 127]
            if bad:
                FAILURES.append(
                    "ascii: %s:%d contains non-ASCII %r (Windows PowerShell 5.1 would misread it)"
                    % (path, number, "".join(bad[:5])))


def check_gitignore():
    text = read_text(".gitignore")
    if not text:
        FAILURES.append(".gitignore missing or empty")
        return
    required = ["build/", "thirdparty", "dist/", "ci-*.log"]
    for entry in required:
        if entry not in text:
            FAILURES.append(".gitignore: required entry %r is missing" % entry)


def main():
    files = tracked_files()
    check_secrets(files)
    check_large_files(files)
    check_workflows()
    check_ascii_scripts()
    check_gitignore()

    print("checked %d tracked files, %d workflow(s)" % (len(files), len(workflow_files())))
    for note in NOTES:
        print("[note] " + note)
    for failure in FAILURES:
        print("[FAIL] " + failure)

    if FAILURES:
        print("repo hygiene: %d problem(s) found" % len(FAILURES))
        return 1
    print("repo hygiene: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

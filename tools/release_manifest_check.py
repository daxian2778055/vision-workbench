#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""发布包一致性门禁（tag ↔ manifest ↔ exe md5）。

为什么要有它：2026-10-23 的复核发现"发布检查靠人和文字"——包内 `版本说明.txt` 虽然由
tools/deploy.ps1 生成，但没有任何命令去核对它；同一轮还实测出 deploy.ps1 在两处会**在写
manifest 前中止**（未打 tag 的 HEAD 上 `describe --exact-match` 非零；受限 PowerShell 缺
`Get-FileHash`），于是"包看起来部署完了、实际没有自识别件"可以无人察觉地发生。
本工具把这件事变成一条命令：

  python tools/release_manifest_check.py --repo .            # auto：HEAD 无 rc-* tag 时明确 SKIP
  python tools/release_manifest_check.py --repo . --mode require
  python tools/release_manifest_check.py --repo . --expect-tag rc-20261023

判据（全部为硬条件，任一不满足即 RED）：
  1. 包目录存在，且包内 `版本说明.txt` 存在、必要键齐全；
  2. 包内 exe 存在，其 md5 与字节数与 manifest 记录逐字一致；
  3. manifest 的提交 == 当前 HEAD（允许 manifest 写短哈希，但须 ≥7 位且为 HEAD 前缀）；
  4. manifest 的"工作树未提交改动条数" == 0（非 0 表示该包不对应任何提交）；
  5. manifest 的标签与 HEAD 上的 rc-* tag 一致（mode=require 时 HEAD 必须有 rc-* tag；
     未打 tag 时 manifest 必须写 none）。

输出契约（ci.ps1 按前缀筛行，故键名稳定）：
  [RM] key=value
  [RM] finding=<code>: <detail>
  [RM-INV] findings=N skipped=0|1 reason=<...>
  [RM-VERDICT] GREEN|RED
退出码：0 = GREEN，1 = RED（脚本自身异常也按 RED 处理，绝不静默放行）。
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys

# Windows 控制台默认 GBK：manifest 的键名是中文，输出需固定 UTF-8，避免编码异常被误读成 RED。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

MANIFEST_NAME = "版本说明.txt"
EXE_NAME = "VisionFlowPlatform.exe"

KEY_COMMIT = "提交"
KEY_TAG = "该提交上的标签"
KEY_DIRTY = "工作树未提交改动条数"
KEY_MD5 = "VisionFlowPlatform.exe md5"
KEY_BYTES = "VisionFlowPlatform.exe 字节"


def out(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def run_git(repo, *args):
    """返回 (rc, stdout.strip, stderr.strip)。git 缺失/失败都由调用方转成 finding。"""
    try:
        p = subprocess.run(["git", "-C", repo] + list(args),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return (p.returncode,
                p.stdout.decode("utf-8", "replace").strip(),
                p.stderr.decode("utf-8", "replace").strip())
    except FileNotFoundError:
        return (127, "", "git not found")
    except Exception as exc:  # noqa: BLE001 - 任何异常都要变成可见 finding
        return (128, "", str(exc))


def parse_manifest(text):
    """manifest 是 '键    : 值' 的纯文本；键名固定（见 deploy.ps1）。"""
    table = {}
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        sep = "\uff1a" if "\uff1a" in line else ":"
        if sep not in line:
            continue
        key, value = line.split(sep, 1)
        table[key.strip()] = value.strip()
    return table


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def decode_text(path):
    with open(path, "rb") as fh:
        data = fh.read()
    # deploy.ps1 写的是 UTF-8 BOM；容忍无 BOM。
    return data.decode("utf-8-sig", "replace")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".", help="仓库根目录（默认当前目录）")
    ap.add_argument("--package", default="", help="包目录（默认 <repo>/dist/VisionFlowPlatform）")
    ap.add_argument("--mode", choices=["auto", "require"], default="auto",
                    help="auto=HEAD 无 rc-* tag 时明确 SKIP；require=必须有 tag 且全一致")
    ap.add_argument("--expect-tag", default="", help="要求的发版 tag（如 rc-20261023）")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    package = os.path.abspath(args.package) if args.package else os.path.join(repo, "dist", "VisionFlowPlatform")
    findings = []

    def finding(code, detail):
        findings.append((code, detail))
        out("[RM] finding=%s: %s" % (code, detail))

    out("[RM] package=%s" % package)
    out("[RM] mode=%s" % args.mode)

    # ---- 0. HEAD / tag 先读：决定 auto 模式是否 SKIP ----
    rc, head, err = run_git(repo, "rev-parse", "HEAD")
    if rc != 0 or not head:
        finding("git_head_unreadable", err or ("git rev-parse rc=%d" % rc))
        head = ""
    else:
        out("[RM] head_commit=%s" % head)

    rc2, tag_lines, err2 = run_git(repo, "tag", "--points-at", head) if head else (1, "", "no head")
    head_tags = [t.strip() for t in tag_lines.splitlines() if t.strip()]
    head_rc_tags = [t for t in head_tags if t.startswith("rc-")]
    out("[RM] head_tags=%s" % (",".join(head_tags) if head_tags else "none"))
    if rc2 != 0:
        finding("git_tags_unreadable", err2 or ("git tag rc=%d" % rc2))

    if args.mode == "auto" and not head_rc_tags and not args.expect_tag:
        # dev build：dist/ 被 gitignore，允许陈旧；但必须明确说出来，不能安静通过。
        out("[RM-INV] findings=0 skipped=1 reason=no_rc_tag_on_head")
        out("[RM-VERDICT] GREEN")
        return 0

    # ---- 1. 包与 manifest ----
    if not os.path.isdir(package):
        finding("package_missing", package)
    manifest_path = os.path.join(package, MANIFEST_NAME)
    if not os.path.isfile(manifest_path):
        finding("manifest_missing", manifest_path)
    man = {}
    if os.path.isfile(manifest_path):
        out("[RM] manifest=%s" % manifest_path)
        man = parse_manifest(decode_text(manifest_path))
        for key in (KEY_COMMIT, KEY_TAG, KEY_DIRTY, KEY_MD5, KEY_BYTES):
            if not man.get(key):
                finding("manifest_key_missing", key)

    # ---- 2. exe 与指纹 ----
    exe_path = os.path.join(package, EXE_NAME)
    exe_bytes = None
    exe_md5 = None
    if not os.path.isfile(exe_path):
        finding("exe_missing", exe_path)
    else:
        exe_bytes = os.path.getsize(exe_path)
        exe_md5 = md5_of(exe_path)
        out("[RM] exe_md5=%s" % exe_md5)
        out("[RM] exe_bytes=%d" % exe_bytes)
        if man.get(KEY_MD5):
            if man[KEY_MD5].lower() != exe_md5:
                finding("exe_md5_mismatch",
                        "manifest=%s actual=%s" % (man[KEY_MD5].lower(), exe_md5))
        if man.get(KEY_BYTES):
            try:
                declared = int(re.sub(r"[^0-9]", "", man[KEY_BYTES]) or "0")
            except ValueError:
                declared = -1
            if declared != exe_bytes:
                finding("exe_bytes_mismatch", "manifest=%s actual=%d" % (man[KEY_BYTES], exe_bytes))

    # ---- 3. 提交一致性 ----
    man_commit = (man.get(KEY_COMMIT) or "").lower()
    if man_commit:
        out("[RM] manifest_commit=%s" % man_commit)
        if len(man_commit) < 7:
            finding("commit_too_short", man_commit)
        elif head and not head.lower().startswith(man_commit):
            finding("commit_mismatch", "manifest=%s head=%s" % (man_commit, head.lower()))

    # ---- 4. 干净度 ----
    man_dirty = (man.get(KEY_DIRTY) or "").strip()
    if man_dirty:
        out("[RM] manifest_dirty=%s" % man_dirty)
        try:
            dirty_n = int(re.sub(r"[^0-9-]", "", man_dirty) or "0")
        except ValueError:
            dirty_n = -1
        if dirty_n != 0:
            finding("dirty_worktree",
                    "manifest_dirty=%s（非 0 表示该包不对应任何提交）" % man_dirty)

    # ---- 5. tag 一致性 ----
    man_tag = (man.get(KEY_TAG) or "").strip()
    out("[RM] manifest_tag=%s" % (man_tag or "none"))
    required_tag = args.expect_tag or (head_rc_tags[0] if head_rc_tags else "")
    if args.mode == "require" and not required_tag:
        finding("no_release_tag", "mode=require 但 HEAD 上没有 rc-* tag")
    if required_tag:
        if required_tag not in head_rc_tags:
            # expect-tag 指定了但 HEAD 不指向它（或它根本不在 HEAD 上）
            finding("expected_tag_not_on_head", "expect=%s head_tags=%s"
                    % (required_tag, ",".join(head_rc_tags) if head_rc_tags else "none"))
        if man_tag != required_tag:
            finding("manifest_tag_mismatch", "manifest=%s expected=%s" % (man_tag or "none", required_tag))
    elif man_tag and man_tag.lower() != "none":
        # 没有要求的 tag，但 manifest 声称有：该 tag 必须真的指向 HEAD，否则是伪造/陈旧读数
        if man_tag not in head_tags:
            finding("manifest_tag_not_on_head", "manifest=%s head_tags=%s"
                    % (man_tag, ",".join(head_tags) if head_tags else "none"))

    # ---- 汇总 ----
    skipped = 0
    if findings:
        out("[RM-INV] findings=%d skipped=%d" % (len(findings), skipped))
        out("[RM-VERDICT] RED")
        return 1
    out("[RM-INV] findings=0 skipped=%d" % skipped)
    out("[RM-VERDICT] GREEN")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - 异常必须是 RED，不是静默
        out("[RM] finding=internal_error: %s" % exc)
        out("[RM-INV] findings=1 skipped=0")
        out("[RM-VERDICT] RED")
        sys.exit(1)

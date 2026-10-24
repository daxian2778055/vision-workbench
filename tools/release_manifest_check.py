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
import json
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
KEY_EXE_EMBEDS = "exe 内嵌 HEAD 版本哈希"


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
    ap.add_argument("--package", default="",
                    help="包目录（默认：HEAD 带 rc-* tag 时取 dist/VisionFlowPlatform-<tag>，"
                         "否则 dist/VisionFlowPlatform）")
    ap.add_argument("--mode", choices=["auto", "require"], default="auto",
                    help="auto=HEAD 无 rc-* tag 时明确 SKIP；require=必须有 tag 且全一致")
    ap.add_argument("--expect-tag", default="", help="要求的发版 tag（如 rc-20261023）")
    ap.add_argument("--registry", default="",
                    help="发版登记表（默认 <repo>/docs/release-registry.json）")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    registry_path = os.path.abspath(args.registry) if args.registry else \
        os.path.join(repo, "docs", "release-registry.json")
    findings = []

    def finding(code, detail):
        findings.append((code, detail))
        out("[RM] finding=%s: %s" % (code, detail))

    # ---- 0. HEAD / tag 先读：决定 auto 模式是否 SKIP、以及校验哪个包 ----
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

    # 带 tag 的 HEAD：装机只认"带日期的包"（dist/VisionFlowPlatform-<tag>）。
    # 无日期目录是开发构建，拿它当发版件正是 2026-10-23 复核指出的误区——所以这里不退回
    # 校验无日期目录，而是直接 RED：打了 tag 就必须有对应的带日期发版包。
    # 注意只对"tag 在 HEAD 上"（CI 场景）强制；显式 --expect-tag 是发版后手工复核
    # （HEAD 早已前进），此时仍可显式 --package 指到具体包，否则用默认目录。
    # 多 tag 同指一枚提交时：--expect-tag 明确指定了就听它的，否则取第一个（head_rc_tags
    # 已按 git 输出序；此处不再做字母序强约定，只保证"显式指定优先"）。
    tag_on_head = head_rc_tags[0] if head_rc_tags else ""
    if args.expect_tag and args.expect_tag in head_rc_tags:
        tag_on_head = args.expect_tag
    required_tag = args.expect_tag or tag_on_head
    if args.package:
        # 显式 --package 是最高优先级（测试与"就校验这一个目录"的场合），不再要求带日期包
        package = os.path.abspath(args.package)
    elif required_tag:
        bare = required_tag[3:] if required_tag.startswith("rc-") else required_tag
        candidates = [os.path.join(repo, "dist", "VisionFlowPlatform-" + required_tag),
                      os.path.join(repo, "dist", "VisionFlowPlatform-" + bare)]
        dated = next((c for c in candidates if os.path.isdir(c)), None)
        if not dated:
            # 只要解析出了 tag（HEAD 上的或 --expect-tag 显式给的），就必须有带日期包。
            # 不退回无日期开发目录：那会"绿着"报告一个不存在的发版件（2026-10-23 复核 P1-2：
            # 发版后复核、或旧包被搬走后，退回开发目录读到的是无关读数）。
            finding("dated_package_missing", " | ".join(candidates))
            out("[RM-INV] findings=%d skipped=0" % len(findings))
            out("[RM-VERDICT] RED")
            return 1
        # CI（tag 在 HEAD 上）与手工复核（--expect-tag）同一口径：有带日期包就校验它
        package = dated
    else:
        package = os.path.join(repo, "dist", "VisionFlowPlatform")
    out("[RM] package=%s" % package)
    out("[RM] mode=%s" % args.mode)
    out("[RM] required_tag=%s" % (required_tag or "none"))

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
        # 发版件（解析出 tag）额外要求"exe 内嵌 HEAD 版本哈希 == yes"。
        # 为什么是硬条件：deploy 只告警、manifest 只记录，这道闸不读它，那么
        # "改了代码 → 增量重编（没重配，哈希陈旧）→ 打 tag → 打包"就能整条绿着过去，
        # 而包里是一枚旧 exe——正是 2026-10-23 复核报的"新构建从未打包"换位复发。
        # 缺行同样判红：老包没有这一行，就不能声称它证明了自己的来源。
        if required_tag:
            embeds = (man.get(KEY_EXE_EMBEDS) or "").strip().lower()
            if not embeds:
                finding("manifest_key_missing", KEY_EXE_EMBEDS)
            elif embeds != "yes":
                finding("exe_not_built_from_head",
                        "%s=%s（发版件必须为 yes：exe 不是打包时 HEAD 编出来的，先重新构建再打包）"
                        % (KEY_EXE_EMBEDS, embeds))

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

    # ---- 5. tag 先解析出它的提交：发版之后 main 继续前进是常态，
    #         包属于 tag，不属于"当前 HEAD"——用 tag 指向的提交做锚，才不会误红。
    man_tag = (man.get(KEY_TAG) or "").strip()
    tag_commit = ""
    if required_tag:
        rc_tag, tc, err_tag = run_git(repo, "rev-list", "-n", "1", required_tag)
        if rc_tag == 0 and tc:
            tag_commit = tc.strip()
            out("[RM] tag_commit=%s" % tag_commit)
        else:
            finding("tag_unresolvable", "tag=%s %s" % (required_tag, err_tag))

    # ---- 2b. 发版登记（md5 的唯一受跟踪落点）----
    # 为什么必须有：包内 版本说明.txt 在 gitignore 的 dist/ 下、从未入库，而重建出来的 exe
    # 不保证与原件逐字节相同（见 docs/ci-cd-guide.md "dist 目录生命周期"）。所以清包之前
    # 必须先在 docs/release-registry.json 里登记 tag/commit/exe_md5；这里强制核对，
    # 缺行或值不符都判红——否则"删除不丢身份"只是句说法（2026-10-24 复核 P2）。
    if required_tag:
        if not os.path.isfile(registry_path):
            finding("registry_missing", registry_path)
        else:
            try:
                reg = json.loads(decode_text(registry_path))
            except Exception as exc:  # noqa: BLE001 - 坏文件必须是可见 finding
                finding("registry_unreadable", "%s: %s" % (registry_path, exc))
                reg = {}
            entries = {}
            for e in (reg.get("releases") or []):
                if isinstance(e, dict) and e.get("tag"):
                    entries[e["tag"]] = e
            entry = entries.get(required_tag)
            out("[RM] registry_entries=%d" % len(entries))
            if entry is None:
                finding("md5_not_registered",
                        "docs/release-registry.json 里没有 tag=%s 的登记行——清理该包前必须先登记 "
                        "tag/commit/exe_md5（重建不等于原字节）" % required_tag)
            else:
                reg_commit = (entry.get("commit") or "").lower()
                out("[RM] registry_commit=%s" % (reg_commit or "none"))
                if tag_commit and reg_commit != tag_commit.lower():
                    finding("registry_commit_mismatch",
                            "registry=%s tag(%s)=%s" % (reg_commit, required_tag, tag_commit.lower()))
                reg_md5 = (entry.get("exe_md5") or "").lower()
                out("[RM] registry_md5=%s" % (reg_md5 or "none"))
                if exe_md5 and reg_md5 != exe_md5:
                    finding("registry_md5_mismatch",
                            "registry=%s actual=%s" % (reg_md5 or "none", exe_md5))
                reg_bytes = entry.get("exe_bytes")
                if exe_bytes is not None and isinstance(reg_bytes, int) and reg_bytes != exe_bytes:
                    finding("registry_bytes_mismatch",
                            "registry=%s actual=%d" % (reg_bytes, exe_bytes))

    # ---- 3. 提交一致性（锚 = tag 提交；无 tag 时锚 = HEAD）----
    target_commit = (tag_commit or head).lower()
    man_commit = (man.get(KEY_COMMIT) or "").lower()
    if man_commit:
        out("[RM] manifest_commit=%s" % man_commit)
        if len(man_commit) < 7:
            finding("commit_too_short", man_commit)
        elif target_commit and not target_commit.startswith(man_commit):
            finding("commit_mismatch",
                    "manifest=%s anchor=%s%s" % (man_commit, target_commit,
                                                 " (tag %s)" % required_tag if required_tag else " (HEAD)"))

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

    # ---- 5b. tag 一致性（required_tag / man_tag 已在解析 tag 提交时取好）----
    out("[RM] manifest_tag=%s" % (man_tag or "none"))
    out("[RM] required_tag=%s" % (required_tag or "none"))
    if args.mode == "require" and not required_tag:
        finding("no_release_tag", "mode=require but HEAD carries no rc-* tag and --expect-tag is empty")
    if required_tag:
        if man_tag != required_tag:
            finding("manifest_tag_mismatch",
                    "manifest=%s expected=%s" % (man_tag or "none", required_tag))
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

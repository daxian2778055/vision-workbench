#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""release_manifest_check.py 的自测（ci.ps1 在跑正式校验前先跑它）。

为什么要自测：一道"永远 GREEN"的闸比没有闸更危险。这里用临时 git 仓库 + 临时包，
把每个判据分别做绿/做红（无 tag、缺 tag、md5 不符、字节不符、dirty>0、tag 不符、
提交不符、manifest 缺失），断言校验器真的会红、且 finding 码正确。

输出契约：
  [RSM] case=<name> ok|FAIL detail
  [RSM-INV] cases=N failed=M
  [RSM-VERDICT] GREEN|RED
退出码：0 = 自测通过；1 = 自测失败（此时正式校验不应被信任）。
"""

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

# Windows 控制台默认 GBK：manifest 键名与 git stderr 的替换字符（U+FFFD）会触发
# UnicodeEncodeError，把"断言失败"伪装成"harness 崩溃"。统一按 UTF-8 输出。
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKER = os.path.join(HERE, "release_manifest_check.py")
EXE_NAME = "VisionFlowPlatform.exe"
MANIFEST_NAME = "版本说明.txt"


def out(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def sh(cwd, *args):
    p = subprocess.run(list(args), cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError("%s failed: %s" % (" ".join(args), p.stderr.decode("utf-8", "replace")))
    return p.stdout.decode("utf-8", "replace").strip()


def make_repo(root, tag=None):
    os.makedirs(root, exist_ok=True)
    sh(root, "git", "init", "-q")
    sh(root, "git", "config", "user.email", "selftest@example.invalid")
    sh(root, "git", "config", "user.name", "selftest")
    sh(root, "git", "config", "commit.gpgsign", "false")
    with open(os.path.join(root, "README.txt"), "w", encoding="utf-8") as fh:
        fh.write("selftest fixture\n")
    sh(root, "git", "add", "README.txt")
    sh(root, "git", "commit", "-q", "-m", "fixture")
    if tag:
        sh(root, "git", "tag", tag)
    return sh(root, "git", "rev-parse", "HEAD")


def make_package(root, commit, tag, dirty="0", md5_override=None, bytes_override=None,
                 manifest=True, pkg_subdir=None):
    pkg = os.path.join(root, pkg_subdir or os.path.join("dist", "VisionFlowPlatform"))
    os.makedirs(pkg, exist_ok=True)
    exe = os.path.join(pkg, EXE_NAME)
    payload = b"selftest-exe-payload"
    with open(exe, "wb") as fh:
        fh.write(payload)
    real_md5 = hashlib.md5(payload).hexdigest()
    real_bytes = len(payload)
    manifest_path = os.path.join(pkg, MANIFEST_NAME)
    if manifest:
        lines = [
            "VisionFlowPlatform 包内自识别（本文件由 tools/deploy.ps1 生成，请勿手改）",
            "打包时间     : 2026-10-23 12:00:00",
            "提交         : %s" % commit,
            "分支         : main",
            "该提交上的标签: %s" % tag,
            "工作树未提交改动条数: %s" % dirty,
            "VisionFlowPlatform.exe md5 : %s" % (md5_override or real_md5),
            "VisionFlowPlatform.exe 字节 : %s" % (bytes_override or real_bytes),
        ]
        with open(manifest_path, "w", encoding="utf-8-sig") as fh:
            fh.write("\n".join(lines) + "\n")
    else:
        # 夹具复位：上一个用例可能已经写过 manifest，不删会让"缺失"用例读到旧件
        try:
            os.remove(manifest_path)
        except FileNotFoundError:
            pass
    return pkg


def run_checker(repo, extra=None):
    args = [sys.executable, CHECKER, "--repo", repo] + (extra or [])
    p = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    text = p.stdout.decode("utf-8", "replace")
    return p.returncode, text


def expect(case, repo, extra, want_rc, want_verdict, want_findings):
    rc, text = run_checker(repo, extra)
    ok = (rc == want_rc) and ("[RM-VERDICT] %s" % want_verdict) in text
    missing = [f for f in want_findings if ("[RM] finding=%s:" % f) not in text]
    if missing:
        ok = False
    detail = ""
    if not ok:
        detail = "rc=%d want=%d verdict=%s missing=%s" % (rc, want_rc, want_verdict, ",".join(missing))
        # 把被测输出打出来，避免只看到"FAIL"却不知为何
        for line in text.splitlines():
            if line.startswith("[RM]"):
                detail += " | " + line
    out("[RSM] case=%s %s%s" % (case, "ok" if ok else "FAIL", (" " + detail) if detail else ""))
    return ok


def main():
    tmp = tempfile.mkdtemp(prefix="vfp_rm_selftest_")
    results = []
    try:
        # 仓库 A：有 rc tag；仓库 B：无 tag
        repo_tagged = os.path.join(tmp, "repo_tagged")
        commit_tagged = make_repo(repo_tagged, tag="rc-selftest")
        repo_plain = os.path.join(tmp, "repo_plain")
        commit_plain = make_repo(repo_plain)

        # 1) 无 tag + auto → 明确 SKIP 且 GREEN
        make_package(repo_plain, commit_plain, "none")
        results.append(expect("no_tag_auto_skips", repo_plain, [], 0, "GREEN", []))

        # 2) 无 tag + require → RED no_release_tag
        results.append(expect("no_tag_require_red", repo_plain, ["--mode", "require"], 1, "RED",
                              ["no_release_tag"]))

        # 以下用例的仓库 HEAD 带 rc tag，但包放在**无日期**目录——那是"开发构建"形态。
        # 带 tag 的 HEAD 默认只认带日期包（见 3c/3d），所以这些用例显式 --package 指到
        # 无日期目录，继续钉各判据本身。
        pkg_tagged_arg = ["--package", os.path.join(repo_tagged, "dist", "VisionFlowPlatform")]

        # 3) 有 tag + 全一致 → GREEN
        make_package(repo_tagged, commit_tagged, "rc-selftest")
        results.append(expect("coherent_green", repo_tagged, pkg_tagged_arg, 0, "GREEN", []))

        # 3b) 发版后 main 继续前进：包属于 tag，不属于"当前 HEAD"。
        #     显式 --expect-tag 时锚必须是 tag 的提交，否则每次发版后手工复核都会误红。
        repo_moved = os.path.join(tmp, "repo_moved")
        commit_before = make_repo(repo_moved, tag="rc-selftest")
        make_package(repo_moved, commit_before, "rc-selftest")
        with open(os.path.join(repo_moved, "NEXT.txt"), "w", encoding="utf-8") as fh:
            fh.write("post-release commit\n")
        sh(repo_moved, "git", "add", "NEXT.txt")
        sh(repo_moved, "git", "commit", "-q", "-m", "post-release")
        results.append(expect("post_release_head_moved_green", repo_moved,
                              ["--expect-tag", "rc-selftest"], 0, "GREEN", []))

        # 3c) HEAD 带 tag：默认校验"带日期"的包（dist/VisionFlowPlatform-<tag> 或历史约定
        #     的 VisionFlowPlatform-<日期>），不是无日期目录——装机只认带日期的包，
        #     这是 2026-10-23 复核的核心结论之一。
        repo_dated = os.path.join(tmp, "repo_dated")
        commit_dated = make_repo(repo_dated, tag="rc-selftest")
        make_package(repo_dated, commit_dated, "rc-selftest",
                     pkg_subdir=os.path.join("dist", "VisionFlowPlatform-rc-selftest"))
        results.append(expect("dated_package_preferred_green", repo_dated, [], 0, "GREEN", []))

        # 3c-bis) 历史命名：VisionFlowPlatform-<去掉 rc- 的日期> 同样被认
        repo_dated_bare = os.path.join(tmp, "repo_dated_bare")
        commit_dated_bare = make_repo(repo_dated_bare, tag="rc-selftest")
        make_package(repo_dated_bare, commit_dated_bare, "rc-selftest",
                     pkg_subdir=os.path.join("dist", "VisionFlowPlatform-selftest"))
        results.append(expect("dated_package_bare_name_green", repo_dated_bare, [], 0, "GREEN", []))

        # 3d) 打了 tag 却没有带日期包 → RED，且不退回去校验无日期目录
        repo_dated_missing = os.path.join(tmp, "repo_dated_missing")
        make_repo(repo_dated_missing, tag="rc-selftest")
        results.append(expect("dated_package_missing_red", repo_dated_missing, [], 1, "RED",
                              ["dated_package_missing"]))

        # 4) md5 不符 → RED
        make_package(repo_tagged, commit_tagged, "rc-selftest", md5_override="0" * 32)
        results.append(expect("md5_mismatch_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["exe_md5_mismatch"]))

        # 5) 字节数不符 → RED
        make_package(repo_tagged, commit_tagged, "rc-selftest", bytes_override="123")
        results.append(expect("bytes_mismatch_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["exe_bytes_mismatch"]))

        # 6) dirty 非 0 → RED
        make_package(repo_tagged, commit_tagged, "rc-selftest", dirty="3")
        results.append(expect("dirty_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["dirty_worktree"]))

        # 7) manifest tag 与 HEAD 的 rc tag 不一致 → RED
        make_package(repo_tagged, commit_tagged, "rc-other")
        results.append(expect("tag_mismatch_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["manifest_tag_mismatch"]))

        # 8) 提交号不符 → RED
        make_package(repo_tagged, "deadbeefdeadbeefdeadbeefdeadbeefdeadbeef", "rc-selftest")
        results.append(expect("commit_mismatch_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["commit_mismatch"]))

        # 9) manifest 缺失 → RED
        make_package(repo_tagged, commit_tagged, "rc-selftest", manifest=False)
        results.append(expect("manifest_missing_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["manifest_missing"]))

        # 10) 包目录缺失 → RED
        shutil.rmtree(os.path.join(repo_tagged, "dist"), ignore_errors=True)
        results.append(expect("package_missing_red", repo_tagged, pkg_tagged_arg, 1, "RED", ["package_missing"]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    failed = len([r for r in results if not r])
    out("[RSM-INV] cases=%d failed=%d" % (len(results), failed))
    if failed:
        out("[RSM-VERDICT] RED")
        return 1
    out("[RSM-VERDICT] GREEN")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001
        out("[RSM] case=harness_error FAIL %s" % exc)
        out("[RSM-INV] cases=0 failed=1")
        out("[RSM-VERDICT] RED")
        sys.exit(1)

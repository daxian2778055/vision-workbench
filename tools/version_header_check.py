#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Version.h 单一数据源守卫（2026-10-23 复审 P3 建议落地）。

背景：`build/generated/Version.h` 有两个生成器——
  ① configure 期 bootstrap：CMakeLists.txt 里的 configure_file(cmake/Version.h.in @ONLY)
  ② 构建期刷新：cmake/RefreshVersionHeader.cmake（vfp_version_header 目标，每次构建跑）
正文模板已经合一（② 改为读 ① 的模板做替换），但"**哪几个 @TOKEN@ 要被替换**"仍写在两处：
模板里的占位符，与刷新脚本里的 string(REPLACE) 各行。将来模板若新增第四个 token，
configure_file 会替换而刷新脚本不会——漂移会从另一条缝回来（该轮复核靠人工复验才发现
同类问题，人工复验不该是长期机制）。

本脚本把这条承诺变成命令，两条判据：
  A 覆盖：模板里的每个 @TOKEN@ 都必须出现在刷新脚本的 string(REPLACE ...) 里；
  B 等价：两个生成器在**同一组输入**下产出必须逐字节相同
          （跑刷新脚本得一份 → 从该份解析出哈希/日期/版本号 → 用同值跑 configure_file 得另一份 → 比对）。

用法：
  python tools/version_header_check.py --repo .            # 正常校验
  python tools/version_header_check.py --repo . --selftest # 先自证能变红，再校验
输出契约（ci.ps1 按前缀筛行）：
  [VHC] key=value
  [VHC] finding=<code>: <detail>
  [VHC-INV] findings=N
  [VHC-VERDICT] GREEN|RED
退出码：0 = GREEN，1 = RED。
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

TOKEN_RX = re.compile(r"@([A-Z][A-Z0-9_]*)@")
REPLACE_RX = re.compile(r'string\(\s*REPLACE\s+"@([A-Z][A-Z0-9_]*)@"')


def out(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def finding(findings, code, detail):
    findings.append(code)
    out("[VHC] finding=%s: %s" % (code, detail))


def read_text(path):
    with open(path, "rb") as fh:
        return fh.read().decode("utf-8", "replace")


def git_short(repo):
    p = subprocess.run(["git", "-C", repo, "rev-parse", "--short", "HEAD"],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        return "unknown"
    return p.stdout.decode("utf-8", "replace").strip() or "unknown"


def run_cmake(args):
    return subprocess.run(["cmake"] + args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def check(repo, template, refresh_script):
    """返回 findings 列表（空 = GREEN）。"""
    findings = []

    # ---- A 覆盖：模板 token ⊆ 刷新脚本的 REPLACE 行 ----
    tpl_tokens = set(TOKEN_RX.findall(read_text(template)))
    script_tokens = set(REPLACE_RX.findall(read_text(refresh_script)))
    out("[VHC] template_tokens=%s" % (",".join(sorted(tpl_tokens)) or "none"))
    out("[VHC] refresh_replaces=%s" % (",".join(sorted(script_tokens)) or "none"))
    missing = sorted(tpl_tokens - script_tokens)
    if missing:
        finding(findings, "token_not_replaced",
                "模板里的 %s 在 %s 中没有对应 string(REPLACE)——configure_file 会替换、"
                "构建期刷新不会，两头会漂移" % (",".join(missing), os.path.basename(refresh_script)))

    # ---- B 等价：两个生成器同输入 → 逐字节相同 ----
    version = "0.0.0-check"
    with tempfile.TemporaryDirectory(prefix="vfp_vhc_") as tmp:
        head_a = os.path.join(tmp, "Version.h.a")
        p = run_cmake(["-DHEADER=%s" % head_a,
                       "-DTEMPLATE=%s" % template,
                       "-DREPO=%s" % repo,
                       "-DVERSION=%s" % version,
                       "-DGIT=git",
                       "-P", refresh_script])
        if p.returncode != 0 or not os.path.isfile(head_a):
            finding(findings, "refresh_generator_failed",
                    p.stderr.decode("utf-8", "replace").strip()[-400:])
            return findings
        text_a = read_text(head_a)

        # 从 A 的产出解析三个值，作为 B 的输入（不依赖时钟/环境，必然同值）
        def grab(name):
            m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, text_a)
            return m.group(1) if m else ""

        v = grab("VFP_VERSION_STRING")
        h = grab("VFP_GIT_HASH")
        d = grab("VFP_BUILD_DATE")
        out("[VHC] generator_a_version=%s hash=%s date=%s" % (v, h, d))

        script_b = os.path.join(tmp, "b.cmake")
        head_b = os.path.join(tmp, "Version.h.b")
        with open(script_b, "w", encoding="utf-8") as fh:
            fh.write('set(PROJECT_VERSION "%s")\n' % v)
            fh.write('set(VFP_GIT_HASH "%s")\n' % h)
            fh.write('set(VFP_BUILD_DATE "%s")\n' % d)
            fh.write('configure_file("%s" "%s" @ONLY)\n' % (template, head_b))
        p2 = run_cmake(["-P", script_b])
        if p2.returncode != 0 or not os.path.isfile(head_b):
            finding(findings, "bootstrap_generator_failed",
                    p2.stderr.decode("utf-8", "replace").strip()[-400:])
            return findings

        with open(head_a, "rb") as fa, open(head_b, "rb") as fb:
            if fa.read() != fb.read():
                finding(findings, "generators_diverge",
                        "两个生成器对同一模板、同一组输入产出了不同内容——单一数据源承诺被破坏")

    return findings


def selftest(repo, template, refresh_script):
    """自证能变红：给临时模板加一个刷新脚本不替换的 token，必须 RED。"""
    with tempfile.TemporaryDirectory(prefix="vfp_vhc_st_") as tmp:
        tpl = os.path.join(tmp, "Version.h.in")
        text = read_text(template)
        with open(tpl, "w", encoding="utf-8") as fh:
            fh.write(text + '\n#define VFP_EXTRA "@VFP_DELIBERATE_DRIFT_TOKEN@"\n')
        findings = check(repo, tpl, refresh_script)
        if "token_not_replaced" in findings:
            out("[VHC] selftest=ok（注入未替换 token 后确实判红）")
            return []
        out("[VHC] selftest=FAIL（注入未替换 token 后仍判绿：这道闸是死的）")
        return ["selftest_dead"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--template", default="", help="默认 <repo>/cmake/Version.h.in")
    ap.add_argument("--refresh", default="", help="默认 <repo>/cmake/RefreshVersionHeader.cmake")
    ap.add_argument("--selftest", action="store_true", help="先自证能变红，再正常校验")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    template = os.path.abspath(args.template) if args.template else os.path.join(repo, "cmake", "Version.h.in")
    refresh = os.path.abspath(args.refresh) if args.refresh else os.path.join(repo, "cmake", "RefreshVersionHeader.cmake")
    out("[VHC] repo=%s" % repo)
    out("[VHC] template=%s" % template)
    out("[VHC] refresh=%s" % refresh)

    if not os.path.isfile(template):
        out("[VHC] finding=template_missing: %s" % template)
        out("[VHC-INV] findings=1")
        out("[VHC-VERDICT] RED")
        return 1
    if not os.path.isfile(refresh):
        out("[VHC] finding=refresh_script_missing: %s" % refresh)
        out("[VHC-INV] findings=1")
        out("[VHC-VERDICT] RED")
        return 1

    findings = []
    if args.selftest:
        findings += selftest(repo, template, refresh)
    findings += check(repo, template, refresh)

    out("[VHC-INV] findings=%d" % len(findings))
    if findings:
        out("[VHC-VERDICT] RED")
        return 1
    out("[VHC-VERDICT] GREEN")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - 异常必须是 RED，不是静默
        out("[VHC] finding=internal_error: %s" % exc)
        out("[VHC-INV] findings=1")
        out("[VHC-VERDICT] RED")
        sys.exit(1)

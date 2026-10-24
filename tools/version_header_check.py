#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Version.h 单一数据源守卫（2026-10-23 复审 P3 建议落地，10-24 盲点修复）。

背景：`build/generated/Version.h` 有两个生成器——
  ① configure 期 bootstrap：CMakeLists.txt 里的 configure_file(cmake/Version.h.in @ONLY)
  ② 构建期刷新：cmake/RefreshVersionHeader.cmake（vfp_version_header 目标，每次构建跑）
正文模板已经合一（② 改为读 ① 的模板做替换），但"哪个值进哪个字段、哪些 @TOKEN@ 被替换"
这类约定若只靠人守，迟早漂移。本脚本把三条承诺变成命令：

  A 覆盖：模板里的每个 @TOKEN@ 都必须出现在刷新脚本的 string(REPLACE ...) 里；
  B 赋值：刷新脚本跑出来的一份，其 版本号/哈希/日期 必须分别等于
          已知输入（常量版本号 / git rev-parse --short HEAD / 当天）——
          **不能**"从 A 的产出解析值再喂给 B"，那会让 A 内部把值替错位时等价恒成立
          （2026-10-24 复审臂3：把 @PROJECT_VERSION@ 与 @VFP_GIT_HASH@ 的替换值写反，
            覆盖检查过、字节比对也过，但生产里 configure 期得到正确头、构建期覆写成错位头，
            而 deploy 的"exe 是否内嵌 HEAD 哈希"只做子串搜索——哈希出现在版本字段里也算命中，
            整条发版链会带着一个错误的关于框版本串一路绿到底）；
  C 等价：两个生成器对**同一组已知输入**的产出必须逐字节相同
          （日期用 A 自己那次跑出来的值喂 B：跨零点不误红，且日期互换无害、
            真被互换也会被 B 的赋值断言抓到）。

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
import datetime
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

# token 名放宽到大小写：configure_file 对 @foo@ 与 @FOO@ 一视同仁，覆盖检查不能漏小写
TOKEN_RX = re.compile(r"@([A-Za-z0-9_]+)@")
REPLACE_RX = re.compile(r'string\(\s*REPLACE\s+"@([A-Za-z0-9_]+)@"')

VERSION_CONST = "0.0.0-check"


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

    # ---- B/C：已知输入跑 A → 断言赋值 → 同值跑 B → 逐字节比对 ----
    expected_hash = git_short(repo)
    expected_date = datetime.date.today().isoformat()
    out("[VHC] known_inputs version=%s hash=%s date=%s" % (VERSION_CONST, expected_hash, expected_date))

    with tempfile.TemporaryDirectory(prefix="vfp_vhc_") as tmp:
        head_a = os.path.join(tmp, "Version.h.a")
        p = run_cmake(["-DHEADER=%s" % head_a,
                       "-DTEMPLATE=%s" % template,
                       "-DREPO=%s" % repo,
                       "-DVERSION=%s" % VERSION_CONST,
                       "-DGIT=git",
                       "-P", refresh_script])
        if p.returncode != 0 or not os.path.isfile(head_a):
            finding(findings, "refresh_generator_failed",
                    p.stderr.decode("utf-8", "replace").strip()[-400:])
            return findings
        text_a = read_text(head_a)

        def grab(name):
            m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, text_a)
            return m.group(1) if m else ""

        v = grab("VFP_VERSION_STRING")
        h = grab("VFP_GIT_HASH")
        d = grab("VFP_BUILD_DATE")
        out("[VHC] generator_a version=%s hash=%s date=%s" % (v, h, d))

        # B 赋值断言：刷新脚本必须把"哪个值"放进"哪个字段"（防值替错位）
        if v != VERSION_CONST:
            finding(findings, "version_misassigned",
                    "版本字段实际是 %r（应为 %r）——刷新脚本把别的值塞进了版本位" % (v, VERSION_CONST))
        if h != expected_hash:
            finding(findings, "hash_misassigned",
                    "哈希字段实际是 %r（应为 git 短哈希 %r）——刷新脚本把别的值塞进了哈希位"
                    % (h, expected_hash))
        if d != expected_date:
            # 跨零点容忍：不判红，只记录（真被互换也会被上面两条断言抓到）
            out("[VHC] note=date_not_today parsed=%s today=%s（跨零点容忍，不判红）" % (d, expected_date))

        # C 等价：B 用同一组已知输入（日期取 A 的，避免跨零点误红）
        script_b = os.path.join(tmp, "b.cmake")
        head_b = os.path.join(tmp, "Version.h.b")
        with open(script_b, "w", encoding="utf-8") as fh:
            fh.write('set(PROJECT_VERSION "%s")\n' % VERSION_CONST)
            fh.write('set(VFP_GIT_HASH "%s")\n' % expected_hash)
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
                        "两个生成器对同一模板、同一组已知输入产出了不同内容——单一数据源承诺被破坏")

    return findings


def selftest(repo, template, refresh_script):
    """两枚破坏臂，都必须判红（自证这道闸不是活的）。"""
    problems = []
    with tempfile.TemporaryDirectory(prefix="vfp_vhc_st_") as tmp:
        # 臂1：模板新增一个刷新脚本不替换的 token → token_not_replaced
        tpl = os.path.join(tmp, "Version.h.in")
        with open(tpl, "w", encoding="utf-8") as fh:
            fh.write(read_text(template) + '\n#define VFP_EXTRA "@VFP_DELIBERATE_DRIFT_TOKEN@"\n')
        f1 = check(repo, tpl, refresh_script)
        if "token_not_replaced" in f1:
            out("[VHC] selftest_arm1=ok（注入未替换 token 后确实判红）")
        else:
            problems.append("arm1_dead")
            out("[VHC] selftest_arm1=FAIL（注入未替换 token 后仍判绿）")

        # 臂2：把刷新脚本里两个 REPLACE 的值写反（版本位塞哈希、哈希位塞版本号）
        #     ——2026-10-24 复审臂3：覆盖检查过、字节比对也过，但生产里是错位头。
        tampered = os.path.join(tmp, "RefreshVersionHeader.cmake")
        text = read_text(refresh_script)
        swapped = text.replace(
            'string(REPLACE "@PROJECT_VERSION@" "${VERSION}"',
            'string(REPLACE "@PROJECT_VERSION@" "${_hash}"').replace(
            'string(REPLACE "@VFP_GIT_HASH@" "${_hash}"',
            'string(REPLACE "@VFP_GIT_HASH@" "${VERSION}"')
        if swapped == text:
            problems.append("arm2_setup_failed")
            out("[VHC] selftest_arm2=FAIL（未能在临时脚本里构造替错位——刷新脚本的 REPLACE 行是否已改形）")
        else:
            with open(tampered, "w", encoding="utf-8") as fh:
                fh.write(swapped)
            f2 = check(repo, template, tampered)
            caught = [c for c in f2
                      if c in ("version_misassigned", "hash_misassigned", "generators_diverge")]
            if caught:
                out("[VHC] selftest_arm2=ok（版本/哈希替错位后判红：%s）" % ",".join(caught))
            else:
                problems.append("arm2_dead")
                out("[VHC] selftest_arm2=FAIL（版本/哈希替错位后仍判绿：这道闸有盲点）")
    return problems


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

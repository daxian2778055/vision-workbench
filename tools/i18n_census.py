# -*- coding: utf-8 -*-
"""中文字面量普查（P2-5「拍板保留 + 普查补齐」的普查部分）。

背景：账本把「英文 UI 改造」记为产品选择（婉拒），但要求**普查落项**——即
"有多少处、在哪些文件"，以便日后真要改造时不必重新盘。本工具只做统计，不改任何代码。

口径：
  · 未国际化 = `QStringLiteral("中文…")`（含 `QString::fromUtf8("中文…")`），且不在 deny 行上；
  · 已国际化 = `tr("中文…")`；
  · 注释里的中文不计（先剥离 // 与 /* */）；
  · deny 行（参数键 / 日志 / QSettings / 对象名 / 输出键）不算"UI 文案"，单独列一类。

用法：
  python tools/i18n_census.py            # 打印汇总 + 前 20 个文件
  python tools/i18n_census.py --md       # 打印可直接贴进文档的 markdown 表
"""
import io
import os
import re
import sys

CJK = r"[\u4e00-\u9fff]"
PAT_TR = re.compile(r'\btr\s*\(\s*"((?:[^"\\]|\\.)*' + CJK + r'(?:[^"\\]|\\.)*)"')
PAT_LIT = re.compile(r'(?:QStringLiteral|QString::fromUtf8)\s*\(\s*"'
                     r'((?:[^"\\]|\\.)*' + CJK + r'(?:[^"\\]|\\.)*)"')

DENY = [
    "m_params[", "paramSpecs", "make", "registerParam", "QSettings",
    "setObjectName", "setProperty", "findChild", "qDebug", "VFP_DEBUG",
    "logMessage", "logOperation", "setParamDirect", ".value(",
]


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def scan(root="src"):
    rows = []
    for name in sorted(os.listdir(root)):
        if not name.endswith(".cpp"):
            continue
        raw = io.open(os.path.join(root, name), encoding="utf-8", errors="replace").read()
        text = strip_comments(raw)
        tr_n = len(PAT_TR.findall(text))
        lit_n = 0
        deny_n = 0
        for line in text.splitlines():
            hits = PAT_LIT.findall(line)
            if not hits:
                continue
            if any(d in line for d in DENY):
                deny_n += len(hits)
            else:
                lit_n += len(hits)
        if tr_n or lit_n or deny_n:
            rows.append((lit_n, deny_n, tr_n, name))
    rows.sort(reverse=True)
    return rows


def main():
    as_md = "--md" in sys.argv
    rows = scan()
    tot_lit = sum(r[0] for r in rows)
    tot_deny = sum(r[1] for r in rows)
    tot_tr = sum(r[2] for r in rows)
    ui_files = len([r for r in rows if r[0] > 0])

    if as_md:
        sys.stdout.write("| 文件 | 未国际化(UI) | 参数/日志类(不计) | 已 tr() |\n")
        sys.stdout.write("|---|---:|---:|---:|\n")
        for lit, deny, tr_n, name in rows[:20]:
            if lit == 0 and tr_n == 0:
                continue
            sys.stdout.write("| `%s` | %d | %d | %d |\n" % (name, lit, deny, tr_n))
        sys.stdout.write("| **合计（%d 个文件）** | **%d** | %d | %d |\n"
                         % (len(rows), tot_lit, tot_deny, tot_tr))
    else:
        sys.stdout.write("%-34s %10s %10s %8s\n" % ("file", "UI-lit", "non-ui", "tr()"))
        for lit, deny, tr_n, name in rows[:20]:
            sys.stdout.write("%-34s %10d %10d %8d\n" % (name, lit, deny, tr_n))
        sys.stdout.write("----\n")
        sys.stdout.write("files=%d ui_literals=%d non_ui=%d already_tr=%d\n"
                         % (ui_files, tot_lit, tot_deny, tot_tr))
    return 0


if __name__ == "__main__":
    sys.exit(main())

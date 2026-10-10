# -*- coding: utf-8 -*-
"""Archive shape gate: a log archive must say which command produced it, once per run.

Why this exists: U-97 落笔时自己造了两处同源缺陷，都是"往一枚档案里追加输出"这一步没有闸。

  1. 落笔后门禁那一枚（build/u44_probe/U97_static_gates_post_row104_1.txt）第一版每节前面
     没有 CMD 行：十道闸的输出连成一片，读档的人说不出某一段是谁打的，只能靠猜。
  2. 宿主那一枚（build/u44_probe/U97_host_ci_1.txt）被同一条命令追加了两遍：
     [HOST-RC] 读 2 而占位那一行 CMD 已经不在首行——一次留档动作写出了两遍宿主。

两处的共同点是：留档件的形状从来没有人判过。交付物的字节有两把尺（repo_hygiene 判编码、
register_shape_gate 判附页行形状），**留档件自己的形状没有**。本闸补这一格，判据全部可从
文件自身读出来，不需要外部真值：

  R1 header        第一个非空行必须是 "CMD: <命令>"——没有它，整份档说不出是谁产出的
  R2 dangling      只判实现真做得到的那一半：一枚 "CMD:" 行是文件里最后一条非空行——那意味着追加动作被打断，
                   或那次运行什么都没产出而档里已写着"我跑过"。"CMD:" 后面紧跟另一枚 "CMD:" 是合法形状
                   （附页第三十八条立的占位惯例：先建一枚只有 CMD 行的占位、跑完再把 stdout 追加进去），不判。
  R3 manifest      --expect-cmd 给出的每条命令必须在档里恰好出现一次（多出来就是被追加了两遍）
  R4 terminal      --terminal 给出的终态行正则至多命中 --terminal-max 次（默认 1）
  R5 bytes         无 CRLF、无孤立 CR、无 BOM、无 NUL、可按 UTF-8 解码；末行必须以换行收尾。两格分开报
                   （crlf= 与 lone_cr=），不合并成一个数——把 \r\n 与 \r 各数一遍再相加会把每行 CRLF 算两遍
  R6 rc accounting "rc=" 行的条数等于 --rc-equals（给了这个开关才判）
  R7 orphan runs    "rc=" 行的条数多于 "CMD:" 行的条数。不依赖调用方声明任何东西：本仓每枚
     留档 runner 都在每回输出末尾自己写一行 rc=，所以"跑过几回"文件自己记着。这一条正是为
     U-97 第一处缺陷设的——那枚档案有十道闸的输出、只有占位那一枚 CMD，没有清单可对时 R3 永远
     不响，而 R7 直接就说得出来。

Judgements (each is one finding, any finding = RED):

R3/R4/R6 是"这次留档跑了什么"的自证：跑几件事，就应当有几枚 CMD、几枚 rc=、几遍终态行。
本闸不判内容对不对——那是对面件的活；它只判"这份档还能不能读"。

Reproduce:
    python tools/archive_shape_check.py --archive <path> [--expect-cmd 'CMD: ...']...
        [--terminal '^\\[HOST-RC\\]' --terminal-max 1] [--rc-equals 10]
    --archive 只能给一枚：第二枚不是覆盖第一枚，而是当场 rc=2 拒收（静默换输入会把"没判的那枚"变成绿）。

Exit: 0 = GREEN | 1 = RED (findings printed) | 2 = unreadable input or misuse (never a green).
Output is ASCII-only (cp936 console); paths and reasons go through esc().
"""
import io
import os
import re
import sys

sys.dont_write_bytecode = True
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

CMD_PREFIX = "CMD: "


def esc(text):
    return text.encode("unicode_escape").decode("ascii")


def read_file(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as exc:
        return None, str(exc)
    return None


def main(argv):
    args = list(argv[1:])
    archive = None
    expect_cmds = []
    terminals = []
    terminal_max = 1
    rc_equals = None
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--archive" and i + 1 < len(args):
            if archive is not None:
                print("ERROR --archive may be named once per run (first=%s second=%s); a second one "
                      "would leave the first judged by nobody" % (esc(archive), esc(args[i + 1])))
                return 2
            archive = args[i + 1]
            i += 2
            continue
        if a == "--expect-cmd" and i + 1 < len(args):
            expect_cmds.append(args[i + 1])
            i += 2
            continue
        if a == "--terminal" and i + 1 < len(args):
            terminals.append(args[i + 1])
            i += 2
            continue
        if a == "--terminal-max" and i + 1 < len(args):
            try:
                terminal_max = int(args[i + 1])
            except ValueError:
                print("ERROR --terminal-max wants a number, got: %s" % esc(args[i + 1]))
                return 2
            i += 2
            continue
        if a == "--rc-equals" and i + 1 < len(args):
            try:
                rc_equals = int(args[i + 1])
            except ValueError:
                print("ERROR --rc-equals wants a number, got: %s" % esc(args[i + 1]))
                return 2
            i += 2
            continue
        if a in ("--help", "-h"):
            print(__doc__)
            return 0
        print("ERROR unknown or incomplete option: %s" % esc(a))
        return 2

    if archive is None:
        print("ERROR --archive <path> is required")
        return 2
    if archive.endswith(".py"):
        print("ERROR this gate judges archives, not scripts: %s" % esc(archive))
        return 2

    data = read_file(archive)
    if isinstance(data, tuple):
        print("ERROR archive not readable: %s (%s)" % (esc(archive), data[1]))
        return 2

    findings = []

    # R5 bytes -- judged first: a file that is not decodable cannot be read at all.
    if data[:3] == b"\xef\xbb\xbf":
        findings.append("R5 bytes bom=yes")
    if b"\x00" in data:
        findings.append("R5 bytes nul=%d" % data.count(b"\x00"))
    crlf = data.count(b"\r\n")
    lone_cr = data.count(b"\r") - crlf
    if crlf or lone_cr:
        findings.append("R5 bytes crlf=%d lone_cr=%d" % (crlf, lone_cr))
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        print("[AS-FIND] R5 bytes not_utf8=%s" % esc(str(exc)[:80]))
        print("[AS-VERDICT] findings=1 verdict=RED")
        return 1
    if not text.endswith("\n"):
        findings.append("R5 bytes no_trailing_newline")

    lines = text.replace("\r\n", "\n").split("\n")
    nonempty = [n for n, l in enumerate(lines) if l.strip()]
    if not nonempty:
        findings.append("R1 header empty_archive")
    else:
        first = lines[nonempty[0]]
        if not first.startswith(CMD_PREFIX):
            findings.append("R1 header first_nonempty=%r" % first[:60])

    # R2 dangling -- a CMD line that owns nothing at all. Deliberately narrow: 附页第三十八条
    # 立的占位惯例就是"先建一枚只有 CMD 行的占位、跑完再把 stdout 追加进去"，所以一枚 CMD 行
    # 后面紧跟另一枚 CMD 行是合法形状（前者是整份档的标签，不是它自己那一次运行）。本规则只判
    # 它能说准的那一半：没有任何非空行跟在后面的 CMD 行——那意味着追加动作被打断，
    # 或那次运行什么都没产出，而档里已经写着"我跑过"。
    last_nonempty = nonempty[-1] if nonempty else None
    for n, l in enumerate(lines):
        if l.startswith(CMD_PREFIX) and n == last_nonempty:
            findings.append("R2 dangling cmd_at_line=%d nothing_follows" % (n + 1))
            break

    # R3 / R6 manifest accounting.
    cmd_lines = [l for l in lines if l.startswith(CMD_PREFIX)]
    rc_lines = [l for l in lines if l.startswith("rc=")]
    # R7 orphan runs -- the rule that needs no manifest, and the one that catches U-97's first
    # defect without being told to. 落笔后门禁那一枚第一版有十道闸的输出却只有占位那一枚 CMD 行：
    # 没有清单可对，R3 永远不会响。而"跑过几回"这件事文件自己记着——每一回都以一枚 rc= 行收尾。
    # 所以 rc= 的条数多过 CMD 的条数，就意味着有运行没报自己是谁。这一条不依赖调用方声明任何东西，
    # 它只依赖本仓留档件"每回输出末尾一行 rc="这个形状（每枚 runner 自己写的那一行）。
    if len(rc_lines) > len(cmd_lines):
        findings.append("R7 orphan_runs rc_lines=%d cmd_lines=%d (a run did not print its own CMD)"
                        % (len(rc_lines), len(cmd_lines)))
    for want in expect_cmds:
        hits = sum(1 for l in cmd_lines if l == want)
        if hits != 1:
            findings.append("R3 manifest hits=%d want=1 cmd=%r" % (hits, want[:70]))
    if rc_equals is not None:
        rc_n = len(rc_lines)
        if rc_n != rc_equals:
            findings.append("R6 rc_accounting rc_lines=%d want=%d" % (rc_n, rc_equals))

    # R4 terminal markers.
    for pat in terminals:
        try:
            rx = re.compile(pat)
        except re.error as exc:
            print("ERROR --terminal is not a regex: %s (%s)" % (esc(pat), esc(str(exc))))
            return 2
        n = sum(1 for l in lines if rx.search(l))
        if n > terminal_max:
            findings.append("R4 terminal hits=%d want<=%d pattern=%r" % (n, terminal_max, pat))

    print("[AS] archive=%s bytes=%d lines=%d cmd_lines=%d rc_lines=%d"
          % (esc(archive), len(data), len([l for l in lines if l.strip()]),
             len(cmd_lines), sum(1 for l in lines if l.startswith("rc="))))
    for f in findings:
        print("[AS-FIND] %s" % esc(f))
    verdict = "GREEN" if not findings else "RED"
    print("[AS-VERDICT] findings=%d verdict=%s" % (len(findings), verdict))
    return 0 if not findings else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except Exception as exc:
        print("ERROR script failed: %s %s" % (type(exc).__name__, str(exc)[:200]))
        sys.exit(3)

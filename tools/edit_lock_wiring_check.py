#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""画布编辑锁的接线守卫（2026-10-24 现场反馈）。

现场现象：工具拖不进画布；双击能进，但进去后节点不能动。
根因两条（都在 MainWindow 的"接线"上，不是算法）：

  ① 四个执行状态槽里，`onExecutionStarted / onExecutionStopped / onExecutionFinished` 都会
     调 `updateEditLockForCurrentScene()` 重算画布锁，只有 `onExecutionError` 漏了。
     错误路径若没走到 executionFinished，画布就永久停在锁定态。
  ② 工具箱双击走 `MainWindow::addNode`，它直接 `createNode`；而 `FlowScene::createNode`
     本身不查 `m_editLocked`（拖拽路径是在 dragEnter/dragMove/drop 里查的）。于是锁定期间
     "拖不进、双击能进"，进去后节点又不可动。

本闸把这两条变成源码形状的硬约束，删掉任一处即红：
  INV1 每个执行状态槽的函数体里必须有 `updateEditLockForCurrentScene`
  INV2 `MainWindow::addNode` 的函数体里必须有 `isEditLocked` 判据

为什么要形状闸而不是只靠运行期用例：运行期用例（EditLockReleaseTest）钉的是"失败停止后
执行器允许编辑"，钉不住"MainWindow 有没有在这个槽里重算"——那正是本次缺陷的位置。

用法：python tools/edit_lock_wiring_check.py [--repo .] [--selftest]
输出契约（ci.ps1 按前缀筛行）：
  [ELW] slot=<name> ok|MISSING
  [ELW] finding=<code>: <detail>
  [ELW-INV] findings=N
  [ELW-VERDICT] GREEN|RED
退出码：0 = GREEN，1 = RED。
"""

import argparse
import os
import re
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

MAINWINDOW_REL = os.path.join("src", "MainWindow.cpp")
LOCK_CALL = "updateEditLockForCurrentScene"

# 四个执行状态槽 + addNode 的入画布路径
REQUIRED_SLOTS = [
    ("onExecutionStarted", LOCK_CALL),
    ("onExecutionStopped", LOCK_CALL),
    ("onExecutionFinished", LOCK_CALL),
    ("onExecutionError", LOCK_CALL),
    ("addNode", "isEditLocked"),
]


def out(line):
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def body_of(lines, start_idx):
    """取函数体：从定义行起，到第一条**列 0 的 }** 为止（顶层函数定义都以它收尾）。"""
    out_lines = []
    started = False
    for i in range(start_idx, len(lines)):
        line = lines[i]
        out_lines.append(line)
        if not started:
            if "{" in line:
                started = True
            continue
        if line.startswith("}"):
            break
    return "\n".join(out_lines)


def find_definition(lines, name):
    """返回定义所在行号；找不到返回 -1。只认列 0 的 'MainWindow::name(' 定义。"""
    pat = re.compile(r"^\s*(?:void|bool|int|QString|QList|QVector|NodeBase|FlowScene)\s+MainWindow::"
                     + re.escape(name) + r"\s*\(")
    for i, line in enumerate(lines):
        if pat.match(line):
            return i
    return -1


def check(text, required=None):
    """返回 findings 列表。required 便于自测替换期望。"""
    findings = []
    lines = text.splitlines()
    for name, needle in (required if required is not None else REQUIRED_SLOTS):
        idx = find_definition(lines, name)
        if idx < 0:
            findings.append("slot_not_found:" + name)
            out("[ELW] slot=%s MISSING(definition not found)" % name)
            continue
        body = body_of(lines, idx)
        ok = needle in body
        out("[ELW] slot=%s %s" % (name, "ok" if ok else "MISSING(%s)" % needle))
        if not ok:
            findings.append("missing_%s_in_%s" % (needle, name))
    return findings


def selftest(repo):
    """两枚破坏臂：删掉错误槽的重算、删掉 addNode 的锁判据，都必须判红。"""
    src = os.path.join(repo, MAINWINDOW_REL)
    text = open(src, "rb").read().decode("utf-8", "replace")
    problems = []

    # 臂1：把 onExecutionError 里的重算行删掉
    arm1 = re.sub(r"(void MainWindow::onExecutionError\([^)]*\)\s*\{.*?)\n[^\n]*"
                  + LOCK_CALL + r"\(\);[^\n]*\n",
                  r"\1\n", text, count=1, flags=re.S)
    if arm1 == text:
        problems.append("arm1_setup_failed")
        out("[ELW] selftest_arm1=FAIL(未能构造：onExecutionError 里找不到重算行)")
    else:
        f1 = check(arm1)
        if any(x.startswith("missing_%s_in_onExecutionError" % LOCK_CALL) for x in f1):
            out("[ELW] selftest_arm1=ok(删掉 onExecutionError 的重算后判红)")
        else:
            problems.append("arm1_dead")
            out("[ELW] selftest_arm1=FAIL(删掉重算后仍判绿：这道闸是死的)")

    # 臂2：把 addNode 里的 isEditLocked 判据改成永假
    arm2 = re.sub(r"if\s*\(\s*scene->isEditLocked\(\)\s*\)",
                  "if (false)", text, count=1)
    if arm2 == text:
        problems.append("arm2_setup_failed")
        out("[ELW] selftest_arm2=FAIL(未能构造：addNode 里找不到 isEditLocked 判据)")
    else:
        f2 = check(arm2)
        if any("missing_isEditLocked_in_addNode" == x for x in f2):
            out("[ELW] selftest_arm2=ok(删掉 addNode 的锁判据后判红)")
        else:
            problems.append("arm2_dead")
            out("[ELW] selftest_arm2=FAIL(删掉锁判据后仍判绿：这道闸有盲点)")

    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=".")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    repo = os.path.abspath(args.repo)
    src = os.path.join(repo, MAINWINDOW_REL)
    out("[ELW] source=%s" % src)
    if not os.path.isfile(src):
        out("[ELW] finding=source_missing: %s" % src)
        out("[ELW-INV] findings=1")
        out("[ELW-VERDICT] RED")
        return 1

    text = open(src, "rb").read().decode("utf-8", "replace")
    findings = []
    if args.selftest:
        findings += selftest(repo)
    findings += check(text)

    out("[ELW-INV] findings=%d" % len(findings))
    if findings:
        out("[ELW-VERDICT] RED")
        return 1
    out("[ELW-VERDICT] GREEN")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:  # noqa: BLE001 - 异常必须是 RED，不是静默
        out("[ELW] finding=internal_error: %s" % exc)
        out("[ELW-INV] findings=1")
        out("[ELW-VERDICT] RED")
        sys.exit(1)

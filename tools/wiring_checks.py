#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""接线守卫（2026-10-24 画布锁 / 2026-10-25 审计操作者）。

有些缺陷不在算法里，而在"接线"上——某个槽忘了调一句、某处写死了一个值。运行期用例钉不住
它们（用例驱动执行器/对话框的方式与生产路径不同），所以用源码形状的硬约束来守：

  INV1 画布锁（2026-10-24 现场：工具拖不进、双击入画布后不能动）
       每个执行状态槽的函数体里必须有 updateEditLockForCurrentScene；
       MainWindow::addNode（工具箱双击路径）必须有 isEditLocked 判据。
  INV2 审计操作者（2026-10-25 复核 P3-5：用户管理日志的操作者写死 "Admin"）
       src/ 下不得出现 logOperation(...,"Admin",...) 这类硬编码操作者——
       操作者必须取 SessionManager::currentUser()，否则审计日志里所有动作都记成同一个人。
  INV3 相机线程池任务的生命周期（2026-10-25 复核 P2-3：关相机与在飞任务抢句柄）
       CameraOperationTask 必须进出 CameraTaskLedger，且 closeCamera 拆句柄前必须
       waitDrained（先关闸再排空；超时跳过关闭），否则就是 use-after-free。

用法：python tools/wiring_checks.py [--repo .] [--selftest]
输出契约（ci.ps1 按前缀筛行）：
  [ELW] key=value
  [ELW] finding=<code>: <detail>
  [ELW-INV] findings=N
  [ELW-VERDICT] GREEN|RED
退出码：0 = GREEN，1 = RED。
"""

import argparse
import glob
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

# INV2：硬编码审计操作者（logOperation("Admin", ...) / logOperation(QStringLiteral("Admin"), ...)）
HARDCODED_OPERATOR_RX = re.compile(
    r'logOperation\(\s*(?:QStringLiteral\(\s*)?"Admin"')

# INV3：相机线程池任务的生命周期（P2-3）——任务自己进出在飞账，关相机前必须等排空
MVS_REL = os.path.join("src", "MvsImageSourceNode.cpp")
FLOW_EXECUTOR_REL = os.path.join("src", "FlowExecutor.cpp")
CAMERA_LEDGER_TOKENS = ["CameraTaskLedger::enter()", "CameraTaskLedger::leave()"]


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

    # 臂3：往 src/ 里塞一个硬编码审计操作者（模拟 P3-5 的回潮）
    #     在临时副本目录里做：真仓库一个字节都不碰。
    with tempfile.TemporaryDirectory(prefix="vfp_elw_st3_") as tmp:
        victim = os.path.join(tmp, "UserManagementDialog.cpp")
        with open(victim, "w", encoding="utf-8") as fh:
            fh.write('db->logOperation(QStringLiteral("Admin"), QStringLiteral("删除用户"), x);\n')
        hits = scan_hardcoded_operator([victim])
        if hits:
            out("[ELW] selftest_arm3=ok(塞入硬编码 Admin 操作者后判红)")
        else:
            problems.append("arm3_dead")
            out("[ELW] selftest_arm3=FAIL(塞入硬编码 Admin 操作者后仍判绿：这道闸有盲点)")

    # 臂4/5：相机生命周期（P2-3）——任务不再记在飞账 / closeCamera 不再等排空
    mvs_src = os.path.join(repo, MVS_REL)
    if not os.path.isfile(mvs_src):
        problems.append("arm4_setup_failed")
        out("[ELW] selftest_arm4=FAIL(找不到 %s)" % MVS_REL)
    else:
        base = open(mvs_src, "rb").read().decode("utf-8", "replace")
        with tempfile.TemporaryDirectory(prefix="vfp_elw_st45_") as tmp:
            # 臂4：摘掉 enter（任务不再进账）
            arm4_path = os.path.join(tmp, "arm4.cpp")
            with open(arm4_path, "w", encoding="utf-8") as fh:
                fh.write(base.replace("CameraTaskLedger::enter();", "", 1))
            if check_camera_lifecycle(arm4_path):
                out("[ELW] selftest_arm4=ok(任务不记在飞账后判红)")
            else:
                problems.append("arm4_dead")
                out("[ELW] selftest_arm4=FAIL(摘掉 enter 后仍判绿)")

            # 臂5：关相机不再等排空
            arm5_path = os.path.join(tmp, "arm5.cpp")
            arm5 = base.replace("if (!CameraTaskLedger::waitDrained(CameraTaskLedger::drainTimeoutMs()))",
                                "if (false)", 1)
            with open(arm5_path, "w", encoding="utf-8") as fh:
                fh.write(arm5)
            if arm5 == base:
                problems.append("arm5_setup_failed")
                out("[ELW] selftest_arm5=FAIL(未能构造：closeCamera 里找不到 waitDrained 判据)")
            elif check_camera_lifecycle(arm5_path):
                out("[ELW] selftest_arm5=ok(closeCamera 不等排空后判红)")
            else:
                problems.append("arm5_dead")
                out("[ELW] selftest_arm5=FAIL(关相机不等排空后仍判绿)")

    return problems


def scan_hardcoded_operator(paths):
    """返回 [(文件, 行号, 行内容)]：硬编码 logOperation("Admin", ...) 的出现处。"""
    hits = []
    for path in paths:
        try:
            with open(path, "rb") as fh:
                lines = fh.read().decode("utf-8", "replace").splitlines()
        except OSError:
            continue
        for i, line in enumerate(lines, 1):
            if HARDCODED_OPERATOR_RX.search(line):
                hits.append((path, i, line.strip()))
    return hits


def check_audit_operator(repo):
    """INV2：src/ 下不得有硬编码审计操作者。"""
    findings = []
    files = sorted(glob.glob(os.path.join(repo, "src", "**", "*.cpp"), recursive=True))
    out("[ELW] audited_files=%d" % len(files))
    hits = scan_hardcoded_operator(files)
    for path, lineno, line in hits:
        rel = os.path.relpath(path, repo)
        findings.append("hardcoded_operator:%s:%d" % (rel, lineno))
        out("[ELW] finding=hardcoded_operator: %s:%d %s" % (rel, lineno, line[:100]))
    return findings


def check_camera_lifecycle(mvs_path):
    """INV3：任务进出在飞账 + closeCamera 拆句柄前等待排空。返回 findings。"""
    findings = []
    if not os.path.isfile(mvs_path):
        out("[ELW] finding=mvs_source_missing: %s" % mvs_path)
        return ["mvs_source_missing"]

    text = open(mvs_path, "rb").read().decode("utf-8", "replace")
    for tok in CAMERA_LEDGER_TOKENS:
        ok = tok in text
        out("[ELW] camera_ledger=%s %s" % (tok, "ok" if ok else "MISSING"))
        if not ok:
            findings.append("camera_ledger_missing:%s" % tok)

    lines = text.splitlines()
    idx = -1
    for i, line in enumerate(lines):
        if line.startswith("void MvsImageSourceNode::closeCamera("):
            idx = i
            break
    if idx < 0:
        out("[ELW] finding=closeCamera_not_found")
        findings.append("closeCamera_not_found")
    else:
        body = body_of(lines, idx)
        ok = "waitDrained" in body
        out("[ELW] closeCamera_waits=%s %s" % ("waitDrained", "ok" if ok else "MISSING"))
        if not ok:
            findings.append("missing_waitDrained_in_closeCamera")
    return findings


def check_round_start_hook(repo):
    """INV4：轮首必须调用 onRoundStart（2026-10-28）。

    流水线计数器这类"按批次累计"的节点靠这一行复位；删掉不会报错、界面也没有异常，
    只是累计值不再清零——最典型的"接线丢掉"型缺陷。
    """
    findings = []
    path = os.path.join(repo, FLOW_EXECUTOR_REL)
    if not os.path.isfile(path):
        findings.append("source_missing: %s" % path)
        return findings
    text = open(path, "rb").read().decode("utf-8", "replace")
    # 只看真实代码：注释里的同一串不算命中（2026-10-28 实测：注释掉调用仍被判绿）
    import re as _re
    code = _re.sub(r"/\*.*?\*/", "", text, flags=_re.S)
    code = _re.sub(r"//[^\n]*", "", code)
    if "onRoundStart();" not in code:
        findings.append("FlowExecutor no longer calls onRoundStart() -> per-batch nodes silently stop resetting")
    return findings


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
    findings += check_audit_operator(repo)
    findings += check_camera_lifecycle(os.path.join(repo, MVS_REL))
    findings += check_round_start_hook(repo)

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

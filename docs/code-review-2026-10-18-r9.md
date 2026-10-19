# Vision Workbench re-review — 2026-10-18 r9

Follow-up to the r1–r8 notes. Tree: `origin/main` at `527a68e` (“U-158 … canRunFlow 翻「登录且写闸未开」… 取消改密即退登；脚本确认默认翻开”). Diff since r8’s `e07e633` is 12 files, +183/−19.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `e07e633`

No new modules. `SessionManager::canRunFlow()` is now “logged in and the write gate is off.” Cancel on the factory-password dialog calls `logout()`, resets the window title, and reapplies permissions. Seven start sites consult that predicate or, on the executor, `writesBlocked()` alone: start, single-shot, idle step, auto-start after load, the runtime-view start/single/trigger actions, the executor’s idle `triggerFired` handler, and idle `requestExternalRound`. A flow that is already running still accepts queued external rounds and is not stopped. `ScriptSecurityPolicy::m_requireConfirmation` defaults to true. `requestConfirmation` remembers one acceptance per language plus SHA-256 of the script text for the process lifetime, and skips the dialog when no `QApplication` exists. Ledger rows 173 and 174 record both decisions. Three roadmap cells were reworded so the September A1 note no longer says `canRunFlow()` is deliberately ungated.

## 2. Status of prior findings

| ID | r8 status | Now | Evidence on `527a68e` |
| --- | --- | --- | --- |
| P0-1 | Partial | **Partial** | Cancel now ends the session: `MainWindow::enforceFactoryPasswordPolicy` calls `SessionManager::logout()` when the change-password dialog is rejected (`src/MainWindow.cpp`). `canRunFlow()` is `isLoggedIn() && !writesBlocked()` (`src/SessionManager.cpp`). The seven start sites above check it or `writesBlocked()`. `tests/factory_password_guard_test.cpp` `flowStartGate_refusesWhileWritesBlocked` requires run allowed, then refused, then allowed again. `m_requireConfirmation` initializes to true (`include/ScriptSecurityPolicy.h`). `tests/script_security_test.cpp` `requireConfirmation_defaultsOn` removes any saved key, calls `load()`, and requires the flag true. `approvalProof_sessionOncePerContent` requires a new text and a new language to miss the cache. Canvas edit lock and `openModuleEditor` are unchanged from r8. Still open inside this item: `MainWindow::executeNodeOnce` calls `node->execute()` with no `canRunFlow()` check (`src/MainWindow.cpp`). The parameter dock builds an “执行算子” button that calls it. The canvas menus “执行到此处”, “从此处执行”, and “重算此算子及下游” are disabled while `isEditLocked()` is set, and the write gate sets that lock. The seed row `admin` / `createPasswordHash("admin")` is still inserted (`src/AppDatabase.cpp`). A flow already running when the gate turns on keeps running, including queued external rounds (`FlowExecutor::requestExternalRound` returns before the new check when state is Running or Paused). `QSettings` still overrides the confirmation default when the key is already saved (ledger row 174). |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` stays unsigned. `ImageReadNode` still takes `filePath` with no path gate. Empty-passphrase export still has no MAC. |
| P1-2 | Fixed | **Fixed** | Unchanged. Import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Still open | **Still open** | Not in this diff. `TcpCommNode` still `listen(QHostAddress::Any, … port default 502)`. `UdpCommNode` still `bind(QHostAddress::AnyIPv4, …)`. `ModbusNode` server mode still sets `NetworkPortParameter` only. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` still names Qt 6, HALCON 24.11, OpenCV, and `tools/ci.ps1`. The CI-guide banner still labels that guide as a blueprint. |
| P2-1 | Partial | **Partial** | Not in this diff. `kHashIterations` is 60000. The family is still PBKDF2-HMAC-SHA256 (ledger row 167). |
| P2-2 | Fixed | **Fixed** | Unchanged. Public AES and PBKDF2 still call Windows CNG, with the handwritten twins as fallback. |
| P2-3 | Partial | **Partial** | Not in this diff. `onDataReceived` still matches equality or `startsWith` (`src/GlobalTriggerManager.cpp`). Ledger row 170 keeps that single-source prefix match. |
| P2-4 | Still open | **Still open** | `src/FlowExecutor.cpp` destructor is still the only `qFatal(` call under `src/`, `include/`, and `ui/`. Ledger row 160 records keeping that abort. |
| P2-5 | Still open | **Still open** | English UI stays off (ledger row 171). `MainWindow.cpp` now has 179 `tr(` call sites. The i18n guide banner is unchanged. |
| P2-6 | Still open | **Still open** | `MainWindow.cpp` is 4392 lines. `FlowExecutor.cpp` is 1967 lines. Dual Halcon/OpenCV node stacks are unchanged. |
| P2-7 | Fixed | **Fixed** | Unchanged. `docs/security-development-guide.md` still opens with the template banner. |
| P2-8 | Partial | **Partial** | Unchanged. `project-audit-report/` stays deleted. `requirements_decoded.txt`, `需求.txt`, and `diag-churn.txt` stay tracked. |
| N1 | Fixed | **Fixed** | Unchanged. Scheme import still rejects a bad iteration count before key derivation. |
| N2 | Fixed | **Fixed** | Unchanged. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` still starts `diff` at 1 when the sizes differ (`src/SchemeCrypto.cpp`). |

## 3. New findings

None. The parameter-dock run button is the remainder of P0-1, not a separate defect: the seven sites named in ledger row 173 do not include `executeNodeOnce`. Headless confirmation skip and the process-lifetime approval cache are the behavior row 174 records. Context-menu run items follow the edit lock, which the write gate already turns on.

## 4. What improved

- Cancelling the factory-password dialog logs the Admin session out. The dialog text now says new runs are refused.
- `canRunFlow()` refuses a new run while the write gate is up, and allows it again after the gate lifts, without a second login. Start, single-shot, idle step, load-time continuous start, and the runtime-view start actions use that check. Idle external triggers use `writesBlocked()` on the executor.
- A fresh install asks before running a script. The same language and the same script text are asked once per process. A changed script, or the same text in the other language, is asked again.

## 5. Leftovers, in order

1. **P0-1 remainder.** The parameter dock’s “执行算子” button still runs the selected node, including a script node, with no `canRunFlow()` check. After cancel the session is gone and the seven start buttons stay refused, and that dock button does not. A flow that was already running keeps running. The `admin`/`admin` seed row is still how the gate is armed.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, listeners on every interface.
3. **P2-3.** One configured source still prefix-matches on the wire. Ledger row 170 keeps that for variable device tails.
4. **P2-1 remainder.** The password KDF is still PBKDF2 at 60000 rounds.
5. **P2-4, P2-5, P2-6, and the tombstones in P2-8.** `qFatal` on join timeout, English UI left off, file size, and `requirements_decoded.txt`, `需求.txt`, `diag-churn.txt`.

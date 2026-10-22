# Vision Workbench re-review — 2026-10-18 r10

Follow-up to the r1–r9 notes. Tree: `origin/main` at `6c974b7` (“U-200 … 令牌收尾超时转入可观测终态”). Range since r9’s `527a68e` is 42 commits, 114 files, +6609/−1268.

This pass did not rebuild or run CTest. The scan below is the leftover list plus behavior that these commits actually changed. Extracted helpers that only move an existing function are not findings.

## 1. Architecture delta since `527a68e`

No new vision-operator family. The executor’s lifetime is now a `FlowExecutorToken`: the window holds the token, the token owns the `FlowExecutor`, and a timeout on the way out does not terminate the thread. It marks the executor abandoned (`getState()` stays `Error`, `allowsGraphEditing()` stays false), emits `executionAbandoned`, and `deleteLater`s when the thread actually finishes. `ExecutionStatusController::onAbandoned` sets the status text to “已放弃（线程未退出）” and disables the four run controls. Nodes read run state through a new `FlowRunContext` instead of the executor object. Trigger bindings keep a weak token and call the executor only after taking a strong reference outside the trigger lock.

`MainWindow` shed several pure helpers (`ExecutionControlPolicy`, `AuxDialogMenuRouter`, `ModuleEditorRegistry`, `UITranslator`, `ManualPathResolver`, `RoundVariableFeed`, `NodeIconPalette`, `UpstreamWalk`, `YieldMonitor::collectRoundFlags`). `MainWindow.cpp` is 4308 lines. `FlowExecutor.cpp` is 2033 lines.

Scheme files: plaintext packages are now format v2 and carry a salt plus HMAC over the header, flags, salt, and payload. The MAC key is `HMAC(pepper, salt)` with the pepper compiled into `src/SchemePackage.cpp`. v1 plaintext still loads and sets `integrityMissing`. Listen sockets take a `bindAddress` parameter (`CommBind::resolveListenAddress`); an empty value still means every interface, and an invalid literal refuses to start. Image and file paths resolve against the scheme directory (`ProjectPaths`). A path outside that directory is reported on load and is not blocked.

Register float reads in the shared byte-order helper now take four big-endian bytes and reinterpret them. The destructor `qFatal` is still in `FlowExecutor`, after a 5s wait and a 15s wait. The token’s timeout path does not call it.

## 2. Status of prior findings

| ID | r9 status | Now | Evidence on `6c974b7` |
| --- | --- | --- | --- |
| P0-1 | Partial | **Fixed** | The r9 dock hole is closed. `MainWindow::executeNodeOnce` returns when `canRunFlow()` is false, and the parameter-dock “执行算子” button and the module editor’s execute signal both call that function (`src/MainWindow.cpp`). `recomputeDownstream` uses the same check before `executeFrom`. `canRunFlow()` is still `isLoggedIn() && !writesBlocked()` (`src/SessionManager.cpp`). Cancel still calls `logout()` (`MainWindow::enforceFactoryPasswordPolicy`). `m_requireConfirmation` still defaults to true. The context-menu item “仅执行此算子” calls `node->execute()` directly (`src/NodeGraphicsItem.cpp`) and does not call `canRunFlow()`; it is disabled while `isEditLocked()` is set, and `canvasEditShouldBeLocked` is true whenever `writesBlocked()` is set. Checked and kept, not left open: an executor that is already `Running` or `Paused` still queues external rounds before the `writesBlocked()` test (`FlowExecutor::requestExternalRound`), and the continuous loop does not re-read the gate. The dialog text still says a flow that is already running is left running. The seed row is still inserted when `users` is empty: `admin` / `createPasswordHash("admin")` / `Admin` (`src/AppDatabase.cpp`). Login with that row still hits the gate before the window is shown. |
| P1-1 | Still open | **Partial** | Plaintext export is format v2. `SchemePackage` MACs the header, flags, salt, and payload with `kPlainPepper` (`src/SchemePackage.cpp`). A bad MAC fails the import. v1 plaintext still loads and sets `integrityMissing`. There is no author signature; the pepper is in the binary. `ProjectPaths::resolveSchemePath` anchors a relative path at the scheme directory. `reviewScenes` reports an absolute path outside that directory through `reportLoadNote` and does not refuse the load (`include/ProjectPaths.h`). Hard path jail was declined in the ledger. |
| P1-2 | Fixed | **Fixed** | Unchanged. Encrypted import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Still open | **Partial** | `TcpCommNode`, `UdpCommNode`, and `ModbusNode` read `bindAddress` and pass it through `CommBind::resolveListenAddress` (`include/CommunicationNodeBase.h`). `Invalid` refuses to start. `Default` (empty) still listens on `QHostAddress::Any` for TCP and `AnyIPv4` for UDP. Explicit `0.0.0.0` / `::` is accepted and logged. The default-all-interfaces choice is recorded as a product decision. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` still names Qt 6, HALCON, OpenCV, and `tools/ci.ps1`. The CI-guide banner still labels that guide as a blueprint. |
| P2-1 | Partial | **Partial** | Not in this diff. `kHashIterations` is 60000. The family is still PBKDF2-HMAC-SHA256. |
| P2-2 | Fixed | **Fixed** | Unchanged. Public AES and PBKDF2 still call Windows CNG, with the handwritten twins as fallback. |
| P2-3 | Partial | **Partial** | Not in this diff. `onDataReceived` still matches equality or `startsWith` (`src/GlobalTriggerManager.cpp`). The config and load gates are unchanged. |
| P2-4 | Still open | **Partial** | `FlowExecutorToken`’s 15s timeout calls `markExecutionAbandoned` and does not `delete` the running object (`src/FlowExecutorToken.cpp`). `MainWindow` connects `executionAbandoned` to `ExecutionStatusController::onAbandoned`, which disables start, stop, single-shot, and pause. `allowsGraphEditing()` returns false while `m_abandoned` is set, so the canvas stays locked. The destructor still calls `qFatal` after `joinForDestroy(5s)` and `joinForDestroy(15s)` both fail (`src/FlowExecutor.cpp`). That call is the only `qFatal(` under `src/`, `include/`, and `ui/`. The production `delete` of an executor is the token’s success branch; the timeout branch waits for `QThread::finished` and then `deleteLater`. |
| P2-5 | Still open | **Still open** | English UI is still off. `MainWindow.cpp` has 182 `tr(` call sites. `docs/客户复核意见挂账项复审对账.md` records a wider census (260 `tr(` sites, 428 UI literals without `tr`). This pass did not re-run that scanner. |
| P2-6 | Still open | **Partial** | The helpers named in section 1 now live outside `MainWindow`. `MainWindow.cpp` went from 4392 lines to 4308. `FlowExecutor.cpp` went from 1967 to 2033. Halcon and OpenCV node types are both still registered. The executor split is the token and the run-context block, not a replacement of the executor. |
| P2-7 | Fixed | **Fixed** | Unchanged. `docs/security-development-guide.md` still opens with the template banner. |
| P2-8 | Partial | **Partial** | `requirements_decoded.txt`, `需求.txt`, and `diag-churn.txt` are still tracked at the repo root. `project-audit-report/` stays deleted. |
| N1 | Fixed | **Fixed** | Unchanged. Scheme import still rejects a bad iteration count before key derivation. |
| N2 | Fixed | **Fixed** | Unchanged. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` still starts `diff` at 1 when the sizes differ (`src/SchemeCrypto.cpp`). |

## 3. New findings

None. The context-menu “仅执行此算子” path is the edit lock, which the write gate already turns on, so it is not a second hole beside `executeNodeOnce`. Plaintext HMAC uses a pepper in the source tree; that is the integrity bit inside P1-1, not a separate defect. The float register read now returns the four-byte bit pattern; that closes a stuck-at-zero read and does not add a second parser.

## 4. What improved

- The parameter dock’s “执行算子” button and downstream recompute both refuse to start while the write gate is up. Cancel still logs the Admin session out. A new run from the seven earlier sites is still refused. A flow that is already running is still left running, which is the recorded line-stop boundary.
- A plaintext scheme written by this tree has an HMAC over the header and the readonly flag. A relative image or file path is resolved from the scheme directory. A path outside that directory is named on load.
- Listen address is a real parameter. A value that is not an IP literal does not fall back to every interface.
- Dropping the last executor token after a stuck worker no longer ends the process. The status bar says the kill was abandoned, the run buttons disable, and the graph stays locked until the thread exits and the object deletes itself.
- `MainWindow`’s menu routing, module-editor book, icon drawing, upstream walk, and several text tables are separate types with their own tests.

## 5. Leftovers, in order

1. **P1-1 remainder.** No author signature. v1 plaintext still loads with an integrity-missing note. An absolute path outside the scheme directory is warned and then used. The plaintext MAC key is the compiled-in pepper.
2. **P1-3 remainder.** Empty `bindAddress` still listens on every interface. An explicit address can narrow that.
3. **P2-3.** One configured source still prefix-matches on the wire.
4. **P2-1 remainder.** Password KDF is still PBKDF2 at 60000 rounds.
5. **P2-4 remainder.** Direct destruction of a running executor still `qFatal`s after the two waits. The token path does not take that branch.
6. **P2-6 remainder.** `FlowExecutor.cpp` is 2033 lines. Halcon and OpenCV operator stacks both remain.
7. **P2-5 and P2-8.** English UI stays off. The three root tombstones stay.

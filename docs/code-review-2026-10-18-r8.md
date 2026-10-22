# Vision Workbench re-review — 2026-10-18 r8

Follow-up to the r1–r7 notes. Tree: `origin/main` at `e07e633` (“U-157 … SchemeCrypto 加密与派生两面换 Windows CNG”). Range since r7’s `02cbb30`: `05bd210`, `08c061c` (both “U-156 … 拍板轮”), then `e07e633`. Combined diff: 22 files, +412/−3120, most of the deletions being `project-audit-report/`.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `02cbb30`

No new node types. `SessionManager::canvasEditShouldBeLocked` ORs the per-scene run lock with the factory-password write lock. `MainWindow` uses that for the existing run-lock call sites, relocks every scene from `applyPermissionRestrictions`, and `openModuleEditor` returns when `writeGateRefusal()` is non-empty. Account PBKDF2’s default round count is 60000; the token still stores its own count. `SchemeCrypto`’s AES block primitive and PBKDF2 now call Windows CNG (`BCrypt`) on the public path. CTR is built here from those ECB blocks. The handwritten AES and PBKDF2 stay in the binary as `*_Handwritten`, used by the comparison tests and as the fallback when CNG fails (`warnCngFallbackOnce`). HMAC stays on `QCryptographicHash`. `CMakeLists.txt` links `bcrypt` on Windows. `project-audit-report/` is gone. `tools/eol_shape_gate.py` freezes 140 LF files. Ledger rows 166–172 record the write-gate wiring, the KDF decision, the CNG decision and the migration, the audit-report deletion, the prefix-match decision, and the i18n decision.

## 2. Status of prior findings

| ID | r7 status | Now | Evidence on `e07e633` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Partial** | Canvas and module editing are on the write gate. `SessionManager::canvasEditShouldBeLocked` returns true when the scene run lock is on or `m_writesBlocked` is set (`include/SessionManager.h`). `MainWindow::openModuleEditor` shows `writeGateRefusal()` and closes an editor already open for that node (`src/MainWindow.cpp`). `applyPermissionRestrictions` relocks every scene. `tests/factory_password_guard_test.cpp` `writeGateRefusal_isTheSinglePredicate` requires the refusal to name 画布, locks when the gate is up and the flow is idle, and keeps the run lock after the gate lifts. Still open inside the original item: `AppDatabase` still seeds `admin` / `createPasswordHash("admin")` (`src/AppDatabase.cpp`). Cancel still breaks the change-password loop and leaves the Admin session (`MainWindow::enforceFactoryPasswordPolicy`). `canRunFlow()` is still `isLoggedIn()` (`src/SessionManager.cpp`). Ledger row 166 says that run path stays. `switchToDesignMode` still calls `setInteractive(true)`; structural edits go through `setEditLocked`, which the new predicate feeds. `ScriptSecurityPolicy` still defaults `m_requireConfirmation = false` (`include/ScriptSecurityPolicy.h`). |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` stays unsigned. `ImageReadNode` still takes `filePath` with no path gate. Empty-passphrase export still has no MAC. |
| P1-2 | Fixed | **Fixed** | Unchanged. Import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Still open | **Still open** | Not in this diff. `TcpCommNode` still `listen(QHostAddress::Any, … port default 502)`. `UdpCommNode` still `bind(QHostAddress::AnyIPv4, …)`. `ModbusNode` server mode still sets `NetworkPortParameter` only. Prefix match on the trigger path is the P2-3 item. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` still names Qt 6, HALCON 24.11, OpenCV, and `tools/ci.ps1`. The CI-guide banner still labels that guide as a blueprint. |
| P2-1 | Partial | **Partial** | Salt still comes from `QRandomGenerator::system()`. `kHashIterations` is now 60000 (`src/AppDatabase.cpp`). `kHashIterationsLegacy` stays 12000 for old rows. The read cap stays 1000000. Ledger row 167 records staying on PBKDF2-HMAC-SHA256 and raising the default count. The family is still not memory-hard. |
| P2-2 | Partial | **Fixed** | Public `aes256EcbBlock`, `aes256Ctr`, and `pbkdf2HmacSha256` call CNG on Windows and fall back to the handwritten twins (`src/SchemeCrypto.cpp`). `CMakeLists.txt` links `bcrypt`. `tests/scheme_package_test.cpp` `ecbCngMatchesHandwritten`, `ctrCngMatchesHandwritten`, and `pbkdf2CngMatchesHandwritten` require the CNG output, the handwritten output, and the public entry to match, including a 60000-round PBKDF2 case. The FIPS 197 and SP 800-38A known answers still go through the public entry. `constTimeEquals` still folds in the length mismatch, and the scheme MAC compare still uses it (`src/SchemePackage.cpp`). HMAC stays the `QCryptographicHash` construction. CTR is assembled here from CNG ECB blocks. The handwritten code remains the fallback and the test oracle (ledger row 172). |
| P2-3 | Partial | **Partial** | Not in this diff. `onDataReceived` still matches `text == source` or `text.startsWith(source)` and breaks on the first `QMap` hit (`src/GlobalTriggerManager.cpp`). The config and load gates from r6 and r7 are unchanged. Ledger row 170 records keeping that single-source prefix match. |
| P2-4 | Still open | **Still open** | `src/FlowExecutor.cpp` destructor is still the only `qFatal(` call under `src/`, `include/`, and `ui/`. Ledger row 160 records keeping that abort. |
| P2-5 | Still open | **Still open** | No language switch was added. `MainWindow.cpp` now has 173 `tr(` call sites (the new gate strings). Ledger row 171 records leaving the English UI off. `docs/internationalization-guide.md` still carries its “this is a tutorial, not the tree” banner. |
| P2-6 | Still open | **Still open** | `MainWindow.cpp` is 4347 lines. `FlowExecutor.cpp` is 1950 lines. Dual Halcon/OpenCV node stacks are unchanged. |
| P2-7 | Fixed | **Fixed** | Unchanged. `docs/security-development-guide.md` still opens with the template banner. |
| P2-8 | Partial | **Partial** | `project-audit-report/` is no longer tracked. `tools/eol_shape_gate.py` `EXPECTED_TOOL_WRITTEN` is 140, with a comment naming that deletion. README stays. Still tracked at the repo root: `requirements_decoded.txt`, `需求.txt`, and `diag-churn.txt`. Ledger row 169 records the deletion; row 162 still keeps the tombstones. |
| N1 | Fixed | **Fixed** | Unchanged. Scheme import still rejects a bad iteration count before key derivation. |
| N2 | Fixed | **Fixed** | Unchanged. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` still starts `diff` at 1 when the sizes differ (`src/SchemeCrypto.cpp`). |

## 3. New findings

None. The CNG failure path rewrites the output with the handwritten twin and emits one `qWarning`. Row 172 records that fallback as same-bytes, and the three comparison tests require the two backends to match. Double-click and the node menu’s “编辑模块” both call `requestNodeEdit`, which lands in `openModuleEditor` and hits the write gate. The parameter dock’s “执行算子” button still runs the selected node; row 166 keeps run separate from edit.

## 4. What improved

- With the factory password still in use, the canvas edit lock is on even when the flow is idle, and the module editor will not open. An editor already open for that node is closed. The refusal text names 画布.
- New account hashes use 60000 rounds of PBKDF2-HMAC-SHA256. Older tokens still verify at the round count stored in the token.
- Envelope AES and PBKDF2 on Windows go through CNG. The handwritten implementations stay as a bit-exact fallback and as the test oracle. Known-answer tests still pass through the public entry.
- `project-audit-report/` (vendored mermaid, echarts, and fonts) is out of the tree. The line-ending freeze was moved from 142 to 140 to match that deletion.

## 5. Leftovers, in order

1. **P0-1 remainder.** Cancel still leaves an Admin session. `canRunFlow()` is still “someone is logged in.” Script confirmation still defaults off, so a script already in the loaded project still runs. The new gate blocks changing that script from the canvas and the module editor.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, listeners on every interface.
3. **P2-3.** One configured source still prefix-matches on the wire. Ledger row 170 keeps that for variable device tails.
4. **P2-1 remainder.** The password KDF is still PBKDF2, now at 60000 rounds. Ledger row 167 records not moving to a memory-hard function.
5. **P2-4, P2-5, P2-6, and the tombstones in P2-8.** `qFatal` on join timeout, English UI left off, `MainWindow` / `FlowExecutor` size, and `requirements_decoded.txt`, `需求.txt`, `diag-churn.txt`.

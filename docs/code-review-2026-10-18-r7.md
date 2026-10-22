# Vision Workbench re-review — 2026-10-18 r7

Follow-up to the r1–r6 notes. Tree: `origin/main` at `02cbb30` (“U-155 … P2-3 残项收口（fromJson 绕门 + 门后两枚洞）、memory-hard KDF 拍板项登记、大文件五枚清点登记”). Diff since r6’s `cbe6337` is 5 files, +122/−5.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `cbe6337`

No new modules. `GlobalTriggerManager::fromJson` takes an optional `QStringList *rejected`. String entries now pass the same prefix gate as `setStringTrigger`: an empty source is dropped, a source that is a prefix of an already loaded source (or the reverse) is dropped, and a repeat of the same source replaces the row and removes the previous id from `m_allTriggers`. Event entries skip that gate. `ProjectManager::applyProjectJson` passes the rejection list through `reportLoadNote` and adds the count to `m_loadRawLossCount`. Ledger rows 163–165 record the load-path gate, the memory-hard KDF decision, and a census of five tracked files over 300 KB. The match path in `onDataReceived`, `qFatal`, i18n call sites, AES, and the password KDF code are unchanged.

## 2. Status of prior findings

| ID | r6 status | Now | Evidence on `02cbb30` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Still open** | Not in this diff. `AppDatabase` still seeds `admin` / `createPasswordHash("admin")` (`src/AppDatabase.cpp`). Cancel still leaves the write lock up (`MainWindow::enforceFactoryPasswordPolicy`). `SessionManager::canRunFlow()` is `isLoggedIn()` (`src/SessionManager.cpp`). Design mode still calls `setInteractive(true)`. `ScriptSecurityPolicy` still defaults `m_requireConfirmation = false` (`include/ScriptSecurityPolicy.h`). |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` stays unsigned. `ImageReadNode` still takes `filePath` with no path gate. Empty-passphrase export still has no MAC. |
| P1-2 | Fixed | **Fixed** | Unchanged. Import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Still open | **Still open** | Not in this diff. `TcpCommNode` still `listen(QHostAddress::Any, … port default 502)`. `UdpCommNode` still `bind(QHostAddress::AnyIPv4, …)`. `ModbusNode` server mode still sets `NetworkPortParameter` only. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` still names Qt 6, HALCON 24.11, OpenCV, and `tools/ci.ps1`. The CI-guide banner still labels that guide as a blueprint. |
| P2-1 | Partial | **Partial** | Password code unchanged: salt from `QRandomGenerator::system()`, 12000 rounds of PBKDF2-HMAC-SHA256 (`src/AppDatabase.cpp` `kHashIterations`), legacy verifiers kept for migration. Ledger row 164 records a memory-hard KDF (argon2id or scrypt) as a decision, with no code change. |
| P2-2 | Partial | **Partial** | Not in this diff. Compares still go through `constTimeEquals`, including the length term (`src/SchemeCrypto.cpp`). AES-256-CTR stays hand-rolled. Ledger row 158 records keeping that implementation. |
| P2-3 | Partial | **Partial** | The load-path remainder from r6 is closed. `fromJson` drops an empty string source (`src/GlobalTriggerManager.cpp` around the “字符串触发源为空” note) and drops a source that `startsWith` an installed key or that an installed key `startsWith` (the “互为前缀未装” note). A repeat key removes the old id from `m_allTriggers` before the new row is inserted. `ProjectManager::applyProjectJson` emits `loadNote` for each drop and adds the count to `m_loadRawLossCount` (`src/ProjectManager.cpp`). `tests/integration_test.cpp` `testTriggerFromJsonGatesApply` requires two rejections, keeps `JSeed` overwritten to flow `F1b`, keeps `JSort2` and event `EVT_A`, and requires the rejected and overwritten ids to be absent from `allTriggers()`. Still open inside the original item (“short string triggers match too much”): `onDataReceived` still matches `text == source` or `text.startsWith(source)` and breaks on the first `QMap` hit (`src/GlobalTriggerManager.cpp`). One short source still fires on any longer payload that begins with it. Ledger rows 159 and 163 keep that single-source prefix match for variable device tails. The original suggestion — exact match, a delimiter, or a minimum length — is not in the tree. |
| P2-4 | Still open | **Still open** | `src/FlowExecutor.cpp` destructor is still the only `qFatal(` call under `src/`, `include/`, and `ui/`. Ledger row 160 records keeping that abort. |
| P2-5 | Still open | **Still open** | No `tr(` call sites moved. Ledger row 161 remains the census (212 `tr` sites, `MainWindow.cpp` 172, `visionflow_en.ts` 230 source entries). |
| P2-6 | Still open | **Still open** | `MainWindow.cpp` is 4325 lines. `FlowExecutor.cpp` is 1950 lines. Dual Halcon/OpenCV node stacks are unchanged. Ledger row 165 counts five tracked files over 300 KB; that census does not split these types. |
| P2-7 | Fixed | **Fixed** | Unchanged. `docs/security-development-guide.md` still opens with the template banner. |
| P2-8 | Partial | **Partial** | README stays. Still tracked at the repo root: `requirements_decoded.txt`, `需求.txt`, `diag-churn.txt`, and `project-audit-report/`. Ledger row 165 records the audit report’s vendored JS (mermaid and echarts) and three ledger volumes as kept in place, because the active ledger names them. |
| N1 | Fixed | **Fixed** | Unchanged. Scheme import still rejects a bad iteration count before key derivation. |
| N2 | Fixed | **Fixed** | Unchanged. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` still starts `diff` at 1 when the sizes differ (`src/SchemeCrypto.cpp`). |

## 3. New findings

None. The empty-source drop and the same-key id removal are the two holes named in the commit, and both are covered by `testTriggerFromJsonGatesApply`. A saved file that already contains a prefix pair loads the earlier row and reports the later one; which row survives follows JSON array order, and the note is visible through `loadNote`.

## 4. What improved

- Opening a project runs string triggers through the same prefix gate as the add dialog. A conflicting pair can no longer come back from disk and leave the winner to map order.
- An empty trigger source, which `startsWith("")` would match on every payload, is refused on load. Each refusal is a load note and counts toward the raw-loss total, so the next save dropping that row is explained.
- Replacing a string trigger from JSON removes the previous id from `m_allTriggers`, so the table does not keep a row the map no longer fires.

## 5. Leftovers, in order

1. **P0-1.** Cancel still leaves an Admin session that can edit a script and run it. The write lock blocks save, export, user admin, and comm config. `canRunFlow()` does not consult it.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, listeners on every interface.
3. **P2-3 remainder.** One configured source still prefix-matches on the wire. Rows 159 and 163 keep that for variable device tails. Exact match, a delimiter, and a minimum length are still absent.
4. **P2-2 remainder.** Hand-rolled AES-256-CTR. Ledger row 158 records keeping it.
5. **P2-1 remainder.** Memory-hard password KDF. Ledger row 164 records it as a decision; the code is still 12000-round PBKDF2-HMAC-SHA256.
6. **P2-4, P2-5, P2-6, and the stale root files in P2-8.** `qFatal` on join timeout, the i18n census, file size, and the tombstones plus `project-audit-report/`. Ledger row 165 records keeping the large tracked files.

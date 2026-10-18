# Vision Workbench re-review — 2026-10-18 r6

Follow-up to the r1–r5 notes. Tree: `origin/main` at `cbe6337` (“U-154 … 字符串触发的前缀歧义闸 … qFatal … i18n … 安全指南更正注与根目录墓碑对账”). Diff since r5’s `54f5d5b` is 5 files, +60/−1.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `54f5d5b`

No new modules. `GlobalTriggerManager::setStringTrigger` now refuses a source that is a prefix of an existing source, or the other way around. Same-key reset still overwrites. `GlobalTriggerDialog::onAddStringTrigger` shows “触发器未添加” when that call returns false. `docs/security-development-guide.md` has a one-paragraph banner under the title; the body is unchanged. Ledger rows 159–162 record the owner’s disposition of the four P2 items in this commit. Match path, `qFatal`, i18n call sites, AES, the password KDF, and scheme import are unchanged.

## 2. Status of prior findings

| ID | r5 status | Now | Evidence on `cbe6337` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Still open** | Not in this diff. `AppDatabase` still seeds `admin` / `createPasswordHash("admin")` / Admin (`src/AppDatabase.cpp` around the default-user insert). `MainWindow::enforceFactoryPasswordPolicy` breaks the change-password loop on Cancel and leaves the write lock up (`src/MainWindow.cpp`). `SessionManager::canRunFlow()` is `isLoggedIn()` (`src/SessionManager.cpp`). Design mode still calls `setInteractive(true)` (`src/MainWindow.cpp`). `ScriptSecurityPolicy` still defaults `m_enabled = true` and `m_requireConfirmation = false` (`include/ScriptSecurityPolicy.h`). |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` stays unsigned. `ImageReadNode` still takes `filePath` with no path gate. Empty-passphrase export still has no MAC (known limit carried since r1). |
| P1-2 | Fixed | **Fixed** | Unchanged. Import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Still open | **Still open** | Not in this diff. `TcpCommNode` still `listen(QHostAddress::Any, … port default 502)`. `UdpCommNode` still `bind(QHostAddress::AnyIPv4, …)`. `ModbusNode` server mode still sets `NetworkPortParameter` only. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` still names Qt 6, HALCON 24.11, OpenCV, and `tools/ci.ps1`. The CI-guide banner still labels that guide as a blueprint. |
| P2-1 | Partial | **Partial** | Not in this diff. Salt and scheme salt/IV still come from `QRandomGenerator::system()`. The cost line is still 12000 rounds of PBKDF2-HMAC-SHA256. Legacy verifiers remain so old rows can log in and migrate. |
| P2-2 | Partial | **Partial** | Not in this diff. Compares still go through `constTimeEquals`, including the length term (`src/SchemeCrypto.cpp`). AES-256-CTR, HMAC, and PBKDF2 in `src/SchemeCrypto.cpp` stay hand-rolled. Ledger row 158 records keeping that implementation. |
| P2-3 | Still open | **Partial** | Config door is closed for `setStringTrigger`. Inside the mutex, a new source that `startsWith` an existing key, or that an existing key `startsWith`, returns false (`src/GlobalTriggerManager.cpp`, the loop before the map write). Same-key reset skips that check and overwrites. The dialog warns instead of dropping the bool (`src/GlobalTriggerDialog.cpp`, “触发器未添加”). `tests/integration_test.cpp` `testStringTriggerPrefixAmbiguityRejected` rejects `Short` against `ShortSeed`, rejects `ShortSeedX` / `ShortSeedXX`, allows the same-key reset, and checks one exact-match `triggerFired`. Still open inside this item: `onDataReceived` still matches `text == source` or `text.startsWith(source)` and breaks on the first hit, so a lone short source still fires on any longer payload that begins with it. `fromJson` still assigns `m_stringTriggers[e.triggerSource] = e` with no prefix check, so a saved project can restore a conflicting pair and the hash-order race with it. Ledger row 159 says the prefix-match semantic stays, because device lines carry a variable tail. |
| P2-4 | Still open | **Still open** | `src/FlowExecutor.cpp` destructor is still the only `qFatal(` call under `src/`, `include/`, and `ui/`. It fires when `joinForDestroy` times out, with the comment that continuing would be a use-after-free. Ledger row 160 records keeping that abort. The census does not remove it. |
| P2-5 | Still open | **Still open** | No `tr(` call sites moved. Ledger row 161 records a census: `tr` in six `src` files, 212 sites, `MainWindow.cpp` holding 172, `visionflow_en.ts` holding 230 source entries. Enabling an English UI stays a product decision. |
| P2-6 | Still open | **Still open** | `MainWindow.cpp` is 4325 lines. `FlowExecutor.cpp` is 1950 lines. Dual Halcon/OpenCV node stacks are unchanged. |
| P2-7 | Still open | **Fixed** | `docs/security-development-guide.md` opens with a 2026-10-18 banner: the guide has been a generic template since `16b9587`, the live baseline is the README security section and the ledger, and the body has no section-by-section map onto this tree. The body is kept. `validateFilePath` and the other named helpers are still absent under `src/`. That absence is now labeled, which was the close condition (call the real checks, or mark the guide as a template). Ledger row 162 records the same choice. |
| P2-8 | Partial | **Partial** | README stays. Still tracked at the repo root: `requirements_decoded.txt` (tombstone, 废弃日期 2026-09-14), `需求.txt` (已迁移), `diag-churn.txt`, and `project-audit-report/`. Ledger row 162 says the tombstones stay so old doc pointers do not dangle. |
| N1 | Fixed | **Fixed** | Unchanged. Scheme import still rejects a bad iteration count before key derivation. |
| N2 | Fixed | **Fixed** | Unchanged. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` still starts `diff` at 1 when the sizes differ (`src/SchemeCrypto.cpp`). |

## 3. New findings

None. The load-path bypass is the remainder of P2-3, not a separate defect: `fromJson` never called `setStringTrigger`, and this commit did not add a check there. A single non-overlapping source still prefix-matches on the wire, which row 159 keeps on purpose.

## 4. What improved

- Two trigger sources that are prefixes of each other can no longer be added through `setStringTrigger`. The add dialog says why.
- For pairs configured after this commit, which flow runs is no longer left to `QHash` order.
- `docs/security-development-guide.md` no longer reads as the live control set. The banner points at the README security section and the ledger.

## 5. Leftovers, in order

1. **P0-1.** Cancel still leaves an Admin session that can edit a script and run it. The write lock blocks save, export, user admin, and comm config. `canRunFlow()` does not consult it.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, listeners on every interface.
3. **P2-3 remainder.** Prefix match on the wire, and `fromJson` restoring a conflicting pair without the new gate.
4. **P2-2 remainder.** Hand-rolled AES-256-CTR. Ledger row 158 records keeping it.
5. **P2-1 remainder.** Memory-hard password KDF, if the 12000-round cost line is ever raised.
6. **P2-4, P2-5, P2-6, and the stale root files in P2-8.** `qFatal` on join timeout, the i18n census, file size, and the tombstones. Ledger rows 160–162 record keeping the first, third-as-census, and the tombstones.

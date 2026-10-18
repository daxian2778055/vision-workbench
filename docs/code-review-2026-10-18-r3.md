# Vision Workbench re-review — 2026-10-18 r3

Follow-up to `docs/code-review-2026-10-18.md` and `docs/code-review-2026-10-18-r2.md`. Tree: `origin/main` at `8dfde57` (“U-151: PBKDF2 进口改为先值检后派生 … + CI 指南更正注”). Diff since r2’s `688e86a` is 5 files, +59/−21.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `688e86a`

No new modules. Encrypted-scheme import no longer clamps the iteration field and then derives. `src/SchemePackage.cpp` `importScheme` reads the `uint32` and, if it is not exactly `kIterations` (100000), returns before `deriveKeys`. `include/SchemePackage.h` dropped `kIterationsMin` / `kIterationsMax` and added `kIterationsValid`. Export is unchanged. `docs/ci-cd-guide.md` gained a banner at the top. Ledger rows 150–152 record the owner’s disposition. Nothing else in the runtime moved.

## 2. Status of prior findings

| ID | r2 status | Now | Evidence on `8dfde57` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Still open** | Unchanged, and not in this diff. `src/AppDatabase.cpp` still seeds `admin` / hash of `admin` / `Admin`. `MainWindow::enforceFactoryPasswordPolicy` still `break`s on Cancel. `SessionManager::canRunFlow()` is still `isLoggedIn()` only. `switchToDesignMode` still calls `setInteractive(true)`. `ScriptSecurityPolicy` still defaults `m_enabled = true`, `m_requireConfirmation = false`. Ledger row 152 restates this as the A1 write-gate design and does not change code. |
| P1-1 | Still open | **Still open** | Unchanged. Empty-passphrase export is still compressed plaintext with no MAC (`SchemePackage::exportScheme` else-branch). `WriteFileNode` / `ImageReadNode` still take `filePath` from the scheme. FR3.6 already records the unencrypted readonly limit. |
| P1-2 | Partial (cap, then ~10 s KDF) | **Fixed** | `src/SchemePackage.cpp` lines 97–98: `if (iterations != kIterationsValid) return QByteArray();` then `deriveKeys(..., kIterations, ...)`. `*ok` is set false at the start of `importScheme` and stays false on that return. `ProjectManager::loadProject` checks `ok` before applying JSON. Tests: `oversizedIterationsRejectedAndBounded` (`0x7FFFFFFF`), `iterationsSensitiveToInnerDelta` (+1), `foreignInRangeValueRejectedFast` (500000). All three must fail, and the two timed cases must return in under 700 ms. |
| P1-3 | Still open | **Still open** | Unchanged. `TcpCommNode` `listen(Any, 502)`, `UdpCommNode` `bind(AnyIPv4)`, `ModbusNode` port only. Ledger row 144. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. `HalconNode::process` still fails the node when a promised image output is empty after `imwrite` clears it. |
| P1-5 | Partial | **Partial** | `docs/ci-cd-guide.md` now opens with a 2026-10-18 banner: the body is a blueprint, `ci-cd.yml` is gone, the live workflows are `ci.yml` and `repo-hygiene.yml`. The body underneath is still the old CodeQL / Codecov / Slack text, kept on purpose. No root README. Qt tests remain inside `if(MSVC)`. |
| P2-1 | Still open | **Still open** | `src/AppDatabase.cpp` still stretches SHA-256 12000 times and salts with `QRandomGenerator::global()`. |
| P2-2 | Still open | **Still open** | `src/SchemeCrypto.cpp` unchanged. MAC check is still `!=` in `src/SchemePackage.cpp`. |
| P2-3 | Still open | **Still open** | `src/GlobalTriggerManager.cpp` still `startsWith`. Ledger row 145. |
| P2-4 | Still open | **Still open** | `FlowExecutor` destructor still `qFatal` after join timeout. |
| P2-5 | Still open | **Still open** | i18n surface unchanged. |
| P2-6 | Still open | **Still open** | `MainWindow` / `FlowExecutor` / dual node stacks unchanged. |
| P2-7 | Still open | **Still open** | `docs/security-development-guide.md` not edited. `validateFilePath` is still absent under `src/`. |
| P2-8 | Still open | **Still open** | No root README. `requirements_decoded.txt`, `需求.txt`, and `project-audit-report/` remain. |
| N1 | P2, 15 s wall clock can flake or miss a higher cap | **Fixed** | The 15000 ms assert is gone. Reject path is O(1) and the bound is 700 ms, which their red-arm measured at ~940 ms when the check is removed and one 100000-iteration derive runs. `foreignInRangeValueRejectedFast` locks the in-range foreign value that the old clamp would have derived. |
| N2 | P2, `docs/ci-cd-guide.md` was not in the citation fix | **Fixed** as a “reads as current CI” bug | Banner at the top of `docs/ci-cd-guide.md`, same shape as the other five files. The leftover “no README / body is still the blueprint” stays under P1-5. |

## 3. New findings

None. The early reject does not skip the MAC for a file whose iteration field is the real 100000: that path still calls `deriveKeys` and compares the HMAC. A file with any other count never reaches the KDF, and `*ok` stays false.

## 4. What improved

- A hostile encrypted envelope can no longer buy PBKDF2 time. `0x7FFFFFFF`, `100001`, and `500000` all fail before `deriveKeys`.
- Existing exports still open: the writer still stores 100000, and that is the only accepted count.
- `docs/ci-cd-guide.md` no longer presents CodeQL, Codecov, Slack, and `ci-cd.yml` as the live pipeline. The first paragraph says the opposite.

## 5. Leftovers, in order

1. **P0-1.** Cancel still leaves an Admin session. Design mode still unlocks the canvas. Script confirmation still defaults off. Run is still outside the write gate. Their ledger treats this as intentional until a role matrix is specified. The code matches that statement and does not match the dialog sentence that anyone logged in with `admin`/`admin` can change the scheme.
2. **P1-1.** Plain `.vfp` is still unsigned, and image `filePath` is still any path the process can touch.
3. **P1-3.** TCP, UDP, and Modbus server still listen on every interface, with prefix triggers.
4. **P1-5 leftover.** Add a root README that names `tools/ci.ps1`, the two workflows, and the MSVC / HALCON / Qt prerequisites. The CI guide banner is enough to stop the false “this is deployed” reading.
5. **P2-1, P2-2, P2-7.** Account KDF, constant-time MAC compare, and the security guide that describes helpers not present in `src/`.

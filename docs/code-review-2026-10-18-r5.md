# Vision Workbench re-review — 2026-10-18 r5

Follow-up to the r1–r4 notes. Tree: `origin/main` at `54f5d5b` (“U-153 … 恒时比较折长钉零尾、旧格式摘要比对恒时化、盐源两处换 OS CSPRNG 直流、AES 手写立维持态拍板项”). Diff since r4’s `2bc1988` is 6 files, +68/−14.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `2bc1988`

No new modules. Password salt (`src/AppDatabase.cpp` `createPasswordHash`) and scheme salt/IV (`src/SchemePackage.cpp` `randomBytes`) now take bytes from `QRandomGenerator::system()->generate()` instead of `global()` / `securelySeeded()`. `SchemeCrypto::constTimeEquals` folds in a length mismatch. Legacy password compares use that function. Hand-rolled AES-256-CTR is unchanged; the commit message records keeping it as a decision, not an omission.

## 2. Status of prior findings

| ID | r4 status | Now | Evidence on `54f5d5b` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Still open** | Not in this diff. Seed, Cancel, `canRunFlow()`, `setInteractive(true)`, and script defaults are unchanged. |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` and image `filePath` unchanged. |
| P1-2 | Fixed | **Fixed** | Unchanged. Import still returns before `deriveKeys` unless the iteration field is exactly 100000. |
| P1-3 | Still open | **Still open** | Not in this diff. TCP `Any`, UDP `AnyIPv4`, Modbus port-only, `startsWith` trigger. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Fixed | **Fixed** | Unchanged. `README.md` and the CI-guide banner still match r4. |
| P2-1 | Partial | **Partial** | Salt half is closed. `createPasswordHash` fills 16 bytes from `QRandomGenerator::system()`. `testPasswordHashRoundTrip` requires two hashes of the same password to differ, and the salt hex to be 32 characters. Scheme `randomBytes` uses the same generator. `QRandomGenerator::global` / `securelySeeded` no longer appear under `src/` or `include/`. Still open inside this item: 12000 rounds of PBKDF2-HMAC-SHA256 is not a memory-hard KDF. The r4 note already recorded that the cost line was kept on purpose. Legacy verifiers remain so old rows can log in and migrate. |
| P2-2 | Partial | **Partial** | Compare half is closed. `verifyPassword` legacy branches call `constTimeEquals` on raw digest bytes (`src/AppDatabase.cpp`). `testLegacyHashesAuthenticateAndUpgrade` rejects a wrong password on both legacy formats. Scheme MAC compare was already `constTimeEquals`. Still open: AES-256-CTR, HMAC, and PBKDF2 in `src/SchemeCrypto.cpp` are still the hand-rolled implementation. This commit says that stays. |
| P2-3 | Still open | **Still open** | `GlobalTriggerManager` still `startsWith`. |
| P2-4 | Still open | **Still open** | `FlowExecutor` destructor still `qFatal`. |
| P2-5 | Still open | **Still open** | i18n surface unchanged. |
| P2-6 | Still open | **Still open** | `MainWindow` / `FlowExecutor` / dual node stacks unchanged. |
| P2-7 | Still open | **Still open** | `docs/security-development-guide.md` not edited. `validateFilePath` is still absent under `src/`. |
| P2-8 | Partial | **Partial** | README stays. `requirements_decoded.txt`, `需求.txt`, and `project-audit-report/` are still in the tree. |
| N1 | Fixed | **Fixed** | Unchanged. |
| N2 | Fixed | **Fixed** | Unchanged. |
| N3 | New P2 | **Fixed** | `src/SchemeCrypto.cpp` `constTimeEquals`: `diff` starts at 1 when `a.size() != b.size()`, then ORs byte differences out to the longer length. `tests/scheme_package_test.cpp` `constTimeEqualsBasics` now requires `constTimeEquals("ab", "ab" + four zero bytes)` to be false, and the same zero tail against itself to be true. |

## 3. New findings

None. The length term makes a zero tail of a different length compare unequal. Equal buffers, including an equal zero tail, still compare equal. The `system()` generator is consumed 8 bits at a time from each `quint32`, so the old `bounded(256)` bias is gone. A wrong-length legacy digest fails the length term before it can match a prefix.

## 4. What improved

- Password salt and envelope salt/IV come from the OS CSPRNG stream Qt documents for credentials.
- `constTimeEquals` no longer treats a shorter buffer as zero-padded.
- Both legacy password compares are constant-time on the digest bytes, same as the new PBKDF2 token and the scheme MAC.

## 5. Leftovers, in order

1. **P0-1.** Cancel still leaves an Admin session that can edit a script and run it.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, listeners on every interface.
3. **P2-2 remainder.** Hand-rolled AES-256-CTR. Recorded as a kept decision, not a missed edit.
4. **P2-1 remainder.** Memory-hard password KDF, if the 12000-round cost line is ever raised. Not required to close the salt bug.
5. **P2-3 through P2-7, and the stale root files in P2-8.** Unchanged.

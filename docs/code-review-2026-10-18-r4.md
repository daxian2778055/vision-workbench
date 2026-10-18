# Vision Workbench re-review — 2026-10-18 r4

Follow-up to the r1, r2, and r3 notes. Tree: `origin/main` at `2bc1988` (“U-152 … 账户口令 KDF 换构造进 PBKDF2 … 信封 MAC 校验换恒时比较、README 缺位补齐”). Diff since r3’s `8dfde57` is 8 files, +255/−12.

This pass did not rebuild or run CTest.

## 1. Architecture delta since `8dfde57`

No new modules. Account passwords and scheme envelopes now share `SchemeCrypto::pbkdf2HmacSha256`. New password tokens are `pbkdf2$<rounds>$<saltHex>$<digestHex>` (`src/AppDatabase.cpp` `createPasswordHash`). Login still accepts the two older formats and rewrites them to the new token (`authenticateUser`). Scheme MAC check uses `SchemeCrypto::constTimeEquals` (`src/SchemePackage.cpp`). A root `README.md` now exists. Ledger rows 153–155 record the owner’s disposition.

## 2. Status of prior findings

| ID | r3 status | Now | Evidence on `2bc1988` |
| --- | --- | --- | --- |
| P0-1 | Still open | **Still open** | Not in this diff. Seed, Cancel, `canRunFlow()`, `setInteractive(true)`, and script defaults are unchanged. `README.md` states the same rule: factory password is `admin`/`admin`, the write gate blocks save/export/user admin, and flow run is not blocked. |
| P1-1 | Still open | **Still open** | Not in this diff. Plain `.vfp` and image `filePath` unchanged. `README.md` repeats that an export with no passphrase is compressed plaintext and does not promise integrity. |
| P1-2 | Fixed | **Fixed** | Unchanged. `importScheme` still returns before `deriveKeys` unless the iteration field is exactly 100000. |
| P1-3 | Still open | **Still open** | Not in this diff. `README.md` states the same choice: servers bind every interface and there is no link authentication. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. |
| P1-5 | Partial | **Fixed** | `README.md` is the missing entry. It names Qt 6, HALCON 24.11, OpenCV under `thirdparty/opencv`, the Visual Studio 2022 x64 configure/build/`ctest` commands, and `tools/ci.ps1`. The `docs/ci-cd-guide.md` banner from r3 still marks the CodeQL / Slack body as a blueprint. The two workflow filenames are in that banner, not repeated in the README. |
| P2-1 | Still open | **Partial** | New hashes are PBKDF2-HMAC-SHA256, 12000 rounds, 32-byte digest (`createPasswordHash`). `verifyPassword` refuses a token whose round count is outside `[1, 1000000]` before `pbkdf2HmacSha256` (`testPasswordHashRoundTrip`, 700 ms bound). Login migrates both legacy formats (`testLegacyHashesAuthenticateAndUpgrade`). Still open inside this item: salt bytes still come from `QRandomGenerator::global()` in `createPasswordHash`, and 12000 rounds is still not a memory-hard KDF. The code comment says the cost line was kept on purpose. |
| P2-2 | Still open | **Partial** | `src/SchemePackage.cpp` MAC check is `constTimeEquals`. New password digests use the same function. AES-256-CTR in `src/SchemeCrypto.cpp` is still the hand-rolled implementation. The two legacy password branches in `verifyPassword` still use `QString ==`. |
| P2-3 | Still open | **Still open** | `GlobalTriggerManager` still `startsWith`. |
| P2-4 | Still open | **Still open** | `FlowExecutor` destructor still `qFatal`. |
| P2-5 | Still open | **Still open** | i18n surface unchanged. |
| P2-6 | Still open | **Still open** | `MainWindow` / `FlowExecutor` / dual node stacks unchanged. |
| P2-7 | Still open | **Still open** | `docs/security-development-guide.md` not edited. `validateFilePath` is still absent under `src/`. |
| P2-8 | Still open | **Partial** | `README.md` added. `requirements_decoded.txt`, `需求.txt`, and `project-audit-report/` are still in the tree. |
| N1 | Fixed | **Fixed** | Unchanged. |
| N2 | Fixed | **Fixed** | Unchanged. The CI guide banner is still the one from r3. |

## 3. New finding

| ID | Sev | Topic | Evidence |
| --- | --- | --- | --- |
| N3 | P2 | `constTimeEquals` treats a missing byte as `0x00` | `src/SchemeCrypto.cpp`: the loop runs to `qMax(a.size(), b.size())` and substitutes `0` past either end. There is no length term in `diff`. So `constTimeEquals("ab", "ab\\0")` is true. `tests/scheme_package_test.cpp` `constTimeEqualsBasics` checks a non-zero tail (`"tail"`, `"ab"`) and does not check a zero tail. Current call sites pass two 32-byte buffers (HMAC vs envelope MAC, and PBKDF2 digest vs the decoded token digest), so this is not an auth bypass on those paths. The helper does not match the test comment that any length difference is false. |

No other new issue. A token with rounds above 1,000,000 is rejected before PBKDF2. A token whose rounds are in range still pays that many iterations; that is the stated compatibility rule, and the cap matches the scheme-import ceiling.

## 4. What improved

- New account hashes are standard PBKDF2-HMAC-SHA256, not the homemade SHA-256 chain. Old unsalted and old salted hashes still log in, then get rewritten.
- Scheme MAC compare no longer returns on the first different byte. New password digests use the same compare.
- A root README now matches the real build (`Visual Studio 17 2022`, `tools/ci.ps1`) and states the factory-password, envelope, and bind-all-interfaces behavior in one place.

## 5. Leftovers, in order

1. **P0-1.** Cancel still leaves an Admin session that can edit a script and run it. The new README documents that, and does not change it.
2. **P1-1 and P1-3.** Unsigned plain schemes, unrestricted image paths, and listeners on every interface. Also documented, not changed.
3. **P2-1 remainder.** Take password salt from `QRandomGenerator::system()` (or the OS CSPRNG). `global()` is still not a CSPRNG. Memory-hard KDF was explicitly not this change.
4. **N3.** Fold `a.size() != b.size()` into `constTimeEquals` and add a zero-tail case next to `constTimeEqualsBasics`.
5. **P2-2 remainder.** Legacy password `==` compares, and the hand-rolled AES. **P2-7.** Security guide helpers that are not in `src/`. **P2-8 remainder.** Stale root files besides the README.

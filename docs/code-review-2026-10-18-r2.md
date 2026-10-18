# Vision Workbench re-review — 2026-10-18 r2

Follow-up to `docs/code-review-2026-10-18.md` (PR #1). Tree reviewed: `origin/main` at `688e86a` (“U-150 … 加密信封进口迭代数限幅”). Diff since the first review (`9db7a81..688e86a`) is 9 files, +84/−3. Production code touched: `include/SchemePackage.h`, `src/SchemePackage.cpp`, `tests/scheme_package_test.cpp`. No other runtime behavior changed.

This pass did not rebuild or run CTest. The suite still needs Qt 6, HALCON, MVS, and MSVC.

## 1. Architecture delta

No module was added or removed. The only runtime change is on encrypted-scheme import: the PBKDF2 iteration count read from the envelope is clamped to `[1, 1000000]` before `deriveKeys`. Export still writes `100000`.

Docs: five files that named a missing `ci-cd.yml` / `pre-review.yml` / `static-analysis.yml` now point at `.github/workflows/ci.yml` and `repo-hygiene.yml`, or carry a 2026-10-18 banner. The owner’s ledger (`docs/对标差距推进计划-登记附表.md`, rows 142–149) records their disposition of the first review. That ledger is their notes, not a code fix.

## 2. Status of the previous findings

| ID | First review | Now | Evidence on `688e86a` |
| --- | --- | --- | --- |
| P0-1 | Seeded `admin`/`admin` can edit a script and run it | **Still open** | Seed is unchanged: `src/AppDatabase.cpp` insert of `admin` / `createPasswordHash("admin")` / `Admin` when `users` is empty. Cancel still breaks `enforceFactoryPasswordPolicy` and leaves the session up (`src/MainWindow.cpp`, `dlg.exec() != Accepted` → `break`). `SessionManager::canRunFlow()` is still `isLoggedIn()` only. `switchToDesignMode` still calls `setInteractive(true)` with no role check. `ScriptSecurityPolicy` still defaults `m_enabled = true`, `m_requireConfirmation = false`. Owner rows 146 and 148 call this a product choice and did not change it. The behavior the first review described is still the behavior. |
| P1-1 | Plain `.vfp` is unsigned code; image paths are unrestricted; unencrypted readonly has no MAC | **Still open** | `SchemePackage::exportScheme` still stores compressed plaintext with no MAC when the passphrase is empty. `WriteFileNode::run` still passes `filePath` straight to `cv::imwrite`. `ImageReadNode` still takes `filePath` from the scheme. `docs/需求规格说明书.md` FR3.6 already says the unencrypted readonly package has no MAC and readonly does not lock the canvas. That part was a known limit, not a surprise. Script auto-run and unrestricted image paths are still not covered by FR3.6. |
| P1-2 | Envelope iteration count is unbounded and applied before the MAC | **Partially fixed** | `include/SchemePackage.h`: `kIterationsMin = 1`, `kIterationsMax = 1000000`. `src/SchemePackage.cpp` `importScheme` does `qBound` then `deriveKeys`. `tests/scheme_package_test.cpp`: `oversizedIterationsRejectedAndBounded` (header `0x7FFFFFFF` must fail) and `iterationsSensitiveToInnerDelta` (+1 iteration must fail MAC). Residual: an out-of-range file is not rejected before the KDF. Import pays up to 1,000,000 iterations on the UI thread, then the MAC fails. The new test’s own comment says 1,000,000 iterations measured about 9.9–10.1 s on one machine. Any in-range count, including `1`, is still accepted when the MAC matches. |
| P1-3 | TCP/UDP/Modbus listen on all interfaces; a string starts a flow | **Still open** | `src/TcpCommNode.cpp` still `listen(QHostAddress::Any, port)` default 502. `src/UdpCommNode.cpp` still `bind(AnyIPv4)`. `src/ModbusNode.cpp` still sets the port and no address. Owner row 144 records this as a plant-network choice and did not change it. |
| P1-4 | `WriteFileNode` stays green when `imwrite` fails | **Withdrawn** | `HalconNode::init` adds an image input and an image output (default `PortDataType::Image`). `WriteFileNode::init` calls that. On `imwrite` failure `run()` clears `m_outputImage` and returns. `HalconNode::process` then hits `requiresImageOutput() && !output.IsInitialized()` and sets `moduleStatus = false` (`src/HalconNode.cpp`, the “成功必须有产出” block). The node fails. The failure text is the generic empty-output path, not “disk write failed”. That is observability, not a green save. |
| P1-5 | CI guide does not match the workflows | **Partially fixed** | Corrected or bannered: `docs/documentation-summary.md`, `docs/optimization-completion-report.md`, `docs/optimization-summary.md`, `docs/optimized-code-review-process.md`, `docs/static-analysis-integration.md`. Still wrong: `docs/ci-cd-guide.md` still titles a workflow `.github/workflows/ci-cd.yml` and still lists static analysis, CodeQL, Codecov, and Slack. That file is not among the five edits. No root `README`. Qt tests remain inside `if(MSVC)` in `CMakeLists.txt`. Real workflows are still only `ci.yml` and `repo-hygiene.yml`. |
| P2-1 | Account KDF is stretched SHA-256; salt uses `QRandomGenerator::global()` | **Still open** | `src/AppDatabase.cpp` `stretchHash` / `kHashIterations = 12000` and `QRandomGenerator::global()->bounded(256)` are unchanged. Envelope salt still uses `QRandomGenerator::securelySeeded()`. |
| P2-2 | Hand-rolled AES/HMAC; compares are not constant-time | **Still open** | `src/SchemeCrypto.cpp` unchanged. MAC check is still `hmacSha256(...) != mac` in `src/SchemePackage.cpp`. |
| P2-3 | String triggers match by prefix | **Still open** | `src/GlobalTriggerManager.cpp`: `text == entry.triggerSource \|\| text.startsWith(entry.triggerSource)`. Owner row 145 records this as PLC convenience and did not change it. |
| P2-4 | Stuck executor calls `qFatal` | **Still open** | `src/FlowExecutor.cpp` destructor still `qFatal` after `joinForDestroy` times out. Still the right choice versus use-after-free. |
| P2-5 | English UI is a thin `tr()` layer | **Still open** | `include/I18n.h` and the i18n guide banner are unchanged. |
| P2-6 | `MainWindow` / `FlowExecutor` size and dual operator stacks | **Still open** | No split in this commit. |
| P2-7 | Security guide shows helpers that are not in `src/` | **Still open** | `docs/security-development-guide.md` was not edited. `validateFilePath` is still absent under `src/`. |
| P2-8 | Stale root artifacts, no README | **Still open** | `requirements_decoded.txt`, `需求.txt`, and `project-audit-report/` are still there. No root README. |

### Correction to the first write-up of P1-2

The first review said a `uint32` of `0xFFFFFFFF` would loop for hours. `deriveKeys` takes `int`. On the Windows toolchain this project builds with, `static_cast<int>(0xFFFFFFFF)` is `-1`, and `for (j = 1; j < iterations; ++j)` does not run. The expensive range is a large **positive** `int`, up to `0x7FFFFFFF`. The new test uses that value. The clamp closes that hang. What remains is the capped cost in the row above.

## 3. New findings from `688e86a`

| ID | Sev | Topic | Evidence | Note |
| --- | --- | --- | --- | --- |
| N1 | P2 | The new iteration test can fail on a slow machine and still pass if the cap moves up | `tests/scheme_package_test.cpp` `oversizedIterationsRejectedAndBounded`: wall clock `< 15000` ms. The appendix says one machine measured 10055 ms for 1,000,000 iterations. | A correct cap fails CTest if that machine is slower than ~15 s. A faster machine still passes if `kIterationsMax` is raised well above 1,000,000, until the clock exceeds 15 s. Pin the constants, or reject out-of-range counts before `deriveKeys` and assert that the call returns without a multi-second budget. |
| N2 | P2 | `docs/ci-cd-guide.md` was left as the live-looking CI guide | Still contains `ci-cd.yml`, CodeQL, Codecov, Slack. The five files edited in U-150 are not this one. | Same gap as the leftover of P1-5. Called out separately because the commit message says the CI citations were aligned. |

No new P0. The clamp does not open a MAC bypass: an out-of-range count is hashed at the cap, so it will not match a MAC made at the original count. An in-range count is verified at that count.

## 4. What is solid now

- Encrypted import can no longer be asked for two billion PBKDF2 rounds. Export count `100000` is inside the window, so existing envelopes still open. Two new QtTest cases lock the layout of the iteration field and the reject path.
- The first review’s write-failure claim does not hold. Empty image output after `run()` is a failed node.
- Unchanged and still in good shape: bound SQL, per-thread SQLite, factory-password refusal on save/export, absolute script interpreter path, executor lock order, formula parser, fail-closed JSON load, fork-PR skip on the self-hosted runner.

## 5. What is still worth doing, in order

1. P0-1, if the product owner wants the dialog’s own sentence (“任何人都能免授权登录并修改方案”) to be false. Today Cancel plus design mode still allows an in-memory script edit and Run. Their ledger says they are leaving this for a later role matrix. The code does not implement that matrix.
2. P1-2 residual: if `iterations < kIterationsMin` or `iterations > kIterationsMax`, return failure before `deriveKeys`. That removes the ~10 s UI stall and makes N1 unnecessary.
3. P1-5 leftover: replace or banner `docs/ci-cd-guide.md` the same way as the other five files, and add a root README.
4. P1-1 and P1-3 stay product decisions. The code is unchanged, so they stay open until a bind address, a signature, or a path root exists.

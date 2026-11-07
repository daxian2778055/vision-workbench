# Vision Workbench re-review — 2026-10-23 r12

Follow-up to the r1–r11 notes. Series filename stays `code-review-2026-10-18-r12.md`. Tree reviewed: `origin/main` at `ca98748cf4f60346df658242265566a9a9e084c0` (“U-216 … 产品选择余量五处”). Range since r11’s `f0c0322` is **4 commits, 11 files, +368 / −49**.

This pass did not rebuild or run CTest. The cloud VM cannot build or run the Windows suite (Qt 6, HALCON, MVS, MSVC). Nothing below is a test result. New test legs are cited only as the contract the authors wrote.

Runtime and product code were not modified. This note is the only change.

Prior draft: [PR #11](https://github.com/daxian2778055/vision-workbench/pull/11) (`docs/code-review-2026-10-18-r11.md`, baseline `f0c0322`).

## 0. Executive summary

U-213, U-214, and U-215 match the code, not only the commit messages. The alive-block `shared_ptr` is now published and copied under `m_mutex`. A failed `loadProject` rolls the scheme-directory snapshot, the readonly flag, and the integrity note back to the values from before the attempt. A relative path whose resolved location leaves the scheme directory now takes the same confirm gate as an absolute path outside that directory. U-216 is documentation only; the five product choices it records are still what the code does.

The shared-pointer half of P1-6 is closed. The other half is not: a tab change still calls `setFlowScene` on the **target** executor without looking at that executor’s run state, and that call still clears graph-cache sets the worker reads without the cache mutex. P1-6 stays **Partial**. P2-9’s load path is **Fixed**. The write-side sibling the U-214 message deferred is still in `saveProject` and `exportEncryptedProject`; that is **P2-10**.

No previously **Fixed** ID regressed.

| ID | Severity | Status | What |
| --- | --- | --- | --- |
| P1-6 | P1 | **Partial** (was Open) | `m_aliveBlock` is swapped and copied under `m_mutex`. `onCurrentTabChanged` still calls `setFlowScene` on a target that may already be Running. That call clears `m_loopBodyNodes` / `m_subFlow*` under `m_graphCacheMutex` while `run` reads and writes those containers with no lock. |
| P2-9 | P2 | **Fixed** (was Open) | `loadProject` commits the path snapshot, readonly flag, and integrity note only after confirm succeeds. Every failure return after the file is open calls `rollbackSnapshot()`. |
| P2-10 | P2 | **Open** (new) | `saveProject` and `exportEncryptedProject` still call `setLastFilePath` before `QSaveFile::commit`. A failed write leaves `ProjectPaths` aimed at the file that was not written. `saveProject` also clears `m_loadedReadonly` before the write. |

Ledger after this pass: **12 Fixed, 5 Partial, 2 Open, 1 Withdrawn**.

| Bucket | IDs |
| --- | --- |
| Fixed (12) | P0-1, P1-2, P1-3, P1-5, P2-2, P2-4, P2-7, P2-8, **P2-9**, N1, N2, N3 |
| Partial (5) | P1-1, **P1-6**, P2-1, P2-3, P2-6 |
| Open (2) | P2-5, **P2-10** |
| Withdrawn (1) | P1-4 |

Moved since r11: **P2-9** Open → Fixed. **P1-6** Open → Partial. P1-1 stays Partial; the relative-`..` gap inside it is closed, and U-216 keeps the other four residuals as product choices. P2-10 is new.

## 1. Delta since `f0c0322`

| Item | Value |
| --- | --- |
| Baseline | `f0c032240161ec2c4d5d10c4eb2eb8b01e6a0988` (r11) |
| Reviewed HEAD | `ca98748cf4f60346df658242265566a9a9e084c0` |
| Commits | 4 |
| Files | 11 |
| Insertions / deletions | +368 / −49 |

Commits, oldest first:

| SHA | Subject (short) |
| --- | --- |
| `2d2eae9` | U-213 publish `m_aliveBlock` under `m_mutex` |
| `701846c` | U-214 roll back scheme snapshot, readonly flag, and note on failed load |
| `b298b01` | U-215 relative `..` that leaves the scheme directory uses the confirm gate |
| `ca98748` | U-216 five product-choice residuals recorded; docs only |

`MainWindow.cpp` is **4351** lines (r11: 4348). `FlowExecutor.cpp` is **2086** lines (r11: 2052). `src/MainWindow.cpp` still has **186** `tr(` call sites; U-215 edited the confirm strings in place. There is no `qFatal(` under `src/` or `include/`.

### Claim check (commit message versus code)

| Claim | Verdict |
| --- | --- |
| U-213 `m_aliveBlock` published and copied under the same mutex as `m_scene` | **Holds** for the `shared_ptr` object. `setFlowScene` swaps the member inside `m_mutex` (`src/FlowExecutor.cpp`). `run` copies it before `locker.unlock()`. `executeUpTo`, `executeFrom`, and `invalidateDownstreamOf` each copy under `m_mutex` and pass the local to `captureViaAliveBlock`. No remaining read of the member itself is outside that mutex. |
| U-213 “behavior unchanged”; tab switch onto a Running/Paused target left as-is | **The shared-pointer publish is behavior-preserving. The tab call is still there, and it is not harmless.** See P1-6. The new ownership test checks that `setFlowScene` binds the scene’s block. It does not start a worker, and it does not cover the tab path. |
| U-214 failed load restores path, readonly flag, and integrity note | **Holds** for `loadProject`. Open failure returns before any of the three is touched. After a successful open, decrypt/MAC failure, `integrityMissing`, both JSON parse failures, both `buildScenesFromJson` failures, and both confirm refusals call `rollbackSnapshot()`. Both success tails call `setLastFilePath` only after confirm. The integrity note for raw JSON is assigned at that same success tail. |
| U-214 write-side sibling left for a later round | **Holds, and it is still open.** See P2-10. The load regression leg does not call `saveProject` or `exportEncryptedProject`. |
| U-215 relative escape joins the confirm gate | **Holds.** `classifySchemePath` resolves a relative string against the scheme directory and returns `EscapedRelative` when `sameOrInsideDir` is false. `confirmOutsideSchemePaths` treats `escapedRelativeCount` like `outsideCount` (no confirmer means refuse). The dialog lists `outsideHits` and `escapedRelativeHits`. A relative path that stays inside the directory is still `RelativeRaw` and does not ask. |
| U-216 five product choices, no code change | **Holds.** The U-216 diff is two documentation files, +2 lines. Pepper, raw JSON, ungated `applyProjectJson`, no author signature, and UDP port 0 with an empty address are still the code. The erratum is also right: crash recovery calls `loadProjectFile` → `loadProject`, so it does pass the confirm gate. `applyProjectJson` has no production caller (tests only). A comment in `loadProject` still says crash recovery shares `applyProjectJson`; that sentence is stale, the call is not. |

## 2. Ledger

| ID | r11 | r12 | Evidence on `ca98748` |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `executeNodeOnce` and `recomputeDownstream` still return when `canRunFlow()` is false (`src/MainWindow.cpp`). `canRunFlow()` is still `isLoggedIn() && !writesBlocked()` (`src/SessionManager.cpp`). Context menu “仅执行此算子” still calls `node->execute()` and is disabled while `isEditLocked()` (`src/NodeGraphicsItem.cpp`). `canvasEditShouldBeLocked` is still the run-lock OR the write gate (`include/SessionManager.h`). “执行到此处” / “从此处执行” still do not call `canRunFlow()` themselves; the same edit lock disables those menu items. A flow already Running or Paused still queues in `requestExternalRound` before the idle-path `writesBlocked()` check (`src/FlowExecutor.cpp`). The `admin` / `createPasswordHash("admin")` seed is still inserted when `users` is empty (`src/AppDatabase.cpp`). |
| P1-1 | Partial | **Partial** | Narrowed again. v2 plaintext HMAC and `kPlainPepper` are unchanged (`src/SchemePackage.cpp`). v1 envelopes with `integrityMissing` are still refused. Absolute paths outside the scheme directory still need a confirmer. **Relative `..` is no longer a hole:** `classifySchemePath` returns `EscapedRelative` after anchor-and-resolve, and that bucket is in the confirm gate (`src/ProjectPaths.cpp`, `src/ProjectManager.cpp`, `src/MainWindow.cpp`). Still open inside this ID, and U-216 records each as a kept choice: the MAC key is the compiled-in pepper; there is no author signature; a confirmer that returns true still loads the outside path; raw JSON still loads and sets `m_lastIntegrityNote` to `旧明文 JSON 格式`; `applyProjectJson` still applies scenes and globals with no path gate. |
| P1-2 | Fixed | **Fixed** | Encrypted import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Fixed | **Fixed** | `CommBind::resolveListenAddress` still maps empty or whitespace to `BindKind::Missing` and does not store an address (`include/CommunicationNodeBase.h`). TCP server, UDP with `localPort > 0`, and Modbus server still refuse `Missing` and `Invalid`. Explicit `0.0.0.0` remains a legal every-interface literal. Residual, kept by U-216: UDP `localPort == 0` with a missing address still binds `QHostAddress::AnyIPv4` (`src/UdpCommNode.cpp`). The default local port is 8000, which takes the refuse path. |
| P1-4 | Withdrawn | **Withdrawn** | Still withdrawn. `WriteFileNode::run` still clears `m_outputImage` when `cv::imwrite` fails (`src/WriteFileNode.cpp`). `HalconNode::process` still sets failure when `requiresImageOutput()` and the image is not initialized (`src/HalconNode.cpp`, the “成功必须有产出” check). |
| P1-5 | Fixed | **Fixed** | `README.md` still names the Qt 6 / HALCON / OpenCV stack and `tools/ci.ps1`. |
| P1-6 | Open | **Partial** | Shared-pointer publish is fixed. Tab-switch `setFlowScene` on a running target is not. See section 3. |
| P2-1 | Partial | **Partial** | `kHashIterations` is still 60000. The family is still PBKDF2-HMAC-SHA256 (`src/AppDatabase.cpp`). |
| P2-2 | Fixed | **Fixed** | Public AES and PBKDF2 still call Windows CNG (`BCryptOpenAlgorithmProvider`, `BCryptDeriveKeyPBKDF2` in `src/SchemeCrypto.cpp`). |
| P2-3 | Partial | **Partial** | `onDataReceived` still matches equality or `text.startsWith(entry.triggerSource)` (`src/GlobalTriggerManager.cpp`). Empty sources are still rejected in `fromJson`. One configured source still prefix-matches on the wire. |
| P2-4 | Fixed | **Fixed** | `~FlowExecutor` is `protected` (`include/FlowExecutor.h`). The body unregisters, disconnects, and clears owner slots. It does not wait and does not call `qFatal`. The token timeout path still calls `invokeOrderlyStopHook`, `markExecutionAbandoned`, and `deleteLater` on `finished` (`src/FlowExecutorToken.cpp`). No `qFatal(` under `src/` or `include/`. No production subclass of `FlowExecutor`. Residual unchanged: `protected` exists so the test type `StuckExecutor` can be defined. |
| P2-5 | Open | **Open** | English UI is still off. This pass did not re-run the i18n scanner. `tr(` sites in `MainWindow.cpp` stayed at 186. U-215 changed the Chinese confirm wording inside existing `tr()` calls. |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4351 lines, `FlowExecutor.cpp` 2086 lines, both up from r11. Halcon and OpenCV node types are both still in the tree. U-213 did not split either translation unit. |
| P2-7 | Fixed | **Fixed** | `docs/security-development-guide.md` still carries the template banner. |
| P2-8 | Fixed | **Fixed** | Repo root still has none of `需求.txt`, `requirements_decoded.txt`, or `diag-churn.txt`. `git ls-tree` of `docs/归档/` still lists all three. |
| P2-9 | Open | **Fixed** | Load path only. See section 3. The write path is P2-10, not a reason to keep this ID open. |
| P2-10 | — | **Open** | New. See section 4. |
| N1 | Fixed | **Fixed** | Same iteration check as P1-2. |
| N2 | Fixed | **Fixed** | `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | `constTimeEquals` is still the compare used for both MAC checks (`src/SchemeCrypto.cpp`, called from `src/SchemePackage.cpp`). |

## 3. Findings that moved

### P1-6 — alive-block `shared_ptr` publish is locked; tab switch still rebinds a running executor

| Field | Content |
| --- | --- |
| Severity | P1 |
| Status | **Partial** |
| Introduced | U-212 (`f0c0322`); lock half closed by U-213 (`2d2eae9`) |
| Paths | `src/FlowExecutor.cpp` (`setFlowScene`, `run`, `executeUpTo`, `executeFrom`, `invalidateDownstreamOf`, `collectSubFlowBodies`); `src/MainWindow.cpp` (`onCurrentTabChanged`, continuous auto-start) |

**What closed.** `setFlowScene` builds the new block, calls `bind` on it, then under `m_mutex` swaps the old `m_aliveBlock` out and moves the new one in. The old block’s `reset` is outside the mutex. `run` copies `m_aliveBlock` into `blockCopy` while `m_mutex` is held, unlocks, and passes `blockCopy` to `captureViaAliveBlock`. The three UI-side borrowers do the same locked copy. A search of `m_aliveBlock` under `src/` shows no other read of the executor member. Concurrent assignment and copy of that `shared_ptr` object is covered by `m_mutex`. The block mutex inside `CaptureTicket` is no longer being asked to do that job.

**What is still open.** `onCurrentTabChanged` returns early only when the **current** `m_executor` is Running or Paused. It then does `m_executor = executorForScene(scene)` and `setFlowScene(scene)` on the target. That target can already be inside `run`:

- After a load, every Continuous scene is started from a queued call, including scenes that are not tab 0 (`src/MainWindow.cpp`, the “载入方案后按每流程模式自启动” loop).
- A string or event trigger calls `requestExternalRound` on the flow named in the trigger, which need not be the visible tab (`src/GlobalTriggerManager.cpp`).

`setFlowScene` on that running executor still, under `m_graphCacheMutex`, clears `m_cachedSortedNodes`, `m_incoming`, `m_outgoing`, `m_loopBodyNodes`, `m_subFlowBodyNodes`, `m_subFlowMembers`, `m_subFlowIOs`, and `m_subFlowCallStack`.

The worker does not keep that mutex for the rest of the round. After releasing it, `run` calls `collectSubFlowBodies`, which clears and fills `m_subFlowBodyNodes`, `m_subFlowMembers`, and `m_subFlowIOs` with no lock, and the node loop calls `m_loopBodyNodes.contains` / `m_subFlowBodyNodes.contains` with no lock. `executeSubFlow` reads `m_subFlowIOs` and `m_subFlowCallStack` the same way. `invalidateDownstreamOf` already refuses this class of touch while Running or Paused, with a comment that a UI-thread clear races the worker. `setFlowScene` has no such guard.

U-213’s message records the tab path as unchanged on purpose. The per-scene executor comment in `onCurrentTabChanged` describes `setFlowScene(scene)` as switching to the scene’s own executor. The call is not a no-op on an executor that is already bound to that scene: it still clears those sets. The ownership regression leg checks that a production `setFlowScene` binds the block. It does not run a worker against a concurrent `setFlowScene`.

Recommended shape, same as r11’s second sentence: do not call `setFlowScene` on an executor whose state is Running or Paused. Same-scene refresh from a tab change can skip the cache clear, or the worker’s loop-body and subflow sets need the same mutex the clear already takes.

### P2-9 — failed load no longer rebases the live scheme directory

| Field | Content |
| --- | --- |
| Severity | P2 |
| Status | **Fixed** |
| Closed by | U-214 (`701846c`) |
| Paths | `src/ProjectManager.cpp` (`loadProject`, `rollbackSnapshot`, `setLastFilePath`) |

`loadProject` snapshots `m_lastFilePath`, `m_loadedReadonly`, and `m_lastIntegrityNote` before import. `rollbackSnapshot` writes all three back through `setLastFilePath`, so `ProjectPaths::setCurrentSchemeFilePath` follows. The failure returns that r11 listed now call it: envelope `!ok`, `integrityMissing`, envelope JSON parse failure, raw JSON parse failure, both `buildScenesFromJson` failures, and both confirm refusals. Failure to open the file returns before any of the three changes. Both success paths assign the new path, the readonly flag, and (for raw JSON) the integrity note only after `confirmOutsideSchemePaths` returns true.

`MainWindow::loadProjectFile` still loads into a temporary scene list and still logs “当前方案保持不变” on failure. That sentence now matches the process snapshot. Crash recovery still calls `loadProjectFile` and, after it returns, `setLastFilePath(info.originalPath)`, so a successful restore does not leave the snapshot on the temporary export.

The U-214 test leg loads a scheme with a relative path, then a v1 envelope, a bad passphrase, a confirm refusal, and a broken JSON file, and checks path, readonly flag, and note. This VM did not run it.

## 4. New finding

### P2-10 — save and encrypted export publish the scheme snapshot before the write commits

| Field | Content |
| --- | --- |
| Severity | P2 |
| Status | **Open** |
| Introduced | Older than r12. U-214 names it and leaves it. The load-side fix did not touch these two functions. |
| Paths | `src/ProjectManager.cpp` (`saveProject`, `exportEncryptedProject`, `saveProjectInteractive`); `src/ProjectPaths.cpp` (`setCurrentSchemeFilePath`, `resolveExecutionPath`) |

`saveProject`’s own comment says the write gate has to run before state changes, otherwise `lastFilePath` is updated and the readonly flag is cleared when the file was never written. The gate does return first. The I/O failures do not.

After the gate, `saveProject` calls `setLastFilePath(filePath)` and sets `m_loadedReadonly = false`, then builds JSON and writes with `QSaveFile`. `open`, short `write`, and failed `commit` each `return false` with the snapshot and the flag already updated. `QSaveFile` leaves the previous file bytes in place. `ProjectPaths` now resolves relative paths against the directory of the file that was not written. A flow that is already running keeps running (P0-1), so the next relative read uses that directory.

The readonly flag is the overwrite guard in `saveProjectInteractive` (refuse when the chosen path is the loaded readonly file). Clearing it before a successful commit means a failed Save As drops the guard. A later save of the original distributed file no longer matches `m_lastFilePath`, and the flag is already false.

`exportEncryptedProject` calls `setLastFilePath(filePath)` before `buildProjectJson` and before `commit`. The three write failures return false with the snapshot left on the export path. On success the snapshot stays there too. The dialog text is “导出”, and the function does not set `m_loadedReadonly` from the export’s readonly flag, so a successful export also retargets relative resolution at the export directory while the readonly guard still describes the previously loaded package.

`setLastFilePath` is the only funnel into `ProjectPaths::setCurrentSchemeFilePath`. `buildProjectJson` reads `m_lastFilePath` to fill `projectPath`. That is why the path is assigned early. The process snapshot does not have to move at the same time: the JSON field can be filled from the target path, and `setLastFilePath` / the readonly flag can commit only after `QSaveFile::commit` returns true. Every `return false` after the assignment should restore both, the same shape as `rollbackSnapshot` on the load side.

## 5. What was re-checked and did not move

- **Write gate / single-node run.** Dock execute and `recomputeDownstream` still go through `canRunFlow`. The context menu still uses the edit lock. A running flow still queues an external round before the gate is consulted.
- **Password hash.** Still PBKDF2-HMAC-SHA256 at 60000 rounds. Factory `admin` seed still present.
- **Trigger match.** Still prefix match on the wire, with the empty-source load gate.
- **Scheme crypto.** v2 HMAC, constant-time MAC compare, and the iteration equality check before key derivation are unchanged. v1 plaintext envelopes are still refused. Raw JSON still loads.
- **Token abandon.** Timeout still does not kill the process. No `qFatal` came back with U-213.
- **U-211 columns.** Not in this diff. No new finding on the identity body or the parameter core.
- **Stale refusal flags on a failed open.** `loadProject` clears `m_lastLoadRefusedForOutsidePaths` and `m_lastLoadRefusedForIntegrity` only after the file opens. A later open that fails at `QFile::open` leaves the previous attempt’s flag set, and `loadProjectFile` prefers that flag over the generic “file missing or corrupt” log. Pre-existing, narrow, and the “当前方案保持不变” half is still true. No new ID.
- **Comment drift.** `loadProject`’s raw-JSON comment still says crash recovery shares `applyProjectJson`. Recovery calls `loadProjectFile`. U-216’s erratum matches the calls.

## 6. Residuals and recommended next steps

In order:

1. **P1-6 remainder.** Skip `setFlowScene` when the target executor is Running or Paused, or stop clearing the loop-body and subflow sets from that call while a worker can be in the node loop. The `shared_ptr` mutex is already the right shape; do not reopen it.
2. **P2-10.** Commit `setLastFilePath` and `m_loadedReadonly` only after `QSaveFile::commit` succeeds, in both `saveProject` and `exportEncryptedProject`. Restore both on every failed write. Decide whether a successful export should retarget the live scheme at all; the dialog says export, and the readonly flag is not updated to match.
3. **P1-1 remainder, kept by U-216.** Compiled-in pepper, no author signature, raw JSON still loads, confirmer-true still loads an outside path, `applyProjectJson` stays ungated. Relative `..` is no longer in this list.
4. **P1-3 residual, kept by U-216.** UDP port 0 with an empty address still uses `AnyIPv4`.
5. **P2-4 residual.** Keep production free of `FlowExecutor` subclasses.
6. **Unchanged product choices.** Prefix match (P2-3), PBKDF2 at 60000 (P2-1), no English UI (P2-5), both vision stacks and the two large `.cpp` files (P2-6). A flow that is already running still keeps running when the write gate rises (P0-1).

## 7. Tests present, not executed here

Legs added or extended in this range, none of them run on this VM:

- `tests/flow_executor_ownership_test.cpp` — production `setFlowScene` binds the scene’s own block (U-213). No worker thread, no concurrent publisher.
- `tests/project_paths_test.cpp` — failed load restores path, readonly flag, and note for a v1 envelope, a bad passphrase, a confirm refusal, and broken JSON (U-214). Relative escape is classified, counted, and refused when the confirmer returns false (U-215). No save or export failure leg.

A Windows development machine that has Qt 6, HALCON, MVS, and MSVC should run `project_paths_test` and `flow_executor_ownership_test` after any further edit to P1-6 or P2-10. This review cannot say they pass. The authors’ “53/53” lines in the commit messages are not a result from this VM.

## 8. 给协调人（可转发）

- 审核对象：`main@ca98748`。相对 r11 基线 `f0c0322`：**4 个提交，11 个文件，+368 / −49**。
- 账本：**12 Fixed，5 Partial，2 Open，1 Withdrawn**。P2-9 从 Open 收到 Fixed。P1-6 从 Open 收到 Partial（存活块指针已加锁，切到正在跑的目标流程仍会 `setFlowScene` 并清执行缓存）。P1-1 仍是 Partial：相对 `..` 已进确认闸；pepper、无作者签名、裸 JSON、`applyProjectJson` 不设闸，按 U-216 维持。
- 新问题一条：**P2-10**（保存方案和加密导出在写成功之前就改了方案目录快照；保存还会提前清掉只读旗。U-214 自己把这条留到下一轮，代码里还在）。
- 此前已 Fixed 的条目没有回退。云端没跑 Windows CTest（缺 Qt6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

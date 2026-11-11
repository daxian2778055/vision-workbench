# Vision Workbench re-review — 2026-10-23 r13

Follow-up to the r1–r12 notes. Series filename stays `code-review-2026-10-18-r13.md`. Tree reviewed: `origin/main` at `4da6f83f11208a32a3371ea26668230c7ee76563` (“U-219 … 钉位缺口两处”). Range since r12’s `ca98748` is **3 commits, 9 files, +266 / −5**.

This pass did not rebuild or run CTest. The cloud VM cannot build or run the Windows suite (Qt 6, HALCON, MVS, MSVC). Nothing below is a test result. New test legs are cited only as the contract the authors wrote.

Runtime and product code were not modified. This note is the only change.

Prior draft: [PR #12](https://github.com/daxian2778055/vision-workbench/pull/12) (`docs/code-review-2026-10-18-r12.md`, baseline `ca98748`).

## 0. Executive summary

U-217, U-218, and U-219 match the code on the points that move the ledger. The tab-switch `setFlowScene` that r12 left open now returns before the graph-cache clear when the executor is already bound to that same scene. `saveProject` publishes the scheme anchor and clears the readonly flag only after `QSaveFile::commit` succeeds. `exportEncryptedProject` no longer moves the anchor at all. U-219 adds comments and two test slots; it does not change UDP bind behavior or put `applyProjectJson` behind the confirm gate.

No previously **Fixed** ID regressed. No new finding ID.

One claim in the U-218 message does not hold: `buildProjectJson` does write a `projectPath` field. See P2-10. Nothing in `src/` or `include/` reads it, so it does not keep that ID open.

| ID | Severity | Status | What |
| --- | --- | --- | --- |
| P1-6 | P1 | **Fixed** (was Partial) | Same-scene `setFlowScene` skips the cache clear. Production tab switch uses `executorForScene`, so the Running target r12 named no longer clears `m_loopBodyNodes` / `m_subFlow*` under the worker. The slow path still has no run-state guard. |
| P2-10 | P2 | **Fixed** (was Open) | `saveProject` calls `setLastFilePath` and clears `m_loadedReadonly` only after `commit`. Failed open / short write / `commit` leave both alone. Export does not call `setLastFilePath`. |

Ledger after this pass: **14 Fixed, 4 Partial, 1 Open, 1 Withdrawn**.

| Bucket | IDs |
| --- | --- |
| Fixed (14) | P0-1, P1-2, P1-3, P1-5, **P1-6**, P2-2, P2-4, P2-7, P2-8, P2-9, **P2-10**, N1, N2, N3 |
| Partial (4) | P1-1, P2-1, P2-3, P2-6 |
| Open (1) | P2-5 |
| Withdrawn (1) | P1-4 |

Moved since r12: **P1-6** Partial → Fixed. **P2-10** Open → Fixed. P1-1 and P1-3 stay where U-216 left them; U-219 pins those two residuals with tests and does not change them.

## 1. Delta since `ca98748`

| Item | Value |
| --- | --- |
| Baseline | `ca98748cf4f60346df658242265566a9a9e084c0` (r12) |
| Reviewed HEAD | `4da6f83f11208a32a3371ea26668230c7ee76563` |
| Commits | 3 |
| Files | 9 |
| Insertions / deletions | +266 / −5 |

Commits, oldest first:

| SHA | Subject (short) |
| --- | --- |
| `131a157` | U-217 same-scene `setFlowScene` skips the cache clear |
| `74b1f57` | U-218 save commits the anchor after `QSaveFile::commit`; export does not move it |
| `4da6f83` | U-219 pin UDP port 0 and ungated `applyProjectJson`; comments and tests |

`MainWindow.cpp` is **4351** lines (r12: 4351). `FlowExecutor.cpp` is **2123** lines (r12: 2086). `src/MainWindow.cpp` still has **186** `tr(` call sites. There is no `qFatal(` under `src/` or `include/`.

### Claim check (commit message versus code)

| Claim | Verdict |
| --- | --- |
| U-217 same-scene rebind skips disconnect, alive-block rebind, and the graph-cache clear | **Holds.** `setFlowScene` copies `m_scene` under `m_mutex` and, when `scene != nullptr` and the pointers match, refreshes node owner fields and `current()` / `currentContext`, then returns (`src/FlowExecutor.cpp`). `m_graphCacheClearCount` increments only inside the slow-path clear. `nullptr` still takes the slow path. |
| U-217 production cross-scene rebind of a Running executor is unreachable | **Holds for the six `MainWindow` call sites.** See section 3. The new ownership leg does not start a worker. |
| U-218 `saveProject` publishes the anchor and clears the readonly flag only after `commit` | **Holds.** The three failure returns sit above `setLastFilePath`. The write gate still returns first. |
| U-218 export never moves the anchor (explicit behavior change) | **Holds.** `exportEncryptedProject` has no `setLastFilePath` call. Success and failure both leave `m_lastFilePath`, `m_loadedReadonly`, and `ProjectPaths` alone. |
| U-218 “JSON has no `projectPath` field to fill” | **Does not hold.** `buildProjectJson` writes `projectPath` from `m_lastFilePath` when that string is non-empty (`src/ProjectManager.cpp`). No reader under `src/` or `include/`. Detail under P2-10. |
| U-219 behavior unchanged; two pins only | **Holds.** The `src/` diff is one comment in `UdpCommNode::openConnection` and one comment in `applyProjectJson`. The UDP refuse (`Missing` and `localPort > 0`) and the port-0 `AnyIPv4` bind are the same statements as r12. `applyProjectJson` still calls `buildScenesFromJson` then `applyGlobalsFromJson` and does not call `confirmOutsideSchemePaths`. |

## 2. Ledger

| ID | r12 | r13 | Evidence on `4da6f83` |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `executeNodeOnce` and `recomputeDownstream` still return when `canRunFlow()` is false (`src/MainWindow.cpp`). `canRunFlow()` is still `isLoggedIn() && !writesBlocked()` (`src/SessionManager.cpp`). Context menu “仅执行此算子” still calls `node->execute()` and is disabled while `isEditLocked()` (`src/NodeGraphicsItem.cpp`). `canvasEditShouldBeLocked` is still the run-lock OR the write gate (`include/SessionManager.h`). “执行到此处” / “从此处执行” still do not call `canRunFlow()` themselves; the same edit lock disables those menu items. A flow already Running or Paused still queues in `requestExternalRound` before the idle-path `writesBlocked()` check (`src/FlowExecutor.cpp`). The `admin` / `createPasswordHash("admin")` seed is still inserted when `users` is empty (`src/AppDatabase.cpp`). |
| P1-1 | Partial | **Partial** | Unchanged product choices, now with a direct test on one of them. v2 plaintext HMAC and `kPlainPepper` are unchanged (`src/SchemePackage.cpp`). v1 envelopes with `integrityMissing` are still refused. Absolute paths outside the scheme directory still need a confirmer. Relative `..` still returns `EscapedRelative` and shares that gate (`src/ProjectPaths.cpp`, `src/ProjectManager.cpp`). Still open inside this ID, and U-216 still records each as a kept choice: the MAC key is the compiled-in pepper; there is no author signature; a confirmer that returns true still loads the outside path; raw JSON still loads and sets `m_lastIntegrityNote` to `旧明文 JSON 格式`; `applyProjectJson` still applies scenes and globals with no path gate. U-219’s new leg asserts that last sentence: a refusing confirmer is not called, and `ProjectPaths::currentSchemeFilePath()` stays empty (`tests/project_paths_test.cpp`). |
| P1-2 | Fixed | **Fixed** | Encrypted import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Fixed | **Fixed** | `CommBind::resolveListenAddress` still maps empty or whitespace to `BindKind::Missing` and does not store an address (`include/CommunicationNodeBase.h`). TCP server, UDP with `localPort > 0`, and Modbus server still refuse `Missing` and `Invalid`. Explicit `0.0.0.0` remains a legal every-interface literal. Residual, kept by U-216 and now pinned by U-219: UDP `localPort == 0` with a missing address still binds `QHostAddress::AnyIPv4` (`src/UdpCommNode.cpp`). The default local port is 8000, which takes the refuse path. `testUdpBindGatePairing` expects both halves. This VM did not run it. |
| P1-4 | Withdrawn | **Withdrawn** | Still withdrawn. `WriteFileNode::run` still clears `m_outputImage` when `cv::imwrite` fails (`src/WriteFileNode.cpp`). `HalconNode::process` still sets failure when `requiresImageOutput()` and the image is not initialized (`src/HalconNode.cpp`, the “成功必须有产出” check). |
| P1-5 | Fixed | **Fixed** | `README.md` still names the Qt 6 / HALCON / OpenCV stack and `tools/ci.ps1`. |
| P1-6 | Partial | **Fixed** | Same-scene fast path closes the cache-clear race r12 named. See section 3. |
| P2-1 | Partial | **Partial** | `kHashIterations` is still 60000. The family is still PBKDF2-HMAC-SHA256 (`src/AppDatabase.cpp`). |
| P2-2 | Fixed | **Fixed** | Public AES and PBKDF2 still call Windows CNG (`BCryptOpenAlgorithmProvider`, `BCryptDeriveKeyPBKDF2` in `src/SchemeCrypto.cpp`). |
| P2-3 | Partial | **Partial** | `onDataReceived` still matches equality or `text.startsWith(entry.triggerSource)` (`src/GlobalTriggerManager.cpp`). Empty sources are still rejected in `fromJson`. One configured source still prefix-matches on the wire. |
| P2-4 | Fixed | **Fixed** | `~FlowExecutor` is `protected` (`include/FlowExecutor.h`). The body unregisters, disconnects, and clears owner slots. It does not wait and does not call `qFatal`. The token timeout path still calls `invokeOrderlyStopHook`, `markExecutionAbandoned`, and `deleteLater` on `finished` (`src/FlowExecutorToken.cpp`). No `qFatal(` under `src/` or `include/`. No production subclass of `FlowExecutor`. Residual unchanged: `protected` exists so the test type `StuckExecutor` can be defined (`tests/integration_test.cpp`). |
| P2-5 | Open | **Open** | English UI is still off. This pass did not re-run the i18n scanner. `tr(` sites in `MainWindow.cpp` stayed at 186. U-217–U-219 did not edit that file. |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4351 lines (unchanged). `FlowExecutor.cpp` 2123 lines (r12: 2086). Halcon and OpenCV node types are both still in the tree. U-217 added the fast path inside `FlowExecutor.cpp`; it did not split the file. |
| P2-7 | Fixed | **Fixed** | `docs/security-development-guide.md` still carries the template banner. |
| P2-8 | Fixed | **Fixed** | Repo root still has none of `需求.txt`, `requirements_decoded.txt`, or `diag-churn.txt`. All three are still under `docs/归档/`. |
| P2-9 | Fixed | **Fixed** | `loadProject` still snapshots path, readonly flag, and integrity note, and the failure returns after a successful open still call `rollbackSnapshot()`. Open failure still returns before any of the three is touched. U-218 did not edit that function. The write path is P2-10 and is now closed on its own. |
| P2-10 | Open | **Fixed** | Save commits the anchor after `commit`. Export does not move it. See section 3. |
| N1 | Fixed | **Fixed** | Same iteration check as P1-2. |
| N2 | Fixed | **Fixed** | `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | `constTimeEquals` is still the compare used for both MAC checks (`src/SchemeCrypto.cpp`, called from `src/SchemePackage.cpp`). |

## 3. Findings that moved

### P1-6 — same-scene tab rebind no longer clears the graph cache

| Field | Content |
| --- | --- |
| Severity | P1 |
| Status | **Fixed** |
| Closed by | U-217 (`131a157`), on top of the U-213 mutex publish |
| Paths | `src/FlowExecutor.cpp` (`setFlowScene`, `run`, `collectSubFlowBodies`); `src/MainWindow.cpp` (`onCurrentTabChanged`, `executorForScene`, load / new / close) |

**What r12 left open.** `onCurrentTabChanged` returns early only when the current executor is Running or Paused, then calls `setFlowScene` on the target. A non-visible Continuous scene can already be inside `run` (the queued auto-start after load). That call used to clear `m_cachedSortedNodes`, `m_incoming`, `m_outgoing`, `m_loopBodyNodes`, `m_subFlowBodyNodes`, `m_subFlowMembers`, `m_subFlowIOs`, and `m_subFlowCallStack` under `m_graphCacheMutex`. After `run` drops that mutex, `collectSubFlowBodies` and the node loop read and write those containers with no lock.

**What closed.** `setFlowScene` now reads `m_scene` under `m_mutex`. If the argument is that same non-null scene, it does not disconnect, does not swap `m_aliveBlock`, and does not enter the clear. The clear, and the `m_graphCacheClearCount` increment, stay on the slow path. `nullptr` still takes the slow path, so unbind still drops the previous scene’s cache.

`onCurrentTabChanged` still does not look at the target’s state. It still assigns `m_executor = executorForScene(scene)` and calls `setFlowScene(scene)`. That is enough, because `executorForScene` returns the executor already stored under that scene key. After the first bind, `m_scene` is that scene, so the fast path runs. The first bind is a slow path on an executor whose `flowScene()` is null: a new executor, or the spare reused only when `flowScene() == nullptr`.

The other production `setFlowScene` calls do not hand a different scene to a live worker:

- New project and load call `setFlowScene(nullptr)` only after `stopExecution` and a shared 15s `wait`. If any thread is still running, the function returns before the unbind and does not delete scenes.
- Close-tab calls `setFlowScene(nullptr)` only after `stopExecution` and `wait(6000)` succeed, or when `isRunning()` is already false. A failed wait returns before the unbind.
- The load and new-flow binds that follow are first binds (`m_scene == nullptr`) on an executor that just came back from that wait, or on a freshly created one.

`invalidateDownstreamOf` still refuses the same class of clear while Running or Paused. The `m_aliveBlock` publish from U-213 is unchanged: the slow path still swaps the `shared_ptr` under `m_mutex`, and `run` still copies it before unlock.

**Residual, not enough to stay Partial.** The slow path still has no Running/Paused check. `run` still uses the five subflow/loop containers outside `m_graphCacheMutex` after the round-start lock (`collectSubFlowBodies`, `m_loopBodyNodes.contains`, `m_subFlowBodyNodes.contains`, `executeSubFlow`). A caller outside `MainWindow` that passes a different scene to an executor already inside `run` would reopen the race. The new leg `testSameSceneRebindSkipsCacheChurn` checks the clear counter, late-node ownership, and `current()`. It does not start a worker.

The fast path still assigns each node’s `m_ownerContext` (`std::shared_ptr`) and `m_ownerToken` (`std::weak_ptr`) on the UI thread. `executeNode` and `rebuildIncomingIndex` assign the same members on the worker, and `DelayNode` / `ScriptNode` / `SubFlowNode` / `MvsImageSourceNode` copy `ownerContext()` during a round. That unlocked assign is older than r13 (the slow path did it too) and is the half U-217 kept so `current()` and late-node ownership still update. It is not a new ID.

### P2-10 — save and export no longer publish the scheme snapshot before the write commits

| Field | Content |
| --- | --- |
| Severity | P2 |
| Status | **Fixed** |
| Closed by | U-218 (`74b1f57`) |
| Paths | `src/ProjectManager.cpp` (`saveProject`, `exportEncryptedProject`, `buildProjectJson`, `setLastFilePath`) |

`saveProject` still returns on the write gate before any state change. It then builds JSON and writes with `QSaveFile`. `open` failure, short `write` (`cancelWriting`), and failed `commit` each `return false` with `m_lastFilePath` and `m_loadedReadonly` untouched. Only the success tail calls `setLastFilePath(filePath)` and sets `m_loadedReadonly = false`. `setLastFilePath` is still the only funnel into `ProjectPaths::setCurrentSchemeFilePath`.

That closes the two failure effects r12 named. A failed Save As no longer aims relative resolution at a file that was not written, so a flow that is already running (P0-1) keeps the previous directory. The readonly flag stays set, so `saveProjectInteractive` still refuses a later overwrite of the loaded readonly file.

`exportEncryptedProject` no longer calls `setLastFilePath`. A failed export leaves the anchor and the flag alone. A successful export leaves them alone too. That is the behavior change the commit message names: previously a successful export retargeted relative resolution at the export directory while the readonly flag still described the loaded package. The dialog text is still “导出”. A running flow keeps the live scheme directory across the export. Loading the exported file later goes through `loadProject` and its confirm gate.

The U-218 test leg covers a successful first save, a missing-directory failure, a directory-as-target failure, a successful export, a failed export, a failed Save As while readonly, and a successful Save As that does move the anchor and clear the flag. This VM did not run it.

**Claim miss, not a reason to stay Open.** The U-218 message says the JSON has no `projectPath` field. `buildProjectJson` writes one when `m_lastFilePath` is non-empty (`src/ProjectManager.cpp`, the `projectPath` assignment).

U-218 moved `setLastFilePath` to after `buildProjectJson`, so the field now records the anchor from before this save, or is omitted when that anchor is empty (first save of a never-saved scheme). Before U-218 the early `setLastFilePath` made the field equal the target path. `exportEncryptedProject` uses the same `buildProjectJson`, so an export now stamps the live scheme path rather than the export path. A search of `src/` and `include/` finds no reader. Crash recovery passes `lastFilePath()` into `RecoveryStore` as its own argument (`src/MainWindow.cpp`, `autoSaveTick`); it does not read this field back. Node paths are stored as whatever string was already in the parameter; save does not re-relativize them against the target directory either before or after U-218. The live `ProjectPaths` snapshot, which is what a running flow resolves against, is the defect r12 filed, and that snapshot now moves only after a successful save.

## 4. New findings

None. U-219’s two pins restate residuals that already sit inside P1-1 and P1-3. The `projectPath` stamp drift is recorded under P2-10 and has no consumer in this tree.

## 5. What was re-checked and did not move

- **Write gate / single-node run.** Dock execute and `recomputeDownstream` still go through `canRunFlow`. The context menu still uses the edit lock. A running flow still queues an external round before the gate is consulted. `requestExternalRound` still returns false for a mode other than software trigger, so a string trigger does not itself start a Continuous scene; the Running target on a background tab is still the post-load Continuous auto-start.
- **Password hash.** Still PBKDF2-HMAC-SHA256 at 60000 rounds. Factory `admin` seed still present.
- **Trigger match.** Still prefix match on the wire, with the empty-source load gate.
- **Scheme crypto.** v2 HMAC, constant-time MAC compare, and the iteration equality check before key derivation are unchanged. v1 plaintext envelopes are still refused. Raw JSON still loads.
- **Token abandon.** Timeout still does not kill the process. No `qFatal` came back with U-217.
- **U-211 columns.** Not in this diff. No new finding on the identity body or the parameter core.
- **Comment drift.** `loadProject`’s raw-JSON comment still says crash recovery shares `applyProjectJson`. Recovery calls `loadProjectFile`. U-216’s erratum still matches the calls. U-219’s new comment on `applyProjectJson` itself is accurate.

## 6. Residuals and recommended next steps

In order:

1. **P1-6 residual.** The slow path of `setFlowScene` still clears the loop-body and subflow sets with no run-state check, and the worker still touches those containers without `m_graphCacheMutex` after the round-start lock. Leave it until a caller can pass a different scene to a live executor. Do not reopen the `m_aliveBlock` mutex.
2. **P2-10 stamp.** If anything outside this repo reads `projectPath`, fill it from the target path inside `buildProjectJson` (or at the success tail) without moving `setLastFilePath` back above `commit`. In-tree, nothing reads it.
3. **P1-1 remainder, kept by U-216 and pinned by U-219.** Compiled-in pepper, no author signature, raw JSON still loads, confirmer-true still loads an outside path, `applyProjectJson` stays ungated. Relative `..` is not in this list.
4. **P1-3 residual, kept by U-216 and pinned by U-219.** UDP port 0 with an empty address still uses `AnyIPv4`.
5. **P2-4 residual.** Keep production free of `FlowExecutor` subclasses.
6. **Unchanged product choices.** Prefix match (P2-3), PBKDF2 at 60000 (P2-1), no English UI (P2-5), both vision stacks and the two large `.cpp` files (P2-6; `FlowExecutor.cpp` grew by 37 lines). A flow that is already running still keeps running when the write gate rises (P0-1).

## 7. Tests present, not executed here

Legs added or extended in this range, none of them run on this VM:

- `tests/flow_executor_ownership_test.cpp` — `testSameSceneRebindSkipsCacheChurn` (U-217). Clear counter, late-node owner, `current()`. No worker thread.
- `tests/project_paths_test.cpp` — `testFailedSaveRestoresAnchorAndReadonly` (U-218) and `testApplyProjectJsonStaysUngated` (U-219).
- `tests/comm_writeback_test.cpp` — `testUdpBindGatePairing` (U-219). Receive-side empty address with a real local port must fail with “绑定地址未配置”; port 0 with an empty address must open.

A Windows development machine that has Qt 6, HALCON, MVS, and MSVC should run `flow_executor_ownership_test`, `project_paths_test`, and `comm_writeback_test` against this tree. This review cannot say they pass. The authors’ “53/53” lines in the commit messages are not a result from this VM.

## 8. 给协调人（可转发）

- 审核对象：`main@4da6f83`。相对 r12 基线 `ca98748`：**3 个提交，9 个文件，+266 / −5**。
- 账本：**14 Fixed，4 Partial，1 Open，1 Withdrawn**。P1-6 从 Partial 收到 Fixed（切到已绑定的同一场景不再清图缓存；慢路径仍无运行态守卫，产线六处调用喂不到正在跑的跨场景换绑）。P2-10 从 Open 收到 Fixed（保存成功才改锚、才清只读旗；导出全程不动锚）。
- 没有新编号。U-218 写的「JSON 没有 projectPath 字段」与代码不符：`buildProjectJson` 仍会写入该字段，另存为/导出时写进去的是保存前的旧锚；仓库内没有读者，所以不单列、也不把 P2-10 留在 Open。
- U-219 只加注释和两条测试，行为与 U-216 的维持项一致（UDP 端口 0 仍放行，`applyProjectJson` 仍不进确认闸）。此前已 Fixed 的条目没有回退。
- 云端没跑 Windows CTest（缺 Qt6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

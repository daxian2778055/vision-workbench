# Vision Workbench re-review — 2026-10-18 r11

Follow-up to the r1–r10 notes. Tree reviewed: `origin/main` at `f0c032240161ec2c4d5d10c4eb2eb8b01e6a0988` (“U-212 … 执行器与场景之间所有权收进存活控制块”). Range since r10’s `6c974b7` is **13 commits, 53 files, +1387 / −345**.

This pass did not rebuild or run CTest. The cloud VM cannot build or run the Windows suite (Qt 6, HALCON, MVS, MSVC). Nothing below is a test result. The new test legs are cited only as the contract the authors wrote, not as a green run.

Runtime and product code were not modified. This note is the only change.

Prior draft: [PR #10](https://github.com/daxian2778055/vision-workbench/pull/10) (`docs/code-review-2026-10-18-r10.md`, baseline `6c974b7`).

## 0. Executive summary

The U-201 … U-204 behavior changes match the code, not only the commit messages. v1 plaintext envelopes are refused at `loadProject`. An absolute path outside the scheme directory is refused unless an injected confirmer returns true (no confirmer means refuse). Empty `bindAddress` is `Missing` and the TCP, UDP-listen, and Modbus-server start paths refuse it. `~FlowExecutor` no longer calls `qFatal`; the destructor is `protected` and the token timeout path is still the soft-fail path. The three root tombstones now live under `docs/归档/`. U-206 adds a regression test for the already-shipped write-gate rule and does not change that rule.

Two new findings, neither of which re-opens a previously Fixed ID:

| ID | Severity | Status | What |
| --- | --- | --- | --- |
| P1-6 | P1 | **Open** | U-212 publishes `FlowExecutor::m_aliveBlock` (`std::shared_ptr`) off the mutex that used to cover `m_scene`. The worker copies it every round. `std::shared_ptr` assignment concurrent with a copy is a data race. Tab switch calls `setFlowScene` on the target executor without checking that executor’s run state. |
| P2-9 | P2 | **Open** | `loadProject` moves the process scheme-directory snapshot (and clears the readonly flag) before the load is known to succeed. The previous path is restored only when the outside-path confirmer refuses. The new v1 rejection, a bad passphrase, and a bad JSON file all return with the snapshot left on the rejected file. The UI text says the current scheme is unchanged. |

No previously **Fixed** ID regressed. P1-1 stays **Partial**: the two r10 leftovers that were “v1 still loads” and “outside absolute path is only a warning” are narrowed, and the pepper, the missing author signature, bare JSON, and relative `..` remain.

Ledger after this pass: **11 Fixed, 4 Partial, 3 Open, 1 Withdrawn**.

| Bucket | IDs |
| --- | --- |
| Fixed (11) | P0-1, P1-2, **P1-3**, P1-5, P2-2, **P2-4**, P2-7, **P2-8**, N1, N2, N3 |
| Partial (4) | P1-1, P2-1, P2-3, P2-6 |
| Open (3) | P2-5, **P1-6**, **P2-9** |
| Withdrawn (1) | P1-4 |

Moved since r10: **P1-3, P2-4, P2-8** from Partial to Fixed. P1-1, P2-1, P2-3, P2-6 stay Partial. P2-5 stays Open. P0-1 and the other Fixed IDs stay Fixed.

## 1. Delta since `6c974b7`

| Item | Value |
| --- | --- |
| Baseline | `6c974b7` (r10) |
| Reviewed HEAD | `f0c032240161ec2c4d5d10c4eb2eb8b01e6a0988` |
| Commits | 13 |
| Files | 53 |
| Insertions / deletions | +1387 / −345 |

Commits, oldest first:

| SHA | Subject (short) |
| --- | --- |
| `ac944e6` | U-201 outside-scheme absolute paths need an explicit confirm; no confirmer means refuse |
| `0ea63d3` | U-201 ledger-number erratum only |
| `620b091` | U-202 empty `bindAddress` is missing, not every interface |
| `3a73329` | U-203 destructor `protected`; the 5s+15s `qFatal` path removed |
| `75c0e49` | U-204 v1 plaintext envelopes refused at the real load entry |
| `c055be0` | U-205 three root tombstones moved to `docs/归档/` |
| `8be9a4c` | U-206 write-gate regression test; behavior unchanged |
| `8062fba` | U-207 ledger / design-doc status text only |
| `4d601ae` | U-208 operator docs aligned with U-201 / U-202 / U-204 |
| `9148555` | U-209 todo-list bookkeeping only |
| `95852f8` | U-210 ownership design notes only |
| `258c123` | U-211 `NodeStateBody` + `ThreadSafeParams::Core` on the graph snapshot |
| `f0c0322` | U-212 `FlowSceneAliveBlock` between executor and scene |

Code that actually changed behavior or structure sits in `ProjectManager`, `ProjectPaths` (comment only), `MainWindow` (confirm dialog), `CommunicationNodeBase` / TCP / UDP / Modbus / `CommunicationManager`, `FlowExecutor` / `FlowExecutorToken` / `FlowScene` / new `FlowSceneAliveBlock`, `NodeBase` / `NodeStateBody` / `ThreadSafeParams` / `HalconNode`, and the tests named in section 6. The other commits are docs.

`MainWindow.cpp` is **4348** lines (r10: 4308). `FlowExecutor.cpp` is **2052** lines (r10: 2033). `src/MainWindow.cpp` has **186** `tr(` call sites (r10: 182). There is no `qFatal(` under `src/` or `include/`.

### Claim check (commit message versus code)

| Claim | Verdict |
| --- | --- |
| U-201 fail-closed confirm for absolute paths outside the scheme directory | **Holds** for `loadProject`. Confirmer absent or returning false deletes the temporary scenes, restores `m_lastFilePath`, and does not apply global config. `applyProjectJson` is explicitly not gated. A relative path, including one with `..`, never asks. |
| U-202 empty bind is no longer Any | **Holds** for the resolver and for TCP listen, UDP with `localPort > 0`, and Modbus server. Explicit `0.0.0.0` / `::` still means every interface. UDP `localPort == 0` and a missing address still binds `QHostAddress::AnyIPv4` (`src/UdpCommNode.cpp`). |
| U-203 protected destructor, fatal teardown removed | **Holds.** `~FlowExecutor()` (`src/FlowExecutor.cpp`) unregisters, disconnects, and clears owner slots. It does not wait and does not call `qFatal`. The token still joins, and on timeout calls `markExecutionAbandoned` plus `deleteLater` on `finished`. |
| U-204 v1 plaintext packages rejected | **Holds** for an envelope with `integrityMissing`. A raw JSON `.vfp` still loads and sets the note `旧明文 JSON 格式`. |
| U-205 tombstones archived | **Holds.** Repo root no longer has `需求.txt`, `requirements_decoded.txt`, or `diag-churn.txt`. All three are tracked under `docs/归档/` with unchanged bytes. |
| U-206 write-gate contract test, no behavior change | **Holds.** `requestExternalRound` still queues when the state is already Running or Paused, and only then checks `writesBlocked()` on the idle path. |
| U-211 snapshot shares identity body and param core | **Reviewed, no new ID.** Columns are `shared_ptr`s to the live objects, aligned with the node list. The param core still has `ThreadSafeParams`’ lock. The identity body is unlocked; the commit says that matches the old naked fields. |
| U-212 alive block, “behavior unchanged” | **The tombstone handshake is in the code. The “unchanged” claim is not, for the publish of the block.** See P1-6. `CaptureTicket` holds a `QMutex` (exclusive, despite the “read lock” comments) across `subFlows()` and `captureGraphSnapshot()`. `markGone` is the first act of `~FlowScene`. That serializes capture against destruction. It does not cover the rest of the round; destroying a scene while a snapshot guard is still held still tears the scene down after a warning. That contract is the pre-existing one. |

## 2. Ledger

| ID | r10 | r11 | Evidence on `f0c0322` |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `executeNodeOnce` and `recomputeDownstream` still return when `canRunFlow()` is false (`src/MainWindow.cpp`). `canRunFlow()` is still `isLoggedIn() && !writesBlocked()` (`src/SessionManager.cpp`). Context menu “仅执行此算子” still calls `node->execute()` and is disabled while `isEditLocked()` (`src/NodeGraphicsItem.cpp`). `canvasEditShouldBeLocked` is still the run-lock OR the write gate. “执行到此处” / “从此处执行” (`MainWindow` handlers for `executeToHereRequested` / `executeFromHereRequested`) still do not call `canRunFlow()` themselves; the same edit lock disables those menu items. A flow already Running or Paused still queues in `requestExternalRound` before the gate (`src/FlowExecutor.cpp`). U-206’s `testWriteGateDoesNotInterruptRunningFlow` pins that rule; it does not change it. The `admin` / `createPasswordHash("admin")` seed is still inserted when `users` is empty (`src/AppDatabase.cpp`). |
| P1-1 | Partial | **Partial** | Narrowed, not closed. v2 plaintext HMAC and `kPlainPepper` are unchanged (`src/SchemePackage.cpp`). v1 plaintext envelopes are now refused (`ProjectManager::loadProject` when `integrityMissing`). Absolute paths outside the scheme directory go through `confirmOutsideSchemePaths`; no confirmer means refuse (`src/ProjectManager.cpp`, `src/MainWindow.cpp`). Still open inside this ID: the MAC key is the compiled-in pepper; there is no author signature; a confirmer that returns true still loads the outside path; raw JSON still loads with `m_lastIntegrityNote = 旧明文 JSON 格式` and the window still logs that note; `classifySchemePath` returns `RelativeRaw` for any relative string and does not look at the resolved path, so `..` never raises the confirm dialog (`src/ProjectPaths.cpp`); `applyProjectJson` applies scenes and globals with no path gate. |
| P1-2 | Fixed | **Fixed** | Unchanged. Encrypted import still returns before `deriveKeys` unless the iteration field is exactly `kIterationsValid` (`src/SchemePackage.cpp`). |
| P1-3 | Partial | **Fixed** | `CommBind::resolveListenAddress` maps empty or whitespace to `BindKind::Missing` and does not store an address (`include/CommunicationNodeBase.h`). TCP server, UDP with `localPort > 0`, and Modbus server each emit an error and return false on `Missing` and on `Invalid`. `CommunicationManager::createDeviceNode` now forwards `bindAddress` for TCP, UDP, and Modbus. Explicit `0.0.0.0` remains a legal every-interface literal. Residual, not enough to keep the ID open: UDP `localPort == 0` with a missing address still binds `QHostAddress::AnyIPv4` (`src/UdpCommNode.cpp`). That path is the documented send-only source selection; the default local port is 8000, which takes the refuse path. |
| P1-4 | Withdrawn | **Withdrawn** | Unchanged. Not reopened. |
| P1-5 | Fixed | **Fixed** | Unchanged in this range. `README.md` still names the Qt 6 / HALCON / OpenCV stack and `tools/ci.ps1`. |
| P2-1 | Partial | **Partial** | Not in this diff. `kHashIterations` is still 60000. The family is still PBKDF2-HMAC-SHA256 (`src/AppDatabase.cpp`). |
| P2-2 | Fixed | **Fixed** | Unchanged. Public AES and PBKDF2 still call Windows CNG (`src/SchemeCrypto.cpp`). |
| P2-3 | Partial | **Partial** | Not in this diff. `onDataReceived` still matches equality or `text.startsWith(entry.triggerSource)` (`src/GlobalTriggerManager.cpp`). Empty sources and mutual-prefix pairs are still rejected in `fromJson`. One configured source still prefix-matches on the wire. |
| P2-4 | Partial | **Fixed** | The r10 remainder was the destructor `qFatal` after the two waits. That block is gone. `~FlowExecutor` is `protected` (`include/FlowExecutor.h`) and the constructor no longer takes a parent. The only `friend` is `FlowExecutorToken`. The token’s timeout path calls `invokeOrderlyStopHook`, `markExecutionAbandoned`, and `deleteLater` on `finished` (`src/FlowExecutorToken.cpp`). No `qFatal(` remains under `src/` or `include/`. Residual: the destructor is protected rather than private so the test type `StuckExecutor` (`tests/integration_test.cpp`) can exist; a production subclass is not present. A subclass that deletes a running executor no longer aborts the process, because the wait-and-`qFatal` net was removed on purpose. |
| P2-5 | Open | **Open** | English UI is still off. This pass did not re-run the i18n scanner. `tr(` sites in `MainWindow.cpp` went from 182 to 186 because of the new confirm dialog. The new operator strings are Chinese. |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4348 lines, `FlowExecutor.cpp` 2052 lines, both up from r10. Halcon and OpenCV node types are both still in the tree. U-211 / U-212 add `NodeStateBody`, a params core, and `FlowSceneAliveBlock`; they do not split the two large translation units. |
| P2-7 | Fixed | **Fixed** | Unchanged. `docs/security-development-guide.md` still carries the template banner. U-208 appended the bind-address and “do not ship an unauthenticated package” lines; it did not remove the banner. |
| P2-8 | Partial | **Fixed** | The three tracked tombstones are no longer at the repo root. `git ls-tree` of `docs/归档/` lists `需求.txt`, `requirements_decoded.txt`, and `diag-churn.txt`. The move keeps the bytes. That is the “archive out of the repo root” half of the r10 recommendation. |
| N1 | Fixed | **Fixed** | Unchanged. Same iteration check as P1-2. |
| N2 | Fixed | **Fixed** | Unchanged in this range. `docs/ci-cd-guide.md` still carries the blueprint banner. |
| N3 | Fixed | **Fixed** | Unchanged. `constTimeEquals` is still the compare used for both MAC checks (`src/SchemeCrypto.cpp`, called from `src/SchemePackage.cpp`). |
| P1-6 | — | **Open** | New. See section 3. |
| P2-9 | — | **Open** | New. See section 3. |

## 3. New findings

### P1-6 — alive-block `shared_ptr` is published without the mutex that covered `m_scene`

| Field | Content |
| --- | --- |
| Severity | P1 |
| Status | **Open** |
| Introduced | U-212 (`f0c0322`) |
| Paths | `src/FlowExecutor.cpp` (`setFlowScene`, `run`, `captureViaAliveBlock`); `src/MainWindow.cpp` (`onCurrentTabChanged`) |

On `6c974b7`, `FlowExecutor::run` copied `m_scene` under `m_mutex` and then captured the graph. On `f0c0322` the worker unlocks `m_mutex` and then calls `captureViaAliveBlock(m_aliveBlock)`. That helper copy-constructs the `std::shared_ptr` (`owned = block`) with no lock.

`setFlowScene` writes `m_scene` under `m_mutex`, then assigns `m_aliveBlock` **outside** that mutex, then calls `bind`. A `std::shared_ptr` member is not safe to assign on one thread and copy on another. The comment in `captureViaAliveBlock` says the copy is an atomic increment. The atomic part is the control-block count, and only when the `shared_ptr` object itself is not being mutated. Concurrent assignment and copy of the same `shared_ptr` is a data race.

The window is reachable. `onCurrentTabChanged` refuses the switch only when the **current** executor is Running or Paused. It then does `m_executor = executorForScene(scene)` and `setFlowScene(scene)` on the target. Another flow’s executor can already be inside `run`. Open-project and new-project wait for executors before `setFlowScene(nullptr)`; the tab path does not.

`executeUpTo`, `executeFrom`, and `invalidateDownstreamOf` also read `m_aliveBlock`. Those three return immediately when the executor is Running or Paused, and they run on the UI thread, so they do not add a second racer beside `setFlowScene`.

The block’s own mutex is not a substitute. `CaptureTicket` locks it only after the `shared_ptr` has already been copied. A torn `shared_ptr` never gets that far safely.

This is a regression relative to the r10 locking of `m_scene`. It is not a re-open of P2-4. The recommended shape is the one the file already uses for `s_currentContext`: publish and copy the `shared_ptr` under one mutex, or use `std::atomic<std::shared_ptr<...>>` / `std::atomic_load` and `std::atomic_store`. `onCurrentTabChanged` should not call `setFlowScene` on an executor that is Running or Paused.

### P2-9 — a failed load still rebases the live scheme directory

| Field | Content |
| --- | --- |
| Severity | P2 |
| Status | **Open** |
| Introduced | The early `setLastFilePath` is older than r11. U-201 documented the invariant and restored the path on one return. U-204 added another return that does not restore it. |
| Paths | `src/ProjectManager.cpp` (`loadProject`, `setLastFilePath`); `src/ProjectPaths.cpp` (`setCurrentSchemeFilePath`, `resolveExecutionPath`); `src/MainWindow.cpp` (the “当前方案保持不变” messages) |

`loadProject` calls `setLastFilePath(filePath)` and sets `m_loadedReadonly = false` before import, parse, or confirm. `setLastFilePath` also calls `ProjectPaths::setCurrentSchemeFilePath`, which is what `resolveExecutionPath` uses for relative image and file paths.

The previous path is written back only inside the two `confirmOutsideSchemePaths` failure branches. These returns leave the snapshot on the file that did not load:

- envelope decrypt / MAC failure
- `integrityMissing` (the U-204 v1 refusal)
- envelope JSON parse failure
- raw JSON parse failure

`m_loadedReadonly` is not restored on any of those returns, including the confirm-refusal returns that do restore the path. `saveProjectInteractive` uses that flag to refuse overwriting a readonly package.

The window then tells the operator the current scheme was kept (`src/MainWindow.cpp`, the four “当前方案保持不变” logs). The scenes are kept. The directory used to resolve relative paths, and the readonly flag, are not. A flow that is already running is left running (P0-1, still the recorded rule), so the next relative read on that flow uses the rejected file’s directory.

U-201’s own comment states this failure mode: if the current scheme did not change but the base directory now points at the rejected file, relative paths resolve in the wrong directory. The confirm path was fixed. The new integrity refusal was not.

Recommended shape: treat path snapshot and readonly flag as commit-on-success. Every `return false` restores both. The confirm path should restore the readonly flag as well as the path.

## 4. What was re-checked and did not move

- **Write gate / single-node run.** The dock button and `recomputeDownstream` still go through `canRunFlow`. The context menu still uses the edit lock, which the write gate turns on. U-206 did not move the gate to “stop the line”.
- **Password hash.** Still PBKDF2-HMAC-SHA256 at 60000 rounds. Factory `admin` seed still present.
- **Trigger match.** Still prefix match on the wire, with the empty-source and mutual-prefix load gates.
- **Scheme crypto.** v2 HMAC, constant-time MAC compare, and the iteration equality check before key derivation are unchanged. v1 encrypted envelopes are unchanged.
- **Token abandon.** Timeout still does not kill the process. The orderly-stop hook moved from the old `qFatal` site onto that timeout path (U-203). That matches the claim.
- **U-211.** Identity and parameter state outlive the node shell only as `shared_ptr` columns on a snapshot that is still defined to last one round. No separate finding. The race that matters is P1-6, on the scene block, not on these columns.

## 5. Residuals and recommended next steps

In order:

1. **P1-6.** Publish `m_aliveBlock` under the same mutex as the old `m_scene` copy (or an atomic `shared_ptr`). Do not `setFlowScene` on a Running or Paused executor from a tab change.
2. **P2-9.** Roll back `setLastFilePath` and `m_loadedReadonly` on every failed `loadProject`, including the v1 refusal. Add a regression leg: load a scheme with a relative path, attempt to open a v1 envelope elsewhere, assert `currentSchemeFilePath()` is still the first file and the readonly flag is unchanged.
3. **P1-1 remainder.** Author signature or trust-on-first-use is still absent. The pepper is still in the binary. Raw JSON still loads. Relative `..` still skips the confirm dialog; if the product wants “outside the scheme directory” to mean the resolved path, classify after `cleanPath` of the anchored path. `applyProjectJson` stays an ungated in-memory entry.
4. **P1-3 residual.** UDP port 0 with an empty address still uses `AnyIPv4`. Leave it if send-only source selection is the product rule; otherwise refuse `Missing` there too.
5. **P2-4 residual.** Keep production free of `FlowExecutor` subclasses. The protected destructor exists so `StuckExecutor` can be defined in tests.
6. **Unchanged product choices.** Prefix match (P2-3), PBKDF2 at 60000 (P2-1), no English UI (P2-5), both vision stacks and the two large `.cpp` files (P2-6). A flow that is already running still keeps running when the write gate rises (P0-1, now pinned by U-206’s test).

## 6. Tests present, not executed here

New or extended legs seen in the diff, none of them run on this VM:

- `tests/project_paths_test.cpp` — confirm required, confirm allows, relative path does not ask
- `tests/integration_test.cpp` — `testLegacyPlaintextPackageRefusedOnLoad`, `testWriteGateDoesNotInterruptRunningFlow`
- `tests/comm_bind_test.cpp` — empty and whitespace are `Missing`; `0.0.0.0` is explicit
- `tests/flow_executor_ownership_test.cpp`, `tests/node_state_body_test.cpp`, `tests/flow_executor_token_test.cpp` — ownership / token updates

A Windows development machine that has Qt 6, HALCON, MVS, and MSVC should run those legs after any fix for P1-6 or P2-9. This review cannot say they pass.

## 7. 给协调人（可转发）

- 审核对象：`main@f0c0322`。相对 r10 基线 `6c974b7`：**13 个提交，53 个文件，+1387 / −345**。
- 账本：**11 Fixed，4 Partial，3 Open，1 Withdrawn**。P1-3、P2-4、P2-8 从 Partial 收到 Fixed。P1-1 仍是 Partial（v1 信封已拒、目录外绝对路径要确认，但 pepper、无作者签名、裸 JSON、相对路径 `..` 仍在）。
- 新问题两条：**P1-6**（U-212 的存活块 `shared_ptr` 没有沿用原来保护 `m_scene` 的那把锁，切到正在跑的流程页会和执行线程抢这个指针）；**P2-9**（加载失败时，除「路径未确认」以外的退回都没把方案目录快照改回去，U-204 拒收旧明文包也在其中；界面却写「当前方案保持不变」）。
- 此前已 Fixed 的条目没有回退。云端没跑 Windows CTest（缺 Qt6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

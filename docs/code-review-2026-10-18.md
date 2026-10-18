# Vision Workbench code review — 2026-10-18

Review of `daxian2778055/vision-workbench` (`main`), product name VisionFlowPlatform. This pass is read-only except for this document. No production code was changed. The tree was not built or tested here: CMake targets Qt 6, HALCON, the Hikvision MVS SDK, and MSVC, and `thirdparty/` plus `dist/` are gitignored.

Counts at review time: about 198 `.cpp` files under `src/`, 207 headers under `include/`, 52 test translation units, 33 registered QtTest cases, two GitHub workflows, 33 files under `docs/`.

## 1. Architecture

VisionFlowPlatform is a Windows desktop machine-vision workbench. A user builds a directed graph of operators, the graph runs on a worker thread, and results, users, and alarms land in a local SQLite file.

| Area | Role |
| --- | --- |
| `src/main.cpp` | Process entry, crash filter (Windows), DB init, i18n install, login gate. |
| `src/MainWindow.cpp` (~4,300 lines) plus `ui/MainWindow.ui` | Shell: menus, docks, design/run modes, project open/save, permission chrome. |
| `include/NodeBase.h`, `src/HalconNode.cpp`, `src/NodeRegistry.cpp`, `src/NodeFactory.cpp` | Operator contract, parameter table, declarative registry (`VFP_REG`, ~104 registrations). |
| `src/*Node.cpp` | Operators. Two parallel families: HALCON nodes and `Opencv*` replacements (threshold, blob, match, calib, DNN, OCR, barcode). |
| `src/FlowScene.cpp`, `src/FlowExecutor.cpp` (~1,950 lines) | Graph model and the execution thread: topological order, loops, subflows, triggers, stats. |
| `src/ProjectManager.cpp`, `src/SchemePackage.cpp`, `src/SchemeCrypto.cpp` | `.vfp` JSON projects and the optional `VFPE` encrypted envelope. |
| `src/AppDatabase.cpp`, `src/SessionManager.cpp`, `src/FactoryPasswordGuard.cpp` | SQLite (alarms, inspection rows, users, audit log) and the role / factory-password gate. |
| `src/CommunicationManager.cpp`, `TcpCommNode`, `UdpCommNode`, `ModbusNode`, `PlcCommNode`, `GlobalTriggerManager` | Device I/O and “string in a frame starts a flow”. |
| `src/ScriptNode.cpp`, `src/ScriptSecurityPolicy.cpp` | Python/Lua execution with a Windows restricted-token sandbox. |
| `tests/` | QtTest suites (executor, DB, crypto envelope, script policy, calibration, comm write-back) plus HALCON/OpenCV probes that are built but not all registered with CTest. |
| `tools/` | `ci.ps1` (configure, build, ctest, a long list of repo gates), `repo_hygiene.py`, doc/ledger checkers. |
| `docs/` | Spec, VisionMaster gap ledger, operator manuals, and several guides that no longer match the tree (see P1-6). |
| `.github/workflows/` | `ci.yml` (self-hosted Windows) and `repo-hygiene.yml` (GitHub-hosted Ubuntu). |

Data flow: UI and comms mutate `ThreadSafeParams` and the scene; `FlowExecutor::run` walks a snapshot of the graph, calls `NodeBase`/`HalconNode::process`, propagates `DataObject`s, and batch-inserts inspection rows. Projects serialize the graph plus global cameras, comms, triggers, and variables.

## 2. Correctness and reliability

The executor, database, and node base class show repeated incident fixes: per-thread SQLite connections, WAL plus `busy_timeout`, bound SQL, output invalidation when a node fails, join-before-destroy, and a bounded external-trigger queue. Those are real.

Remaining risks:

- **Write failure is reported as success.** `HalconNode` presets `moduleStatus = true` before `run()` (`src/HalconNode.cpp` around the success-default comment). `WriteFileNode::run` returns on `cv::imwrite` failure after only clearing `m_outputImage` and never writes `moduleStatus = false` (`src/WriteFileNode.cpp`). With stop-on-failure enabled, a failed save still looks green.
- **String triggers are prefix matches.** `GlobalTriggerManager::onDataReceived` accepts `text == trigger` or `text.startsWith(trigger)` and fires the first hit (`src/GlobalTriggerManager.cpp`). A trigger of `T` matches any payload that begins with `T`.
- **A stuck worker aborts the process.** `FlowExecutor::~FlowExecutor` calls `qFatal` if `joinForDestroy(5000)` fails (`src/FlowExecutor.cpp`). That is the right choice versus use-after-free. The operational effect is that one blocked operator (camera SDK, script, HALCON) takes down the whole HMI, including inspection logging.
- **PBKDF2 work factor is attacker-controlled.** See P1-2. Opening a hostile envelope blocks the UI thread before the MAC is checked.
- **Design-mode editing is not tied to `canEditScheme()`.** Menus are. The canvas is not. See P0.

## 3. Security

SQL paths use placeholders (`AppDatabase::prepareWrite` / `addBindValue`). `FormulaNode` is a small recursive-descent parser, not `eval`. Script launch refuses a bare `python` name and resolves an absolute interpreter path. The self-hosted CI workflow refuses fork pull requests and pins `actions/checkout` to a commit SHA. `tools/repo_hygiene.py` scans for runner tokens, private keys, and unpinned actions.

Those controls do not cover the issues below.

### P0 — Default `admin` / `admin` can still change the live graph and run scripts

Evidence:

1. First launch inserts user `admin`, role `Admin`, password hash of the literal `admin` when `users` is empty (`src/AppDatabase.cpp`, `createTables`, the `createTables/seedAdmin` insert).
2. The forced-change dialog is cancelable. Cancel breaks the loop and leaves the session logged in (`src/MainWindow.cpp`, `enforceFactoryPasswordPolicy`, `dlg.exec() != Accepted` → `break`).
3. While the write gate is on, save/export/user admin/comm config are blocked. Flow run is not: `SessionManager::canRunFlow()` is `isLoggedIn()` only (`src/SessionManager.cpp`). The dialog text says flow execution is unaffected.
4. `canEditScheme()` only disables Save/Load/New and the Edit menu (`MainWindow::applyPermissionRestrictions`). `switchToDesignMode` always calls `setInteractive(true)` on every view. The parameter dock is not disabled, so a Script node’s `scriptContent` can be edited in memory.
5. Script policy defaults: execution enabled, confirmation off, languages Python and Lua (`include/ScriptSecurityPolicy.h`). `evaluate()` does not look at the script body (`src/ScriptSecurityPolicy.cpp`). `ScriptNode::run` then starts the interpreter.

Net: on a station that still has the seeded account, a person at the keyboard logs in with published credentials, dismisses the dialog, edits or adds a script, and runs it as the Windows user that owns the HMI. The Windows sandbox (on by default) strips privileges and drops to low integrity. The same header states that this mode does **not** deny network access and is not an untrusted-code boundary. `blockWhenElevated` defaults to false.

### P1 — A project file is code

Plain `.vfp` is JSON (`ProjectManager::loadProject`, the non-envelope branch). Node params, including `scriptContent`, round-trip through `toJson` / `fromJson`. Nothing signs that JSON. `requireConfirmation` defaults to false, including when there is no `QApplication` (`requestConfirmation` returns true). Loading a scheme from a USB stick or a share and pressing Run executes whatever Python or Lua is inside it, subject only to the sandbox above.

`WriteFileNode` and `ImageReadNode` take `filePath` from that same JSON and pass it to `cv::imwrite` / directory listing with no root jail. A scheme can read or overwrite any path the process can touch.

The encrypted envelope (`VFPE`) is optional. The unencrypted “readonly” flag is one byte and has no MAC (`SchemePackage::exportScheme` else-branch; `importScheme` trusts `flags`). Flipping that byte clears the readonly UI.

### P1 — Envelope KDF is an open-ended loop

`SchemePackage::importScheme` reads a `uint32` iteration count from the file and calls `SchemeCrypto::deriveKeys` **before** comparing the HMAC. `pbkdf2HmacSha256` loops `j < iterations` with no ceiling (`src/SchemeCrypto.cpp`). A file whose iteration field is `0xFFFFFFFF` hangs the UI thread as soon as any passphrase is submitted, right or wrong. Export uses 100,000; import trusts the file.

### P1 — Listeners on every interface, no authentication

- TCP server: `QTcpServer::listen(QHostAddress::Any, port)` default port 502 (`src/TcpCommNode.cpp`, `openConnection`).
- UDP: `bind(QHostAddress::AnyIPv4, localPort)` default 8000 (`src/UdpCommNode.cpp`).
- Modbus server sets `NetworkPortParameter` and never `NetworkAddressParameter` (`src/ModbusNode.cpp`). Qt’s server then listens on all interfaces. No auth exists at this layer.

`CommunicationManager` emits `dataReceived`. `GlobalTriggerManager` turns a matching (or prefix-matching) string into `FlowExecutor::requestExternalRound()`. On a plant network that is “start the inspection cycle,” not just “log a packet.”

### P2 — Password storage and scheme crypto are below the comments around them

- Account hashes are salted SHA-256 iterated 12,000 times (`kHashIterations` in `src/AppDatabase.cpp`). That is not a memory-hard KDF. Salt bytes come from `QRandomGenerator::global()`, which Qt documents as not a CSPRNG. Envelope salt/IV already use `QRandomGenerator::securelySeeded()` (`src/SchemePackage.cpp`). The two call sites disagree. Legacy unsalted SHA-256 still verifies.
- AES-256-CTR, HMAC-SHA256, and PBKDF2 are hand-rolled (`src/SchemeCrypto.cpp`). The header says this is not meant for a high-adversary setting. HMAC check is `QByteArray` inequality (`SchemePackage.cpp`), not a constant-time compare. Password verify is the same pattern.
- Minimum new admin password advertised in the dialog is 6 characters.

No committed runner tokens or private keys showed up in the tracked tree. `tools/repo_hygiene.py` is what keeps them out. `.gitignore` excludes `.push-*.ps1` specifically because those scripts have held credentials before.

## 4. Build, test, and CI

**Build (Windows, from `CMakeLists.txt` and `tools/ci.ps1`):**

- C++17, Qt 6 (`Widgets Core Gui Sql SerialPort Network SerialBus Concurrent Test Charts`).
- OpenCV is not found via `find_package`. Includes and `opencv_world4130` are hardcoded under `thirdparty/opencv/build` (`vc16`).
- HALCON defaults to `D:/Program Files/MVTec/HALCON-24.11-Progress-Steady` or `HALCONROOT`.
- MVS defaults to `D:/MVS` or `MVSROOT`. Missing headers are a warning, not a hard failure.
- `thirdparty/` and `dist/VisionFlowPlatform` are gitignored. CI creates junctions to `E:/halcon/2/xin1` unless `VFP_ASSETS_ROOT` is set (`.github/workflows/ci.yml`).

**Tests:** 33 `vfp_add_qtest` targets (NodeCore, integration, database, scheme package, script policy, factory password, soak at 20 seconds, calibration, comm write-back, and others). Assertions are real (`QCOMPARE` / `QVERIFY` appear hundreds of times in `tests/integration_test.cpp` and `tests/comm_writeback_test.cpp`). `tools/run_qtest.cmake` exists because QtTest writes nothing useful when stdout is a pipe. Almost every test target sits inside `if(MSVC)`. HALCON probes (`halcon_*_probe`, `hde_train_probe`, `tracker_test`) are built and not part of the QtTest gate. `OpencvNodesTest` is a raw executable plus a CTest regex, also MSVC-only.

**CI that actually exists:**

| Workflow | Runner | What it does |
| --- | --- | --- |
| `.github/workflows/ci.yml` | `self-hosted`, `windows` | `tools/ci.ps1 -Config Release` on push to `main`, same-repo PRs, and manual dispatch. Fork PRs are skipped on purpose: the runner executes workspace scripts. |
| `.github/workflows/repo-hygiene.yml` | `ubuntu-latest` | `tools/repo_hygiene.py` (secrets, large files, workflow SHA pins, self-hosted PR guard, `.ps1` encoding, gitignore). |

`tools/ci.ps1` exit codes 0–15 also cover doc anchors, ledger shape, and “open item” gates. That is a serious local gate. It is not what `docs/ci-cd-guide.md` describes (cppcheck, clang-tidy, CodeQL, Codecov, Slack, a deploy job). Those jobs are not in `.github/workflows/`.

This review did not execute `ci.ps1`. There is no HALCON/Qt/MSVC toolchain in the environment used for the review.

## 5. Maintainability

- **Size.** `src/MainWindow.cpp` is ~4,325 lines, `src/FlowExecutor.cpp` ~1,950, `tests/integration_test.cpp` ~5,170. Behavior is commented heavily, which helps, and it also means a change to permissions or execution has a wide blast radius.
- **Two operator stacks.** HALCON nodes and `Opencv*` nodes implement overlapping vision steps and are both registered. That is an explicit portability strategy (the CMake comment says OpenCV replaces the HALCON region path). It doubles the surface that smoke tests must cover.
- **i18n.** Source language is Simplified Chinese (`include/I18n.h`). `tr()` is concentrated in `MainWindow.cpp` (about 172 calls) and a handful of other files. `docs/internationalization-guide.md` already records that an English UI was declined and that parameter panels, logs, and operator names stay Chinese. `translations/visionflow_en.ts` exists; most UI strings will not follow a language switch. `MainWindow::switchLanguage` restarts the process via `QProcess::startDetached`.
- **Docs versus the tree.** `docs/security-development-guide.md` shows `validateFilePath` / `validateStringInput` helpers that do not exist under `src/`. `docs/ci-cd-guide.md` describes a pipeline that was never added. `docs/internationalization-guide.md` is a general Qt tutorial with a later banner that contradicts the tutorial. `requirements_decoded.txt` is marked destroyed. `project-audit-report/` is an HTML snapshot dated 2026-09-09, not this review. There is no root `README`.
- **Dead-ish entry points.** `NodeFactory::tryCreateNamedTool` falls back to a generic `HalconNode` named “MVS图像源” if registry creation throws, which hides a missing camera SDK behind a node that cannot acquire. Probes under `tests/halcon_*` are easy to mistake for the CTest suite.

Naming inside the executor and database is consistent and the comments usually say why a check exists. That part is in good shape.

## 6. Prioritized findings

| ID | Sev | Topic | Where | What to do |
| --- | --- | --- | --- | --- |
| P0-1 | P0 | Seeded `admin`/`admin` still yields a session that can edit a script and run it | `src/AppDatabase.cpp` seed insert; `src/MainWindow.cpp` `enforceFactoryPasswordPolicy`; `src/SessionManager.cpp` `canRunFlow`; `src/MainWindow.cpp` `switchToDesignMode`; `include/ScriptSecurityPolicy.h` defaults; `src/ScriptNode.cpp` | Do not leave an Admin session up if the factory password dialog is cancelled (quit, or drop to a role that cannot open the parameter dock). Disable canvas interaction and param edits while `writesBlocked()` or `!canEditScheme()`. Default `requireConfirmation` on, or refuse script nodes until the factory password is retired. |
| P1-1 | P1 | Scheme files are unsigned code plus arbitrary paths | `src/ProjectManager.cpp` `loadProject`; `src/ScriptNode.cpp`; `src/WriteFileNode.cpp`; `src/ImageReadNode.cpp`; `src/SchemePackage.cpp` readonly flag | Treat `.vfp` as executable. Sign schemes, or confirm before the first script run of a newly loaded file. Jail image read/write to a project directory. MAC the readonly flag even when the payload is not encrypted. |
| P1-2 | P1 | Iteration count in the envelope is unbounded and applied before MAC verify | `src/SchemePackage.cpp` `importScheme`; `src/SchemeCrypto.cpp` `pbkdf2HmacSha256` | Clamp iterations to a fixed window (for example 1..200000) and reject the rest before any KDF work. |
| P1-3 | P1 | TCP/UDP/Modbus listen on all interfaces; a matching string starts a flow | `src/TcpCommNode.cpp`; `src/UdpCommNode.cpp`; `src/ModbusNode.cpp`; `src/GlobalTriggerManager.cpp` | Bind address must be an explicit parameter, default `127.0.0.1` unless the user opts into a NIC. Require exact trigger match, not prefix. Document that these ports are unauthenticated. |
| P1-4 | P1 | Image write failure stays `moduleStatus = true` | `src/WriteFileNode.cpp` `run`; `src/HalconNode.cpp` success default | On `imwrite` failure set `moduleStatus` false and clear outputs, same as other nodes. |
| P1-5 | P1 | Published CI guide does not match the workflows; tests do not build off MSVC | `docs/ci-cd-guide.md`; `.github/workflows/ci.yml`; `.github/workflows/repo-hygiene.yml`; `CMakeLists.txt` `if(MSVC)` around `vfp_add_qtest` | Replace the CI guide with the two real workflows and `tools/ci.ps1` exit codes. Add a root README with the HALCON/Qt/MVS prerequisites. Keep the MSVC gate, and say so, so Linux checkouts are not expected to `ctest`. |
| P2-1 | P2 | Account KDF and salt generator | `src/AppDatabase.cpp` `stretchHash`, `QRandomGenerator::global()` | Move new hashes to a memory-hard KDF. Take salt from `QRandomGenerator::system()` or the OS CSPRNG. Drop the unsalted SHA-256 verifier once no row still uses it. |
| P2-2 | P2 | Hand-rolled AES/HMAC, non-constant-time compares | `src/SchemeCrypto.cpp`; `src/SchemePackage.cpp` MAC check; `AppDatabase::verifyPassword` | Prefer the OS crypto API (bcrypt/CNG) for new envelopes. Compare MAC and password hashes in constant time. |
| P2-3 | P2 | Short string triggers match too much | `src/GlobalTriggerManager.cpp` `startsWith` | Exact match, or a delimiter, and reject triggers shorter than a configured minimum. |
| P2-4 | P2 | Stuck executor kills the process | `src/FlowExecutor.cpp` destructor `qFatal` | Keep the abort (it prevents use-after-free). Surface `workerPhaseName()` in the UI before exit, and add a watchdog that stops the flow earlier than the destructor. |
| P2-5 | P2 | English UI is a thin `tr()` layer | `include/I18n.h`; `src/MainWindow.cpp`; `docs/internationalization-guide.md` | Leave the product decision as-is. Delete or banner the tutorial sections that still tell a reader to add `ja_JP` / `qt_add_translations`. |
| P2-6 | P2 | God objects and dual node stacks | `src/MainWindow.cpp`, `src/FlowExecutor.cpp`, `src/NodeRegistry.cpp` | Split permission/project actions out of `MainWindow` when the next feature touches them. Do not add a third copy of a vision operator without deleting or wrapping the HALCON one. |
| P2-7 | P2 | Security guide describes helpers that are not in the tree | `docs/security-development-guide.md` | Either call real checks from `WriteFileNode` / project load, or mark the guide as a template. Right now it implies path checks that `WriteFileNode` does not perform. |
| P2-8 | P2 | Stale artifacts at the repo root | `requirements_decoded.txt`, `需求.txt`, `project-audit-report/`, no `README` | Point newcomers at `docs/需求规格说明书.md` and this review. The September 2026 HTML audit is not current. |

### Suggested order of work

1. P0-1 session and canvas lock, plus script confirmation. This is the issue that matches the dialog’s own threat sentence.
2. P1-2 iteration clamp. Small, local, no product redesign.
3. P1-4 write-node status. One failure path, easy to unit test.
4. P1-3 bind address and exact trigger match.
5. P1-1 scheme trust (signature or first-run script prompt, path jail).
6. Rewrite `docs/ci-cd-guide.md` and add a README (P1-5). Crypto and KDF upgrades after that.

## What is already in good shape

- Bound SQL, per-thread SQLite connections, and failure strings that include the operation name (`AppDatabase::failureText`). The factory-password gate is tested (`tests/factory_password_guard_test.cpp`) and the writers that must refuse (`ProjectManager` save/export) do refuse.
- Script launch refuses a relative interpreter name, strips `LD_*` / `LUA_*` style variables, and has a Windows restricted-token path with tests (`tests/script_security_test.cpp`, `tests/restricted_token_probe.cpp`). The comments are honest about what the sandbox does not do.
- `FlowExecutor` documents lock order (`m_graphCacheMutex` then `m_mutex`), parks the worker before graph edits, and queue-bounds external triggers instead of dropping them silently.
- `FormulaNode` does not shell out or call a JS engine.
- CI for the self-hosted runner states the trust boundary in the workflow header and encodes it in `if:`. Hygiene CI is safe for fork PRs because it does not touch that runner.
- QtTest results are wrapped so a green ctest log cannot hide a crash or an empty run (`tools/run_qtest.cmake`).
- Project save uses `QSaveFile`. JSON parse errors fail the load instead of wiping the open scheme (`ProjectManager::loadProject`).

## Out of scope for this pass

No fix was applied. None of the P0/P1 items is a one-line change that preserves current operator behavior, and the instruction for this review was to report rather than rewrite. The Windows test suite was not executed in this environment.

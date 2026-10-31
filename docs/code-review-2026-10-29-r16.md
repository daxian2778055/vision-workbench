# Vision Workbench 复核 — 2026-10-29 r16

接 r1–r15 笔记。本轮审的是 `origin/main` 的 `26cc44b069f534b3e9cae7899023b85a43ea2882`（发版登记 `rc-20261029.8`，登记的装机包提交是 `0bd456390bdf1882ed724e819e60f9240fbb1241`）。相对 r15 基线 `95fb226ef48e2ca0870c40db1cda315e5a5f224c`：**2 个提交，8 个文件，+222 / −29**。

本轮没有编译，也没有跑 CTest。云端虚拟机没有 Windows 套件所需的 Qt 6、HALCON、MVS、MSVC。下面凡是提到测试，只说明作者写进仓库的契约，不是本机跑出来的结果。`python tools/wiring_checks.py --repo .` 与 `python tools/reason_key_sites.py --check` 在本机跑过。INV5 的三条改坏臂是在临时副本上改 `src` 再调用 `check_param_registration`，没有改工作区。

产品代码没有改。本文件是这一轮唯一的变更。

上一轮的结论在 r15 笔记里（基线 `95fb226`，账本 20 Fixed / 7 Partial / 1 Open / 1 Withdrawn）。

## 0. 结论先说

r15 的唯一 Open **P1-9 可以关**。`saveCurrentImage` 写的是 `m_currentPath`（框属于哪张图），不再读已经切过去的 `currentRow`。`reloadImages` 在 `setCurrentRow(0)` 之前把 `m_currentPath` 清掉，空路径直接返回，不调用 `YoloAnnotation::save`，所以打开数据集不会把第一张已有 txt 截成空文件。仓库里有两条对话框级用例对得上这两条现场路径；本机没跑。

**P2-12 可以关。** 删除、清空、换目录三处都看了 `saveCurrentImage` 的返回值：失败不再写成「已删除 / 已清空」，换目录在失败时直接返回。余量：删除和清空先改内存再存，失败不把框放回画布；这两处没有用例。

**P2-13 仍是 Partial，不关。** 复制失败会删掉本轮已经 `QFile::copy` 成功的目标，谓词改成了 `!allowsGraphEditing()`，槽入口也查了写闸和这把锁。菜单重算只挂在 `onExecutionStarted` / `onExecutionStopped`。正常跑完一轮只发 `executionFinished`，然后把状态写成 Idle，**不发** `executionStopped`（全仓库 `emit executionStopped` 只有 `FlowExecutor::run` 里那 6 处，都是停止、空场景或报错）。`onExecutionFinished` 会解画布锁，不会重算打包菜单。于是点一次「开始」把菜单关掉之后，单次运行正常结束，菜单一直灰，画布已经可编辑。这是这 2 个提交引进的行为。另外，槽里的复查在目录对话框之前；对话框开着时外部触发仍可以 `startExecution()`，返回后 `pack()` 不再查运行态。

INV5 现在能抓住 **P1-8 的原文件形态**：把 `DnnDetectNode` 的 `yoloLayout` 从 `registerParams({...})` 里搬出去，临时副本上是 RED。它抓不住同一形态出现在 **已经有 `registerParam(sp)` 的 `init`** 里：`ObjectCountingNode` 把 `registerParam(makeDoubleParam("scale", ...))` 改成丢掉返回值的 `makeDoubleParam(...)`，闸仍是 GREEN。摘掉整行 `scale` 登记会 RED，作者说的那条改坏臂成立。键名已含下划线和数字，`lineA_x` 在 `run` 正文里能被看见。

P1-1、P2-1、P2-3、P2-5、P2-6 的判定点不在这 8 个文件里，维持 Partial。已 Fixed 项的抽查没有回退。没有新的编号。

| 桶 | 本轮 |
| --- | --- |
| Fixed（22） | 原 20 条不变，加上 **P1-9**、**P2-12** |
| Partial（6） | P1-1、P2-1、P2-3、P2-5、P2-6，以及仍未关上的 **P2-13** |
| Open（0） | — |
| Withdrawn（1） | P1-4 |

`MainWindow.cpp` **4656** 行（r15：4637，+19）。`FlowExecutor.cpp` **2312** 行（r15 同数）。`src/` 与 `include/` 下仍无 `qFatal(`。`MainWindow.cpp` 里 `tr(` 按出现次数计仍是 **206**；本轮新增的界面句子是 `QStringLiteral`。

静态复核，Windows CTest 没跑。

### 逐条（可转发）

- **P1-9 关。** 保存用 `m_currentPath`（`src/BoxAnnotatorDialog.cpp:337-343`）。打开目录时先清归属再 `setCurrentRow(0)`（`:241-248`），空路径当成功返回，不打开 txt。载入成功之后才把路径认下来（`:327-328`）。读不出的图清掉归属，避免随后用空框去覆盖（`:317-324`）。用例：`tests/ui_guards_test.cpp:302-313`、`:315-337`。本机未跑。
- **P2-12 关。** `onChooseDir`（`:214-215`）、`onDeleteBox`（`:395-400`）、`onClearImage`（`:409-412`）都在返回假时停住。关窗和 Esc 仍走 `confirmDestructive`，默认按钮是「取消」（`include/ConfirmDestructive.h:18-22`）。余量：画布上的框已经删掉或清空，失败只改状态栏，不把内存改回去。没有这三处的用例。
- **P2-13 不关。** 回滚在 `src/ProjectPackager.cpp:116-123`。谓词在 `src/MainWindow.cpp:4234-4237`。槽复查在 `:4079-4086`，位于 `getExistingDirectory`（`:4087`）之前。菜单重算在 `:3199-3200` 与 `:3208-3209`。正常结束的路径是 `src/FlowExecutor.cpp:848` 的 `executionFinished`，接着 `:900-902` 把状态写成 Idle 并 `break`，没有 `executionStopped`。`onExecutionFinished`（`src/MainWindow.cpp:3212-3218`）只解当前画布的锁。`m_packModelsAct->setEnabled` 全文件只有 `applyPermissionRestrictions` 里两处（`:4209`、`:4234`）。
- **INV5。** 当前树 `[ELW-VERDICT] GREEN`。`DnnDetectNode` 上把 `yoloLayout` 搬出 `registerParams` → RED。`ObjectCountingNode` 上丢掉 `scale` 那次 `registerParam` 的返回值、正文里仍留着 `makeDoubleParam("scale")`，并且文件里还有 `registerParam(sp)` → GREEN。见第 5 节。
- **P1-1、P2-1、P2-3、P2-5、P2-6 仍 Partial。** 判定点抽查行号仍在，见第 6 节。
- **已 Fixed 抽查没有回退。** 见第 7 节。

## 1. 相对 `95fb226` 的变化量

| 项 | 值 |
| --- | --- |
| r15 基线 | `95fb226ef48e2ca0870c40db1cda315e5a5f224c` |
| 本轮 HEAD | `26cc44b069f534b3e9cae7899023b85a43ea2882` |
| 代码提交 | `0bd456390bdf1882ed724e819e60f9240fbb1241`（HEAD 只在它后面加了一行登记） |
| 提交 | 2 |
| 文件 | 8 |
| 增 / 删 | +222 / −29 |
| 登记的当前装机包 | `docs/release-registry.json` 末项 `rc-20261029.8`，提交 `0bd4563`，exe md5 `c6ab05aed0059cdd81b5dec76168806a`，10128384 字节 |

HEAD 比装机包提交多一个登记提交（`26cc44b` 只改登记表，把 `rc-20261029.7` 标成已被取代）。代码行为以 `0bd4563` 为准。`0bd4563..HEAD` 的 diff 只有 `docs/release-registry.json`（+10 / −1）。`95fb226..0bd4563` 是 7 个文件、+212 / −28。本机没有那个 exe，没有对 md5 `c6ab05ae` 做字节核对。

| 提交 | 本轮核对 |
| --- | --- |
| `0bd4563` | 作者说明要修 P1-9、P2-12 残留、P2-13 残留，并收紧 INV5。见第 3–5 节。路线图第八批把这四项记成已修（`docs/VisionMaster4.4对标分析与路线图.md:404-411`）。P2-13 那一行与源码不一致，见第 4 节。 |
| `26cc44b` | 登记 `rc-20261029.8`。登记说明写「P1-9 + P2-12/P2-13 残留 + INV5 收紧」。P2-13 不能按「残留已清」读。 |

## 2. 账本

| ID | r15 | r16 | `26cc44b` 上的证据 |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `canRunFlow()` 仍是已登录且写闸未开（`src/SessionManager.cpp:71-76`）。空用户表仍播种 `admin` / `createPasswordHash("admin")`（`src/AppDatabase.cpp:418-428`）。打包菜单见 P2-13，不把 P0-1 打回 Partial。 |
| P1-1 | Partial | **Partial** | `kPlainPepper` 仍是 `VisionFlow-plaintext-envelope-MAC-1`（`src/SchemePackage.cpp:14`）。裸 JSON 成功路径仍把 `m_lastIntegrityNote` 设为 `旧明文 JSON 格式`（`src/ProjectManager.cpp:307`）。`applyProjectJson` 仍只建场景再套全局配置，不调用 `confirmOutsideSchemePaths`（`:416-424`）。本轮 diff 未触及这些文件。 |
| P1-2 / N1 | Fixed | **Fixed** | 加密导入仍在轮数不等于 `kIterationsValid` 时于 `deriveKeys` 之前返回（`src/SchemePackage.cpp:142-143`）。 |
| P1-3 | Fixed | **Fixed** | 本轮 diff 未触及 `UdpCommNode.cpp`。 |
| P1-4 | Withdrawn | **Withdrawn** | 未重开。 |
| P1-5 | Fixed | **Fixed** | 本轮 diff 未触及 `README.md`。 |
| P1-6 | Fixed | **Fixed** | 同场景快路径仍在清缓存之前返回（`src/FlowExecutor.cpp:333-346`）。 |
| P2-1 | Partial | **Partial** | `kHashIterations` 仍是 60000（`src/AppDatabase.cpp:51`）。 |
| P2-2 | Fixed | **Fixed** | 本轮 diff 未触及 `SchemeCrypto.cpp`。 |
| P2-3 | Partial | **Partial** | 字符串触发仍是相等或 `text.startsWith(entry.triggerSource)`（`src/GlobalTriggerManager.cpp:176`）。 |
| P2-4 | Fixed | **Fixed** | 本轮 diff 未触及 `include/FlowExecutor.h`。 |
| P2-5 | Partial | **Partial** | 语言菜单仍在（`src/MainWindow.cpp:1159-1168`）。本轮新增界面字符串是 `QStringLiteral`，`tr(` 仍是 206 次。 |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4656 行（4637→4656），`FlowExecutor.cpp` 2312 行。`src/NodeRegistry.cpp` 仍同时包含 Halcon 与 OpenCV 节点。 |
| P2-7 | Fixed | **Fixed** | 本轮 diff 未触及 `docs/security-development-guide.md`。 |
| P2-8 | Fixed | **Fixed** | 本轮 diff 未触及 `docs/归档/`。 |
| P2-9 | Fixed | **Fixed** | 本轮 diff 未触及 `ProjectManager.cpp` 的加载回滚。 |
| P2-10 | Fixed | **Fixed** | `saveProject` 在 `open` 失败、短写、`commit` 失败时直接返回（`src/ProjectManager.cpp:146-161`）。`setLastFilePath` 仍在 `commit` 成功之后（`:164-166`）。 |
| N2 | Fixed | **Fixed** | 本轮 diff 未触及 `docs/ci-cd-guide.md`。 |
| N3 | Fixed | **Fixed** | 本轮 diff 未触及 MAC 比较。 |
| P1-7 | Fixed | **Fixed** | 见第 7 节。文件不在这 8 个里。 |
| P1-8 | Fixed | **Fixed** | `dnnBackend` / `yoloLayout` 仍在 `registerParams` 的 `});` 之前（`src/DnnDetectNode.cpp:242-248`）。 |
| P2-11 | Fixed | **Fixed** | 见第 7 节。 |
| P2-12 | Partial | **Fixed** | 见第 3 节。 |
| P2-13 | Partial | **Partial** | 见第 4 节。回滚与谓词、槽复查关上了；菜单没有跟正常结束走。 |
| P2-14 | Fixed | **Fixed** | `dnnMutex()` 仍是函数内静态 `QMutex`（`src/DnnDetectNode.cpp:39-43`）。`getOrLoadNet` 仍不加锁（`:81-96`）。 |
| P2-15 | Fixed | **Fixed** | 见第 7 节。路线图只追加了第八批，没有把口令句子改回去。 |
| P3-1 | Fixed | **Fixed** | `onProbeEnv` 仍把 `TrainingRunner::probe` 放进 `QtConcurrent::run`（`src/TrainingRunnerDialog.cpp:216-220`）。 |
| P1-9 | Open | **Fixed** | 见第 3 节。 |

## 3. P1-9 与 P2-12

### P1-9 — 关

| 字段 | 内容 |
| --- | --- |
| 严重度 | P1 |
| 状态 | **Fixed** |
| 路径 | `include/BoxAnnotatorDialog.h:102-104`；`src/BoxAnnotatorDialog.cpp:241-248`、`:285-306`、`:317-328`、`:337-354`；`include/YoloAnnotation.h:102-117` |

r15 的两条现场路径分别对上现在的控制流。

1. **保存写的是离开的那张，不是列表已经选中的那张。** `onImageChanged` 仍由 `currentRowChanged` 驱动（`:167`），进槽时当前行已经是新行。`saveCurrentImage` 只用 `m_currentPath`（`:341`）。这个成员只在 `loadCurrentImage` 把图读成功之后写成 `currentImagePath()`（`:328`）。切换时内存里的框还是上一张的，路径也还是上一张的，所以框写回上一张的 txt。写完才 `m_lastRow = row` 并 `loadCurrentImage()`（`:305-306`）。
2. **第一次 `setCurrentRow(0)` 不截断已有 txt。** `reloadImages` 在 `m_loading = false` 之后、`setCurrentRow(0)` 之前执行 `m_boxes.clear()`、`m_currentPath.clear()`、`m_lastRow = -1`（`:243-248`）。紧接着的 `onImageChanged` 调用 `saveCurrentImage`，路径是空的，`:342-343` 直接 `return true`，不会走到 `YoloAnnotation::save`。`save` 本身仍是 `QIODevice::Truncate`（`include/YoloAnnotation.h:105`），但这条打开路径到不了 `open`。

读失败也不认领：`QImageReader::read` 得到空图时清 `m_boxes` 和 `m_currentPath` 再返回（`:317-324`）。之后的保存仍是空路径，不会把这张已有 txt 写成空文件。`m_lastRow` 在 `loadCurrentImage` 之前就写成了新行（`:305`），读失败不会把它改回去；下一张切换时因为路径已空，保存是空操作，再载入新行。丢的是「没能认领的那一张上还没落盘的框」，磁盘上的旧 txt 还在。

`onChooseDir` 先保存再 `reloadImages`（`:214-218`）。保存走的是旧的 `m_currentPath`，不依赖即将换掉的 `m_dir`。`reloadImages` 再把归属清掉，新目录的第 0 张不会吃到上一套数据集的框。

用例和契约（本机未编译、未跑）：

| 用例 | 契约 | 静态上盖住的路径 |
| --- | --- | --- |
| `annotationDialogDoesNotTruncateOnOpen`（`tests/ui_guards_test.cpp:302-313`） | 构造对话框之后，第一张已有 txt 的字节与打开前相同 | 对应「空 `m_currentPath` 不写盘」。它不断言画布真的载入了框；若 `setCurrentRow(0)` 没发出信号，这条也会绿 |
| `annotationDialogSwitchSavesToPreviousImage`（`:315-337`） | `onBoxDrawn` 之后把列表设到第 1 行；`img0.txt` 变成 2 行，`img1.txt` 不存在 | 要绿，必须已经把第 0 张认领进 `m_currentPath`，并且切换时写的是第 0 张。作者说明里的改坏臂（改回 `currentImagePath()`）会让框写到第 1 张，这条会红 |

缺口，不阻止关闭：第二张事先已有 txt 时，用例没有断言那些字节保持不变。当前 `loadCurrentImage` 只读不写（`YoloAnnotation::load` 以 `ReadOnly` 打开）。用例用 `QMetaObject::invokeMethod` 调私有槽 `onBoxDrawn`，不经过鼠标。没有「保存返回假则不换行」的对话框级用例，那一条仍是 r15 已经看过的失败回退（`:290-295`），路径改对之后它退回的是 `m_lastRow`，写盘目标仍是 `m_currentPath`。

### P2-12 — 关

r15 还没接上的三个调用点现在都看返回值。

- `onChooseDir`：`saveCurrentImage` 为假就 `return`（`:214-215`），不改 `m_dir`，也不 `reloadImages`。失败文案由 `saveCurrentImage` 自己写（`:350`）。
- `onDeleteBox`：先从 `m_boxes` 删掉，保存失败时状态是「已从画布删除，但保存失败…」，然后 `return`（`:395-399`）。成功文案「已删除，剩 N 个框」只在返回真之后（`:401`）。
- `onClearImage`：同样，失败文案在 `:409-412`，成功文案「已清空本图」在 `:414`。

关窗与 Esc 的逻辑不在本轮 diff 的失败分支里，仍是保存失败且操作员不确认时留下窗口（`:255-274`）。

余量，不单列编号：

- 删除和清空在调用 `save` 之前已经改了内存和画布。保存失败时磁盘仍是旧内容，画布是新内容，状态栏说明了这件事。下一次切换会再存一次；若用户随后在失败确认里强行关窗，磁盘保持旧框，这次删除不会落盘。方向是「没写成成功」，不是 r15 那种成功文案盖掉失败。
- 「保存本图」按钮仍直接连到 `saveCurrentImage`（`:187-188`）。`clicked` 不接收 bool；失败时函数内部会写状态栏。
- 这三处没有新用例。`tests/project_packager_test.cpp` 与对话框失败路径都没进本轮 diff。

## 4. P2-13 — 仍 Partial

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Partial** |
| 路径 | `src/ProjectPackager.cpp:46-48`、`:116-123`；`src/MainWindow.cpp:732-736`、`:751-770`、`:3190-3218`、`:4076-4092`、`:4230-4237`；`src/FlowExecutor.cpp:625-633`、`:848-902`、`:459-468`、`:546-554` |

作者说明和路线图第八批写的是：菜单随执行起停重算，谓词与画布锁同源，槽内再查，复制失败回滚。前半句里的「起停」只覆盖了两个槽，而且正常跑完并不走停止槽。

### 已经关上的一半

- **孤儿文件。** `createdTargets` 只在 `QFile::copy` 成功之后追加（`:123`）。复制失败时先 `QFile::remove` 这些路径再返回（`:116-121`），`Result::ok` 保持默认假，后面的 `setParam`（`:131-134`）不会执行。参数仍指向原路径，本轮复制成功的那些目标会被删掉，下一轮不必为了躲开它们而改成 `name_2.ext`。`r.ok == false` 时界面只弹出 `r.error`（`src/MainWindow.cpp:4093-4095`），不会把已回滚的清单说成已打包。
- **谓词。** `applyPermissionRestrictions` 里打包项用的是 `canEditScheme() && !canvasEditShouldBeLocked(!allowsGraphEditing())`（`:4234-4237`）。`canEditScheme` 含 `!writesBlocked()`（`src/SessionManager.cpp:66-68`）。不再把 `QThread::isRunning()` 传进画布锁。
- **槽入口。** `triggered` 里先看 `canEditScheme` 和 `m_executor->allowsGraphEditing()`（`:4079-4086`），不通过就警告并返回。

`tests/project_packager_test.cpp` 不在本轮 diff 里，没有复制失败回滚的腿，也没有菜单腿。本机未跑。

回滚的缝，单独不构成继续 Partial 的理由：`QFile::remove` 的返回值没看。删不掉的目标会留在 `assets/`，下一轮 `QFile::exists` 仍会把新文件加成 `_2`。失败的那一次 `copy` 本身不在 `createdTargets` 里；若实现留下半截目标文件，同样会占住名字。这比 r15 的「成功复制的全部留下」窄。

### 没有关上的一半：菜单跟错了结束信号

`applyPermissionRestrictions` 在 `onExecutionStarted`（`:3200`）和 `onExecutionStopped`（`:3209`）里调用。`m_packModelsAct->setEnabled` 没有第三个调用点。

`FlowExecutor::run` 正常收尾（软触发一轮跑完、连续模式里非 Running 后跳出、没有相机源的硬触发跳出）是：

1. `emit executionFinished()`（`src/FlowExecutor.cpp:848`），此时往往还在 Running。
2. 软触发没有待补跑时 `setRunState(Idle)` 并 `break`（`:900-902`）。
3. 循环外再写一次 Idle（`:905-907`）。

这条路径上没有 `executionStopped`。全仓库的 `emit executionStopped` 只有六处，都在 `run()` 里：状态已是 Stopped（`:633`）、场景没接上（`:664`）、空场景（`:670`）、拓扑有环（`:690`）、轮内发现 Stopped（`:741`、`:752`）。用户点停止、失败停、空场景、报错，会进 `onExecutionStopped`，菜单会按当时的 `allowsGraphEditing()` 重算。点「开始」跑完一轮正常结束，不会。

`onExecutionFinished`（`src/MainWindow.cpp:3212-3218`）会 `updateEditLockForCurrentScene()`。那个函数用的就是 `!allowsGraphEditing()`（`:3366-3367`）。单次运行的 `executionFinished` 从工作线程排队到界面线程时，`:900` 的 Idle 多半已经写上，画布锁解开。打包菜单仍停在 `onExecutionStarted` 里设下的禁用。

`QAction` 禁用之后 `triggered` 不会发，槽里的复查进不去。操作员要等一次真正的 `executionStopped`，或者再走登录 / 改密那些会调用 `applyPermissionRestrictions` 的路径（`:636`、`:4278`、`:4283`、`:4334`、`:4342`），菜单才会回来。

这和 r15 的洞方向相反，所以不把 P0-1 打回，也不另开编号：r15 是「跑着的时候菜单仍可点」；这一轮是「跑完回到空闲，菜单仍不可点，画布已经解锁」。危险的那一半（整段运行期间菜单保持登录时的可用）被开始槽关掉了。作者写的「随执行起停重算」没有覆盖这个进程里最常见的结束方式。

停稳暂停同一类。`executionParked` / `executionResumed` 只改画布锁和状态栏（`:751-770`），不调用 `applyPermissionRestrictions`。`allowsGraphEditing()` 在已停稳的 Paused 上返回真（`src/FlowExecutor.cpp:459-468`），画布会解，打包菜单保持开始时的禁用。恢复之后菜单也仍是禁用，这和「正在跑」一致。偏严，不是 r15 那种跑着还能打包。

谓词改成 `allowsGraphEditing()` 之后，和 `isRunning()` 的分叉只在「已停稳的暂停」。菜单刷新不发生在这个分叉上，所以 `applyPermissionRestrictions` 里换谓词，并不改变暂停期间菜单的实际可用态。槽函数用的是新谓词：若菜单是亮的，停稳暂停里会放行。当前时序下菜单是灰的，点不到。

### 目录对话框期间仍可能打包进正在跑的流程

槽复查通过之后才 `QFileDialog::getExistingDirectory`（`:4087-4092`）。对话框用的是嵌套事件循环。这个窗口里：

- 界面线程上排队的 `startExecution` 可以执行。
- 其他线程上的外部触发在空闲时直接 `startExecution()`（`src/FlowExecutor.cpp:546-554`），不经过这次菜单槽。

对话框返回后没有第二次 `allowsGraphEditing()` 检查，直接 `ProjectPackager::pack(currentFlowScene(), ...)`。`HalconNode::setParam` 仍不读编辑锁（`src/HalconNode.cpp:143-168`）。跑着的流程不会被这次打包停掉，之后的轮次按新的相对路径解析。这是 r15 要关掉的那一半里还留着的时间窗：从「整个运行期间菜单可点」收成「选目录的那一段时间」。无人值守线上，操作员在空闲时点打包、PLC 在选目录时触发，就会撞上。

`onExecutionFinished` 若要补上菜单重算，不能把「Finished」当成「已停止」：连续模式每轮都发这个信号（`:848` 之后 `:857` 继续循环），状态仍是 Running。重算必须再读一次 `allowsGraphEditing()`。

## 5. INV5：P1-8 的原文件能红，`registerParam(sp)` 仍会放行

`tools/wiring_checks.py:276-367` 的 `check_param_registration`。本机对当前树运行 `python tools/wiring_checks.py --repo .`，结果是 `[ELW-VERDICT] GREEN`，`findings=0`。

和 r15 的差别：

- 登记键只从 `registerParam(` / `registerParams(` 的括号配对块里抽 `make*Param` / `sp.name =`（`:310`、`:326-329`）。块外的 `makeEnumParam` 不再算登记。
- `init` 正文里若出现字面量 `registerParam(sp)`，则 **整个 init 正文** 里的 `make*Param` 和 `sp.name` 都算登记（`:330-331`）。这是为了接住 `sp.name = "..."; registerParam(sp);`，因为那次调用的括号里没有键名。
- 键名是 `[A-Za-z][A-Za-z0-9_]*`（`:304`）。`lineA_x` 这一类能被看见。
- 全仓 `run()` 一个键都解析不到时报 `regex_degraded`（`:365-366`）。
- 注释写明了 r15 已经点过的盲区：只扫 `::run(` 正文、不看 letterbox、本文件里 `m_params[键] =` 就算写过（`:291-294`）。

本机在临时副本上改源码再调用同一函数（工作区文件未改）：

| 改坏臂 | 结果 |
| --- | --- |
| 当前树 | GREEN |
| 真实 `DnnDetectNode.cpp`：把 `yoloLayout` 的 `makeEnumParam` 挪到 `registerParams` 的 `});` 之后 | RED，`DnnDetectNode.cpp run() reads but nothing registers/writes: yoloLayout`。该 `init` 里没有 `registerParam(sp)` |
| 真实 `ObjectCountingNode.cpp`：删掉 `scale` 那一整行 `registerParam(makeDoubleParam(...))` | RED，`scale` |
| 真实 `ObjectCountingNode.cpp`：保留 `makeDoubleParam(QStringLiteral("scale"), ...)` 文本，但不再包在 `registerParam(...)` 里。文件里仍有多处 `registerParam(sp)` | **GREEN** |

所以作者写的两条改坏臂（摘掉 `scale` 登记、把枚举搬出 `registerParams`）在他们实际改的那种文件上成立。P1-8 的历史形状是 `DnnDetectNode`，这条现在会红。

还绿的是第三种形状：`init` 里只要出现过 `registerParam(sp)`，后面丢掉返回值的 `make*Param("键")` 仍算已登记。`ObjectCountingNode::init` 就是这种文件（`src/ObjectCountingNode.cpp:33` 起的 `registerParam(sp)`，以及 `:180-185` 的 `registerParam(makeDoubleParam("scale"...))`）。把计数节点的 `scale` 改成 P1-8 那种逗号表达式，闸是绿的。r15 要求的判据是「键出现在 `registerParam` / `registerParams` 的实参里」。`registerParam(sp)` 这条旁路把实参收成了「函数正文里出现过」。

其余边界，和 r15 同一类，不另编号：

- `::run(` 调出去的函数仍不看。`SendDataNode::doSend` 读 `sendText`、`ModbusNode::openConnection` 读 `portName` / `baudRate` 的说明仍写在闸的注释里。单独一个这样的文件会触发 `regex_degraded`（本机用合成文件看过）；全仓还有别的 `run()` 读键时，这条盲区被总数盖住，仍是绿的。
- letterbox 的整数 pad 与 `parseYoloOutput` 的浮点 pad 仍不在闸里。见第 7 节 P1-7。
- 字符串里成对的 ASCII 括号对深度是净零，不会把 `registerParams` 截断。当前检测节点的布局文案用的是全角括号（`src/DnnDetectNode.cpp:380-381`），不参与深度。不成对的 `)` 会让配对提前结束，后面的键被漏记，表现为假红。当前树是绿的，这条没有爆。
- `body()` 仍取第一处 `::run(` 到其后第一个行首 `}`。`lineA_x` 落在 `ObjectCountingNode::run` 这段正文里，并且经 `sp.name` + `registerParam(sp)` 算作已登记，所以当前是绿，不是「正则没看见下划线所以假装绿」。

`python tools/reason_key_sites.py --check` 本机通过：`lastError=122`，`*Note=41`，`trainStatus=34`，合计 197，与 `include/NodeResultFields.h` 的注释一致。

## 6. 维持 Partial 的判定点

这 8 个文件没有改这些行。

| ID | 抽查 |
| --- | --- |
| P1-1 | pepper `src/SchemePackage.cpp:14`；裸 JSON 附注 `src/ProjectManager.cpp:307`；`applyProjectJson` 不闸 `:416-424` |
| P2-1 | `kHashIterations` 60000，`src/AppDatabase.cpp:51` |
| P2-3 | `src/GlobalTriggerManager.cpp:176` |
| P2-5 | 语言菜单 `src/MainWindow.cpp:1159-1168`。本轮新句子（打包警告、标注失败文案）是 `QStringLiteral` |
| P2-6 | `MainWindow.cpp` 4656 行，`FlowExecutor.cpp` 2312 行；`src/NodeRegistry.cpp` 仍同时登记 Halcon 与 OpenCV 节点 |

## 7. 已 Fixed 项的回归抽查

`0bd4563` 没有改 `FlowExecutor.cpp`、`ProjectManager.cpp`、`AppDatabase.cpp`、`SchemePackage.cpp`、`DnnDetectNode.cpp`、`ObjectCountingNode.cpp`、`TrainingRunnerDialog.cpp`、`tools/deploy.ps1`、`docs/用户操作手册.md`。抽查落点：

| 项 | 结果 |
| --- | --- |
| P1-6 同场景快路径 | `src/FlowExecutor.cpp:333-346` 仍在清缓存之前返回 |
| P2-10 保存锚 | `src/ProjectManager.cpp:146-166` 仍是失败即返回，成功后才 `setLastFilePath` |
| 写闸 / `canRunFlow` | `src/SessionManager.cpp:71-76`。打包菜单见 P2-13，不把本条打回 Partial |
| 轮次中止 | `m_roundAborted` 时不写汇总行（`src/FlowExecutor.cpp:829-839`） |
| UTC | `utcBindText` 走 `toUTC()`（`src/AppDatabase.cpp:29-31`），读回 `setTimeSpec(Qt::UTC)`（`:40`） |
| 轮首 `onRoundStart` | `src/FlowExecutor.cpp:1993-1998` 仍调用。INV4 随 wiring 脚本本机为绿 |
| P1-7 | `scale` / `swapRB` / `mean` 仍在 `ObjectCountingNode::init` 登记（`src/ObjectCountingNode.cpp:180-185`）。`run` 入口把 `moduleStatus` 写成假（`:207-208`）。`yoloBlob` 仍拒绝 `scale <= 0`（`src/DnnDetectNode.cpp:48-51`），letterbox 仍是整数左上角（`:60-65`），反算仍是浮点 pad（`:162-165`）。余量不单列 |
| P1-8 | `registerParams` 的 `});` 仍在 `yoloLayout` 之后（`src/DnnDetectNode.cpp:242-248`）。面板槽位冻结值仍是 277（`tests/param_panel_binding_test.cpp:2444`） |
| P2-11 | `classWarn` 用最大 `classId + 1`（`src/ObjectCountingNode.cpp:317-328`），成功路径拼在累计句后面（`:415-421`），没有再覆盖成另一句 |
| P2-14 | 检测节点在取网络之前加 `dnnMutex`（`src/DnnDetectNode.cpp:271`）。`getOrLoadNet` 自己不加锁（`:81-96`）。计数节点同样在取网络前加锁（`src/ObjectCountingNode.cpp:259`） |
| P2-15 | `tools/deploy.ps1:355`、`:592-595`，`docs/用户操作手册.md:71`、`:453`、`:515` 仍是出厂 `admin / admin`、立刻改密、改密前写闸。播种仍在 `src/AppDatabase.cpp:427-428` |
| P3-1 | `onProbeEnv` 在 `QFutureWatcher::isRunning()` 时返回，探测放进 `QtConcurrent::run`（`src/TrainingRunnerDialog.cpp:216-220`） |
| P1-7 的 `moduleStatus` | 检测节点 `run` 入口写成假（`src/DnnDetectNode.cpp:257`），走到框结果才写成真（`:357`） |

没有发现已 Fixed 项回退。

## 8. 建议顺序

1. **P2-13 的菜单生命周期。** 在「运行态锁」发生变化的每条界面槽上重算 `m_packModelsAct`，判据继续用当时的 `allowsGraphEditing()`，不要只挂 `executionStarted` / `executionStopped`。正常结束走 `executionFinished` 然后 Idle，不走 `executionStopped`（`src/FlowExecutor.cpp:848-902` 对比 `:633`）。`executionParked` / `executionResumed` 同样要算，否则停稳暂停时画布和菜单仍是两套可用态（`:751-760` 对比 `:4234-4237`）。目录对话框返回之后、调用 `pack` 之前再查一次写闸和 `allowsGraphEditing()`（现在的复查在 `:4079`，`pack` 在 `:4092`）。
2. **INV5 的 `registerParam(sp)` 旁路。** 有 `registerParam(sp)` 时只承认 `sp.name = QStringLiteral("键")`，不要把同一 `init` 里所有 `make*Param` 都算进实参。改坏臂用现在的计数节点：去掉 `scale` 外面的 `registerParam(...)`、留下 `makeDoubleParam("scale")`，应当 RED。`DnnDetectNode` 上「搬出 `yoloLayout`」已经是 RED，保持即可。
3. **P2-13 回滚的返回值。** `QFile::remove` 失败要留在 `r.error` 里，避免下一轮静默 `_2`。补一条复制中途失败的打包用例。
4. **P2-12 余量，可选。** 删除 / 清空在 `saveCurrentImage` 返回假时把内存里的框恢复成保存前的内容，或者至少加一条失败用例。不恢复也不构成 r15 那个「成功文案」洞。
5. **维持。** pepper、裸 JSON、`applyProjectJson` 不闸（P1-1）；PBKDF2 60000（P2-1）；前缀匹配（P2-3）；英文界面与 `QStringLiteral`（P2-5）；双栈与两个大文件（P2-6，`MainWindow.cpp` 本轮 4637→4656）；letterbox 整数 pad 与浮点反算（P1-7 余量）；`getOrLoadNet` 不加锁（P2-14 余量）；探测进行中关窗后线程池仍可能再等（P3-1 余量）。

## 9. 测试在仓库里，本机没有执行

云端不能跑 Windows CTest。下面这些名字只说明仓库里有对应槽。提交说明里的「改坏臂实测两条都红」「两个改坏臂均 RED」不是本虚拟机的 CTest 结果。INV5 的 RED/GREEN 是本机对 `wiring_checks.py` 的直接调用，见第 5 节。

| 套件 | 与本轮有关的契约 | 缺口 |
| --- | --- | --- |
| `tests/ui_guards_test.cpp` | `annotationDialogDoesNotTruncateOnOpen`：打开后第一张 txt 字节不变。`annotationDialogSwitchSavesToPreviousImage`：画一个框再切到第 1 张，第 0 张变成 2 行，第 1 张的 txt 不被创建 | 不预置第 1 张已有 txt。不覆盖保存失败、删除 / 清空 / 换目录。本机未跑 |
| `tests/project_packager_test.cpp` | 本轮未改断言 | 没有复制失败回滚，没有菜单与 `executionFinished` |
| `tests/object_counting_test.cpp` | 本轮未改。`dnnParamsAreRegistered` 仍在 `:264` | 不负责 INV5 的 `registerParam(sp)` 旁路 |
| `tests/param_panel_binding_test.cpp` | `fieldSlots == 277`（`:2444`）仍在 | 本机未跑 |
| `tools/wiring_checks.py` | 本机 GREEN | 见第 5 节：P1-8 的 `DnnDetectNode` 形态会红；同一形态放进带 `registerParam(sp)` 的 `init` 会绿 |
| `tools/reason_key_sites.py --check` | 本机 OK，197 与头文件一致 | — |

有 Qt 6、HALCON、MVS、MSVC 的 Windows 机器上，至少应跑 `ui_guards_test`、`project_packager_test`、`object_counting_test`、`param_panel_binding_test`。本复核不能说它们通过。

## 10. 给协调人（可转发）

- 审核对象：`main@26cc44b`（`26cc44b069f534b3e9cae7899023b85a43ea2882`）。代码行为在 `0bd4563`；HEAD 只追加 `rc-20261029.8` 登记行。相对 r15 基线 `95fb226`：**2 个提交，8 个文件，+222 / −29**。登记的当前装机包是 `rc-20261029.8` / 提交 `0bd4563` / md5 `c6ab05aed0059cdd81b5dec76168806a` / 10128384 字节。本机没有该 exe。
- 账本：**22 Fixed，6 Partial，0 Open，1 Withdrawn**。P1-9 关闭。P2-12 关闭。P2-13 维持 Partial。没有已 Fixed 项回退。P1-1、P2-1、P2-3、P2-5、P2-6 仍是 Partial。没有新编号。
- **P1-9：** `saveCurrentImage` 写 `m_currentPath`（`src/BoxAnnotatorDialog.cpp:341`）。`reloadImages` 在 `setCurrentRow(0)` 之前把路径清掉（`:243-248`），空路径不调用 `YoloAnnotation::save`，第一张已有 txt 不会被截断。切换时框写回上一张。两条用例在 `tests/ui_guards_test.cpp:302-337`。本机未跑。
- **P2-12：** 删除、清空、换目录都看保存返回值（`:214-215`、`:395-400`、`:409-412`）。失败不再用成功文案。余量是内存先改、失败不回滚画布。
- **P2-13：** 复制失败会删除本轮已复制的文件（`src/ProjectPackager.cpp:116-123`）。打包菜单在 `onExecutionStarted` 里关掉（`src/MainWindow.cpp:3200`），正常跑完只发 `executionFinished` 并进入 Idle（`src/FlowExecutor.cpp:848-902`），不发 `executionStopped`，菜单不再打开，画布锁在 `onExecutionFinished` 里已经解开（`src/MainWindow.cpp:3212-3218`）。槽复查在选目录之前（`:4079-4087`），外部触发可以在对话框期间把流程跑起来再被 `pack` 改写路径。
- **INV5** 本机为绿。把 `DnnDetectNode` 的 `yoloLayout` 搬出 `registerParams` 会红。`ObjectCountingNode` 里若仍有 `registerParam(sp)`，丢掉返回值的 `makeDoubleParam("scale")` 仍是绿。摘掉整行 `scale` 登记会红。
- 静态复核，Windows CTest 没跑（缺 Qt 6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

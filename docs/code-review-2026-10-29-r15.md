# Vision Workbench 复核 — 2026-10-29 r15

接 r1–r14 笔记。本轮审的是 `origin/main` 的 `95fb226ef48e2ca0870c40db1cda315e5a5f224c`（发版登记 `rc-20261029.7`，登记的装机包提交是 `ff8980228ac1854537f8613a6e0b9adb4b7bcd8b`）。相对 r14 基线 `f2807849e618bde2eca1f743e647826ce87c4e93`：**6 个提交，20 个文件，+423 / −79**。

本轮没有编译，也没有跑 CTest。云端虚拟机没有 Windows 套件所需的 Qt 6、HALCON、MVS、MSVC。下面凡是提到测试，只说明作者写进仓库的契约，不是本机跑出来的结果。`python tools/wiring_checks.py` 与 `python tools/reason_key_sites.py --check` 在本机跑过，这两条不依赖那套 Windows 工具链。

产品代码没有改。本文件是这一轮唯一的变更。

上一轮草案的结论在 r14 笔记里（基线 `f280784`，账本 14 Fixed / 5 Partial / 8 Open / 1 Withdrawn）。

## 0. 结论先说

r14 的 8 条 Open 里，6 条按作者说的修法对上了源码，可以关：P1-7、P1-8、P2-11、P2-14、P2-15、P3-1。P2-12、P2-13 只完成了一半，从 Open 收到 Partial。已 Fixed 的抽查（P1-6 同场景快路径、P2-10 保存锚、写闸、轮次中止、UTC）没有回退。P1-1、P2-1、P2-3、P2-5、P2-6 本轮 diff 没有动它们的判定点，维持 Partial。

新的一条是 P1-9：标注对话框「切换即存」写的是**已经切过去的那张图**。打开数据集时，第一张图如果已有 txt，会先被截成空文件再读回来；切到下一张时，上一张的框写进下一张的 txt。这不是 r15 新引进的形状（`f280784` 上 `onImageChanged` 已经是先 `saveCurrentImage()` 再 `loadCurrentImage()`），r14 没单列。本轮为了核 P2-12 把这条路径读完，失败分支是 r15 新加的，它仍然调用同一个 `saveCurrentImage()`，所以磁盘满时截断发生在下一张图上，然后选择才退回去。

INV5 能抓住 P1-7 那种「`run()` 里读了、`init()` 正文里根本没有这个键」。它抓不住 P1-8 那种「`makeEnumParam` 写在 `registerParams` 的 `});` 外面、返回值被丢掉」：旧树上同一套正则把 `dnnBackend` / `yoloLayout` 算成已登记。它也不看 letterbox。

| 桶 | 本轮 |
| --- | --- |
| Fixed（20） | 原 14 条不变，加上 **P1-7**、**P1-8**、**P2-11**、**P2-14**、**P2-15**、**P3-1** |
| Partial（7） | P1-1、P2-1、P2-3、P2-5、P2-6，加上 **P2-12**、**P2-13**（这两条从 Open 移入） |
| Open（1） | **P1-9**（新） |
| Withdrawn（1） | P1-4 |

`MainWindow.cpp` **4637** 行（r14：4631）。`FlowExecutor.cpp` **2312** 行（r14 同数）。`src/` 与 `include/` 下仍无 `qFatal(`。`MainWindow.cpp` 里 `tr(` 按字面计 **206** 处；本轮 diff 没有新增 `tr(` 调用，和 r14 笔记里的 200 对不上的是计数口径，不是这 6 个提交把英文界面又写回去了。

静态复核，Windows CTest 没跑。

### 逐条（可转发）

- **P1-7 关。** `ObjectCountingNode::init` 登记 `scale = 1/255`、`swapRB = true`、`mean = "0,0,0"`（`src/ObjectCountingNode.cpp:177-185`）。`run` 改走 `DnnDetectNode::yoloBlob`（`:291-295`），`scale <= 0` 或尺寸非法直接返回假（`src/DnnDetectNode.cpp:48-51`）。两个节点的 `run` 开头都把 `moduleStatus` 写成假（`src/ObjectCountingNode.cpp:207-208`，`src/DnnDetectNode.cpp:257`），前处理失败只写 `lastError` 就返回，不再是「成功且累计 0」。letterbox 与检测节点是同一函数，不再把整幅图拉伸进正方形。余量：放置用整数左上角 `(inputSize - nw) / 2`（`src/DnnDetectNode.cpp:61-65`），`parseYoloOutput` 反算用浮点 `(inputSize - 原边 * scale) / 2`（`:162-165`）。抽查最大差约 0.75 个网络像素，不是 r14 那种整幅拉伸。现有用例查灰边角点，不把框再反算回去。
- **P1-8 关。** `dnnBackend` 与 `yoloLayout` 在 `registerParams({...})` 的 `});` 之前（`src/DnnDetectNode.cpp:242-248`）。面板槽位冻结值 272→277（`tests/param_panel_binding_test.cpp:2443-2445`），+5 对得上计数节点新登记的 3 项加上检测节点这 2 项。`tests/object_counting_test.cpp:264` 起的 `dnnParamsAreRegistered` 看的是 `paramSpecs()`，不再只看源码里有没有这两行字。
- **P2-11 关。** 告警用框上最大 `classId + 1` 对 `classNames.size()`，拼在累计摘要后面（`src/ObjectCountingNode.cpp:317-328`、`:415-421`）。成功返回不再覆盖它。文案仍写「模型输出 N 类」，N 是最大 id 加一，不是张量里的类别维；这是 r14 写明的改法。
- **P2-12 改 Partial，不关。** 关窗和 Esc 在 `save` 返回假时走 `confirmDestructive`，默认按钮是「取消」（`src/BoxAnnotatorDialog.cpp:245-268`，`include/ConfirmDestructive.h:18-22`）。切图失败会把列表行设回 `m_lastRow`（`:284-289`）。删除、清空、换数据集目录仍不看返回值，删除和清空随后用成功文案盖掉失败状态（`:214`、`:379`、`:388`）。切图写错文件，见 P1-9。
- **P2-13 改 Partial，不关。** 参数改成两段式：复制失败在 `setParam` 之前返回（`src/ProjectPackager.cpp:113-127`），半套路径改写没有了。已经拷进 `assets/` 的文件不删，下次打包会因为同名把新文件加成 `_2`。写闸这一半生效：`canEditScheme()` 本身要求写闸未开（`src/SessionManager.cpp:66-68`），`applyPermissionRestrictions` 在登录和改密路径上会把菜单关掉（`src/MainWindow.cpp:4216-4218`）。运行中这一半没有生效：该函数不在 `onExecutionStarted` / `onExecutionStopped` 里调用（`:3190-3198` 只更新画布锁）。菜单表达式传的是 `QThread::isRunning()`，画布锁用的是 `allowsGraphEditing()`（`:3353-3363`）。触发槽（`:4072-4078`）内部不再查闸。
- **P2-14 关。** `DnnDetectNode::dnnMutex()` 是函数内静态 `QMutex`（`src/DnnDetectNode.cpp:39-43`）。检测与计数都在取网络之前加锁，`forward` 和 `parseYoloOutput` 都在锁内（检测 `:271-316`，计数 `:259-329`，计数的锁在 `try` 结束时放开，跟踪在锁外）。`getOrLoadNet` 自己不加锁（`:81-96`）；当前只有这两处调用，都加了锁。锁是全模型一把，检测节点会把画框也放在锁里。
- **P2-15 关。** `tools/deploy.ps1:355-356` 的部署说明、`:592-597` 的版本说明、`docs/用户操作手册.md:71`、`:453`、`:515` 都是出厂 `admin / admin`、立刻改密、改密前写闸。版本说明的角色行是 UTF-8 中文，不再是字面 `\uXXXX`。`src/AppDatabase.cpp:418-428` 仍在空用户表播种 `admin` / `createPasswordHash("admin")`。仓库里「无默认口令」只剩路线图对这条旧缺陷的转述（`docs/VisionMaster4.4对标分析与路线图.md:400`）。
- **P3-1 关。** `onProbeEnv` 把 `TrainingRunner::probe` 放进 `QtConcurrent::run`（`src/TrainingRunnerDialog.cpp:216-246`）。`probe` 在调用线程上建局部 `QProcess` 再 `waitForFinished`（`src/TrainingRunner.cpp:35-47`），不碰对话框里的成员进程。`CMakeLists.txt:70` 与 `:637` 链接了 `Qt6::Concurrent`。关窗仍只在训练进程 `running()` 时拒绝关闭（`:357-368`），不在界面线程上等探测。余量：探测进行中关掉对话框，线程池里的 `probe` 仍会等到超时（最长 20 秒），lambda 只捕获了 python 路径，不会再碰已析构的窗口。
- **P1-9 开。** 见第 4 节。`src/BoxAnnotatorDialog.cpp:240-242`、`:271-298`、`:323-338`；`include/YoloAnnotation.h:105`。
- **P1-1、P2-1、P2-3、P2-5、P2-6 仍 Partial。** 判定点不在这 20 个文件里，抽查行号仍在：pepper `src/SchemePackage.cpp:14`，裸 JSON 附注 `src/ProjectManager.cpp:307`，`applyProjectJson` 不闸 `:416-424`；PBKDF2 60000 `src/AppDatabase.cpp:51`；字符串触发前缀 `src/GlobalTriggerManager.cpp:176`；语言菜单 `src/MainWindow.cpp:1159-1168`，新节点标签仍是 `QStringLiteral`；两个大文件与双栈注册表还在。
- **INV5。** 对 P1-7 的「读了但 init 文本里没有这个键」会红。对 P1-8 的被丢掉的逗号表达式会绿。见第 5 节。

## 1. 相对 `f280784` 的变化量

| 项 | 值 |
| --- | --- |
| r14 基线 | `f2807849e618bde2eca1f743e647826ce87c4e93` |
| 本轮 HEAD | `95fb226ef48e2ca0870c40db1cda315e5a5f224c` |
| 代码提交 | `ff8980228ac1854537f8613a6e0b9adb4b7bcd8b`（HEAD 只在它后面加了一行登记） |
| 提交 | 6 |
| 文件 | 20 |
| 增 / 删 | +423 / −79 |
| 登记的当前装机包 | `docs/release-registry.json` 末行 `rc-20261029.7`，提交 `ff89802`，exe md5 `b57054591d1466ce68b3380b997bc2fa`，10127360 字节 |

HEAD 比装机包提交多一个登记提交（`95fb226` 只改登记表，把 `rc-20261029.6` 标成已被取代）。代码行为以 `ff89802` 为准。`ff89802..HEAD` 的 diff 只有 `docs/release-registry.json`。

| 提交 | 本轮核对 |
| --- | --- |
| `70fc30b` | 版本说明角色行从字面 `\uXXXX` 改回 UTF-8 中文。训练强杀腿加了 `>= 4500 ms` 和日志里的「强制结束」。本机未跑该测试。 |
| `b193ceb` | 登记 `rc-20261029.6`，并写明 `.5` 的「角色行已恢复」与交付字节不符。 |
| `e6ac3ec` | 登记表 `.5` 更正说明的反斜杠层数；`OpencvBridgeTest` 的「8 腿」改成「6 条用例（Totals 8 passed）」。文档口径，没有产品行为。 |
| `e81d51a` | r14 的 2 个 P1 与 6 项 P2/P3 的代码改动。见第 3、4 节。 |
| `ff89802` | `tools/wiring_checks.py` 的 INV5，以及路线图第 8 节把上述改动记成已修。见第 5 节。 |
| `95fb226` | 登记 `rc-20261029.7`。 |

## 2. 账本

| ID | r14 | r15 | `95fb226` 上的证据 |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `canRunFlow()` 仍是已登录且写闸未开（`src/SessionManager.cpp:71-76`）。空用户表仍播种 `admin` / `createPasswordHash("admin")`（`src/AppDatabase.cpp:418-428`）。打包菜单的写闸一半见 P2-13，不把 P0-1 打回 Partial。 |
| P1-1 | Partial | **Partial** | `kPlainPepper` 仍是 `VisionFlow-plaintext-envelope-MAC-1`（`src/SchemePackage.cpp:14`）。裸 JSON 成功路径仍把 `m_lastIntegrityNote` 设为 `旧明文 JSON 格式`（`src/ProjectManager.cpp:307`）。`applyProjectJson` 仍只建场景再套全局配置，不调用 `confirmOutsideSchemePaths`（`:416-424`）。本轮 diff 未触及这些文件。 |
| P1-2 / N1 | Fixed | **Fixed** | 加密导入仍在轮数不等于 `kIterationsValid` 时于 `deriveKeys` 之前返回（`src/SchemePackage.cpp:142-143`）。 |
| P1-3 | Fixed | **Fixed** | UDP 在 `Missing` 且 `localPort > 0` 时拒绝绑定（`src/UdpCommNode.cpp:57-64`）。本轮 diff 未触及。 |
| P1-4 | Withdrawn | **Withdrawn** | 未重开。 |
| P1-5 | Fixed | **Fixed** | 本轮 diff 未触及 `README.md`。 |
| P1-6 | Fixed | **Fixed** | 同场景快路径仍在清缓存之前返回（`src/FlowExecutor.cpp:333-346`）。`FlowExecutor.cpp` 不在这 20 个文件里。 |
| P2-1 | Partial | **Partial** | `kHashIterations` 仍是 60000（`src/AppDatabase.cpp:51`）。 |
| P2-2 | Fixed | **Fixed** | 方案 AES / PBKDF2 仍走 Windows CNG（`src/SchemeCrypto.cpp` 的 `BCrypt*`）。本轮 diff 未触及。 |
| P2-3 | Partial | **Partial** | 字符串触发仍是相等或 `text.startsWith(entry.triggerSource)`（`src/GlobalTriggerManager.cpp:176`）。 |
| P2-4 | Fixed | **Fixed** | `~FlowExecutor` 仍是 `protected`（`include/FlowExecutor.h:213-224`）。 |
| P2-5 | Partial | **Partial** | 语言菜单仍在（`src/MainWindow.cpp:1159-1168`）。本轮新增界面字符串是 `QStringLiteral`。 |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4637 行，`FlowExecutor.cpp` 2312 行。`src/NodeRegistry.cpp` 仍同时包含 Halcon 与 OpenCV 节点。 |
| P2-7 | Fixed | **Fixed** | `docs/security-development-guide.md` 开头仍是模板横幅。 |
| P2-8 | Fixed | **Fixed** | 三份文件仍在 `docs/归档/`，仓库根目录没有它们。 |
| P2-9 | Fixed | **Fixed** | 本轮 diff 未触及 `ProjectManager.cpp` 的加载回滚。 |
| P2-10 | Fixed | **Fixed** | `saveProject` 在 `open` 失败、短写、`commit` 失败时直接返回（`src/ProjectManager.cpp:146-161`）。`setLastFilePath` 仍在 `commit` 成功之后（`:164-166`）。 |
| N2 | Fixed | **Fixed** | 本轮 diff 未触及 `docs/ci-cd-guide.md`。 |
| N3 | Fixed | **Fixed** | 本轮 diff 未触及 MAC 比较。 |
| P1-7 | Open | **Fixed** | 见第 3 节。 |
| P1-8 | Open | **Fixed** | 见第 3 节。 |
| P2-11 | Open | **Fixed** | 见第 3 节。 |
| P2-12 | Open | **Partial** | 见第 3 节与 P1-9。 |
| P2-13 | Open | **Partial** | 见第 3 节。 |
| P2-14 | Open | **Fixed** | 见第 3 节。 |
| P2-15 | Open | **Fixed** | 见第 3 节。 |
| P3-1 | Open | **Fixed** | 见第 3 节。 |
| P1-9 | — | **Open** | 标注切图写错文件，打开数据集会截断第一张已有 txt。见第 4 节。 |

## 3. r14 的 Open：关闭或残留

### P1-7 — 关

计数节点补了三项登记，缺省不再是 `toDouble()` 的 0。`run` 里的 `value(..., 1.0/255.0)` 是第二道缺省（`src/ObjectCountingNode.cpp:276-285`），真正挡住全零 blob 的是 `yoloBlob` 对 `scale > 0` 的拒绝，以及 `registerParam` 把 `1/255` 写进 `m_params`（`src/HalconNode.cpp:810-815` 仍是「有缺省且键不存在才写入」）。

几何改到与检测节点共用 `yoloBlob`：灰边 114、按长宽比缩进正方形，再 `blobFromImage`。计数节点把同一张图的 `cols/rows` 交给 `parseYoloOutput`（`src/ObjectCountingNode.cpp:302-303`）。r14 的两半（全零 blob、拉伸对 letterbox 反算）都不再是那条旧路径。

`moduleStatus` 在 `run` 入口写成假，只有走到累计摘要才写成真（计数 `:422`，检测 `:357`）。前处理失败留下 `lastError`，模块状态保持假。

余量不单列编号：放置用 `cvRound` 后的整数边长和整数除法左上角，反算用未取整的浮点 pad。对 640 输入、宽高从 32 扫到约 2000，两者之差最大约 0.75 个网络像素（例如 214×1286）。这是检测节点原来就有的取整，现在两节点共享。`tests/object_counting_test.cpp:300` 的 `yoloBlobRejectsZeroScaleAndLettersboxes` 拒绝 `scale == 0`，并检查 64×48 放进 64×64 时左上角接近 `114/255`。它不调用 `parseYoloOutput`，所以锁不住「反算用的 pad 等于放置用的 pad」。

### P1-8 — 关

`registerParams` 的初始化列表在 `yoloLayout` 那一项之后才 `});`（`src/DnnDetectNode.cpp:242-248`）。r14 看到的被丢掉的逗号表达式不在了。`dnnParamsAreRegistered` 对检测节点断言 `paramSpecs()` 含这两项（`tests/object_counting_test.cpp:288-297`），对计数节点断言 `scale > 0`。槽位注释写明 277 含「P1-8 修好后真正上面板」的两项。本机没跑这个测试；静态对得上的是 +3（计数的 scale/swapRB/mean）+2（检测的两项）= 272→277。

### P2-11 — 关

`classWarn` 在成功路径拼到累计句后面，中间没有再赋一次 `countNote`。异常路径在写摘要之前返回，告警不会单独变成成功句。最大 id 来自全部框，不只是没名字的框；没名字且 `classId >= 0` 才计入 `unnamed`。这和 r14 第 6 节第 3 条一致。

### P2-12 — Partial

关窗与 Esc：`saveCurrentImage` 返回假且操作员不确认破坏性关闭时，`closeEvent` 忽略事件，`reject` 不调用 `QDialog::reject()`。`confirmDestructive` 的默认按钮是「取消」（`include/ConfirmDestructive.h:20-22`）。保存失败时状态栏有路径（`src/BoxAnnotatorDialog.cpp:334-336`）。

还没接上的：

- `onDeleteBox`（`:379`）和 `onClearImage`（`:388`）丢掉布尔值，下一行把状态改成「已删除」或「已清空」。
- `onChooseDir`（`:214`）丢掉布尔值，然后换目录。
- 切图看了布尔值，但保存的路径是新行。见 P1-9。所以「存失败就不换图」只在 `YoloAnnotation::save` 返回假时成立；返回真时，框已经写到下一张图上。

### P2-13 — Partial

两段式成立。`PendingRewrite` 在循环里只追加（`:84`、`:99`、`:118`），`QFile::copy` 失败立刻 `return r`（`:113-115`），此时还没执行 `:123-127` 的 `setParam`。`r.ok` 保持默认假。同源和「已在方案内」的改写也放在同一批里，失败时相对路径也不会先写进去。稳定排序仍在 `:51-55`。

残留：

- 失败返回不删除本轮已经复制的文件。目标名用 `while (QFile::exists)` 避开已有文件（`:107-112`），遗留文件会把下一轮成功的拷贝挤成 `name_2.ext`。方案参数在失败时仍指向原路径，所以这不是半套改写，是磁盘上的孤儿文件。
- 菜单与写闸：未登录直接禁用（`:4194-4195`）。已登录时 `canEditScheme() && !canvasEditShouldBeLocked(m_executor && m_executor->isRunning())`（`:4218`）。`canEditScheme` 已含 `!writesBlocked()`（`src/SessionManager.cpp:66-68`），所以出厂口令仍在用时，只要走过 `applyPermissionRestrictions`，菜单是灰的。调用点是登录（`src/MainWindow.cpp:636`）和改密策略（`:4259`、`:4264`、`:4315`、`:4323`），没有执行开始/停止。
- 流程跑起来之后画布锁会更新（`onExecutionStarted` → `updateEditLockForCurrentScene`，`:3198`、`:3362-3363`，判据是 `!allowsGraphEditing()`），打包菜单的 `setEnabled` 不会跟着走。登录时执行器通常没在跑，菜单被打开，之后一直保持可点。槽函数（`:4072-4078`）直接 `ProjectPackager::pack(currentFlowScene(), ...)`，不再看写闸或运行态。`HalconNode::setParam`（`src/HalconNode.cpp:143`）仍不读编辑锁。按 P0-1，跑着的流程不会被停掉，下一轮会按新路径解析。这是 r14 要关掉的「画布锁着时不可用」那一半。
- 即便补上刷新，谓词传进 `canvasEditShouldBeLocked` 的是 `QThread::isRunning()`，不是 `!allowsGraphEditing()`。线程在暂停等待里仍是 `isRunning()==true`，而已停稳的暂停按 `:459-468` 允许改图。两套判据不是同一个布尔。

`tests/project_packager_test.cpp` 仍不覆盖复制中途失败，也不覆盖菜单启用。本机未跑。

### P2-14 — 关

两台执行器对同一份 `cv::dnn::Net` 实现的 `setPreferableTarget` / `forward` 现在串行。计数节点在解析完框、把结果放进 `QVector<DetectionBox>` 之后才出 `try`，锁随 `QMutexLocker` 释放；检测节点的锁一直持有到 `try` 末尾，画框也在锁里。解析在锁内完成，`forward` 返回的 `cv::Mat` 不会在另一线程再次 `forward` 时被读。

`getOrLoadNet` 仍直接碰静态 `QMap`。今天只有上述两处调用。以后若有第三处不加这把锁，竞态会回来。锁不是按模型路径拆开的：不同 onnx 也不能并行 `forward`。这是正确性修复的范围，不是新的数据竞争。

`QMutex` 默认非递归。`getOrLoadNet` 和 `yoloBlob` 不再加同一把锁，当前调用链没有重入。

### P2-15 — 关

三份客户能看到的句子对齐为：存在出厂账号 `admin / admin`，登录后立刻改密，改密前写闸挡住画布和保存。`版本说明.txt` 里印出口令仍是 r14 记下的交付选择；`deploy.ps1:589-591` 的注释仍告诉打包者，包若离开现场，把这几行改成「初始口令见交付单」。本轮不要求删掉口令行。

`70fc30b` 把角色行从 Python 式 `\uXXXX` 改回中文。`95fb226` 登记的装机包提交是 `ff89802`，其祖先包含 `70fc30b`，所以登记指向的代码树里角色行已经是中文。本机没有那个 exe，没有对 md5 `b5705459` 做字节核对。

### P3-1 — 关

界面线程不再调用 `TrainingRunner::probe`。重复点击在 `QFutureWatcher::isRunning()` 时直接返回（`src/TrainingRunnerDialog.cpp:220-221`）。探测期间按钮禁用；`finished` 槽在界面线程写标签、`m_envOk` 和设置。`onStart` 在环境未确认时只发起探测并弹出说明，不再同步等结果（`:252-259`）。

`probe` 的 `QProcess` 是栈上对象，建在执行 lambda 的线程池线程上，用完即毁。没有把对话框拥有的 `TrainingRunner::m_proc` 挪到别的线程。`EnvStatus` 含 `QString`，按值从 `QFuture` 取回；`finished` 信号本身不携带该类型。

本机不能启动 Windows 上的 python 探测，也不能观察线程池线程里 `QProcess::waitForFinished` 的实际行为。静态上，r14 的「界面停最多 20 秒」不在这条调用栈上了。

## 4. 新发现

### P1-9 — 标注切换保存的是新图的路径，打开目录会截断第一张已有标注

| 字段 | 内容 |
| --- | --- |
| 严重度 | P1 |
| 状态 | **Open** |
| 路径 | `src/BoxAnnotatorDialog.cpp:167`、`:209-217`、`:240-242`、`:271-298`、`:323-338`；`include/YoloAnnotation.h:102-117` |

`QListWidget::currentRowChanged` 在当前行已经变成新行之后才进槽（连接在 `:167`）。`saveCurrentImage` 用 `currentImagePath()`，而那个函数读的就是 `m_imageList->currentRow()`（`:271-276`）。`m_boxes` 要等后面的 `loadCurrentImage` 才换成新图的框。于是「切换即存」把**上一张的框**写进**下一张的 txt**。

打开数据集走同一条槽。`reloadImages` 在 `m_loading = false` 之后 `setCurrentRow(0)`（`:240-242`）。此时 `m_boxes` 仍是空的（构造后第一次），或者仍是上一套数据集的框（`onChooseDir` 在 `:214` 保存的是旧目录的当前行，保存不清 `m_boxes`，`reloadImages` 也不清）。`YoloAnnotation::save` 用 `QIODevice::Truncate` 打开（`include/YoloAnnotation.h:105`）。

两条现场路径：

1. 第一次选中第 0 张，框是空的，但这张图已经有同名 txt。条件 `!m_boxes.isEmpty() || QFileInfo::exists(...)` 为真（`:330`），空框被写成空文件，然后 `loadCurrentImage` 把这个空文件读回来。操作员看到「已载入 0 个框」，磁盘上的标注已经没了。
2. 在第 0 张上画了框，再点第 1 张。保存发生时当前行已是 1，第 0 张的 txt 不会更新，第 1 张的 txt 被换成第 0 张的框。接着加载第 1 张，画布上仍是刚才那些框，看起来像「跟着走了」。第 1 张原来的标注被覆盖。

r15 的失败分支（`:284-289`）在 `save` 返回假时把列表设回 `m_lastRow`，并用 `m_loading` 挡住重入。它没有改保存用的路径。`Truncate` 在 `open` 成功时就已经发生；若随后 `flush`/`close` 失败，函数返回假，选择退回旧图，**下一张的文件已经被动过**，旧图的框还在内存里。`m_lastRow` 初始是 -1（`include/BoxAnnotatorDialog.h` 的成员），第一次保存失败时退不回任何行，并且不会调用 `loadCurrentImage`。

`f280784` 的 `onImageChanged` 已是「先 save 再 load」，没有 `m_lastRow`。r14 只记了返回值被忽略（P2-12），没有把路径写错单列出来。本轮把它标成新的 Open，不把它算进「作者声称已修但其实没动」——作者修的是返回值，路径问题在修之前就在。

`tests/yolo_annotation_test.cpp` 仍不覆盖对话框。本机未跑。

## 5. INV5 抓住了 P1-7 的哪一半

`tools/wiring_checks.py:276-333` 的 `check_param_registration` 已挂进 `main`（`:359`）。本机对当前树运行 `python tools/wiring_checks.py --repo .`，结果是 `[ELW-VERDICT] GREEN`，`findings=0`。

判据：每个同时有 `::init()` 和 `::run(` 的 `src/*.cpp`，取**第一处** `::run(` 到其后第一个行首 `}` 的正文，抽出 `m_params.value(QStringLiteral("键"))` / `m_params[QStringLiteral("键")]` 里由纯字母组成的键。键必须出现在 `init()` 正文的 `make*Param(QStringLiteral("键")` 或 `sp.name = QStringLiteral("键")` 里，或本文件任意位置有 `m_params[键] =` / `setParamDirect(键)`，或在具名豁免 `{roiCol, roiRow, roiWidth, roiHeight, lastInput}` 里。带缺省的 `value(key, default)` 不算豁免。注释会先剥掉。

用这份正则对 `f280784` 的两个文件重放：

| 树 | `scale` / `swapRB` / `mean` | `dnnBackend` / `yoloLayout` |
| --- | --- | --- |
| `f280784` 的 `ObjectCountingNode.cpp` | 算缺失（会红） | 已在 `sp.name = ...` 里，不缺 |
| `f280784` 的 `DnnDetectNode.cpp` | 已在 `registerParams` 列表里 | **也算已登记**。它们的 `makeEnumParam(...)` 文本在 `init()` 里、`});` 之后，返回值被丢掉，正则不看这个调用有没有被 `registerParams` 接住 |
| 当前树摘掉计数节点三行 `registerParam(make*Param("scale"/"swapRB"/"mean"))` | 再次算缺失 | 不变 |

所以作者写的改坏臂（摘掉 `scale` 登记则红）对 P1-7 成立。INV5 不覆盖：

- P1-8 的形状。旧检测节点在这道闸下是绿的。
- letterbox 与 `parseYoloOutput` 是否同一套 pad。那是 P1-7 的另一半，闸只看键名。
- 键名里有下划线或数字。`lineA_x` 的正则是 `[A-Za-z]+`，匹配不上 `"lineA_x"`。这四项今天是登记过的（`src/ObjectCountingNode.cpp:76-106`）；把登记删掉，INV5 仍绿。
- `run()` 调出去的函数。`SendDataNode::run` 只调用 `doSend()`（`src/SendDataNode.cpp:36-39`），`doSend` 读 `sendText`（`:65`），`init` 只给 `deviceName` 和 `suffix` 赋了值（`:24-25`）。读不在 `::run(` 正文里，闸是绿的。`ModbusNode::openConnection` 读 `portName` / `baudRate`（`src/ModbusNode.cpp:219-221`），同样不在 `::run(` 正文里。这两处是闸的盲区，不是本轮引入的产品缺陷，不另编号。
- 文件里任意一处 `m_params[键] =` 都算「写过」，不要求进 `paramSpecs()`。面板上看不见、但 init 里直接赋过值的键，闸也绿。

`python tools/reason_key_sites.py --check` 本机通过：`lastError=122`，`*Note=41`，`trainStatus=34`，合计 197，与 `include/NodeResultFields.h` 的注释一致。

## 6. 已 Fixed 项的回归抽查

这 6 个提交没有改 `FlowExecutor.cpp`、`ProjectManager.cpp`、`AppDatabase.cpp`、`SchemePackage.cpp`、`UdpCommNode.cpp`。抽查落点：

| 项 | 结果 |
| --- | --- |
| P1-6 同场景快路径 | `src/FlowExecutor.cpp:333-346` 仍在清缓存之前返回 |
| P2-10 保存锚 | `src/ProjectManager.cpp:146-166` 仍是失败即返回，成功后才 `setLastFilePath` |
| 写闸 / `canRunFlow` | `src/SessionManager.cpp:76`。打包菜单见 P2-13，不把本条打回 Partial |
| 轮次中止 | `m_roundAborted` 时不写汇总行（`src/FlowExecutor.cpp:829-839`） |
| UTC | `utcBindText` 走 `toUTC()`，读回 `setTimeSpec(Qt::UTC)`（`src/AppDatabase.cpp:29-41`） |
| 轮首 `onRoundStart` | `src/FlowExecutor.cpp:1993-1998` 仍调用。INV4 随 wiring 脚本本机为绿 |
| 训练强杀三重防护 | `src/TrainingRunner.cpp` 未在 `e81d51a` 里改语义。`start` 停表（`:74-75`），`finished` 停表并 `m_killTarget.clear()`（`:93-95`），定时器认 `QPointer`（`:120-126`）。`70fc30b` 给测试加了耗时下界和「强制结束」日志（`tests/training_runner_test.cpp`）。本机未跑 |

没有发现已 Fixed 项回退。

## 7. 建议顺序

1. **P1-9。** `onImageChanged` 在改当前行之前，用上一张的路径保存 `m_boxes`（`m_lastRow` 或槽外暂存的路径）。`reloadImages` / 第一次 `setCurrentRow(0)` 不要对尚未载入的空 `m_boxes` 调用 `save`。`YoloAnnotation::save` 的 `Truncate` 意味着「打开即破坏」，失败分支也要避免先打开下一张的 txt。补一条对话框级用例：第 0 张已有 txt，打开后字节不变；画一个框再切到第 1 张，框出现在第 0 张的 txt 里，第 1 张的 txt 不变。
2. **P2-13 的运行态一半。** 在 `onExecutionStarted` / `onExecutionStopped` / `executionParked` / `executionResumed` 重算 `m_packModelsAct`，谓词与画布锁一样用 `!allowsGraphEditing()`。槽函数入口再查一次写闸和这把锁。复制失败时删掉本轮已经写进 `assets/` 的文件，避免下一轮被迫 `_2` 后缀。
3. **P2-12 的剩余调用点。** `onDeleteBox`、`onClearImage`、`onChooseDir` 在 `saveCurrentImage` 返回假时停住，不要改成功文案，换目录不要丢掉当前框。
4. **INV5 若还要挡住 P1-8 那种回归。** 登记判据改成「键出现在 `registerParam` / `registerParams` 的实参里」，不能把 `init()` 正文里任意 `make*Param` 文本算登记。键名字符类放进下划线和数字。只扫 `::run(` 本体的限制写进闸的注释里，避免把它读成「所有读参都有登记」。
5. **维持。** pepper、裸 JSON、`applyProjectJson` 不闸（P1-1）；PBKDF2 60000（P2-1）；前缀匹配（P2-3）；英文界面（P2-5）；双栈与两个大文件（P2-6，`MainWindow.cpp` 本轮 4631→4637）；letterbox 整数 pad 与浮点反算差不到 1 个网络像素（P1-7 余量，不单列）；`getOrLoadNet` 不加锁、靠调用方记得加（P2-14 余量）。探测进行中关窗后线程池仍可能再等最多 20 秒（P3-1 余量，不单列）。

## 8. 测试在仓库里，本机没有执行

云端不能跑 Windows CTest。下面这些名字只说明仓库里有对应槽。提交说明里的「counting/panel/pack/ui/smoke 全绿」不是本虚拟机的结果。

| 套件 | 与本轮有关的契约 | 缺口 |
| --- | --- | --- |
| `tests/object_counting_test.cpp` | `dnnParamsAreRegistered`：计数五项登记且 `scale > 0`；检测节点 `paramSpecs` 含 `dnnBackend` / `yoloLayout`。`yoloBlobRejectsZeroScaleAndLettersboxes`：`scale==0` 拒绝，非方图 blob 的角点接近灰边 | 不把 blob 送进 `parseYoloOutput` 再对回原图坐标。不构造「未登记 scale」的运行路径（那条由 INV5 在静态侧看） |
| `tests/param_panel_binding_test.cpp` | `fieldSlots == 277` | 本机未跑。272→277 的算术与源码新增的 5 个参数一致，不能代替在 Windows 上建面板 |
| `tests/project_packager_test.cpp` | 本轮未改断言 | 没有复制失败回滚，没有菜单与运行态 |
| `tests/yolo_annotation_test.cpp` | 本轮未改 | 没有对话框切图 / 打开即截断 |
| `tests/training_runner_test.cpp` | 强杀腿加了 `>= 4500 ms` 和「强制结束」 | 没有探测离开界面线程的腿。本机未跑 |
| `tools/wiring_checks.py` | 本机 GREEN | 见第 5 节的漏报 |
| `tools/reason_key_sites.py --check` | 本机 OK，197 与头文件一致 | — |

有 Qt 6、HALCON、MVS、MSVC 的 Windows 机器上，至少应跑 `object_counting_test`、`param_panel_binding_test`、`project_packager_test`、`yolo_annotation_test`、`training_runner_test`。本复核不能说它们通过。

## 9. 给协调人（可转发）

- 审核对象：`main@95fb226`（`95fb226ef48e2ca0870c40db1cda315e5a5f224c`）。代码行为在 `ff89802`；HEAD 只追加 `rc-20261029.7` 登记行。相对 r14 基线 `f280784`：**6 个提交，20 个文件，+423 / −79**。登记的当前装机包是 `rc-20261029.7` / 提交 `ff89802` / md5 `b5705459…` / 10127360 字节。本机没有该 exe。
- 账本：**20 Fixed，7 Partial，1 Open，1 Withdrawn**。r14 的 8 条 Open：P1-7、P1-8、P2-11、P2-14、P2-15、P3-1 关闭；P2-12、P2-13 改为 Partial。没有已 Fixed 项回退。P1-1、P2-1、P2-3、P2-5、P2-6 仍是 Partial。
- 新 Open：**P1-9**。`BoxAnnotatorDialog::onImageChanged` 在当前行已经变成新图之后才 `saveCurrentImage()`，写的是新图路径、内容是上一张的框（`src/BoxAnnotatorDialog.cpp:279-298` 与 `:323-326`）。`YoloAnnotation::save` 以截断方式打开（`include/YoloAnnotation.h:105`）。打开数据集时 `setCurrentRow(0)` 会把第一张已有 txt 写成空文件（`:240-242`）。这在 `f280784` 上已存在；r15 的失败回退没有改路径。
- P2-13：参数两段式成立（`src/ProjectPackager.cpp:113-127`）。打包菜单的启用状态不随执行开始刷新（`src/MainWindow.cpp:3190-3198` 对比 `:4216-4218`），跑着的流程仍可以打包并 `setParam`。
- INV5 本机为绿，并且对「摘掉 scale 登记」会报缺失。同一正则对 `f280784` 的 `DnnDetectNode::init` 不会报 P1-8：被丢掉的 `makeEnumParam` 仍算登记。闸不看 letterbox。
- 静态复核，Windows CTest 没跑（缺 Qt 6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

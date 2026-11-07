# Vision Workbench 复核 — 2026-10-31 r19

接 r1–r18 笔记。本轮审的是 `origin/main` 的 `7f80530cf69ef42e522bfb045932082580eb571a`（发版登记 `rc-20261030.35`）。相对 r18 基线 `b35588f3cbd7c38b98b6c5f08634a0a6cf4ea508`：**80 个提交**（其中发版登记 33、其余 47），**68 个文件，+3018 / −397**。`git rev-list --count` 与 `git diff --shortstat` 与任务预期一致。

本轮 **未** 跑 Windows CTest / 完整编译（无 Qt6 / HALCON / MVS / MSVC）。纯 Python 闸已跑，见 §6。产品代码未改；本文件是唯一交付物。

两套编号不要混：

- **历史台账**沿 r18：进入本轮 23 Fixed / 5 Partial / 0 Open / 1 Withdrawn。新缺陷从历史空号起编。作者 r19 已占用 P1-10…P1-13，本轮新开的历史 P1 **从 P1-14 起**，故意跳过 10–13，避免同号不同义。
- **作者路线图**一律写作 `roadmap-P0-1`、`roadmap-P1-1` 等。作者的 `roadmap-P1-1` 是「检测落库失败丢批次」，历史 P1-1 仍是明文 pepper。作者提交里的「P2-3（上一帧输出预算）」是 `roadmap-P2-3`，历史 P2-3 仍是触发前缀匹配。

## 0. 结论先说

历史 Partial 五条都还在，没有一条有证据可转 Fixed。已 Fixed 的历史项抽查没有回退。本轮新开 **4 个 P1、3 个 P2、1 个 P3**，全部 Open。

| 桶 | r18 进入 | r19 离开 |
| --- | --- | --- |
| Fixed | 23 | **23**（无新增、无回退） |
| Partial | 5（P1-1、P2-1、P2-3、P2-5、P2-6） | **5**（同一组，无迁移） |
| Open | 0 | **8**（P1-14、P1-15、P1-16、P1-17、P2-14、P2-15、P2-16、P3-2） |
| Withdrawn | 1（P1-4） | **1** |

作者点名的 14 条编号主张（`roadmap-P0-1` 与 `roadmap-P1-1`…`P1-13`，含后续补上的 `roadmap-P1-8`）：**9 Fixed / 5 Partial / 0 Open**。作者说「P2 池已清空」不成立：多数具体项已落地，新鲜度门与全局相机 `StartGrabbing` 仍开着。模板匹配五项补全都进了源码；粗到细位置与多边形过滤在多角度大图上坐标系是错的（历史 P1-14），基准腿只钉了耗时和分数。

`MainWindow.cpp` **4688** 行（r18 为 4674）。`FlowExecutor.cpp` **2453** 行（r18 为 2312）。`MainWindow.cpp` 里 `\btr\s*\(` 仍是 **200**。`tools/i18n_census.py` 本机读数：**169 文件 / 2033 未国际化 UI / 156 非 UI / 245 已 tr()**（r18 文档为 166 / 2012 / 158 / 245；`tr()` 没变，UI 字面量净增）。

## 1. 增量

- 基线：`b35588f3cbd7c38b98b6c5f08634a0a6cf4ea508`
- HEAD：`7f80530cf69ef42e522bfb045932082580eb571a`
- 提交：80（`git rev-list --count b35588f..HEAD`）
- 文件：68；`+3018 / −397`（`git diff --shortstat`）
- 本地快照原停在 `9882494`，落后 `origin/main`；复核前已 `git fetch origin main`。工作分支 `cursor/code-review-r19-51f8` 从更新后的 `origin/main` 拉出。

主线是 r19 修复池（DNN 缓存、落库、库锁、MVS、串口、发送失败门、图像缓存、析构等待）加上模板匹配交互/性能，中间夹着多轮发版登记与复核回补。

## 2. 历史台账

### 2.1 维持 Partial（无迁移）

| 历史 ID | 状态 | 本轮证据 |
| --- | --- | --- |
| P1-1 明文 pepper / `applyProjectJson` | Partial | `src/SchemePackage.cpp:14` 仍是 `kPlainPepper = "VisionFlow-plaintext-envelope-MAC-1"`。`ProjectManager::applyProjectJson` 仍在 `src/ProjectManager.cpp:416`。这两处文件不在本批 68 个文件里。与 `roadmap-P1-1`（落库重试）无关。 |
| P2-1 PBKDF2 非 memory-hard | Partial | `src/AppDatabase.cpp:81` `kHashIterations = 210000` 仍在。全仓 `src/` 无 argon / scrypt。轮数余量在 r18 已收，本体不变。 |
| P2-3 触发前缀在匹配侧 | Partial | `src/GlobalTriggerManager.cpp:176` 仍是 `text == entry.triggerSource \|\| text.startsWith(entry.triggerSource)`。该文件不在本批。不要把作者 `7abac8e` 的输出预算记到这个号上。 |
| P2-5 i18n 产品暂停 + 普查 | Partial | 路线图暂停句未撤。普查已实测（§6）：UI 字面量 2012→2033，`tr()` 仍 245。暂停口径不变，表面还在涨。 |
| P2-6 双栈 / 大文件 | Partial | Qt + HALCON 双栈未动。`MainWindow.cpp` 4674→4688，`FlowExecutor.cpp` 2312→2453。 |

Withdrawn 的历史 P1-4 没有回打开的证据。作者 `roadmap-P1-4` 是 MVS 句柄并发，另一件事。

### 2.2 本轮新开（Open）

#### 历史 P1-14 — 模板匹配粗到细 / 多边形坐标系

多角度、长边 > 640 的图会走粗搜（`src/OpencvTemplateMatchNode.cpp:382-384`，`coarseFactor = maxSide/480`）。现场对照正是这条路径（1500×1000、31 个角度）。

1. 粗搜把 `bestLoc` / `bestCenterInWarp` 记在**降采样像素**里（`:466-471`）。精修只有在全分辨率分数**严格大于**粗搜分数时才改写成全分辨率坐标（`:499-505`）。分数持平或精修略低时，`:519-520` 仍把粗像素当成裁剪后全图坐标去加 `searchRect`。位置会偏一个 `coarseFactor`（约 3 倍）。
2. 多边形点在裁剪后的全分辨率 `gray` 上（`:253-264`）。`evalOne` 用这组点做 `pointPolygonTest`（`:433-448`），但粗搜传入的是降采样的 `graySearch`（`:465`），精修传入的是子窗口 `win`（`:499`）。过滤框与结果图不是同一栅格，多角度大图上多边形要么把合法峰打成无效，要么放过窗外的峰。
3. `trainFromImage` 在搜索区裁剪**之后**抠模板（`:287-289` 对已经裁过的 `gray` 用全图 ROI）。搜索区为 0 时无影响；搜索区非 0 时训练框落点是错的。

`tests/feature_match_test.cpp:642-683` `templateMatchSearchIsFastEnough` 只断言 `< 8s`、`score > 0.9`、坐标非负，并写明几何约定不钉。这条腿盖不住 P1-14。

角度/尺度都是恒等、或长边 ≤ 640 时 `coarseFactor == 1`，精修不跑，多边形与 `gray` 同栅格，上述 1、2 不触发。

#### 历史 P1-15 — 令牌短等返回后场景仍被删

`FlowExecutorToken::~FlowExecutorToken`（`src/FlowExecutorToken.cpp:55-78`）在 GUI 上只 `joinForDestroy(300)`。没停干净就 `detach` 一条后台线程，析构立即返回。头文件仍写收尾是同步的、返回时本体已收干净（`include/FlowExecutorToken.h:22-24`），与实现相反。

`MainWindow::~MainWindow`（`src/MainWindow.cpp:685-688`）在 `token.reset()` 返回后立刻 `qDeleteAll(m_flowScenes)`。关窗路径 `cleanupCameras` 先停执行器并共享等待最多 3 秒（`:1001-1024`）；3 秒仍卡住就跳过相机关闭并继续关窗。随后析构只再给 300ms。工作线程若还停在 `MV_CC_GetOneFrameTimeout` 或节点里，场景和节点会被拆掉。

关流程页是另一条路：`onCloseFlowTab` 先 `wait(6000)`，超时则**不关页**（`:3109-3115`）。问题在主窗口析构，不在关页。

作者要解决的是 GUI 冻 15 秒，短等本身达到了。没把场景寿命延到后台 `join` 结束，所以 `roadmap-P1-13` 记 Partial，洞记在本号。

#### 历史 P1-16 — 发送失败与轮次错位

`CommunicationManager::sendData` 把字节放进队列后立刻返回 true（`src/CommunicationManager.cpp:572-577`，`requestSend` → `Qt::QueuedConnection`）。真正的 `write` 在节点线程之后才发生，失败才 `noteSendWriteFailure`（`src/CommunicationNodeBase.cpp:54-58`）。

轮末在 `src/FlowExecutor.cpp:841-845` 用「当前计数 − 轮初快照」判 NG，然后把快照推到当前值。快照修掉了「一次失败之后轮轮 NG」和跨流程串桶（`40c1307`）。它没有等这次入队的写做完。

- 软触发单轮、且发送节点靠近轮末：记录已经按 OK 落库，失败计数发生在快照推进之后，**这一轮永远不会因此记 NG**。
- 连续模式：失败往往记到**下一轮**。作者在 `a6245ab` 里写过「发送失败错轮」要轮内 ack/排空，当时标成未做；后续只加了快照，没有排空。

门是有的，所以 `roadmap-P1-11` 不是 Open；单轮漏计和错轮还在，所以是 Partial，洞记在本号。

#### 历史 P1-17 — 串口每次打开都再接一层信号

`SerialCommNode::openConnection` 里 `doOpen` 每次成功都 `connect(readyRead)` 和 `connect(errorOccurred)`（`src/SerialCommNode.cpp:195-200`）。`closeConnection` 只 `close()` 端口（`:286-292`），不 `disconnect`。掉线重连、用户再次打开，都会再叠一层。

`readyRead` 槽在 IO 线程里 `emit dataReceived`。重连 N 次后，一帧数据会触发 N 次。`errorOccurred` 同样叠加，会多次走进 `onSerialError`。验收清单把「通信中途断线」列为重点，这条会在自愈之后开始双计。

端口对象已 `moveToThread` 到专属 IO 线程（`:20-27`），`write` 经 `BlockingQueuedConnection` 派回去（`:336-346`）。跨线程使用 `QIODevice` 这一条 UB 收住了，所以 `roadmap-P1-12` 记 Partial 而不是 Open。

#### 历史 P2-14 — 全局相机 StartGrabbing 失败仍报打开成功

本地打开路径已改：`MvsImageSourceNode::openCameraInThread` 在 `MV_CC_StartGrabbing` 失败时关句柄、`cameraStatusChanged(false)` 并 return（`src/MvsImageSourceNode.cpp:762-769`）。

全局相机仍是失败只打日志、照样 `cameraStatusChanged(cameraName, true)` 并 `return true`（`src/GlobalCameraManager.cpp:658-667`）。生产路径优先采用全局句柄（`MvsImageSourceNode::openCamera` `:671`）。本批给 `GlobalCameraManager.cpp` 加了 `sdkMutexFor`，没有改这段成功语义。

#### 历史 P2-15 — 句柄锁只罩住取图

`sdkMutexFor`（`src/GlobalCameraManager.cpp:13-24`）用在 `grabImage` 的软触发与 `GetOneFrameTimeout`（`src/MvsImageSourceNode.cpp:454-457`、`:470-471`、`:484-485`）。两个节点抢同一句柄取图会串行，这是 `roadmap-P1-4` 的主体。

同一句柄上的其它 `MV_CC_*` 不拿这把锁：`readCameraParams` 持 `m_mutex` 直接 `GetFloatValue`（`:618` 起）、`openCamera` 查 `PayloadSize`（`:681`）、`GlobalCameraManager` 的开/关/改参数。取图持锁的那一秒里，界面读曝光仍是并发 SDK 调用。

#### 历史 P2-16 — 关键点缓存的 LRU 顺序表会在并发下重复

`ModelCache::keypointsFor` 在锁外 `detectAndCompute`，再入表（`src/ModelCache.cpp:121-157`）。两个流程同时未命中同一键时，两边都会 `kpOrder().append(key)`。`imageFor` 全程持锁，没有这个窗口。

顺序表出现重复键之后，淘汰 `takeFirst` 可能先摘掉表项、留下另一份同名键；随后 `while (size > 32)` 摘不掉已经不在表里的键，表可以超过 32 且不再收缩。结果仍然算得对（后写覆盖同一键），错的是上限。多流程共用一个模板文件时会走到。

#### 历史 P3-2 — 串口失败文案在 IO 线程外读

`onSendRequested` 把 `write` 派到 IO 线程之后，在调用线程上 `port->errorString()`（`src/SerialCommNode.cpp:347-348`）。端口亲和性在 IO 线程。失败路径因此又碰了一次跨线程 `QIODevice`。

### 2.3 不单开号的残余

- `DnnNetCache::acquire` 在 `mapMutex` 下 `readNetFromONNX`（`src/DnnNetCache.cpp:71-118`）。冷路径，注释写了。8 条上限只淘汰 `use_count()==1`（`:136-145`），且按 `QHash` 遍历顺序，不是 LRU；八个模型同时被持有时上限不生效。
- 旧 mtime 条目用 `absPath == path` 比较（`:127`），键用的是 `canonicalFilePath`（`:38`）。不同写法指向同一文件时，旧条目要靠那 8 条上限慢慢挤。
- `ModelCache::imageFor` 仍用 `QDir::cleanPath`（`src/ModelCache.cpp:69`），没有跟随 DNN 缓存的 canonical。
- `ImageReadNode::cachedReadImage` 是进程内**一个槽**（`src/ImageReadNode.cpp:64-66`）。同一文件不重读；两个节点交替不同文件会互相挤掉。
- `releaseRoundOutputsOverBudget` 按 `w*h*通道数` 估字节（`src/FlowExecutor.cpp:1509`），16-bit 会低估。超过 64MB 才释放。
- `lastDatabaseError()` 仍先拿 `s_writeMutex`（`src/AppDatabase.cpp:191`）。查询接口本身已经不拿写锁。
- `include/ShapeBinding.h` 没有任何 cpp 包含它。双向同步走的是 `ModuleEditorDialog`（`:453-457`），不是这个类型。
- `HalconNode::executePreview` 在所属流程 `Running` 时直接 return（`src/HalconNode.cpp:217-218`）。躲过了预览与执行器抢同一节点，连续运行期间定时预览也不会跑。

## 3. 作者路线图核验

状态只针对作者自己的主张，不写进历史台账的 Fixed/Partial 计数。

| 作者 ID | 状态 | 证据 |
| --- | --- | --- |
| roadmap-P0-1 DNN 静态缓存无锁 | **Fixed** | `DnnNetCache` 表锁 + 条目锁。推理 `src/DnnInferNode.cpp:115-117`、分割 `src/DnnSegmentNode.cpp:515` 只在 `setInput/forward` 持 `entry->mutex`。键含 canonical、mtime、backend（`src/DnnNetCache.cpp:35-42`）。残余见 §2.3，不推翻「无锁 QMap 已消失」。 |
| roadmap-P1-9 检测/计数进同一缓存 | **Fixed** | `src/DnnDetectNode.cpp:282` 与 `:311`；`src/ObjectCountingNode.cpp:265` 与 `:293`。NMS / 过线在锁外。 |
| roadmap-P1-1 落库失败丢批次 | **Fixed** | 最多 3 次、50/150ms 间隔（`src/FlowExecutor.cpp:1548-1554`），仍失败则 JSONL（`:1555-1570`）。spill 有互斥和 64MB 轮转（`:1450-1458`）。与历史 P1-1 无关。 |
| roadmap-P1-2 拆 `s_dbMutex` + `synchronous=NORMAL` | **Fixed** | `s_connMutex` / `s_writeMutex` / `s_errMutex`（`src/AppDatabase.cpp:22-26`）。`queryAlarms`（`:506`）不再拿写锁。`PRAGMA synchronous=NORMAL` 在 `:105`。设计笔记里的报表分页没做，见 §2.3 的 `lastDatabaseError`。 |
| roadmap-P1-3 日志路径 | **Partial** | Debug 默认不落盘（`src/main.cpp:97-98`），句柄常开、256 条才重开（`:114-127`），UI 日志 2000 行（`src/MainWindow.cpp:185`）。路线图 `:503` 仍把「异步 appender」列在未做。本批没有 appender 线程。 |
| roadmap-P1-4 MVS 并发取图 / PayloadSize / StartGrabbing | **Partial** | 取图串行与采用句柄时查 `PayloadSize`（`src/MvsImageSourceNode.cpp:676-685`）已落地。本地 `StartGrabbing` 失败不再报成功。全局相机仍报成功（历史 P2-14），其它 SDK 入口仍无句柄锁（历史 P2-15）。 |
| roadmap-P1-5 主预览每轮全图转换 | **Fixed** | 三处改走 `requestImage`：`src/MainWindow.cpp:914`、`:1794`、`:1863`。 |
| roadmap-P1-6 图像源重读 + 缩略图 | **Fixed** | `cachedReadImage` 按路径+mtime+mono8（`src/ImageReadNode.cpp:62-115`）。缩略图签名没变就不发（`:120-131`）。单槽残余见 §2.3。 |
| roadmap-P1-7 按键/mtime/参数缓存 | **Fixed** | `ModelCache`。空 `templatePath` 不缓存（`src/OpencvFeatureMatchNode.cpp:341-342`）。样本/文件键加内容指纹（`:329-339`）。并发顺序表是历史 P2-16，不否定单线程命中。 |
| roadmap-P1-8 预览并发 | **Fixed** | 跨线程把 `triggerDelayedPreview` 排回对象线程（`src/HalconNode.cpp:203-208`）。流程在跑则跳过预览（`:217-218`）。 |
| roadmap-P1-10 热路径图像拷贝 | **Fixed** | 三通道改 `cv::merge`（`src/OpencvUtil.cpp:72-79`）。头注改为「深拷贝」（`include/OpencvUtil.h:20`）。布局不同，拷贝还在，与提交说明一致。 |
| roadmap-P1-11 发送入队 ≠ 成功 | **Partial** | 按流程分桶 + 每轮快照已在（`src/FlowExecutor.cpp:431`、`:841-845`；`src/CommunicationNodeBase.cpp:25-29`）。轮末不等待排队中的 write。见历史 P1-16。 |
| roadmap-P1-12 串口专属 IO 线程 | **Partial** | 创建、打开、关闭、写入都派到端口线程（`src/SerialCommNode.cpp:20-27`、`:203-206`、`:343-346`）。重连叠加连接（历史 P1-17）和 `errorString`（历史 P3-2）是这次改出来的。 |
| roadmap-P1-13 析构最多等 15 秒 | **Partial** | GUI 不再同步等满 15 秒（`src/FlowExecutorToken.cpp:58-62`）。后台收尾有，场景没有一起留住。见历史 P1-15。 |

### 3.1 作者 P2 与复核回补

作者称 P2 池清空。按提交能对上的项：

| 项 | 状态 | 证据 |
| --- | --- | --- |
| `catch` 写入 `lastError` | Fixed | `src/NodeBase.cpp:119-130`，`src/HalconNode.cpp:531-533` |
| 预览「有输入」判据取反 | Fixed | `src/HalconNode.cpp:231-232`（去掉错误的 `!IsInitialized`） |
| 线程库连接打开失败不缓存坏句柄 | Fixed | `src/AppDatabase.cpp:328-336` |
| VACUUM 移出写锁 | Fixed | `src/AppDatabase.cpp:947-950` |
| `feature_samples` 进入清理 | Fixed | `:918-922`，列是 `created_at` |
| 切全局相机先关旧句柄 | Fixed | `src/MvsImageSourceNode.cpp` 的 `setParam`：`globalCameraName` 变化时在拿 `m_mutex` 之前 `closeCamera()` |
| 取图缓冲复用、避免每帧 memset | Fixed | `src/MvsImageSourceNode.cpp:445-447` |
| 崩溃日志自锁 | Fixed | `TryLocker`（`src/main.cpp:46-51`），拿不到锁只写 stderr（`:109-112`） |
| 关窗共享 3 秒 | Fixed | `src/MainWindow.cpp:1004-1013` |
| 接收节点断线不输出 | Fixed | `src/ReceiveDataNode.cpp:60-63`；连通或清空设备名时清 `lastDataNote`（`:65-70`） |
| Modbus/PLC 读失败可见，且读成功不抹掉写失败 | Fixed | `m_lastReadError` 与「仅当 lastError 仍是那句读失败文本才清」（`src/ModbusNode.cpp:672-679`，`src/PlcCommNode.cpp:218-225`） |
| roadmap-P2-3 轮末图像预算 | Fixed | `FlowExecutor::releaseRoundOutputsOverBudget`（`src/FlowExecutor.cpp:1487-1527`），调用点在发送失败检查之后（`:847`）。不是历史 P2-3。 |
| 数据新鲜度 | **Partial** | 写出 `lastDataTs` / `lastDataAgeMs`（`src/ReceiveDataNode.cpp:76-80`）。`e773db7` 撤回了 `maxAgeMs` 过期门（U4/U8）。设备仍连接时，旧缓存继续输出。 |
| 全局 `StartGrabbing` 失败语义 | **Partial** | 只修了本地打开。见历史 P2-14。 |

### 3.2 模板匹配交互

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 参数区可滚动、数值最小宽度 | Fixed | `src/ModuleEditorDialog.cpp:328-340` 一带 `QScrollArea` |
| 掩膜擦除开关 | Fixed | `HalconWindow::setMaskPaintKeep`，对话框 `:182` |
| 「训练模板」按钮 + 预览 | Fixed（功能在） | `:221-226` 置 `trainFromImage`、`executeRequested`、`refreshTemplatePreview`。与非零搜索区同时用时受历史 P1-14 第 3 点影响。 |
| 自动重算后刷新叠加 | Fixed | `src/MainWindow.cpp:1615-1620`：`recomputeDownstream` 之后 `reloadFromNode` + `setResultOverlay` |
| 未命中灰框 | Fixed | `src/ImageDisplayController.cpp:73-80` 不再因 `!mr.valid` 丢弃 |
| 矩形搜索区 | Fixed | `src/OpencvTemplateMatchNode.cpp:222-237`，命中坐标加回 `searchRect`（`:519-520`） |
| 多边形搜索区 | **Partial** | 参数已注册（`:160-163`），`run()` 会读。过滤坐标系在粗搜/精修下是错的（历史 P1-14）。 |
| 粗到细性能 | **Partial** | 降采样、姿态缓存、0.95 提前结束都在（`:378-478`）。位置换算缺一截（历史 P1-14）。基准腿不钉坐标。 |

## 4. 热区抽查（没有另开号的部分）

- **DnnNetCache**：借出 `shared_ptr` 全程持 `mapMutex`，不会对同一键双加载。`clear()` 只丢表项，已借出的条目互斥仍在。检测、计数、推理、分割四个调用点都在 forward 外包了 `entry->mutex`。
- **FlowExecutor 轮末**：发送失败增量、预算释放、汇总行、`flushPendingResults` 的顺序在 `:841-865`。预算释放在落库之前，清的是 `m_nodeData` 里的图像，不碰已拷进 `InspectionRecord` 的文本。
- **发送分桶**：键是 `FlowRunContext*`（`CommunicationNodeBase.cpp:57`），执行器读的是 `m_runContext.get()`（`FlowExecutor.cpp:842`）。`setFlowScene` 把 `setOwnerContext(m_runContext)` 写到场景节点上（`:404`）。桶是对齐的；错的是时序（历史 P1-16），不是键。
- **AppDatabase**：写事务走 `WriteLock`，读查询走 `threadDatabase()`。本机没有跑 `database_test`。
- **MVS 缓冲复用**：`GenImage1` / `GenImageInterleaved` 在 HALCON 里会拷贝像素；复用成员缓冲替代每帧 `vector` 不构成悬垂，前提是生成函数在返回前完成拷贝。本环境不能链接 HALCON 复核这一条。

## 5. 下一步

1. 历史 Partial 维持：P1-1、P2-1、P2-3、P2-5、P2-6。P2-5 的新基线是 169 / 2033 / 156 / 245。
2. 先处理历史 P1-14（粗像素换算 + 多边形换到当前搜索图坐标系；基准腿钉 `matchCol/matchRow`）。单轮发送漏计（P1-16）和串口信号叠加（P1-17）紧随。
3. P1-15：要么在场景仍可能被执行时不要从析构返回，要么后台 join 持有场景/节点的寿命。头文件里的同步契约要改成与实现一致。
4. `roadmap-P1-4` 的全局 `StartGrabbing`（P2-14）与句柄锁覆盖面（P2-15）不要在「取图已串行」之后当成整项关闭。
5. 有 Qt 套件时补跑：`FeatureMatchTest`（加上坐标断言）、`database_test`、`comm_writeback_test` 里串口重连腿、`ObjectCountingTest` 的缓存腿。

## 6. 方法与未执行项

对照 `b35588f..7f80530` 的补丁与当前树阅读，重点在 `DnnNetCache`、`ModelCache`、`FlowExecutor` 轮末、`AppDatabase` 锁、`MvsImageSourceNode` / `GlobalCameraManager`、`SerialCommNode`、`CommunicationManager::sendData`、`OpencvTemplateMatchNode`、`MainWindow` 自动重算、`FlowExecutorToken`。

已执行：

- `python3 tools/wiring_checks.py --repo .` → `[ELW-VERDICT] GREEN`，`findings=0`，`audited_files=227`
- 同脚本 `--selftest` → 五条改坏臂均判红，复原后 GREEN
- `python3 tools/verify_inv5_arms.py`：首次因缺少 `build/` 在备份处失败，**没有改源码**。`mkdir -p build` 后重跑：A/B/C 三条均为 RED（`findings=1`），复原后 GREEN。结束后 `git status` 干净。
- `python3 tools/i18n_census.py --md`：合计 169 文件，未国际化 UI 2033，非 UI 156，已 `tr()` 245

未执行：

- 任何 CTest / 需 Qt、HALCON、MVS、OpenCV 链接的用例（含模板匹配基准、数据库、通信、DNN 注入腿）
- Windows 打包与 `tools/deploy.ps1`
- 真机相机、串口、PLC

`verify_inv5_arms.py` 把备份写到 `build/_arm_*.bak`。目录不存在时脚本在改文件之前就失败；仓库里没有 `build/` 时需要先建目录，否则这条证据跑不起来。

# Vision Workbench 复核 — 2026-10-29 r14

接 r1–r13 笔记。本轮审的是 `origin/main` 的 `f2807849e618bde2eca1f743e647826ce87c4e93`（发版登记 `rc-20261029.5`，登记的装机包提交是 `c587239`）。相对 r13 基线 `4da6f83f11208a32a3371ea26668230c7ee76563`：**153 个提交，174 个文件，+13484 / −462**。

本轮没有编译，也没有跑 CTest。云端虚拟机没有 Windows 套件所需的 Qt 6、HALCON、MVS、MSVC。下面凡是提到测试，只说明作者写进仓库的契约，不是本机跑出来的结果。

产品代码没有改。本文件是这一轮唯一的变更。

上一轮草案：[PR #13](https://github.com/daxian2778055/vision-workbench/pull/13)（`docs/code-review-2026-10-18-r13.md`，基线 `4da6f83`）。U-220（`abae137`）是 r13 之后的记档轮，零产品代码。

## 0. 结论先说

r13 留下的已 Fixed 条目没有回退。同场景 `setFlowScene` 快路径、保存成功才改锚、轮次中止不写 OK 汇总、异常清缓存、数据库时间按 UTC 绑定，都还在。

新表面里，跟踪过线、训练进程强杀、打包稳定排序、特征库空库报错，和提交说明对得上。流水线计数的推理前处理对不上：`ObjectCountingNode` 读取 `scale` / `swapRB` / `mean`，但 `init()` 没有登记这三项，缺省 `scale` 是 `0`，送进网络的是全零 blob；解码函数又按 letterbox 反算坐标，计数节点却把整幅图拉伸进正方形。新建节点可以在 `lastError` 为空的情况下把 `moduleStatus` 写成成功、累计保持 0。`DnnDetectNode` 的「推理后端 / 输出布局」写在 `registerParams({...})` 的闭括号外面，是一条被丢掉的逗号表达式，面板和序列化都不会出现这两项；v8 默认路径仍可用，YOLOX/v5 在检测节点上选不了布局，类别会按作者自己写的现场症状错位。

| 桶 | 本轮 |
| --- | --- |
| Fixed（14，不变） | P0-1、P1-2、P1-3、P1-5、P1-6、P2-2、P2-4、P2-7、P2-8、P2-9、P2-10、N1、N2、N3 |
| Partial（5） | P1-1、P2-1、P2-3、**P2-5**（本轮从 Open 移入）、P2-6 |
| Open（8，全是新号） | **P1-7**、**P1-8**、**P2-11**、**P2-12**、**P2-13**、**P2-14**、**P2-15**、**P3-1** |
| Withdrawn（1） | P1-4 |

P2-5 从 Open 改为 Partial：语言菜单、`translations/visionflow_en.ts`（268 条已译）和重启后加载 `visionflow_en.qm` 已经接上。新的计数 / 标注 / 训练界面仍是 `QStringLiteral` 中文，`MainWindow.cpp` 的 `tr(` 从 186 增到 200。英文界面没有做完。

`MainWindow.cpp` **4631** 行（r13：4351）。`FlowExecutor.cpp` **2312** 行（r13：2123）。`src/` 与 `include/` 下仍无 `qFatal(`。

## 1. 相对 `4da6f83` 的变化量

| 项 | 值 |
| --- | --- |
| r13 基线 | `4da6f83f11208a32a3371ea26668230c7ee76563` |
| 本轮 HEAD | `f2807849e618bde2eca1f743e647826ce87c4e93` |
| 提交 | 153 |
| 文件 | 174 |
| 增 / 删 | +13484 / −462 |
| 登记的当前装机包 | `docs/release-registry.json` 末行 `rc-20261029.5`，提交 `c587239cb64c0a85f2d25d0a9f8822dee6144276`，exe md5 `4d0b65c294359d1f1e53c2328b82568a`，10109440 字节 |

HEAD 比装机包提交多一个登记提交（`f280784` 只改登记表，把 `rc-20261029.4` 标成已被取代）。代码行为以 `c587239` 为准，登记行与该提交说明一致。

这一段里和本轮结论有关的表面（不是 153 条的逐条清单）：

| 主题 | 代表提交 | 本轮核对 |
| --- | --- | --- |
| U-220 记档 | `abae137` | 记档轮。`projectPath` 仍只在 `buildProjectJson` 写入，`src/` / `include/` 无读者 |
| 特征检索 | `541c820`、`1105425` | `FeatureIndex` 空库报错、维度不一致则跳过；见第 4 节 |
| 流水线计数 + YOLO 布局 | `ade15b4`、`26143a7`、`843a367` | 跟踪 / 过线 / 布局解码成立。计数前处理与检测节点的布局参数不成立。见 P1-7、P1-8、P2-11 |
| 方案打包 | `5146432` 及 10-29 的排序 / 去重修复 | 稳定排序与真实路径去重成立。复制失败时已改写的参数不回滚。见 P2-13 |
| 检测框标注 | `f1b3ebf`、`4bf8dc4` | `closeEvent` / `reject` 都会调用保存；`YoloAnnotation::save` 会看 close 错误。对话框丢掉返回值。见 P2-12 |
| 外部训练进程 | `d46dda5` 至 `c587239` | `start` 停表、`finished` 停表并清目标、定时器回调用 `QPointer` 对上具体进程，三处都在。探测仍在界面线程同步等。见第 4 节与 P3-1 |
| 发版门禁 | `8909f28` 起，`tools/release_manifest_check.py` | 登记表末行与门禁要核对的字段形状一致。门禁不扫口令文案。见 P2-15 |
| 部署文案 | `4bf8dc4`、`ab3dd53` | `版本说明.txt` 仍印出 `admin / admin`（有意留给现场装机）。同一次打包生成的 `部署说明.txt` 仍写「无默认口令」。见 P2-15 |

`src/MainWindow.cpp` 与 `src/FlowExecutor.cpp` 合计 +516 / −47。

## 2. 账本

| ID | r13 | r14 | `f280784` 上的证据 |
| --- | --- | --- | --- |
| P0-1 | Fixed | **Fixed** | `executeNodeOnce` / `recomputeDownstream` 仍在 `canRunFlow()` 为假时返回（`src/MainWindow.cpp`）。`canRunFlow()` 仍是已登录且写闸未开（`src/SessionManager.cpp:71`）。空用户表仍播种 `admin` / `createPasswordHash("admin")`（`src/AppDatabase.cpp:418-428`）。已在跑的流程仍先入队，写闸只挡空闲时的新一轮（`src/FlowExecutor.cpp` 的 `requestExternalRound`）。 |
| P1-1 | Partial | **Partial** | `kPlainPepper` 仍是编译进本体的 `VisionFlow-plaintext-envelope-MAC-1`（`src/SchemePackage.cpp:14`）。裸 JSON 成功路径仍把 `m_lastIntegrityNote` 设为 `旧明文 JSON 格式`（`src/ProjectManager.cpp:307`）。`applyProjectJson` 仍只建场景再套全局配置，不调用 `confirmOutsideSchemePaths`（`src/ProjectManager.cpp:416-424`）。相对 `..` 仍走 `EscapedRelative`。确认器返回真仍可加载方案外绝对路径。维持 U-216 的产品选择。 |
| P1-2 / N1 | Fixed | **Fixed** | 加密导入仍在轮数不等于 `kIterationsValid` 时于 `deriveKeys` 之前返回。 |
| P1-3 | Fixed | **Fixed** | UDP 在 `Missing` 且 `localPort > 0` 时拒绝绑定（`src/UdpCommNode.cpp:57-64`）。端口 0 仍落到 `QHostAddress::AnyIPv4`（`src/UdpCommNode.cpp:77`）。U-219 钉住的余量还在。 |
| P1-4 | Withdrawn | **Withdrawn** | 未重开。 |
| P1-5 | Fixed | **Fixed** | `README.md` 仍写明 Qt 6 / HALCON / OpenCV 与 `tools/ci.ps1`。 |
| P1-6 | Fixed | **Fixed** | 同场景快路径仍在清缓存之前返回（`src/FlowExecutor.cpp:333-346`）。`nullptr` 仍走慢路径。慢路径仍无 Running/Paused 守卫。产线 `setFlowScene` 调用仍是：新建、加载、关页先停再等，切页用 `executorForScene` 绑回同一场景（`src/MainWindow.cpp:1307`、`1903`、`2859`、`2936`、`3109`、`3393`）。没有出现「对正在跑的执行器喂另一个场景」的调用方。 |
| P2-1 | Partial | **Partial** | `kHashIterations` 仍是 60000，算法仍是 PBKDF2-HMAC-SHA256（`src/AppDatabase.cpp:47-51`）。 |
| P2-2 | Fixed | **Fixed** | 方案 AES / PBKDF2 仍走 Windows CNG（`src/SchemeCrypto.cpp`）。 |
| P2-3 | Partial | **Partial** | 字符串触发仍是相等或 `text.startsWith(entry.triggerSource)`（`src/GlobalTriggerManager.cpp:176`）。 |
| P2-4 | Fixed | **Fixed** | `~FlowExecutor` 仍是 `protected`，析构不 `wait`、不 `qFatal`。超时路径仍是有序停止、标记放弃、`deleteLater`。生产代码没有 `FlowExecutor` 子类。 |
| P2-5 | Open | **Partial** | `src/MainWindow.cpp:1159-1174` 有语言菜单；`src/I18n.cpp:40` 起加载 `visionflow_en`。`translations/visionflow_en.ts` 有 268 条译文，抽查为已填写的英文。计数、标注、训练对话框的标签是 `QStringLiteral`，英文模式不会翻译它们。`tr(` 站点：`MainWindow.cpp` 200（r13：186）。 |
| P2-6 | Partial | **Partial** | `MainWindow.cpp` 4631 行，`FlowExecutor.cpp` 2312 行。Halcon 与 OpenCV 两套节点都还在 `src/NodeRegistry.cpp`。 |
| P2-7 | Fixed | **Fixed** | `docs/security-development-guide.md` 开头仍是模板横幅。 |
| P2-8 | Fixed | **Fixed** | 仓库根目录没有 `需求.txt`、`requirements_decoded.txt`、`diag-churn.txt`。三份仍在 `docs/归档/`。 |
| P2-9 | Fixed | **Fixed** | `loadProject` 仍先拍下路径 / 只读旗 / 完整性说明，打开失败在改这三项之前返回，其后的失败路径仍 `rollbackSnapshot()`。 |
| P2-10 | Fixed | **Fixed** | `saveProject` 在 `open` 失败、短写、`commit` 失败时直接返回（`src/ProjectManager.cpp:146-161`）。`setLastFilePath` 与清 `m_loadedReadonly` 只在 `commit` 成功之后（`:164-166`）。`exportEncryptedProject` 不调用 `setLastFilePath`。`buildProjectJson` 在锚非空时仍写 `projectPath`（`:188-189`），仓库内仍无读者。 |
| N2 | Fixed | **Fixed** | `docs/ci-cd-guide.md` 仍有蓝图横幅。 |
| N3 | Fixed | **Fixed** | MAC 比较仍走 `constTimeEquals`。 |
| P1-7 | — | **Open** | 计数节点推理前处理。见第 3 节。 |
| P1-8 | — | **Open** | 检测节点的后端 / 布局参数没有登记。见第 3 节。 |
| P2-11 | — | **Open** | `classes.txt` 告警写错两个数字，并在返回前被覆盖。见第 3 节。 |
| P2-12 | — | **Open** | 标注对话框忽略 `save` 的失败。见第 3 节。 |
| P2-13 | — | **Open** | 打包器失败时留下半套路径改写，且不看写闸。见第 3 节。 |
| P2-14 | — | **Open** | 静态 `s_netCache` 无锁，`cv::dnn::Net` 副本共享实现。见第 3 节。 |
| P2-15 | — | **Open** | 三份客户文案对出厂口令说法不一致。见第 3 节。 |
| P3-1 | — | **Open** | 环境探测在界面线程里 `waitForFinished`，默认可达 20 秒。见第 3 节。 |

## 3. 新发现

### P1-7 — 流水线计数的推理输入不是检测节点那条前处理

| 字段 | 内容 |
| --- | --- |
| 严重度 | P1 |
| 状态 | **Open** |
| 路径 | `src/ObjectCountingNode.cpp`（`init`、`run`）；`src/DnnDetectNode.cpp`（`parseYoloOutput`、`run`） |

`ObjectCountingNode::init` 登记了模型、阈值、计数线、`dnnBackend`、`yoloLayout`、`resetOnStart`，没有登记 `scale`、`swapRB`、`mean`（`src/ObjectCountingNode.cpp:21-176`）。`HalconNode::registerParam` 才会把 `defaultValue` 写进 `m_params`（`src/HalconNode.cpp:810-815`）。`run` 却直接读这三项：

```265:275:src/ObjectCountingNode.cpp
        const double scale = m_params.value(QStringLiteral("scale")).toDouble();
        const bool swapRB = m_params.value(QStringLiteral("swapRB")).toBool();
        // mean 形如 "0,0,0"
        cv::Scalar mean(0, 0, 0);
        const QStringList parts =
            m_params.value(QStringLiteral("mean")).toString().split(QLatin1Char(','));
        if (parts.size() == 3)
            mean = cv::Scalar(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());

        cv::Mat blob = cv::dnn::blobFromImage(img, scale, cv::Size(inputSize, inputSize),
                                              mean, swapRB, false, CV_32F);
```

缺键时 `QVariant::toDouble()` 为 `0`，`toBool()` 为假。`blobFromImage` 的缩放系数为 0 时，blob 为全零（均值也是 0）。检测节点把同一组参数登记成 `scale = 1/255`、`swapRB = true`、`mean = "0,0,0"`（`src/DnnDetectNode.cpp:184-189`）。

坐标也不在同一套几何里。检测节点先 letterbox（灰边 114，保持长宽比），再 `blobFromImage`（`src/DnnDetectNode.cpp:249-258`）。`parseYoloOutput` 始终按 letterbox 反算：`padX` / `padY` 与 `min(inputSize/宽, inputSize/高)`（`src/DnnDetectNode.cpp:109-140`）。计数节点把原图直接拉伸到正方形，再把原图尺寸交给这个解码函数。非正方形画面上，框中心会偏离真实目标，过线用的就是这个中心。

推理没抛异常时，后面把 `moduleStatus` 设为真，并把 `countNote` 写成「累计 N（本帧 +M，在跟 K）」（`src/ObjectCountingNode.cpp:390-392`）。全零输入通常解不出框，累计停在 0，`lastError` 保持空。现场看到的是「流程成功、计数为 0」。

`tests/object_counting_test.cpp` 的 `yoloLayoutV8AndV5BothDecode` 只喂 `parseYoloOutput`。它不构造 `ObjectCountingNode`，也不经过 `blobFromImage`。跟踪与过线的腿（含 `reverseThenForwardCountsOnce`）用合成框，不经过这张 blob。

### P1-8 — 检测节点的推理后端与输出布局没有进入参数表

| 字段 | 内容 |
| --- | --- |
| 严重度 | P1 |
| 状态 | **Open** |
| 路径 | `src/DnnDetectNode.cpp:173-198`；`tests/param_panel_binding_test.cpp:2443-2446` |

`registerParams` 的初始化列表在 `swapRB` 之后已经用 `});` 结束。后面两行 `makeEnumParam` 是单独的逗号表达式，返回值被丢掉：

```188:196:src/DnnDetectNode.cpp
        makeBoolParam(QStringLiteral("swapRB"), true,
                      QStringLiteral("BGR/RGB 交换")),
    });
        makeEnumParam(QStringLiteral("dnnBackend"), 0,
                      {QStringLiteral("CPU"), QStringLiteral("OpenCL（GPU）")},
                      QStringLiteral("推理后端")),
        makeEnumParam(QStringLiteral("yoloLayout"), 0,
                      yoloLayoutLabels(),
                      QStringLiteral("输出布局")),
```

`run` 用 `m_params.value("dnnBackend", 0)` 和 `value("yoloLayout", 0)`（`src/DnnDetectNode.cpp:222`、`:265`）。缺键时两个默认都是 0：CPU，以及 v8/v11（无 objectness）。面板不会出现这两项，方案 JSON 也不会把它们存下来。

这是 `26143a7` 起就有的形状：`dnnBackend` 当时已经放在 `});` 外面。`843a367` 把 `yoloLayout` 加在同一条被丢掉的语句上，并把面板槽位冻结值从 271 改成 272。同一提交里 `ObjectCountingNode` 是用 `registerParam` 真正加上 `yoloLayout` 的。272 比 271 多出来的那一格，对得上计数节点这一次真实登记，对不上检测节点。因此 `fieldSlots == 272` 即使在 Windows 上为绿，也不能证明检测节点面板上有「输出布局」。注释写「与 DnnDetectNode 的 dnnBackend/yoloLayout」一起计入，和 `init()` 不符。

解码本身认布局参数（`src/DnnDetectNode.cpp:105-107`：`yoloLayout == 1` 时第 4 列是 objectness）。作者在同一函数里写了选错布局的现场表现：「框对了、类别全错」。检测节点上操作员无法把布局改成 v5/YOLOX，加载这类模型就会落在这句症状上。计数节点的 `yoloLayout` 是登记过的（`src/ObjectCountingNode.cpp:150-158`），那一侧可以选；它挡不住 P1-7 的全零 blob。

### P2-11 — 类别文件告警的两个数字是同一个，而且到不了界面

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Open** |
| 路径 | `src/ObjectCountingNode.cpp:292-303`、`:390-391` |

无类名的框被数出来之后，`countNote` 写成「模型输出 %1 类，classes.txt 只有 %2 行」。两个占位都是 `classNames.size()`（空列表时第一个写成 0，第二个仍是 `size()`）。模型输出的类别数没有参与这句文案。

同一次成功返回在 `:390-391` 无条件改写 `countNote` 为累计摘要。`:298-303` 的赋值在这之前，中间没有 return。操作员在成功帧上看不到这句告警。`26143a7` 把这句话写成现场最高频配置错误的提示。

### P2-12 — 标注保存会检查 close 错误，对话框不看返回值

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Open** |
| 路径 | `include/YoloAnnotation.h:114-117`；`src/BoxAnnotatorDialog.cpp:244-254`、`:301-309` |

`YoloAnnotation::save` 在 `flush` 与 `close` 之后返回 `streamOk && f.error() == NoError`。`closeEvent` 与 `reject` 都会调用 `saveCurrentImage`，切图也会（`:269`）。`saveCurrentImage` 调用 `YoloAnnotation::save` 后不看布尔值（`:308-309`）。磁盘满时，关窗和切图仍按「已经存了」离开当前图。`tests/yolo_annotation_test.cpp` 覆盖了往返、裁剪、tab 分隔和负类号，没有覆盖保存失败后的界面。

### P2-13 — 打包复制失败时参数已改写，入口也不看写闸

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Open** |
| 路径 | `src/ProjectPackager.cpp:74-110`；`src/MainWindow.cpp:4071-4081`；`include/SessionManager.h:35-46` |

循环里，同源命中和「已在方案内」都会先 `setParam` 再继续（`:75`、`:90`）。复制失败时函数立刻 `return r`（`:104-106`），此时 `r.ok` 仍为假，但前面的节点参数已经改成 `assets/...`，已经拷成功的文件留在磁盘上。界面只弹出 `r.error`（`src/MainWindow.cpp:4079-4080`）。之后若写闸已经解开、用户保存，半套改写会进方案。

写闸的定义是：出厂口令仍在用时，禁止方案覆盖保存 / 导出、通信配置落盘、用户管理，以及画布与模块编辑，避免「改完就跑」绕过落库闸（`include/SessionManager.h:35-46`）。「打包模型与依赖…」是运行时 `addAction` 出来的，`applyPermissionRestrictions` 没有把它关掉。`HalconNode::setParam`（`src/HalconNode.cpp:143`）不读 `isEditLocked()`。打包会改活方案里的文件路径；按 P0-1，闸开着时已经在跑的流程不会被停掉，下一轮会按新路径解析。

`c587239` 要求的稳定排序在 `:42-46`（按 `moduleId` / `fullName`）。同源去重键用 `resolveFinalPath`（`:71-77`）。这两处与提交说明一致。`tests/project_packager_test.cpp` 断言每个节点指向自己的源内容，不覆盖复制中途失败，也不覆盖写闸。

标注数据集和外部训练进程不在写闸那句列举里（它们不改方案文件）。本轮不把它们记成写闸绕过。

### P2-14 — 检测网络缓存无锁，两台执行器会共享同一个 Net

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Open** |
| 路径 | `src/DnnDetectNode.cpp:16-43`、`:221-225`；`src/ObjectCountingNode.cpp:248-262` |

`s_netCache` 是静态 `QMap`，`getOrLoadNet` 没有互斥。返回的 `cv::dnn::Net` 按值拷贝，OpenCV 的 `Net` 副本共享同一份实现。检测节点与计数节点都按模型路径取这一份，并在每次 `run` 里 `setPreferableBackend` / `setPreferableTarget` 再 `forward`。

一台执行器上的节点是串行的，单页流程不会自己撞上这把缓存。主窗口按场景各持一个执行器，非当前页的连续运行可以和当前页同时进 `run`（这是 r13 讨论 P1-6 时已经在用的结构）。两个线程对同一个 `Net` 调用 `forward`，或一个在改 target、另一个在 `forward`，都落在这份无锁缓存上。缓存键只有路径字符串，检测与计数共用是有意的；缺的是互斥，或者按线程分开的网络实例。

### P2-15 — 出厂口令在三份客户文案里有两种说法

| 字段 | 内容 |
| --- | --- |
| 严重度 | P2 |
| 状态 | **Open** |
| 路径 | `tools/deploy.ps1:353-355`、`:588-595`；`docs/用户操作手册.md:71`、`:453-454`、`:515`；`src/AppDatabase.cpp:418-428` |

数据库在用户表为空时播种 `admin` / `admin`。`deploy.ps1` 写进每个输出目录的 `版本说明.txt` 包含「默认账号/口令（首次启动自动播种）：admin / admin」，并要求现场改密、记入交付单（`:591-595`）。`ab3dd53` 移走的是「如何改这段文案」的打包者说明，那段现在是脚本注释（`:588-590`）。口令行本身留在客户文件里，与 `4bf8dc4` 的交付口径一致：现场装机需要看见初始口令。这不是新的秘密，pepper 与出厂口令本来就编译在程序里。

同一次打包写的 `部署说明.txt` 第 3 步仍是「首次启动需创建初始管理员账号（……无默认口令）」（`:355`）。`docs/用户操作手册.md` 第 71 行与第 515 行同样写「无默认口令」，第 453 行则写写闸对应 `admin/admin`。按部署说明操作的人不会去改口令，写闸会把画布和保存关掉，现象像故障。

`tools/release_manifest_check.py` 核对 tag、提交、exe md5、字节数、脏工作树、exe 是否内嵌 HEAD。它不检查清单正文里有没有口令，也不要求 md5 必须是 32 位十六进制（`rc-20261029.5` 这一行本身是 32 位）。`tools/wiring_checks.py` 的 INV4 要求 `FlowExecutor` 在去掉注释后出现 `onRoundStart();`，对应 `src/FlowExecutor.cpp:1997`。

### P3-1 — 训练环境探测仍在界面线程里同步等待

| 字段 | 内容 |
| --- | --- |
| 严重度 | P3 |
| 状态 | **Open** |
| 路径 | `src/TrainingRunnerDialog.cpp:214-217`；`src/TrainingRunner.cpp:45-48` |

`onProbeEnv` 直接调用 `TrainingRunner::probe`。`probe` 在调用线程上 `waitForStarted(5000)`，再 `waitForFinished(timeoutMs)`，默认 `timeoutMs` 为 20000。点「检测环境」时界面可以停在这一等上。取消路径已经改成 `requestCancel()`，不再在界面线程上等进程（`src/TrainingRunner.cpp:107-129`，`src/TrainingRunnerDialog.cpp:339-351`）。探测这条还留着。

`TrainingRunner` 析构在进程仍在跑时 `kill` 然后 `waitForFinished(3000)`（`src/TrainingRunner.cpp:10-15`）。对话框在 `running()` 时拒绝关闭，正常路径不会走到这个析构等待。

## 4. 核对过、与提交说明一致的部分

### 训练进程的 5 秒强杀（`c587239`）

三处都在 `src/TrainingRunner.cpp`：

- `start` 开头若已有 `m_killTimer` 则 `stop()`（`:74-75`），避免上一趟的定时器打到下一趟。
- `finished` 槽里 `stop()` 并 `m_killTarget.clear()`（`:93-95`）。
- 定时器回调要求 `m_killTarget == m_proc` 且仍在运行才 `kill()`（`:120-126`）。`m_killTarget` 是 `QPointer<QProcess>`。

`requestCancel` 只 `terminate` 再武装 5 秒定时器，不在界面线程 `waitForFinished`（`:107-129`）。`TrainingRunnerDialog` 的 `closeEvent`、`accept`、`reject` 在 `running()` 时都走 `requestCancel` 并忽略关闭（`:339-373`）。关闭按钮在训练中的「自动关掉」发生在 `finished` 之后再 `accept()`（`:142-149`），那时 `running()` 已为假，重入的 `accept()` 会真正关闭。

`tests/training_runner_test.cpp` 有 `stubbornProcessIsKilledAfterGrace` 与 `restartWithinWindowIsNotKilled`。本机未跑。对话框的 `closeEvent` / `accept` 没有对应测试腿。

分类训练向导是另一条路径：取消标志是按值捕获的 `shared_ptr`，界面更新前看 `QPointer<TrainingWizardDialog>`（`src/TrainingWizardDialog.cpp:294-308`）。关闭按钮在 `setRunning(true)` 时被禁用（`:179-181`），并接到 `QDialog::accept`（`:139`）。向导没有重写 `accept()`。按钮禁用之后，这次点击进不了 `accept`。本轮不为此单列编号。

### 跟踪身份与方向过滤（`4bf8dc4`）

`ObjectTracker` 用 `m_nextId++` 分配 id，丢弃轨迹后不回收（`include/ObjectTracking.h:94`、`:115-118`）。匹配要求两边类名都非空且相等才允许 IOU 配对（`:64-66`）。`LineCounter::update` 在方向不被接受时 `continue`，不把 id 放进 `m_counted`（`:219-226`）。侧记录在判定之前更新（`:209-210`），所以「先被拒绝的方向、再走允许的方向」仍能记到 1 次。`ObjectTrack::counted` 字段没有被计数器读取；记账在 `LineCounter::m_counted`。这是死字段，不单列。

### 特征库

`FeatureIndex::search` 在提取失败或库为空时把原因写进 `errors` 并返回空（`src/FeatureIndex.cpp:108-116`）。描述子类型或列数不一致的样本被跳过（`:125-126`）。比值检验与单应内点在 `:139-180`。`FeatureSearchNode` 的 mode / detector 是带 `paramSpecs` 的枚举整型。`tests/feature_index_test.cpp` 含持久化往返、空库报错、截断样本拒绝。本机未跑。抽查没有看到 SIFT 腿。

### 轮次中止、异常缓存、UTC、编辑锁

- 用户停止把 `m_roundAborted` 置位后，轮末不写汇总行；汇总行的 `passed` 只在未中止时取 `!m_roundHadFailure`（`src/FlowExecutor.cpp:819-836`）。
- `executeNode` 的两个 `catch` 都调用 `clearNodeCachesOnException`：清输出端口、在 `m_graphCacheMutex` 下清三类缓存、置 `m_roundHadFailure`（`src/FlowExecutor.cpp:1387-1401`、`:1440-1456`）。
- `resetState` 在清缓存之后对场景节点调用 `onRoundStart()`（`src/FlowExecutor.cpp:1991-1998`）。计数节点在 `resetOnStart` 为真时清累计（`src/ObjectCountingNode.cpp:190-193`）。
- 时间绑定走 `toUTC()`，读回用 `Qt::UTC`（`src/AppDatabase.cpp:29-41`）。
- `onExecutionError` 会 `updateEditLockForCurrentScene()`（`src/MainWindow.cpp:3286-3293`）。锁的判据仍是运行态锁或写闸（`include/SessionManager.h:48-50`）。

### 相机关闭

本地 MVS 节点仍先 `waitDrained`，超时则不拆句柄并撤闸（`src/MvsImageSourceNode.cpp:773-777`）。关窗路径在任一执行器 3 秒内没停下来时，整段相机关闭都跳过（`src/MainWindow.cpp:1001-1010`）。`GlobalCameraManager::closeCamera` 在非 `force` 且仍有消费者时拒绝拆句柄（`src/GlobalCameraManager.cpp:663-671`）。这是 r13 之后仍保持的口径，本轮不新开编号。

## 5. 已 Fixed 项的回归抽查

没有发现已 Fixed 项回退。抽查落点：

| 项 | 结果 |
| --- | --- |
| `m_aliveBlock` | 慢路径仍在 `m_mutex` 内发布 `shared_ptr`；`run` 在解锁前拷贝（`src/FlowExecutor.cpp:646-649`） |
| 同场景快路径 | 见 P1-6。快路径仍在界面线程写节点的 owner 指针，与 r13 记录的余量相同 |
| 保存 / 导出锚 | 见 P2-10 |
| 写闸 / `canRunFlow` | 见 P0-1。新菜单「打包模型与依赖…」不在这道闸上，记在 P2-13，不把 P0-1 打回 Partial |
| 方案包 pepper / 裸 JSON / `applyProjectJson` | 见 P1-1，仍是 Partial |
| 轮次中止当 OK | 汇总行在 `m_roundAborted` 时不写 |
| 加载后撤销 | `loadProject` 成功路径仍 `clearUndoHistory()`（`src/ProjectManager.cpp:774`） |
| 相机等待 | 见第 4 节。本地路径仍等待；关窗仍在执行器没停时放弃拆句柄 |

## 6. 建议顺序

1. **P1-7。** 给计数节点登记 `scale = 1/255`、`swapRB = true`、`mean = "0,0,0"`，并与 `DnnDetectNode::run` 共用 letterbox 再送进 `parseYoloOutput`。用一张非正方形图做对照：全零 blob 与拉伸几何都不应该再计成「成功且累计 0」。现有跟踪腿不用改语义。
2. **P1-8。** 把 `dnnBackend` 与 `yoloLayout` 放进 `registerParams` 的列表。槽位冻结值要按真实面板重数后再改；现在的 272 不能当作这两项已上面板的证据。
3. **P2-11。** 类别告警用「框上的最大 classId」对「classes.txt 行数」，并且附加在累计摘要后面，不要先写再被 `:390` 盖掉。
4. **P2-12。** `saveCurrentImage` 在 `YoloAnnotation::save` 返回假时停在当前图并写状态栏，关窗路径同样要让操作员看见失败。
5. **P2-13。** 复制失败时回滚本次已经 `setParam` 的路径（或先拷到临时名，全部成功再改参数）。写闸开着或画布锁着时，「打包模型与依赖…」应不可用。
6. **P2-14。** 给 `getOrLoadNet` 加锁，或让每个执行器持有自己的 `Net`。不要在无锁的共享网上每帧 `setPreferableTarget`。
7. **P2-15。** `部署说明.txt` 与手册第 2.3 / 第 12 节改成与播种和 `版本说明.txt` 同一句话：存在出厂口令 `admin` / `admin`，装机当场改掉。`版本说明.txt` 里印出口令是当前交付选择，本轮不要求删掉；若包会离开现场，沿用 `deploy.ps1:588-590` 那条注释，改成「初始口令见交付单」。
8. **P3-1。** 探测改到工作线程，界面只收结果。取消路径已经是这个形状。
9. **维持。** pepper、无作者签名、裸 JSON、`applyProjectJson` 不闸、确认通过的方案外路径（P1-1）；UDP 端口 0（P1-3 余量）；PBKDF2 60000（P2-1）；前缀匹配（P2-3）；双栈与两个大文件（P2-6，本轮又长大）；慢路径 `setFlowScene` 仍不设运行态守卫，直到真有调用方对 Running 执行器传入另一场景（P1-6 余量）。英文界面留在 Partial，直到新节点的可见字符串进 `tr(`。

## 7. 测试在仓库里，本机没有执行

云端不能跑 Windows CTest。下面这些名字只说明仓库里有对应槽，不说明它们是绿的。提交说明里的「training_runner 8 腿」「连跑 15 次 0 红」不是本虚拟机的结果。

| 套件 | 与本轮有关的契约 | 缺口 |
| --- | --- | --- |
| `tests/training_runner_test.cpp` | 顽固进程 5 秒后被杀；5 秒窗口内重启不被旧定时器杀死 | 没有对话框 `closeEvent` / `accept` |
| `tests/object_counting_test.cpp` | ID 延续、过线一次、方向过滤、`parseYoloOutput` 的 v8/v5 | 不跑计数节点的 blob / `scale` / letterbox |
| `tests/yolo_annotation_test.cpp` | 往返、裁剪、tab、负类号 | 不看对话框是否处理 `save` 失败 |
| `tests/project_packager_test.cpp` | 复制并改写、方案内不重复、同名加后缀、同源只一份 | 没有复制失败回滚，没有写闸 |
| `tests/feature_index_test.cpp` | 持久化、空库、截断行 | 抽查未见 SIFT |
| `tests/param_panel_binding_test.cpp` | `fieldSlots == 272` | 数字与「检测节点两项已上面板」的注释不一致，见 P1-8 |
| `tests/node_exception_exit_test.cpp` | 直连槽抛异常时本轮 NG | 与 `clearNodeCachesOnException` 同向 |
| `tests/round_abort_summary_test.cpp` | 中止不写 OK 汇总 | 本机未跑 |
| `tools/selftest_release_manifest.py` | 门禁自测（tag / md5 / 字节 / 登记行） | 不扫口令文案 |

有 Qt 6、HALCON、MVS、MSVC 的 Windows 机器上，至少应跑 `object_counting_test`、`param_panel_binding_test`、`training_runner_test`、`project_packager_test`、`yolo_annotation_test`、`feature_index_test`。本复核不能说它们通过。P1-7 现有的计数测试即使用例全绿，也覆盖不到全零 blob。

## 8. 给协调人（可转发）

- 审核对象：`main@f280784`（`f2807849e618bde2eca1f743e647826ce87c4e93`）。相对 r13 基线 `4da6f83`：**153 个提交，174 个文件，+13484 / −462**。登记的当前装机包是 `rc-20261029.5` / 提交 `c587239` / md5 `4d0b65c2…` / 10109440 字节。
- 账本：**14 Fixed，5 Partial，8 Open，1 Withdrawn**。P2-5 从 Open 收到 Partial（语言菜单和英文 qm 已接上，新界面大多仍是中文字面量）。没有已 Fixed 项回退。
- 新 Open：P1-7 计数节点 `scale` 缺省为 0 且不做 letterbox，成功路径可以把累计 0 报成模块成功。P1-8 检测节点的后端 / 布局参数在 `registerParams` 闭括号之外，面板上没有这两项；槽位冻结值 272 对得上计数节点新增的那一格，对不上检测节点。P2-11 类别告警数字写错且被累计文案盖掉。P2-12 标注保存失败被对话框忽略。P2-13 打包失败留下半套路径，且打包菜单不看写闸。P2-14 静态网络缓存无锁。P2-15 `部署说明` / 手册写「无默认口令」，`版本说明` 与数据库播种是 `admin/admin`。P3-1 环境探测在界面线程最多等 20 秒。
- 训练强杀的三重防护、方向过滤不把拒绝的穿越写入已计集合、打包稳定排序，与 10-29 的修复说明一致。
- 云端没跑 Windows CTest（缺 Qt 6 / HALCON / MVS / MSVC）。本轮只加了这份审核记录，没有改产品代码。

# VisionMaster 4.4 对齐声明复核（模板匹配 / 高精度匹配）

> 复核日期：2026-11-02
> 复核对象：`rc-20261102.4` 发版声明「VM 4.4 对齐全部闭合」，以及三份模板匹配文档中的对齐表述：
> `docs/VM4.4高精度匹配对齐规范.md`、`docs/模板匹配进度与交接.md`、`docs/模板匹配VM对齐清单.md`。
> 权威依据：本机随 VisionMaster 4.4.2 安装的官方帮助
> `D:\Program Files\VisionMaster4.4.0\Applications\Help\CH\`，优先使用以下主题页：
>
> | 主题 | 帮助页 GUID |
> |---|---|
> | 高精度匹配 | `GUID-11F05391-A171-4F3F-A43C-8A1348B3E02C.html` |
> | 轮廓匹配（特征模板流程的母页） | `GUID-3EEB6D91-78DA-40BF-AC7B-537FEBB00FFC.html` |
> | 快速匹配 | `GUID-1E0B9D61-08B9-4DE8-A2FB-63828E93FF32.html` |
> | 灰度匹配 | `GUID-30450AD7-F6BE-4128-B2F0-591880E9156D.html` |
> | 基本参数 | `GUID-D22BB5D5-EF87-4323-AE9D-1DAD7892C8C1.html` |
> | 结果显示 | `GUID-1552B2A6-F573-4CF0-990D-3B07EC23E074.html` |
>
> 复核方式：官方帮助原文逐条摘录 → 在代码里找对应实现/参数 → 判定「一致 / 部分 / 缺失 / 相反」。
> 不以「默认路径能跑通」代替「语义对齐」，也不以测试全绿代替「官方口径已覆盖」。

## 一、结论

**「VM 4.4 对齐全部闭合」这一声明不成立。**

- 核心参数与主要交互确实已经大量落地（模板对象、配置窗、多形状、输出掩膜、尺度 XY、匹配中心、擦除轮廓点、四页签等），这部分不是空话。
- 但按官方 4.4.2 帮助逐条核对后，仍有 **5 项会直接导致结果错误或漏检**、**11 项官方参数/功能缺失或语义不符**、**6 项默认值/交互/概念偏差**。
- 另有 **9 处文档自身失真**：包括「全部闭合」结论、面板槽位 298 与测试 310 不一致、规范里同一参数同时写「未做」和「已做」、超时与最大重叠率的口径与官方相反等。
- 现有测试没有钉住这些语义：超时、最大重叠率、全模板多目标、尺度 Y 输出、结果判断均无用例覆盖。**测试全绿只能说明既有路径不回归，不能证明 VM 对齐。**

## 二、P0：会直接造成结果错误或漏检

| 编号 | 官方口径（4.4.2 帮助原文） | 当前实现与证据 | 影响 |
|---|---|---|---|
| P0-1 | 超时控制：「搜索时间达到超时时长算法将停止搜索，**不返回任何搜索结果**。取值范围 0~10000 ms，0=关闭。」（高精度匹配·超时控制） | `timeoutMs` 只跳出角度/尺度循环，之前已找到的 `bestScore` 仍保留；函数末尾仍按 `bestScore >= minScore` 置 `moduleStatus`（`src/OpencvTemplateMatchNode.cpp:1255`、`src/OpencvTemplateMatchNode.cpp:1533`）。无超时用例 | 超时场景可能继续输出坐标并判 OK，与官方「无结果」相反；上位机可能拿过期结果继续动作。**2026-11-08 已修**：超时后不再输出任何结果（matchCount=0、matchList 为空、moduleStatus=false 且原因含「搜索超时」）；用例 `FeatureMatchTest::templateTimeoutReturnsNoResults`（改坏臂：关掉作废逻辑后该腿判红）。 |
| P0-2 | 全部搜索模式：「逐个使用**所有勾选的模板**对目标进行匹配，输出最优的匹配结果。默认关闭。」配合「多模板重叠率」决定不同模板命中的重叠目标是否都输出（高精度匹配·全部搜索模式/多模板重叠率） | 关：按列表顺序逐个试、达标即停（`src/OpencvTemplateMatchNode.cpp:589`）；开：所有模板各跑一遍，只保留最高分模板，再用该模板重跑一次（`src/OpencvTemplateMatchNode.cpp:613`）。没有「多模板重叠率」参数，也不合并不同模板命中的目标 | 两个不同模板对应画面上的不同工件时，只输出其中一个模板的目标，另一类目标全部漏掉；无法复现 VM 的多模板多目标结果 |
| P0-3 | 最大重叠率：「重叠率的计算方式为『两个重叠目标中重叠部分的面积』/『两个重叠目标的总面积』。取值范围 0~100，默认 50。重叠率大于设置值时舍弃低分目标。」（高精度匹配·最大重叠率）**2026-11-08 帮助页核对（原文保留，就地更正）**：本机 Help/CH 高精度匹配页只写「两个匹配框的最大重叠比例」，**没有**给出「交叠/两框总面积」这句；本帮助里唯一明写分母的是其它模块的「重叠面积与较小预测框的比值」。故此引文在 Help/CH 内无出处，需以用户手册 PDF 页码补证，否则本条不应按「官方=总面积」计缺陷。 | 实际用「交叠面积 / **较小框面积**」，且框按轴对齐、忽略旋转（`src/OpencvTemplateMatchNode.cpp:1457`–`src/OpencvTemplateMatchNode.cpp:1461`） | 分母偏小，重叠率被高估，相邻但可共存的目标被过度丢弃；旋转目标的重叠判断与官方不一致。**2026-11-08 复核改判**：Help/CH 未定义分母；现实现（较小框）与帮助中唯一的显式公式同向，暂维持代码，待实机/PDF 对照后再定。 |
| P0-4 | 匹配个数：「代表图像中与模板相同或相似对象的个数。」（高精度匹配·模块结果） | `matchCount` 在最大重叠率过滤**之前**写入（`src/OpencvTemplateMatchNode.cpp:1441`）；`matchList` 在过滤之后重建（`src/OpencvTemplateMatchNode.cpp:1488`–`src/OpencvTemplateMatchNode.cpp:1499`）。两者分母不同 | 面板/下游读到的「匹配个数」可能大于实际保留的目标数；按个数做 OK/NG 判断会误判 |
| P0-5 | 模块结果按每个命中分别给出「匹配框宽度/高度」「尺度 X」「尺度 Y」；尺度 XY 模式下两者独立（高精度匹配·模块结果） | 输出框宽高只用 X 尺度：`outW/outH` 都用 `bestScale`（`src/OpencvTemplateMatchNode.cpp:1512`–`src/OpencvTemplateMatchNode.cpp:1513`）；`extraValues` 的尺度 X/Y 都写 `bestScale`（`src/OpencvTemplateMatchNode.cpp:1577`–`src/OpencvTemplateMatchNode.cpp:1580`）；`matchList` 每个命中只有单值尺度、没有尺度 Y（`src/OpencvTemplateMatchNode.cpp:1417`–`src/OpencvTemplateMatchNode.cpp:1424`）；结果表「尺度 Y」直接复制「尺度 X」（`src/ModuleEditorDialog.cpp:1271`）；画布多目标叠加按单一比例缩放（`src/ImageDisplayController.cpp:139`–`src/ImageDisplayController.cpp:145`） | 非均匀缩放目标（如 1.2×0.8）虽然能命中，但输出的框尺寸、尺度 Y、叠加框全部错误；下游尺寸测量会被带偏 |

## 三、P1：官方有、当前缺失或语义不符

| 编号 | 缺口 | 官方口径 | 当前实现与证据 | 影响 |
|---|---|---|---|---|
| P1-1 | 最大匹配个数上限 | 「默认值 1，有效取值范围 **1~1000**」（高精度匹配·最大匹配个数） | 注册范围与运行时夹取都是 1~16（`src/OpencvTemplateMatchNode.cpp:368`、`src/OpencvTemplateMatchNode.cpp:1224`–`src/OpencvTemplateMatchNode.cpp:1225`） | 多件/密集目标场景超过 16 个就丢目标；VM 手册允许到 1000。**2026-11-08 已修**：注册与运行时上限改为 1~1000；用例 `FeatureMatchTest::templateMaxMatchesAllowsVmRange`（20 个目标全检出；改坏臂钳回 16 时该腿判红，实得 16）。 |
| P1-2 | 尺度范围量程 | 「有效值范围 **0.1~10.0**；默认 1.00~1.00」（高精度匹配·尺度范围） | 注册范围 0.3~3.0（`src/OpencvTemplateMatchNode.cpp:435`–`src/OpencvTemplateMatchNode.cpp:450`）；采样固定 16 档（`src/OpencvTemplateMatchNode.cpp:909`、`src/OpencvTemplateMatchNode.cpp:917`–`src/OpencvTemplateMatchNode.cpp:920`） | 小于 0.3 或大于 3 的目标无法搜索；大范围下 16 档过粗，可能跨过真实尺度 |
| P1-3 | 模板列表的「载入 / 排序 / 勾选」 | 特征模板页支持：模板排序（未开全部搜索模式时按排序优先级逐个试）、勾选模板（勾选多个时按勾选先后执行）、载入模板（从本地路径载入）（轮廓匹配·特征模板） | 特征模板页只有「创建/编辑/删除所有」（`src/ModuleEditorDialog.cpp:171`–`src/ModuleEditorDialog.cpp:186`）；没有载入、上移/下移、勾选状态；顺序只能是数组写入顺序 | 无法复现 VM 的多模板工作流；无法只启用部分模板；顺序不可调导致顺序匹配结果不可控 |
| P1-4 | 多模板重叠率 | 全部搜索模式开启时使用：「不同模板检测的目标重叠时，若重叠率低于该数值，则目标均输出；若高于该数值，则仅输出匹配分数高的目标。」（高精度匹配·多模板重叠率） | `src` 与 `include` 全量检索 `多模板重叠率` 零命中；全部搜索模式也没有合并不同模板命中的目标（见 P0-2） | 多模板结果无法按官方规则合并，重叠目标要么全丢、要么只出一个模板的目标 |
| P1-5 | 是否考虑噪点 | 「噪点是指匹配图中不在边缘链上的特征点。开启后，若特征存在毛刺，则评分降低。**默认关闭**。」（高精度匹配·是否考虑噪点） | `src` 与 `include` 检索 `噪点` 零命中；没有该参数，评分只走可见率/方向一致性 | 无法复现 VM 在毛刺/噪点场景下的分数；默认行为接近「不考虑」，但参数本身缺失 |
| P1-6 | 是否获取轮廓 / 是否输出轮廓 | 「是否获取轮廓：开启显示模板轮廓特征点；关闭则不显示特征点、只显示匹配框，**可降低匹配耗时**。」「是否输出轮廓：打开则在模块结果中输出轮廓。」（轮廓匹配·运行参数；高精度匹配有「是否获取轮廓」与轮廓结果） | 只有 `showFeaturePoints` 显示开关（`src/OpencvTemplateMatchNode.cpp:398`–`src/OpencvTemplateMatchNode.cpp:401`），不影响计算，也不输出轮廓结果 | 无法通过关闭轮廓降低节拍；下游拿不到 VM 的「匹配轮廓信息 / 匹配轮廓点个数」 |
| P1-7 | 结果判断 | 结果显示的「结果判断」：「若设置多个判断条件，当所有判断条件均满足时，则模块输出 OK；否则输出 NG。」高精度匹配截图含忽略匹配个数/数量判断/角度判断/尺度判断（结果显示·结果判断） | `src` 与 `include` 检索 `结果判断` 零命中；当前 OK/NG 只由 `bestScore >= minScore` 决定（`src/OpencvTemplateMatchNode.cpp:1533`） | 无法按「数量/角度/尺度」做产线判定；只能判分数，和 VM 的模块结果判断不等价 |
| P1-8 | 基本参数：运行期屏蔽区、继承方式、位置修正方式 | ROI 区域支持「屏蔽区创建=绘制/继承」；ROI 创建=继承时还要选「按区域继承 / 按参数继承」；位置修正支持「按信息 / 按点 / 按坐标」（基本参数） | 只有 `roiSource` 0/1（绘制/继承上游框）与 `writeFixtureName`（`src/OpencvTemplateMatchNode.cpp:393`–`src/OpencvTemplateMatchNode.cpp:394`、`src/OpencvTemplateMatchNode.cpp:451`–`src/OpencvTemplateMatchNode.cpp:452`）；运行期没有屏蔽区参数（屏蔽形状只存在于模板对象） | 基本参数页只对齐了「图像型 / 图形型」与简化继承；VM 的屏蔽区与位置修正三种取法无法配置 |
| P1-9 | 模板配置参数：尺度模式、链长模式/最小链长、增强匹配 | 模板配置窗含：尺度模式（速度尺度的自动/手动，**默认自动**）、速度尺度、特征尺度（≤速度尺度）、阈值模式、对比度阈值、链长模式、最小链长；掩膜列表含「增强匹配（仅高精度匹配支持）」（轮廓匹配·特征模板） | 配置窗只有阈值模式/对比度阈值/特征尺度/速度尺度四项（`src/TemplateConfigDialog.cpp:116`–`src/TemplateConfigDialog.cpp:130`）；链长模式/最小链长在节点参数里（`src/OpencvTemplateMatchNode.cpp:372`–`src/OpencvTemplateMatchNode.cpp:375`，分组见 `:479`–`:485`）；`scaleMode` 既控制速度尺度自动、又控制尺度范围自动（`src/OpencvTemplateMatchNode.cpp:857`–`src/OpencvTemplateMatchNode.cpp:861`、`src/OpencvTemplateMatchNode.cpp:899`–`src/OpencvTemplateMatchNode.cpp:907`），默认 0=手动（`:395`–`:396`）；`src`/`include` 检索 `增强匹配` 零命中 | 模板配置窗无法完整复现 VM 六项；尺度模式语义被扩大成「搜索尺度范围也自动」，与官方只控制速度尺度不符；增强匹配缺失 |
| P1-10 | 模块分类与默认方法 | VM 有四个匹配模块：高精度匹配（多层金字塔，精度高）、快速匹配（仅金字塔顶层，速度快）、轮廓匹配、灰度匹配（NCC 灰度相关）。快速匹配仍是边缘特征法，不是灰度法（高精度匹配/快速匹配/灰度匹配页） | `method` 0/1/2 被注册成「匹配方法」，默认 1（归一化相关系数），并归到「图像输入」组（`src/OpencvTemplateMatchNode.cpp:364`–`src/OpencvTemplateMatchNode.cpp:365`、`:472`–`:474`）。文档把 0/1 叫「快速匹配（形貌/灰度）」 | 开箱默认走灰度 NCC，不是 VM 高精度匹配；把快速匹配等同灰度匹配，参数与算法归属都会误导后续对齐 |
| P1-11 | 模块结果字段不完整 | 官方结果含：匹配状态、匹配个数、匹配模板编号/名称、匹配框（框/中心/宽/高/角度）、匹配点、尺度/尺度 X/尺度 Y、分数、检测区域（中心/宽/高/角度）、匹配轮廓信息/点数、屏蔽区多边形、输出掩膜（图/宽/高/像素格式）（高精度匹配·模块结果） | 当前只有 `matchCount`、`matchTemplateIndex/Name`、`matchRow/Col/Score/Angle/Scale/Point/ScaleX/Y` 与 7 项 `matchList`；输出对象只挂一个「掩膜」具名结果（`src/OpencvTemplateMatchNode.cpp:1547`–`src/OpencvTemplateMatchNode.cpp:1585`）；全量检索 `matchStatus` 零命中；没有检测区域/轮廓/屏蔽区结果 | 下游脚本或报表按 VM 字段名取数会取不到；结果面板也不具备 VM 的完整字段 |

## 四、P2：默认值、交互与概念偏差

| 编号 | 项 | 官方口径 | 当前实现与证据 | 说明 |
|---|---|---|---|---|
| P2-1 | 出厂默认值 | 最小匹配分数默认 **0.5**；角度范围默认 **-45~45**；模板配置「尺度模式」默认 **自动**（高精度匹配·运行参数/轮廓匹配·特征模板） | `minScore` 默认 0.6（`src/OpencvTemplateMatchNode.cpp:402`）；`angleMin/angleMax` 默认 -30/30（`:429`–`:432`）；`scaleMode` 默认 0=手动（`:395`–`:396`）。注：新建模板窗的「阈值模式」默认勾选自动（`src/TemplateConfigDialog.cpp:114`–`:116`），这一项与官方一致，不计入缺口；节点 `thresholdType` 默认手动属老方案兼容 | 老方案兼容可以保留，但新方案/新模板的默认必须与 VM 一致，否则用户按 VM 手册操作会得到不同结果 |
| P2-2 | 排序类型的并列规则 | 「按 X 坐标**整数化后**值相同，再按 Y 从小到大」「按 Y 整数化后相同，再按 X」 | 排序直接用浮点比较（`src/OpencvTemplateMatchNode.cpp:1482`–`src/OpencvTemplateMatchNode.cpp:1483`） | 同一整数 X 下、浮点差很小但不相等的目标不会按 Y 兜底，顺序与官方不同 |
| P2-3 | 擦除轮廓点的橡皮擦大小 | 「可单击右下角三角形，滑动滑块调整『橡皮擦』的大小。」（轮廓匹配·特征模板） | 固定半径 20px（`src/TemplateConfigDialog.cpp:608`–`src/TemplateConfigDialog.cpp:616`） | 擦大面积或保护细节时不可调；功能存在但交互不等价 |
| P2-4 | 模板配置窗其余工具 | 官方还有：全屏显示、图像自适应、撤销上一步/恢复至撤销前、复制掩膜、掩膜列表隐藏/显示与逐条屏蔽、模板存图可关闭（轮廓匹配·特征模板） | 配置窗工具条见 `src/TemplateConfigDialog.cpp:43`–`src/TemplateConfigDialog.cpp:87`；没有全屏/撤销/恢复/复制掩膜/逐条隐藏；模板存图是必填，取消即中止创建（`src/TemplateConfigDialog.cpp:910`–`src/TemplateConfigDialog.cpp:917`） | 图像缩放/平移/自适应由 `HalconWindow` 承担，可算部分对齐；其余为缺口 |
| P2-5 | 自订「不发明功能」铁律冲突 | `docs/VM4.4高精度匹配对齐规范.md` 第六节明写「VM 没有的（魔术棒/分割/剪刀等）一律不加」 | 配置窗实际有「自由绘制 / 魔术棒 / 魔术棒+ / 分割 / 剪刀」（`src/TemplateConfigDialog.cpp:52`–`src/TemplateConfigDialog.cpp:65`）；官方 4.4.2 帮助全量检索 `魔术棒`、`剪刀`、`自由绘制` 均零命中 | 要么补官方截图证据并改规范，要么按规范移除/隐藏；现状是规范与实现自相矛盾，且后续维护无法判断哪些是「VM 对齐」、哪些是自有扩展 |
| P2-6 | 结果显示的文本与样式 | 结果显示分「结果判断 / 图像显示 / 文本显示」；图像显示可设置颜色、透明度、粗细；文本显示可设置内容、OK/NG 颜色、字号、透明度、位置 X/Y（结果显示） | 只有检测区域/匹配结果/匹配点/特征点四个显隐开关（`src/OpencvTemplateMatchNode.cpp:398`–`src/OpencvTemplateMatchNode.cpp:401`）；`src` 检索 `文本显示`/`OK颜色`/`NG颜色`/`字号`/`透明度` 无模板匹配侧实现 | 结果判断见 P1-7；这里指显示样式与叠加文本，当前不可配置 |

## 五、文档自身失真（需要先更正声明，避免下一轮继续误判）

| 编号 | 位置 | 现状 | 应改为 |
|---|---|---|---|
| D-1 | `docs/模板匹配进度与交接.md:10`、`:89`–`:91`；`docs/release-registry.json:978`–`:984` | 写「VM 4.4 对齐全部闭合」「VM 4.4 对齐缺口表已全部闭合」 | 按本报告结论改为「主要工程化项已落地；官方语义仍有 P0/P1 缺口，见 `docs/VM4.4对齐声明复核-20261102.md`」 |
| D-2 | `docs/模板匹配进度与交接.md:14` | 面板槽位基准写 298 | 测试现值是 310（`tests/param_panel_binding_test.cpp` 的槽位统计断言；不引行号——该文件用例持续增长，行号必腐）；应按测试读数改为 310，并保留 298→310 的变更说明 |
| D-3 | `docs/VM4.4高精度匹配对齐规范.md:52` 与 `:57` | 同一「最大重叠率」在 52 行写「未做」，57 行写「已做（交叠/较小框面积）」 | 统一为「参数已存在；计算口径与官方不一致（交叠/两框面积和），待修」，并删掉重复行 |
| D-4 | `docs/VM4.4高精度匹配对齐规范.md:56` | 写「超时到点停止搜索，保留已找到的最优」 | 官方是「不返回任何搜索结果」；当前实现是保留最优，属 P0-1，不可写成对齐 |
| D-5 | `docs/VM4.4高精度匹配对齐规范.md:57` | 写最大重叠率「按交叠/较小框面积计」并标已做 | 官方是「交叠/两个重叠目标的总面积」；当前实现与官方相反，属 P0-3 |
| D-6 | `docs/VM4.4高精度匹配对齐规范.md:69` | 写「VM 是两个工具：高精度匹配与快速匹配（形貌/灰度）」 | 官方是四个模块，快速匹配仍是边缘法、灰度匹配才是 NCC；应改为四模块口径，并说明本节点当前覆盖哪一类 |
| D-7 | `docs/VM4.4高精度匹配对齐规范.md:6` | 以「轮廓匹配」页作为「高精度匹配」的唯一依据 | 高精度匹配有独立页 `GUID-11F05391-A171-4F3F-A43C-8A1348B3E02C.html`；两页在尺度 XY（横切角度范围）、是否输出轮廓、增强匹配等处有差异，应分页引用并写明适用范围 |
| D-8 | `docs/VM4.4高精度匹配对齐规范.md:119` | 明令不得新增魔术棒/分割/剪刀 | 实现已加入这些工具；应二选一：补官方截图证据并改规范，或按规范移除/隐藏 |
| D-9 | `docs/模板匹配VM对齐清单.md:3`–`:26` | 基线是 `rc-20261031.6`，仍把「多目标只输出最优 1 个」「无亚像素」「掩膜未参与评分」「无训练向导」列为未对齐 | 这些项在 `rc-20261102.4` 已部分/全部落地；应把该文件标注为历史基线，或按本报告重写「未对齐」表 |

## 六、已核对一致的部分（避免只列缺口）

以下项在代码中能找到对应实现，且有测试或实测数据；未列入上面的缺口：

| 项 | 复核落点 |
|---|---|
| 粗到细搜索、GOC 边缘法、亚像素峰值插值 | `FeatureMatchTest::templateMatchSearchIsFastEnough`、`templateMatchOnRealImage`、`templateMatchSurvivesPartialOcclusion`；`src/OpencvTemplateMatchNode.cpp` 粗搜/精修段 |
| 多目标搜索框架（1~16） | `FeatureMatchTest::templateMatchFindsMultipleInstances`；`src/OpencvTemplateMatchNode.cpp:1237`–`:1431` |
| 图形即参数、搜索区域矩形/多边形、结果叠加 | `FeatureMatchTest::templateMatchOverlayShowsRegionAndTemplate`、`templateMatchMaskMovesBestHit`；`src/ImageDisplayController.cpp` |
| 模板对象、创建/编辑/删除、active 切换、全部模板取最优 | `FeatureMatchTest::templateEntryRoundTripAndLegacyMigration`、`templateMatchUsesActiveTemplateFromList`、`templateMatchAllTemplatesPicksBest` |
| 结果表、匹配点、角度、分数 | `addAction` / `addActionFor`（`src/ModuleEditorDialog.cpp:189-195`）、`:1265`–`:1271`（尺度 Y 见 P0-5） |
| 输出掩膜、图像型 ROI、简化继承 ROI | `FeatureMatchTest::templateOutputsSearchMask`、`templateImageTypeRoiLimitsCandidates`、`templateInheritsUpstreamBoxAsSearchRegion` |
| 匹配中心（锚点）+ 锁定 | `FeatureMatchTest::templateMatchAnchorOffsetsMatchPoint` |
| 多形状模板、屏蔽形状（保留−屏蔽） | `FeatureMatchTest::templateMultiShapeEntryAndMask`、`templateExcludeShapesSubtractFromMask` |
| 阈值类型 3 项、排序类型 6 项、最大重叠率/延拓/超时的参数壳 | `makeIntParam` 系列（`thresholdType` / `matchContrastThreshold` / `sortType` / `maxOverlap` / `extendThreshold` / `timeoutMs`）见 `src/OpencvTemplateMatchNode.cpp:410`–`:421`；语义问题见 P0/P1。**2026-11-08 更正**：原引用 `:377`–`:388` 实为构造函数与 `init()` 头部（`setName` / `addInputSlot`），不含参数壳 |
| 四页签（基本参数/特征模板/运行参数/结果显示） | `m_vmTabs` 容器见 `src/ModuleEditorDialog.cpp:698`，四个 `addTab` 见 `:1447`–`:1451`。**2026-11-08 更正**：原引用 `:522`–`:525` 不成立 |
| 尺度 XY 的搜索能力（只验证分数/尺度参数） | `FeatureMatchTest::templateScaleRangeXYFindsAnisotropicInstance`（输出字段问题见 P0-5） |
| 擦除轮廓点（固定半径） | `FeatureMatchTest::templateErasesRemoveFeaturePoints`（可调半径见 P2-3） |
| 建模板后的参数自动建议 | `FeatureMatchTest::templateSuggestParamsFollowsTemplate` |

## 七、建议处置顺序

1. **先修 P0 五项并各自补一条会变红的腿**：超时无结果、全模板多目标合并、最大重叠率按官方分母、匹配个数与列表同源、尺度 XY 的框/尺度 Y 输出。
2. **再补 P1 的运行参数**：最大匹配个数 1~1000、尺度范围 0.1~10.0、多模板重叠率、是否考虑噪点、是否获取轮廓/输出轮廓、结果判断四项。
3. **再补模板列表与模板配置窗**：载入/排序/勾选、尺度模式拆分（速度尺度自动 ≠ 搜索尺度自动）、链长参数进配置窗、增强匹配、擦除半径可调。
4. **补齐模块结果字段**：匹配状态、检测区域、匹配轮廓信息、屏蔽区多边形、输出掩膜宽高/像素格式；同时把基本参数的继承方式/位置修正方式补上。
5. **最后更正文档与发版声明**：先按第五节改文档，再重跑 `FeatureMatchTest` 与 `ParamPanelBindingTest`，确认新用例能覆盖到上述语义，才可恢复「VM 4.4 对齐闭合」的表述。

## 八、复核限制

- 本报告以随安装包发布的 **4.4.2 帮助文本**为依据；未启动 VisionMaster 界面逐项点验默认值。若官方 UI 与帮助文本有出入（尤其是多模板重叠率默认值、阈值类型默认值），需以现场截图补证。
- 本报告只复核模板匹配/高精度匹配这一条线；`docs/VisionMaster4.4对标分析与路线图.md` 里的全局路线图、其他算子与真机四项未在本轮逐条重核。
- 文中代码行号按提交 `9ddee37` 实测；后续代码改动后，行号可能漂移，复核时以 `git grep` 的当前命中为准。
- 官方帮助里「高精度匹配」与「轮廓匹配」存在范围差异（轮廓匹配多「横切角度范围」「是否输出轮廓」「增强匹配」等）。本报告以高精度匹配页为主，轮廓页只用于补充特征模板流程；若后续要声称对齐「轮廓匹配」，需另开一份复核。

## 九、复现命令

官方帮助原文（在 PowerShell 中直接检索主题页）：

```powershell
Select-String -Path 'D:\Program Files\VisionMaster4.4.0\Applications\Help\CH\GUID-11F05391-A171-4F3F-A43C-8A1348B3E02C.html' `
  -Pattern '最大匹配个数','超时控制','多模板重叠率','是否考虑噪点'
Select-String -Path 'D:\Program Files\VisionMaster4.4.0\Applications\Help\CH\GUID-3EEB6D91-78DA-40BF-AC7B-537FEBB00FFC.html' `
  -Pattern '尺度模式','最小链长','增强匹配','模板存图'
Select-String -Path 'D:\Program Files\VisionMaster4.4.0\Applications\Help\CH\GUID-1552B2A6-F573-4CF0-990D-3B07EC23E074.html' `
  -Pattern '结果判断','文本显示'
```

代码侧证据：

```powershell
git grep -n "maxMatches\|maxOverlap\|timeoutMs\|matchAllTemplates\|minScore" -- src/OpencvTemplateMatchNode.cpp
git grep -n "噪点\|matchStatus\|多模板重叠率\|结果判断\|增强匹配\|是否获取轮廓\|是否输出轮廓" -- src include
git grep -n "fieldSlots == 310" -- tests/param_panel_binding_test.cpp
```

现有测试（注意：这些用例没有覆盖 P0/P1 的语义，跑绿不等于对齐）：

```powershell
cd build
ctest -C Release -R "FeatureMatchTest|ParamPanelBindingTest" --output-on-failure
```

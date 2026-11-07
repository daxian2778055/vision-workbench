# VM 4.4 卡尺工具（IMVSCaliperModu）对齐规范

> 依据：本机官方帮助
> `D:/Program Files/VisionMaster4.4.0/Applications/Help/CH/GUID-59D1E6A2-C5BF-49AE-8F36-6B5A90A23F1A.html`
> （DC.Title = 「卡尺工具」）。命名、分组、语义、取值范围一律以该页为准。
> 对应实现：`src/OpencvCaliperNode.cpp` / `include/OpencvCaliperNode.h`。

## 一、官方工作流（6 步）

> 原文：「将图像区域内数据沿一个方向投影之后得到一维数据，对该数据进行滤波处理，
> 选择局部极值，并对这些极值按照预设的方式计算得分，最终输出符合得分要求的亚像素极值点。」

1. 选投影区域（任意角度矩形，插值采样；**超出图像边界的区域填充其他灰度值**）
2. 投影（相当于沿特定方向的**均值滤波**）
3. **一维差分滤波**：滤波核越大，抑制噪声越强，但极值位置精度越差（官方图 3 核=3 / 图 4 核=5）
4. 按边缘极性选取极值点（滤波后转成极大值/极小值）
5. 极值点插值 → 亚像素位置
6. 极值点评分 → 输出符合得分要求的结果

## 二、官方参数口径（「参数配置」节原文摘要）

| 参数 | 官方定义 | 我们的键 | 状态 |
|---|---|---|---|
| 边缘模式 | 单边缘 / 边缘对 | `edgeMode` 0/1 | ✅ |
| **滤波尺寸** | 增强边缘抑制噪声，**最小值 1**；核越大越稳但精度越差 | `filterSize` 1/3/5/7/9 | ✅ 本轮补 |
| 边缘阈值 | 梯度阈值 0~255，只有大于该值的边缘才被检出 | `threshold` | ✅ |
| **边缘0/1极性** | 从黑到白 / 从白到黑 / 任意极性；**边缘对模式才有「边缘1极性」** | `polarity` / `edge1Polarity` | ✅ 本轮补「边缘1极性」 |
| **边缘对宽度** | 期望输出的边对像素间距；**单独调节无法直接筛选**，仅当各类间距/位置归一化计分开启时作为**计分缩放因子** | `expectedPairWidth`（计分缩放） / `pairWidth`（硬上限，兼容老方案） | ✅ 第二批补 |
| 最大结果数 | 超过则按**分数由高到低**输出 N 个 | `maxCount` | ✅ 第二批改用真分数 |
| **排序方式** | 分数升序 / 分数降序 / 方向正向 / 方向逆向（4 种） | `sortType` 0..3 | ✅ 第一批补，第二批分数改真值 |
| 对比度/边缘对对比度 | 按边缘对比度（边对取均值）计分；最大 1.0 对应对比度 255 | `scoreContrast` | ✅ 第二批补 |
| 间距 / 间距差 / 相对间距差 | `距离/宽度`、`|(距离−宽度)/宽度|`、`(距离−宽度)/宽度`（双边） | `scoreSpacing` / `scoreSpacingDiff` / `scoreRelativeSpacingDiff` | ✅ 第二批补 |
| 位置 / 相对位置 / 归一位置 / 归一相对位置 | 候选中心相对**投影区域中心点**的绝对/带符号位置差；归一两项分母 = 边缘对宽度 | `scorePosition` / `scoreRelativePosition` / `scoreNormPosition` / `scoreNormRelativePosition` | ✅ 第三批补 |
| 灰度 | 按边缘灰度或边缘对灰度均值计分 | `scoreGray` | ✅ 第三批补 |
| 计分曲线 | 单边（递增/递减）与双边函数，`X0/X1/XC/Y0~Y1`（XML 命名：起点/X中点/终点/Y计分） | 每项独立：`contrast*/position*/spacing*/…` + 双边 `*H` | ✅ 第十一批：每项 6 参数，默认值照 XML |
| **参数条件显隐** | 官方多处注明「**边缘模式设置为边缘对时，才有该参数**」（边缘1极性、边缘对宽度、归一位置、归一相对位置、间距、间距差、相对间距差）；「单边缘时该参数为**对比度**，边缘对时为**边缘对对比度**」 | `applyParamVisibility()` | ✅ 第五批补 |
| 投影方向 | 矩形 ROI：纵向投影（投向矩形宽）/ 横向投影（投向矩形高）；扇环 ROI：径向 / 环向 | `projectionDirection` 0=横向（老行为）/ 1=纵向 | ✅ 矩形两向已做；扇环 ROI 用户决定不做 |
| 模糊边缘 | 开启增强候选边缘点集提取能力，获取更多候选点集数，耗时增加明显 | `blurEdge` | ⚠️ 按描述实现（见 §三第六批） |
| 计分方式 | 位置评分（绝对/相对/归一化位置）、间距评分（间距差/相对间距差/间距）、对比度评分（对比度/灰度）；单边/双边计分函数 | `score*` 9 个开关 + 总分 N 次方根 | ✅ 第二/三批补 |

**极性枚举说明（第七批改为与官方逐位一致）**：官方 UI 顺序是「从黑到白 / 从白到黑 / 任意极性」。
旧内部编号是 `0=任意 / 1=白到黑 / 2=黑到白`，第七批改成 **`0=从黑到白 / 1=从白到黑 / 2=任意极性`**，
与官方 UI 顺序一致、默认 2（任意）保持老方案行为。项目未上线，按用户决定不做旧方案迁移；
旧工程里存的 0/1 会被解释成新的固定极性，需要重新设置极性参数。

## 三、本轮实现映射（默认值 = 老行为）

| 项 | 实现 |
|---|---|
| 滤波尺寸 | `side=(filterSize-1)/2+1`，`grad[m] = mean(p[m+1..m+side]) − mean(p[m-side+1..m])`。`filterSize=1` 时 `side=1`，逐位等于老的一阶差分。**不能**写成「先盒式平滑再差分」——代数上会约掉中间项只剩两个端点，噪声没被平均掉（实测 k=5 反而报出更多伪边缘）。 |
| 极值检测 | 改为非极大值抑制 `|g[m]| >= |g[m-1]| && |g[m]| > |g[m+1]|`。旧逻辑要求梯度符号跨零，只对一阶差分下「阶跃两侧恰为 0」成立；滤波核放大后梯度峰两侧同号 → 真实边缘会被全部漏检（实测 k=5 得 0 条）。 |
| 边缘1极性 | 边对配对时约束第二条边（`edge1Polarity` 0=从黑到白 / 1=从白到黑 / 2=任意，第七批统一编号）。 |
| 排序方式 | 0=方向正向（采集顺序，老默认）、1=分数降序（老按对比度）、2=分数升序、3=方向逆向（结果集 reverse）。 |
| 越界填充 | 旧 `sampleGray` 越界返回 **0**，在亮背景上会凭空造出「亮→黑」伪边缘；改为**边界复制**（clamp 坐标）保持亮度连续。官方只说「填充其他灰度值」，边界复制是其中一种具体取法。 |

### 第二批：极值点评分 + 边缘对宽度语义

| 项 | 实现 |
|---|---|
| `expectedPairWidth` | 官方「边缘对宽度」的**真实语义**：只作计分缩放因子，**不直接筛选**。默认 0 = 不启用间距评分，老方案行为不变。`pairWidth` 保留为硬上限（老语义），两者并存。 |
| 4 个计分项 | `scoreSpacing`（官方「间距」，用 `|距离/宽度−1|` 表示偏差）、`scoreSpacingDiff`（官方「间距差」`|(距离−宽度)/宽度|`）、`scoreRelativeSpacingDiff`（官方「相对间距差」`(距离−宽度)/宽度`，双边）、`scoreContrast`（官方「对比度」，`对比度/255`，最大 1.0，边对取均值）。 |
| 计分函数 | 官方只给**示意图**、未给解析式。按分段线性 S 曲线实现：以 `x0/xc/x1` 把输入归一化到 [0,1]，`xc` 为拐点，`y0~y1` 为值域；`curveType=1`（默认递减）时偏差越小分越高，0（递增）反转。双边项用左右镜像合并、理想值 0。**这是按示意图的实现，不是逐位复刻**。 |
| 综合分数 | 各启用项得分相乘，值域 [0,1]；全部关闭时退化为 `contrast/255`（老行为）。结果写到 `edgeScores`（逐结果）与 `avgScore`。 |
| `maxCount` | 启用任一计分项时，先按分数降序保留前 N 个（官方原文），再按 `sortType` 排显示顺序；未启用计分时走老路径（按 sortType 顺序截断）。 |
| `sortType` | 1/2 的「分数」改用真 `score`；无计分项时与老「对比度」单调一致。 |

**已知简化（如实记录，不假装对齐）**：官方每个计分项各有**一组独立**曲线参数，当前实现为**一组共享**参数；因此「间距」与「间距差」在共享曲线下数值相同（两者公式本就等价，区别在可单独配曲线）。

### 第三批：位置类计分 + 灰度计分 + 模块结果字段

先纠正一次判断：第二批交付时曾写「位置类计分需先把 ROI 从线段扩成矩形投影区域」，**该判断过度**。官方「投影区域中心点」在我们的几何里已有对应物——矩形 ROI = 卡尺线 × ±`halfWidth`，中心就是**卡尺线中点**；候选沿法线的位移 `offset = sub − half` 正是「相对投影区域中心的位置差」，无需架构改动。

| 项 | 实现 |
|---|---|
| `scorePosition` | 官方「位置」：绝对位置差，两侧皆正 → `|offset| / halfWidth` |
| `scoreRelativePosition` | 官方「相对位置」：位置差可正可负 → `offset / halfWidth`，**不取绝对值**（负侧由曲线 `x0` 决定） |
| `scoreNormPosition` | 官方「归一位置」：绝对归一化距离，分母 = **边缘对宽度** → `|offset| / expectedPairWidth` |
| `scoreNormRelativePosition` | 官方「归一相对位置」：归一化位置差可正可负 → `offset / expectedPairWidth` |
| `scoreGray` | 官方「灰度」：边缘处投影灰度（亚像素线性插值），边对取两边缘均值，输入 `gray/255` |
| 口径修正 | 官方「如果是边缘对查找模式，则使用**两个边缘的平均对比度**作为评分因子」；此前 `PairHit::contrast` 用 `min`，已改为均值 |
| 计分曲线 | `scoreOfDeviation` 不再强制 `fabs`：相对位置/归一相对位置的负侧要保留符号，否则与绝对位置项完全重复 |

**官方「模块结果」字段对齐**（逐结果按顺序输出，与端口 1 的 `extraValues` 同序）：

| 官方字段 | 我们的键 | 说明 |
|---|---|---|
| 耗时（ms） | `elapsedMs` | `QElapsedTimer` |
| 模块状态 | `moduleStatus` | 0=NG / 1=OK |
| 边缘个数 | `edgeCount` | |
| 分数 | `edgeScores` / `avgScore` | 逐结果分数 + 平均 |
| 边缘*极性 | `edgePolarities` | int，1=从黑到白 / 2=从白到黑 / 3=任意（官方三种取值，检测所得只会是 1 或 2） |
| 边缘*位置 | `edgePositions` | 沿法线相对投影区域中心的位置差 `offset` |
| 边缘点*X/Y | 端口 1 `MeasureResult.extraValues` | 逐结果 (col,row) |
| 测量宽度 | `avgWidth` / 端口 3 `extraValues` | 平均 + 逐边对宽度 |
| 直线*起点/终点 X/Y | 端口 3 `extraValues` | 逐边对 (col1,row1,col2,row2,width) |
| 圆弧*（中心点/半径/角度） | — | 扇环 ROI 专用，用户决定不做（见 §四） |

### 第四批：投影方向（矩形 ROI）

官方原文：「纵向投影…针对矩形 ROI，投向矩形宽」「横向投影…针对矩形 ROI，投向矩形高」，
并由扇环的类比（「边缘点都在一个圆上 / 都在一个半径上」）可知 **「投向 X」= 一维信号沿 X 方向排列**。
再次纠正一次判断：第三批交付时写「投影方向需要 ROI 几何扩展」，**并不需要**——
同一个矩形 ROI（卡尺线 × ±`halfWidth`）只需交换「信号轴」与「剖面轴」。

| 项 | 实现 |
|---|---|
| `projectionDirection=0` | 横向投影（老行为）：一维信号沿法线 `n`，`K+1` 个剖面沿卡尺线 `u` 排列。重构后全部老腿读数逐位不变（除下述半采样修正） |
| `projectionDirection=1` | 纵向投影：一维信号沿卡尺线 `u`，`M` 个剖面沿法线 `n` 排列，剖面中心 = 卡尺线中点 + 法线偏移 |
| 轴抽象 | `nSig/nProf/sigHalf/sigSpacing/sx,sy/qx,qy`，梯度滤波、极值检测、灰度插值、配对逻辑全部复用；`hit.offset`（位置评分输入）在纵向时取剖面法线偏移，保证语义恒为「相对投影区域中心沿法线的位移」 |
| 精度修正 | `grad[m]` 的物理位置在 `m` 与 `m+1` 之间，亚像素基准由 `m` 改为 **`m+0.5`**。旧实现让边缘位置系统性偏半个采样间隔（step=5 的纵向投影把 y=100 报成 95，修正后 97.5）。**这会微调老方案的边缘点坐标（半像素），但边对宽度不变** |

**数值腿** `NodeCoreTest::testCaliperProjectionDirection`（`tests/node_core_test.cpp`）：
- 水平边缘 y=100：横向投影 `edgeCount=0`，纵向投影 `61` 条、`firstRow=97.5`
- 细采样反证：step 5→1 时 `firstRow` 从 97.5 收敛到 **99.50**
- 竖直边缘 x=100（互为反证）：横向投影 `21` 条，纵向投影 `edgeCount=0`

**数值腿** `NodeCoreTest::testCaliperPositionScoringAndResults`（`tests/node_core_test.cpp`）：
- 两条宽 20 的亮带，中心分别距投影区域中心 −5 / +45 px；三种位置口径 + `maxCount=1` 都稳定留下 `offset=−5.00` 的边对
- 灰度计分：`avgScore=0.5098`
- 结果字段：`n=12` 时 `edgePolarities / edgePositions / edgeGrays / edgeScores` 各 12 项，极性只取 1/2，灰度在 0~255

**数值腿** `NodeCoreTest::testCaliperScoring`（`tests/node_core_test.cpp`）：
- 默认（计分全关）：12 个边对、宽 20 与宽 15 都在，分数全相等（`avgScore=0.5882`）
- 启用间距计分 + 期望宽度 20：`w20=1.0000 / w15=0.7500`，两种宽度**都保留**（证明不硬筛选）
- `maxCount=1`：留下宽度 20 的边对
- `curveType=0` 反转：留下宽度 15 的边对

### 第五批：参数条件显隐（UI 操作性）

沿用项目已有先例 `OpencvBlobNode::applyParamVisibility`：自动面板把控件 `objectName`
设成参数名，用 `QFormLayout::setRowVisible` 连标签一起隐藏；`createParamPanel` 连接
`edgeMode` 下拉与 9 个计分 checkbox 的切换信号，`updateParamPanel` 末尾再刷一次（面板恢复路径）。

| 条件 | 显隐 |
|---|---|
| `edgeMode=0` 单边缘 | 隐藏 `pairWidth / edge1Polarity / expectedPairWidth / scoreSpacing / scoreSpacingDiff / scoreRelativeSpacingDiff / scoreNormPosition / scoreNormRelativePosition` |
| `edgeMode=1` 边缘对 | 上述全部显示 |
| 任一计分开关启用 | 显示 `curveType / curveX0 / curveXc / curveX1 / curveY0 / curveY1`；全关时隐藏 |
| `scoreContrast` 标签 | 单边缘显示「对比度计分」，边缘对显示「边缘对对比度计分」（官方原文） |

**数值腿** `NodeCoreTest::testCaliperParamVisibility`（`tests/node_core_test.cpp`）：
单边缘 → 边对 → 单边缘的显隐切换、标签文案变化、计分开关→曲线组联动、`updateParamPanel` 后显隐保持，共 5 组断言。本批**不新增参数**，`fieldSlots` 仍为 442（第七批加 `showCaliperEdge` 后为 444）。

**一次明确的取舍**：官方对 9 个计分项都重复同一句「曲线类型/起点/X中点/终点/Y计分」，
但**没有写明是每项各一组、还是共用一组**。若按"每项独立"硬做就是 9×6=54 个参数，属于臆造；
当前保持一组共享曲线并靠条件显隐收敛 UI，歧义留在 §四第 1 条，等能查到 VM 实际界面或实测再定。

### 第六批：模糊边缘

官方原文：「开启后，可增强候选边缘点集的提取能力，获取更多候选点集数，从而可在"干扰点较多的图像"
或"边缘模糊的图像"场景中更大可能地提取到目标边缘点，但耗时增加明显。」

**未给具体算法**。当前实现：`effThr = blurEdge ? thr/2 : thr`，即把极值门槛减半以纳入更弱的候选。
**如实说明**：这实现了「更多候选点集」的效果，但**没有复刻官方的额外计算开销**（官方说耗时增加明显，
说明它做了不止降阈值的事）；若后续能实测 VM 的候选集差异，再按差异调整。

**数值腿** `NodeCoreTest::testCaliperBlurEdge`（`tests/node_core_test.cpp`）：
弱边缘对比度 30、阈值 50 → 关闭 `edgeCount=0`，开启 `edgeCount=22`。

### 第七批：「当前结果」字段口径 + 画布叠加 + 极性枚举

官方「模块结果」字段表（GUID-59D1E6A2「模块结果」节）逐项照抄：

| 官方字段 | 我们的键 / 渲染 |
|---|---|
| 耗时（ms） | `elapsedMs`，标签「耗时(ms)」，0 位小数 |
| 模块状态 | `moduleStatus`，显示 `OK`（绿）/ `NG`（红） |
| 边缘个数 | 单边缘 `edgeCount`；边对模式显示「边缘对个数」= `pairCount` |
| 分数 | `edgeScores`，按结果分组「边缘%1/边对%1 → 分数」，4 位小数 |
| 边缘*极性 | `edgePolarities`，编号 1/2/3 → 从黑到白 / 从白到黑 / 任意极性 |
| 边缘*位置 | `edgePositions`，单位 px，2 位小数 |
| 边缘点*X/Y | `edgePoints`（`col,row;…`）→「X=…, Y=…」 |
| 灰度 | `edgeGrays`，1 位小数 |
| 测量宽度 | `avgWidth/minWidth/maxWidth`（边对模式），单位 px |
| 直线*起点/终点 | `pairLines`（`c1,r1,c2,r2,w;…`）→ 起点→终点+宽度 |
| 检测区域 | `roiCenterX/Y`、`roiWidth/Height/Angle`（中心点/宽/高/角度） |
| 圆弧* | 用户决定不做（扇环 ROI，见 §四） |

实现：
- 新增 `include/NodeResultFields.h` 的 `NodeResultField`（key/label/group/unit/decimals/kind）与
  `NodeBase::resultDisplayFields()` 虚钩子；`ResultTablePanel::setModuleResult` 增加可选 `fields` 参数，
  非空时**只**按声明顺序渲染结果字段（配置参数不再混进结果表），数组按「边缘0/边缘1…」分组展开，
  CSV/报告按「分组 子项」扁平化；判红原因（`lastError` / `*Note`）仍强制补在末尾。
- 画布叠加：`caliper` 边缘点受 `showCaliperEdge` 显隐；`caliperPair` 每个边对都画连线 + 「边对N 宽=…」
  （旧实现只在最后一个边对写宽度）。序号与「当前结果」的「边对0/边对1…」分组一致（0 起）。
- 极性参数 `polarity / edge1Polarity` 由 `makeIntParam` 改 `makeEnumParam`，UI 顺序即官方顺序。

**数值腿**：`ResultTableTest::testResultFieldSpecDisplay`（字段顺序、配置参数不出现、分组展开、
极性中文、单位/精度、原因补位、CSV/报告扁平化）；`fieldSlots` 444（新增 `showCaliperEdge`；
第八批加 6 项右曲线参数后为 450；第十批加 `scorePairContrast` 后为 451）。

### 第八批：计分曲线解析式（官方图 6/7/8/9 + 模块 XML）

依据（比截图更权威）：
- 官方帮助图 6「单边计分函数（递增）」、图 7「单边计分函数（递减）」、图 8/9「双边计分函数」；
- 本机模块 XML `IMVSCaliperModuCs.xml`：命名是 **X0=起点 / X1=X中点 / XC=终点 / Y计分 0~100**；
- `IMVSCaliperModuAlgorithm.xml`：各计分项的默认值（如 ContrastX1=0、ContrastXC=255、
  PositionX1=100、SizeDiffNormX1=1、SizeDiffNormAsymX0=-1/XCH=2、HalfKernelSize=2、ContrastTH=15 等）；
- 官方公式图：`OverallScore = (Score1 × Score2 × … × ScoreN)^(1/N)`。

实现：
- 单边曲线改为**分段直线**（旧实现是两段斜率的 S 曲线，形状不对）：
  - 递增：`x0~x1` 平于 `y0`，`x1~xc` 线性升到 `y1`，之后恒 `y1`；
  - 递减：`x0~x1` 线性由 `y1` 降到 `y0`，之后恒 `y0`。
- 双边（相对间距差）：负侧用左曲线、正侧用右曲线，各带一组 `*H` 参数
  （XML 里就是 `SizeDiffNormAsym*` 与 `*DropH/*X0H/*X1H/*XCH/*Y0H/*Y1H`）。
- Y计分范围改 **0~100**（XML 口径），最终总分除以 100 归一化回 `0~1` 供结果面板/排序使用。
- 总分合并改为官方公式的 **N 次方根**（旧实现直接连乘，启用多项时会越乘越小）。
- 「滤波尺寸」按 XML `HalfKernelSize` 语义：半核尺寸 1~50、默认 2，实际核宽 `2h+1`
  （与帮助图 4「滤波核尺寸=5」一致）。边缘阈值默认改 15（XML `ContrastTH=15`）。

**已知简化（如实记录）**：
- 官方每个计分项各有一组曲线参数（XML 里 `ContrastX0/X1/XC/Y0/Y1/Drop`、
  `Position*`、`SizeNorm*` …共 9 组）；本批先保留一组共享曲线，**第十一批已拆成每项独立**。
- 各计分项的横轴输入：本批仍统一归一化为「偏差量」（对比度/灰度取 `1−值/255`，
  位置取 `offset/半宽` 或 `offset/边缘对宽度`，间距取 `|距离/宽度−1|`）；
  官方 XML 的默认 X 范围是按各物理量给的（如对比度 0~255、位置 0~100px），
  **第十一批已改回物理量输入**。

**数值腿（本批当时）**：`testCaliperScoring` 的 ②（期望宽 20 得分 1.0、宽 15 得分 0.5）与
④（递增 + 中点收紧到 0.1/0.4，偏差 0.25 得 0.5 并胜出）；其余位置/极性/投影用例全绿。
第十一批拆成每项独立曲线后，②的现值变为 0.20 / 0.15（见第十一批数值腿）。

### 第九批：边缘对配对策略（工程改进）

官方未给配对算法（原文只说最终输出「符合得分要求的亚像素极值点」）。旧实现是**原始序列相邻两条边**
配对，一条夹在中间的噪声边就会把真实边对拆散。

新口径：
- **候选对**：同一剖面内任意两条**极性相反**、间距 ≤ `pairWidth` 的边缘（同极性噪声边夹在中间不影响候选）；
- **选优**：候选对先用同一套极值点评分函数打分，再按分数降序**贪心**接受；
- **约束**：每条边最多属于一个边对（非重叠），冲突时分数高者优先；
- **输出顺序**：贪心后先恢复采集顺序，再按「排序方式」最终排序（0=采集顺序、1=分数降序、
  2=分数升序、3=采集逆序）。同时修掉旧实现「启用 `maxCount` + 计分时输出残留分数顺序」的问题；
- **防御**：单剖面候选对封顶 4096（`pairWidth` 极大 + 高噪声图时防止平方增长）。

**数值腿**：
- `NodeCoreTest::testCaliperPairSelection`：亮带内压一条 2px 暗线（边缘序列 上升/下降/上升/下降），
  旧相邻配对只能得到 10/8，新策略仍输出真实宽 20 的边对（`pairs=12, true20=1`）。
- `testCaliperScoring` ③/④ 的 `pairWidth` 收到 30：放宽到 200 时两条亮带之间的暗区间（宽 90）
  也会成为合法候选对，反转曲线会优先它；用例目的只是验证曲线方向，故只保留亮带内部候选对。
  maxCount 仍分别留下 20 / 15。
- 其余卡尺用例（极性互补、位置评分、投影方向、显隐、模糊边缘、角度面积）全绿。

### 第十批：默认值对齐（官方 Algorithm XML）

官方 `IMVSCaliperModuAlgorithm.xml` 给出的默认值（此前我们为了兼容老方案一直没动）：

| 参数 | 官方默认 | 本批处理 |
|---|---|---|
| `EdgeMode` | 1（边缘对） | `edgeMode` 默认改 1 |
| `Maximum` | 1（范围 1~1000） | `maxCount` 默认 1、范围 1~1000，去掉「0=不限」 |
| `ContrastEnable` / `ContrastPairEnable` | True / False | 拆成 `scoreContrast`（默认 true）与 `scorePairContrast`（默认 false），按边缘模式互斥显示 |
| `SizeDiffNormEnable`（间距差） | True | `scoreSpacingDiff` 默认改 true |
| `EdgePairWidth` | 10 | `expectedPairWidth` 默认改 10 |
| 其余计分项 | 全部 False | 保持 false |

共享曲线默认值也改为 `X0=0 / X1=1 / XC=1 / Y0=0 / Y1=100`（与 `SizeDiffNorm` 的官方默认一致）。

**连带修正**：曲线参数只在**当前模式下可用**的计分项启用时显示。单边缘模式下
`scoreSpacing/scoreSpacingDiff/scorePairContrast/scoreNormPosition/scoreNormRelativePosition`
不可用（宽度恒为 0），它们即使勾选也不再把曲线参数带出来；
`scorePairContrast` 只在边缘对模式出现，`scoreContrast` 只在单边缘模式出现。

**数值腿**：`testCaliperParamVisibility` 改为验证「默认边缘对 + 两个对比度开关互斥」；
`testCaliperVmParams` / `testCaliperScoring` / `testCaliperPositionScoringAndResults` /
`testCaliperProjectionDirection` / `testCaliperBlurEdge` / `testCaliperPairSelection` /
`opencv_angle_area_test` 都显式声明 `edgeMode` 与 `maxCount=1000`，保证各自只验证本用例语义。
`fieldSlots` 450→451（新增 `scorePairContrast`）。

### 第十一批：每个计分项独立曲线（官方 XML 分组）

官方 `IMVSCaliperModuCs.xml` 已证实每个计分项各有一组曲线参数（此前只有「一组共享曲线」）：

| 计分项 | 参数前缀 | 默认（XML） |
|---|---|---|
| 间距 | `spacing*` | 递增, X0=0/X1=0/XC=5, Y0=0/Y1=100 |
| 间距差 | `spacingDiff*` | 递减, X0=0/X1=1/XC=1 |
| 相对间距差 | `spacingDiffRelative*`（左）+ `*H`（右） | 左：递增 X0=-1/X1=-1/XC=0；右：递减 X0H=0/X1H=2/XCH=2 |
| 对比度 / 边缘对对比度 | `contrast*`（共用一组） | 递增, X0=0/X1=0/XC=255 |
| 位置 | `position*` | 递减, X0=0/X1=100/XC=10000（像素） |
| 相对位置 | `positionRelative*` | 递减, X0=-100/X1=100/XC=10000 |
| 归一位置 | `positionNorm*` | 递减, X0=0/X1=5/XC=5（偏移/边缘对宽度） |
| 归一相对位置 | `positionNormRelative*` | 递减, X0=-5/X1=5/XC=5 |
| 灰度 | `gray*` | 递增, X0=0/X1=0/XC=255 |

实现：
- 每个计分项 = 开关 + 紧跟的 6 个曲线参数；面板里只在该项启用时显示它的曲线（`applyParamVisibility` 按开关逐项 gate）。
- 横轴输入改回**物理量**（官方默认值就是按物理量给的）：对比度/灰度 0~255、位置 px、
  归一位置 = 偏移/边缘对宽度、间距 = 距离/边缘对宽度。
- 相对间距差改为真正的双边：带符号偏差，负侧走左曲线、正侧走右曲线（图 8/图 9），
  不再用 |偏差| 镜像。

**数值腿**：`testCaliperScoring` ②（间距默认递增曲线：比值 1.0 得 20 分、0.75 得 15 分，
分数 0.20 vs 0.15）与 ④（间距曲线改递减后 15px 边对胜出）；`testCaliperParamVisibility`
改为逐项曲线显隐（默认间距差曲线可见、对比度关闭后其曲线隐藏、启用位置后位置曲线出现）。
`fieldSlots` 451→499。

### 第十二批：结果判断（官方 XML）

官方 XML 的 6 类判断，各带 `Enable + Low + High`，共 18 个参数（默认全关）：

| 判断 | 参数前缀 | 默认 | 判定对象 |
|---|---|---|---|
| 数量判断 | `numLimit*` | 关, 0~99999 | 结果数（单边缘=边缘数、边对=边对数） |
| 宽度判断 | `edgeWidthLimit*` | 关, 0~99999 | 每个输出边对的宽度（仅边对模式） |
| 边缘点0X/Y | `edge0PointX/YLimit*` | 关, ±99999 | 第一个结果的第一端点（单边缘=该边缘点） |
| 边缘点1X/Y | `edge1PointX/YLimit*` | 关, ±99999 | 第一个边对的第二端点（仅边对模式） |

实现：任一项不满足 → `moduleStatus=false`，原因写 `judgeNote`（`*Note` 属判红原因口径，
随失败轮推到「当前结果」面板）；输出结果仍保留，便于现场看到越界值。
宽度判断与边缘点1判断在单边缘模式下隐藏。

**口径说明**：官方只给参数名，未写明「边缘点0/1」对应哪一个结果；我们取**第一个输出结果**
（边对模式即第一条边的两个端点），已登记为已知口径。

**数值腿** `NodeCoreTest::testCaliperJudgment`：数量 50 对 [1,10] → NG、对 [1,1000] → OK；
边对宽 20 对 [30,40] → NG、[18,22] → OK；首个边缘点 X≈109.5 对 [0,50] → NG、[100,120] → OK。
`fieldSlots` 499→517。

**已知边界（第十四批已解决）**：原框架 `HalconNode::execute` 在 `moduleStatus=false` 时会清空输出，
判 NG 后画布不显示结果。第十四批新增「判 NG 但结果有效」标记，卡尺/Blob 判 NG 时保留并继续发布
输出（端口 0 聚合结果 + 子端口原样保留），画布可显示红色结果、结果面板能看到越界值 + 原因；
异常/执行失败仍按原逻辑清空。

### 第十三批：清理官方没有的 pairWidth + 位置单位确认

- **删除 `pairWidth` 硬上限**：官方 XML 没有这个参数，官方明确「边缘对宽度只作计分缩放因子、
  不直接筛选」。现在候选对 = 同一剖面内任意两条极性相反的边缘，由评分贪心 + 非重叠选优；
  单剖面候选对封顶 4096 防平方增长（正常图像远达不到）。
- `expectedPairWidth` 范围按 XML 改为 **1~10000**（去掉「0=不启用间距评分」的自造语义）。
- **位置单位确认**：官方 XML 的 `PositionX1=100`、`PositionXC=10000`，即位置输入就是**像素**；
  我们一直用 `offset` 像素，口径正确，原 §四第 4 条关闭。
- 旧工程里存的 `pairWidth` 键加载后不再对应任何参数（会被忽略）；项目未上线，不做迁移。

**数值腿**：原先靠 `pairWidth=30/200` 限制候选的用例改为用计分区分候选——
`testCaliperScoring` ②/③ 改用「间距差」（宽 20 偏差 0 → 100 分、宽 15 偏差 0.25 → 75 分，
远距离候选偏差 ≥1 → 0 分），maxCount=1 仍留下 20；④ 间距递减仍留 15。
`fieldSlots` 517→516。

### 第十四批：判 NG 保留结果输出（框架级）

VM 是「模块变红但结果仍显示」；原框架在 `moduleStatus=false` 时清空端口，判 NG 后什么都看不到。

实现：
- `NodeBase` 新增 `setResultNg/isResultNg`：标记「业务判 NG 但结果有效」，每轮 `process()` 先复位。
- `HalconNode::process`：`moduleStatus=false` 且 resultNg 时**不清理**输出，照常发布聚合输出
  （端口 0 = 图像 + 全部结果，子端口 1..N 原样保留），只返回 false；异常/执行失败仍清空。
- `FlowExecutor` 失败分支：resultNg 时不清端口、不清本轮缓存；结果快照除原因键外，
  按 `resultDisplayFields()` **只推结果字段**（配置参数不混入），结果面板按「模块结果」渲染红色值。
- 卡尺结果判断（数量/宽度/边缘点）与 Blob 结果判断都置该标记。
- 画布叠加配色：判 NG 且保留结果时，结果图元由绿改为红色（`ImageDisplayController`
  的 `resultColor = ng ? 红 : 绿`；边对连线/宽度文字、角度线、模板框等同步）。

**数值腿**：`testCaliperJudgment` ①b —— 走 `execute()` 判 NG（数量 50 对 [1,10]）后：
`execute()` 返回 false、`isResultNg()=true`、`getOutputData(1)` 非空且 `extraValues` 仍在；
`collectOverlayFromNode` 产出的图元颜色为红色（R>200、G<100）。

### 第七批：排序采集顺序与数值边界（P1 收口）

| 项 | 实现 |
|---|---|
| 亚像素 clamp | `sub = qBound(m, m+0.5+0.5*(a0-a2)/denom, m+1)`，防二次项接近 0 时插值发散导致 `int(floor(sub))` 未定义、灰度外插、坐标溢出。**如实说明**：在 NMS 条件 `a1>=a0 && a1>a2` 下，令 `u=a1-a0>=0, v=a1-a2>0`，偏移 `=0.5*(u-v)/(u+v) ∈ (-0.5,0.5]`，即 `sub` 本就恒在 `(m,m+1]`；所以这是**防御性加固**，不会改变正常结果，不是修一个会复现的错值。 |
| 采集顺序排序键 | `EdgeHit/PairHit` 增 `sigPos`（沿一维信号轴的亚像素位置）；`byAcquisitionPair/Edge` 由 `(profile, offset)` 改为 `(profile, sigPos)`。纵向投影时 `offset` 来自剖面法线位移、同剖面内恒定，用它会把边对贪心选优的残留顺序当成「方向正向」。 |
| 单边分支恢复 | `sortType=0/3` 的单边分支也执行 `stable_sort(byAcquisitionEdge)`。此前只有边对分支恢复，`maxCount + 任一计分项` 会先按分数排序，「方向正向」实际输出分数顺序。 |
| 参数边界 | `halfWidth/step/threshold/edgeMode` 用 `qBound` 夹到注册范围，防负向量 size 与 `L/step` 除零；`threshold` 回退 10→15、`edgeMode` 回退 0→1（`run` / `applyParamVisibility` / `resultDisplayFields` 三处），与 `registerParams` 默认值一致。 |

**数值腿** `NodeCoreTest::testCaliperAcquisitionSortOrder`（`tests/node_core_test.cpp`）：
- 单边 + 位置评分 + `maxCount=12` + `sortType=0`：12 个边缘点按 `(row↑, col↓)` 采集顺序（卡尺线竖直 ⇒ 法线朝 −x）
- 纵向投影 + 边对贪心 + `sortType=0`：33 个边对，同剖面内按信号轴 y 递增
- 所有边缘点坐标有限且不溢出图像（守住亚像素 clamp）

**第一批数值腿** `NodeCoreTest::testCaliperVmParams`（`tests/node_core_test.cpp`）：
- 滤波尺寸：固定椒盐噪声图、阈值 60 → `size1=50 / size5=43`（噪声被抑制，42 条真实边缘不丢）
- 边缘1极性：亮带边对 `any=21 / 从黑到白=0 / 从白到黑=21`（两种固定极性互补覆盖全部边对）
- 排序：方向正向首点 `(109.5,50)` → 方向逆向 `(89.5,150)`
- 越界填充：卡尺贴近左边界、剖面大量越界的均匀亮背景 → `edgeCount=0`

## 四、剩余差距（下一批）

1. **模糊边缘的具体算法**：当前只做了「降低极值门槛」，未复刻官方的额外计算（见 §三第六批）。
2. ~~**扇环 ROI**（径向/环向投影，含「圆弧*」结果字段）~~：**用户决定不做**（2026-11-07 明确排除）。
   矩形 ROI 的纵向/横向投影已做，本工具只覆盖矩形 ROI；「圆弧*」结果字段随之不做。

（第七批已关闭原第 8 条：结果字段口径与画布叠加样式已按官方字段表实现。
第八批已关闭原第 2 条：计分曲线改为官方分段直线 + N 次方根合并。
第九批已关闭原第 3 条：边缘对配对改为「异极性候选 + 评分贪心 + 非重叠」。
第十批已关闭默认值对齐。
第十一批已关闭原第 1 条：每个计分项独立曲线参数按官方 XML 拆分完成。
第十二批已关闭原第 5 条：结果判断（数量/宽度/边缘点）按官方 XML 实现。
第十三批已关闭原第 3/4 条：删除官方没有的 pairWidth、确认位置单位为像素。）

## 五、约束

1. 不发明 VM 没有的参数；新增参数先写进本表并注明官方原文。
2. 默认值必须保持老方案行为（`filterSize=2`（半核尺寸，核宽 5）/ `edge1Polarity=2`（任意极性）/ `sortType` 0、1 语义不变）。
3. 每项改动必须带数值腿；不要只断言「执行成功」。

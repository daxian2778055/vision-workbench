# VisionMaster 4.4 界面与操作对齐复核（整体面板 / 交互 / 规则）

- 复核日期：2026-11-03
- 复核对象：`e:\halcon\2\xin1`（VisionFlowPlatform，Qt6 + HALCON 24.11 + OpenCV）
- 官方依据：本机 VisionMaster 4.4.0 帮助（`D:\Program Files\VisionMaster4.4.0\Applications\Help\CH`），HTML 原文已抽取为纯文本（临时目录 build/vmhelp_ui，不入库）
- 复核范围：主界面布局、面板、流程搭建/运行操作、模块配置规则、结果显示、图像显示、菜单/工具栏/快捷键
- 不在范围：模板匹配算法语义与参数正确性（见 [VM4.4对齐声明复核-20261102.md](VM4.4对齐声明复核-20261102.md)）
- 复核方式：官方帮助逐页取证 + 源码逐行核对；未启动 VM 图形界面做像素级比对，也未修改代码
- **阅读提示**：正文（第三～十一节）保留 2026-11-03 基线结论，不代表当前状态；已闭合项与剩余缺口以文末
  [第十二节](#十二2026-11-05-更新框架纯页签--二值化试点) 和 [第十三节](#十三2026-11-06-更新结果显示三项配置落地) 为准。

## 一、结论

**不成立。**“模板匹配四页签”只是**单算子局部样板**，不是平台的全局 UI 规则；整体面板结构、流程搭建习惯、模块窗口规则、结果显示方式均与 VM 4.4 有明显差距。

- P0：4 项（数据丢失风险 1 项、规则直接冲突 3 项）
- P1：10 项（核心操作路径缺失或入口不一致）
- P2：13 项（面板细节、辅助能力、命名与密度）

最关键的四个事实：

1. 关闭流程标签（或按 Ctrl+Shift+T）会**立即销毁流程场景**，无确认、无撤销。
2. “基本参数 / 运行参数 / 结果显示”三页签只在 `OpencvTemplateMatchNode` 上生效，其余算子仍是平铺长表单。
3. 模板专属工具条按钮（训练模板/评估模板/参数体检/向导/绘制搜索区域）**对所有模块可见**，非模板模块会写模板专属参数。
4. 拖入模块与已有模块对齐时**不会自动连接**；官方明确要求“对齐即自动连接”。

## 二、官方基线（本次引用的权威页面）

| 页面 | GUID | 本文引用的关键规则 |
|---|---|---|
| 界面介绍 | GUID-60443F51-F0DE-40E3-AF59-F0F1D53A2D5D | 欢迎页、顶部方案名/角色/资源、快捷工具条、菜单结构、图像显示区、流程栏、模块箱、流程编辑区、鹰眼/缩放/耗时、日志栏、结果显示区 |
| 其他操作与配置 | GUID-63A93478-A768-46C4-B3C1-B81F0337F8EB | 流程栏右键：导出 Prc、拷贝、删除、运行间隔、NG 时停止、重命名、流程超时 |
| 模块通用配置 | GUID-09AA4603-2BC1-44E4-B406-3212A71603D7 | 配置窗口分“基本参数/运行参数/结果显示”，左上角帮助入口 |
| 运行参数 | GUID-85034A53-48B2-4C38-908A-ACCC87E65E4A | 参数分自定义/订阅，左下角恢复默认值 |
| 结果显示 | GUID-1552B2A6-F573-4CF0-990D-3B07EC23E074 | 结果判断、图像显示、文本显示三类设置 |
| 结果显示区域 | GUID-86F09A5B-5690-49EB-B8F5-E71EAB4BDE9C | 随选中对象联动，当前结果/历史结果/帮助信息 |
| 参数查找工具 | GUID-664CEA92-A797-492E-BAF8-6CC7880C132B | 流程→模块→输入/输出/运行参数，软件参数名/算子参数名/当前值/调用代码 |
| 搭建入门级方案 | GUID-D1C1F667-E0CB-4FE6-833F-8FAEBD8DDFEC | 欢迎页、对齐自动连接、图像区本地图按钮、模块窗右下角“确定”、运行界面预览 |
| 图像显示相关功能 | GUID-CBDEA506-DAAC-488C-AABD-2C80C333B054 | 单/双画面、十字辅助线配置、保存/另存、全屏、本地图④区、叠加多选 |
| 查看模块图像 | GUID-8FE35F6E-45BD-441E-97A2-4AA642E6DDA5 | 图像下拉选择“当前模块输出 + 所有上游输出” |

## 三、整体面板对照

| 区域 | VM 4.4 官方形态 | 当前实现 | 判定 |
|---|---|---|---|
| 启动/欢迎页 | 默认弹出欢迎页：新建方案类型、最近打开、示例、帮助、不再显示 | 启动即强制登录，无欢迎页 | 缺失 |
| 顶部信息 | 当前方案名称、角色切换、内存/CPU/硬盘、窗口控制 | 仅标题栏“视觉方案工作台 - 用户名[角色]”，无方案名/资源/角色切换 | 缺失 |
| 快捷工具条 | 保存、打开、撤销（带历史）、重做、锁定、相机、全局变量、通讯、全局触发、全局脚本、单次、连续、综合配置、运行界面设计 | 2026-11-07 起：图标单行（保存/打开/新建｜撤销▾/重做▾/锁定｜相机/变量/触发/脚本｜开始/停止/单次/单步/暂停｜综合配置/运行界面/参数查找）+ 流程模式下拉；通讯入口由综合配置覆盖 | 图标化已对齐；通讯独立入口仍缺 |
| 菜单栏 | 文件 / 设置 / 工具 / 系统 / 帮助 | 文件 / 编辑 / 视图 / 通讯 / 系统 / 语言（帮助在代码里追加） | 不一致 |
| 图像显示区 | 单画面/双画面、十字辅助线配置、保存/另存、全屏、本地图④、叠加多选 | 单画面、适应/100%/缩放/十字线开关/保存/浮动 | 部分 |
| 流程栏 | 标签 + 新建；右键重命名/拷贝/删除/导出 Prc/运行间隔/NG 停止/超时 | 标签 + ＋ + 常驻 X；无右键菜单 | 部分且危险 |
| 模块箱 | 分类模块箱 | 2026-11-07 起：左侧常驻分类图标竖条 + 可展开模块列表（搜索/树收进列表面板）；此前为“工具箱”树 | 形态已对齐（图标为本地绘制） |
| 流程编辑区 | 画布 + 缩放滑块 + 鹰眼 + 流程耗时 | 纯 QGraphicsView，无缩放、无适应窗口、无鹰眼、无画布耗时 | 缺失 |
| 日志栏 | 默认只显示最近一条，单击可展开全量 | 常驻全量 QPlainTextEdit | 部分 |
| 结果显示区 | 常驻右下，随选中对象联动，当前/历史/帮助三页 | 2026-11-07 起：结果区常驻右侧图像区下方（历史结果/帮助，列 执行序号/时间/模块数据）；图像区「模块结果」承载当前轮结果；随选中联动 | 位置与页签已对齐 |

## 四、P0 差距

### P0-1 关闭流程标签即销毁流程，无确认、无撤销

- 官方口径：流程删除放在**流程栏右键菜单**（其他操作与配置 :10-11、:16-17），是明确指向对象的操作。
- 当前实现：
  - `ui/MainWindow.ui:57-60`：`flowTabs` 设置 `tabsClosable=true`，每个标签常驻 X。
  - `src/MainWindow.cpp:3127-3181`：`onFlowTabCloseRequested` 直接 `delete scene`、`delete ex`，无 `QMessageBox`。
  - `src/MainWindow.cpp:3973-3977`：`onDeleteFlow` 同样直接销毁；`src/MainWindow.cpp:1076` 绑定 Ctrl+Shift+T。
  - 删除后场景/执行器/撤销栈一起销毁，应用级无法恢复。
- 影响：误点 X 或误按快捷键即丢失整条流程；项目保存后不可逆。
- 建议：X 与 Ctrl+Shift+T 统一走“删除流程”动作，弹确认框（流程名 + 模块数 + 是否有未保存修改）；删除记为一次可撤销操作，或至少提供“最近删除恢复”。

### P0-2 模块配置页签规则只对模板匹配生效

- 官方口径：模块通用配置 :11——“弹窗分为基本参数、运行参数和结果显示 3 个页签，大部分模块的配置都在该页签中完成”。
- 当前实现：
  - `src/ModuleEditorDialog.cpp:428-456`：所有节点都会调用 `rebuildVmTabs`。
  - `src/ModuleEditorDialog.cpp:464`：函数首行即 `qobject_cast<OpencvTemplateMatchNode *>`，非模板直接 `return`。
  - `m_vmTabs` 容器见 `src/ModuleEditorDialog.cpp:698`，四个 `addTab`（基本参数 / 特征模板（仅模板类）/ 运行参数 / 结果显示）见 `:1447`–`:1451`。
    **2026-11-08 更正**：原引用 `:475-525` 实为信号连接与 `scheduleAutoRecompute` 定时器，并非页签创建处。
  - `src/ModuleEditorDialog.cpp:279`：`m_resultTable->setVisible(isTmpl)`，非模板模块窗口没有结果表。
- 影响：除模板匹配外的所有算子仍是平铺长表单，参数分组、运行参数、结果显示没有统一位置；这与“规则一致”直接冲突，也是现场培训成本最高的差异之一。
- 建议：把页签重建提升为所有节点的通用规则：按 `ParamSpec.group` 映射到基本/运行/结果显示；模板类节点额外增加“特征模板”页；结果表对所有节点可见（至少输出值/状态/耗时）。

### P0-3 模板专属工具条按钮泄漏到所有模块

- 官方口径：模块窗口只应出现该模块可用的动作；模板训练/评估属于模板匹配模块。
- 当前实现：
  - `drawBtn` 起的单行按钮（`src/ModuleEditorDialog.cpp:134`）、`:105-112`：无条件创建“绘制搜索区域、绘制多边形区域、编辑掩膜、保存模板图像、训练模板、评估模板、参数体检、向导”。
  - `src/ModuleEditorDialog.cpp:263-279`：只隐藏了 `saveTmplBtn`、模板预览、模板列表、结果表，**没有隐藏** `trainTmplBtn / evalBtn / budgetBtn / wizardBtn / searchBtn / polyBtn`。
  - `src/ModuleEditorDialog.cpp:1118-1146`：这些槽函数直接读取 `angleMin/angleMax/matchScore/matchCount` 等模板参数，非模板节点会得到空值或默认值。
  - `src/ModuleEditorDialog.cpp:886-889`：搜索区域覆盖层直接读取 `searchRegion*` 模板专属参数。
- 影响：任意算子窗口都能看到“训练模板/评估模板”等无关按钮，点击后提示错误或写入无效参数；这是典型的“面板操作习惯不一致”。
- 建议：按 `isTmpl` 统一控制搜索区域/多边形/训练/评估/体检/向导的可见性；非模板节点只保留通用 ROI 绘制；槽函数入口再加节点类型守卫。

### P0-4 拖入模块与已有模块对齐时不自动连接

- 官方口径：搭建入门级方案 :78-82——“若拖入的模块与另一个模块对齐，则两者自动连接，连接后两个模块出现一条连接线”。
- 当前实现：
  - `src/FlowScene.cpp:680-720`：`dropEvent` 只根据落点 `createNode`，没有最近模块扫描、没有对齐阈值、没有自动建边。
  - `canConnectPorts`（`src/FlowScene.cpp:355-374`）：连接合法性只在手动拖端口时校验。
- 影响：官方“拖入即搭建”的主操作习惯不成立；每条边都要手动拖端口，慢且易错。
- 建议：落点后按端口几何距离/水平对齐阈值找候选前后序，做数据类型校验，合法则自动连接并合并为一条撤销记录；同时保留手动连接。

## 五、P1 差距

| 编号 | 差距 | 官方口径 | 当前实现与证据 | 建议 |
|---|---|---|---|---|
| P1-1 | 模块配置窗缺帮助入口，缺“确定/取消” | 模块通用配置 :11（左上角帮助）；搭建入门级方案 :114（右下角确定） | `drawBtn` 起的按钮行（`src/ModuleEditorDialog.cpp:134`） 工具条无帮助按钮；`:229-258` 底部只有“完成”；`:354-359` 关闭只存几何；参数即时生效 | 左上角加“?”；底部改为“确定/取消”，打开时快照参数，确定提交、取消回滚 |
| P1-2 | 结果显示区不随选中联动，无当前/历史/帮助 | 结果显示区域 :11-26、:35-37 | `src/MainWindow.cpp:1401-1440` 选中只刷新图像/参数；`:4629-4634` 结果表手动打开；`setModuleResult`（`src/ResultTablePanel.cpp:396`） 每轮覆盖，只保留“本轮结果” | 常驻结果区，选中即切换；当前/历史分页；历史含执行序号/时间；增加帮助页 |
| P1-3 | 流程栏缺右键操作 | 其他操作与配置 :10-32 | `ui/MainWindow.ui:57-60` 只有关闭 X；`src/MainWindow.cpp:1078-1084` 只有＋；全仓无流程重命名/拷贝/导出 Prc/运行间隔/NG 停止/超时实现 | 给 `flowTabs.tabBar()` 加右键菜单，逐项补齐上述动作 |
| P1-4 | 流程画布无缩放、无适应窗口、无鹰眼 | 界面介绍 :64-72（缩放滑块、鹰眼） | `src/MainWindow.cpp:1299-1303` 直接 `new QGraphicsView`；`src/VisionWorkbenchStyle.cpp:154-164` 只设渲染/拖放，无 wheel 缩放；全仓无鹰眼/缩略图实现 | 增加缩放滑块、适应窗口、Ctrl+滚轮缩放、鹰眼导航；大流程才可操作 |
| P1-5 | 快捷工具条缺关键入口 | 界面介绍 :37-38、:164-201 | `mainToolBar`（`src/MainWindow.cpp:1279`） 仅新建/打开/保存、开始/停止、单次/单步/暂停、模式下拉 | 补齐撤销/重做（含历史）、锁定、相机、全局变量、通讯、触发、脚本、综合配置、运行界面设计 |
| P1-6 | 菜单结构缺“设置/工具”，文件菜单不完整 | 界面介绍 :35、:82-156 | `ui/MainWindow.ui:145-223` 为文件/编辑/视图/通讯/系统/语言；`m_helpMenu`（`src/MainWindow.cpp:2690`） 代码追加帮助；文件菜单无另存为/示例/导入流程 | 按 VM 重建菜单分工：设置、工具独立；文件补另存为/示例/导入流程；导出算子代码归文件菜单 |
| P1-7 | 无用户可操作的“锁定” | 界面介绍 :178-179（工具栏锁定需确认） | `include/FlowScene.h:320` 仅有 `setEditLocked`；`src/MainWindow.cpp:755-772` 只在运行/暂停时自动加解锁；无工具栏动作 | 增加锁定按钮与确认提示，锁定后禁止增删改连线和参数编辑 |
| P1-8 | 无角色切换/注销，启动强制登录 | 界面介绍 :43-45（右上角角色切换菜单） | `src/MainWindow.cpp:621-639` 构造时强制 `LoginDialog::exec()`；标题只显示用户/角色；全仓无用户角色切换/注销 | 保留登录审计，但支持运行中切换角色/注销并即时刷新权限 |
| P1-9 | “结果显示”页缺结果判断/图像显示/文本显示 | 结果显示 :12-26 | `src/ModuleEditorDialog.cpp:518-520` 结果显示页只有结果表；无判断条件、无颜色/透明度/线宽、无文本显示配置 | 结果显示页分三组：结果判断（全部条件 AND→OK）、图像显示（勾选+颜色+透明度+线宽）、文本显示（内容/颜色/字号/透明度/位置订阅） |
| P1-10 | 参数提交语义与 VM 相反：即时生效，无取消 | 运行参数 :20-24；搭建入门级方案 :114 | `m_autoRecomputeCheck`（`src/ModuleEditorDialog.cpp:165-168`） 默认勾选“自动重算”；`:338-341` 400ms 去抖；`:539-577` 改参数立即写节点并触发下游重算；`:134-146` 变量引用插入最近编辑框 | 默认“确定提交/取消回滚”，保留“自动预览”开关；重算只读预览，不落盘 |

## 六、P2 差距

| 编号 | 差距 | 官方口径 | 当前实现与证据 | 建议 |
|---|---|---|---|---|
| P2-1 | 无欢迎页 | 界面介绍 :12-23；搭建入门级方案 :66-73 | 全仓无 Welcome 相关实现；`src/MainWindow.cpp:624` 启动即登录 | 增加欢迎页：新建方案类型、最近方案、示例、帮助、不再显示 |
| P2-2 | 顶部无当前方案名，无内存/CPU/硬盘 | 界面介绍 :40-41、:47-49 | `setWindowTitle`（`src/MainWindow.cpp:1854`） 标题仅用户/角色；全仓无资源监控 | 标题或顶栏显示方案名；右上角加资源占用与窗口控制 |
| P2-3 | 日志栏常驻全量，不是“最近一条” | 界面介绍 :74-75 | `ui/MainWindow.ui:124-128` 常驻 QPlainTextEdit | 默认一行，单击/双击展开全量；保留滚动到底 |
| P2-4 | 无“选中模块耗时 / 流程耗时”画布角标 | 界面介绍 :68-69 | `src/ExecutionStatusController.cpp:76-79` 状态栏只显示全局最近耗时；性能面板在 `src/MainWindow.cpp:522-525` 手动打开 | 画布右下常驻流程耗时；选中模块显示该模块耗时 |
| P2-5 | 图像区缺单/双画面、十字线配置、全屏、叠加多选 | 图像显示相关功能 :23-26 | `src/MainWindow.cpp:247-293` 只有适应/100%/放大/缩小/十字线开关；`src/HalconWindow.cpp:740-793` 无全屏；`src/MainWindow.cpp:1569-1606` 图像源单选，无多选叠加 | 增加单/双画面、两条独立十字线配置、全屏、上游图像多选叠加 |
| P2-6 | 图像区无本地图④区域 | 图像显示相关功能 :29-30、:45-56；搭建入门级方案 :96-97 | 本地图列表在 `addLocalImageFiles`（`src/ImageReadNode.cpp:292`） 的算子参数面板内（含多文件与自动切换），不在图像显示区；图像区缺“添加文件夹、全部删除、首张、运行全部/选中”及与连续执行的联动 | 把本地图列表提升为图像区固定区域，保留节点内列表作为备选 |
| P2-7 | 参数查找工具结构简化 | 参数查找工具 :10-17 | `setupUI`（`src/ParameterSearchDialog.cpp:26-70`） 关键词搜索 + 定位，无流程→模块→输入/输出/运行参数分层，无算子参数名/调用代码 | 按 VM 三分层与四列实现；保留关键词搜索作为加速入口 |
| P2-8 | 运行参数缺逐参数订阅与恢复默认 | 运行参数 :20-24 | `drawBtn`（`src/ModuleEditorDialog.cpp:134-146`） 仅文本式变量引用；全仓无“恢复默认值”按钮 | 每个数值参数右侧放订阅图标（自定义/订阅切换）；左下角加恢复默认 |
| P2-9 | 撤销/重做无按钮、无历史下拉 | 界面介绍 :171-174 | `src/MainWindow.cpp:1092-1099` 只有 Ctrl+Z/Ctrl+Y 快捷键，工具栏和编辑菜单都没有入口 | 工具栏加撤销/重做，并按下拉显示历史操作文本 |
| P2-10 | 命名与密度：工具箱/英文表头/模块窗按钮过密 | 界面介绍 :61（模块箱） | `ui/MainWindow.ui:18-34` 标题“工具箱”、表头“Tool Library”；`drawBtn` 起的按钮行（`src/ModuleEditorDialog.cpp:134`） 单行十几个按钮，无溢出收纳 | 统一“模块箱”中文命名；表头本地化；模块窗按执行/几何/模板/查看分组或折叠 |
| P2-11 | 无“综合配置”，运行界面设计无预览 | 界面介绍 :198-201；搭建入门级方案 :270-275 | 全仓无综合配置入口；`runtimeControlTypeName` 类型预览（`src/RuntimeInterfaceDesigner.cpp:205`） 只有类型/表格预览，无运行界面预览按钮 | 增加综合配置入口；运行界面设计加“预览”与“保存”并排 |
| P2-12 | 帮助菜单能力不全 | 界面介绍 :149-156 | `m_helpMenu`（`src/MainWindow.cpp:2690`） 仅用户手册/环境检查/关于；无在线文档、版本信息、V 社区、打开欢迎页 | 按 VM 补齐帮助菜单项 |
| P2-13 | 结果表无执行序号/执行时间 | 结果显示区域 :22 | `setModuleResult`（`src/ResultTablePanel.cpp:396`） 只按模块覆盖名称/状态/耗时/输出值，无执行序号/执行时间 | 增加“执行序号、执行时间”两列；历史结果按序号保留最近 N 次 |

## 七、操作规则一致性核对

| 规则 | VM 4.4 | 当前实现 | 判定 |
|---|---|---|---|
| 双击模块打开配置 | 是（搭建入门级方案 :91、:105） | `requestNodeEdit`（`src/NodeGraphicsItem.cpp:233-235`） 双击发 `openEditorRequested` | 一致 |
| 连线合法性 | 前后序数据类型匹配 | `canConnectPorts`（`src/FlowScene.cpp:355-374`） 按 `PortDataType`/方向校验 | 一致 |
| 拖入对齐自动连接 | 是（搭建入门级方案 :78-82） | 只落点创建，无对齐检测 | 缺失 |
| 模块配置页签 | 基本/运行/结果显示，模板另有模板页 | 仅模板节点四页签，其余平铺 | 相反 |
| 参数提交 | 执行 + 确定/取消 | 即时生效 + 完成 | 相反 |
| 结果显示内容 | 判断/图像/文本三类设置 | 只有结果表 | 缺失 |
| 删除流程 | 流程栏右键删除 | 标签 X / 快捷键直接销毁 | 相反且不可逆 |
| 锁定 | 工具栏显式锁定，需确认 | 仅运行态自动锁 | 缺失 |
| 撤销/重做 | 工具栏按钮 + 历史下拉 | 仅快捷键 | 部分 |
| 运行模式 | 单次/连续/触发 | 连续/软/硬 + 单次/单步/暂停 | 功能有对应，入口不同 |
| 选中模块看图像 | 当前模块 + 上游图像下拉 | `resolveDisplayNode`（`src/MainWindow.cpp:980`） 图像源单选（当前/上游） | 一致（无叠加多选） |
| 模块 OK/NG 视觉 | 运行状态/结果色 | `executionAborted` 配色（`src/NodeGraphicsItem.cpp:83-96`） 绿/红/黄边框 | 一致 |

## 八、已对齐项（可保留）

| 能力 | 证据 |
|---|---|
| 双击模块打开独立配置窗 | `requestNodeEdit`（`src/NodeGraphicsItem.cpp:233-235`） |
| 连线数据类型校验 | `canConnectPorts`（`src/FlowScene.cpp:355-374`） |
| 模块运行/结果状态色 | `executionAborted` 配色（`src/NodeGraphicsItem.cpp:83-96`） |
| 单次/连续/暂停执行能力 | `mainToolBar`（`src/MainWindow.cpp:1279`） |
| 最近打开方案 | `m_recentFiles`（`src/MainWindow.cpp:1830`） |
| 选中模块即显示其图像 | `onNodeSelected`（`src/MainWindow.cpp:2938`） |
| 图像源含当前模块与上游 | `resolveDisplayNode`（`src/MainWindow.cpp:980`） |
| 模板匹配四页签/结果表（局部样板） | `m_vmTabs` 容器与四个 `addTab`（`src/ModuleEditorDialog.cpp:698`、`:1447-1451`）、`:189-201` |
| 模板配置窗确定/取消（与模块窗不一致） | `okBtn` / `cancelBtn`（`src/TemplateConfigDialog.cpp:410-413`） |
| 深色工作台主题 | `applyFusionDarkPalette`（`src/VisionWorkbenchStyle.cpp:12`）（非像素级比对） |

## 九、文档口径失真

**D-1** `docs/release-registry.json:978-984` 把“模块配置窗口四页签”写成已完成的全量对齐项。实现只在 `src/ModuleEditorDialog.cpp:464` 的模板分支内生效，其余节点没有四页签，该条目应改为“仅模板匹配算子”。

**D-2** `docs/VM4.4高精度匹配对齐规范.md:121` 把“分组即页签”写成通用参数规则。实现未对非模板节点执行该规则，规范应注明当前适用范围，或按 P0-2 全量落地后再生效。

**D-3** `docs/visionmaster-optimization-report.md:239` 结论称“UI 操作性更接近 VM 4.4”。在流程栏右键、自动连接、结果显示区、锁定、角色切换等核心操作仍缺失的情况下，该表述只能算“局部改善”，不能作为整体对齐依据。

## 十、复现命令

官方原文（在仓库根目录执行）：

```powershell
python build\vmhelp_dump.py "D:\Program Files\VisionMaster4.4.0\Applications\Help\CH" build\vmhelp_ui `
  "GUID-60443F51-F0DE-40E3-AF59-F0F1D53A2D5D.html" `
  "GUID-63A93478-A768-46C4-B3C1-B81F0337F8EB.html" `
  "GUID-09AA4603-2BC1-44E4-B406-3212A71603D7.html" `
  "GUID-86F09A5B-5690-49EB-B8F5-E71EAB4BDE9C.html" `
  "GUID-1552B2A6-F573-4CF0-990D-3B07EC23E074.html" `
  "GUID-D1C1F667-E0CB-4FE6-833F-8FAEBD8DDFEC.html"
Select-String -Path build\vmhelp_ui\GUID-60443F51-F0DE-40E3-AF59-F0F1D53A2D5D.txt -Pattern "快捷工具条|鹰眼|流程耗时|日志栏|结果显示区"
Select-String -Path build\vmhelp_ui\GUID-63A93478-A768-46C4-B3C1-B81F0337F8EB.txt -Pattern "导出|拷贝|删除|运行间隔|NG|超时"
Select-String -Path build\vmhelp_ui\GUID-D1C1F667-E0CB-4FE6-833F-8FAEBD8DDFEC.txt -Pattern "自动连接|确定|欢迎"
```

源码证据：

```powershell
Select-String -Path src\ModuleEditorDialog.cpp -Pattern "qobject_cast<OpencvTemplateMatchNode>|训练模板|评估模板|参数体检|向导|自动重算"
Select-String -Path ui\MainWindow.ui -Pattern "tabsClosable|Tool Library|menuFile|menuView"
Select-String -Path src\MainWindow.cpp -Pattern "onFlowTabCloseRequested|delete scene|onDeleteFlow|QGraphicsView|setWindowTitle"
Select-String -Path src\FlowScene.cpp -Pattern "dropEvent|createNode|canConnect"
```

门禁：

```powershell
powershell -ExecutionPolicy Bypass -File tools\doc_check.ps1
python tools\doc_anchors.py
python tools\repo_hygiene.py
```

## 十一、复核边界与建议顺序

- 本报告只核对“界面/操作/规则”，不覆盖模板匹配参数语义、算法精度、性能指标。
- 未启动 VM 4.4 做人工点击比对；结论以官方帮助文字与当前源码为准。
- 建议顺序：P0-1（防数据丢失）→ P0-2/P0-3（模块窗规则统一）→ P0-4（自动连接）→ P1-2/P1-3（结果区与流程栏）→ P1-4/P1-5/P1-6（画布缩放、工具条、菜单）→ 其余 P1/P2。
- P0-2 与 P0-4 是“规则一致”的核心；只补功能而不统一规则，仍不能称为“整体面板对齐”。

## 十二、2026-11-05 更新（框架纯页签 + 二值化试点）

本轮按“先框架、再逐算子”的顺序落地，以下条目已不再是缺口：

| 原编号 | 现状 |
|---|---|
| P0-2 模块配置页签只对模板生效 | 已闭合：`ModuleEditorDialog::rebuildVmTabs` 对**所有算子**建 基本参数/运行参数/结果显示 三页；模板类多一页「特征模板」。 |
| P0-3 模板专属按钮泄漏 | 已闭合：训练/评估/体检/向导/搜索区域等按钮只对模板算子可见；模块窗顶部工具条整体隐藏。 |
| P1-1 缺帮助入口 | 已闭合：顶部固定「?」按钮（不再随工具条隐藏）；底部「连续执行/执行/确定」，关窗=回滚。 |
| P1-9 结果显示页缺三组配置 | 已闭合：结果判断 + 通用结果表（非模板显示输出项/值/类型/状态）+ 图像/文本显示配置。 |
| P1-10 参数即时生效无取消 | 已闭合：打开时快照，取消回滚，确定提交一次重算。 |

**框架统一（VM 纯页签窗）**：所有模块窗不再内置图像区与工具条；ROI 绘制/掩膜涂抹统一落在主画布
（`ImageDisplayController::startGeometryPick/startMaskEdit`，`MainWindow::beginGeometryPick/beginMaskEdit`）。
9 个带 ROI/掩膜的算子在「基本参数」页保留「ROI区域」「掩膜」入口，只发主画布请求。

**二值化试点（OpencvThresholdNode ↔ IMVSBinaryModu）**：已补齐 15 项运行参数
（ROI外处理、硬阈值/均值/高斯/Sauvola/自动、高低阈值、均值核、高斯核与标准差、比较类型、偏移、Sauvola 五项），
类型切换动态隐藏无关参数；矩形 ROI + 掩膜 + ROI 外黑色/原图；默认值按 VM（均值二值化、低阈值 100）。
腿：`NodeCoreTest::testThresholdVmModesAndRoi`（五种模式 + 高低阈值反向 + ROI 外处理）；
`ModuleEditorTest::testAllModuleWindowsArePureTabs`（纯页签 + 参数动态显隐）。
快照（build/ 不入库）：`ui_puretab.png`、`ui_binary.png`、`ui_binary_run.png`。

**形态学（OpencvMorphNode ↔ IMVSImageMorphModu）**：5 项运行参数按 VM 对齐
（形态学类型 膨胀/腐蚀/开/闭、形态学形状 矩形/椭圆/十字、迭代次数 0~10、核宽度/高度 1~51），
默认腐蚀/矩形/1/3×3；ROI 外保留原图；矩形 ROI + 掩膜。
腿：`NodeCoreTest::testMorphVmParamsAndRoi`（腐蚀/膨胀面积、宽高独立、三种结构元、迭代 0、ROI 外原图）。

**滤波（BlurNode/MedianFilterNode ↔ IMVSImageFilterModu）**：
- 模糊 = VM「高斯」类型：高斯滤波核 1~101，默认 3
- 中值滤波 = VM「中值」类型：核宽度/高度 1~101，默认 3×3；方形奇数核走 OpenCV `medianBlur`，矩形核走自研直方图中值
- 两者都支持矩形 ROI + 掩膜，ROI 外保留原图
腿：`NodeCoreTest::testFilterVmParamsAndRoi`（高斯摊开单像素、方形/矩形中值去椒盐、ROI 外原图）。

**增强与归一化（SharpenNode/ContrastStretchNode/HistogramEqualizeNode ↔ IMVSImageEnhanceModu / IMVSImageNormlizeModu）**：
- 锐化 = VM「锐化」类型：锐化强度 0~1000（100=1.0，0=不锐化）、锐化核大小 1~51，默认 0/3
- 对比度 = VM「对比度」类型：对比度系数 1~10000，默认 100（100=不变）
- 直方图均衡 = VM「直方图均衡化」类型：无运行参数
- 三者都支持矩形 ROI + 掩膜，ROI 外保留原图
腿：`NodeCoreTest::testEnhanceVmParamsAndRoi`（锐化 0/边缘过冲、对比度 100/200、直方图均衡拉开范围、ROI 外原图）。

**归一化与缩放（GrayStretchNode ↔ IMVSImageNormlizeModu；ResizeNode ↔ IMVSImageResizeModu）**：
- 灰度拉伸改为 VM「均值标准差归一化」：均值 0~255 默认 128、标准差 0~255 默认 20；按 ROI/掩膜内统计量做标准化
- 图像缩放改为 VM 输出宽高（8~65000，默认 800×800）+ 插值类型（最近邻/双线性/双三次）+ 抗混叠
- 灰度拉伸支持矩形 ROI + 掩膜；缩放暂未接 ROI（VM 缩放的 ROI 语义待确认）
腿：`NodeCoreTest::testNormalizeAndResizeVmParams`（均值/标准差归一化数值、ROI 外原图、缩放宽高/最近邻采样）。

**几何变换（TranslateImageNode/OpencvRotateNode/MirrorNode ↔ IMVSAffineTransformModu）**：
- 平移：X/Y 移动距离、插值（最近邻/双线性）、填充方式（常量/邻近复制）、填充值、锁定输出尺寸
- 旋转：旋转角度 0~360、插值、填充方式/值、锁定输出尺寸
- 镜像：镜像方向（水平/垂直/水平垂直）
- 未锁定输出尺寸时画布按「原画布 ∪ 变换后包围盒」扩展；锁定时保持原尺寸
腿：`NodeCoreTest::testAffineVmParams`（平移扩展/锁定+常量填充、三种镜像、0°/90° 旋转包围盒）。

**Blob 批 1（OpencvBlobNode ↔ IMVSBlobFindModu）：阈值与连通域**
- 阈值方式 6 档：不进行二值化 / 单阈值 / 双阈值 / 自动阈值 / 软阈值（固定）/ 软阈值（相对）
- 极性：亮于背景 / 暗于背景；低阈值 100、高阈值 150（VM 默认）
- 软阈值 7 项：固定低/高阈值、柔和度、低/高尾部比例、低/高阈值比例
- 查找个数默认 100（1~20000）；孔洞填充面积、8/4 连通性保留
- 支持矩形 ROI + 掩膜，ROI 外不参与 Blob 统计
- 软阈值权重目前按 0.5 转为二值后做连通域（VM 的加权连通域精度待后续实现）
腿：`NodeCoreTest::testBlobThresholdVmModes`（单阈值亮/暗极性、双阈值低<高与低>高、软阈值固定、ROI 限定）。

**Blob 批 2（形状筛选）**：11 组筛选统一为 VM 的 enable + min/max
（面积、外接宽、外接高、周长、短轴、长轴、圆形度、矩形度、质心偏移、轴比、box 角度）。
圆形度按 VM 帮助改为 `面积 / (π × 质心到轮廓最大距离²)`；矩形度=面积/最小外接矩形面积；
轴比=短轴/长轴；质心偏移=质心到最小外接矩形中心的距离；box 角度归一化到 [-90,90)。
腿：`NodeCoreTest::testBlobShapeFilters`（方块/三角形矩形度、方块/细长矩形轴比、周长上下限）。

**Blob 批 3（排序与结果判断）**
- 13 种排序特征：面积、周长、圆形度、矩形度、质心X/Y、box 角度/宽/高、矩形左上X/Y、主轴角度、轴比
- 排序方式：升序 / 降序 / 不排序（VM：0=升序，1=降序，2=不排序）
- 结果判断 10 项：无目标判定 OK、Blob 数量检查、单个 Blob 面积检查、Blob 总面积检查
- `moduleStatus` 由判断结果驱动，失败原因写入 `judgeReason`
腿：`NodeCoreTest::testBlobSortAndJudgment`（面积降序+截断顺序、质心X升序、不排序、数量/面积/总面积 NG、无目标 OK 开关）。

**下一步队列**：卡尺（IMVSCaliperModu）→ 直线/圆拟合 → 特征匹配 → 测量 → 识别 → 采集/标定/逻辑数据。
VM 滤波的「均值/取反/边缘提取」暂未单独建节点（项目约束不扩算子目录），其中取反/边缘提取已有独立算子，后续按各自模块对齐。

## 十三、2026-11-06 更新（「结果显示」三项配置落地）

本轮把「结果显示」页里此前**只写参数、无人消费**的配置接到实际显示链路，并统一模块窗结果页口径。

| 原编号 | 现状 | 证据 |
|---|---|---|
| P1-9 结果显示页缺三组配置 | 已闭合（第三组“图像/文本显示”真正生效） | 见下 |
| P0-2 模块配置页签规则 | 已闭合（所有算子三页签 + 模板多一页） | `ModuleEditorDialog::rebuildVmTabs`、`ModuleEditorTest::testAllModuleWindowsArePureTabs` |
| P1-1 缺帮助入口/确定取消 | 已闭合 | 模块窗顶部「?」、底部「连续执行/执行/确定」，关窗回滚 |
| P1-10 参数即时生效无取消 | 已闭合 | 打开快照、取消回滚、确定提交一次重算 |

**① 图像显示（`resShowImage/resImageColor/resImageOpacity/resImageLineWidth`）**
- `ImageDisplayController::collectOverlayFromNode` 读取四项配置：未勾选「显示结果图像」⇒ 不绘制结果图元
  （ROI/检测区域等搭建期参照仍保留）；勾选后颜色作用于 OK 结果图元，透明度 0~100、线宽 1~10 逐图元生效。
- 语义色不被配置色覆盖：判 NG 红、未命中灰、角度副线青、交点黄、缺陷红。
- 老工程没有这些键时保持原行为（命中绿/未命中灰/判 NG 红），不会被默认值改写。
- 绘制侧：`HalconWindow::paintEvent` 按 `OverlayShape::lineWidth/opacity/fontSize` 设置画笔与透明度。

**② 文本显示（`resShowText/resTextContent/resTextColor/resTextFontSize/resTextOpacity/resTextPosition`）**
- 文本按 `resTextPosition` 画在显示图像四角（0 左上 / 1 右上 / 2 左下 / 3 右下，8px 内边距）。
- 内容支持占位符：`{module}` 模块名、`{status}` OK/NG、`{elapsed}` 本轮耗时(ms)、
  `{global.变量名}` 全局变量、`{结果键}` 本轮结果值（按 resultDisplayFields 精度格式化）；
  解析不到的键保持原样（`{key}`），便于区分“变量名写错”和“值为空”。
- `{elapsed}`/结果键优先取执行器 `nodeOutputsUpdated` 推送的 vars 快照（`MainWindow` 注入 provider），
  节点参数表没有 `elapsedMs`，未注入时回退读节点参数。
- 只有显式保存过 `resShowText` 或 `resTextContent` 才叠加，老工程不会凭空多出一行文字。

**③ 模块窗「结果显示」页结果表（非模板算子）**
- `ModuleEditorDialog::refreshResultTable` 优先按 `resultDisplayFields()` 渲染（结果项/值/单位/状态），
  与运行界面「当前结果」面板同一份字段、名称、单位、精度；分组字段（逐目标结果）留给运行界面分组表。
- 没有结果规范的节点（`NodeBase::resultDisplayFields()` 默认空表）才回退到“具名输出端口”列表。
- 表对象名 `moduleResultTable`，供 UI 测试定位。

**新增腿（`ModuleEditorTest`）**
- `testResultOverlayUsesDisplayConfig`：颜色/线宽/透明度、四角文本、占位符解析、`{elapsed}` 走快照、
  关闭「显示结果图像」后结果图元不再出现。
- `testModuleResultPageUsesResultFields`：结果表按规范渲染（模块状态 OK / 平均灰度 12.50 / 耗时 -）。

**其它条目复核（2026-11-07，按当前源码逐项核对）**

| 原编号 | 现状 | 证据 |
|---|---|---|
| P1-2 结果区三页/随选中联动 | 已闭合 | `ResultTablePanel` 三页签（当前/历史/帮助）、`setCurrentModule`、`MainWindow::onNodeSelected` 联动 |
| P1-3 流程栏右键 | 已闭合 | `MainWindow::showFlowTabContextMenu`（重命名/拷贝/删除/导出 Prc/运行间隔/NG 停止/超时） |
| P1-4 画布缩放/鹰眼/流程耗时 | 已闭合 | `FlowCanvasView`（缩放工具条、鹰眼缩略图）、`updateCanvasSelectionElapsed` |
| P1-5 快捷工具条 | 已闭合 | 撤销/重做/锁定/相机/变量/触发/运行界面等入口 |
| P1-7 显式锁定 | 已闭合 | `m_lockCanvasAction` + 确认框 + `canvasEditShouldBeLocked` |
| P1-8 角色切换/注销 | 已闭合 | 顶部用户菜单：切换角色 / 注销（`logoutUser`） |
| P2-1 欢迎页 | 已闭合 | `WelcomeDialog`（新建/最近/示例/帮助/不再显示） |
| P2-2 方案名/资源监控 | 已闭合 | 顶栏方案名 + `resourceUsageLabel` |
| P2-3 日志展开 | 已闭合 | `m_logExpanded` + 「展开/收起」 |
| P2-5 单双画面/十字线/全屏/叠加 | 已闭合 | `HalconWindow` 双画面分流、`CrosshairConfig`、全屏查看、多选叠加 |
| P2-6 本地图④区 | 已闭合 | 图像区本地图列表与连续执行联动 |
| P2-7 参数查找分层 | 已闭合 | `ParameterSearchDialog::buildTree`：流程→模块→输入/输出/运行参数 + 4 列 |
| P2-8 订阅/恢复默认 | 已闭合 | 运行参数页订阅入口 + 「恢复默认值」 |
| P2-9 撤销/重做按钮与历史 | 已闭合 | 工具条按钮 + 历史下拉 |
| P2-10 模块箱命名/表头 | 已闭合 | `ui/MainWindow.ui`：`工具箱`、表头 `模块` |
| P2-11 综合配置/运行界面预览 | 已闭合 | 综合配置入口 + 运行界面设计预览 |
| P2-12 帮助菜单 | 已闭合 | 在线文档 / 版本信息 / 打开欢迎页 / 关于 |
| P2-13 执行序号/时间 | 已闭合 | 结果表 `kColSeq/kColTime` + 历史页轮次/时间 |

**P1-6 菜单结构（2026-11-07 已闭合）**
- 顶层菜单收敛为官方五栏：**文件 / 设置 / 工具 / 系统 / 帮助**；编辑、视图、通讯不再作为顶层
  （`MainWindow::reorganizeMenuBar`）。
- 文件按官方顺序：新建方案 / 打开方案 / 最近打开方案 / 打开示例方案 / 保存方案 / 另存为… /
  导出加密（只读）方案 / 导出算子代码 / 导入流程… / 退出。旧实现把「另存为/导入/导出加密」追加到
  「退出」之后且「导出加密」有两个入口，本轮一并修正。
- 设置：权限（用户管理）/ 面板显示 / 运行策略 / 采集策略 / 综合配置。
- 工具：参数查找工具 / 编辑 / 算子搜索 / 性能分析 / 运行界面设计 / 结果表 / 统计报表 /
  全局触发 / 全局变量 / 配方 / 报警 / 检测结果 / 报表 / 打包模型与依赖。
- 系统：日志（操作日志）/ 通信管理（原「通讯」整栏收为子菜单）/ 相机管理。
- 帮助：语言 / 使用手册 / 在线文档 / 版本信息 / V 社区 / 打开欢迎页 / 关于。
- 搬移动作统一从旧菜单 `removeAction`（旧实现只 add 不 remove，会让同一动作在「系统」和「工具」
  重复出现）；搬移后清理孤立分隔符。
- 剩余差异（2026-11-07 二次更新）：官方「工具」栏四项现在全部有对应功能。
  **标定配置引导** 第一批已落地（三类类型选择 + 官方步骤引导页；静态标定固化算子链，
  动态/映射两类如实标注缺 N 图像标定 / 映射标定算子，不造空流程）。
  原记「暂无对应功能」的 标定板生成 / 异常信息收集 / 软件设置 / 方案设置 已由 `6980a05`
  补齐（`CalibrationBoard`、`DiagnosticsCollector` 与 MainWindow 的菜单/对话框）。
  完整引导链仍待补齐上述两类算子。

**菜单核对入口（2026-11-07）**
- `VisionFlowPlatform.exe --preview-ui`：跳过模态登录，建立只读预览会话（Viewer 角色 + 写闸），
  保存/打开方案/导入导出/编辑/运行均已禁用；用于人工核对菜单与整体布局。正常启动路径不变。
- 设置环境变量 `VFP_MENU_SNAPSHOT_DIR=<目录>` 后，预览模式在首帧渲染完成后自动生成
  `menu-structure.txt`（完整菜单树，含子菜单与禁用态）和各顶层菜单 PNG，随后自动退出，
  可在无人值守环境核对菜单结构。

**结果区选中规则（2026-11-07 已闭合）**
- 官方「结果显示区域」：选中「采集」或「逻辑工具」模块时只展示历史结果——采集模块的输出是
  图像（已在预览区实时展示），逻辑工具只产出流程走向，均无需实时关注的当前值。
- 实现：`ResultTablePanel::setHistoryOnlySelection(true)` 隐藏「当前结果」页并切到「历史结果」；
  其它模块恢复「当前 + 历史」双页；帮助页始终按选中模块联动。
  `MainWindow::onNodeSelected` 按 `NodeBase::IMAGE_ACQUISITION / LOGIC` 分派。
  腿：`ResultTableTest::testHistoryOnlyTabs`。
- 2026-11-07 右侧分区调整：当前轮结果移到图像区「模块结果」页（常驻），底部结果区只保留
  「历史结果 / 帮助」；采集/逻辑工具的「只展示历史结果」只作用于底部历史区，
  不再隐藏图像区当前结果页。两处面板的页签裁剪由 `ResultTablePanel::setPageVisibility`
  与 `setEmbeddedMode` 控制。腿：`ResultTableTest::testPageVisibilitySplit`。
- 当前结果页新增「仅显示选中模块」开关（默认开启 = VM 口径）：选中模块时只显示该模块的当前结果，
  历史页同步过滤；关闭后回到全部模块总览。导出 CSV / 报告始终包含全部模块（不受视图过滤影响）。
  腿：`ResultTableTest::testFollowSelectionFilter`。

**仍为缺口的条目（截至 2026-11-07）**
- **帮助内容深度**：帮助页已是随选中模块切换的简要说明，但内容深度仍少于官方帮助。
- **未做像素级比对**：本机 VM 4.4 图形界面未启动，以上以官方帮助文字 + 源码/测试为准。

## 十四、2026-11-11 直线／圆拟合首次逐条对照与可靠性首批

本节扩展前文的逐算子队列，不代表匹配模块之外的全量对照已经完成。官方依据为本机安装目录 `D:/Program Files/VisionMaster4.4.0/Applications/Help/CH/` 下两份完整帮助：直线 `GUID-A1C670FC-95ED-4BA7-A305-A5A85CABA804.html`、圆 `GUID-D2906374-C408-4ED7-A2C2-4848CF3B74D6.html`。二者均归「图形生成」，不是「直线查找／圆查找」模块。

| 对照项 | 官方帮助契约 | 本仓现状与判定 |
|---|---|---|
| 图像输入 | 输入源可不设置 | 两节点继承必需图像契约；尚未对齐 |
| 拟合点输入 | 从前序点集按点或按坐标订阅 | 当前从阈值图的外轮廓取点，无拟合点订阅；尚未对齐 |
| 最小点数／图形数量 | 直线至少 2 点、圆至少 3 点，每次拟合一个图形 | 当前 `minPoints` 直线默认 20、下限 3，圆默认 10、下限 5；数量均为一个，但输入与最少点数契约不同 |
| 剔除点数 | 不参与拟合的最小离群点数 | 无对应参数／消费逻辑 |
| 剔除距离 | 按距离排除离群点 | 无对应参数／消费逻辑；圆帮助此项原文写「拟合直线」，不能自行当作已证圆算法细节 |
| 初始化类型 | 全局法／局部最优 | 无对应参数；直线取前若干轮廓全部点，圆取首个点数合格轮廓 |
| 权重函数 | 最小二乘／huber／tukey | 直线固定 OpenCV `DIST_L2`；圆固定 `minEnclosingCircle`，没有三档权重选择；不能认定与官方迭代拟合等价 |
| 最大迭代次数 | 控制拟合算法最大执行次数 | 无对应参数／消费逻辑 |
| 直线角度约束 | 开关＋期望角度＋容忍范围 | 三项均未接入；通用结果判断不能替代其拟合约束语义 |
| 耗时／模块状态 | 耗时及 OK／NG | 框架已有；只证明字段存在，不证明全部官方判定条件一致 |
| 输出直线／角度 | 起点、终点及各自 X/Y，直线角度 | Measure 有两个端点和角度，但端点是中心沿方向固定外推 200px；结果表只列拟合点和角度，官方端点取法仍待证 |
| 输出圆环 | 圆心 X/Y、内径、半径、起始角度、角度范围 | 只有圆心和半径；内径及两项角度字段未实现 |
| 输出拟合点 | 每个用于拟合的点及其 X/Y | 当前只输出点数，没有逐点结果 |
| 拟合误差 | 描述拟合准确性 | 无对应结果；帮助没有给出误差计算公式 |
| 拟合状态 | 是否存在拟合图形的独立状态 | 只有 `moduleStatus`／Measure.valid，没有独立结果字段 |
| 拟合点数 | 用于拟合的输入点数 | 有 `fitPointCount`，但统计的是图像轮廓点，不是订阅的输入点集 |
| 拟合距离 | 点到拟合图形的最近距离 | 无对应逐点结果 |
| 匹配点／匹配点数 | 每个输入点是否参与拟合及参与数量 | 无逐点掩码和独立匹配点数 |

实现核对入口：`OpencvFitLineNode::init/run/resultDisplayFields`、`OpencvFitCircleNode::init/run/resultDisplayFields`；输入与聚合契约见 `HalconNode::process`、`DataObject::measureResults`。帮助的圆示例同时提到「最小外接圆」，而原理／参数段描述迭代权重拟合，因此本批不靠这一句替换或认证圆算法；数值等价仍需官方配置与实机对照。

**本批已修的是现有图像模式的两个可靠性缺陷，不是上述拟合契约全量对齐：**
- ROI 宽高均为正但与图像无交集时，两节点明确失败并报告「ROI 与输入图像无交集」，不再悄悄用全图拟合；仅边界接触同样失败。部分相交仍裁剪，结果仍为整图坐标；任一尺寸为 0 的全图语义和清除 ROI 行为不变。
- 两节点在 `process()` 进入基类缺图检查之前清零拟合读数，覆盖 `execute()` 的缺图早退、无轮廓、点数不足及异常失败；普通失败由基类清空输出，成功后不会沿用上一轮坐标／角度／半径／点数。
- 节点回归：`NodeCoreTest::testFitRoiBoundary` 共 22 个场景，`testFitFailureClearsResults` 共 8 个场景。改前日志 `build/fit_roi_before_1.txt`：14 项失败（8 项越界误成功、6 项旧读数残留），其余 16 个场景通过；改后日志 `build/fit_roi_after_1.txt`：新 30 场景及两个既有用例均通过，连初始化／清理共 34 passed。正向 ROI 夹具额外放置区域外干扰轮廓，且同时核对参数和 Measure 的全图坐标。

**下一批优先顺序**：点集订阅与可选图像契约 → 拟合算法及鲁棒参数 → 官方结果全表；订阅无效时不得回退图像。当前尚无 VM GUI 点验、实机数值比对或发布包验收，其他模块也未在本批逐条审完。

## 十五、2026-11-11 台账回写：本机帮助深度 + 拟合稳健参数（匹配模块之外）

> 本节只记录**可复核的事实**。凡未做数值对照的，一律写"未认证"，不按已对齐理解。

### 15.1 本机帮助（帮助信息页）

| 项 | 改前 | 改后 |
|---|---|---|
| 参数清单 | 只列前 12 项，且只有"标签 = 当前值" | 全部参数进四列表格：**参数 / 当前值 / 取值范围（含默认） / 说明** |
| 枚举参数 | 显示裸索引（0/1/2） | 显示枚举文本（越界索引显式标注"越界索引 N"） |
| 单位 | 不显示 | 当前值后追加参数单位 |
| 结果字段 | 无 | 与「模块结果」同源的 `resultDisplayFields()` 清单（含单位、逐项分组、OK/NG 标记） |
| 说明来源 | `ParamSpec.tooltip` 基本为空 | 新增 `HalconNode::setParamTooltip`，参数面板 tooltip 与帮助页共用同一份说明 |
| 算子帮助窗 | 无论点哪个模块都显示同一份模板文案（"参数在模块配置窗中…"） | `HelpViewer::showHelpForNode` 优先按**实例**渲染：真实参数值/范围/默认/说明、结果字段、端口；取不到实例信息才回退旧模板 |
| 结尾 | 无 | 提示"参数改后立即生效；F1 打开完整手册" |

实现入口：`MainWindow::buildNodeHelpHtml`；`HalconNode::setParamTooltip`（`include/HalconNode.h`、`src/HalconNode.cpp`）；算子帮助窗的实例级版本在 `HelpViewer::buildInstanceHelpHtml`。

**仍缺**：帮助页仍是"界面内简要说明"，不是官方 1006 页手册的逐参数全文；每个节点的算法原理、示例图与失败排查树未写入。此项不宣称已对齐。

### 15.2 直线／圆拟合：稳健参数首批（§十四"下一批"的第 2 项）

新增纯函数面 `FitRobust`（`include/FitRobust.h`、`src/FitRobust.cpp`），直线拟合节点改为调用它；圆拟合补剔除参数。

| VM 参数 | 本仓实现 | 判定 |
|---|---|---|
| 权重函数（最小二乘／huber／tukey） | `weightFunc` 三档：最小二乘为一次总最小二乘；Huber/Tukey 以 MAD（1.4826×中位残差）定尺度做 IRLS，Tukey 可把强离群权重压到 0 | 参数面已落地；**算法数值等价未认证**（官方未给迭代细节） |
| 最大迭代次数 | `maxIterations`（1..10000，默认 100，权重收敛提前结束） | 已落地 |
| 剔除点数 | `rejectPoints`：按稳健初值（中位数分半）残差从大到小最多剔除 N 个 | 已落地（直线／圆） |
| 剔除距离 | `rejectDistance`（px）：残差超阈值才算离群；两参数都 >0 才生效 | 已落地（直线／圆） |
| 直线角度约束 | `angleConstrained` + `expectedAngle` + `angleTolerance`：按 0..90° 最小夹角判定，超差判 NG 并在 `lastError` 写明实际值／期望值／偏差 | 已落地 |
| 拟合点数 | `fitPointCount` 改为**最终参与拟合**的点数（含剔除后）；`fitRejectedCount` 作为排查读数（不进模块结果表，避免改变官方 11 项结果字段口径） | 已改口径（与官方"用于拟合的输入点数"语义一致，但仍非订阅点集） |

回归与取证：
- 新增 `FitRobustTest`（10 例）：角度归一/夹角、最小二乘与直线一致性、Huber/Tukey 抗离群、剔除上限与关闭语义、角度约束通过/超差/180° 等价、点数不足/重合点/NaN 拒绝。
- 全量 `ctest` 81/81 通过；`ParamPanelBindingTest` 槽位分母按新增 9 项参数从 538 重定为 547（注释已记明细）。
- 既有 `NodeCoreTest::testFitRoiBoundary`（22 场景）与 `testFitFailureClearsResults`（8 场景）默认参数路径不变（默认最小二乘、不剔除、不约束）。
- 圆拟合默认 `rejectPoints=0 / rejectDistance=0`，行为与旧版 `minEnclosingCircle` 一致；开启剔除时才先剔除再重拟合。
- 文档门禁：`doc_check.ps1`、`doc_anchors.py`、`repo_hygiene.py` 全绿；原因键站点表按实测更新为 236 处（lastError 157）。

### 15.3 仍未对齐（明确挂账，不并入"已对齐"）

1. **拟合点订阅输入**：**2026-11-11 第二批交付"按点集订阅"、第四批交付"按坐标订阅"**（见 15.5、15.7）；订阅语义、冲突与数量校验均有测试，但未做 VM 数值等价认证。
2. **初始化类型**（全局法／局部最优）：**2026-11-11 第二批已交付参数与选点分支**（见 15.5）；两个取值是本仓按"候选点如何组织"的解释，官方内部算法细节未公开，不能声称等价。
3. **官方结果全表**：**2026-11-11 第三批已补**逐点拟合距离、匹配点掩码、匹配点数、拟合点数、独立拟合状态、圆的起始角度/角度范围（见 15.6）；仍缺**内径**（本仓只拟合单圆，无环宽输入），官方"拟合误差"的公式未公开（本仓用 RMS 承载，不能声称等价）。
4. **数值等价**：无 VM 配置快照与同图对照组，不能声称与官方拟合数值一致。
5. **其他模块**：测量模块逐条对照中——**线线/线圆/圆圆测量已落地**（见 15.8–15.10），VM 六个几何测量模块已齐（点点/点线/点圆/线线/线圆/圆圆）；测量其余节点、识别、通信尚未逐条重审。

### 15.4 台账口径提醒

- 本文 §三～§十三 是 2026-11-03 基线；§十四、§十五（含 15.1–15.10）是后续增量，**判断当前状态以最后两节 + §0 类速览为准**。
- 界面像素级差异另见 `docs/VM4.4像素级比对-20261107.md`（含第二批对齐 1–19 条）；工具菜单、运行界面设计的落地记录分别在同文档与 `docs/VM4.4官方UI对照-20261109.md`。

### 15.5 2026-11-11 第二批：拟合点集订阅 + 初始化类型

对应 §十四"下一批优先顺序"的第 1 项与 §15.3 的第 1、2 项。**只交付本仓口径**，不宣称与官方算法等价。

| 项 | 实现 | 判定 |
|---|---|---|
| 点集订阅入口 | 直线/圆拟合各加 1 个逻辑槽 `点集`（下标 1，类型 `Any`，不占画布输入点）；在「基本参数 → 输入订阅」里选择前序模块输出 | 已落地 |
| 订阅载体 | XLD 每条轮廓一组（多边形按顶点）；Region 每条外边界一组；Point 单点一组；卡尺点集 `Measure.type=="caliper"` 的 `extraValues=[col,row,...]` | 已落地 |
| B1 聚合输出 | 主类型不是点集时，下探一层具名子结果，取其中的 XLD/Region/Point/卡尺点集（如「亚像素边缘点」的图像+轮廓聚合对象） | 已落地 |
| 订阅优先级 | 绑定后 `requiresInputImage()` 返回 false；订阅数据为空/类型不符/点数不足时直接判 NG 并写 `lastError`，**不回退**阈值图 | 已落地（关键契约） |
| 订阅模式 ROI | 不使用输入图像与 ROI，点集按整图坐标直接拟合 | 已落地（口径与官方"输入点集"不同，明确记录） |
| 候选截取 | 订阅点集与图像轮廓同一口径：按点数从多到少取前 `maxContours` 组 | 已落地 |
| 初始化类型 | `initType`：全局法 = 全部候选组合并拟合一次；局部最优 = 逐组拟合取残差最小（低于 `minPoints` 的组跳过） | 已落地（本仓解释） |
| 默认值 | 直线 `全局法`（与旧行为一致）；圆 `局部最优`（旧行为即单轮廓，多轮廓时改为按径向残差选） | 已落地 |
| 结果字段 | 新增 `fitResidual`（拟合残差 RMS，px）：直线为垂距 RMS、圆为径向 RMS；`fitPointCount` 在订阅模式下统计订阅点 | 已落地（不等于官方"拟合误差"） |
| 帮助 | 算子帮助窗补「订阅输入」清单，参数表自动带上 `initType` 与说明 | 已落地 |

实现位置：`include/FitPointSource.h`、`src/FitPointSource.cpp`（取点/分组/合并），`include/OpencvFitLineNode.h`、`src/OpencvFitLineNode.cpp`、`include/OpencvFitCircleNode.h`、`src/OpencvFitCircleNode.cpp`（节点接线与选点），`src/HelpViewer.cpp`（帮助窗订阅输入）。

回归取证：
- 新增 `tests/fit_point_sourcing_test.cpp`（`FitPointSourcingTest`，12 例，第三批增至 14 例）：槽位声明与默认值、无图订阅 XLD 拟合、订阅优先于图像、**订阅无效不回退图像**、卡尺点集接受/其它 Measure 拒绝、Region→圆、直线全局法 vs 局部最优、圆全局法 vs 局部最优、局部最优跳过小分组、`maxContours` 截取、聚合端口 0 的 XLD 图标数据保留。
- `NodeCoreTest::testFitRoiBoundary`（22 场景）与 `testFitFailureClearsResults`（8 场景）默认参数路径不变；`testRemainingResultDisplaySpecs` 的结果字段数按新增 `fitResidual` 由 11 改为 12（第三批再改为直线 14 / 圆 16，见 15.6）。
- `ParamPanelBindingTest` 槽位分母按新增 2 项 `initType` 从 547 重定为 549。
- 取证：全量 `ctest` 82/82 通过；`doc_check.ps1`、`doc_anchors.py`、`dup_cn_literal_gate.py`、`ledger_size_gate.py`、`repo_hygiene.py`、`register_shape_gate.py`、`layout_field_pairing_gate.py`、`open_items_gate.py` 全绿。`eol_shape_gate.py` 的冻结值按 HEAD 已有的 LF 增量（ChannelSplit/ChannelMerge/CommResponseManager/fins_test 等 7 个新文件，同期若干文件回正 CRLF，净 +2）重定 205 → 207，原因写在该脚本注释里；本轮改写的 4 个既有文件已回正 CRLF。未做 VM 数值对照。

> 就地更正（2026-11-12）：本轮把此前 15 个未跟踪新文件 `git add` 进版本控制后，
> `git ls-files --eol` 首次计入它们（全部以 LF 落盘），`tool_written` 由 207 增至
> **222**、`tracked` 由 738 增至 753，差额一一对应。冻结值按同一口径重定为 222，
> 原因已写进该脚本注释；**判据口径未变**，只重冻结读数。

**仍不覆盖**：圆仍是 `minEnclosingCircle`，没有权重函数与迭代次数。逐点结果与圆的起始角/角度范围已在 §15.6 补齐；官方"按坐标订阅"已在 §15.7 补齐。

> **就地更正（2026-11-12）**：圆**仍保持** `minEnclosingCircle` 口径（不换最小二乘，
> 默认数值与旧版一致），但 `fitCircleGroup` 里三处与口径无关的真缺陷已修：
> ① 剔除基准由 `|r_p - radius|` 改为「移除该点后外接圆半径缩小多少」的留一法——
> 外接圆语义下所有点都在圆内/圆上，旧式残差对圆内合法采样点系统性偏大，
> 剔除会优先删掉圆内点而不是真正撑大半径的远点；
> ② 剔除下限由写死的 `usable.size()-3` 改为 `minPoints`，旧代码能剔到剩 3 点
> （任意 3 点定一个圆、残差恒 0）报出「完美拟合」的假成功；
> ③ `fitResidual` 改为统计**剔除前**的全部有效输入点，旧代码只算保留点，
> 剔得越多指标越好看。
> 两条几何硬期望腿（圆内点不被误剔 + 残差诚实、剔除不绕过 `minPoints`）与改坏自证已落地。
> **本节的「仍不覆盖」结论本身不变**：权重函数、迭代次数、圆弧圆心偏移、VM 数值等价认证
> 仍未做；留一法在「多个同类远点互相掩护」时也不会剔，属已知边界。

### 15.6 2026-11-11 第三批：官方结果全表（逐点/状态/圆角度）

对应 §15.3 第 3 项。官方依据与 §十四相同（本机 `GUID-A1C670FC…`、`GUID-D2906374…` 两份帮助的「模块结果」段）。**只落字段与可复核口径，不宣称数值等价**。

| 官方字段 | 本仓键 | 口径与边界 |
|---|---|---|
| 拟合状态（int 1/0） | `fitStatus` | 拟合成功置 1，失败/缺输入保持 0；与 `moduleStatus` 并存，不替代它 |
| 拟合点数（用于拟合的输入点数） | `fitInputCount` | 本轮**候选输入点总数**（订阅或图像轮廓，经 `maxContours` 截取后） |
| 匹配点数 | `fitMatchedCount` | 参与最终拟合的点数（= 旧 `fitPointCount`，旧键保留但不再进结果表） |
| 输出拟合点 X/Y | `fitInputPoints` | 参与拟合点的整图坐标，`"col,row;…"`（`PointItem` 渲染） |
| 拟合距离 | `fitPointDistances` | **每个输入点**到拟合直线的垂距 / 到圆的径向距离（px），与输入点顺序一一对应 |
| 匹配点 | `fitPointMask` | 每个输入点是否参与拟合，`"1,0,…"`，长度 = `fitInputCount` |
| 拟合误差 | `fitResidual` | 本仓口径 = 匹配点残差 RMS；官方未公开公式，**不能当作同一指标** |
| 圆起始角度 | `fitStartAngle` | 参与拟合点相对拟合圆心的角度覆盖起点，归一化 [0,360)；"最大空隙之后"为起点 |
| 圆角度范围 | `fitAngleRange` | 360 − 最大空隙；整圆接近 360，圆弧明显更小 |
| 内径 | — | **未实现**：本仓只拟合单圆，没有环宽输入；输出 0 会误导，故不造字段 |

口径提醒：
- 逐点数组完整写入参数表；**结果表**在输入点超过 200 时只显示一行 `fitPointHint` 摘要（避免上万行 `QTreeWidgetItem` 拖死界面），参数仍可完整读取。
- 圆用 `minEnclosingCircle`：圆弧输入时圆心相对真圆会偏移，起始角/角度范围只是近似值（不钉精确角）。
- 逐点数组随输入规模增长并随方案存盘（与卡尺逐结果字段同一形态）；大点集场景请留意方案体积。

实现位置：`include/FitRobust.h`、`src/FitRobust.cpp`（`usedMask`）、`src/OpencvFitLineNode.cpp`、`src/OpencvFitCircleNode.cpp`（逐点/角度字段与结果表口径）、`include/FitPointSource.h`、`src/FitPointSource.cpp`（`joinNumbers`/`joinMask`/`joinPoints`/表格上限）。

回归：`FitRobustTest` 增补掩码断言（剔除 3 点 → 3 个 0，内点为 1）；`FitPointSourcingTest` 增至 14 例（逐点字段长度/离群点掩码与距离、圆弧角度范围、Region 整圆范围）；`NodeCoreTest::testRemainingResultDisplaySpecs` 的字段数按新口径改为直线 14 / 圆 16。

### 15.7 2026-11-11 第四批：按坐标订阅（输入方式）

官方依据：两份帮助的「拟合点」段——"选择流程中采集到的点集作为拟合来源。可通过 **输入方式** 参数选择**按点**或**按坐标**方式订阅点的数据。"

| 项 | 实现 | 判定 |
|---|---|---|
| `inputMode` | 枚举 `按点`（默认）/ `按坐标`；直线、圆拟合各一份 | 已落地 |
| 按点 | 使用「点集」逻辑槽（原第二批行为） | 已落地 |
| 按坐标 | 使用「点集X」「点集Y」两个逻辑槽；两个数值序列按序配对成点 | 已落地 |
| 数值载体 | `Number`（单点）、`Array`（`QVector<double>` / `QList<double>` / `QStringList`）、`String`（逗号/分号/空白分隔） | 已落地 |
| 数量校验 | X/Y 数量不一致直接判 NG 并写明两边个数，**不截断**成较短的一边 | 已落地 |
| 冲突处理 | 按点模式下 X/Y 仍有绑定、按坐标模式下「点集」仍有绑定、按坐标缺 X 或 Y ⇒ 明确 `lastError`，不猜意图 | 已落地 |
| 图像关系 | 任一订阅槽绑定即 `requiresInputImage()=false`；订阅失败不回退图像 | 已落地 |
| `maxContours` | 按坐标只产生一个点组，截取不生效（按点/图像模式语义不变） | 已落地 |

实现位置：`include/FitPointSource.h`、`src/FitPointSource.cpp`（`SubscriptionInputs`、`collectSubscribedGroups`、`collectNumberSeries`），两个拟合节点只做槽位与参数接线。

测试：`FitPointSourcingTest` 增至 18 例——新增按坐标 Array 拟合、文本序列解析（逗号/分号）、数量不一致与只绑一半的失败原因、两种模式冲突的失败原因；`NodeCoreTest` 与 `ParamPanelBindingTest`（槽位分母 549 → 551）同步。

**未认证**：官方"按坐标"是否支持多组配对、掩码或空值跳过，帮助未给出，本轮不做推断；数值等价仍未认证。

### 15.8 2026-11-11 第五批：线线测量节点（测量模块首个缺口）

官方依据：本机 `GUID-B65A34E9-AFE3-404F-8CAF-189DD94871B5.html`（「几何测量 → 线线测量」）。本仓此前没有线线/线圆/圆圆测量节点，只能靠脚本或外部计算拼交点。

| 官方能力 | 本仓实现 | 判定 |
|---|---|---|
| 来源选择 = 订阅 | `sourceMode=订阅`：两个逻辑槽 `直线1` / `直线2`，各取一条 `Measure(type="line")`（拟合直线、线段查找等输出） | 已落地 |
| 来源选择 = 绘制 | `sourceMode=绘制`：8 个坐标参数直接给两条直线端点（默认 90° 相交，开箱可跑） | 已落地 |
| 测量直线 1/2 起点、终点 | 结果字段 `line1StartX/Y`、`line1EndX/Y`、`line2StartX/Y`、`line2EndX/Y` | 已落地 |
| 测量直线 1/2 角度 | `line1Angle` / `line2Angle`（图像坐标系 atan2，-180..180） | 已落地 |
| 交点 | `intersectX` / `intersectY`；平行或重合判 NG 并写原因，不造交点 | 已落地 |
| 夹角（锐角） | `acuteAngle`：`atan2(|cross|, |dot|)`，0..90°（无向直线，钝向不会算成 135°） | 已落地 |
| 输出角度范围 | `angleRange` = -90°~90° / -180°~180°：只影响 `signedAngle`（直线1→直线2，顺时针为正，图像 y 向下） | 已落地 |
| 绝对距离 | `absDistance`：4 个端点到对应另一条直线垂距的平均值（官方口径） | 已落地 |
| 输入源图像 | 绘制模式要求图像（与 VM 基本参数一致，缺图给明确原因）；订阅模式不要求，接了图则原样透传 | 已落地 |
| 按点订阅（两个点配一条线） | `lineInputMode=按点`：4 个点槽（直线1起点/终点、直线2起点/终点）各取一个 Point；按线/按点互斥校验，缺一个明确报错 | 已落地（2026-11-11 补） |
| 特定坐标系 / 标定矩阵换算 | — | **未实现**（输出仍为图像像素坐标；可用坐标系变换节点后接） |
| 卡尺绘制、卡尺检测区、角度标识点 1~5、屏蔽区多边形 | — | **未实现**（本节点没有卡尺链路，不造这些字段） |

实现位置：`include/LineLineMeasureNode.h`、`src/LineLineMeasureNode.cpp`；注册见 `src/NodeRegistry.cpp`（「几何测量」组，别名 线线夹角 / 两线夹角）。

回归：新增 `tests/line_line_measure_test.cpp`（`LineLineMeasureTest`，2026-11-11 增至 9 例）——默认绘制模式 90° 交点与绝对距离、带符号角度两档折回（-135° → 45° / 保持 -135°）、平行与端点重合判 NG、订阅两条 Measure 直线、只绑一条判 NG、非 line Measure 判 NG、结果字段 17 项、按点订阅 4 个 Point 配线、按点缺槽与按线/按点冲突判 NG。全节点冒烟（`NodeExecuteSmokeTest`）自动覆盖新节点默认参数执行。

**未认证**：官方"绝对距离"的端点是否按直线1→直线2方向配对我方按 4 点平均（与帮助文字同向），未做 VM 实机数值对照。

### 15.9 2026-11-11 第六批：线圆测量节点

官方依据：本机 `GUID-D1E73475-679F-4019-B38E-35F59B422EA7.html`（「几何测量 → 线圆测量」）。

| 官方能力 | 本仓实现 | 判定 |
|---|---|---|
| 来源选择 = 订阅 / 绘制 | 订阅：`线输入 = 按线` 时 `直线` 槽取 `Measure(line)`、`圆` 槽取 `Measure(circle)`（圆心 = point1，半径 = value）；`线输入 = 按点` 时「直线起点」「直线终点」各取一个 Point 组线，圆仍走圆槽；绘制：直线端点 + 圆心/半径参数 | 已落地 |
| 测量直线 起点/终点/角度 | `lineStartX/Y`、`lineEndX/Y`、`lineAngle` | 已落地 |
| 测量圆环 圆心/半径 | `circleCenterX/Y`、`circleRadius` | 已落地 |
| 圆内径 / 起始角度 / 角度范围 | `circleInnerRadius`=0、`circleStartAngle`=0、`circleAngleRange`=360（单圆无环宽输入，取官方默认值） | 已落地（默认值口径） |
| 交点 1/2 | `intersect1X/Y`、`intersect2X/Y`；相离 0 个、相切 1 个（= 垂足）、相交 2 个；另存 `intersectCount` 排查读数 | 已落地 |
| 垂足点 | `footX/Y`：圆心到**无限长直线**的投影（不裁到端点之间） | 已落地 |
| 距离 | `distance`：圆心到直线垂距 | 已落地 |
| 角度（最小外接矩形较长边） | — | **未实现**：帮助描述与线圆几何不对应，不臆造 |
| 线输入 = 按点 | `lineInputMode`：按线读 `直线` 槽；按点读「直线起点」「直线终点」两个 Point（非 Point 类型/非有限坐标判 NG）；两种模式混绑直接报冲突 | 已落地（2026-11-11 补） |
| 卡尺检测区 1/2、特定坐标系 | — | **未实现**（无卡尺链路/无标定矩阵换算） |

实现位置：`include/LineCircleMeasureNode.h`、`src/LineCircleMeasureNode.cpp`；注册见 `src/NodeRegistry.cpp`（「几何测量」组，别名 线圆交点 / 直线与圆）。

回归：新增 `tests/line_circle_measure_test.cpp`（`LineCircleMeasureTest`，10 例）——相离 0 交点、相切 1 交点、相交代交点数值、订阅 line/circle、只绑一条判 NG、按点两点组线出同样交点数值、按点缺槽/按线绑点槽/按点绑线槽判冲突、点槽喂非 Point 判 NG、半径 0 判 NG、绘制缺图判 NG、结果字段 20 项。全节点冒烟自动覆盖默认参数。

**未认证**：交点顺序按直线参数从小到大（官方未规定顺序），未做 VM 实机对照。

### 15.10 2026-11-11 第七批：圆圆测量节点（几何测量六模块收口）

官方依据：本机 `GUID-B405EF10-8757-408F-AF44-EC96C69FC259.html`（「几何测量 → 圆圆测量」）。

| 官方能力 | 本仓实现 | 判定 |
|---|---|---|
| 来源选择 = 订阅 / 绘制 | 订阅：`圆1` / `圆2` 两个槽各取 `Measure(circle)`（圆心 = point1，半径 = value）；绘制：两组圆心/半径参数 | 已落地 |
| 圆 1/2 圆心、半径 | `circle1CenterX/Y`、`circle1Radius`、`circle2CenterX/Y`、`circle2Radius` | 已落地 |
| 圆 1/2 内径、起始角、角度范围 | 0 / 0° / 360°（单圆无环宽输入，取官方默认值） | 已落地（默认值口径） |
| 交点 1/2 | `intersect1X/Y`、`intersect2X/Y`：内含/外离 0 个、内切/外切 1 个、相交 2 个；另存 `intersectCount` | 已落地 |
| 距离 | `centerDistance`：两圆圆心距（官方帮助此段文字写「圆心垂直于直线」，与圆圆几何不符，按圆心距实现并在此记录） | 已落地（口径偏差已记） |
| 位置关系 | `relation`：Inside / Inscribe / Intersect / Circumscribe / Outside；圆心重合且半径相同取 Inside | 已落地 |
| 角度（最小外接矩形较长边） | — | **未实现**：帮助描述与圆圆几何不对应，不臆造 |
| 卡尺检测区、按点订阅、特定坐标系 | — | **未实现** |

实现位置：`include/CircleCircleMeasureNode.h`、`src/CircleCircleMeasureNode.cpp`；注册见 `src/NodeRegistry.cpp`（「几何测量」组，别名 圆圆交点 / 两圆位置关系）。

回归：新增 `tests/circle_circle_measure_test.cpp`（`CircleCircleMeasureTest`，7 例）——相交两交点坐标、外离/内含 0 交点、外切/内切 1 交点与切点坐标、订阅两个 circle、只绑一个判 NG、半径 0 判 NG、结果字段 20 项。全节点冒烟自动覆盖默认参数。

至此 VM 4.4 六个几何测量模块（点点/点线/点圆/线线/线圆/圆圆）在本仓均有对应节点；线线与线圆的「线输入 = 按点」已补齐。剩余未对齐项：官方「角度」字段、卡尺检测区、圆圆测量的点输入（官方帮助未确认口径，暂挂）、特定坐标系换算，以及全部数值等价认证。

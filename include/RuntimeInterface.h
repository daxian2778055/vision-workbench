#pragma once

#include <QObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QJsonObject>
#include <QVariant>
#include <QSet>
#include <QStringList>
#include <QHash>

/// 运行界面控件类型（对齐 VisionMaster 4.4 运行界面元素）
enum class RuntimeControlType {
    ImageView,    /// 图像显示：绑定流程节点图像输出
    ValueDisplay, /// 数值显示：绑定全局变量/节点输出
    TextLabel,    /// 文本标签：静态文本
    StatusLight,  /// 状态灯：绑定布尔/字符串全局变量（OK=绿，NG/False=红）
    Button,       /// 按钮：触发流程动作（开始/停止/单次/触发流程）
    ResultTable,  /// 结果表格：多列历史结果（列绑定全局变量/节点输出，按轮提交行）
    IoStatus,     /// IO 状态：直绑相机 IO 节点（成功=灯，值/错误=文本）
    ParamSetting, /// 参数设置：订阅节点 int/float 参数，运行界面直接改（官方 9.3）
    SwitchControl, /// 开关控制：订阅节点 bool 参数，运行界面控制其是否启用（官方 9.3）
    ComboBox,     /// 组合框：在候选值之间切换（切换配方/模式，官方 9.3）
    TabPage,      /// Tab页：页内二级分组（官方 9.3）；实现为**页级**内层 Tab，
                  /// 拖入即启用，不产生控件对象，因此不会有"子控件指向已删容器"的孤儿问题
    ImageAsset,   /// 图片：显示本地图片（官方 9.3「图片控件可添加本地图片」）
    WorldClock,   /// 世界时钟：显示所选时区当前时间（官方 9.3）
    LineChart,    /// 线图阵列：执行次数 vs 当前值，每个数据源一张线图（官方 9.3 图表）
    MultiLineChart, /// 多数据线图：多个数据源同绘一张线图（官方 9.3 图表）
    PieChart,     /// 饼图：各数据源当前值占整体的比例（官方 9.3 图表）
    SetVariable,  /// 设置变量：运行界面把全局变量写成指定值（官方 9.3 基础组）
    RunControl,   /// 运行控制：开始 / 停止 / 单次执行（官方 9.3 基础组）
    OkNgCount,    /// OK/NG：双路计数显示（官方 9.3 基础组）
    ConditionalLight, /// 条件信号灯：按数值条件切换灯色与文本（官方 9.3 基础组）
    MultiImage,   /// 多图像：同一控件内并排显示多路图像（官方「多图像」）
    RunStatus,    /// 运行状态：显示全流程/单一流程的运行中与停止运行（官方「运行状态」）
    UiLog,        /// 界面日志：实时显示软件日志（官方「界面日志」）
    TextEntryCombo, /// 文本框组合：多行「字符信息 + 数据绑定 + 数据类型 + 颜色编辑」
    SubInterface  /// 子界面：点击弹出新的运行界面窗口（官方「子界面」，最多嵌套 3 层）
};

/// 多图像的一个画面（官方：画面名称 + 图像源）
struct MultiImagePanel {
    QString name;
    QString bindKey;   /// 图像源 = 节点完整名（与 ImageView 同一绑定口径）
};

/// 多图像画面的显示名：画面表为空或某画面未命名时退化成「画面N」。
/// 越界索引返回占位名而不是越界读（空画面表 = 单占位画面）。
QString multiImagePanelName(const QList<MultiImagePanel> &panels, int index);

/// 条件信号灯的一条条件（官方 9.3）：按列表顺序求值，首个满足的条件生效
struct LightCondition {
    int op = 0;            /// 0 >= / 1 > / 2 <= / 3 < / 4 == / 5 !=
    double threshold = 0.0;
    QString color = QStringLiteral("#3db97a");
    QString text;          /// 满足时的显示文本（空 = 沿用控件标题）
};

/// 文本框组合的一行（官方：字符信息 / 数据绑定 / 数据类型 / 数据源 / 颜色编辑）
struct TextEntryItem {
    QString text;          /// 未绑定 = 静态文本；绑定后 = 描述前缀或格式串
    QString bindType;      /// "global" / "node" / ""（无绑定）
    QString bindKey;
    int dataType = 0;      /// 0=string / 1=int / 2=float（官方只此三型）
    QString format;        /// 空 = 按类型默认；string 认 %s、int 认 %Nd（N 位补零）、float 认 %1.Nf
    QString color = QStringLiteral("#d8d8e0");
    /// 颜色编辑：按列表顺序取首个命中；int/float 用 threshold 数值比较，
    /// string 只用 op 4（==）/5（!=），比较值放在 text 里。
    QList<LightCondition> colorConditions;
};

/// 文本框组合一行的显示文本（bound=false 时直接返回描述前缀）。
/// 绑定值按 dataType + format 格式化；格式化语法不合用时退回“前缀 + 原值”，不吞值。
QString formatTextEntry(const TextEntryItem &item, const QVariant &value, bool bound);

/// 文本框组合一行的显示颜色（首个命中的颜色条件；无命中用 item.color）
QString textEntryDisplayColor(const TextEntryItem &item, const QVariant &value, bool bound);

/// 文本框组合空行占位文案（设计器预览与运行视图共用，避免两处漂移）
QString textEntryEmptyText();

/// 结果表格列（ResultTable 专用）
struct ResultColumn {
    QString header;      /// 列标题
    QString bindType;    /// "global" 全局变量 / "node" 节点输出
    QString bindKey;     /// 全局变量名 / 节点完整名
};

/// 线图阵列的一条数据（官方：数据类型 int/float、数据源、名称、默认颜色、上下限）
struct ChartSeries {
    QString name;        /// 显示名（默认 = 绑定键）
    QString bindType;    /// "global" / "node"
    QString bindKey;     /// 全局变量名 / 节点完整名
    QString color = QStringLiteral("#4FC3F7");
    bool showLimits = false;
    double lower = 0.0;
    double upper = 100.0;
};

/// 控件所属区域（官方 9.1 工具条可增加上/下/左/右固定栏；画布是默认区域）
enum class RuntimeZone {
    Canvas = 0,   /// 画布主区域（随窗口缩放）
    TopBar,       /// 顶部固定栏（固定厚度，1:1 不缩放）
    BottomBar,    /// 底部固定栏
    LeftBar,      /// 左侧固定栏
    RightBar      /// 右侧固定栏
};

/// 按钮「数据源配置」的一行动作（官方实机截图：触发命令类型 | 方法 | 输入参数）
struct RuntimeAction {
    QString commandType;   /// "ui" 界面链接 / "param" 参数配置 / "scheme" 方案 / "exec" 执行 / "reset" 重置
    QString method;        /// 方法（随命令类型变化；"exec" 时沿用 start/stop/single/trigger）
    QString param;         /// 输入参数（页面名 / 参数名 / 方案名，可空）
};

/// 运行界面控件描述
struct RuntimeControl {
    RuntimeControlType type = RuntimeControlType::TextLabel;
    QString title;          /// 标题/显示文本
    QRect geometry;         /// 相对画布的位置与大小（像素）
    QString bindKey;        /// 绑定键：全局变量名 / 节点完整名 / 动作ID
    QString bindType;       /// "global" 全局变量 / "node" 节点输出 / "action" 动作 / "" 无
    QString color = QStringLiteral("#3a6ea5"); /// 控件主色（前景/灯色）
    int fontSize = 16;      /// 字体大小
    /// 官方外观分组（2026-11-09 按实机截图补齐）：字体「字形：加粗」+ 背景/边框/点击色。
    /// 三个颜色默认空串 = 不绘制/继承，保证旧布局外观完全不变。
    bool bold = false;
    QString bgColor;        /// 背景色（空 = 不绘制）
    QString borderColor;    /// 边框色（空 = 不绘制）
    QString pressedColor;   /// 按钮按下色（空 = 继承主色）
    bool visible = true;    /// 是否显示
    QList<ResultColumn> columns; /// ResultTable 专用：列配置
    int maxRows = 100;      /// ResultTable 专用：最大保留行数（环形覆盖）
    /// Button 专用动作表（官方「数据源配置」弹窗，可多行）。
    /// 旧布局只有 bindType="action" + bindKey，加载时自动合成一行，不丢配置。
    QList<RuntimeAction> actions;
    /// ParamSetting 专用（官方 9.3「订阅模块 int/float 参数」）：
    /// bindKey = 节点完整名，paramKey = 该节点参数键，paramMin/Max 供运行界面数值框钳制。
    QString paramKey;
    double paramMin = 0.0;
    double paramMax = 100.0;
    /// ComboBox 专用：候选值（显示即值）。选中后按字符串写入 paramKey。
    QStringList comboOptions;
    /// ImageAsset 专用：本地图片路径（空 = 显示占位提示）
    QString imagePath;
    /// WorldClock 专用：相对 UTC 的分钟偏移（如 +480 = UTC+8）
    int timeZoneOffsetMinutes = 480;
    /// 页内二级 Tab 归属：-1 = 不在任何内层 Tab 里；
    /// >=0 = 落在该页 innerTabNames 的第 N 个 Tab（越界时按 -1 处理，见 fromJson）。
    /// 刻意不存"父容器引用"：页级内层 Tab 只有名字列表，删标签不会留下悬空指向。
    int innerTab = -1;
    /// OkNgCount 专用：NG 绑定键（bindKey 存 OK）。两者都按全局变量处理。
    QString bindKey2;
    /// ConditionalLight 专用：按顺序求值，首个满足的条件生效
    QList<LightCondition> lightConditions;
    /// MultiImage 专用（官方「多图像」）：画面数量由列表长度决定；
    /// 布局 0=横向 / 1=纵向 / 2=网格。每画面一个节点图像源。
    QList<MultiImagePanel> imagePanels;
    int multiImageLayout = 0;
    /// RunStatus 专用（官方「运行状态」）：statusSource = "all" 全流程 / "flow" 单一流程；
    /// 忙碌 = 该流程（或任一流程）处于 Running/Paused。空/停 = 空闲显示。
    QString statusSource = QStringLiteral("all");
    QString statusFlow;
    QString idleBgColor = QStringLiteral("#4a4a52");
    QString idleFgColor = QStringLiteral("#d0d0d8");
    QString busyBgColor = QStringLiteral("#3db97a");
    QString busyFgColor = QStringLiteral("#ffffff");
    int cornerRadius = 0;
    /// UiLog 专用（官方「界面日志」）：标题栏开关与最大保留行数
    bool logShowTitle = true;
    int logMaxLines = 500;
    QString logHeaderBgColor = QStringLiteral("#20262a");
    QString logRowBgColor = QStringLiteral("#17171c");
    QString logAltRowBgColor = QStringLiteral("#1c1c22");
    QString logFontColor = QStringLiteral("#d8d8e0");
    /// TextEntryCombo 专用（官方「文本框组合」）：每行一条数据；
    /// 阵列 0=纵向 / 1=横向，间距 0–40 像素。
    QList<TextEntryItem> textEntries;
    int textEntryOrientation = 0;
    int textEntrySpacing = 4;
    bool showTextEntryTitle = true;
    QString textEntryBgColor;        /// 空 = 不绘制背景
    QString textEntryBorderColor;    /// 空 = 不绘制边框
    QString textEntryHeaderBgColor = QStringLiteral("#20262a");
    /// SubInterface 专用（官方「子界面」）：页面存在 RuntimeInterface::subPages，
    /// 控件只持 id（不内嵌页面 ⇒ 撤销快照、复制控件都不会共享/自包含页面数据）。
    QString subPageId;
    QString subPassword;             /// 空 = 打开不需要密码
    bool subIconVisible = true;      /// 官方「显示图标」
    /// 图表（官方 9.3）：线图阵列每源一图 / 多数据线图同绘一图 / 饼图按当前值占比
    QVector<ChartSeries> chartSeries;
    int chartHistoryLimit = 30;   ///< 官方「最新执行数据条数」（线图类）
    QString chartTitle;           ///< 空 = 用控件标题
    bool showChartTitle = true;   ///< 官方「显示标题栏」
    QString chartBgColor = QStringLiteral("#1e1e24");
    QString chartAxisColor = QStringLiteral("#9aa0b0");
    /// 所属区域（官方固定栏）。geometry 的坐标系随区域而定：
    /// Canvas → 相对画布区左上角、随窗口缩放；固定栏 → 相对该栏左上角、1:1 不缩放。
    RuntimeZone zone = RuntimeZone::Canvas;

    QString displayTitle() const
    {
        return title.isEmpty() ? defaultTitle() : title;
    }

    QString defaultTitle() const
    {
        switch (type) {
        case RuntimeControlType::ImageView:    return QStringLiteral("图像显示");
        case RuntimeControlType::ValueDisplay: return QStringLiteral("数值显示");
        case RuntimeControlType::TextLabel:    return QStringLiteral("文本标签");
        case RuntimeControlType::StatusLight:  return QStringLiteral("状态灯");
        case RuntimeControlType::Button:       return QStringLiteral("按钮");
        case RuntimeControlType::ResultTable:  return QStringLiteral("结果表格");
        case RuntimeControlType::IoStatus:     return QStringLiteral("IO状态");
        case RuntimeControlType::ParamSetting: return QStringLiteral("参数设置");
        case RuntimeControlType::SwitchControl: return QStringLiteral("开关控制");
        case RuntimeControlType::ComboBox:    return QStringLiteral("组合框");
        case RuntimeControlType::TabPage:     return QStringLiteral("Tab页");
        case RuntimeControlType::ImageAsset:  return QStringLiteral("图片");
        case RuntimeControlType::WorldClock:  return QStringLiteral("世界时钟");
        case RuntimeControlType::LineChart:   return QStringLiteral("线图阵列");
        case RuntimeControlType::MultiLineChart: return QStringLiteral("多数据线图");
        case RuntimeControlType::PieChart:    return QStringLiteral("饼图");
        case RuntimeControlType::SetVariable: return QStringLiteral("设置变量");
        case RuntimeControlType::RunControl:  return QStringLiteral("运行控制");
        case RuntimeControlType::OkNgCount:   return QStringLiteral("OK/NG");
        case RuntimeControlType::ConditionalLight: return QStringLiteral("条件信号灯");
        case RuntimeControlType::MultiImage:  return QStringLiteral("多图像");
        case RuntimeControlType::RunStatus:   return QStringLiteral("运行状态");
        case RuntimeControlType::UiLog:       return QStringLiteral("界面日志");
        case RuntimeControlType::TextEntryCombo: return QStringLiteral("文本框组合");
        case RuntimeControlType::SubInterface: return QStringLiteral("子界面");
        }
        return QStringLiteral("控件");
    }
};

struct RuntimeInterfacePage;   // 前置声明：下面的区域映射函数只用到它的四向栏厚度

/// 设计画布坐标 → 所属区域 + 该区域内的局部坐标（官方 9.1 四向固定栏）。
/// canvasSize 是整块画布尺寸（含固定栏）；pt 为画布绝对坐标。
/// 顺序：上/下/左/右依次判定，重叠角落归上/下（横条优先，与视觉一致）。
RuntimeZone zoneAtCanvasPoint(const RuntimeInterfacePage &page, const QSize &canvasSize,
                              const QPoint &pt, QPoint *local = nullptr);

/// 区域的中文名（设计器提示与日志用）
QString runtimeZoneName(RuntimeZone zone);

/// 解析按钮动作表里 `param`（参数配置）一行的目标：
///   method = 节点完整名，param = "参数键=值"
/// 值先按数值解析（int/float 参数），失败则按字符串（枚举/模式参数）。
/// key 为空或缺 '=' 返回 false。
bool parseParamActionTarget(const QString &method, const QString &param,
                            QString *nodeName, QString *paramKey, QVariant *value);

/// 画布配置（官方 9.3.1「画布默认为单画面，可配置基础配置与布局分割样式」；
/// 第二张实机截图的画布属性为 显示网格 / 网格颜色 / 背景）
struct RuntimeCanvasConfig {
    int width = 1200;
    int height = 760;
    bool showGrid = false;
    QString gridColor = QStringLiteral("#1e1e1e");
    QString bgColor = QStringLiteral("#383838");
};

/// 运行界面单页：控件集合 + 固定栏 + JSON 序列化
struct RuntimeInterfacePage
{
    QString pageName = QStringLiteral("页面1");
    QList<RuntimeControl> controls;
    /// 固定栏厚度（官方 9.1 工具条「设置是否增加顶部/底部/左侧/右侧固定栏」，可多选）。
    /// 0 = 该方向没有固定栏。分割模板与控件归属是下一步（B2-3b）。
    int topBarHeight = 0;
    int bottomBarHeight = 0;
    int leftBarWidth = 0;
    int rightBarWidth = 0;
    /// 各固定栏的分割格数（官方 9.3.1「固定栏可设置分割模板」）。
    /// 栏厚度为 0 时格数无意义，但保留默认 1 便于直接开启固定栏。
    int topBarSlots = 1;
    int bottomBarSlots = 1;
    int leftBarSlots = 1;
    int rightBarSlots = 1;
    /// 页内二级 Tab（官方 9.3「Tab页」）：空 = 未启用。启用后画布区顶部让出
    /// innerTabHeight 像素给标签条，innerTab >= 0 的控件按 Tab 切换显示。
    QStringList innerTabNames;
    int innerTabHeight = 36;
    /// 运行时默认选中的内层 Tab（越界按 0 处理）
    int defaultInnerTab = 0;

    RuntimeControl *addControl(RuntimeControlType type, const QRect &geo);
    void removeControl(int index);
    void clear();
    int indexOf(const RuntimeControl *ctrl) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);
};

/// 按画布尺寸变化等比缩放一页里「画布区」控件的存储几何（固定栏控件 1:1 不动）。
/// oldSize/newSize 任一非正或相等时返回 false 且不改动。
bool scaleCanvasControls(RuntimeInterfacePage &page, const QSize &oldSize, const QSize &newSize);

/// 运行界面：多页集合（运行时按 Tab 切换）
class RuntimeInterface
{
public:
    RuntimeInterface() { ensurePage(); }

    QList<RuntimeInterfacePage> pages;
    /// 子界面页表（官方「子界面」）：控件用 subPageId 引用，避免控件自包含页面。
    /// 键为 "subN"；删除控件时只删除无其它引用的页（复制控件会共享同一页）。
    QHash<QString, RuntimeInterfacePage> subPages;
    int currentPageIndex = 0;    /// 运行时默认显示页索引
    RuntimeCanvasConfig canvas;  /// 画布尺寸与外观（所有页共用一块画布）

    /// 保证至少一页（空布局也有一页可编辑）
    void ensurePage();
    RuntimeInterfacePage *page(int index);
    RuntimeInterfacePage *currentPage();
    int addPage();                  /// 新增页，返回索引
    void removePage(int index);     /// 删除页（至少保留一页）
    int totalControlCount() const;  /// 全部页控件总数（判断是否已配置）

    /// 新建一个空子界面页并返回其 id（"subN" 递增，避开已有键）。
    QString createSubPage(const QString &pageName = QString());
    /// 按 id 取子界面页（不存在返回 nullptr）。
    RuntimeInterfacePage *subPage(const QString &id);
    /// 子界面嵌套深度：根页 = 0，一级子界面 = 1……未知 id 返回 -1。
    /// 用 BFS 从根页沿控件引用计算，带 visited 防环。
    int subPageDepth(const QString &id) const;
    /// 某子界面页是否仍被任一页/子页的 SubInterface 控件引用。
    bool subPageReferenced(const QString &id) const;
    /// 无引用时删除该子页（有引用时保留，返回 false）。
    bool removeSubPageIfUnused(const QString &id);

    /// 绑定体检（2026-10-27）：返回**失效**的绑定键（找不到对应全局变量/节点）。
    /// 设计器与运行视图共用同一判据——否则会出现"设计器看得见、运行态红一片"或反过来。
    /// 失效键以 "global:<键>" / "node:<键>" 形式返回，便于定位到具体控件来源。
    QStringList staleBindings(const QSet<QString> &globals, const QSet<QString> &nodes) const;

    /// 运行界面布局文件当前结构版本，与方案文件 ProjectManager::kProjectSchemaVersion
    /// 同级：写出时带上、读入时高于本值拒收，避免新版本文件被旧程序静默读坏。
    static constexpr int kLayoutSchemaVersion = 2;
    /// 子界面最大嵌套层数（官方口径 3 层；超深直接拒收，防递归展开失控）
    static constexpr int kMaxSubPageDepth = 3;

    /// 最近一次 fromJson 的诊断（版本不匹配 / 环 / 超深 / 悬空引用被清理），空 = 无问题
    QString lastLoadWarning() const { return m_loadWarning; }

    /// 结构自检：悬空 subPageId 就地清空（可修复，记入 warning）；
    /// 引用环或超深返回 false（致命，调用方应拒收文件）。
    bool validateStructure();

    QJsonObject toJson() const;
    /// 校验版本与结构后才填充本对象；任何失败都**不改动现有状态**。
    bool fromJson(const QJsonObject &json);
    bool saveToFile(const QString &fileName) const;
    bool loadFromFile(const QString &fileName);

private:
    QString m_loadWarning;
};

/// 控件类型 → 中文名
QString runtimeControlTypeName(RuntimeControlType type);
/// 中文名 → 控件类型（无法识别返回 false）
bool runtimeControlTypeFromName(const QString &name, RuntimeControlType &out);

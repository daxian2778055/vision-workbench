#pragma once

#include <QObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QJsonObject>
#include <QVariant>
#include <QSet>
#include <QStringList>

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
    WorldClock    /// 世界时钟：显示所选时区当前时间（官方 9.3）
};

/// 结果表格列（ResultTable 专用）
struct ResultColumn {
    QString header;      /// 列标题
    QString bindType;    /// "global" 全局变量 / "node" 节点输出
    QString bindKey;     /// 全局变量名 / 节点完整名
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
    /// 所属区域（官方固定栏）。geometry 的坐标系随区域而定：
    /// Canvas → 相对画布区左上角、随窗口缩放；固定栏 → 相对该栏左上角、1:1 不缩放。
    RuntimeZone zone = RuntimeZone::Canvas;
    /// 固定栏内的分割格序号（0 起）。栏被切成 N 格时，控件落在第 zoneSlot 格。
    int zoneSlot = 0;

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

/// 运行界面：多页集合（运行时按 Tab 切换）
class RuntimeInterface
{
public:
    RuntimeInterface() { ensurePage(); }

    QList<RuntimeInterfacePage> pages;
    int currentPageIndex = 0;    /// 运行时默认显示页索引
    RuntimeCanvasConfig canvas;  /// 画布尺寸与外观（所有页共用一块画布）

    /// 保证至少一页（空布局也有一页可编辑）
    void ensurePage();
    RuntimeInterfacePage *page(int index);
    RuntimeInterfacePage *currentPage();
    int addPage();                  /// 新增页，返回索引
    void removePage(int index);     /// 删除页（至少保留一页）
    int totalControlCount() const;  /// 全部页控件总数（判断是否已配置）

    /// 绑定体检（2026-10-27）：返回**失效**的绑定键（找不到对应全局变量/节点）。
    /// 设计器与运行视图共用同一判据——否则会出现"设计器看得见、运行态红一片"或反过来。
    /// 失效键以 "global:<键>" / "node:<键>" 形式返回，便于定位到具体控件来源。
    QStringList staleBindings(const QSet<QString> &globals, const QSet<QString> &nodes) const;

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);   /// 兼容旧单页格式
    bool saveToFile(const QString &fileName) const;
    bool loadFromFile(const QString &fileName);
};

/// 控件类型 → 中文名
QString runtimeControlTypeName(RuntimeControlType type);
/// 中文名 → 控件类型（无法识别返回 false）
bool runtimeControlTypeFromName(const QString &name, RuntimeControlType &out);

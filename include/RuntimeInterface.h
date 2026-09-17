#pragma once

#include <QObject>
#include <QList>
#include <QRect>
#include <QString>
#include <QJsonObject>
#include <QVariant>

/// 运行界面控件类型（对齐 VisionMaster 4.4 运行界面元素）
enum class RuntimeControlType {
    ImageView,    /// 图像显示：绑定流程节点图像输出
    ValueDisplay, /// 数值显示：绑定全局变量/节点输出
    TextLabel,    /// 文本标签：静态文本
    StatusLight,  /// 状态灯：绑定布尔/字符串全局变量（OK=绿，NG/False=红）
    Button,       /// 按钮：触发流程动作（开始/停止/单次/触发流程）
    ResultTable,  /// 结果表格：多列历史结果（列绑定全局变量/节点输出，按轮提交行）
    IoStatus      /// IO 状态：直绑相机 IO 节点（成功=灯，值/错误=文本）
};

/// 结果表格列（ResultTable 专用）
struct ResultColumn {
    QString header;      /// 列标题
    QString bindType;    /// "global" 全局变量 / "node" 节点输出
    QString bindKey;     /// 全局变量名 / 节点完整名
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
    bool visible = true;    /// 是否显示
    QList<ResultColumn> columns; /// ResultTable 专用：列配置
    int maxRows = 100;      /// ResultTable 专用：最大保留行数（环形覆盖）

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

/// 运行界面单页：控件集合 + JSON 序列化
struct RuntimeInterfacePage
{
    QString pageName = QStringLiteral("页面1");
    QList<RuntimeControl> controls;

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

    /// 保证至少一页（空布局也有一页可编辑）
    void ensurePage();
    RuntimeInterfacePage *page(int index);
    RuntimeInterfacePage *currentPage();
    int addPage();                  /// 新增页，返回索引
    void removePage(int index);     /// 删除页（至少保留一页）
    int totalControlCount() const;  /// 全部页控件总数（判断是否已配置）

    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &json);   /// 兼容旧单页格式
    bool saveToFile(const QString &fileName) const;
    bool loadFromFile(const QString &fileName);
};

/// 控件类型 → 中文名
QString runtimeControlTypeName(RuntimeControlType type);
/// 中文名 → 控件类型（无法识别返回 false）
bool runtimeControlTypeFromName(const QString &name, RuntimeControlType &out);

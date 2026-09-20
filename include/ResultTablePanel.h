#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QHash>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;

/// 结果数据表：一次运行后汇总「各模块 → 输出项 / 数值」，并标注状态与耗时。
/// 与 OutputDataViewer（看选中节点的端口数据）互补：这里看整条流程的全部数值结果，
/// 便于现场对照、报警定位与导出（对标 VisionMaster 的「结果」面板）。
class ResultTablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ResultTablePanel(QWidget *parent = nullptr);

    /// 用某模块的一次执行结果更新表格；同一模块重复调用只原地更新，不新增行沿用旧值。
    void setModuleResult(int moduleId, const QString &moduleName, bool success,
                         qint64 elapsedMs, const QVariantMap &vars);
    /// 标记某模块本轮被跳过（未激活分支 / 循环体调度）：状态列显示"跳过"（灰色），
    /// 与"失败"区分——现场排查"哪个算子没跑"靠它，而不是从"失败"里猜。
    void setModuleSkipped(int moduleId, const QString &moduleName);
    /// 清空全部结果
    void clearResults();
    /// 设置筛选关键字（按模块名/输出项/值模糊匹配；空串表示显示全部）
    void setFilterText(const QString &text);
    /// 当前已展示的模块行数
    int moduleCount() const;
    /// 导出为 CSV（带 UTF-8 BOM，Excel 可直接打开）
    bool exportCsv(const QString &path, QString *errorOut = nullptr) const;
    /// 本次运行的明细报告文本（流程名/时间/汇总 + 各模块输出项与数值）。
    /// 报表对话框统计的是「数据库历史」，本报告补的是「数据库没存的测量值明细」。
    QString toReportText(const QString &flowName = QString()) const;

private:
    void updateSummary();
    /// 按 m_filterText 逐行套用筛选（模块行：自身或任一可见子项命中即保留）
    void applyFilter();

    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QLineEdit *m_filter = nullptr;           ///< 筛选输入框
    QString m_filterText;                    ///< 当前筛选关键字（空=显示全部）
    QHash<int, QTreeWidgetItem *> m_rows;    ///< 模块号 -> 顶层行
};

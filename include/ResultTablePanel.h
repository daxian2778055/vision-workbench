#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QHash>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

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
    /// 清空全部结果
    void clearResults();
    /// 当前已展示的模块行数
    int moduleCount() const;
    /// 导出为 CSV（带 UTF-8 BOM，Excel 可直接打开）
    bool exportCsv(const QString &path, QString *errorOut = nullptr) const;

private:
    void updateSummary();

    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QHash<int, QTreeWidgetItem *> m_rows;   ///< 模块号 -> 顶层行
};

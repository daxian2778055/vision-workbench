#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QHash>
#include <QStringList>
#include <QVector>

#include "NodeResultFields.h"

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QLineEdit;
class QTabWidget;
class QTableWidget;
class QTextBrowser;

/// 结果数据表：一次运行后汇总「各模块 → 输出项 / 数值」，并标注状态与耗时。
/// 与 OutputDataViewer（看选中节点的端口数据）互补：这里看整条流程的全部数值结果，
/// 便于现场对照、报警定位与导出（对标 VisionMaster 的「结果」面板）。
class ResultTablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit ResultTablePanel(QWidget *parent = nullptr);

    /// 用某模块的一次执行结果更新表格；同一模块重复调用只原地更新，不新增行沿用旧值。
    /// fields 非空时按它声明的顺序/名称/单位/精度渲染（VM「模块结果」口径，只显示结果字段）；
    /// 空表 = 旧行为（平铺 vars 全部键，供尚未对齐的算子）。
    void setModuleResult(int moduleId, const QString &moduleName, bool success,
                         qint64 elapsedMs, const QVariantMap &vars,
                         const QVector<NodeResultField> &fields = QVector<NodeResultField>());
    /// 标记某模块本轮被跳过（未激活分支 / 循环体调度）：状态列显示"跳过"（灰色），
    /// 与"失败"区分——现场排查"哪个算子没跑"靠它，而不是从"失败"里猜。
    void setModuleSkipped(int moduleId, const QString &moduleName);
    /// 清空全部结果
    void clearResults();
    /// 设置筛选关键字（按模块名/输出项/值模糊匹配；空串表示显示全部）
    void setFilterText(const QString &text);
    /// 当前已展示的模块行数
    int moduleCount() const;
    /// 轮次开始（每次「开始执行」调用）：递增执行序号并清空「当前结果」。
    /// 历史页保留此前各轮，供 VM「当前/历史」页签对照。
    void beginRunRound();
    /// 跟随画布选中模块：在当前结果中高亮并滚动到该模块（-1 = 清除高亮）。
    void setCurrentModule(int moduleId);
    /// 导出为 CSV（带 UTF-8 BOM，Excel 可直接打开）
    bool exportCsv(const QString &path, QString *errorOut = nullptr) const;
    /// 本次运行的明细报告文本（流程名/时间/汇总 + 各模块输出项与数值）。
    /// 报表对话框统计的是「数据库历史」，本报告补的是「数据库没存的测量值明细」。
    QString toReportText(const QString &flowName = QString()) const;
    /// 帮助信息页内容（跟随画布选中模块更新；实装 4.4 的“帮助信息”语义）
    void setHelpContent(const QString &html);

private:
    void updateSummary();
    /// 按 m_filterText 逐行套用筛选（模块行：自身或任一可见子项命中即保留）
    void applyFilter();
    /// 追加一条历史记录（当前结果页每次更新时同步写入；最多保留 500 条）
    void appendHistoryEntry(int moduleId, const QString &moduleName, const QString &status,
                            qint64 elapsedMs, const QStringList &values);

    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QLineEdit *m_filter = nullptr;           ///< 筛选输入框
    QString m_filterText;                    ///< 当前筛选关键字（空=显示全部）
    QHash<int, QTreeWidgetItem *> m_rows;    ///< 模块号 -> 顶层行
    QTabWidget *m_tabs = nullptr;            ///< 当前结果 / 历史结果 / 帮助（P1-2）
    QTableWidget *m_history = nullptr;       ///< 历史结果表
    QTextBrowser *m_helpText = nullptr;      ///< 帮助信息页（随选中模块联动）
    int m_runSequence = 0;                   ///< 执行轮次序号（历史记录的「轮次」列）
    int m_currentModuleId = -1;              ///< 画布当前选中模块（跟随高亮）
};

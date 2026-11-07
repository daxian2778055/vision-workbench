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
class QCheckBox;

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
    /// 实装 4.4「结果显示区域」：选中的是采集 / 逻辑工具模块时只展示历史结果
    /// （隐藏「当前结果」页并切到历史页）；其它模块恢复「当前 + 历史」双页。
    /// 该开关只作用于被 setPageVisibility 允许显示的页签——底部结果区若配置为
    /// 「仅历史 / 帮助」，这里不会把当前结果页反手翻出来。
    void setHistoryOnlySelection(bool historyOnly);
    /// 页签可见性配置：同一面板复用两处——
    /// 图像区「模块结果」只看当前轮结果（current=true, history=false, help=false）；
    /// 底部结果区只看历史与帮助（current=false, history=true, help=true）。
    /// 默认三页全开（测试与独立使用口径不变）。
    void setPageVisibility(bool current, bool history, bool help);
    /// 嵌入式显示：隐藏内部页签栏（唯一允许显示的页直接铺满）。
    /// 图像区「模块结果」页用它，避免单页还多出一条页签栏。
    void setEmbeddedMode(bool on);
    /// 当前是否处于“仅历史结果”模式（供测试/诊断）
    bool historyOnlySelection() const { return m_historyOnly; }
    /// 「当前结果」是否只显示画布选中的模块（VM 4.4 口径；关闭后显示全部模块）。
    /// 默认开启；历史页同样按选中模块过滤，导出 CSV / 报告始终包含全部模块。
    void setFollowSelectionEnabled(bool on);
    bool followSelectionEnabled() const { return m_followSelectionEnabled; }
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
    bool m_historyOnly = false;              ///< 采集/逻辑工具选中态：结果区仅历史结果
    QCheckBox *m_followSelection = nullptr;  ///< 「仅显示选中模块」开关（当前结果页顶栏）
    bool m_followSelectionEnabled = true;    ///< 是否按选中模块过滤（默认 VM 口径开启）
    /// 页签可见性配置（两处复用：图像区「模块结果」只留当前；底部结果区只留历史/帮助）
    bool m_showCurrentPage = true;
    bool m_showHistoryPage = true;
    bool m_showHelpPage = true;
    bool m_embedded = false;                 ///< 嵌入模式：隐藏内部页签栏
};

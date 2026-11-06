#include "ResultTablePanel.h"
#include "ExportWriteCheck.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QHeaderView>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QBrush>
#include <QColor>
#include <QMessageBox>
#include <QTime>
#include <QDateTime>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QSignalBlocker>
#include <QSet>
#include <QtNumeric>
#include <functional>

namespace {
constexpr int kColName = 0;
constexpr int kColValue = 1;
constexpr int kColStatus = 2;
constexpr int kColElapsed = 3;
constexpr int kColSeq = 4;
constexpr int kColTime = 5;

/// 结果树子项：模块行仍参与表头排序，子项（结果字段/分组/原因）恒按创建顺序（=规范声明顺序）插入。
/// QTreeWidget 开启排序后会对**所有层级**自动排序，会把「耗时/模块状态/边缘0…」的官方顺序打乱。
class ResultTreeItem : public QTreeWidgetItem
{
public:
    explicit ResultTreeItem(QTreeWidgetItem *parent)
        : QTreeWidgetItem(parent), m_seq(++s_seq) {}

    bool operator<(const QTreeWidgetItem &other) const override
    {
        if (const ResultTreeItem *o = dynamic_cast<const ResultTreeItem *>(&other))
            return m_seq < o->m_seq;   // 子项：按声明顺序
        return QTreeWidgetItem::operator<(other);   // 顶层模块行：按表头文本
    }

private:
    quint64 m_seq;
    static quint64 s_seq;
};
quint64 ResultTreeItem::s_seq = 0;

/// 显示名 + 单位（VM「模块结果」把单位写在字段名后，如「耗时(ms)」）
QString withUnit(const QString &label, const QString &unit)
{
    return unit.isEmpty() ? label : QStringLiteral("%1(%2)").arg(label, unit);
}

/// 单值格式化：bool 保持 true/false；数值按 decimals 定点，-1 用有效数字。
QString formatScalar(const QVariant &v, int decimals)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    const int type = v.metaType().id();
    if (type == QMetaType::Double || type == QMetaType::Float) {
        const double d = v.toDouble();
        if (qIsNaN(d))
            return QStringLiteral("NaN");
        return decimals >= 0 ? QString::number(d, 'f', decimals) : QString::number(d, 'g', 10);
    }
    return v.toString();
}

/// 数组元素格式化（list/point/pairLine 三种口径）
QString formatListItem(const NodeResultField &field, const QString &item)
{
    const int dec = field.decimals >= 0 ? field.decimals : 2;
    switch (field.kind) {
    case NodeResultField::PolarityItem: {
        const int p = item.toInt();
        if (p == 1) return QStringLiteral("从黑到白");
        if (p == 2) return QStringLiteral("从白到黑");
        if (p == 3) return QStringLiteral("任意极性");
        return item;
    }
    case NodeResultField::PointItem: {
        const QStringList xy = item.split(QLatin1Char(','));
        if (xy.size() >= 2) {
            return QStringLiteral("X=%1, Y=%2")
                .arg(xy.at(0).toDouble(), 0, 'f', dec)
                .arg(xy.at(1).toDouble(), 0, 'f', dec);
        }
        return item;
    }
    case NodeResultField::PairLineItem: {
        const QStringList v = item.split(QLatin1Char(','));
        if (v.size() >= 5) {
            return QStringLiteral("起点(%1, %2) → 终点(%3, %4)，宽度=%5")
                .arg(v.at(0).toDouble(), 0, 'f', dec)
                .arg(v.at(1).toDouble(), 0, 'f', dec)
                .arg(v.at(2).toDouble(), 0, 'f', dec)
                .arg(v.at(3).toDouble(), 0, 'f', dec)
                .arg(v.at(4).toDouble(), 0, 'f', dec);
        }
        return item;
    }
    default:
        break;
    }
    bool ok = false;
    const double d = item.toDouble(&ok);
    if (ok && field.decimals >= 0)
        return QString::number(d, 'f', field.decimals);
    return item;
}

/// 历史摘要：组行折叠成「组名[子项=值; …]」，避免把整棵结果树平铺成一行巨串
void collectSummary(QTreeWidgetItem *item, QStringList *out)
{
    for (int c = 0; c < item->childCount(); ++c) {
        QTreeWidgetItem *child = item->child(c);
        if (child->childCount() > 0) {
            QStringList sub;
            collectSummary(child, &sub);
            out->append(QStringLiteral("%1[%2]").arg(child->text(kColName), sub.join(QStringLiteral("; "))));
            continue;
        }
        const QString value = child->text(kColValue);
        out->append(value.isEmpty() ? child->text(kColName)
                                    : QStringLiteral("%1=%2").arg(child->text(kColName), value));
    }
}

/// 按 NodeResultField 声明顺序渲染结果字段（VM「模块结果」口径）。
/// 返回已经渲染过的键，供调用方补判红原因、避免重复。
QSet<QString> renderResultFields(QTreeWidgetItem *row, const QVariantMap &vars,
                                 const QVector<NodeResultField> &fields)
{
    QSet<QString> rendered;
    QHash<QString, QTreeWidgetItem *> groups;   // 组标签 → 组行（同一序号的多个字段共用）
    for (const NodeResultField &field : fields) {
        if (!vars.contains(field.key))
            continue;   // 失败轮没写到的字段不画空行（模块状态/原因由调用方补）
        rendered.insert(field.key);
        const QVariant value = vars.value(field.key);

        if (field.kind == NodeResultField::Status) {
            const bool ok = value.toBool();
            auto *child = new ResultTreeItem(row);
            child->setText(kColName, withUnit(field.label, field.unit));
            child->setText(kColValue, ok ? QStringLiteral("OK") : QStringLiteral("NG"));
            child->setForeground(kColValue, QBrush(ok ? QColor(0x1B, 0x7F, 0x37)
                                                      : QColor(0xC0, 0x2B, 0x1D)));
            continue;
        }
        if (field.kind == NodeResultField::Value) {
            auto *child = new ResultTreeItem(row);
            child->setText(kColName, withUnit(field.label, field.unit));
            child->setText(kColValue, formatScalar(value, field.decimals));
            continue;
        }

        // 逐结果字段：值本身就是列表（半角逗号或分号分隔）
        const QChar sep = (field.kind == NodeResultField::PointItem
                           || field.kind == NodeResultField::PairLineItem)
                              ? QLatin1Char(';')
                              : QLatin1Char(',');
        const QStringList items = value.toString().split(sep, Qt::SkipEmptyParts);
        for (int i = 0; i < items.size(); ++i) {
            const QString groupLabel =
                field.group.contains(QLatin1String("%1")) ? field.group.arg(i) : field.group;
            QTreeWidgetItem *group = groups.value(groupLabel, nullptr);
            if (!group) {
                group = new ResultTreeItem(row);
                group->setText(kColName, groupLabel);
                groups.insert(groupLabel, group);
            }
            auto *child = new ResultTreeItem(group);
            child->setText(kColName, withUnit(field.label, field.unit));
            child->setText(kColValue, formatListItem(field, items.at(i)));
        }
    }
    return rendered;
}
}   // namespace

ResultTablePanel::ResultTablePanel(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QHBoxLayout();
    auto *clearBtn = new QPushButton(QStringLiteral("清空"), this);
    auto *exportBtn = new QPushButton(QStringLiteral("导出 CSV"), this);
    exportBtn->setToolTip(QStringLiteral("把当前结果导出为 CSV（含模块/输出项/值/状态/耗时）"));
    auto *reportBtn = new QPushButton(QStringLiteral("导出报告"), this);
    reportBtn->setToolTip(QStringLiteral("导出本次运行的明细报告（含各模块测量值；"
                                         "数据库只记 OK/失败，报告用于现场留档）"));
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("筛选：模块 / 输出项 / 值"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(220);
    bar->addWidget(clearBtn);
    bar->addWidget(exportBtn);
    bar->addWidget(reportBtn);
    bar->addWidget(m_filter);
    bar->addStretch();
    m_summary = new QLabel(QStringLiteral("暂无结果"), this);
    bar->addWidget(m_summary);
    // 顶部操作栏稍后放入「当前结果」页（P1-2 三页签）

    // 顶部操作栏稍后放入「当前结果」页（P1-2 三页签）

    m_tree = new QTreeWidget(this);
    // P2-13：结果表补「执行序号 / 执行时间」两列（VM 结果显示区域口径）
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({QStringLiteral("模块 / 输出项"), QStringLiteral("值"),
                             QStringLiteral("状态"), QStringLiteral("耗时(ms)"),
                             QStringLiteral("执行序号"), QStringLiteral("执行时间")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setRootIsDecorated(true);
    // 开启表头点击排序，并显式设定初始排序键：只调 setSortingEnabled() 时 sortColumn() 仍是 -1，
    // 后续按当前排序键重排会变成空操作（新模块不会归位）。
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(kColName, Qt::AscendingOrder);
    // ── P1-2：VM 结果区三页签「当前结果 / 历史结果 / 帮助」──
    auto *currentPage = new QWidget(this);
    auto *currentLay = new QVBoxLayout(currentPage);
    currentLay->setContentsMargins(0, 0, 0, 0);
    currentLay->addLayout(bar);
    currentLay->addWidget(m_tree, 1);

    auto *historyPage = new QWidget(this);
    auto *historyLay = new QVBoxLayout(historyPage);
    historyLay->setContentsMargins(0, 0, 0, 0);
    m_history = new QTableWidget(0, 6, historyPage);
    m_history->setHorizontalHeaderLabels(
        {QStringLiteral("轮次"), QStringLiteral("时间"), QStringLiteral("模块"),
         QStringLiteral("状态"), QStringLiteral("耗时(ms)"), QStringLiteral("输出摘要")});
    m_history->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_history->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_history->setAlternatingRowColors(true);
    m_history->horizontalHeader()->setStretchLastSection(true);
    m_history->verticalHeader()->setVisible(false);
    historyLay->addWidget(m_history, 1);

    auto *helpPage = new QWidget(this);
    auto *helpLay = new QVBoxLayout(helpPage);
    helpLay->setContentsMargins(0, 0, 0, 0);
    auto *helpText = new QTextBrowser(helpPage);
    m_helpText = helpText;
    helpText->setOpenExternalLinks(false);
    helpText->setHtml(QStringLiteral(
        "<p><b>结果区说明（对标 VisionMaster 4.4）</b></p>"
        "<p><b>当前结果</b>：显示本轮各模块的输出项、状态、耗时、执行序号与执行时间；"
        "在画布上选中模块时会自动高亮对应行。执行新的一轮会清空并重新汇总。</p>"
        "<p><b>历史结果</b>：按轮次保留每次执行的模块状态与关键输出（最多 500 条），"
        "用于对比不同参数/不同来料下的结果变化。</p>"
        "<p><b>状态含义</b>：<span style='color:#1B7F37;'>成功</span> = 算子正常返回；"
        "<span style='color:#C02B1D;'>失败</span> = 算子判红或异常；"
        "<span style='color:#808080;'>跳过</span> = 分支未激活或由循环调度，本轮未执行。</p>"
        "<p><b>导出</b>：CSV 为扁平表（模块/输出项/值/状态/耗时）；"
        "运行明细报告额外带流程名与生成时间，便于现场留档。</p>"));
    helpLay->addWidget(helpText, 1);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(currentPage, QStringLiteral("当前结果"));
    m_tabs->addTab(historyPage, QStringLiteral("历史结果"));
    m_tabs->addTab(helpPage, QStringLiteral("帮助信息"));
    root->addWidget(m_tabs, 1);

    connect(clearBtn, &QPushButton::clicked, this, &ResultTablePanel::clearResults);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &t) { setFilterText(t); });
    connect(exportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出结果"),
                                                          QStringLiteral("results.csv"),
                                                          QStringLiteral("CSV (*.csv)"));
        if (path.isEmpty())
            return;
        QString err;
        if (!exportCsv(path, &err))
            QMessageBox::warning(this, QStringLiteral("导出失败"), err);
    });
    connect(reportBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出运行明细报告"),
                                                          QStringLiteral("run-report.txt"),
                                                          QStringLiteral("文本 (*.txt);;全部文件 (*)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        // 打开与收尾都走 ExportWriteCheck（2026-10-26 复核：此前这里只查 open，
        // 写失败照样当成功，句子也与其它导出重复）
        if (!ExportWriteCheck::openForExport(this, file, path))
            return;
        QTextStream out(&file);
        out << toReportText();
        ExportWriteCheck::finishTextExport(this, file, out, path);
    });
}

QString ResultTablePanel::toReportText(const QString &flowName) const
{
    QStringList lines;
    lines << QStringLiteral("Vision Flow Platform · 运行明细报告");
    lines << QStringLiteral("流程: %1")
                 .arg(flowName.isEmpty() ? QStringLiteral("(未命名)") : flowName);
    lines << QStringLiteral("生成时间: %1")
                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));

    const int total = m_tree ? m_tree->topLevelItemCount() : 0;
    if (total == 0) {
        lines << QString();
        lines << QStringLiteral("（暂无结果：请先运行一次流程）");
        return lines.join(QLatin1Char('\n'));
    }

    int failed = 0;
    int skipped = 0;
    for (int i = 0; i < total; ++i) {
        const QString st = m_tree->topLevelItem(i)->text(kColStatus);
        if (st == QStringLiteral("失败"))
            ++failed;
        else if (st == QStringLiteral("跳过"))
            ++skipped;   // 跳过 ≠ 失败：报告分开汇总
    }
    lines << QStringLiteral("汇总: %1 个模块，失败 %2 个，跳过 %3 个").arg(total).arg(failed).arg(skipped);
    lines << QString();

    for (int i = 0; i < total; ++i) {
        QTreeWidgetItem *row = m_tree->topLevelItem(i);
        lines << QStringLiteral("■ %1（%2，%3 ms，第 %4 次 @ %5）")
                     .arg(row->text(kColName), row->text(kColStatus), row->text(kColElapsed),
                          row->text(kColSeq), row->text(kColTime));
        if (row->childCount() == 0) {
            lines << QStringLiteral("    - （无输出项）");
            continue;
        }
        // 分组行（如「边缘0」）要连同子项一起打印，报告才能还原结果树
        std::function<void(QTreeWidgetItem *, int)> dump = [&](QTreeWidgetItem *parent, int depth) {
            const QString pad(QStringLiteral("    ") + QString(depth, QLatin1Char(' ')));
            for (int c = 0; c < parent->childCount(); ++c) {
                QTreeWidgetItem *item = parent->child(c);
                if (item->childCount() > 0) {
                    lines << QStringLiteral("%1- [%2]").arg(pad, item->text(kColName));
                    dump(item, depth + 2);
                    continue;
                }
                const QString value = item->text(kColValue);
                lines << (value.isEmpty()
                              ? QStringLiteral("%1- %2").arg(pad, item->text(kColName))
                              : QStringLiteral("%1- %2 = %3")
                                    .arg(pad, item->text(kColName), value));
            }
        };
        dump(row, 0);
    }
    return lines.join(QLatin1Char('\n'));
}

void ResultTablePanel::setHelpContent(const QString &html)
{
    if (m_helpText)
        m_helpText->setHtml(html);
}

void ResultTablePanel::setModuleResult(int moduleId, const QString &moduleName, bool success,
                                       qint64 elapsedMs, const QVariantMap &vars,
                                       const QVector<NodeResultField> &fields)
{
    QTreeWidgetItem *row = m_rows.value(moduleId, nullptr);
    const bool isNew = (row == nullptr);
    if (isNew) {
        row = new QTreeWidgetItem(m_tree);
        m_rows.insert(moduleId, row);
    }

    row->setText(kColName, moduleName.isEmpty() ? QStringLiteral("模块 %1").arg(moduleId) : moduleName);
    row->setText(kColStatus, success ? QStringLiteral("成功") : QStringLiteral("失败"));
    row->setText(kColElapsed, QString::number(elapsedMs));
    // P2-13：执行序号（轮次）与执行时间（本模块结果落地时刻）
    row->setText(kColSeq, QString::number(m_runSequence));
    row->setText(kColTime, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    row->setForeground(kColStatus, success ? QBrush(QColor(0x1B, 0x7F, 0x37))
                                           : QBrush(QColor(0xC0, 0x2B, 0x1D)));
    row->setText(kColValue, QString());

    // 子项每次重建：保证同一模块重复执行时显示的是本轮结果（值不会与上一轮叠加或残留旧值）
    const QList<QTreeWidgetItem *> old = row->takeChildren();
    qDeleteAll(old);

    // U-18：子项构建不再只在成功分支执行——判红轮执行器会把算子本轮写下的「原因」字段
    // （口径见 include/NodeResultFields.h）随快照推出，必须渲染出来；
    // 口径以外的字段仍为空，故下面按同一套格式化泛型遍历。
    if (!fields.isEmpty()) {
        // VM「模块结果」口径：只按声明顺序渲染结果字段，配置参数不混进结果表
        const QSet<QString> rendered = renderResultFields(row, vars, fields);
        QStringList reasonKeys;
        for (const QString &k : vars.keys()) {
            if (isNodeReasonKey(k) && !rendered.contains(k))
                reasonKeys << k;
        }
        reasonKeys.sort();
        for (const QString &k : reasonKeys) {
            auto *child = new ResultTreeItem(row);
            child->setText(kColName, k);
            child->setText(kColValue, formatScalar(vars.value(k), -1));
        }
    } else {
        QStringList keys = vars.keys();
        keys.sort();   // 名称排序，便于多轮之间对照
        for (const QString &k : keys) {
            const QVariant v = vars.value(k);
            QString text;
            if (v.typeId() == QMetaType::Bool)
                text = v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
            else if (v.metaType().id() == QMetaType::Double || v.metaType().id() == QMetaType::Float)
                text = QString::number(v.toDouble(), 'g', 10);
            else
                text = v.toString();
            auto *child = new ResultTreeItem(row);
            child->setText(kColName, k);
            child->setText(kColValue, text);
        }
    }
    if (!success && row->childCount() == 0) {
        // 本轮没人写下原因（基类守卫那几条不写）⇒ 保留占位，现场至少知道这行判红了且无原因可读
        auto *child = new ResultTreeItem(row);
        child->setText(kColName, QStringLiteral("(执行失败，无输出)"));
    }

    row->setExpanded(true);
    if (isNew) {
        // 首次出现时把列宽铺开，避免值/状态列被挤住
        m_tree->resizeColumnToContents(kColName);
    }
    // P1-2：当前结果同步写入历史页（同轮同模块覆盖，避免重复）
    QStringList summary;
    collectSummary(row, &summary);
    QString summaryText = summary.join(QStringLiteral("; "));
    if (summaryText.size() > 400)
        summaryText = summaryText.left(399) + QStringLiteral("…");
    appendHistoryEntry(moduleId, row->text(kColName),
                       success ? QStringLiteral("成功") : QStringLiteral("失败"),
                       elapsedMs, {summaryText});
    if (!m_filterText.isEmpty())
        applyFilter();   // 筛选生效期间新增的结果也要遵守筛选
    // 排序：Qt 只在「插入时」排序，而行文本是插入后才填的，所以这里按当前排序键显式重排；
    // 否则新模块永远落在末尾，用户点了表头排序也会被新数据打乱。
    if (m_tree->isSortingEnabled() && m_tree->topLevelItemCount() > 1)
        m_tree->sortItems(m_tree->sortColumn(), m_tree->header()->sortIndicatorOrder());
    updateSummary();
}

void ResultTablePanel::setModuleSkipped(int moduleId, const QString &moduleName)
{
    QTreeWidgetItem *row = m_rows.value(moduleId, nullptr);
    const bool isNew = (row == nullptr);
    if (isNew) {
        row = new QTreeWidgetItem(m_tree);
        m_rows.insert(moduleId, row);
    }

    row->setText(kColName, moduleName.isEmpty() ? QStringLiteral("模块 %1").arg(moduleId) : moduleName);
    row->setText(kColStatus, QStringLiteral("跳过"));
    row->setForeground(kColStatus, QBrush(QColor(0x80, 0x80, 0x80)));   // 灰色：未执行 ≠ 失败
    row->setText(kColElapsed, QStringLiteral("-"));
    row->setText(kColSeq, QString::number(m_runSequence));
    row->setText(kColTime, QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    row->setText(kColValue, QString());

    const QList<QTreeWidgetItem *> old = row->takeChildren();
    qDeleteAll(old);
    auto *child = new ResultTreeItem(row);
    child->setText(kColName, QStringLiteral("(本轮未执行：分支未激活或由循环调度)"));
    row->setExpanded(true);
    // P1-2：跳过也要进历史（现场排查"哪个算子没跑"需要跨轮对照）
    appendHistoryEntry(moduleId, row->text(kColName), QStringLiteral("跳过"), -1,
                       {child->text(kColName)});

    if (isNew)
        m_tree->resizeColumnToContents(kColName);
    if (!m_filterText.isEmpty())
        applyFilter();
    if (m_tree->isSortingEnabled() && m_tree->topLevelItemCount() > 1)
        m_tree->sortItems(m_tree->sortColumn(), m_tree->header()->sortIndicatorOrder());
    updateSummary();
}

void ResultTablePanel::setFilterText(const QString &text)
{
    m_filterText = text;
    if (m_filter && m_filter->text() != text) {
        // 程序化设置时同步输入框，但屏蔽信号避免再回调一次（递归）
        const QSignalBlocker blocker(m_filter);
        m_filter->setText(text);
    }
    applyFilter();
}

void ResultTablePanel::applyFilter()
{
    if (!m_tree)
        return;

    const bool filtering = !m_filterText.isEmpty();
    auto matches = [this](const QTreeWidgetItem *item) {
        for (int c = 0; c < m_tree->columnCount(); ++c) {
            if (item->text(c).contains(m_filterText, Qt::CaseInsensitive))
                return true;
        }
        return false;
    };

    // 结果字段可能带「边缘0」这类分组行（三层），筛选必须递归到叶子，
    // 否则组行会被当成"无命中且无可见子项"整片隐藏。
    std::function<bool(QTreeWidgetItem *, bool)> filterItem =
        [&](QTreeWidgetItem *item, bool ancestorHit) -> bool {
        const bool selfHit = ancestorHit || !filtering || matches(item);
        bool anyChildVisible = false;
        for (int c = 0; c < item->childCount(); ++c)
            anyChildVisible = filterItem(item->child(c), selfHit) || anyChildVisible;
        const bool visible = selfHit || anyChildVisible;
        item->setHidden(filtering && !visible);
        return visible;
    };
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        filterItem(m_tree->topLevelItem(i), false);
}

void ResultTablePanel::clearResults()
{
    m_tree->clear();
    m_rows.clear();
    updateSummary();
}

int ResultTablePanel::moduleCount() const
{
    return m_tree ? m_tree->topLevelItemCount() : 0;
}

void ResultTablePanel::beginRunRound()
{
    ++m_runSequence;
    clearResults();
}

void ResultTablePanel::setCurrentModule(int moduleId)
{
    m_currentModuleId = moduleId;
    if (!m_tree)
        return;
    m_tree->clearSelection();
    m_tree->setCurrentItem(nullptr);
    if (moduleId < 0)
        return;
    if (QTreeWidgetItem *row = m_rows.value(moduleId, nullptr)) {
        if (row->isHidden())
            return;   // 被筛选隐藏时不强行选中
        row->setSelected(true);
        m_tree->setCurrentItem(row);
        m_tree->scrollToItem(row, QAbstractItemView::PositionAtCenter);
    }
}

void ResultTablePanel::appendHistoryEntry(int moduleId, const QString &moduleName,
                                          const QString &status, qint64 elapsedMs,
                                          const QStringList &values)
{
    Q_UNUSED(moduleId)
    if (!m_history)
        return;

    // 同一轮内同一模块可能先"跳过"后被更新/重试：先删掉该轮同名旧行，保证一条
    const QString roundText = QString::number(m_runSequence);
    for (int r = m_history->rowCount() - 1; r >= 0; --r) {
        QTableWidgetItem *roundItem = m_history->item(r, 0);
        QTableWidgetItem *moduleItem = m_history->item(r, 2);
        if (roundItem && moduleItem && roundItem->text() == roundText
            && moduleItem->text() == moduleName) {
            m_history->removeRow(r);
        }
    }

    m_history->insertRow(0);
    m_history->setItem(0, 0, new QTableWidgetItem(roundText));
    m_history->setItem(0, 1, new QTableWidgetItem(
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))));
    m_history->setItem(0, 2, new QTableWidgetItem(moduleName));
    m_history->setItem(0, 3, new QTableWidgetItem(status));
    m_history->setItem(0, 4, new QTableWidgetItem(
        elapsedMs < 0 ? QStringLiteral("-") : QString::number(elapsedMs)));
    m_history->setItem(0, 5, new QTableWidgetItem(values.join(QStringLiteral("; "))));

    if (status == QStringLiteral("成功"))
        m_history->item(0, 3)->setForeground(QBrush(QColor(0x1B, 0x7F, 0x37)));
    else if (status == QStringLiteral("失败"))
        m_history->item(0, 3)->setForeground(QBrush(QColor(0xC0, 0x2B, 0x1D)));
    else
        m_history->item(0, 3)->setForeground(QBrush(QColor(0x80, 0x80, 0x80)));

    // 历史上限：500 条，超出丢弃最旧（避免长时间运行内存无界增长）
    while (m_history->rowCount() > 500)
        m_history->removeRow(m_history->rowCount() - 1);
}

void ResultTablePanel::updateSummary()
{
    if (!m_summary)
        return;
    int total = 0;
    int failed = 0;
    int skipped = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        ++total;
        const QString st = m_tree->topLevelItem(i)->text(kColStatus);
        if (st == QStringLiteral("失败"))
            ++failed;
        else if (st == QStringLiteral("跳过"))
            ++skipped;   // 跳过 ≠ 失败：三态分开计数
    }
    if (total == 0) {
        m_summary->setText(QStringLiteral("暂无结果"));
        return;
    }
    m_summary->setText(QStringLiteral("共 %1 个模块（失败 %2，跳过 %3） · 更新于 %4")
                           .arg(total)
                           .arg(failed)
                           .arg(skipped)
                           .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))));
}

bool ResultTablePanel::exportCsv(const QString &path, QString *errorOut) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }

    auto esc = [](const QString &s) {
        QString t = s;
        t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"%1\"").arg(t);
    };

    QTextStream out(&file);   // Qt6 默认 UTF-8
    out << QChar(0xFEFF);     // BOM：Excel 直接打开不乱码
    out << esc(QStringLiteral("模块")) << ',' << esc(QStringLiteral("输出项")) << ','
        << esc(QStringLiteral("值")) << ',' << esc(QStringLiteral("状态")) << ','
        << esc(QStringLiteral("耗时(ms)")) << ',' << esc(QStringLiteral("执行序号")) << ','
        << esc(QStringLiteral("执行时间")) << '\n';

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_tree->topLevelItem(i);
        const QString module = row->text(kColName);
        const QString status = row->text(kColStatus);
        const QString elapsed = row->text(kColElapsed);
        const QString seq = row->text(kColSeq);
        const QString time = row->text(kColTime);
        if (row->childCount() == 0) {
            out << esc(module) << ',' << esc(QString()) << ',' << esc(QString()) << ','
                << esc(status) << ',' << esc(elapsed) << ',' << esc(seq) << ','
                << esc(time) << '\n';
            continue;
        }
        // CSV 是扁平表：分组行的「组名 + 子项名」拼成输出项名（如「边缘0 极性」）
        std::function<void(QTreeWidgetItem *, const QString &)> dump =
            [&](QTreeWidgetItem *parent, const QString &prefix) {
            for (int c = 0; c < parent->childCount(); ++c) {
                QTreeWidgetItem *item = parent->child(c);
                const QString name = prefix.isEmpty()
                                         ? item->text(kColName)
                                         : QStringLiteral("%1 %2").arg(prefix, item->text(kColName));
                if (item->childCount() > 0) {
                    dump(item, name);
                    continue;
                }
                out << esc(module) << ',' << esc(name) << ',' << esc(item->text(kColValue)) << ','
                    << esc(status) << ',' << esc(elapsed) << ',' << esc(seq) << ','
                    << esc(time) << '\n';
            }
        };
        dump(row, QString());
    }

    // 写失败要能报回调用方（2026-10-26 复核）：原先 close 后直接 return true，
    // 磁盘满/网络盘断开会当成功。
    out.flush();
    const bool streamOk = out.status() == QTextStream::Ok;
    const bool flushOk = file.flush();
    file.close();
    if (!streamOk || !flushOk) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }
    return true;
}

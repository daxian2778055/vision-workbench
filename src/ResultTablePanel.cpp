#include "ResultTablePanel.h"

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

namespace {
constexpr int kColName = 0;
constexpr int kColValue = 1;
constexpr int kColStatus = 2;
constexpr int kColElapsed = 3;
}

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
    root->addLayout(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("模块 / 输出项"), QStringLiteral("值"),
                             QStringLiteral("状态"), QStringLiteral("耗时(ms)")});
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setRootIsDecorated(true);
    // 开启表头点击排序，并显式设定初始排序键：只调 setSortingEnabled() 时 sortColumn() 仍是 -1，
    // 后续按当前排序键重排会变成空操作（新模块不会归位）。
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(kColName, Qt::AscendingOrder);
    root->addWidget(m_tree, 1);

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
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("导出失败"),
                                 QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
            return;
        }
        QTextStream out(&file);
        out << toReportText();
        file.close();
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
        lines << QStringLiteral("■ %1（%2，%3 ms）")
                     .arg(row->text(kColName), row->text(kColStatus), row->text(kColElapsed));
        if (row->childCount() == 0) {
            lines << QStringLiteral("    - （无输出项）");
            continue;
        }
        for (int c = 0; c < row->childCount(); ++c) {
            QTreeWidgetItem *child = row->child(c);
            const QString value = child->text(kColValue);
            lines << (value.isEmpty()
                          ? QStringLiteral("    - %1").arg(child->text(kColName))
                          : QStringLiteral("    - %1 = %2").arg(child->text(kColName), value));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void ResultTablePanel::setModuleResult(int moduleId, const QString &moduleName, bool success,
                                       qint64 elapsedMs, const QVariantMap &vars)
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
    row->setForeground(kColStatus, success ? QBrush(QColor(0x1B, 0x7F, 0x37))
                                           : QBrush(QColor(0xC0, 0x2B, 0x1D)));
    row->setText(kColValue, QString());

    // 子项每次重建：保证同一模块重复执行时显示的是本轮结果（值不会与上一轮叠加或残留旧值）
    const QList<QTreeWidgetItem *> old = row->takeChildren();
    qDeleteAll(old);

    if (success) {
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
            auto *child = new QTreeWidgetItem(row);
            child->setText(kColName, k);
            child->setText(kColValue, text);
        }
    } else {
        auto *child = new QTreeWidgetItem(row);
        child->setText(kColName, QStringLiteral("(执行失败，无输出)"));
    }

    row->setExpanded(true);
    if (isNew) {
        // 首次出现时把列宽铺开，避免值/状态列被挤住
        m_tree->resizeColumnToContents(kColName);
    }
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
    row->setText(kColValue, QString());

    const QList<QTreeWidgetItem *> old = row->takeChildren();
    qDeleteAll(old);
    auto *child = new QTreeWidgetItem(row);
    child->setText(kColName, QStringLiteral("(本轮未执行：分支未激活或由循环调度)"));
    row->setExpanded(true);

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

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_tree->topLevelItem(i);
        const bool rowHit = matches(row);   // 模块行：名称/状态/耗时命中
        bool anyChildVisible = false;
        for (int c = 0; c < row->childCount(); ++c) {
            QTreeWidgetItem *child = row->child(c);
            const bool visible = !filtering || rowHit || matches(child);
            child->setHidden(!visible);
            anyChildVisible = anyChildVisible || visible;
        }
        // 模块行：有子项时只要还有可见子项就保留；无子项时以自身命中为准
        row->setHidden(filtering && !rowHit && !anyChildVisible);
    }
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
        << esc(QStringLiteral("耗时(ms)")) << '\n';

    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *row = m_tree->topLevelItem(i);
        const QString module = row->text(kColName);
        const QString status = row->text(kColStatus);
        const QString elapsed = row->text(kColElapsed);
        if (row->childCount() == 0) {
            out << esc(module) << ',' << esc(QString()) << ',' << esc(QString()) << ','
                << esc(status) << ',' << esc(elapsed) << '\n';
            continue;
        }
        for (int c = 0; c < row->childCount(); ++c) {
            QTreeWidgetItem *child = row->child(c);
            out << esc(module) << ',' << esc(child->text(kColName)) << ','
                << esc(child->text(kColValue)) << ',' << esc(status) << ',' << esc(elapsed) << '\n';
        }
    }

    file.close();
    return true;
}

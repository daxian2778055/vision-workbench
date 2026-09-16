#include "ResultTablePanel.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
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
    bar->addWidget(clearBtn);
    bar->addWidget(exportBtn);
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
    root->addWidget(m_tree, 1);

    connect(clearBtn, &QPushButton::clicked, this, &ResultTablePanel::clearResults);
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
    updateSummary();
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
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        ++total;
        if (m_tree->topLevelItem(i)->text(kColStatus) != QStringLiteral("成功"))
            ++failed;
    }
    if (total == 0) {
        m_summary->setText(QStringLiteral("暂无结果"));
        return;
    }
    m_summary->setText(QStringLiteral("共 %1 个模块（失败 %2） · 更新于 %3")
                           .arg(total)
                           .arg(failed)
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

#include "FeatureLibraryDialog.h"

#include "AppDatabase.h"
#include "FeatureIndex.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

FeatureLibraryDialog::FeatureLibraryDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("特征库管理"));
    resize(880, 520);

    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("检测器：")));
    m_detectorFilter = new QComboBox();
    m_detectorFilter->addItems({QStringLiteral("全部"), QStringLiteral("ORB"), QStringLiteral("SIFT")});
    connect(m_detectorFilter, &QComboBox::currentTextChanged, this, &FeatureLibraryDialog::fillTable);
    top->addWidget(m_detectorFilter);
    top->addStretch();
    m_summary = new QLabel();
    top->addWidget(m_summary);
    root->addLayout(top);

    auto *mid = new QHBoxLayout();
    m_table = new QTableWidget();
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("样本名"),
                                        QStringLiteral("检测器"), QStringLiteral("特征点"),
                                        QStringLiteral("入库时间(UTC)")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &FeatureLibraryDialog::onSelectionChanged);
    mid->addWidget(m_table, 3);

    m_preview = new QLabel(QStringLiteral("（选中一行查看缩略图）"));
    m_preview->setMinimumWidth(260);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setStyleSheet(QStringLiteral("background:#222; color:#aaa;"));
    mid->addWidget(m_preview, 2);
    root->addLayout(mid, 1);

    auto *bottom = new QHBoxLayout();
    m_deleteBtn = new QPushButton(QStringLiteral("删除所选"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &FeatureLibraryDialog::onDeleteSelected);
    m_clearBtn = new QPushButton(QStringLiteral("清空特征库"));
    connect(m_clearBtn, &QPushButton::clicked, this, &FeatureLibraryDialog::onClearAll);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    connect(refreshBtn, &QPushButton::clicked, this, &FeatureLibraryDialog::reload);
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(m_deleteBtn);
    bottom->addWidget(m_clearBtn);
    bottom->addStretch();
    bottom->addWidget(refreshBtn);
    bottom->addWidget(closeBtn);
    root->addLayout(bottom);

    reload();
}

void FeatureLibraryDialog::reload()
{
    fillTable();
}

void FeatureLibraryDialog::fillTable()
{
    m_table->setRowCount(0);
    const QString det = m_detectorFilter->currentText();
    const QStringList detectors = (det == QStringLiteral("全部"))
                                      ? QStringList{QStringLiteral("ORB"), QStringLiteral("SIFT")}
                                      : QStringList{det};
    int total = 0;
    for (const QString &d : detectors) {
        const auto lib = AppDatabase::instance()->loadFeatureSamples(d);
        total += lib.size();
        for (const FeatureSample &s : lib) {
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            m_table->setItem(row, 0, new QTableWidgetItem(QString::number(s.id)));
            m_table->setItem(row, 1, new QTableWidgetItem(s.name));
            m_table->setItem(row, 2, new QTableWidgetItem(s.detector));
            m_table->setItem(row, 3, new QTableWidgetItem(QString::number(s.descRows)));
            m_table->setItem(row, 4,
                             new QTableWidgetItem(s.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
        }
    }
    m_summary->setText(QStringLiteral("共 %1 个样本").arg(total));
    m_preview->setPixmap(QPixmap());
    m_preview->setText(QStringLiteral("（选中一行查看缩略图）"));
}

void FeatureLibraryDialog::onSelectionChanged()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const int id = m_table->item(row, 0)->text().toInt();
    const QString det = m_table->item(row, 2)->text();
    for (const FeatureSample &s : AppDatabase::instance()->loadFeatureSamples(det)) {
        if (s.id != id)
            continue;
        QPixmap px;
        if (px.loadFromData(s.imagePng)) {
            m_preview->setPixmap(px.scaled(m_preview->size(), Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation));
            m_preview->setText(QString());
        } else {
            m_preview->setPixmap(QPixmap());
            m_preview->setText(QStringLiteral("（该样本没有缩略图）"));
        }
        break;
    }
}

void FeatureLibraryDialog::onDeleteSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const int id = m_table->item(row, 0)->text().toInt();
    const QString name = m_table->item(row, 1)->text();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("确认"));
    box.setText(QStringLiteral("确定要删除样本 %1 吗？").arg(name));
    QPushButton *yes = box.addButton(QStringLiteral("删除"), QMessageBox::YesRole);
    QPushButton *no = box.addButton(QStringLiteral("取消"), QMessageBox::NoRole);
    box.setDefaultButton(no);   // 破坏性操作默认取消
    box.exec();
    if (box.clickedButton() != yes)
        return;
    if (!AppDatabase::instance()->deleteFeatureSample(id)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("删除失败（写操作闸/数据库？）"));
        return;
    }
    fillTable();
}

void FeatureLibraryDialog::onClearAll()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("确认"));
    box.setText(QStringLiteral("确定要清空整个特征库吗？此操作不可撤销。"));
    QPushButton *yes = box.addButton(QStringLiteral("清空"), QMessageBox::YesRole);
    QPushButton *no = box.addButton(QStringLiteral("取消"), QMessageBox::NoRole);
    box.setDefaultButton(no);
    box.exec();
    if (box.clickedButton() != yes)
        return;
    if (!AppDatabase::instance()->clearFeatureSamples()) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("清空失败（写操作闸/数据库？）"));
        return;
    }
    fillTable();
}

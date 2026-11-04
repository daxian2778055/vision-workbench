#include "LargeFileStoreDialog.h"
#include "LargeFileImageStore.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

LargeFileStoreDialog::LargeFileStoreDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("采集策略"));
    resize(640, 480);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_enabledCheck = new QCheckBox(QStringLiteral("启用大文件存图"), this);
    form->addRow(QString(), m_enabledCheck);

    auto *dirRow = new QHBoxLayout();
    m_dirEdit = new QLineEdit(this);
    m_dirEdit->setPlaceholderText(QStringLiteral("选择 .dat 保存目录"));
    m_browseBtn = new QPushButton(QStringLiteral("浏览…"), this);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(m_browseBtn);
    form->addRow(QStringLiteral("大文件路径"), dirRow);

    m_batchSpin = new QSpinBox(this);
    m_batchSpin->setRange(1, 10000);
    m_batchSpin->setValue(100);
    m_batchSpin->setSuffix(QStringLiteral(" 张/批"));
    form->addRow(QStringLiteral("每批张数"), m_batchSpin);
    layout->addLayout(form);

    auto *note = new QLabel(
        QStringLiteral("开启后，各输出图像模块不再按自身路径逐张写盘，而是攒够一批后统一写成 .dat；"
                       "关闭或停止流程时会把未满一批立即落盘。"),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8f9aa8;"));
    layout->addWidget(note);

    auto *pendingRow = new QHBoxLayout();
    m_pendingLabel = new QLabel(this);
    auto *flushBtn = new QPushButton(QStringLiteral("立即写入当前批次"), this);
    pendingRow->addWidget(m_pendingLabel, 1);
    pendingRow->addWidget(flushBtn);
    layout->addLayout(pendingRow);

    layout->addWidget(new QLabel(QStringLiteral("已生成的批次文件（双击可解包）："), this));
    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("文件"), QStringLiteral("大小"), QStringLiteral("修改时间")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);

    auto *bottom = new QHBoxLayout();
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新列表"), this);
    m_extractBtn = new QPushButton(QStringLiteral("解包选中批次…"), this);
    m_extractBtn->setEnabled(false);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    bottom->addWidget(refreshBtn);
    bottom->addWidget(m_extractBtn);
    bottom->addStretch();
    bottom->addWidget(buttons);
    layout->addLayout(bottom);

    connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_dirEdit->setEnabled(on);
        m_browseBtn->setEnabled(on);
        m_batchSpin->setEnabled(on);
    });
    connect(m_browseBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, QStringLiteral("选择大文件存图目录"), m_dirEdit->text());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    connect(flushBtn, &QPushButton::clicked, this, [this]() {
        applySettings();
        if (LargeFileImageStore::instance()->flush() > 0)
            QMessageBox::information(this, QStringLiteral("采集策略"),
                                     QStringLiteral("当前批次已写入磁盘。"));
        refreshPendingLabel();
        refreshBatchList();
    });
    connect(refreshBtn, &QPushButton::clicked, this, &LargeFileStoreDialog::refreshBatchList);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // Apply 按钮不触发 accepted/rejected，单独接
    if (QPushButton *applyBtn = buttons->button(QDialogButtonBox::Apply)) {
        connect(applyBtn, &QPushButton::clicked, this, &LargeFileStoreDialog::applySettings);
    }
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_extractBtn->setEnabled(m_table->currentRow() >= 0);
    });
    connect(m_table, &QTableWidget::doubleClicked, this,
            [this](const QModelIndex &) { extractSelected(); });
    connect(m_extractBtn, &QPushButton::clicked, this, &LargeFileStoreDialog::extractSelected);

    loadSettingsToUi();
    refreshPendingLabel();
    refreshBatchList();
}

void LargeFileStoreDialog::loadSettingsToUi()
{
    const LargeFileImageStore::Settings s = LargeFileImageStore::instance()->settings();
    m_enabledCheck->setChecked(s.enabled);
    m_dirEdit->setText(s.directory);
    m_batchSpin->setValue(s.batchSize);
    m_dirEdit->setEnabled(s.enabled);
    m_browseBtn->setEnabled(s.enabled);
    m_batchSpin->setEnabled(s.enabled);
}

void LargeFileStoreDialog::applySettings()
{
    LargeFileImageStore::Settings s;
    s.enabled = m_enabledCheck->isChecked();
    s.directory = m_dirEdit->text().trimmed();
    s.batchSize = m_batchSpin->value();
    QString error;
    if (!LargeFileImageStore::instance()->setSettings(s, &error)) {
        QMessageBox::warning(this, QStringLiteral("采集策略"), error);
        return;
    }
    m_dirEdit->setText(s.directory);
    refreshPendingLabel();
    refreshBatchList();
}

void LargeFileStoreDialog::refreshPendingLabel()
{
    const auto s = LargeFileImageStore::instance()->settings();
    m_pendingLabel->setText(QStringLiteral("状态：%1 ｜ 待写入 %2 张 ｜ 每批 %3 张")
                                .arg(s.enabled ? QStringLiteral("已启用")
                                               : QStringLiteral("未启用"))
                                .arg(LargeFileImageStore::instance()->pendingCount())
                                .arg(s.batchSize));
}

void LargeFileStoreDialog::refreshBatchList()
{
    m_table->setRowCount(0);
    m_extractBtn->setEnabled(false);
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty())
        return;
    QDir d(dir);
    const QFileInfoList files = d.entryInfoList({QStringLiteral("*.dat")}, QDir::Files,
                                                QDir::Name | QDir::Reversed);
    for (const QFileInfo &fi : files) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *nameItem = new QTableWidgetItem(fi.fileName());
        nameItem->setData(Qt::UserRole, fi.absoluteFilePath());
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(
                                     QStringLiteral("%1 KB").arg(fi.size() / 1024)));
        m_table->setItem(row, 2, new QTableWidgetItem(
                                     fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
}

void LargeFileStoreDialog::extractSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString path = m_table->item(row, 0)->data(Qt::UserRole).toString();
    const QString outDir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择解包输出目录"), QFileInfo(path).absolutePath());
    if (outDir.isEmpty())
        return;
    QString error;
    const int count = LargeFileImageStore::extractBatch(path, outDir, &error);
    if (count <= 0 || !error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("解包失败"),
                             error.isEmpty() ? QStringLiteral("没有可解包的图像") : error);
        return;
    }
    QMessageBox::information(this, QStringLiteral("解包完成"),
                             QStringLiteral("已解出 %1 张图像到：\n%2").arg(count).arg(outDir));
}

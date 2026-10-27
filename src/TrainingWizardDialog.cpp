#include "TrainingWizardDialog.h"

#include "ClassifierTrainer.h"
#include "OpencvClassifyNode.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

TrainingWizardDialog::TrainingWizardDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("训练向导"));
    resize(980, 640);

    auto *root = new QVBoxLayout(this);

    // 数据集
    auto *dsRow = new QHBoxLayout();
    dsRow->addWidget(new QLabel(QStringLiteral("数据集目录（按类别子目录组织）：")));
    m_dirEdit = new QLineEdit(QDir::current().filePath(QStringLiteral("data/annotations")));
    dsRow->addWidget(m_dirEdit, 1);
    auto *browseBtn = new QPushButton(QStringLiteral("浏览…"));
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择数据集目录"),
                                                            m_dirEdit->text());
        if (!d.isEmpty())
            m_dirEdit->setText(d);
    });
    dsRow->addWidget(browseBtn);
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新统计"));
    connect(refreshBtn, &QPushButton::clicked, this, &TrainingWizardDialog::refreshDataset);
    dsRow->addWidget(refreshBtn);
    root->addLayout(dsRow);

    // 类别统计
    m_classTable = new QTableWidget();
    m_classTable->setColumnCount(2);
    m_classTable->setHorizontalHeaderLabels({QStringLiteral("类别"), QStringLiteral("样本数")});
    m_classTable->horizontalHeader()->setStretchLastSection(true);
    m_classTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_classTable, 1);

    // 训练参数
    auto *form = new QFormLayout();
    m_featureMode = new QComboBox();
    m_featureMode->addItems({QStringLiteral("HOG 64×64（更稳）"), QStringLiteral("32×32 灰度（更快）")});
    form->addRow(QStringLiteral("特征"), m_featureMode);
    m_hidden = new QSpinBox();
    m_hidden->setRange(4, 512);
    m_hidden->setValue(64);
    form->addRow(QStringLiteral("隐层神经元"), m_hidden);
    m_iters = new QSpinBox();
    m_iters->setRange(50, 10000);
    m_iters->setValue(500);
    form->addRow(QStringLiteral("最大迭代"), m_iters);
    m_valRatio = new QDoubleSpinBox();
    m_valRatio->setRange(0.05, 0.95);
    m_valRatio->setSingleStep(0.05);
    m_valRatio->setValue(0.30);
    form->addRow(QStringLiteral("验证比例"), m_valRatio);
    m_modelName = new QLineEdit(QStringLiteral("model.yaml"));
    form->addRow(QStringLiteral("模型文件名"), m_modelName);
    m_augEnable = new QCheckBox(QStringLiteral("启用数据增广（小样本必开）"));
    form->addRow(QString(), m_augEnable);
    m_augFactor = new QSpinBox();
    m_augFactor->setRange(1, 10);
    m_augFactor->setValue(3);
    form->addRow(QStringLiteral("每图扩成 N 张"), m_augFactor);
    m_augRotate = new QDoubleSpinBox();
    m_augRotate->setRange(0.0, 45.0);
    m_augRotate->setValue(8.0);
    form->addRow(QStringLiteral("随机旋转 ±°"), m_augRotate);
    m_augNoise = new QDoubleSpinBox();
    m_augNoise->setRange(0.0, 30.0);
    m_augNoise->setValue(6.0);
    form->addRow(QStringLiteral("高斯噪声 σ"), m_augNoise);
    root->addLayout(form);

    // 按钮
    auto *btns = new QHBoxLayout();
    auto *trainBtn = new QPushButton(QStringLiteral("训练"));
    connect(trainBtn, &QPushButton::clicked, this, &TrainingWizardDialog::onTrain);
    auto *evalBtn = new QPushButton(QStringLiteral("评估"));
    connect(evalBtn, &QPushButton::clicked, this, &TrainingWizardDialog::onEvaluate);
    auto *publishBtn = new QPushButton(QStringLiteral("发布并插入分类节点"));
    connect(publishBtn, &QPushButton::clicked, this, [this]() {
        if (!QFile::exists(modelPath())) {
            QMessageBox::warning(this, QStringLiteral("失败"),
                                 QStringLiteral("模型不存在，请先训练：%1").arg(modelPath()));
            return;
        }
        QJsonObject params;
        params[QStringLiteral("modelPath")] = modelPath();
        if (insertNode) {
            insertNode(QStringLiteral("OpencvClassifyNode"), params);
            m_log->appendPlainText(QStringLiteral("已插入分类节点并预填模型路径：%1").arg(modelPath()));
        } else {
            m_log->appendPlainText(QStringLiteral("发布：请把模型路径填到分类节点 → %1").arg(modelPath()));
        }
    });
    auto *closeBtn = new QPushButton(QStringLiteral("关闭"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btns->addWidget(trainBtn);
    btns->addWidget(evalBtn);
    btns->addWidget(publishBtn);
    btns->addStretch();
    btns->addWidget(closeBtn);
    root->addLayout(btns);

    // 结果区
    auto *mid = new QHBoxLayout();
    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    mid->addWidget(m_log, 3);
    auto *right = new QVBoxLayout();
    m_summary = new QLabel(QStringLiteral("尚未评估"));
    right->addWidget(m_summary);
    m_confusion = new QTableWidget();
    right->addWidget(m_confusion, 1);
    mid->addLayout(right, 2);
    root->addLayout(mid, 2);

    refreshDataset();
}

QString TrainingWizardDialog::datasetDir() const
{
    return m_dirEdit->text().trimmed();
}

QString TrainingWizardDialog::modelPath() const
{
    QString name = m_modelName->text().trimmed();
    if (name.isEmpty())
        name = QStringLiteral("model.yaml");
    return QDir(datasetDir()).filePath(name);
}

void TrainingWizardDialog::refreshDataset()
{
    m_classTable->setRowCount(0);
    const QDir dir(datasetDir());
    if (!dir.exists()) {
        m_log->appendPlainText(QStringLiteral("数据集目录不存在：%1").arg(datasetDir()));
        return;
    }
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    int total = 0;
    for (const QString &sub : subs) {
        const int n = QDir(dir.filePath(sub))
                          .entryList({QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                      QStringLiteral("*.jpeg"), QStringLiteral("*.bmp")},
                                     QDir::Files)
                          .size();
        const int row = m_classTable->rowCount();
        m_classTable->insertRow(row);
        m_classTable->setItem(row, 0, new QTableWidgetItem(sub));
        m_classTable->setItem(row, 1, new QTableWidgetItem(QString::number(n)));
        total += n;
    }
    m_log->appendPlainText(QStringLiteral("数据集：%1 个类别，共 %2 张").arg(subs.size()).arg(total));
    if (subs.size() < 2)
        m_log->appendPlainText(QStringLiteral("提示：训练需要至少 2 个类别子目录"));
}

void TrainingWizardDialog::onTrain()
{
    const QString dir = datasetDir();
    if (!QDir(dir).exists()) {
        QMessageBox::warning(this, QStringLiteral("失败"), QStringLiteral("数据集目录不存在"));
        return;
    }
    const std::string savePath = modelPath().toStdString();
    m_log->appendPlainText(QStringLiteral("开始训练：%1 …").arg(modelPath()));
    setCursor(Qt::BusyCursor);
    ClsTrainer::AugmentOptions aug;
    aug.enabled = m_augEnable->isChecked();
    aug.factor = m_augFactor->value();
    aug.rotateDeg = m_augRotate->value();
    aug.noiseSigma = m_augNoise->value();
    const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
        dir.toStdString(), savePath, m_featureMode->currentIndex(),
        m_hidden->value(), m_iters->value(), aug);
    unsetCursor();

    if (!st.ok) {
        m_log->appendPlainText(QStringLiteral("训练失败：%1")
                                   .arg(QString::fromStdString(st.error)));
        return;
    }
    m_log->appendPlainText(
        QStringLiteral("训练完成：%1 个样本 / %2 类 / 特征 %3 维\n模型已保存：%4")
            .arg(st.samples)
            .arg(st.classes)
            .arg(st.featDim)
            .arg(modelPath()));
    m_log->appendPlainText(QStringLiteral("下一步：点「评估」看模型质量；发布时把模型路径填到分类节点。"));
}

void TrainingWizardDialog::onEvaluate()
{
    const QString mp = modelPath();
    if (!QFile::exists(mp)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("模型文件不存在，请先训练：%1").arg(mp));
        return;
    }
    setCursor(Qt::BusyCursor);
    const ClsTrainer::EvalResult ev = ClsTrainer::evaluateFromDir(
        mp.toStdString(), datasetDir().toStdString(), m_featureMode->currentIndex(),
        m_valRatio->value(), 42);
    unsetCursor();

    if (!ev.ok) {
        m_log->appendPlainText(QStringLiteral("评估失败：%1").arg(QString::fromStdString(ev.error)));
        m_summary->setText(QStringLiteral("评估失败"));
        return;
    }
    m_summary->setText(QStringLiteral("验证集 %1 张，正确 %2，准确率 %3%")
                           .arg(ev.total)
                           .arg(ev.correct)
                           .arg(ev.accuracy * 100.0, 0, 'f', 1));
    m_log->appendPlainText(QStringLiteral("评估：%1/%2 正确（%3%）")
                               .arg(ev.correct)
                               .arg(ev.total)
                               .arg(ev.accuracy * 100.0, 0, 'f', 1));

    // 混淆矩阵
    const int n = int(ev.classNames.size());
    m_confusion->clear();
    m_confusion->setRowCount(n);
    m_confusion->setColumnCount(n + 1);
    QStringList headers{QStringLiteral("真\\预")};
    for (const auto &c : ev.classNames)
        headers << QString::fromStdString(c);
    m_confusion->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < n; ++i) {
        m_confusion->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(ev.classNames[size_t(i)])));
        for (int j = 0; j < n; ++j)
            m_confusion->setItem(i, j + 1,
                                 new QTableWidgetItem(QString::number(ev.confusion[size_t(i)][size_t(j)])));
    }
}

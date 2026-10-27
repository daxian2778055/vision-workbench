#include "TrainingWizardDialog.h"

#include "ClassifierTrainer.h"
#include <QtConcurrent>
#include <QMetaObject>
#include <QProgressBar>
#include <QPointer>
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
    m_progress = new QProgressBar();
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    btns->addWidget(m_progress);
    m_cancelBtn = new QPushButton(QStringLiteral("取消"));
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_cancel.store(true);
        m_cancelBtn->setEnabled(false);
        m_log->appendPlainText(QStringLiteral("正在取消…（当前样本处理完即停）"));
    });
    btns->addWidget(m_cancelBtn);
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
            // 回调返回成败（2026-10-27 P2-3）：画布锁定时插入会失败，原先照样写"已插入"，
            // 现场以为节点进图了——现在失败明确写红
            if (insertNode(QStringLiteral("OpencvClassifyNode"), params)) {
                m_log->appendPlainText(QStringLiteral("已插入分类节点并预填模型路径：%1").arg(modelPath()));
            } else {
                m_log->appendHtml(QStringLiteral("<span style='color:#c22'>插入失败：画布已锁定或无可用流程页——模型未进图</span>"));
            }
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

    m_watcher = new QFutureWatcher<void>(this);
    connect(m_watcher, &QFutureWatcher<void>::finished, this,
            [this]() { setRunning(false); });
    refreshDataset();
}

void TrainingWizardDialog::setRunning(bool on)
{
    m_running = on;
    m_cancelBtn->setVisible(on);
    m_cancelBtn->setEnabled(on);
    m_progress->setVisible(on);
    if (on)
        m_cancel.store(false);
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
    m_log->appendPlainText(QStringLiteral("开始训练：%1 …（后台运行，可取消）").arg(modelPath()));
    ClsTrainer::AugmentOptions aug;
    aug.enabled = m_augEnable->isChecked();
    aug.factor = m_augFactor->value();
    aug.rotateDeg = m_augRotate->value();
    aug.noiseSigma = m_augNoise->value();
    // 验证比例同时作用于**训练留出**（2026-10-27 P2-2）：训练不得见验证样本
    setRunning(true);
    const int featMode = m_featureMode->currentIndex();
    const int hidden = m_hidden->value();
    const int iters = m_iters->value();
    const double valRatio = m_valRatio->value();
    QPointer<TrainingWizardDialog> self(this);
    m_watcher->setFuture(QtConcurrent::run([this, self, dir, savePath, featMode, hidden,
                                            iters, aug, valRatio]() {
        const ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
            dir.toStdString(), savePath, featMode, hidden, iters, aug,
            valRatio, 42,
            [self](int done, int) {
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, done]() {
                    if (self) self->m_progress->setFormat(
                        QStringLiteral("已处理 %1 个样本…").arg(done));
                }, Qt::QueuedConnection);
            },
            [this]() { return m_cancel.load(); });
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, st, savePath]() {
            if (!self) return;
            if (st.ok) {
                self->m_log->appendPlainText(
                    QStringLiteral("训练完成：%1 个样本 / %2 类 / 特征 %3 维\n模型已保存：%4")
                        .arg(st.samples).arg(st.classes).arg(st.featDim)
                        .arg(QString::fromStdString(savePath)));
                self->m_log->appendPlainText(
                    QStringLiteral("下一步：点「评估」看模型质量；发布时把模型路径填到分类节点。"));
            } else {
                self->m_log->appendPlainText(
                    QStringLiteral("训练失败：%1").arg(QString::fromStdString(st.error)));
            }
        }, Qt::QueuedConnection);
    }));
    m_log->appendPlainText(
        QStringLiteral("提示：classes.txt 与数据集目录同级——同一目录放多个模型会共用类名表，"
                       "换模型请分目录或重新训练。"));
}

void TrainingWizardDialog::onEvaluate()
{
    const QString mp = modelPath();
    if (!QFile::exists(mp)) {
        QMessageBox::warning(this, QStringLiteral("失败"),
                             QStringLiteral("模型文件不存在，请先训练：%1").arg(mp));
        return;
    }
    // 评估用模型自带特征模式（2026-10-27 P3-2）：下拉在评估时禁用，避免误以为生效
    m_featureMode->setEnabled(false);
    setRunning(true);
    const int featMode = m_featureMode->currentIndex();
    const double valRatio = m_valRatio->value();
    const QString ds = datasetDir();
    QPointer<TrainingWizardDialog> self(this);
    m_watcher->setFuture(QtConcurrent::run([this, self, mp, ds, featMode, valRatio]() {
        const ClsTrainer::EvalResult ev = ClsTrainer::evaluateFromDir(
            mp.toStdString(), ds.toStdString(), featMode, valRatio, 42,
            [self](int done, int) {
                if (!self) return;
                QMetaObject::invokeMethod(self, [self, done]() {
                    if (self) self->m_progress->setFormat(
                        QStringLiteral("已评估 %1 张…").arg(done));
                }, Qt::QueuedConnection);
            },
            [this]() { return m_cancel.load(); });
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, ev]() {
            if (!self) return;
            self->m_featureMode->setEnabled(true);
            if (!ev.ok) {
                self->m_log->appendPlainText(
                    QStringLiteral("评估失败：%1").arg(QString::fromStdString(ev.error)));
                self->m_summary->setText(QStringLiteral("评估失败"));
                return;
            }
            self->m_log->appendPlainText(
                QStringLiteral("评估：%1/%2 正确（%3%）")
                    .arg(ev.correct).arg(ev.total).arg(ev.accuracy * 100.0, 0, 'f', 1));
            self->m_summary->setText(
                QStringLiteral("留出验证集 %1 张（未参与训练），正确 %2，准确率 %3%")
                    .arg(ev.total).arg(ev.correct).arg(ev.accuracy * 100.0, 0, 'f', 1));
            const int n = int(ev.classNames.size());
            self->m_confusion->clear();
            self->m_confusion->setRowCount(n);
            self->m_confusion->setColumnCount(n + 1);
            QStringList headers{QStringLiteral("真\\预")};
            for (const auto &c : ev.classNames)
                headers << QString::fromStdString(c);
            self->m_confusion->setHorizontalHeaderLabels(headers);
            for (int i = 0; i < n; ++i) {
                self->m_confusion->setItem(i, 0,
                    new QTableWidgetItem(QString::fromStdString(ev.classNames[size_t(i)])));
                for (int j = 0; j < n; ++j)
                    self->m_confusion->setItem(i, j + 1,
                        new QTableWidgetItem(QString::number(ev.confusion[size_t(i)][size_t(j)])));
            }
        }, Qt::QueuedConnection);
    }));
}

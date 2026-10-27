#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QDoubleSpinBox;
class QTableWidget;
class QLabel;

/// 训练向导（2026-10-27）：数据集装配 → 训练 → 评估 → 发布，一条链走完。
/// 对标 VM 的"训练工具"；此前只有 OpencvTrainClassifierNode 的参数面板，
/// 现场看不到数据集构成、训练结果与模型质量，出错只能翻日志。
class TrainingWizardDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TrainingWizardDialog(QWidget *parent = nullptr);

private slots:
    void refreshDataset();
    void onTrain();
    void onEvaluate();

private:
    QString datasetDir() const;
    QString modelPath() const;

    QLineEdit *m_dirEdit = nullptr;
    QTableWidget *m_classTable = nullptr;
    QComboBox *m_featureMode = nullptr;
    QSpinBox *m_hidden = nullptr;
    QSpinBox *m_iters = nullptr;
    QDoubleSpinBox *m_valRatio = nullptr;
    QLineEdit *m_modelName = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTableWidget *m_confusion = nullptr;
    QLabel *m_summary = nullptr;
};

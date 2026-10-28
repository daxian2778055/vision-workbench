#pragma once

#include <QDialog>
#include <QFutureWatcher>
#include <atomic>
#include <memory>
#include <QJsonObject>
#include <functional>

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

    /// 一键发布为算子（2026-10-27）：由主窗口注入"在画布插入预设节点"的能力，
    /// 对话框不直接依赖 FlowScene/MainWindow（保持可单测、低耦合）。
    std::function<bool(const QString &nodeType, const QJsonObject &params)> insertNode;

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
    class QCheckBox *m_augEnable = nullptr;
    QSpinBox *m_augFactor = nullptr;
    QDoubleSpinBox *m_augRotate = nullptr;
    QDoubleSpinBox *m_augNoise = nullptr;
    class QPushButton *m_trainBtn = nullptr;
    class QPushButton *m_evalBtn = nullptr;
    class QPushButton *m_publishBtn = nullptr;
    class QPushButton *m_closeBtn = nullptr;
    class QProgressBar *m_progress = nullptr;
    class QPushButton *m_cancelBtn = nullptr;
    QFutureWatcher<void> *m_watcher = nullptr;
    /// 取消标志用 shared_ptr 按值捕获（2026-10-28 P1）：worker 的 shouldCancel 若捕获
    /// this，训练中点关闭/标题栏 X 时对话框析构 → worker 读已释放成员 = UB。
    std::shared_ptr<std::atomic<bool>> m_cancel =
        std::make_shared<std::atomic<bool>>(false);
    bool m_running = false;

    void setRunning(bool on);
    /// 关窗前先让工作线程收尾（2026-10-28 P1）：训练/评估中点 X 不能直接析构
    void closeEvent(QCloseEvent *e) override;
    /// Esc 走 reject()（不经过 closeEvent）：必须与关窗同一套策略（2026-10-28 复核）
    void reject() override;
    void statusBarHint();   /// 轻量提示：不阻塞、不弹模态
    QLineEdit *m_modelName = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QTableWidget *m_confusion = nullptr;
    QLabel *m_summary = nullptr;
};

#pragma once

#include "TrainingRunner.h"

#include <QDialog>
#include <QCloseEvent>

class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QLabel;

/// 训练编排对话框（2026-10-29）
///
/// 只把"环境已装好的训练机"变得好用：检测环境 → 拼命令 → 跑 → 看日志 → 取消。
/// 产线机没装环境时，这里会把"开始"禁用并说清缺什么，而不是让人点出一个莫名其妙的失败。
class TrainingRunnerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TrainingRunnerDialog(QWidget *parent = nullptr);

protected:
    /// 训练中关窗不得静默杀进程（2026-10-28 复核 P3）：跑了几小时的训练
    /// 会被顺手关窗带走——与训练向导同一策略：请求取消 + 拒绝关闭。
    void closeEvent(QCloseEvent *e) override;
    void accept() override;
    void reject() override;

private slots:
    void onProbeEnv();
    void onBrowsePython();
    void onBrowseYolox();
    void onBrowseExp();
    void onBrowseCkpt();
    void onStart();
    void onCancel();
    void refreshCommand();

private:
    QString buildProgram() const;
    QStringList buildArgs() const;
    void appendLog(const QString &line);
    void setRunning(bool on);
    void loadSettings();
    void saveSettings();

    QComboBox *m_trainer = nullptr;      /// 0=YOLOX 1=Ultralytics
    QLineEdit *m_python = nullptr;
    QLineEdit *m_yoloxDir = nullptr;
    QLineEdit *m_expOrWeights = nullptr;
    QLineEdit *m_dataYaml = nullptr;
    QLineEdit *m_ckpt = nullptr;
    QLineEdit *m_extra = nullptr;
    QSpinBox *m_epochs = nullptr;
    QSpinBox *m_imgsz = nullptr;
    QSpinBox *m_batch = nullptr;
    QComboBox *m_device = nullptr;
    QLabel *m_envLabel = nullptr;
    QPlainTextEdit *m_commandPreview = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_probeBtn = nullptr;
    TrainingRunner *m_runner = nullptr;
    bool m_envOk = false;
    /// 用户点了关闭/Esc：收尾后自动关窗（2026-10-29，与训练向导同策略）
    bool m_closeRequested = false;
};

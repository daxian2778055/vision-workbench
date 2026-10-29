#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

/// 训练编排（2026-10-29）：把"外部训练环境"接进界面，但**不打包环境本身**。
///
/// 为什么这样切：
///   · 训练环境（Python + PyTorch + YOLOX）约 3 GB、还要匹配 Python 版本与 GPU，装到每台
///     产线机是浪费与风险；产线机只需要我们的主程序；
///   · 但"敲命令行"对现场不友好，所以把环境检测、启动、日志、取消做进界面——
///     训练机上能用，产线机上自动禁用并说明原因。
class TrainingRunner : public QObject
{
    Q_OBJECT
public:
    struct EnvStatus {
        bool ok = false;
        QString pythonPath;
        QString detail;      /// 成功时是 "torch 2.4.1 | cuda False"；失败时是可读原因
    };

    explicit TrainingRunner(QObject *parent = nullptr);
    ~TrainingRunner() override;

    /// 探测训练环境（python 能否 import torch）。**同步**执行，最多等 timeoutMs。
    static EnvStatus probe(const QString &pythonPath, int timeoutMs = 20000);

    /// 用命令模板启动训练。模板里的占位符先由调用方替换好，这里只负责跑。
    void start(const QString &program, const QStringList &args, const QString &workDir);
    void cancel();
    bool running() const;

signals:
    void logLine(const QString &line);
    /// ok=true 表示进程正常退出（是否"训练成功"仍要看日志，这里不猜）
    void finished(bool ok, const QString &message);

private:
    QProcess *m_proc = nullptr;
    bool m_cancelRequested = false;
};

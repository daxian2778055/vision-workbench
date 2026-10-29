#include "TrainingRunner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>

TrainingRunner::TrainingRunner(QObject *parent) : QObject(parent) {}

TrainingRunner::~TrainingRunner()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
}

TrainingRunner::EnvStatus TrainingRunner::probe(const QString &pythonPath, int timeoutMs)
{
    EnvStatus st;
    st.pythonPath = pythonPath;
    if (pythonPath.trimmed().isEmpty()) {
        st.detail = QStringLiteral("未配置 Python 路径");
        return st;
    }
    // 允许只写名字（"python"）：PATH 里的也要能找到（2026-10-28 复核 P3）
    QString resolved = pythonPath;
    if (!QFileInfo::exists(resolved))
        resolved = QStandardPaths::findExecutable(pythonPath);
    if (resolved.isEmpty()) {
        st.detail = QStringLiteral("Python 不存在：%1").arg(pythonPath);
        return st;
    }

    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    // 只查最关键的两件事：torch 在不在、CUDA 有没有。YOLOX 目录由界面单独校验。
    proc.start(resolved, {QStringLiteral("-c"),
                            QStringLiteral("import torch,sys;"
                                           "print(torch.__version__, '| cuda', torch.cuda.is_available())")});
    if (!proc.waitForStarted(5000)) {
        st.detail = QStringLiteral("无法启动 Python（路径是否正确？）");
        return st;
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        st.detail = QStringLiteral("探测超时（%1 ms）：Python 启动太慢或卡住").arg(timeoutMs);
        return st;
    }
    const QString out = QString::fromLocal8Bit(proc.readAll()).trimmed();
    if (proc.exitCode() != 0) {
        st.detail = QStringLiteral("import torch 失败：%1")
                        .arg(out.isEmpty() ? QStringLiteral("（无输出）") : out.left(300));
        return st;
    }
    st.ok = true;
    st.detail = out;
    return st;
}

bool TrainingRunner::running() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

void TrainingRunner::start(const QString &program, const QStringList &args, const QString &workDir)
{
    if (running())
        return;
    m_cancelRequested = false;
    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    if (!workDir.isEmpty())
        m_proc->setWorkingDirectory(workDir);

    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString chunk = QString::fromLocal8Bit(m_proc->readAll());
        for (const QString &line : chunk.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty())
                emit logLine(line.trimmed());
    });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, QStringLiteral("启动失败：%1").arg(m_proc->errorString()));
    });
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus status) {
                const bool ok = (status == QProcess::NormalExit && code == 0);
                const QString msg = m_cancelRequested
                                        ? QStringLiteral("已取消（进程已终止）")
                                        : QStringLiteral("进程结束：exit=%1").arg(code);
                emit finished(ok && !m_cancelRequested, msg);
            });

    emit logLine(QStringLiteral("> %1 %2").arg(program, args.join(QLatin1Char(' '))));
    m_proc->start(program, args);
}

void TrainingRunner::cancel()
{
    if (!running())
        return;
    m_cancelRequested = true;
    // 先温和后强杀：YOLOX 收到 terminate 有机会写完最后一个 checkpoint
    m_proc->terminate();
    if (!m_proc->waitForFinished(5000))
        m_proc->kill();
}

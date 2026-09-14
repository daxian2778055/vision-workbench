#include "ScriptNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include "ScriptSecurityPolicy.h"
#include "FlowExecutor.h"
#include <QWidget>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryFile>
#include <QTextStream>
#include <QSignalBlocker>
#include <QCheckBox>

ScriptNode::ScriptNode(QObject *parent)
    : HalconNode(parent)
{
    m_type = OUTPUT;
}

void ScriptNode::init()
{
    HalconNode::init();
    m_params[QStringLiteral("language")] = QStringLiteral("Python");
    m_params[QStringLiteral("scriptContent")] = QStringLiteral(
        "# \u8F93\u5165\u53D8\u91CF: inputImage (\u56FE\u50CF), inputValue (\u6570\u503C)\n"
        "# \u8F93\u51FA\u53D8\u91CF: result\n"
        "result = \"Hello from Python\"\n"
    );
    m_params[QStringLiteral("scriptArgs")] = QStringList();
    m_params[QStringLiteral("lastOutput")] = QString();
}

void ScriptNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("lastOutput")] = QString();

    QString script = m_params.value(QStringLiteral("scriptContent")).toString();
    if (script.isEmpty()) {
        m_params[QStringLiteral("lastOutput")] = QStringLiteral("\u811A\u672C\u5185\u5BB9\u4E3A\u7A7A");
        m_outputImage = m_inputImage;
        return;
    }

    QStringList args = m_params.value(QStringLiteral("scriptArgs")).toStringList();
    QString lang = m_params.value(QStringLiteral("language")).toString();

    {
        ScriptSecurityPolicy &policy = ScriptSecurityPolicy::instance();
        QString denyReason;
        if (!policy.evaluate(lang, script, denyReason)) {
            policy.audit(lang, script, false, denyReason);
            m_params[QStringLiteral("lastOutput")] = denyReason;
            m_outputImage = m_inputImage;
            return;
        }
        if (!policy.requestConfirmation(lang, script)) {
            policy.audit(lang, script, false, QStringLiteral("用户取消了脚本执行"));
            m_params[QStringLiteral("lastOutput")] = QStringLiteral("用户取消了脚本执行");
            m_outputImage = m_inputImage;
            return;
        }
        policy.audit(lang, script, true, QString());
    }

    QString output;
    if (lang == QStringLiteral("Python")) {
        output = executePythonScript(script, args);
    } else if (lang == QStringLiteral("Lua")) {
        output = executeLuaScript(script, args);
    } else {
        output = QStringLiteral("\u4E0D\u652F\u6301\u7684\u811A\u672C\u8BED\u8A00: %1").arg(lang);
    }

    m_params[QStringLiteral("lastOutput")] = output;
    m_outputImage = m_inputImage; // Pass through image

    if (m_outputImage.IsInitialized()) {
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(0, outObj);
    }
}

bool ScriptNode::waitCancellable(QProcess &process, int maxMs, QString &errOut)
{
    QElapsedTimer timer;
    timer.start();
    while (true) {
        const int remain = maxMs - timer.elapsed();
        const int chunk = remain > 0 ? qMin(remain, 200) : 100;
        if (process.waitForFinished(chunk))
            return true;
        if (timer.elapsed() >= maxMs) {
            process.kill();
            ScriptSecurityPolicy::instance().closeJob(&process);
            errOut = QStringLiteral("脚本执行超时");
            return false;
        }
        FlowExecutor *exec = ownerExecutor();
        if (exec && exec->getState() == ExecutionState::Stopped) {
            process.kill();
            ScriptSecurityPolicy::instance().closeJob(&process);
            errOut = QStringLiteral("脚本执行被取消");
            return false;
        }
    }
}

QString ScriptNode::executePythonScript(const QString &script, const QStringList &args)
{
    QTemporaryFile tmpFile;
    if (!tmpFile.open()) {
        return QStringLiteral("\u65E0\u6CD5\u521B\u5EFA\u4E34\u65F6\u811A\u672C\u6587\u4EF6");
    }

    {
        // QTextStream 自带缓冲区：必须先 flush（或让流析构）再启动解释器，
        // 否则解释器读到的是空脚本文件（历史缺陷：脚本静默不执行）
        QTextStream out(&tmpFile);
        out << script;
        out.flush();
    }
    tmpFile.flush();

    QProcess process;
    QStringList processArgs;
    processArgs << ScriptSecurityPolicy::instance().interpreterFlags(QStringLiteral("Python"));
    processArgs << tmpFile.fileName();
    processArgs << args;

    auto &policy = ScriptSecurityPolicy::instance();
    process.setProcessEnvironment(policy.buildEnvironment());
    policy.applyProcessSandbox(&process);
    process.start(QStringLiteral("python"), processArgs);
    if (!process.waitForStarted(5000)) {
        return QStringLiteral("Python \u672A\u627E\u5230\u6216\u65E0\u6CD5\u542F\u52A0");
    }

    // 沙箱开启时，Job Object 建立/加入失败必须拒绝执行，否则脚本将以完整用户权限运行（P1）
    if (policy.isSandboxEnabled() && !policy.attachJob(&process)) {
        process.kill();
        process.waitForFinished(2000);
        policy.audit(QStringLiteral("Python"), script, false,
                     QStringLiteral("进程沙箱不可用（Job Object 建立失败）"));
        return QStringLiteral("进程沙箱不可用，已拒绝执行脚本");
    }

    QString waitErr;
    if (!waitCancellable(process, policy.maxExecutionMs(), waitErr)) {
        return waitErr;
    }
    policy.closeJob(&process);

    QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());

    if (!stdErr.isEmpty()) {
        return QStringLiteral("STDERR: %1\nSTDOUT: %2").arg(stdErr.trimmed(), stdOut.trimmed());
    }

    return stdOut.trimmed();
}

QString ScriptNode::executeLuaScript(const QString &script, const QStringList &args)
{
    QTemporaryFile tmpFile;
    if (!tmpFile.open()) {
        return QStringLiteral("\u65E0\u6CD5\u521B\u5EFA\u4E34\u65F6\u811A\u672C\u6587\u4EF6");
    }

    {
        // QTextStream 自带缓冲区：必须先 flush（或让流析构）再启动解释器，
        // 否则解释器读到的是空脚本文件（历史缺陷：脚本静默不执行）
        QTextStream out(&tmpFile);
        out << script;
        out.flush();
    }
    tmpFile.flush();

    QProcess process;
    QStringList processArgs;
    processArgs << tmpFile.fileName();
    processArgs << args;

    auto &policy = ScriptSecurityPolicy::instance();
    process.setProcessEnvironment(policy.buildEnvironment());
    policy.applyProcessSandbox(&process);
    process.start(QStringLiteral("lua"), processArgs);
    if (!process.waitForStarted(5000)) {
        return QStringLiteral("Lua \u672A\u627E\u5230\u6216\u65E0\u6CD5\u542F\u52A8");
    }

    // 沙箱开启时，Job Object 建立/加入失败必须拒绝执行，否则脚本将以完整用户权限运行（P1）
    if (policy.isSandboxEnabled() && !policy.attachJob(&process)) {
        process.kill();
        process.waitForFinished(2000);
        policy.audit(QStringLiteral("Lua"), script, false,
                     QStringLiteral("进程沙箱不可用（Job Object 建立失败）"));
        return QStringLiteral("进程沙箱不可用，已拒绝执行脚本");
    }

    QString waitErr;
    if (!waitCancellable(process, policy.maxExecutionMs(), waitErr)) {
        return waitErr;
    }
    policy.closeJob(&process);

    QString stdOut = QString::fromLocal8Bit(process.readAllStandardOutput());
    QString stdErr = QString::fromLocal8Bit(process.readAllStandardError());

    if (!stdErr.isEmpty()) {
        return QStringLiteral("STDERR: %1\nSTDOUT: %2").arg(stdErr.trimmed(), stdOut.trimmed());
    }

    return stdOut.trimmed();
}

QWidget *ScriptNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->setSpacing(8);

    layout->addWidget(new QLabel(QStringLiteral("<b>\u811A\u672C\u6267\u884C</b>")));

    auto *warnLabel = new QLabel(QStringLiteral("⚠ 脚本以当前用户权限运行，存在任意代码执行风险，仅运行可信脚本。"));
    warnLabel->setWordWrap(true);
    warnLabel->setStyleSheet(QStringLiteral("QLabel { color: #b00020; font-size: 11px; }"));
    layout->addWidget(warnLabel);

    auto *sandboxChk = new QCheckBox(QStringLiteral("进程隔离（Job Object 限制桌面/剪贴板/内存，父退出即终止脚本进程）"));
    sandboxChk->setToolTip(QStringLiteral("默认开启。脚本子进程关入 Job Object 并限制 UI/内存，父进程退出时整棵进程树被终止；"
                                          "同时保留解释器隔离（Python -I -E）与环境变量清理。"
                                          "注意：当前 Qt 版本无法施加受限令牌，不含降权/低完整性级别。"));
    sandboxChk->setChecked(ScriptSecurityPolicy::instance().isSandboxEnabled());
    connect(sandboxChk, &QCheckBox::toggled, this, [](bool on) {
        ScriptSecurityPolicy::instance().setSandboxEnabled(on);
    });
    layout->addWidget(sandboxChk);

    // Language selection
    auto *langCombo = new QComboBox();
    langCombo->setObjectName(QStringLiteral("scriptLanguage"));
    langCombo->addItem(QStringLiteral("Python"));
    langCombo->addItem(QStringLiteral("Lua"));
    langCombo->setCurrentText(m_params.value(QStringLiteral("language")).toString());
    connect(langCombo, &QComboBox::currentTextChanged, this, [this](const QString &lang) {
        setParam(QStringLiteral("language"), lang);
    });
    layout->addWidget(new QLabel(QStringLiteral("\u8BED\u8A00:")));
    layout->addWidget(langCombo);

    // Script editor
    auto *scriptEdit = new QPlainTextEdit();
    scriptEdit->setObjectName(QStringLiteral("scriptContent"));
    scriptEdit->setPlaceholderText(QStringLiteral("\u8F93\u5165\u811A\u672C\u5185\u5BB9..."));
    scriptEdit->setPlainText(m_params.value(QStringLiteral("scriptContent")).toString());
    scriptEdit->setMinimumHeight(200);
    scriptEdit->setStyleSheet(
        "QPlainTextEdit { font-family: 'Consolas', 'Courier New', monospace; font-size: 12px; }"
    );
    connect(scriptEdit, &QPlainTextEdit::textChanged, this, [this, scriptEdit]() {
        setParam(QStringLiteral("scriptContent"), scriptEdit->toPlainText());
    });
    layout->addWidget(new QLabel(QStringLiteral("\u811A\u672C\u5185\u5BB9:")));
    layout->addWidget(scriptEdit);

    // Last output display
    auto *outputEdit = new QPlainTextEdit();
    outputEdit->setObjectName(QStringLiteral("scriptOutput"));
    outputEdit->setReadOnly(true);
    outputEdit->setMaximumHeight(80);
    outputEdit->setPlaceholderText(QStringLiteral("\u811A\u672C\u8F93\u51FA\u5C06\u663E\u793A\u5728\u8FD9\u91CC"));
    outputEdit->setPlainText(m_params.value(QStringLiteral("lastOutput")).toString());
    layout->addWidget(new QLabel(QStringLiteral("\u8F93\u51FA:")));
    layout->addWidget(outputEdit);

    layout->addStretch();
    return panel;
}

void ScriptNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;

    if (auto *w = panel->findChild<QComboBox *>(QStringLiteral("scriptLanguage"))) {
        QSignalBlocker b(w);
        w->setCurrentText(m_params.value(QStringLiteral("language")).toString());
    }
    if (auto *w = panel->findChild<QPlainTextEdit *>(QStringLiteral("scriptContent"))) {
        QSignalBlocker b(w);
        w->setPlainText(m_params.value(QStringLiteral("scriptContent")).toString());
    }
    if (auto *w = panel->findChild<QPlainTextEdit *>(QStringLiteral("scriptOutput"))) {
        QSignalBlocker b(w);
        w->setPlainText(m_params.value(QStringLiteral("lastOutput")).toString());
    }
}

QJsonObject ScriptNode::toJson() const
{
    QJsonObject json = HalconNode::toJson();
    json[QStringLiteral("language")] = m_params.value(QStringLiteral("language")).toString();
    json[QStringLiteral("scriptContent")] = m_params.value(QStringLiteral("scriptContent")).toString();
    QJsonArray args;
    for (const QString &arg : m_params.value(QStringLiteral("scriptArgs")).toStringList()) {
        args.append(arg);
    }
    json[QStringLiteral("scriptArgs")] = args;
    return json;
}

void ScriptNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    setParam(QStringLiteral("language"), json[QStringLiteral("language")].toString());
    setParam(QStringLiteral("scriptContent"), json[QStringLiteral("scriptContent")].toString());
    QStringList args;
    for (const QJsonValue &v : json[QStringLiteral("scriptArgs")].toArray()) {
        args.append(v.toString());
    }
    setParam(QStringLiteral("scriptArgs"), args);
}

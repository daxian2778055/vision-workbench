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
#include <QMessageBox>

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
    setParamDirect(QStringLiteral("moduleStatus"), false);
    m_lastRunFailed = false;

    QString script = m_params.value(QStringLiteral("scriptContent")).toString();
    if (script.isEmpty()) {
        m_params[QStringLiteral("lastOutput")] = QStringLiteral("\u811A\u672C\u5185\u5BB9\u4E3A\u7A7A");
        m_lastRunFailed = true;
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
            m_lastRunFailed = true;
            m_outputImage = m_inputImage;
            return;
        }
        if (!policy.requestConfirmation(lang, script)) {
            policy.audit(lang, script, false, QStringLiteral("用户取消了脚本执行"));
            m_params[QStringLiteral("lastOutput")] = QStringLiteral("用户取消了脚本执行");
            m_lastRunFailed = true;
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
        m_lastRunFailed = true;
    }

    m_params[QStringLiteral("lastOutput")] = output;
    m_outputImage = m_inputImage; // Pass through image

    if (m_outputImage.IsInitialized()) {
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(0, outObj);
    }

    // 只有解释器正常执行完才算成功。此前 run() 从不置位 moduleStatus，
    // 导致本节点恒报失败，默认的“失败时停止”会把含脚本节点的流程误停（P1）
    setParamDirect(QStringLiteral("moduleStatus"), !m_lastRunFailed);
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
        m_lastRunFailed = true;
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

    auto &policy = ScriptSecurityPolicy::instance();

    // 解释器必须是可解析的**绝对路径**：解析不到直接 fail-closed。
    // （历史实现把裸名 "python" 交给启动接口——Windows 搜索序先看应用目录/当前目录，
    // 同目录放一个同名 exe 即可顶替解释器：确认过的可信脚本跑的是攻击者的程序。）
    const QString interpreter = policy.interpreterPath(QStringLiteral("Python"));
    if (interpreter.isEmpty()) {
        policy.audit(QStringLiteral("Python"), script, false,
                     QStringLiteral("解释器未解析到绝对路径（未配置全路径且 PATH 中找不到）"));
        m_lastRunFailed = true;
        return QStringLiteral("未找到 Python 解释器：请在「脚本安全」中配置 python.exe 全路径（已拒绝执行）");
    }

    // 沙箱开启：以「受限令牌」启动解释器（去特权）。Qt 6.11 起 QProcess 无法
    // 施加令牌，故走 CreateProcessAsUserW 路径；建立失败即拒绝执行（fail-closed）。
    if (policy.isSandboxEnabled()) {
        QStringList restrictedArgs = policy.interpreterFlags(QStringLiteral("Python"));
        restrictedArgs << tmpFile.fileName();
        restrictedArgs << args;
        QString stdOut;
        QString stdErr;
        QString runError;
        int exitCode = -1;
        const bool ok = policy.runWithRestrictedToken(
            interpreter, restrictedArgs, policy.maxExecutionMs(),
            [this]() {
                FlowExecutor *exec = ownerExecutor();
                return exec && exec->getState() == ExecutionState::Stopped;
            },
            stdOut, stdErr, runError, &exitCode);
        if (!ok) {
            m_lastRunFailed = true;
            return runError;
        }
        if (exitCode != 0) {
            m_lastRunFailed = true;
        }
        if (!stdErr.isEmpty()) {
            return QStringLiteral("STDERR: %1\nSTDOUT: %2").arg(stdErr.trimmed(), stdOut.trimmed());
        }
        return stdOut.trimmed();
    }

    QProcess process;
    QStringList processArgs;
    processArgs << policy.interpreterFlags(QStringLiteral("Python"));
    processArgs << tmpFile.fileName();
    processArgs << args;

    process.setProcessEnvironment(policy.buildEnvironment());
    policy.applyProcessSandbox(&process);
    process.start(interpreter, processArgs);
    if (!process.waitForStarted(5000)) {
        m_lastRunFailed = true;
        return QStringLiteral("Python 启动失败（解释器 %1 无法执行：检查权限/杀软拦截）").arg(interpreter);
    }

    // 注意：本路径只在沙箱**关闭**时到达——沙箱开启时上面已在受限令牌路径 return
    // （那条路径自身 fail-closed：令牌/Job 建立失败即拒绝执行）。历史上这里还留着
    // 一段 "isSandboxEnabled() && !attachJob → 拒绝执行"，因条件恒假而是**死代码**，
    // 却让审查者误以为 QProcess 路径也具备 fail-closed 语义。这里如实改为 best-effort：
    // 附加 Job 约束成功是加分项，失败仅记录（沙箱本就关闭，脚本以当前用户权限运行）。
    if (!policy.attachJob(&process)) {
        qWarning() << "Job Object 附加失败（沙箱关闭，best-effort）：脚本以当前用户权限运行";
    }

    QString waitErr;
    if (!waitCancellable(process, policy.maxExecutionMs(), waitErr)) {
        m_lastRunFailed = true;
        return waitErr;
    }
    policy.closeJob(&process);

    // 解释器非正常退出（语法错误/被终止等）视为执行失败
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        m_lastRunFailed = true;
    }

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
        m_lastRunFailed = true;
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

    auto &policy = ScriptSecurityPolicy::instance();

    // 解释器必须是可解析的**绝对路径**：解析不到直接 fail-closed（同 Python，见上方注释）
    const QString interpreter = policy.interpreterPath(QStringLiteral("Lua"));
    if (interpreter.isEmpty()) {
        policy.audit(QStringLiteral("Lua"), script, false,
                     QStringLiteral("解释器未解析到绝对路径（未配置全路径且 PATH 中找不到）"));
        m_lastRunFailed = true;
        return QStringLiteral("未找到 Lua 解释器：请在「脚本安全」中配置 lua.exe 全路径（已拒绝执行）");
    }

    // 沙箱开启：同 Python，以「受限令牌」启动 Lua 解释器；失败即拒绝执行（fail-closed）
    if (policy.isSandboxEnabled()) {
        QStringList restrictedArgs = policy.interpreterFlags(QStringLiteral("Lua"));
        restrictedArgs << tmpFile.fileName();
        restrictedArgs << args;
        QString stdOut;
        QString stdErr;
        QString runError;
        int exitCode = -1;
        const bool ok = policy.runWithRestrictedToken(
            interpreter, restrictedArgs, policy.maxExecutionMs(),
            [this]() {
                FlowExecutor *exec = ownerExecutor();
                return exec && exec->getState() == ExecutionState::Stopped;
            },
            stdOut, stdErr, runError, &exitCode);
        if (!ok) {
            m_lastRunFailed = true;
            return runError;
        }
        if (exitCode != 0) {
            m_lastRunFailed = true;
        }
        if (!stdErr.isEmpty()) {
            return QStringLiteral("STDERR: %1\nSTDOUT: %2").arg(stdErr.trimmed(), stdOut.trimmed());
        }
        return stdOut.trimmed();
    }

    QProcess process;
    QStringList processArgs;
    processArgs << tmpFile.fileName();
    processArgs << args;

    process.setProcessEnvironment(policy.buildEnvironment());
    policy.applyProcessSandbox(&process);
    process.start(interpreter, processArgs);
    if (!process.waitForStarted(5000)) {
        m_lastRunFailed = true;
        return QStringLiteral("Lua 启动失败（解释器 %1 无法执行：检查权限/杀软拦截）").arg(interpreter);
    }

    // 同 Python：本路径只在沙箱关闭时到达，Job 附加为 best-effort（详见上方注释）
    if (!policy.attachJob(&process)) {
        qWarning() << "Job Object 附加失败（沙箱关闭，best-effort）：脚本以当前用户权限运行";
    }

    QString waitErr;
    if (!waitCancellable(process, policy.maxExecutionMs(), waitErr)) {
        m_lastRunFailed = true;
        return waitErr;
    }
    policy.closeJob(&process);

    // 解释器非正常退出（语法错误/被终止等）视为执行失败
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        m_lastRunFailed = true;
    }

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
    sandboxChk->setToolTip(QStringLiteral("Windows 默认开启（受限令牌 / 低完整性 / Job Object 仅有 Windows 实现，"
                                          "非 Windows 平台默认关闭，否则脚本会因沙箱不可用被拒绝执行）。"
                                          "子进程以受限令牌启动（去除全部特权 + 低完整性 Low IL）：**无法写入用户目录**，"
                                          "临时文件只能写在本次执行专属的沙箱目录（执行结束即删除）；"
                                          "进程关入 Job Object 限制 UI/内存，父进程退出时整棵进程树被终止；"
                                          "同时保留解释器隔离（Python -I -E）与环境变量清理。"
                                          "未施加：管理员组 deny-only（实测任何子进程都会 0xC0000142，"
                                          "已定位为 Windows 自身 KERNELBASE 附加失败）。"
                                          "需要独立文件视图与网络隔离请改用下方「AppContainer 容器」。"));
    sandboxChk->setChecked(ScriptSecurityPolicy::instance().isSandboxEnabled());
    connect(sandboxChk, &QCheckBox::toggled, this, [](bool on) {
        ScriptSecurityPolicy::instance().setSandboxEnabled(on);
    });
    layout->addWidget(sandboxChk);

    // 沙箱强度：决定子进程以「受限令牌」还是「AppContainer 容器」启动。
    auto *modeCombo = new QComboBox();
    modeCombo->setObjectName(QStringLiteral("scriptSandboxMode"));
    modeCombo->addItem(QStringLiteral("去特权 + 低完整性（默认）"));
    modeCombo->addItem(QStringLiteral("AppContainer 容器（独立文件视图、无网络）"));
    modeCombo->setCurrentIndex(ScriptSecurityPolicy::instance().sandboxMode()
                                       == ScriptSecurityPolicy::SandboxMode::AppContainer
                                   ? 1
                                   : 0);
    modeCombo->setToolTip(QStringLiteral("AppContainer 模式下脚本进程运行在容器内："
                                         "只能访问显式授权的路径与自己的容器目录，**无网络能力**；"
                                         "写用户目录会被拒绝。首次使用需点下方按钮做一次性准备"
                                         "（创建容器配置 + 给解释器目录授权，需要管理员权限）。"
                                         "容器不可用时脚本会被**拒绝执行**（不会静默降级）。"));
    connect(modeCombo, &QComboBox::currentIndexChanged, this, [](int index) {
        auto &policy = ScriptSecurityPolicy::instance();
        policy.setSandboxMode(index == 1 ? ScriptSecurityPolicy::SandboxMode::AppContainer
                                         : ScriptSecurityPolicy::SandboxMode::PrivilegeStripped);
        policy.save();
    });
    layout->addWidget(new QLabel(QStringLiteral("沙箱强度:")));
    layout->addWidget(modeCombo);

    // 一次性准备：创建 AppContainer 配置文件 + 给解释器目录授权（RX）。
    // 做成显式按钮而不是自动执行：这会改动机器状态（profile、解释器目录 ACL），
    // 必须由用户知情触发。
    auto *prepareBtn = new QPushButton(QStringLiteral("准备容器沙箱（一次性，需要管理员）"));
    prepareBtn->setToolTip(QStringLiteral("创建 AppContainer 配置文件，并为解释器所在目录追加「读取+执行」权限"
                                          "（只追加容器 SID 的条目，不改动其它权限）。"
                                          "解释器当前按 PATH 解析的命令名为 python。"));
    connect(prepareBtn, &QPushButton::clicked, this, [prepareBtn]() {
        auto &policy = ScriptSecurityPolicy::instance();
        Q_UNUSED(policy)
        QString error;
        if (!ScriptSecurityPolicy::ensureAppContainerProfile(&error)) {
            QMessageBox::warning(prepareBtn, QStringLiteral("准备容器沙箱失败"), error);
            return;
        }
        if (!ScriptSecurityPolicy::grantInterpreterAccess(policy.interpreterPath(QStringLiteral("Python")), &error)) {
            QMessageBox::warning(prepareBtn, QStringLiteral("准备容器沙箱失败"), error);
            return;
        }
        QMessageBox::information(
            prepareBtn, QStringLiteral("准备完成"),
            QStringLiteral("容器沙箱已就绪：\n  profile SID: %1\n  已为 python 所在目录授予读取+执行权限。\n\n"
                           "现在把「沙箱强度」切到 AppContainer 即可生效。\n\n"
                           "⚠ 注意：该授权会在解释器目录上留下容器 SID 条目，实测会使"
                           "「去特权 + 低完整性」模式下的解释器无法启动（0xC0000135）；"
                           "而只给文件授权又会让容器内解释器起不来（无法遍历目录）。"
                           "即两种强度在同一台机器上**互斥，需二选一**。"
                           "要切回「去特权 + 低完整性」，请先移除该授权：\n"
                           "  icacls \"<解释器目录>\" /reset")
                .arg(ScriptSecurityPolicy::appContainerSid()));
    });
    layout->addWidget(prepareBtn);

    // 解释器路径（全局设置，按语言）：现场多套 Python/venv（系统/虚拟环境/便携版）时显式指定，
    // 避免按 PATH 解析选错版本；留空 = 使用 PATH 中的 python（历史行为）。
    // 注意：容器授权的是"哪个解释器目录"，与这里启动的解释器必须同源——改完路径后
    // 建议重新点一次「准备容器沙箱」。
    auto *interpRow = new QHBoxLayout();
    auto *interpEdit = new QLineEdit();
    interpEdit->setObjectName(QStringLiteral("scriptInterpreterPath"));
    interpEdit->setPlaceholderText(QStringLiteral("留空 = 按 PATH 解析 python（解析不到将拒绝执行）"));
    interpEdit->setText(ScriptSecurityPolicy::instance().interpreterPath(QStringLiteral("Python")));
    interpEdit->setToolTip(QStringLiteral("解释器可执行文件全路径，例如 C:\\Python314\\python.exe。\n"
                                          "留空则按 PATH 解析 python。\n"
                                          "沙箱（含 AppContainer 授权）与启动都使用这里的值；\n"
                                          "改动后请重新点一次「准备容器沙箱」，让授权对象同步。"));
    interpRow->addWidget(interpEdit, 1);
    layout->addWidget(new QLabel(QStringLiteral("解释器路径:")));
    layout->addLayout(interpRow);
    connect(interpEdit, &QLineEdit::editingFinished, this, [interpEdit]() {
        auto &policy = ScriptSecurityPolicy::instance();
        policy.setInterpreterPath(QStringLiteral("Python"), interpEdit->text());
        policy.save();
    });

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

#include "ScriptSecurityPolicy.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

/// ScriptSecurityPolicy 行为验证
///
/// 验证四个关键控制点：
///  1. 语言白名单：仅放行 Python/Lua，未知语言一律拒绝
///  2. 解释器隔离：Python 默认返回 -I -E（忽略环境/用户 site）
///  3. 环境清理：剥离 PYTHONPATH/LD_PRELOAD 等可注入变量，保留 PATH
///  4. 审计日志：允许/拒绝动作写入结构化日志行
///  5. 总开关：disabled 时拦截一切执行
///
/// 注意：本测试只验证"策略决策逻辑"，不启动真实解释器，也不依赖 Python/Lua。
class ScriptSecurityPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void languageWhitelist_blocksUnknown();
    void interpreterFlags_isolation();
    void buildEnvironment_stripsDangerousVars();
    void audit_writesStructuredLog();
    void toggleOff_blocksEverything();
    void sandbox_disabled_isNoOp();
    void sandboxMode_persistsAcrossLoad();
    void interpreterPath_persistsAndFallsBack();
};

void ScriptSecurityPolicyTest::initTestCase()
{
    // 给本测试进程一个**确定且独立**的 QSettings 作用域：
    //   - 作用域不确定时 save()/load() 往返会悄悄落空（取决于组织名/应用名是否已被设置），
    //     本机实测表现为"什么都没写"；
    //   - 独立的应用名确保测试不会写进正式应用（VisionFlowPlatform）的设置里。
    QCoreApplication::setOrganizationName(QStringLiteral("VisionFlowPlatform"));
    QCoreApplication::setApplicationName(QStringLiteral("VisionFlowPlatform-Test"));
}

void ScriptSecurityPolicyTest::languageWhitelist_blocksUnknown()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    QString reason;

    QVERIFY(p.evaluate(QStringLiteral("Python"), QStringLiteral("print(1)"), reason));
    QVERIFY(p.evaluate(QStringLiteral("Lua"), QStringLiteral("print(1)"), reason));

    // 不在白名单的语言必须被拒绝（防止通过 QProcess 启动 bash/cmd/powershell 等）
    QVERIFY(!p.evaluate(QStringLiteral("Bash"), QStringLiteral("echo hi"), reason));
    QVERIFY(!p.evaluate(QStringLiteral("cmd"), QStringLiteral("dir"), reason));
    QVERIFY(!reason.isEmpty());
}

void ScriptSecurityPolicyTest::interpreterFlags_isolation()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    p.setIsolatedPython(true);

    // Python 隔离模式：忽略环境、用户 site、PYTHON* 变量
    QStringList flags = p.interpreterFlags(QStringLiteral("Python"));
    QVERIFY(flags.contains(QStringLiteral("-I")));
    QVERIFY(flags.contains(QStringLiteral("-E")));

    // Lua 当前无等价的解释器级隔离参数
    QVERIFY(p.interpreterFlags(QStringLiteral("Lua")).isEmpty());
}

void ScriptSecurityPolicyTest::buildEnvironment_stripsDangerousVars()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    p.setStripEnvironment(true);

    QProcessEnvironment env = p.buildEnvironment();

    // 可注入解释器行为的变量必须被剥离
    QVERIFY(!env.contains(QStringLiteral("PYTHONPATH")));
    QVERIFY(!env.contains(QStringLiteral("PYTHONHOME")));
    QVERIFY(!env.contains(QStringLiteral("LD_PRELOAD")));
    QVERIFY(!env.contains(QStringLiteral("DYLD_INSERT_LIBRARIES")));

    // PATH 必须保留，否则无法定位 python/lua 解释器本体
    QVERIFY(env.contains(QStringLiteral("PATH")));
}

void ScriptSecurityPolicyTest::audit_writesStructuredLog()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString logPath = dir.path() + QStringLiteral("/script-audit.log");
    p.setAuditLogPath(logPath);
    p.setAuditLogEnabled(true);

    p.audit(QStringLiteral("Python"), QStringLiteral("print('x')"), true, QString());
    p.audit(QStringLiteral("Bash"), QStringLiteral("rm -rf /"), false,
            QStringLiteral("语言 'Bash' 不在允许列表内，禁止执行"));

    QFile f(logPath);
    QVERIFY(f.exists());
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QByteArray content = f.readAll();
    QVERIFY(!content.isEmpty());
    // 每行应是合法 JSON，且包含语言与允许标志，便于事后溯源
    QVERIFY(content.contains("\"language\":\"Python\""));
    QVERIFY(content.contains("\"allowed\":true"));
    QVERIFY(content.contains("\"language\":\"Bash\""));
    QVERIFY(content.contains("\"allowed\":false"));
}

void ScriptSecurityPolicyTest::toggleOff_blocksEverything()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    const bool prev = p.isEnabled();

    p.setEnabled(false);
    QString reason;
    QVERIFY(!p.evaluate(QStringLiteral("Python"), QStringLiteral("x"), reason));
    QVERIFY(!reason.isEmpty());

    p.setEnabled(prev);
    QVERIFY(p.evaluate(QStringLiteral("Python"), QStringLiteral("x"), reason));
}

void ScriptSecurityPolicyTest::sandbox_disabled_isNoOp()
{
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    const bool prev = p.isSandboxEnabled();
    p.setSandboxEnabled(false);

    QProcess proc;
    // 未启用沙箱时：applyProcessSandbox 不应抛异常；attachJob 对未启动/未启用返回 false；
    // closeJob 对无作业句柄应安全无操作。该用例同时锁定沙箱 API 在所有平台可编译。
    p.applyProcessSandbox(&proc);
    QCOMPARE(p.attachJob(&proc), false);
    p.closeJob(&proc);

    p.setSandboxEnabled(prev);
}

void ScriptSecurityPolicyTest::sandboxMode_persistsAcrossLoad()
{
    // 沙箱强度必须跨进程记住（面板上切换后重启仍生效）。用真实的 save()/load() 往返验证。
    // 【重要】测试直接读写机器的 QSettings，结束后必须把设置恢复原状，
    // 否则会把状态留在本机、污染其它运行（沙箱模式一旦被留在 AppContainer，
    // 未做容器准备的机器上脚本会被整体拒绝执行）。
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    const auto originalMode = p.sandboxMode();
    const QString originalAuditPath = p.auditLogPath();

    p.setSandboxMode(ScriptSecurityPolicy::SandboxMode::AppContainer);
    p.save();

    p.setSandboxMode(ScriptSecurityPolicy::SandboxMode::PrivilegeStripped);   // 模拟"换了个进程"
    p.load();
    QVERIFY(p.sandboxMode() == ScriptSecurityPolicy::SandboxMode::AppContainer);

    // 恢复现场（值 + 持久化）
    p.setSandboxMode(originalMode);
    p.setAuditLogPath(originalAuditPath);
    p.save();
    p.load();
    QVERIFY(p.sandboxMode() == originalMode);
}

void ScriptSecurityPolicyTest::interpreterPath_persistsAndFallsBack()
{
    // 解释器路径：现场常有多套 Python/venv，必须能显式指定并跨进程记住；
    // 未配置时要回落到命令名（与历史行为一致，保证向后兼容）。
    ScriptSecurityPolicy &p = ScriptSecurityPolicy::instance();
    const QString original = p.interpreterPath(QStringLiteral("Python"));

    // ① 未配置 → 回落命令名
    p.setInterpreterPath(QStringLiteral("Python"), QString());
    QCOMPARE(p.interpreterPath(QStringLiteral("Python")), QStringLiteral("python"));

    // ② 显式路径 → save/load 往返保持（跨进程生效）
    p.setInterpreterPath(QStringLiteral("Python"), QStringLiteral("C:/Python314/python.exe"));
    p.save();
    p.setInterpreterPath(QStringLiteral("Python"), QString());
    p.load();
    QCOMPARE(p.interpreterPath(QStringLiteral("Python")), QStringLiteral("C:/Python314/python.exe"));

    // ③ 前后空格要清理（从资源管理器复制路径时常见）
    p.setInterpreterPath(QStringLiteral("Python"), QStringLiteral("  C:/x/python.exe  "));
    QCOMPARE(p.interpreterPath(QStringLiteral("Python")), QStringLiteral("C:/x/python.exe"));

    // 恢复现场（值 + 落盘）
    p.setInterpreterPath(QStringLiteral("Python"), original);
    p.save();
}

QTEST_GUILESS_MAIN(ScriptSecurityPolicyTest)

#include "script_security_test.moc"

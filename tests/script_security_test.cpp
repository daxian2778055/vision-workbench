#include "ScriptSecurityPolicy.h"

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFile>
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
    void languageWhitelist_blocksUnknown();
    void interpreterFlags_isolation();
    void buildEnvironment_stripsDangerousVars();
    void audit_writesStructuredLog();
    void toggleOff_blocksEverything();
};

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

QTEST_GUILESS_MAIN(ScriptSecurityPolicyTest)

#include "script_security_test.moc"

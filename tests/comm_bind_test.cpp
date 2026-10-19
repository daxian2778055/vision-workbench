// 通信监听地址解析（177 行）：纯解析器行为面，不碰网络。
// 判据：空参 = Default（产品默认全网卡，历史 0.0.0.0 语义保持）；显式 IP 字面量 =
// Explicit 原样地址（含回环与 0.0.0.0 字面量）；非法配置 = Invalid（调用方拒启动，
// 静默回落 Any = 变相扩面，本腿钉死不得发生）。主机名不解析（DNS 依赖不进绑定面）。
#include <QtTest>
#include <QHostAddress>

#include "CommunicationNodeBase.h"

class CommBindTest : public QObject {
    Q_OBJECT

private slots:
    void emptyMeansDefaultAny();
    void whitespaceMeansDefaultAny();
    void explicitPassthrough();
    void explicitAnyLiteralAllowed();
    void invalidRefusesNotFallback();
    void hostnameIsInvalidNotDns();
};

void CommBindTest::emptyMeansDefaultAny() {
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QString());
    QVERIFY(c.kind == CommBind::BindKind::Default);
    QVERIFY(c.address.isNull()); // Default 面地址留空，由调用方按协议选 Any/AnyIPv4
}

void CommBindTest::whitespaceMeansDefaultAny() {
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QStringLiteral("   "));
    QVERIFY(c.kind == CommBind::BindKind::Default);
}

void CommBindTest::explicitPassthrough() {
    const CommBind::BindChoice loop = CommBind::resolveListenAddress(QStringLiteral("127.0.0.1"));
    QVERIFY(loop.kind == CommBind::BindKind::Explicit);
    QCOMPARE(loop.address.toString(), QString(QStringLiteral("127.0.0.1")));

    const CommBind::BindChoice lan = CommBind::resolveListenAddress(QStringLiteral("192.168.1.10"));
    QVERIFY(lan.kind == CommBind::BindKind::Explicit);
    QCOMPARE(lan.address.toString(), QString(QStringLiteral("192.168.1.10")));

    // IPv6 字面量同支持（::1 回环）
    const CommBind::BindChoice v6 = CommBind::resolveListenAddress(QStringLiteral("::1"));
    QVERIFY(v6.kind == CommBind::BindKind::Explicit);
    QCOMPARE(v6.address.toString(), QString(QStringLiteral("::1")));
}

// "0.0.0.0" 是合法字面量：读作"显式全网卡"（发布就位时留绑定日志），不是 Invalid。
void CommBindTest::explicitAnyLiteralAllowed() {
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QStringLiteral("0.0.0.0"));
    QVERIFY(c.kind == CommBind::BindKind::Explicit);
    QVERIFY(c.address == QHostAddress::AnyIPv4);
}

void CommBindTest::invalidRefusesNotFallback() {
    // 非法配置必须 Invalid（调用方拒启动），不得静默回落成 Default/Any 变相扩面。
    const CommBind::BindChoice bad = CommBind::resolveListenAddress(QStringLiteral("banana"));
    QVERIFY(bad.kind == CommBind::BindKind::Invalid);

    // 边界：用户把目标地址语义填进来（带端口 / 常见笔误形态）同样 Invalid。
    const CommBind::BindChoice withPort = CommBind::resolveListenAddress(QStringLiteral("127.0.0.1:502"));
    QVERIFY(withPort.kind == CommBind::BindKind::Invalid);
    const CommBind::BindChoice withScheme = CommBind::resolveListenAddress(QStringLiteral("tcp://127.0.0.1"));
    QVERIFY(withScheme.kind == CommBind::BindKind::Invalid);
}

void CommBindTest::hostnameIsInvalidNotDns() {
    // 绑定面只收数字字面量：主机名需要 DNS，解析成功与否不可控（静默语义漂移），拒收。
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QStringLiteral("localhost"));
    QVERIFY(c.kind == CommBind::BindKind::Invalid);
}

QTEST_MAIN(CommBindTest)
#include "comm_bind_test.moc"

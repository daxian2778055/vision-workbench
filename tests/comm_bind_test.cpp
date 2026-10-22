// 通信监听地址解析（177 行；U-202 期客户改口径）：纯解析器行为面，不碰网络。
// 判据：空参 = Missing（**未配置**；调用方须拒启动）——U-202 前空参是 Default＝全网卡，
// 本轮按客户口径把「留空＝全网卡」这条默认面撤掉：空参不再等价于全网卡；
// 显式 IP 字面量 = Explicit 原样地址（含回环与 0.0.0.0 字面量，即"确实要全网卡"仍可显式写）；
// 非法配置 = Invalid（调用方拒启动，静默回落 Any = 变相扩面，本腿钉死不得发生）。
// 主机名不解析（DNS 依赖不进绑定面）。
#include <QtTest>
#include <QHostAddress>

#include "CommunicationNodeBase.h"

class CommBindTest : public QObject {
    Q_OBJECT

private slots:
    void emptyMeansMissing();
    void whitespaceMeansMissing();
    void explicitPassthrough();
    void explicitAnyLiteralAllowed();
    void invalidRefusesNotFallback();
    void hostnameIsInvalidNotDns();
    void explicitAlwaysCarriesAddress();   // U-202：Explicit 必须带真地址（调用方直接拿它去 bind）
};

void CommBindTest::emptyMeansMissing() {
    // U-202：空参 = 未配置（不再是全网卡默认）。解析面只报事实，拒启动由调用方决定。
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QString());
    QVERIFY(c.kind == CommBind::BindKind::Missing);
    QVERIFY(c.address.isNull()); // Missing 面不携带监听地址（不得暗藏 Any）
}

void CommBindTest::whitespaceMeansMissing() {
    // 纯空白与空参同口径：都是「没填」，不是「填了全网卡」。
    const CommBind::BindChoice c = CommBind::resolveListenAddress(QStringLiteral("   "));
    QVERIFY(c.kind == CommBind::BindKind::Missing);
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

void CommBindTest::explicitAlwaysCarriesAddress() {
    // U-202：Explicit 必须带真地址——三处调用方都直接拿它去 listen/bind/设网络地址参数，
    // 地址为空就等于绑了个空地址（Qt 视作 Any 的等价物，等于把默认面偷偷放回来）。
    const CommBind::BindChoice any = CommBind::resolveListenAddress(QStringLiteral("0.0.0.0"));
    QVERIFY(any.kind == CommBind::BindKind::Explicit);
    QVERIFY(!any.address.isNull());

    const CommBind::BindChoice lan = CommBind::resolveListenAddress(QStringLiteral("192.168.1.10"));
    QVERIFY(lan.kind == CommBind::BindKind::Explicit);
    QVERIFY(!lan.address.isNull());
}

QTEST_MAIN(CommBindTest)
#include "comm_bind_test.moc"

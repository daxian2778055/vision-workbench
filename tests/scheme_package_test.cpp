#include <QtTest>
#include <QByteArray>

#include "SchemeCrypto.h"
#include "SchemePackage.h"

class SchemePackageTest : public QObject {
    Q_OBJECT

private slots:
    // AES-256-CTR 已知答案向量（NIST 风格：初始计数器=IV，大端自增）。
    // 仅 round-trip 抓不出 MixColumns/密钥扩展类对称 bug，必须用 KAT 钉死。
    void aes256EcbKnownAnswer(); // FIPS 197 样例#4，钉核心正确性
    void aes256CtrKnownAnswer();
    void aes256CtrRoundTrip_data();
    void aes256CtrRoundTrip();

    void envelopeRoundTripEncryptedReadonly();
    void envelopeRoundTripEncryptedPlain();
    void envelopeRoundTripNoEncryptionReadonly();
    void wrongPassphraseRejected();
    void tamperedCipherRejected();
    void legacyJsonIsNotEnvelope();
};

void SchemePackageTest::aes256EcbKnownAnswer() {
    // FIPS 197 Appendix C.3 样例 #4（AES-256 ECB），钉死 AES 核心（S-box/密钥扩展/MixColumns）。
    const QByteArray key = QByteArray::fromHex(
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    const QByteArray block = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    const QByteArray expected = QByteArray::fromHex("8ea2b7ca516745bfeafc49904b496089");

    QByteArray out;
    SchemeCrypto::aes256EcbBlock(key, block, out);
    QCOMPARE(out.toHex(), expected.toHex());
}

void SchemePackageTest::aes256CtrKnownAnswer() {
    const QByteArray key = QByteArray::fromHex(
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    // NIST SP 800-38A F.5（AES-256-CTR）初始计数器为 f0f1…feff（非 0001…0e0f）。
    const QByteArray iv = QByteArray::fromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const QByteArray plain = QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a");
    const QByteArray expected = QByteArray::fromHex("601ec313775789a5b7a7f504bbf3d228");

    QByteArray out;
    SchemeCrypto::aes256Ctr(key, iv, plain, out);
    QCOMPARE(out.toHex(), expected.toHex());
}

void SchemePackageTest::aes256CtrRoundTrip_data() {
    QTest::addColumn<int>("len");
    QTest::newRow("empty") << 0;
    QTest::newRow("one") << 1;
    QTest::newRow("fifteen") << 15;
    QTest::newRow("block") << 16;
    QTest::newRow("thirtyone") << 31;
    QTest::newRow("two_blocks") << 32;
    QTest::newRow("bulk") << 1000;
}

void SchemePackageTest::aes256CtrRoundTrip() {
    QFETCH(int, len);
    const QByteArray key(32, char(0x42));
    const QByteArray iv(16, char(0x11));
    QByteArray plain;
    plain.reserve(len);
    for (int i = 0; i < len; ++i)
        plain.append(static_cast<char>(i & 0xff));

    QByteArray cipher, back;
    SchemeCrypto::aes256Ctr(key, iv, plain, cipher);
    SchemeCrypto::aes256Ctr(key, iv, cipher, back);
    QCOMPARE(back, plain);
    if (len > 0)
        QVERIFY(cipher != plain); // 密文不应等于明文
}

void SchemePackageTest::envelopeRoundTripEncryptedReadonly() {
    const QByteArray json = R"({"scenes":[],"schemaVersion":1})";
    const QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("s3cret"), true);
    QVERIFY(SchemePackage::isEnvelope(env));
    QVERIFY(SchemePackage::peekReadonly(env));

    bool readonly = false, ok = false;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("s3cret"), &readonly, &ok);
    QVERIFY(ok);
    QVERIFY(readonly);
    QCOMPARE(out, json);
}

void SchemePackageTest::envelopeRoundTripEncryptedPlain() {
    const QByteArray json = R"({"a":123,"b":"中文测试"})";
    const QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);
    QVERIFY(SchemePackage::isEnvelope(env));
    QVERIFY(!SchemePackage::peekReadonly(env));

    bool readonly = true, ok = false;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("pw"), &readonly, &ok);
    QVERIFY(ok);
    QVERIFY(!readonly);
    QCOMPARE(out, json);
}

void SchemePackageTest::envelopeRoundTripNoEncryptionReadonly() {
    const QByteArray json = R"({"x":1})";
    const QByteArray env = SchemePackage::exportScheme(json, QString(), true);
    QVERIFY(SchemePackage::isEnvelope(env));
    QVERIFY(SchemePackage::peekReadonly(env));

    bool readonly = false, ok = false;
    const QByteArray out = SchemePackage::importScheme(env, QString(), &readonly, &ok);
    QVERIFY(ok);
    QVERIFY(readonly);
    QCOMPARE(out, json);
}

void SchemePackageTest::wrongPassphraseRejected() {
    const QByteArray json = R"({"y":2})";
    const QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("right"), false);

    bool readonly = false, ok = false;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("wrong"), &readonly, &ok);
    QVERIFY(!ok);
    QVERIFY(out.isEmpty());
}

void SchemePackageTest::tamperedCipherRejected() {
    const QByteArray json = R"({"z":[1,2,3]})";
    QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);

    // 末段 32 字节是 HMAC；翻转其中一字节，MAC 校验必失败。
    const int last = env.size() - 1;
    env[last] = static_cast<char>(env[last] ^ 0xff);

    bool readonly = false, ok = false;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("pw"), &readonly, &ok);
    QVERIFY(!ok);
}

void SchemePackageTest::legacyJsonIsNotEnvelope() {
    const QByteArray json = R"({"scenes":[],"schemaVersion":1})";
    QVERIFY(!SchemePackage::isEnvelope(json));
    QVERIFY(!SchemePackage::peekReadonly(json));
}

QTEST_MAIN(SchemePackageTest)
#include "scheme_package_test.moc"

#include <QtTest>
#include <QByteArray>
#include <QElapsedTimer>

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
    void oversizedIterationsRejectedAndBounded();
    void iterationsSensitiveToInnerDelta();
    void foreignInRangeValueRejectedFast();
    // 恒时比较的行为面：等值/单字节差/长度差/双空——四个方向的读数都有据。
    // 差异位置不进耗时这半条不是墙钟能钉的（见 SchemeCrypto.h 侧注），行为面照钉。
    void constTimeEqualsBasics();
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

// 篡改的不是密文而是 PBKDF2 代数字段：一个恶意加密信封可把它写成 0x7FFFFFFF，
// 进口先派生后验 MAC，若不做值检会给 GUI 线程套上小时级的 PBKDF2（打开方案的
// 口令框当场僵死）。同一版格式的写侧只发 kIterations 一个值，进口对其它值在读出
// 后立即拒收——一次派生都不付。偏移 22 = 4 魔数 + 1 版本 + 1 标志 + 16 盐。
void SchemePackageTest::oversizedIterationsRejectedAndBounded() {
    const int iterOff = 4 + 1 + 1 + 16;
    const QByteArray json = R"({"fits":1})";
    QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);

    // 先钉布局：写侧恒把该字段写成 100000（0x000186A0），偏移就位才谈篡改。
    QCOMPARE(static_cast<quint8>(env[iterOff]),   quint8(0x00));
    QCOMPARE(static_cast<quint8>(env[iterOff+1]), quint8(0x01));
    QCOMPARE(static_cast<quint8>(env[iterOff+2]), quint8(0x86));
    QCOMPARE(static_cast<quint8>(env[iterOff+3]), quint8(0xA0));

    env[iterOff]   = static_cast<char>(0x7f);
    env[iterOff+1] = static_cast<char>(0xff);
    env[iterOff+2] = static_cast<char>(0xff);
    env[iterOff+3] = static_cast<char>(0xff);

    QElapsedTimer clock;
    clock.start();
    bool readonly = false, ok = true;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("pw"), &readonly, &ok);
    const qint64 ms = clock.elapsed();
    QVERIFY(!ok); // 先值检：0x7FFFFFFF 不是本版格式承认的行情
    QVERIFY(out.isEmpty());
    // 早拒收应在毫秒级付完；任何先派生的退回形状在本机都要付约一秒（派生恒用
    // 写侧值），700ms 的界当场读红，在场的毫秒级给它百倍余量。
    QVERIFY2(ms < 700,
             qPrintable(QStringLiteral("oversized iterations import took %1 ms, early-reject bound is 700").arg(ms)));
}

// 同形的小步对照：只把 000186A0 抬成 000186A1（+1 代）。值检下它也是 foreign 值、
// 与 0x7FFFFFFF 走同一条早拒收；钉的是"接受与否只取决于该字段是否等于写侧行情"。
void SchemePackageTest::iterationsSensitiveToInnerDelta() {
    const int iterOff = 4 + 1 + 1 + 16;
    const QByteArray json = R"({"delta":1})";
    QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);
    QCOMPARE(static_cast<quint8>(env[iterOff+3]), quint8(0xA0));

    env[iterOff+3] = static_cast<char>(0xA1);
    bool readonly = false, ok = true;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("pw"), &readonly, &ok);
    QVERIFY(!ok);
    QVERIFY(out.isEmpty());
}

// 值检的 fast-path 钉：限内的 foreign 值（往上限走一眼看不出差别的 500000）同样
// 必须在读出后立即拒收。若哪天有人把值检退回成任何先派生的形状，本机口径下
// 一次派生恒约一秒，700ms 的界当场读红；值检在场的读数是毫秒级，余量百倍。
void SchemePackageTest::foreignInRangeValueRejectedFast() {
    const int iterOff = 4 + 1 + 1 + 16;
    const QByteArray json = R"({"cap":1})";
    QByteArray env = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);
    // 000186A0 → 0007A120（500000）：大端低三字节各动一次。
    QCOMPARE(static_cast<quint8>(env[iterOff+1]), quint8(0x01));
    env[iterOff+1] = static_cast<char>(0x07);
    env[iterOff+2] = static_cast<char>(0xA1);
    env[iterOff+3] = static_cast<char>(0x20);

    QElapsedTimer clock;
    clock.start();
    bool readonly = false, ok = true;
    const QByteArray out = SchemePackage::importScheme(env, QStringLiteral("pw"), &readonly, &ok);
    const qint64 ms = clock.elapsed();
    QVERIFY(!ok);
    QVERIFY(out.isEmpty());
    QVERIFY2(ms < 700,
             qPrintable(QStringLiteral("in-range foreign value took %1 ms, no-derive bound is 700").arg(ms)));
}

// 恒时比较的行为面。信封 MAC 校验已从「早退 !=」换成 SchemeCrypto::constTimeEquals；
// 这里钉四向读数：等值真、单字节差假、长度差假（公共前缀再长也假）、双空真。
// 时序安全本身是代码评审性质（任何逐字节累计实现都成立），墙钟把它钉不住——
// 接线的证据走改坏自证面：把 constTimeEquals 短路成恒真重编，篡改/错口令两腿同批读红。
void SchemePackageTest::constTimeEqualsBasics() {
    const QByteArray digest = SchemeCrypto::hmacSha256(QByteArrayLiteral("k"), QByteArrayLiteral("m"));

    QVERIFY(SchemeCrypto::constTimeEquals(digest, digest));
    QByteArray oneByteOff = digest;
    oneByteOff[17] = static_cast<char>(oneByteOff[17] ^ 0x01);
    QVERIFY(!SchemeCrypto::constTimeEquals(digest, oneByteOff));

    const QByteArray withPrefix = digest + QByteArrayLiteral("tail");
    QVERIFY(!SchemeCrypto::constTimeEquals(digest, withPrefix));

    QVERIFY(SchemeCrypto::constTimeEquals(QByteArray(), QByteArray()));
    QVERIFY(!SchemeCrypto::constTimeEquals(QByteArrayLiteral("a"), QByteArrayLiteral("ab")));
}

QTEST_MAIN(SchemePackageTest)
#include "scheme_package_test.moc"

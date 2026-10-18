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
    void envelopeEmbedsFreshRandomness();

    // 迁移态（172 行）：两只后端逐位对照——显式点名 CNG 的三枚检定先 assert 成功，
    // 再与自实现（*_Handwritten）同输入比输出逐位；公共入口再与显式 CNG 比一次为接线读数。
    void ecbCngMatchesHandwritten();
    void ctrCngMatchesHandwritten();
    void pbkdf2CngMatchesHandwritten();
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
// 这里钉五向读数：等值真、单字节差假、长度差假（公共前缀再长也假）、双空真、
// 短串加零尾巴假（N3：长度差折进累计，短侧不补零——补零会把「ab」与「ab\0\0\0\0」读成等值）。
// 时序安全本身不是墙钟能钉的（见 SchemeCrypto.h 侧注），行为面照钉；
// 接线的证据走改坏自证面：把 constTimeEquals 短路成恒真重编，篡改腿同批读红；
// 把长度折入摘掉重编（退回纯补零形状），零尾巴两枚同批读红。
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

    // N3：短侧不判空补齐的钉
    const QByteArray zeroTail = QByteArrayLiteral("ab") + QByteArray(4, '\0');
    QVERIFY(!SchemeCrypto::constTimeEquals(QByteArrayLiteral("ab"), zeroTail));
    QVERIFY(SchemeCrypto::constTimeEquals(zeroTail, QByteArrayLiteral("ab") + QByteArray(4, '\0')));
}

// 信封盐与 IV 是随行随机量：同口令连打两枚，密文层不该出重件。
// 折在 envelope 本体上（盐 16 + 迭代 4 + IV 16 + 密文 + MAC），两枚全同即随机位失效——
// 把 randomBytes 换成定值填充的红臂形状，本腿当场红。
void SchemePackageTest::envelopeEmbedsFreshRandomness() {
    const QByteArray json = R"({"cap":1})";
    const QByteArray a = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);
    const QByteArray b = SchemePackage::exportScheme(json, QStringLiteral("pw"), false);
    QVERIFY2(a != b, "同口令同方案的两枚信封不该逐字节同串（盐/IV 随机位失效）");
}

namespace {
// 确定性伪随机场（LCG）：同种子两处跑出逐位相同字节，腿的判据不依赖平台随机数。
QByteArray brewBytes(int n, quint32 seed) {
    QByteArray out(n, char(0));
    for (int i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u;
        out[i] = static_cast<char>((seed >> 17) & 0xff);
    }
    return out;
}
}

void SchemePackageTest::ecbCngMatchesHandwritten() {
    // 三侧九组：全零、全一、伪随机各一组。
    const QList<QByteArray> keySet = {QByteArray(32, char(0x00)), QByteArray(32, char(0xff)),
                                      brewBytes(32, 7)};
    const QList<QByteArray> blockSet = {QByteArray(16, char(0x00)), QByteArray(16, char(0xff)),
                                        brewBytes(16, 13)};
    for (const QByteArray &key : keySet) {
        for (const QByteArray &block : blockSet) {
            QByteArray viaCng, viaHand;
            QVERIFY2(SchemeCrypto::aes256EcbBlockCng(key, block, viaCng),
                     "显式点名与公共入口两处 CNG：单块 ECB 加密在对照腿里先要可用");
            SchemeCrypto::aes256EcbBlockHandwritten(key, block, viaHand);
            QCOMPARE(viaCng.toHex(), viaHand.toHex());
            QByteArray viaPublic;
            SchemeCrypto::aes256EcbBlock(key, block, viaPublic);
            QCOMPARE(viaPublic.toHex(), viaCng.toHex());
        }
    }
}

void SchemePackageTest::ctrCngMatchesHandwritten() {
    // 八档长度：空、1、15（半块内）、16、31、41（越块）、1000、16385（越千块）。
    static const int lens[] = {0, 1, 15, 16, 31, 41, 1000, 16385};
    const QByteArray key = brewBytes(32, 3);
    const QByteArray iv = brewBytes(16, 5);
    for (int len : lens) {
        const QByteArray plain = brewBytes(len, 11);
        QByteArray viaCng, viaHand;
        QVERIFY2(SchemeCrypto::aes256CtrCng(key, iv, plain, viaCng),
                 "显式点名与公共入口两处 CNG：CTR 面在对照腿里先要可用");
        SchemeCrypto::aes256CtrHandwritten(key, iv, plain, viaHand);
        QCOMPARE(viaCng.toHex(), viaHand.toHex());
        QByteArray viaPublic;
        SchemeCrypto::aes256Ctr(key, iv, plain, viaPublic);
        QCOMPARE(viaPublic.toHex(), viaCng.toHex());
        // 往返对称：同后端加密再解密还原（对 CNG 显式与自实现各验一次）。
        QByteArray back;
        SchemeCrypto::aes256CtrHandwritten(key, iv, viaCng, back);
        QCOMPARE(back, plain);
        QByteArray backHand;
        SchemeCrypto::aes256CtrHandwritten(key, iv, viaHand, backHand);
        QCOMPARE(backHand, plain);
    }
}

void SchemePackageTest::pbkdf2CngMatchesHandwritten() {
    // 六参数组：空口令、空盐、超长口令（HMAC 键 >64 的哈希分支）、dkLen 1（截断面）、
    // 千轮、60000 轮（与 167 行账户默认同参数）——六格定点。
    struct Case { QByteArray pass; QByteArray salt; int iters; int dkLen; };
    const QList<Case> cases = {
        {QByteArrayLiteral("pass"), QByteArray(), 1, 32},
        {QByteArray(), QByteArray(), 1, 64},
        {brewBytes(200, 21), brewBytes(16, 22), 2, 64},
        {brewBytes(33, 31), brewBytes(24, 32), 3, 1},
        {brewBytes(10, 41), brewBytes(8, 42), 1000, 32},
        {QByteArrayLiteral("s3cret"), QByteArrayLiteral("NaCl"), 60000, 64},
    };
    for (const Case &c : cases) {
        QByteArray viaCng, viaHand;
        QVERIFY2(SchemeCrypto::pbkdf2HmacSha256Cng(c.pass, c.salt, c.iters, c.dkLen, viaCng),
                 "显式点名 CNG 派生在对照腿里必须可用");
        viaHand = SchemeCrypto::pbkdf2HmacSha256Handwritten(c.pass, c.salt, c.iters, c.dkLen);
        QCOMPARE(viaCng.toHex(), viaHand.toHex());
        QCOMPARE(viaCng.size(), c.dkLen);
    }
}

QTEST_MAIN(SchemePackageTest)
#include "scheme_package_test.moc"
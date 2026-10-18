#include "SchemePackage.h"
#include "SchemeCrypto.h"

#include <QDataStream>
#include <QRandomGenerator>
#include <QString>

const char SchemePackage::kMagic[4] = {'V', 'F', 'P', 'E'};

void SchemePackage::putUint32BE(QByteArray &out, quint32 v) {
    out.append(static_cast<char>((v >> 24) & 0xff));
    out.append(static_cast<char>((v >> 16) & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
    out.append(static_cast<char>(v & 0xff));
}

bool SchemePackage::getUint32BE(const char *p, quint32 &v) {
    v = (static_cast<quint8>(p[0]) << 24) | (static_cast<quint8>(p[1]) << 16) |
        (static_cast<quint8>(p[2]) << 8) | static_cast<quint8>(p[3]);
    return true;
}

QByteArray randomBytes(int n) {
    // 盐与 IV 是密钥材料：直接走 OS 的 CSPRNG 流（QRandomGenerator::system()，Qt 文档
    // 注明其输出可用于生成密码学凭据）。原先那两个构造（global() 的 MT 流与
    // securelySeeded()——种子来自安全源但后续序列仍是同一族）都只适合 Monte-Carlo。
    QByteArray r(n, char(0));
    quint32 buf = 0;
    quint32 left = 0;
    for (int i = 0; i < n; ++i) {
        if (left == 0) {
            buf = QRandomGenerator::system()->generate();
            left = 4;
        }
        r[i] = static_cast<char>(buf & 0xff);
        buf >>= 8;
        --left;
    }
    return r;
}

QByteArray SchemePackage::exportScheme(const QByteArray &jsonPayload,
                                       const QString &passphrase,
                                       bool readonly) {
    const bool encrypted = !passphrase.isEmpty();
    quint8 flags = (readonly ? kFlagReadonly : 0) | (encrypted ? kFlagEncrypted : 0);

    const QByteArray compressed = qCompress(jsonPayload);

    QByteArray body;
    body.append(kMagic, 4);
    body.append(static_cast<char>(kFormatVersion));
    body.append(static_cast<char>(flags));

    if (encrypted) {
        const QByteArray salt = randomBytes(16);
        const QByteArray iv = randomBytes(16);
        QByteArray encKey, macKey;
        SchemeCrypto::deriveKeys(passphrase.toUtf8(), salt, kIterations, encKey, macKey);

        QByteArray cipher;
        SchemeCrypto::aes256Ctr(encKey, iv, compressed, cipher);

        body.append(salt);
        putUint32BE(body, static_cast<quint32>(kIterations));
        body.append(iv);
        body.append(cipher);

        const QByteArray mac = SchemeCrypto::hmacSha256(macKey, body);
        body.append(mac);
    } else {
        // 非加密只读打包：直接存压缩后的明文，无 MAC（篡改可由解压/JSON 解析失败暴露）
        body.append(compressed);
    }
    return body;
}

QByteArray SchemePackage::importScheme(const QByteArray &envelope,
                                       const QString &passphrase,
                                       bool *readonly, bool *ok) {
    *ok = false;
    *readonly = false;
    if (!isEnvelope(envelope) || envelope.size() < 7) {
        return QByteArray();
    }

    const quint8 version = static_cast<quint8>(envelope[4]);
    const quint8 flags = static_cast<quint8>(envelope[5]);
    *readonly = (flags & kFlagReadonly) != 0;

    if (version != kFormatVersion)
        return QByteArray();

    const bool encrypted = (flags & kFlagEncrypted) != 0;
    QByteArray compressed;
    int off = 6;

    if (encrypted) {
        if (envelope.size() < off + 16 + 4 + 16 + 32)
            return QByteArray();
        const QByteArray salt = envelope.mid(off, 16); off += 16;
        quint32 iterations = 0;
        getUint32BE(envelope.constData() + off, iterations); off += 4;
        // 进口先值检后派生（见 SchemePackage.h 注）：本版格式只发 kIterations 一个值，
        // 其它值（无论超限还是限内 foreign 值）一律在此 O(1) 拒收，一次派生都不付；
        // 旧的"限幅后派生"仍会把上限额度约 10s 付给恶意文件，这是把那笔尾款退掉。
        // 判定在 MAC 之前且无例外分支，fail-closed 不受信封任何其它字节影响。
        if (iterations != kIterationsValid)
            return QByteArray();
        const QByteArray iv = envelope.mid(off, 16); off += 16;

        const int macOffset = envelope.size() - 32;
        if (macOffset < off)
            return QByteArray();
        const QByteArray cipher = envelope.mid(off, macOffset - off);
        const QByteArray mac = envelope.mid(macOffset, 32);

        // 校验口令需在信封里（否则不能解密）；先派生 MAC 密钥验证完整性。
        // 恒时比较（见 SchemeCrypto.h 侧注）：信封摘要 32 字节公开，差异位置不该进耗时。
        QByteArray encKey, macKey;
        SchemeCrypto::deriveKeys(passphrase.toUtf8(), salt, kIterations, encKey, macKey);

        QByteArray bodyForMac = envelope.left(macOffset);
        // 恒时比较（见 SchemeCrypto.h 侧注）：信封摘要 32 字节公开，差异位置不该进耗时。
        if (!SchemeCrypto::constTimeEquals(SchemeCrypto::hmacSha256(macKey, bodyForMac), mac))
            return QByteArray(); // 口令错误或文件被篡改

        QByteArray plain;
        SchemeCrypto::aes256Ctr(encKey, iv, cipher, plain);
        compressed = plain;
    } else {
        compressed = envelope.mid(off);
    }

    QByteArray payload = qUncompress(compressed);
    if (payload.isEmpty()) // qUncompress 失败返回空（注意：空输入也返回空，但方案 JSON 不空）
        return QByteArray();
    if (!payload.startsWith('{')) // 防"空解压"误判：合法方案 JSON 必以 { 开头
        return QByteArray();

    *ok = true;
    return payload;
}

bool SchemePackage::isEnvelope(const QByteArray &data) {
    return data.size() >= 4 &&
           memcmp(data.constData(), kMagic, 4) == 0;
}

bool SchemePackage::isEncryptedEnvelope(const QByteArray &data) {
    if (!isEnvelope(data) || data.size() < 6)
        return false;
    return (static_cast<quint8>(data[5]) & kFlagEncrypted) != 0;
}

bool SchemePackage::peekReadonly(const QByteArray &data) {
    if (!isEnvelope(data) || data.size() < 6)
        return false;
    return (static_cast<quint8>(data[5]) & kFlagReadonly) != 0;
}

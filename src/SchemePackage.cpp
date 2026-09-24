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
    QByteArray r(n, char(0));
    QRandomGenerator rng = QRandomGenerator::securelySeeded();
    for (int i = 0; i < n; ++i)
        r[i] = static_cast<char>(rng.generate() & 0xff);
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
        const QByteArray iv = envelope.mid(off, 16); off += 16;

        const int macOffset = envelope.size() - 32;
        if (macOffset < off)
            return QByteArray();
        const QByteArray cipher = envelope.mid(off, macOffset - off);
        const QByteArray mac = envelope.mid(macOffset, 32);

        // 校验口令需在信封里（否则不能解密）；先派生 MAC 密钥验证完整性。
        QByteArray encKey, macKey;
        SchemeCrypto::deriveKeys(passphrase.toUtf8(), salt, static_cast<int>(iterations), encKey, macKey);

        QByteArray bodyForMac = envelope.left(macOffset);
        if (SchemeCrypto::hmacSha256(macKey, bodyForMac) != mac)
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

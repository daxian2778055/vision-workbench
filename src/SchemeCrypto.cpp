#include "SchemeCrypto.h"

#include <QDebug>
#include <QCryptographicHash>
#include <array>
#include <cstdint>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <bcrypt.h>
#endif

namespace SchemeCrypto {
namespace {

// ---- AES-256 常量 ----
constexpr int kNk = 8;   // 密钥字数（32 字节）
constexpr int kNr = 14;  // 轮数（AES-256）
constexpr int kNb = 4;   // 状态列数

// 标准 S-box
const uint8_t kSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16};

const uint8_t kRcon[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

inline uint8_t xtime(uint8_t x) {
    return static_cast<uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1b : 0x00));
}

uint8_t gfMul(uint8_t a, uint8_t b) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1)
            r ^= a;
        a = xtime(a); // xtime 内含高位置位的 0x1b 约简，勿在此二次约简
        b >>= 1;
    }
    return r;
}

void keyExpansion(const uint8_t *key, uint8_t w[/*4*(Nr+1)*4*/]) {
    const int roundKeys = kNb * (kNr + 1); // 60 字 = 240 字节
    for (int i = 0; i < kNk; ++i) {
        w[4 * i + 0] = key[4 * i + 0];
        w[4 * i + 1] = key[4 * i + 1];
        w[4 * i + 2] = key[4 * i + 2];
        w[4 * i + 3] = key[4 * i + 3];
    }
    uint8_t tmp[4];
    for (int i = kNk; i < roundKeys; ++i) {
        tmp[0] = w[4 * (i - 1) + 0];
        tmp[1] = w[4 * (i - 1) + 1];
        tmp[2] = w[4 * (i - 1) + 2];
        tmp[3] = w[4 * (i - 1) + 3];
        if (i % kNk == 0) {
            // RotWord
            const uint8_t t = tmp[0];
            tmp[0] = tmp[1];
            tmp[1] = tmp[2];
            tmp[2] = tmp[3];
            tmp[3] = t;
            // SubWord
            tmp[0] = kSbox[tmp[0]];
            tmp[1] = kSbox[tmp[1]];
            tmp[2] = kSbox[tmp[2]];
            tmp[3] = kSbox[tmp[3]];
            tmp[0] ^= kRcon[i / kNk - 1];
        } else if (i % kNk == 4) {
            // Nk==8 专属
            tmp[0] = kSbox[tmp[0]];
            tmp[1] = kSbox[tmp[1]];
            tmp[2] = kSbox[tmp[2]];
            tmp[3] = kSbox[tmp[3]];
        }
        w[4 * i + 0] = w[4 * (i - kNk) + 0] ^ tmp[0];
        w[4 * i + 1] = w[4 * (i - kNk) + 1] ^ tmp[1];
        w[4 * i + 2] = w[4 * (i - kNk) + 2] ^ tmp[2];
        w[4 * i + 3] = w[4 * (i - kNk) + 3] ^ tmp[3];
    }
}

void encryptBlock(const uint8_t in[16], const uint8_t *w, uint8_t out[16]) {
    uint8_t s[16];
    for (int i = 0; i < 16; ++i)
        s[i] = in[i];

    // AddRoundKey(round 0)
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            s[r + 4 * c] ^= w[4 * c + r];

    for (int round = 1; round < kNr; ++round) {
        // SubBytes
        for (int i = 0; i < 16; ++i)
            s[i] = kSbox[s[i]];
        // ShiftRows
        auto shift = [&](int r, int n) {
            uint8_t t[4];
            for (int c = 0; c < 4; ++c)
                t[c] = s[r + 4 * ((c + n) % 4)];
            for (int c = 0; c < 4; ++c)
                s[r + 4 * c] = t[c];
        };
        shift(1, 1);
        shift(2, 2);
        shift(3, 3);
        // MixColumns
        for (int c = 0; c < 4; ++c) {
            const uint8_t a0 = s[0 + 4 * c], a1 = s[1 + 4 * c];
            const uint8_t a2 = s[2 + 4 * c], a3 = s[3 + 4 * c];
            s[0 + 4 * c] = gfMul(a0, 2) ^ gfMul(a1, 3) ^ a2 ^ a3;
            s[1 + 4 * c] = a0 ^ gfMul(a1, 2) ^ gfMul(a2, 3) ^ a3;
            s[2 + 4 * c] = a0 ^ a1 ^ gfMul(a2, 2) ^ gfMul(a3, 3);
            s[3 + 4 * c] = gfMul(a0, 3) ^ a1 ^ a2 ^ gfMul(a3, 2);
        }
        // AddRoundKey(round)
        const int base = round * kNb * 4;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                s[r + 4 * c] ^= w[base + 4 * c + r];
    }

    // 末轮（无 MixColumns）
    for (int i = 0; i < 16; ++i)
        s[i] = kSbox[s[i]];
    auto shiftLast = [&](int r, int n) {
        uint8_t t[4];
        for (int c = 0; c < 4; ++c)
            t[c] = s[r + 4 * ((c + n) % 4)];
        for (int c = 0; c < 4; ++c)
            s[r + 4 * c] = t[c];
    };
    shiftLast(1, 1);
    shiftLast(2, 2);
    shiftLast(3, 3);
    const int base = kNr * kNb * 4;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            s[r + 4 * c] ^= w[base + 4 * c + r];

    for (int i = 0; i < 16; ++i)
        out[i] = s[i];
}

// 16 字节计数器（大端）自增
void incrementCounter(uint8_t ctr[16]) {
    for (int i = 15; i >= 0; --i) {
        if (++ctr[i] != 0)
            break;
    }
}

// —— 自实现面（原公共三枚的本体原样搬家）——
// 照旧供三处使用：*_Handwritten 公共声名（对照腿）、公共三枚在 CNG 不可用时的同字节兜底。
void handEcbBlock16(const QByteArray &key, const QByteArray &block, QByteArray &out) {
    uint8_t w[kNb * (kNr + 1) * 4];
    keyExpansion(reinterpret_cast<const uint8_t *>(key.constData()), w);
    uint8_t in[16], ek[16];
    memcpy(in, block.constData(), 16);
    encryptBlock(in, w, ek);
    out = QByteArray(reinterpret_cast<const char *>(ek), 16);
}

void handCtr(const QByteArray &key, const QByteArray &iv,
             const QByteArray &in, QByteArray &out) {
    uint8_t w[kNb * (kNr + 1) * 4];
    keyExpansion(reinterpret_cast<const uint8_t *>(key.constData()), w);

    QByteArray result(in.size(), char(0));
    uint8_t ctr[16];
    memcpy(ctr, iv.constData(), 16);
    uint8_t ek[16];

    const int n = in.size();
    for (int off = 0; off < n; off += 16) {
        encryptBlock(ctr, w, ek);
        const int len = qMin(16, n - off);
        for (int i = 0; i < len; ++i)
            result[off + i] = static_cast<char>(in[off + i] ^ ek[i]);
        incrementCounter(ctr);
    }
    out = result;
}

QByteArray handPbkdf2HmacSha256(const QByteArray &password, const QByteArray &salt,
                                int iterations, int dkLen) {
    const int hLen = 32; // SHA-256
    const int l = (dkLen + hLen - 1) / hLen;
    QByteArray dk;
    dk.reserve(dkLen);
    QByteArray saltBlock = salt;
    for (int i = 1; i <= l; ++i) {
        // salt || INT_32_BE(i)
        QByteArray u = hmacSha256(password, saltBlock + QByteArray(1, static_cast<char>((i >> 24) & 0xff))
                                                     + QByteArray(1, static_cast<char>((i >> 16) & 0xff))
                                                     + QByteArray(1, static_cast<char>((i >> 8) & 0xff))
                                                     + QByteArray(1, static_cast<char>(i & 0xff)));
        QByteArray t = u;
        for (int j = 1; j < iterations; ++j) {
            u = hmacSha256(password, u);
            for (int k = 0; k < hLen; ++k)
                t[k] = static_cast<char>(t[k] ^ u[k]);
        }
        dk.append(t);
    }
    return dk.left(dkLen);
}

#if defined(Q_OS_WIN)
// —— Windows CNG 后端（迁移态，172 行）——
// CNG 公开面不提供 AES-CTR 链模式：这里只担两枚原语——单块 ECB 加密与 PBKDF2 派生；
// CTR 的计数循环留在本侧，与自实现失败路径同形。
class CngAesSession {
public:
    CngAesSession() = default;
    ~CngAesSession() {
        if (m_key)
            ::BCryptDestroyKey(m_key);
        if (m_alg)
            ::BCryptCloseAlgorithmProvider(m_alg, 0);
    }
    CngAesSession(const CngAesSession &) = delete;
    CngAesSession &operator=(const CngAesSession &) = delete;

    // 开算法句柄 + 钉 ECB 链模式 + 从 32 字节装出对称密钥，三步一次到位。
    bool init(const QByteArray &key) {
        NTSTATUS st = ::BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
        if (!BCRYPT_SUCCESS(st))
            return false;
        wchar_t modeEcb[] = BCRYPT_CHAIN_MODE_ECB;
        st = ::BCryptSetProperty(m_alg, BCRYPT_CHAINING_MODE,
                                 (PUCHAR)modeEcb, sizeof(modeEcb), 0);
        if (!BCRYPT_SUCCESS(st))
            return false;
        st = ::BCryptGenerateSymmetricKey(m_alg, &m_key, nullptr, 0,
                                          (PUCHAR)key.constData(), ULONG(key.size()), 0);
        if (!BCRYPT_SUCCESS(st)) {
            m_key = nullptr; // 生成失败句柄无效，防析构误销
            return false;
        }
        return true;
    }

    // 单块 ECB 加密（无填充；入块 16 字节、出块 16 字节）。
    bool encryptBlock(const QByteArray &block, QByteArray &out) const {
        if (block.size() != out.size())
            return false;
        ULONG done = 0;
        NTSTATUS st = ::BCryptEncrypt(m_key, (PUCHAR)block.constData(), ULONG(block.size()),
                                      nullptr, nullptr, 0, (PUCHAR)out.data(),
                                      ULONG(out.size()), &done, 0);
        return BCRYPT_SUCCESS(st) && done == ULONG(out.size());
    }

private:
    BCRYPT_ALG_HANDLE m_alg = nullptr;
    BCRYPT_KEY_HANDLE m_key = nullptr;
};

// 兜底告警的去重旗：竞态无害（同时到位顶多各警告一枚）。它是在日志里认得出
// “这台机器退了自实现”的唯一线索——两只后端逐位一致（对照腿钉死），别无行为差。
void warnCngFallbackOnce() {
    static bool warned = false;
    if (warned)
        return;
    warned = true;
    qWarning("SchemeCrypto: CNG backend unavailable, falling back to self-contained crypto "
             "(bit-exact twin legs in scheme_package_test pin both)");
}

bool cngEcbBlock(const QByteArray &key, const QByteArray &block, QByteArray &out) {
    CngAesSession session;
    if (!session.init(key))
        return false;
    out.assign(16, char(0));
    return session.encryptBlock(block, out);
}

bool cngCtr(const QByteArray &key, const QByteArray &iv,
            const QByteArray &in, QByteArray &out) {
    CngAesSession session;
    if (!session.init(key))
        return false;
    QByteArray result(in.size(), char(0));
    QByteArray keystream(16, char(0));
    uint8_t ctr[16];
    memcpy(ctr, iv.constData(), 16);
    const int n = in.size();
    for (int off = 0; off < n; off += 16) {
        const QByteArray counter(reinterpret_cast<const char *>(ctr), 16);
        if (!session.encryptBlock(counter, keystream))
            return false;
        const int len = qMin(16, n - off);
        for (int i = 0; i < len; ++i)
            result[off + i] = static_cast<char>(in[off + i] ^ keystream[i]);
        incrementCounter(ctr);
    }
    out = result;
    return true;
}

bool cngPbkdf2HmacSha256(const QByteArray &password, const QByteArray &salt,
                         int iterations, int dkLen, QByteArray &dk) {
    // PBKDF2 在 CNG 里走 HMAC 域：算法句柄必须带 BCRYPT_ALG_HANDLE_HMAC_FLAG 开出——
    // SHA-256 伪句柄没这枚旗，直接拿会 STATUS_INVALID_HANDLE（首轮绿面踩过这坑）。
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS st = ::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                                BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (!BCRYPT_SUCCESS(st))
        return false;
    // 开销可以忽略：一次派生的主体是派生本身（万轮计），开句柄是微秒档。
    dk.resize(dkLen);
    st = ::BCryptDeriveKeyPBKDF2(
        alg,
        (PUCHAR)password.constData(), ULONG(password.size()),
        (PUCHAR)salt.constData(), ULONG(salt.size()),
        quint64(iterations),
        (PUCHAR)dk.data(), ULONG(dkLen), 0);
    ::BCryptCloseAlgorithmProvider(alg, 0);
    return BCRYPT_SUCCESS(st);
}
#else
// 非 Windows 面（本仓实际不构建到此）：显式 CNG 体一律失败，公共三枚直落自实现。
void warnCngFallbackOnce() {}
bool cngEcbBlock(const QByteArray &, const QByteArray &, QByteArray &) { return false; }
bool cngCtr(const QByteArray &, const QByteArray &, const QByteArray &, QByteArray &) { return false; }
bool cngPbkdf2HmacSha256(const QByteArray &, const QByteArray &, int, int, QByteArray &) { return false; }
#endif

} // namespace

// —— 对照腿显式点名声：成功返回 true，生产路径不直接调（公共三枚负责回退与尺寸口径）——
bool aes256EcbBlockCng(const QByteArray &key, const QByteArray &block, QByteArray &out) {
    out.clear();
    if (key.size() != 32 || block.size() != 16)
        return false;
    return cngEcbBlock(key, block, out);
}

bool aes256CtrCng(const QByteArray &key, const QByteArray &iv,
                  const QByteArray &in, QByteArray &out) {
    out.clear();
    if (key.size() != 32 || iv.size() != 16)
        return false;
    return cngCtr(key, iv, in, out);
}

bool pbkdf2HmacSha256Cng(const QByteArray &password, const QByteArray &salt,
                         int iterations, int dkLen, QByteArray &dk) {
    if (dkLen <= 0 || iterations <= 0)
        return false;
    return cngPbkdf2HmacSha256(password, salt, iterations, dkLen, dk);
}

void aes256EcbBlockHandwritten(const QByteArray &key, const QByteArray &block,
                               QByteArray &out) {
    out.clear();
    if (key.size() != 32 || block.size() != 16)
        return;
    handEcbBlock16(key, block, out);
}

void aes256CtrHandwritten(const QByteArray &key, const QByteArray &iv,
                          const QByteArray &in, QByteArray &out) {
    out.clear();
    if (key.size() != 32 || iv.size() != 16)
        return;
    handCtr(key, iv, in, out);
}

QByteArray pbkdf2HmacSha256Handwritten(const QByteArray &password, const QByteArray &salt,
                                       int iterations, int dkLen) {
    return handPbkdf2HmacSha256(password, salt, iterations, dkLen);
}

void aes256EcbBlock(const QByteArray &key, const QByteArray &block,
                    QByteArray &out) {
    out.clear();
    if (key.size() != 32 || block.size() != 16)
        return;                                     // 尺寸口径与自实现一致：空出
#if defined(Q_OS_WIN)
    QByteArray viaCng;
    if (cngEcbBlock(key, block, viaCng)) {
        out = viaCng;
        return;
    }
    warnCngFallbackOnce();
#endif
    handEcbBlock16(key, block, out);
}

void aes256Ctr(const QByteArray &key, const QByteArray &iv,
               const QByteArray &in, QByteArray &out) {
    out.clear();
    if (key.size() != 32 || iv.size() != 16)
        return;
#if defined(Q_OS_WIN)
    if (cngCtr(key, iv, in, out))
        return;
    warnCngFallbackOnce();
#endif
    handCtr(key, iv, in, out);
}

QByteArray hmacSha256(const QByteArray &key, const QByteArray &message) {
    constexpr int kBlock = 64;
    QByteArray k = key;
    if (k.size() > kBlock)
        k = QCryptographicHash::hash(k, QCryptographicHash::Sha256);
    if (k.size() < kBlock)
        k = k.leftJustified(kBlock, char(0));

    QByteArray ikey(kBlock, char(0));
    QByteArray okey(kBlock, char(0));
    for (int i = 0; i < kBlock; ++i) {
        ikey[i] = static_cast<char>(k[i] ^ 0x36);
        okey[i] = static_cast<char>(k[i] ^ 0x5c);
    }
    const QByteArray inner = QCryptographicHash::hash(ikey + message, QCryptographicHash::Sha256);
    return QCryptographicHash::hash(okey + inner, QCryptographicHash::Sha256);
}

bool constTimeEquals(const QByteArray &a, const QByteArray &b) {
    // 长度本身不保密，长度差先折进累计；内容按较长侧轮数逐字节异或，差异逐位进同一只读数。
    // 短侧不判空补齐是 N3 的钉：补零会把「短串加零尾巴」读成等值（前缀哈希截断一类的碰撞面）。
    unsigned diff = (a.size() != b.size()) ? 1u : 0u;
    const int n = qMax(a.size(), b.size());
    for (int i = 0; i < n; ++i) {
        const quint8 x = i < a.size() ? static_cast<quint8>(a[i]) : quint8(0);
        const quint8 y = i < b.size() ? static_cast<quint8>(b[i]) : quint8(0);
        diff |= static_cast<unsigned>(x ^ y);
    }
    return diff == 0;
}

QByteArray pbkdf2HmacSha256(const QByteArray &password, const QByteArray &salt,
                            int iterations, int dkLen) {
#if defined(Q_OS_WIN)
    // 轮数 ≤0 与 dkLen ≤0 的怪形仍交给自实现（语义同现状），CNG 只接合法区间的活。
    if (dkLen > 0 && iterations > 0) {
        QByteArray viaCng;
        if (cngPbkdf2HmacSha256(password, salt, iterations, dkLen, viaCng))
            return viaCng;
        warnCngFallbackOnce();
    }
#endif
    return handPbkdf2HmacSha256(password, salt, iterations, dkLen);
}

void deriveKeys(const QByteArray &password, const QByteArray &salt,
                int iterations, QByteArray &encKey, QByteArray &macKey) {
    const QByteArray dk = pbkdf2HmacSha256(password, salt, iterations, 64);
    encKey = dk.left(32);
    macKey = dk.mid(32, 32);
}

} // namespace SchemeCrypto

#pragma once
// 方案信封格式：在明文方案 JSON 之外包一层（magic / 版本 / 只读标志 /
// 盐 / 迭代次数 / IV / 密文 / HMAC），实现"加密 + 防篡改 + 只读导出"。
//
// 版本面（176 行）：v1 加密信封（原状不动）与 v1 明文打包（旧读者/旧写者历史格式，
// 无完整性位，读侧加注）；v2 = 明文打包带随机盐与 HMAC 完整性位，写侧自此只发 v2。
// 老方案仍是裸 JSON（.vfp），loadProject 自动识别：以 "VFPE" 开头即信封。
// 分层语义：v2 的 pepper 内嵌于程序本体，这层校验位防"误毁/漏改/盲改"（静默篡改
// 能被当场读红），不防"知情者拿同一 pepper 重签"；要后者请发加密信封（MAC 密钥
// 从口令派生，文件里没有）。旧 reader 遇 v2 按"版本不支持"显式拒收——与迭代值
// 值检同一拆版本原则：拆版本要显式，不做静默错解。

#include <QByteArray>

class SchemePackage {
public:
    static const char kMagic[4]; // "VFPE"
    static constexpr quint8 kFormatVersion = 1;         // v1：加密信封 / 旧明文打包
    static constexpr quint8 kPlaintextFormatVersion = 2; // v2：明文打包 + 盐 + HMAC

    // 加密 + 只读标志，输出信封字节。passphrase 为空时发 v2 明文打包（带完整性位）。
    static QByteArray exportScheme(const QByteArray &jsonPayload,
                                   const QString &passphrase,
                                   bool readonly);

    // 解析信封。成功时 *ok=true、*readonly 取标志、返回明文 JSON；失败 *ok=false。
    // passphrase 仅在信封加密时需提供。
    // *integrityMissing（可空）：成功返回且文件不带完整性校验位（v1 明文打包）时置
    // true，供读侧加注；加密信封与 v2 恒为 false。失败路径不承诺该位。
    static QByteArray importScheme(const QByteArray &envelope,
                                   const QString &passphrase,
                                   bool *readonly, bool *ok,
                                   bool *integrityMissing = nullptr);

    // 是否本模块定义的信封（用于 loadProject 自动分流）。
    static bool isEnvelope(const QByteArray &data);

    // 信封是否加密（需口令才能解密；用于打开前判断要不要弹入口令框）。
    static bool isEncryptedEnvelope(const QByteArray &data);

    // 不解密、仅读出只读标志（用于加载前提示）。非信封返回 false。
    static bool peekReadonly(const QByteArray &data);

private:
    static constexpr int kIterations = 100000;
    // 进口值检：信封头的 iterations 只是数据，不是"写多少就给你多少算力"。
    // 恶意文件可把它写成 0x7FFFFFFF，而进口先派生密钥后验 MAC（派生在验之前），
    // 一个 2^31 次迭代的 PBKDF2 会把 GUI 线程冻住小时级 = 打开方案的口令框点是僵死。
    // 本版格式的写侧只发过 kIterations 这一个值：进口对任何其它值（含限幅内的任何
    // 非本值）一律在读出后立即拒收，一行派生都不付——限幅后派生的旧修法仍会把
    // 上限额度（≈10s）付给恶意文件，先值检把这条 G 路上最后一笔代价也退掉。
    // 前向余地：未来若抬写侧默认，须同批改 kIterations 与 kFormatVersion（旧读者
    // 按值检拒收新文件是显式拆版本，不是静默错解）。
    static constexpr quint32 kIterationsValid = static_cast<quint32>(kIterations);
    static constexpr quint8 kFlagEncrypted = 0x01;
    static constexpr quint8 kFlagReadonly  = 0x02;

    static void putUint32BE(QByteArray &out, quint32 v);
    static bool getUint32BE(const char *p, quint32 &v);
};

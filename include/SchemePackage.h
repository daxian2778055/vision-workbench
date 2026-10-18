#pragma once
// 方案信封格式：在明文方案 JSON 之外包一层（magic / 版本 / 只读标志 /
// 盐 / 迭代次数 / IV / 密文 / HMAC），实现"加密 + 防篡改 + 只读导出"。
//
// 老方案仍是裸 JSON（.vfp），loadProject 自动识别：以 "VFPE" 开头即信封。

#include <QByteArray>

class SchemePackage {
public:
    static const char kMagic[4]; // "VFPE"
    static constexpr quint8 kFormatVersion = 1;

    // 加密 + 只读标志，输出信封字节。passphrase 为空时仅做"只读打包"（不加密、无 MAC，
    // 仍带只读标志，便于只读下发但不增加口令负担）。
    static QByteArray exportScheme(const QByteArray &jsonPayload,
                                   const QString &passphrase,
                                   bool readonly);

    // 解析信封。成功时 *ok=true、*readonly 取标志、返回明文 JSON；失败 *ok=false。
    // passphrase 仅在信封加密时需提供。
    static QByteArray importScheme(const QByteArray &envelope,
                                   const QString &passphrase,
                                   bool *readonly, bool *ok);

    // 是否本模块定义的信封（用于 loadProject 自动分流）。
    static bool isEnvelope(const QByteArray &data);

    // 信封是否加密（需口令才能解密；用于打开前判断要不要弹入口令框）。
    static bool isEncryptedEnvelope(const QByteArray &data);

    // 不解密、仅读出只读标志（用于加载前提示）。非信封返回 false。
    static bool peekReadonly(const QByteArray &data);

private:
    static constexpr int kIterations = 100000;
    // 进口限幅：信封头的 iterations 只是数据，不是"写多少就给你多少算力"。
    // 恶意文件可把它写成 0x7FFFFFFF，而进口先派生密钥后验 MAC（派生在验之前），
    // 一个 2^31 次迭代的 PBKDF2 会把 GUI 线程冻住小时级 = 打开方案的口令框点是僵死。
    // 限幅 [Min, Max] 后派生的密钥与原文件 MAC（按原值派生）不同 ⇒ 超幅文件
    // 在支付 1 次上限额度的代价后被原样拒收；writer 恒写 100000，幅内行情不变。
    static constexpr quint32 kIterationsMin = 1;
    static constexpr quint32 kIterationsMax = 1000000; // 写侧 10 倍，留未来上调余量
    static constexpr quint8 kFlagEncrypted = 0x01;
    static constexpr quint8 kFlagReadonly  = 0x02;

    static void putUint32BE(QByteArray &out, quint32 v);
    static bool getUint32BE(const char *p, quint32 &v);
};

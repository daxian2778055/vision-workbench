#pragma once
// 方案加密底座：AES-256-CTR + PBKDF2-HMAC-SHA256。
// 纯自包含实现，零第三方依赖（离线环境、避免许可风险）。
// 算法标准、实现可单测：envelope round-trip / 错误口令拒绝 / 篡改拒绝。
//
// 设计定位：方案文件的"防随手读取/防篡改 + 只读管控"保护层。
// 它不是为高对抗场景设计（那需要硬件密钥/白盒），目标是让现场部署的方案
// 不被轻易反编译编辑、且能以只读形态下发运行。

#include <QByteArray>

namespace SchemeCrypto {

// AES-256-CTR 加解密（CTR 模式加解密同构）。
// key 必须为 32 字节；iv 为 16 字节初始计数器（按 128 位大端自增）。
void aes256Ctr(const QByteArray &key, const QByteArray &iv,
               const QByteArray &in, QByteArray &out);

// AES-256 单块 ECB 加密（仅用于测试/已知答案验证；正常信封流程走 CTR）。
void aes256EcbBlock(const QByteArray &key, const QByteArray &block,
                    QByteArray &out);

// HMAC-SHA256（基于 Qt 的 QCryptographicHash，块长 64）。
QByteArray hmacSha256(const QByteArray &key, const QByteArray &message);

// PBKDF2-HMAC-SHA256，输出 dkLen 字节（默认 64 = 加密密钥 32 + MAC 密钥 32）。
QByteArray pbkdf2HmacSha256(const QByteArray &password, const QByteArray &salt,
                            int iterations, int dkLen = 64);

// 便捷：从口令派生 (encKey32, macKey32)。
void deriveKeys(const QByteArray &password, const QByteArray &salt,
                int iterations, QByteArray &encKey, QByteArray &macKey);

} // namespace SchemeCrypto

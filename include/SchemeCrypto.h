#pragma once
// 方案加密底座：AES-256-CTR + PBKDF2-HMAC-SHA256。
// 迁移态（172 行）：主路径走 Windows CNG（BCrypt——操作系统部件，不是新第三方包）；
// 自实现以 *_Handwritten 名义留场：逐位对照腿专用 + CNG 失败时的同字节兜底。
// 算法标准、两端可互证：KAT / envelope round-trip / 错误口令拒绝 / 篡改拒绝 / 双后端对照。
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
// 迁移态（172 行）：本枚走 CNG，自实现版本在同名 _Handwritten。
void aes256EcbBlock(const QByteArray &key, const QByteArray &block,
                    QByteArray &out);

// HMAC-SHA256（基于 Qt 的 QCryptographicHash，块长 64）。
QByteArray hmacSha256(const QByteArray &key, const QByteArray &message);

// PBKDF2-HMAC-SHA256，输出 dkLen 字节（默认 64 = 加密密钥 32 + MAC 密钥 32）。
QByteArray pbkdf2HmacSha256(const QByteArray &password, const QByteArray &salt,
                            int iterations, int dkLen = 64);

// 迁移态（172 行）：公共三枚改走 CNG；CNG 失败时退回自实现同字节兜底（qWarning 去重告警），
// 两只后端逐位一致由 scheme_package_test 的对照腿钉死。显式点名 CNG 的三枚（*_Cng）是对照腿
// 的检定口——生产路径走公共三枚，不直接调它们。
void aes256EcbBlockHandwritten(const QByteArray &key, const QByteArray &block,
                               QByteArray &out);
void aes256CtrHandwritten(const QByteArray &key, const QByteArray &iv,
                          const QByteArray &in, QByteArray &out);
QByteArray pbkdf2HmacSha256Handwritten(const QByteArray &password, const QByteArray &salt,
                                       int iterations, int dkLen = 64);
bool aes256EcbBlockCng(const QByteArray &key, const QByteArray &block, QByteArray &out);
bool aes256CtrCng(const QByteArray &key, const QByteArray &iv,
                  const QByteArray &in, QByteArray &out);
bool pbkdf2HmacSha256Cng(const QByteArray &password, const QByteArray &salt,
                         int iterations, int dkLen, QByteArray &dk);

// 恒时等值比较：按较长侧长度定轮数，逐字节异或累计后一次判读，差异位置不进耗时。
// 用在 MAC/密钥摘要两件"不该把差异位置泄露给耗时"的场合；长度本身不参与保密。
bool constTimeEquals(const QByteArray &a, const QByteArray &b);

// 便捷：从口令派生 (encKey32, macKey32)。
void deriveKeys(const QByteArray &password, const QByteArray &salt,
                int iterations, QByteArray &encKey, QByteArray &macKey);

} // namespace SchemeCrypto

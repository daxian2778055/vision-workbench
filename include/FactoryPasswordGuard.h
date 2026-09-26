#pragma once

#include <QString>

/// 出厂默认口令治理（A1-①「强制首装改密 + 写操作闸」）。
///
/// 背景：AppDatabase 建表时会自动插入 admin/admin（老库升级兼容路径），于是"出厂口令仍在使用"
/// 是绝大多数现场的默认态。历史实现只在登录后弹一次告警就放行，等于把"无鉴权"写进了运行态。
///
/// 本单元只做判定与落库，不碰任何控件：对话框只负责收三个字符串，
/// 这样闸的语义可以脱离 GUI 单测（QtTest 无头跑）。
namespace FactoryPasswordGuard {

/// 出厂口令当前是否仍在使用（admin 账户的哈希仍能匹配出厂口令）。
bool factoryPasswordInUse();

/// 按数据库实际状态刷新 SessionManager 的写操作闸，返回刷新后闸是否开启。
/// 冷启动必须在登录成功后调用一次；改密成功后再调用一次即可自动解锁。
bool refreshWritesLock();

/// 用新口令替换出厂口令：先校验调用方确实持有出厂口令，再校验新口令，最后落库并复检。
/// 全部通过才解锁写操作闸并返回 true；任何一步失败都返回 false、闸保持开启，
/// 并把原因写进 error（error 可为 nullptr）。
bool retireFactoryPassword(const QString &factoryPasswordInput,
                           const QString &newPassword,
                           const QString &confirmPassword,
                           QString *error);
}

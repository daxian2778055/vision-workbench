#include "FactoryPasswordGuard.h"

#include "AppDatabase.h"
#include "SessionManager.h"

namespace {

/// 口令长度下限：与 UserLoginDialog::initFirstAdmin（首次初始化管理员）保持同一口径，
/// 两处不一致会出现"首装能设的口令，改密时反而不让设"。
constexpr int kMinPasswordLength = 6;

const QString kFactoryUser = QStringLiteral("admin");
const QString kFactoryPassword = QStringLiteral("admin");

}

namespace FactoryPasswordGuard {

bool factoryPasswordInUse()
{
    return AppDatabase::instance()->isFactoryAdminPasswordInUse();
}

bool refreshWritesLock()
{
    const bool inUse = factoryPasswordInUse();
    SessionManager::instance()->setWritesBlocked(inUse);
    return inUse;
}

bool retireFactoryPassword(const QString &factoryPasswordInput,
                           const QString &newPassword,
                           const QString &confirmPassword,
                           QString *error)
{
    auto fail = [error](const QString &reason) {
        if (error) {
            *error = reason;
        }
        return false;
    };

    auto *db = AppDatabase::instance();
    if (!factoryPasswordInUse()) {
        // 已经不在出厂口令上了：不需要改，顺手把闸放掉（幂等，不报错）
        SessionManager::instance()->setWritesBlocked(false);
        return true;
    }

    // 必须由"确实持有出厂口令的人"来改：否则任何一次登录（哪怕 Operator）
    // 都能顺手改掉 admin 的口令，那是改密接口自身的越权。
    if (!db->authenticateUser(kFactoryUser, factoryPasswordInput)) {
        return fail(QStringLiteral("出厂口令校验失败：请填写当前的 admin 口令。"));
    }

    if (newPassword.length() < kMinPasswordLength) {
        return fail(QStringLiteral("新口令至少 %1 位。").arg(kMinPasswordLength));
    }
    if (newPassword == kFactoryPassword) {
        return fail(QStringLiteral("新口令不能仍然是出厂口令。"));
    }
    if (newPassword != confirmPassword) {
        return fail(QStringLiteral("两次输入的新口令不一致。"));
    }

    if (!db->changeUserPassword(kFactoryUser, newPassword)) {
        return fail(QStringLiteral("口令写入数据库失败：请检查数据库可写后重试。"));
    }

    // 复检而不是"改了就算"：哈希格式/旧版迁移等任何一步没落实，闸都不能放开
    if (refreshWritesLock()) {
        return fail(QStringLiteral("口令已修改，但仍被判定为出厂口令在用，写操作保持禁止。"));
    }

    db->logOperation(SessionManager::instance()->currentUser(),
                     QStringLiteral("修改管理员口令"),
                     QStringLiteral("出厂口令已停用，写操作闸解除"));
    return true;
}

}

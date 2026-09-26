// A1-①「出厂默认口令 → 写操作闸」的行为锁定测试（纯逻辑，不起 GUI）
//
// 为什么需要它：改密之前的实现是"登录后弹一次告警然后照常放行"，而且全仓没有任何
// 修改口令的接口（用户管理只能增删），那条告警在现场根本无法执行——等于出厂口令永久有效。
// 现在收口为：出厂口令仍在用 ⇒ 配置写入类权限一律为假 + 方案落盘被持久化层拒绝，
// 直到 admin 的口令被真正改掉。本套件锁住两件事：
//   1) 闸的覆盖面（Admin 也拦、运行不拦）与 fail-closed 方向——三条写入路径都要有直接
//      断言：saveProject、exportEncryptedProject、用户表 addUser/removeUser（W-1① 补）；
//   2) 解锁路径确实有效：改密成功后出厂口令判定消失、方案能落盘。
// 未覆盖（如实写明）：ProjectManager::saveProjectInteractive() 的拒绝路径先弹
// QMessageBox（模态），无头环境跑不动 ⇒ 不断言它；改为锁住它内部调用的同一条判据
// （writeGateRefusal_isTheSinglePredicate），弹框与顺序由代码走查保证。
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFileInfo>

#include "AppDatabase.h"
#include "SessionManager.h"
#include "FactoryPasswordGuard.h"
#include "ProjectManager.h"

class FactoryPasswordGuardTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void writesLock_disablesConfigPermissionsEvenForAdmin();
    void writesLock_doesNotDisableRunFlow();
    void retire_refusesWhenFactoryPasswordWrong();
    void retire_rejectsWeakOrMismatchedNewPassword();
    void saveProject_refusedWhileWritesBlocked();
    void exportEncryptedProject_refusedWhileWritesBlocked();
    void writeGateRefusal_isTheSinglePredicate();
    void userTableWrites_refusedWhileWritesBlocked();
    void retire_unlocksWritesOnSuccess();
    void saveProject_allowedAfterRetire();
    void userTableWrites_allowedAfterRetire();
    void changePassword_failsForUnknownUser();

private:
    QTemporaryDir m_tempDir;
};

void FactoryPasswordGuardTest::initTestCase()
{
    QVERIFY2(m_tempDir.isValid(), "无法创建临时目录");
    // 独立临时库：AppDatabase 建表时会自动插入 admin/admin，正是本套件要验证的默认态
    QVERIFY2(AppDatabase::instance()->initialize(m_tempDir.filePath(QStringLiteral("guard.db"))),
             "数据库初始化失败");
    QVERIFY2(AppDatabase::instance()->isFactoryAdminPasswordInUse(),
             "新建库应处于出厂口令状态（否则本套件的断言全部失去前提）");
}

void FactoryPasswordGuardTest::writesLock_disablesConfigPermissionsEvenForAdmin()
{
    auto *s = SessionManager::instance();
    s->login(QStringLiteral("tester"), QStringLiteral("Admin"));

    s->setWritesBlocked(false);
    QVERIFY(s->canEditScheme());
    QVERIFY(s->canManageUsers());
    QVERIFY(s->canConfigCommunications());

    s->setWritesBlocked(true);
    QVERIFY2(!s->canEditScheme(), "闸开启后 Admin 仍可编辑方案：闸未接进权限判断");
    QVERIFY2(!s->canManageUsers(), "闸开启后仍可增删用户：拿到出厂口令就能建后门账户");
    QVERIFY2(!s->canConfigCommunications(), "闸开启后仍可改通信配置");

    s->setWritesBlocked(false);
}

// 闸的边界（有意为之，不是漏）：产线正在跑的活不因未改口令而中断
void FactoryPasswordGuardTest::writesLock_doesNotDisableRunFlow()
{
    auto *s = SessionManager::instance();
    s->login(QStringLiteral("operator"), QStringLiteral("Operator"));
    s->setWritesBlocked(true);
    QVERIFY2(s->canRunFlow(), "写操作闸不应阻断流程运行（它只拦配置写入）");
    s->setWritesBlocked(false);
}

void FactoryPasswordGuardTest::retire_refusesWhenFactoryPasswordWrong()
{
    QVERIFY2(FactoryPasswordGuard::refreshWritesLock(),
             "本用例前提：出厂口令仍在使用，闸应处于开启态");

    QString error;
    const bool ok = FactoryPasswordGuard::retireFactoryPassword(
        QStringLiteral("not-the-factory-password"),
        QStringLiteral("N3wStrongPass"), QStringLiteral("N3wStrongPass"), &error);

    QVERIFY2(!ok, "出厂口令填错却改密成功：任何人都能替 admin 改口令");
    QVERIFY2(!error.isEmpty(), "失败必须给出原因，不能静默");
    QVERIFY2(SessionManager::instance()->writesBlocked(), "改密失败后闸必须仍然开启");
    QVERIFY(AppDatabase::instance()->isFactoryAdminPasswordInUse());
}

void FactoryPasswordGuardTest::retire_rejectsWeakOrMismatchedNewPassword()
{
    QVERIFY(FactoryPasswordGuard::refreshWritesLock());

    QString error;
    // 过短
    QVERIFY(!FactoryPasswordGuard::retireFactoryPassword(
        QStringLiteral("admin"), QStringLiteral("123"), QStringLiteral("123"), &error));
    QVERIFY(!error.isEmpty());
    // 两次输入不一致
    QVERIFY(!FactoryPasswordGuard::retireFactoryPassword(
        QStringLiteral("admin"), QStringLiteral("N3wStrongPass"), QStringLiteral("N3wOtherPass"), &error));
    QVERIFY(!error.isEmpty());
    // 改来改去还是出厂口令（绕一圈等于没改）
    QVERIFY(!FactoryPasswordGuard::retireFactoryPassword(
        QStringLiteral("admin"), QStringLiteral("admin"), QStringLiteral("admin"), &error));
    QVERIFY(!error.isEmpty());

    QVERIFY2(AppDatabase::instance()->isFactoryAdminPasswordInUse(),
             "三次非法输入后口令不应被改动");
    QVERIFY2(SessionManager::instance()->writesBlocked(), "失败路径不得解锁");
}

void FactoryPasswordGuardTest::saveProject_refusedWhileWritesBlocked()
{
    QVERIFY(FactoryPasswordGuard::refreshWritesLock());
    SessionManager::instance()->setWritesBlocked(true);

    const QString path = m_tempDir.filePath(QStringLiteral("blocked.vfp"));
    ProjectManager pm;
    QVERIFY2(!pm.saveProject(path, {}), "闸开启时方案仍能落盘：持久化层未收口");
    QVERIFY2(!QFileInfo::exists(path), "拒绝后不得留下（哪怕是空的）方案文件");
}

// S-3：三条方案/账户写入路径各是独立的一处代码，只测一处等于另外两处可以被人删掉而全绿。
void FactoryPasswordGuardTest::exportEncryptedProject_refusedWhileWritesBlocked()
{
    QVERIFY(FactoryPasswordGuard::refreshWritesLock());
    SessionManager::instance()->setWritesBlocked(true);

    const QString path = m_tempDir.filePath(QStringLiteral("blocked.vfps"));
    ProjectManager pm;
    QVERIFY2(!pm.exportEncryptedProject(path, {}, QStringLiteral("P@ssphrase-1"), false),
             "闸开启时加密方案仍能导出：持久化层未收口");
    QVERIFY2(!QFileInfo::exists(path), "拒绝后不得留下（哪怕是空的）导出包");
}

// W-1：所有写入路径与菜单可用态必须取同一个理由串。判据一旦各写一份，
// "文案说禁了、代码没禁"就会重演（本用例把这条锁住）。
void FactoryPasswordGuardTest::writeGateRefusal_isTheSinglePredicate()
{
    auto *s = SessionManager::instance();

    s->setWritesBlocked(false);
    QVERIFY2(s->writeGateRefusal().isEmpty(), "闸关闭时判据必须放行（返回非空即拒绝一切写入）");

    s->setWritesBlocked(true);
    const QString refusal = s->writeGateRefusal();
    QVERIFY2(!refusal.isEmpty(), "闸开启时判据必须给出拒绝理由");
    QVERIFY2(refusal.contains(QStringLiteral("admin/admin")),
             "理由要点明是哪把口令没换，否则用户不知道该做什么");

    s->login(QStringLiteral("root"), QStringLiteral("Admin"));
    QVERIFY(!s->canEditScheme());
    QVERIFY(!s->canManageUsers());
    QVERIFY2(!s->canConfigCommunications(),
             "W-1②：该谓词已接进 MainWindow 的通信菜单，判据不得再退化成死码");
    s->setWritesBlocked(false);
}

// W-1①：闸此前只管方案文件；用户管理只灰菜单，而改密弹框一直宣称"用户管理已被禁止"
void FactoryPasswordGuardTest::userTableWrites_refusedWhileWritesBlocked()
{
    QVERIFY(FactoryPasswordGuard::refreshWritesLock());
    SessionManager::instance()->setWritesBlocked(true);

    QVERIFY2(!AppDatabase::instance()->addUser(QStringLiteral("gate_probe_blocked"),
                                               QStringLiteral("Xy7#mKp2q"), QStringLiteral("Admin")),
             "闸开启时仍能建用户：拿到出厂口令就能开后门账户");
    QVERIFY2(!AppDatabase::instance()->removeUser(QStringLiteral("admin")),
             "闸开启时仍能删用户");

    QStringList names;
    for (const UserRecord &u : AppDatabase::instance()->queryUsers()) {
        names << u.name;
    }
    QVERIFY2(!names.contains(QStringLiteral("gate_probe_blocked")), "拒绝后用户表里不得出现该账户");
    QVERIFY2(names.contains(QStringLiteral("admin")), "拒绝删用户后 admin 必须还在表里");

    SessionManager::instance()->setWritesBlocked(false);
}

void FactoryPasswordGuardTest::retire_unlocksWritesOnSuccess()
{
    QVERIFY(FactoryPasswordGuard::refreshWritesLock());

    QString error;
    const bool ok = FactoryPasswordGuard::retireFactoryPassword(
        QStringLiteral("admin"),
        QStringLiteral("N3wStrongPass"), QStringLiteral("N3wStrongPass"), &error);

    QVERIFY2(ok, qPrintable(error.isEmpty() ? QStringLiteral("改密失败且未给出原因") : error));
    QVERIFY2(!SessionManager::instance()->writesBlocked(), "改密成功后写操作闸应自动解除");
    QVERIFY2(!AppDatabase::instance()->isFactoryAdminPasswordInUse(), "出厂口令判定应消失");
    QVERIFY(AppDatabase::instance()->authenticateUser(QStringLiteral("admin"),
                                                     QStringLiteral("N3wStrongPass")));
    QVERIFY2(!AppDatabase::instance()->authenticateUser(QStringLiteral("admin"),
                                                       QStringLiteral("admin")),
             "旧出厂口令仍能登录：哈希未被真正覆盖");
}

// 反向对照：上一用例的"拒绝"不是环境造成的假象——解锁后同一条写路径必须能成功
void FactoryPasswordGuardTest::saveProject_allowedAfterRetire()
{
    QVERIFY2(!FactoryPasswordGuard::refreshWritesLock(),
             "前提：出厂口令已被改掉（依赖 retire_unlocksWritesOnSuccess 先跑）");

    const QString path = m_tempDir.filePath(QStringLiteral("allowed.vfp"));
    ProjectManager pm;
    QVERIFY2(pm.saveProject(path, {}), "解锁后方案保存仍失败：上一用例的拒绝没有说服力");
    QVERIFY2(QFileInfo::exists(path), "方案文件应已生成");
}

// 反向对照：解锁后同一条用户表写路径必须能成功，证明上一条的"拒绝"不是 addUser 本身坏掉
void FactoryPasswordGuardTest::userTableWrites_allowedAfterRetire()
{
    QVERIFY2(AppDatabase::instance()->addUser(QStringLiteral("gate_probe_ok"),
                                              QStringLiteral("Xy7#mKp2q"), QStringLiteral("Operator")),
             "解锁后仍拒绝建用户：上一条用例的拒绝没有说服力");
    QVERIFY(AppDatabase::instance()->removeUser(QStringLiteral("gate_probe_ok")));
}

void FactoryPasswordGuardTest::changePassword_failsForUnknownUser()
{
    // 影响 0 行必须报失败：否则调用方会以为口令已改，闸也就被错误解除
    QVERIFY2(!AppDatabase::instance()->changeUserPassword(
                 QStringLiteral("no_such_user"), QStringLiteral("Whatever123")),
             "给不存在的用户改口令返回了成功");
}

QTEST_GUILESS_MAIN(FactoryPasswordGuardTest)

#include "factory_password_guard_test.moc"

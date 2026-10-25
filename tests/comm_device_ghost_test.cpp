// 未知设备类型不得留下"幽灵设备"（2026-10-25 复核 P3-12）。
//
// 缺陷：CommunicationManager::addDevice 先把设备写进 m_deviceInfos，再调 createDeviceNode；
// 而 createDeviceNode 遇到未知类型什么都不建（直接 return）。于是 addDevice 照样 emit
// deviceAdded + 返回 true ⇒ 方案里留下一个"列表里有、点开报错、永远打不开"的幽灵设备。
//
// 修法：createDeviceNode 改返回 bool；建不出来时 addDevice 回滚登记并返回 false。
#include "CommunicationManager.h"

#include <QtTest/QtTest>
#include <QJsonObject>

class CommDeviceGhostTest : public QObject
{
    Q_OBJECT
private slots:
    void unknownTypeIsRejectedAndLeavesNoGhost();
    void knownTypeStillAddsDevice();
};

void CommDeviceGhostTest::unknownTypeIsRejectedAndLeavesNoGhost()
{
    auto *mgr = CommunicationManager::instance();
    const QString name = QStringLiteral("ghost_probe_device");
    // 前置：同名设备不应存在（上一个用例残留就清掉）
    if (mgr->hasDevice(name))
        mgr->removeDevice(name);

    bool addedEmitted = false;
    // 捕获按值 + 用后断开（2026-10-25 复核）：单例上的连接若捕获局部变量引用且不断开，
    // 下一个用例发 deviceAdded 时引用已悬空（UB）。name 是局部 QString，按值捕获；
    // 连接在用例末尾 disconnect。
    const QMetaObject::Connection conn =
        QObject::connect(mgr, &CommunicationManager::deviceAdded, mgr,
                         [&addedEmitted, name](const QString &n, const QString &) {
                             if (n == name)
                                 addedEmitted = true;
                         });

    // 未知类型：必须 false，且不留登记、不发 deviceAdded
    const bool ok = mgr->addDevice(name, QStringLiteral("NoSuchType"), QJsonObject());
    QVERIFY2(!ok, "未知设备类型被当成添加成功了（幽灵设备，P3-12）");
    QVERIFY2(!mgr->hasDevice(name), "未知类型仍在设备表里：幽灵设备残留");
    QVERIFY2(!addedEmitted, "未知类型也发出了 deviceAdded：界面会出现一个打不开的设备");
    QObject::disconnect(conn);
}

void CommDeviceGhostTest::knownTypeStillAddsDevice()
{
    auto *mgr = CommunicationManager::instance();
    const QString name = QStringLiteral("ghost_probe_tcp");
    if (mgr->hasDevice(name))
        mgr->removeDevice(name);

    // 已知类型（TCP）：照旧成功（修复不能把正常添加也挡掉）
    QVERIFY2(mgr->addDevice(name, QStringLiteral("TCP"), QJsonObject()),
             "已知类型 TCP 添加失败：修复把正常路径也挡掉了");
    QVERIFY(mgr->hasDevice(name));
    mgr->removeDevice(name);
}

QTEST_MAIN(CommDeviceGhostTest)
#include "comm_device_ghost_test.moc"

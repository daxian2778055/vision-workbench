// 相机 IO 控制功能测试（🥉：MVS 数字 IO 节点逻辑 + 序列化 + 缺相机优雅降级）
// 不依赖真实相机：IO 硬件调用在无相机时由 GlobalCameraManager 返回 false，节点应优雅降级。
#include <QtTest/QtTest>
#include <QObject>
#include <QJsonObject>
#include <QString>

#include "CameraIoNode.h"
#include "DataObject.h"
#include "Port.h"

class CameraIoTest : public QObject
{
    Q_OBJECT
private slots:
    void testInitPortsAndParams();
    void testSerialization();
    void testMissingCameraName();
    void testCameraNotOpen();
    void testWriteRequiresOutputMode();
};

void CameraIoTest::testInitPortsAndParams()
{
    CameraIoNode node;
    node.init();
    // 注：B1 统一输出后只剩一个「输出」端口；值/成功/错误是它上面的具名子结果
    // B2 单输入契约：画布只有 1 个输入点「执行」；写值改为逻辑槽（订阅绑定注入）。
    QCOMPARE(node.inputPorts().size(), 1);
    QCOMPARE(node.outputPorts().size(), 1);
    QVERIFY(node.hasParam(QStringLiteral("cameraName")));
    QVERIFY(node.hasParam(QStringLiteral("lineIndex")));
    QVERIFY(node.hasParam(QStringLiteral("lineMode")));
    QVERIFY(node.hasParam(QStringLiteral("action")));
    QVERIFY(node.hasParam(QStringLiteral("writeValue")));
    QCOMPARE(node.getParam(QStringLiteral("lineMode")).toInt(), 1);
    QCOMPARE(node.getParam(QStringLiteral("action")).toInt(), 1);
    // 折叠后的子输出：主端口「值」= Bool；成功 = Bool，错误 = String（按名取值仍能对上）
    QCOMPARE(node.outputPorts().first()->name(), QStringLiteral("值"));
    const auto logical = node.logicalOutputs();
    QCOMPARE(logical.size(), 2);
    QCOMPARE(logical.at(0).first, QStringLiteral("成功"));
    QCOMPARE(static_cast<int>(logical.at(0).second), static_cast<int>(PortDataType::Bool));
    QCOMPARE(logical.at(1).first, QStringLiteral("错误"));
    QCOMPARE(static_cast<int>(logical.at(1).second), static_cast<int>(PortDataType::String));
    // 输入端口：执行 = Any；写值在逻辑槽 1（Bool），不占画布点
    QCOMPARE(node.inputPorts()[0]->name(), QStringLiteral("执行"));
    QCOMPARE(static_cast<int>(node.inputPorts()[0]->dataType()), static_cast<int>(PortDataType::Any));
    QCOMPARE(node.logicalInputPortCount(), 2);
    QCOMPARE(node.inputSlotName(1), QStringLiteral("值"));
}

void CameraIoTest::testSerialization()
{
    CameraIoNode n1;
    n1.init();
    n1.setParam(QStringLiteral("cameraName"), QStringLiteral("Camera 1"));
    n1.setParam(QStringLiteral("lineIndex"), 2);
    n1.setParam(QStringLiteral("action"), 0);   // 读
    n1.setParam(QStringLiteral("writeValue"), true);
    QJsonObject j = n1.toJson();

    CameraIoNode n2;
    n2.init();
    n2.fromJson(j);
    QCOMPARE(n2.getParam(QStringLiteral("cameraName")).toString(), QStringLiteral("Camera 1"));
    QCOMPARE(n2.getParam(QStringLiteral("lineIndex")).toInt(), 2);
    QCOMPARE(n2.getParam(QStringLiteral("action")).toInt(), 0);
    QCOMPARE(n2.getParam(QStringLiteral("writeValue")).toBool(), true);
}

void CameraIoTest::testMissingCameraName()
{
    CameraIoNode node;
    node.init();
    node.run();   // 未设相机名
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "缺相机名时 moduleStatus 应为 false");
    QCOMPARE(node.getParam(QStringLiteral("lastError")).toString(),
             QStringLiteral("请设置相机名"));
}

void CameraIoTest::testCameraNotOpen()
{
    CameraIoNode node;
    node.init();
    node.setParam(QStringLiteral("cameraName"), QStringLiteral("不存在的相机"));
    node.run();   // 相机未打开/不存在
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "相机未打开时 moduleStatus 应为 false");
    QCOMPARE(node.getParam(QStringLiteral("lastError")).toString(),
             QStringLiteral("相机未打开或不存在"));
}

void CameraIoTest::testWriteRequiresOutputMode()
{
    CameraIoNode node;
    node.init();
    node.setParam(QStringLiteral("cameraName"), QStringLiteral("不存在的相机"));
    node.setParam(QStringLiteral("lineMode"), 0);   // 输入
    node.setParam(QStringLiteral("action"), 1);     // 写
    node.run();   // 写操作但方向=输入，参数级校验应先于相机在线检查
    QVERIFY2(!node.getParam(QStringLiteral("moduleStatus")).toBool(),
             "写操作方向=输入时 moduleStatus 应为 false");
    QCOMPARE(node.getParam(QStringLiteral("lastError")).toString(),
             QStringLiteral("写入操作要求线方向=输出(1)"));
}

QTEST_MAIN(CameraIoTest)
#include "camera_io_test.moc"

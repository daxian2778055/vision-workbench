// VM 4.4「工具 → 标定配置引导」数据面验证：三类官方类型、静态标定的算子链
// 必须真实注册、缺算子的类型不得伪造流程。
#include <QtTest>

#include "CalibrationWizard.h"
#include "CalibrationWizardDialog.h"
#include "NodeRegistry.h"

#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QRadioButton>

class CalibrationWizardTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testThreeOfficialTypes();
    void testDialogShowsOfficialSteps();
    void testStaticFlowChainIsReal();
    void testUnsupportedTypesDoNotFakeFlow();
};

void CalibrationWizardTest::initTestCase()
{
    // 测试进程没有 MainWindow：vfp_core 静态库里 NodeRegistry.cpp 的注册对象
    // 不会被自动拉入，必须显式走一次集中注册，findById 才有内容。
    registerAllNodes();
}

void CalibrationWizardTest::testThreeOfficialTypes()
{
    const QVector<CalibrationWizardSpec> specs = CalibrationWizard::allSpecs();
    QCOMPARE(specs.size(), 3);
    QCOMPARE(specs.at(0).name, QStringLiteral("静态标定"));
    QCOMPARE(specs.at(1).name, QStringLiteral("动态标定"));
    QCOMPARE(specs.at(2).name, QStringLiteral("映射标定"));
    QVERIFY2(specs.at(0).description.contains(QStringLiteral("标定板")),
             "静态标定释义应来自官方：静止单相机 + 标定板");
    QVERIFY2(specs.at(1).description.contains(QStringLiteral("旋转中心")),
             "动态标定释义应来自官方：特征点移动 + 旋转中心");
    QVERIFY2(specs.at(2).description.contains(QStringLiteral("两个相机")),
             "映射标定释义应来自官方：两个相机图像坐标系");
    QCOMPARE(CalibrationWizard::specFor(CalibrationWizardType::Mapping).name,
             QStringLiteral("映射标定"));
}

void CalibrationWizardTest::testStaticFlowChainIsReal()
{
    const CalibrationWizardSpec s =
        CalibrationWizard::specFor(CalibrationWizardType::Static);
    QVERIFY2(s.supported, "静态标定应具备完整算子链");
    QVERIFY(s.unsupportedReason.isEmpty());
    QCOMPARE(s.targetModule, QStringLiteral("标定板标定"));
    QCOMPARE(s.steps.size(), 7);   // 官方静态标定引导子页的 7 个步骤
    QCOMPARE(s.flowNodes.size(), 2);
    QCOMPARE(s.flowNodeIds.size(), s.flowNodes.size());
    QCOMPARE(s.flowNodeIds.at(0), QStringLiteral("ImageReadNode"));
    QCOMPARE(s.flowNodeIds.at(1), QStringLiteral("OpencvCalibNode"));
    // 规格里的注册 ID 必须真的在册，否则「创建」会生成空节点
    for (const QString &id : s.flowNodeIds) {
        QVERIFY2(NodeRegistry::instance().findById(id) != nullptr,
                 qPrintable(QStringLiteral("引导规格引用了未注册类型 ID：%1").arg(id)));
    }
}

void CalibrationWizardTest::testUnsupportedTypesDoNotFakeFlow()
{
    const CalibrationWizardType types[] = {CalibrationWizardType::Dynamic,
                                           CalibrationWizardType::Mapping};
    for (CalibrationWizardType t : types) {
        const CalibrationWizardSpec s = CalibrationWizard::specFor(t);
        QVERIFY2(!s.supported, qPrintable(QStringLiteral("%1 不应标记为可用").arg(s.name)));
        QVERIFY2(s.flowNodes.isEmpty(), "缺算子链的类型不得生成流程");
        QVERIFY2(s.steps.isEmpty(), "缺算子链的类型不得编造引导步骤");
        QVERIFY2(!s.unsupportedReason.isEmpty(), "必须说明缺哪个模块");
        QVERIFY2(!s.targetModule.isEmpty(), "必须给出官方指定的落地模块名");
    }
}

void CalibrationWizardTest::testDialogShowsOfficialSteps()
{
    CalibrationWizardDialog dlg;
    QCOMPARE(dlg.selectedType(), CalibrationWizardType::Static);
    auto *create = dlg.findChild<QPushButton *>(QStringLiteral("wizardCreateButton"));
    QVERIFY(create != nullptr);
    create->click();   // 第一次「创建」进入引导页
    auto *steps = dlg.findChild<QListWidget *>(QStringLiteral("wizardSteps"));
    QVERIFY(steps != nullptr);
    QCOMPARE(steps->count(), 7);
    auto *hint = dlg.findChild<QLabel *>(QStringLiteral("wizardUnsupported"));
    QVERIFY(hint != nullptr);
    QVERIFY2(hint->text().isEmpty(), "静态标定不应显示缺模块说明");

    CalibrationWizardDialog dynamicDlg;
    auto *radio = dynamicDlg.findChild<QRadioButton *>(QStringLiteral("wizardType1"));
    QVERIFY(radio != nullptr);
    radio->setChecked(true);
    QCOMPARE(dynamicDlg.selectedType(), CalibrationWizardType::Dynamic);
    dynamicDlg.findChild<QPushButton *>(QStringLiteral("wizardCreateButton"))->click();
    auto *unsupported =
        dynamicDlg.findChild<QLabel *>(QStringLiteral("wizardUnsupported"));
    QVERIFY(unsupported != nullptr);
    QVERIFY2(!unsupported->text().isEmpty(), "动态标定必须说明缺哪个模块");
    QCOMPARE(dynamicDlg.findChild<QListWidget *>(QStringLiteral("wizardSteps"))->count(), 0);
}

QTEST_MAIN(CalibrationWizardTest)
#include "calibration_wizard_test.moc"

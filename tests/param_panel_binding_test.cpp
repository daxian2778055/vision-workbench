// 参数面板绑定验证（推进计划 §3.6 F-2 的取证 + 契约）。
// 契约：updateParamPanel(panel) 必须把每个参数写回**它自己的**控件，且不得改动别的参数；
//      且刷新只改控件显示、不触发控件变更信号（不回写参数表）。
// 缺陷族：部分节点的 updateParamPanel 用**无名** findChild<T>() 取控件 ⇒ 每次拿到的是同一个
//        （子对象顺序里第一个匹配的）后代，多字段被写进同一控件，并经控件信号把错值回写进节点参数。
// 取证口径：控件一律按**面板内容**识别（下拉项文本 / 创建期初始值），不按 objectName 识别，
//          因此修复前（无名）与修复后（具名）用同一套识别逻辑定位"本应被写入的那个控件"，
//          断言不会因改名而失真；识别本身另有前置断言把守（识别错则先红在"识别前置"上）。
// 说明：不 show() 窗口，因此可在无桌面的 CI（自托管 Windows runner）上稳定运行。
#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QWidget>

#include "ColorConversionNode.h"
#include "HalconImageSourceNode.h"

namespace {

// 按 item 文本唯一识别下拉框（内容签名 ⇒ 与控件创建顺序、objectName 无关）
QComboBox *comboByTexts(QWidget *root, const QStringList &texts)
{
    QList<QComboBox *> hits;
    const QList<QComboBox *> all = root->findChildren<QComboBox *>();
    for (QComboBox *c : all) {
        QStringList its;
        for (int i = 0; i < c->count(); ++i) its << c->itemText(i);
        if (its == texts) hits << c;
    }
    return hits.size() == 1 ? hits.first() : nullptr;
}

// 按**布局顺序**展平面板里的行编辑框（递归进嵌套布局）。
// 布局顺序 = 源码里 addWidget 的顺序 = 开发者意图，与 QObject 子对象顺序无关。
void collectLeafWidgets(QWidget *root, QList<QWidget *> &out)
{
    if (!root || !root->layout()) return;
    QList<QLayoutItem *> stack;
    QLayout *l = root->layout();
    for (int i = l->count() - 1; i >= 0; --i) stack << l->itemAt(i);
    while (!stack.isEmpty()) {
        QLayoutItem *item = stack.takeLast();
        if (!item) continue;
        if (QWidget *w = item->widget()) {
            out << w;
            continue;
        }
        if (QLayout *sub = item->layout()) {
            for (int i = sub->count() - 1; i >= 0; --i) stack << sub->itemAt(i);
        }
    }
}

QList<QLineEdit *> lineEditsInLayoutOrder(QWidget *root)
{
    QList<QWidget *> flat;
    collectLeafWidgets(root, flat);
    QList<QLineEdit *> edits;
    for (QWidget *w : flat) {
        if (QLineEdit *e = qobject_cast<QLineEdit *>(w)) edits << e;
    }
    return edits;
}

} // namespace

class ParamPanelBindingTest : public QObject
{
    Q_OBJECT

private slots:
    void testUnnamedFindChildIsPositional();
    void testColorConversionPanelWritesToOwnWidget();
    void testImageSourcePanelWritesToOwnWidget();
    void testImageSourceRefreshKeepsParamTable();
};

// ── T1 最小复现：把 F-2 的悬空前提变成实测 ──
// 前提内容：无名 findChild<T>() 只按"子对象顺序"返回第一个匹配后代，与语义无关；
//          具名 findChild<T>(name) 才精确命中。第一个控件即便已有 objectName 也救不了后面字段
//          ——因为无名那次调用根本不看名字。
void ParamPanelBindingTest::testUnnamedFindChildIsPositional()
{
    QWidget panel;
    QVBoxLayout *layout = new QVBoxLayout(&panel);

    auto *alpha = new QComboBox();          // 面板里第一个 QComboBox（且已具名）
    alpha->setObjectName(QStringLiteral("alphaCombo"));
    alpha->addItems({QStringLiteral("A0"), QStringLiteral("A1"), QStringLiteral("A2")});
    auto *label = new QLabel(QStringLiteral("中间标签"));
    auto *beta = new QComboBox();           // 第二个：真正的"目标"
    beta->setObjectName(QStringLiteral("betaCombo"));
    beta->addItems({QStringLiteral("B0"), QStringLiteral("B1"), QStringLiteral("B2")});

    layout->addWidget(alpha);
    layout->addWidget(label);
    layout->addWidget(beta);

    // ① 无名查找 ⇒ 命中第一个（alpha），而不是语义上该被找的那个（beta）
    QVERIFY2(panel.findChild<QComboBox *>() == alpha,
             "无名 findChild 命中的不是第一个 QComboBox（本用例的前提被推翻，需重开结论）");
    QVERIFY2(panel.findChild<QComboBox *>() != beta,
             "无名 findChild 精确命中了 beta ⇒ 缺陷族不可能发生");

    // ② 具名查找 ⇒ 精确命中 beta（修复方向的可行性前提）
    QVERIFY2(panel.findChild<QComboBox *>(QStringLiteral("betaCombo")) == beta,
             "具名 findChild 未能命中 beta");

    // ③ findChildren 顺序 = 布局加入顺序（本套件用"创建期初始值/内容签名"识别，不依赖此条，
    //    这里只作为读数记录）
    QCOMPARE(panel.findChildren<QComboBox *>().size(), 2);
    QVERIFY(panel.findChildren<QComboBox *>().first() == alpha);
}

// ── T2 真节点（ColorConversionNode）──
// 面板里两个下拉框：inputImageCombo（第一个）、conversionTypeCombo（第二个）。
// 未修复时 updateParamPanel 用无名 findChild ⇒ 把 conversionType 写进 inputImageCombo。
void ParamPanelBindingTest::testColorConversionPanelWritesToOwnWidget()
{
    auto *node = new ColorConversionNode();
    node->init();
    QWidget *panel = node->createParamPanel();
    QVERIFY2(panel != nullptr, "createParamPanel 返回空");

    // 识别前置：面板里必须恰好两个下拉框，其中一个是六种转换类型
    QCOMPARE(panel->findChildren<QComboBox *>().size(), 2);
    QComboBox *typeCombo = comboByTexts(panel, ColorConversionNode().getAvailableConversions());
    QVERIFY2(typeCombo != nullptr,
             "识别前置失败：未能按内容签名唯一定位『转换类型』下拉框");
    QComboBox *otherCombo = nullptr;
    const QList<QComboBox *> combos = panel->findChildren<QComboBox *>();
    for (QComboBox *c : combos) {
        if (c != typeCombo) otherCombo = c;
    }
    QVERIFY2(otherCombo != nullptr, "识别前置失败：未找到输入图像下拉框");

    // 创建期读数：类型下拉框停在参数表默认值（RGB_TO_GRAY=0）
    QCOMPARE(node->getParam(QStringLiteral("conversionType")).toInt(), 0);
    QCOMPARE(typeCombo->currentIndex(), 0);

    const int inputIndexBefore = otherCombo->currentIndex();

    // 造一次"面板已建好、参数随后被改"的常规场景（改配置 / fromJson / 程序化下发）
    node->setParam(QStringLiteral("conversionType"), 4);   // HSV转RGB
    QCOMPARE(node->getParam(QStringLiteral("conversionType")).toInt(), 4);

    // 契约③探针：程序化刷新不该触发控件变更信号（用 currentTextChanged 避开 Qt6 下 currentIndexChanged 的重载歧义）
    QSignalSpy typeChangedSpy(typeCombo, &QComboBox::currentTextChanged);

    node->updateParamPanel(panel);

    // 契约①：参数值必须出现在**它自己的**控件上
    QCOMPARE(typeCombo->currentIndex(), 4);

    // 契约②：不得顺手挪动别的控件
    QCOMPARE(otherCombo->currentIndex(), inputIndexBefore);

    // 契约③：刷新只写控件，不回写参数表（变更信号数为 0 ⇒ 挂在其上的 lambda 不会被触发）
    QCOMPARE((int)typeChangedSpy.count(), 0);

    // 释放 ColorConversionNode::createParamPanel 里 100ms 的延迟初始化
    // （其 lambda 裸捕获 panel/this，必须在对象存活期内跑完）
    QTest::qWait(200);

    delete panel;
    delete node;
}

// ── T3 真节点（HalconImageSourceNode）──
// **修复前**该面板 createParamPanel 里没有任何 setObjectName，updateParamPanel 八次无名查找
// （4 个 QComboBox + 4 个 QLineEdit）⇒ 五个下拉框的写入全落在第一个（图像源类型）、
// 四个输入框的写入全落在第一个（文件路径）。
void ParamPanelBindingTest::testImageSourcePanelWritesToOwnWidget()
{
    auto *node = new HalconImageSourceNode();

    // 建面板**之前**先把四个行编辑框的值设成互不相同的已知量 ⇒
    // 创建期读数即可反证"按布局顺序的位置识别"没有认错控件。
    const QString pathBefore = QStringLiteral("E:/vfp_u1/before.png");
    node->setParam(QStringLiteral("sourceType"), 0);          // 本地文件
    node->setParam(QStringLiteral("filePath"), pathBefore);
    node->setParam(QStringLiteral("isDirectory"), true);      // 必须在 filePath 之后：setParam(filePath) 会用 isDir() 重置它
    node->setParam(QStringLiteral("exposureTime"), 1234.0);
    node->setParam(QStringLiteral("gain"), 5.0);
    node->setParam(QStringLiteral("frameRate"), 24.0);
    node->setParam(QStringLiteral("triggerMode"), 0);         // 软件触发
    node->setParam(QStringLiteral("pixelFormat"), QStringLiteral("默认"));

    QWidget *panel = node->createParamPanel();
    QVERIFY2(panel != nullptr, "createParamPanel 返回空");

    // ── 识别前置（组合框：内容签名）──
    const QStringList sourceTypes{QStringLiteral("本地文件"), QStringLiteral("相机")};
    const QStringList fileModes{QStringLiteral("单文件"), QStringLiteral("多文件(目录)")};
    const QStringList triggerModes{QStringLiteral("软件触发"), QStringLiteral("硬件触发"),
                                   QStringLiteral("自由运行")};
    const QStringList pixelFormats{QStringLiteral("默认"), QStringLiteral("Mono8"),
                                   QStringLiteral("RGB8"), QStringLiteral("BayerRG8")};
    QComboBox *sourceTypeCombo = comboByTexts(panel, sourceTypes);
    QComboBox *fileModeCombo = comboByTexts(panel, fileModes);
    QComboBox *triggerModeCombo = comboByTexts(panel, triggerModes);
    QComboBox *pixelFormatCombo = comboByTexts(panel, pixelFormats);
    QVERIFY2(sourceTypeCombo && fileModeCombo && triggerModeCombo && pixelFormatCombo,
             "识别前置失败：未能按内容签名唯一定位四个下拉框");

    // ── 识别前置（行编辑框：四个创建期初始值互不相同）──
    const QList<QLineEdit *> edits = lineEditsInLayoutOrder(panel);
    QCOMPARE(edits.size(), 4);
    QCOMPARE(edits.at(0)->text(), pathBefore);
    QCOMPARE(edits.at(1)->text(), QString::number(1234.0));
    QCOMPARE(edits.at(2)->text(), QString::number(5.0));
    QCOMPARE(edits.at(3)->text(), QString::number(24.0));

    // 创建期读数：下拉框停在各自参数值
    QCOMPARE(sourceTypeCombo->currentIndex(), 0);
    QCOMPARE(fileModeCombo->currentIndex(), 1);       // isDirectory=true ⇒ findData(true) 命中"多文件(目录)"
    QCOMPARE(triggerModeCombo->currentIndex(), 0);
    QCOMPARE(pixelFormatCombo->currentText(), QStringLiteral("默认"));

    // 造一次"面板已建好、参数随后被改"的常规场景
    const QString pathAfter = QStringLiteral("E:/vfp_u1/after.png");
    node->setParam(QStringLiteral("filePath"), pathAfter);
    node->setParam(QStringLiteral("isDirectory"), false);     // 多文件(目录) → 单文件
    node->setParam(QStringLiteral("exposureTime"), 4321.0);
    node->setParam(QStringLiteral("gain"), 7.5);
    node->setParam(QStringLiteral("frameRate"), 60.0);
    node->setParam(QStringLiteral("triggerMode"), 1);         // 硬件触发
    node->setParam(QStringLiteral("pixelFormat"), QStringLiteral("Mono8"));

    const int sourceTypeIndexBefore = sourceTypeCombo->currentIndex();
    // 契约③探针：程序化刷新不该触发变更信号（触发即经面板 lambda 回写参数、并把输入框标"脏"）。
    // 文件路径框未修复态会被别的字段串写 ⇒ 本探针自身也红；曝光时间框未修复态没被写过，只作"撤 QSignalBlocker"的探针。
    QSignalSpy pathChangedSpy(edits.at(0), &QLineEdit::textChanged);
    QSignalSpy exposureChangedSpy(edits.at(1), &QLineEdit::textChanged);
    node->updateParamPanel(panel);

    // 契约①：每个字段写回自己的控件
    QCOMPARE(edits.at(0)->text(), pathAfter);
    QCOMPARE(edits.at(1)->text(), QString::number(4321.0));
    QCOMPARE(edits.at(2)->text(), QString::number(7.5));
    QCOMPARE(edits.at(3)->text(), QString::number(60.0));
    QCOMPARE(fileModeCombo->currentIndex(), 0);
    QCOMPARE(triggerModeCombo->currentIndex(), 1);
    QCOMPARE(pixelFormatCombo->currentText(), QStringLiteral("Mono8"));

    // 契约②：刷新面板不得改动别的参数（无名查找会把后写进来的值串到别的字段上）
    QCOMPARE(node->getParam(QStringLiteral("sourceType")).toInt(), 0);
    QCOMPARE(sourceTypeCombo->currentIndex(), sourceTypeIndexBefore);
    QCOMPARE(node->getParam(QStringLiteral("filePath")).toString(), pathAfter);

    // 契约③：刷新只改控件显示，不触发变更信号（不回流参数表、不标脏）
    QCOMPARE((int)pathChangedSpy.count(), 0);
    QCOMPARE((int)exposureChangedSpy.count(), 0);

    delete panel;
    delete node;
}

// ── T4 契约②独立成例：刷新面板不得改动参数表 ──
// T3 里显示断言先中止 ⇒ "参数被写脏"那半句（T3 末尾的 getParam 断言）从未跑到；本例不看任何控件，
// 只在 updateParamPanel 前后读 getParam ⇒ 未修复态"写错控件→控件 lambda 回写 m_params"直接落在这里。
void ParamPanelBindingTest::testImageSourceRefreshKeepsParamTable()
{
    auto *node = new HalconImageSourceNode();
    node->setParam(QStringLiteral("sourceType"), 0);
    node->setParam(QStringLiteral("filePath"), QStringLiteral("E:/vfp_u1/before.png"));
    node->setParam(QStringLiteral("isDirectory"), true);
    node->setParam(QStringLiteral("exposureTime"), 1234.0);
    node->setParam(QStringLiteral("gain"), 5.0);
    node->setParam(QStringLiteral("frameRate"), 24.0);
    node->setParam(QStringLiteral("triggerMode"), 0);
    node->setParam(QStringLiteral("pixelFormat"), QStringLiteral("默认"));

    QWidget *panel = node->createParamPanel();
    QVERIFY2(panel != nullptr, "createParamPanel 返回空");

    const QString pathAfter = QStringLiteral("E:/vfp_u1/after.png");
    node->setParam(QStringLiteral("filePath"), pathAfter);
    node->setParam(QStringLiteral("isDirectory"), false);
    node->setParam(QStringLiteral("exposureTime"), 4321.0);
    node->setParam(QStringLiteral("gain"), 7.5);
    node->setParam(QStringLiteral("frameRate"), 60.0);
    node->setParam(QStringLiteral("triggerMode"), 1);
    node->setParam(QStringLiteral("pixelFormat"), QStringLiteral("Mono8"));

    node->updateParamPanel(panel);

    // 刷新只做"参数 → 控件"的单向推送 ⇒ 八个参数逐条等于刷新前那一刻的值
    QCOMPARE(node->getParam(QStringLiteral("sourceType")).toInt(), 0);
    QCOMPARE(node->getParam(QStringLiteral("filePath")).toString(), pathAfter);
    QCOMPARE(node->getParam(QStringLiteral("isDirectory")).toBool(), false);
    QCOMPARE(node->getParam(QStringLiteral("exposureTime")).toDouble(), 4321.0);
    QCOMPARE(node->getParam(QStringLiteral("gain")).toDouble(), 7.5);
    QCOMPARE(node->getParam(QStringLiteral("frameRate")).toDouble(), 60.0);
    QCOMPARE(node->getParam(QStringLiteral("triggerMode")).toInt(), 1);
    QCOMPARE(node->getParam(QStringLiteral("pixelFormat")).toString(), QStringLiteral("Mono8"));

    delete panel;
    delete node;
}

QTEST_MAIN(ParamPanelBindingTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "param_panel_binding_test.moc"

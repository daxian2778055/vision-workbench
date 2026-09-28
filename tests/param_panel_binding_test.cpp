// 参数面板绑定验证（推进计划 §3.6 F-2 的取证 + 契约）。
// 契约：updateParamPanel(panel) 必须把每个参数写回**它自己的**控件，且不得改动别的参数；
//      且刷新只改控件显示、不触发控件变更信号（不回写参数表）。
// 缺陷族：部分节点的 updateParamPanel 用**无名** findChild<T>() 取控件 ⇒ 每次拿到的是同一个
//        （子对象顺序里第一个匹配的）后代，多字段被写进同一控件，并经控件信号把错值回写进节点参数。
// 取证口径：控件一律按**面板内容**识别（下拉项文本 / 创建期初始值），不按 objectName 识别，
//          因此修复前（无名）与修复后（具名）用同一套识别逻辑定位"本应被写入的那个控件"，
//          断言不会因改名而失真；识别本身另有前置断言把守（识别错则先红在"识别前置"上）。
// 说明：不 show() 窗口，因此可在无桌面的 CI（自托管 Windows runner）上稳定运行。
// T5～T7（U-4 本轮新增）：把上面那条"具名查找只在有人断言的字段上成立"的边界收成一道全表闸——
//      逐条遍历 NodeRegistry 里每个算子的 paramSpecs()，按 controlsTheRefresherLooksFor 抄写的
//      updateAutoParamPanel 查找口径（控件名 ＋ 控件类型）去面板里找，找不到即红
//      （名字拼错／控件根本没建／类型对不上都从这里出）。
//      T7 另用四个合成夹具钉住"闸会报警"本身与产品代码里 0 目标的那条复合控件（Point／Rect）口径。
// ✅ 边界更新（推进计划 §3.13，U-7 收口；原登记见 §3.10 的 C4b）：那份 (名字,类型) 清单以前是
//      本文件里手抄的镜像、产品代码不读它 ⇒ 改坏建控件侧的命名可检（C1／C2 实测红），
//      只改刷新侧 updateAutoParamPanel 的查找名检不出。
//      T8 现在按同一份镜像认控件，再走真实刷新（改参数表 → updateParamPanel）回读同一批控件
//      实例的显示值 ⇒ 刷新侧的查找名／控件类型／写入值三处改坏都当场红（§3.13 表 2 的 A1～A7）。
// ✅ 边界更新（推进计划 §3.14，U-15 收口；原登记见 §3.13 的「登记未修」）：文件头第 3 行那条
//      契约的另一半（刷新**不得发**控件变更信号）以前没人守——撤掉 updateAutoParamPanel 的
//      QSignalBlocker 后 T5～T8 仍全绿。T9 按 createAutoParamPanel 的接线逐类挂 QSignalSpy，
//      要求一次真实刷新里 13 个控件零发射、预览防抖定时器没被重新点起、参数表没被反灌，
//      并用「直接写同一个控件必须发信号」排除 spy 挂错对象的假绿 ⇒ 12 处 blocker 撤任意一处都红。
//      仍未覆盖：48 个真算子的值级扫描（＝U-7 的路线②，另算一轮）与不走自动面板的手写面板
//      （契约见 §3.7 表 1，无 paramSpecs 的那 25 个算子见 §3.10 表 3／U-8）。
// ✅ U-16 本轮（推进计划 §3.16；原登记见 §3.14「登记未修」）：上面那句「仍未覆盖」里指的**建控件侧**
//      那 13 处 QSignalBlocker（createAutoParamPanel 播种段 :964～:1020）撤掉后没有任何测试会红。
//      T10 把「建面板这一步本身不得有对外写回痕迹」立成闸，跑两个场景（表值==默认值／表值≠默认值），
//      并用「直写同一个控件必须改参数表」排除零痕迹是空转；T11 把 §3.14 表 3 那条**静态推导**
//      （播种落在哪条腿／哪件控件，含 :982～:984 那条不可达腿）跑成运行期读数。
#include <QtTest>
#include <QApplication>
#include <QSignalSpy>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QWidget>
#include <QFormLayout>
#include <QSet>
#include <QPointF>
#include <QRectF>

#include "ColorConversionNode.h"
#include "HalconImageSourceNode.h"
#include "HalconNode.h"
#include "NodeRegistry.h"

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

// ── U-4 全表闸用：把「生产代码怎么找控件」逐类型抄成一份可对照的清单 ──
// 口径来源＝src/HalconNode.cpp 的 updateAutoParamPanel（同一套名字与类型）：
//   Int→QSpinBox / Double→QDoubleSpinBox / Bool→QCheckBox / String,FilePath→QLineEdit /
//   MultiLine→QPlainTextEdit / Enum→QComboBox；Point 与 Rect 是复合控件，子件按
//   "<参数名>_x"（Rect 另有 _w/_h）命名，类型都是 QDoubleSpinBox。
struct NamedControl {
    QString name;
    const char *cls;
};

QList<NamedControl> controlsTheRefresherLooksFor(const ParamSpec &spec)
{
    const QString n = spec.name;
    switch (spec.type) {
    case ParamType::Int:    return {{n, "QSpinBox"}};
    case ParamType::Double: return {{n, "QDoubleSpinBox"}};
    case ParamType::Bool:   return {{n, "QCheckBox"}};
    case ParamType::String:
    case ParamType::FilePath: return {{n, "QLineEdit"}};
    case ParamType::MultiLine: return {{n, "QPlainTextEdit"}};
    case ParamType::Enum:   return {{n, "QComboBox"}};
    case ParamType::Point:
        return {{n + QStringLiteral("_x"), "QDoubleSpinBox"},
                {n + QStringLiteral("_y"), "QDoubleSpinBox"}};
    case ParamType::Rect:
        return {{n + QStringLiteral("_x"), "QDoubleSpinBox"},
                {n + QStringLiteral("_y"), "QDoubleSpinBox"},
                {n + QStringLiteral("_w"), "QDoubleSpinBox"},
                {n + QStringLiteral("_h"), "QDoubleSpinBox"}};
    }
    return {{n, "QWidget"}};
}

// 真正照抄 Qt 的 findChild<T *>(name) 语义：按 children() 顺序深度优先，返回**第一个**
// 「objectName 相等且继承自 T」的后代。
// 为什么不能"先按名字取任意控件、再判类型"：自动面板里 String 型参数的**容器 QWidget** 也被
// 命名成参数名（src/HalconNode.cpp:950 只对 Point/Rect/FilePath 跳过覆盖），而生产代码要的是
// 容器里那个 QLineEdit ⇒ 容器同名是既有形态、不是缺陷，只有按类型过滤才量得准。
// Qt6 的 QMetaObject::inherits 只收 QMetaObject*，不收类名字符串 ⇒ 自己沿 superClass() 链比名字
bool metaInherits(const QMetaObject *mo, const char *cls)
{
    for (; mo; mo = mo->superClass())
        if (qstrcmp(mo->className(), cls) == 0) return true;
    return false;
}

QWidget *findChildByNameAndClass(QWidget *root, const char *cls, const QString &name)
{
    const QObjectList &children = root->children();
    for (QObject *child : children) {
        QWidget *w = qobject_cast<QWidget *>(child);
        if (!w) continue;
        if (w->objectName() == name && metaInherits(w->metaObject(), cls)) return w;
        if (QWidget *hit = findChildByNameAndClass(w, cls, name)) return hit;
    }
    return nullptr;
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
    // U-4：paramSpecs × 面板 objectName 的完整性闸（全注册表，不只看有断言的字段）
    void testEveryParamSpecHasControlTheRefresherLooksFor();
    void testNoDuplicateParamDeclarations();
    void testU4GateHasTeethOnSyntheticProbes();
    // U-7：刷新侧自身的查找名／控件类型／写入值（值级回读，不再只靠手抄镜像）
    void testRefreshSideEmitsNoWriteBackSignal();
    void testRefreshSideWritesIntoMirrorNamedControl();
    // U-16：建面板这一步本身（createAutoParamPanel 的播种段）受检
    void testCreationPhaseLeavesNoWriteBackTrace();     // T10：创建期不得有对外写回痕迹
    void testCreationSeedLandingLegIsPinned();          // T11：播种落在哪条腿／哪件控件的运行期读数
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

// ── U-4 全表闸（T5／T6 共享一次扫描口径）──
namespace {

struct SweepResult {
    int nodesWithSpecs = 0;   // 有 paramSpecs 的算子数
    int specs = 0;            // 参数声明总数
    int lookups = 0;          // 面板查找次数（Point／Rect 一次贡献 2／4 次）
    QStringList misses;       // 刷新时按 (名字+类型) 取不到控件 —— 本闸的判红项
    QStringList threw;        // createParamPanel 抛异常（本条不判"为什么抛"，只记"没扫到"）
    QStringList dupNames;     // 同一算子里重复声明的参数名（本闸判红项之二）
    QStringList noSpecIds;    // paramSpecs 为空的算子 id（informational：本闸对它们无从判定）
    int pointRectSpecs = 0;   // Point／Rect 声明条数：本仓若为 0，则复合控件那两条口径本轮无从实测
    int namedWrongClass = 0;  // misses 的子集：面板里确有同名控件，但类型不是刷新代码要的那个

    int okLookups() const { return lookups - misses.size(); }
};

QByteArray typeTag(ParamType t)
{
    switch (t) {
    case ParamType::Int: return "Int";
    case ParamType::Double: return "Double";
    case ParamType::Bool: return "Bool";
    case ParamType::String: return "String";
    case ParamType::Enum: return "Enum";
    case ParamType::FilePath: return "FilePath";
    case ParamType::Point: return "Point";
    case ParamType::Rect: return "Rect";
    case ParamType::MultiLine: return "MultiLine";
    }
    return "Unknown";
}

// findings 全部用 ASCII：QtTest 日志经 cp936 控制台会把中文打成乱码（推进计划 §3.9 表 3 记过），
// 而本闸的产出正是"逐条可照抄的明细"，读不了的明细等于没取证。

// 单个算子：参数声明 → 面板控件的逐条核对。调用方负责 init() 与 delete。
// 拆出来是为了让 T7 能用一个合成节点**单独喂**进同一套判定来验闸的牙齿，不必改产品代码。
void sweepNodePanel(const QString &idTag, HalconNode *node, SweepResult &r)
{
    const ParamSpecList specs = node->paramSpecs();
    if (specs.isEmpty()) {
        r.noSpecIds << idTag;
        return;
    }
    ++r.nodesWithSpecs;
    r.specs += specs.size();

    QHash<QString, int> declared;
    for (const ParamSpec &s : specs) {
        if (s.type == ParamType::Point || s.type == ParamType::Rect) ++r.pointRectSpecs;
        declared.insert(s.name, declared.value(s.name) + 1);
    }
    // 同名参数被声明两次：registerParam 会把默认值写进同一个 m_params 键两遍，自动面板 then
    // 给两个控件同一个 objectName ⇒ 刷新时只有第一个拿得到值，第二个永远显示旧值（F-2 同族）。
    // （"两个不同参数名命中同一个控件实例"这里不查：一个控件只有一个 objectName，精确匹配下不可达。）
    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (it.value() > 1)
            r.dupNames << QStringLiteral("%1 :: param=%2 declared %3 times")
                              .arg(idTag, it.key()).arg(it.value());
    }

    QWidget *panel = nullptr;
    try {
        panel = node->createParamPanel();
    } catch (...) {
        r.threw << QStringLiteral("%1 :: createParamPanel threw").arg(idTag);
        return;
    }
    if (!panel) {
        r.misses << QStringLiteral("%1 :: createParamPanel returned nullptr").arg(idTag);
        return;
    }

    for (const ParamSpec &s : specs) {
        const QList<NamedControl> wants = controlsTheRefresherLooksFor(s);
        for (const NamedControl &c : wants) {
            ++r.lookups;
            QWidget *w = findChildByNameAndClass(panel, c.cls, c.name);
            if (w) continue;
            QWidget *anyName = panel->findChild<QWidget *>(c.name);
            QString detail;
            if (anyName) {
                ++r.namedWrongClass;
                detail = QStringLiteral(" ; a widget with this name exists but it is %1")
                             .arg(QString::fromLatin1(anyName->metaObject()->className()));
            }
            r.misses << QStringLiteral("%1 :: param=%2 type=%3 -> updateAutoParamPanel looks for "
                                       "objectName=\"%4\" class=%5, not found%6")
                            .arg(idTag, s.name, typeTag(s.type), c.name, c.cls, detail);
        }
    }
    if (!panel->parent()) delete panel;   // 自动面板返回未挂父的顶层控件
}

SweepResult sweepRegisteredParamPanels(QObject *parent)
{
    SweepResult r;
    const QList<NodeRegistration> regs = NodeRegistry::instance().all();
    for (const NodeRegistration &reg : regs) {
        HalconNode *node = qobject_cast<HalconNode *>(NodeRegistry::instance().createById(reg.id, parent));
        if (!node) {
            r.misses << QStringLiteral("%1 :: createById returned null or non-HalconNode").arg(reg.id);
            continue;
        }
        node->init();   // 参数在 init() 里注册（与 FlowScene::createNode 同一条路径）
        sweepNodePanel(reg.id, node, r);
        delete node;
    }
    return r;
}

void dumpSweep(const char *caseTag, const SweepResult &r)
{
    qWarning().noquote() << QStringLiteral("[U4-SWEEP %1] nodesWithSpecs=%2 specs=%3 lookups=%4 ok=%5 "
                                           "misses=%6 (namedWrongClass=%7) threw=%8 dupNames=%9 noSpecs=%10 "
                                           "pointRectSpecs=%11")
                                .arg(caseTag).arg(r.nodesWithSpecs).arg(r.specs).arg(r.lookups)
                                .arg(r.okLookups()).arg(r.misses.size()).arg(r.namedWrongClass)
                                .arg(r.threw.size()).arg(r.dupNames.size()).arg(r.noSpecIds.size())
                                .arg(r.pointRectSpecs);
    for (const QString &line : r.misses)   qWarning().noquote() << "  MISS   " << line;
    for (const QString &line : r.threw)    qWarning().noquote() << "  THREW  " << line;
    for (const QString &line : r.dupNames) qWarning().noquote() << "  DUP    " << line;
    qWarning().noquote() << "  NOSPECS " << r.noSpecIds.join(QStringLiteral(","));
}

} // namespace

// ── T5（U-4）：每个参数声明都必须有"刷新代码取到的那个控件" ──
// 咬的正是 §3.7 登记的边界：T1～T4 只护住"有断言的字段"，名字拼错／控件根本没建在别的字段上
// 会静默通过。本条不依赖任何字段断言，直接按生产代码的查找口径扫全表。
void ParamPanelBindingTest::testEveryParamSpecHasControlTheRefresherLooksFor()
{
    registerAllNodes();
    QVERIFY2(NodeRegistry::instance().all().size() > 60,
             qPrintable(QStringLiteral("注册表规模异常：%1").arg(NodeRegistry::instance().all().size())));

    const SweepResult r = sweepRegisteredParamPanels(this);
    dumpSweep("T5", r);
    // 扫描本身不得是空转：否则"0 miss"只是因为什么都没扫（同 registryIsNotEmpty 那条教训）
    QVERIFY2(r.nodesWithSpecs > 40,
             qPrintable(QStringLiteral("本闸只扫到 %1 个带参数声明的算子，口径已失效").arg(r.nodesWithSpecs)));
    QVERIFY2(r.lookups > 200,
             qPrintable(QStringLiteral("本闸只核了 %1 次面板查找，口径已失效").arg(r.lookups)));

    QStringList problems = r.misses + r.threw;
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("U4-INCOMPLETE: %1 条参数在面板里取不到刷新用控件"
                                        "（明细见上方 [U4-SWEEP T5]）\n%2")
                            .arg(problems.size()).arg(problems.join(QStringLiteral("\n")))));
}

// ── T6（U-4）：同一算子内不得重复声明同名参数 ──
// 重复声明时 registerParam 把默认值写进同一个 m_params 键两遍，自动面板又给两个控件同一个
// objectName ⇒ 刷新只有第一个控件拿得到值，第二个永远显示旧值（§3.6 F-2 同族，且不靠字段断言就可检）。
void ParamPanelBindingTest::testNoDuplicateParamDeclarations()
{
    registerAllNodes();
    const SweepResult r = sweepRegisteredParamPanels(this);
    dumpSweep("T6", r);
    QVERIFY2(r.nodesWithSpecs > 40,
             qPrintable(QStringLiteral("本闸只扫到 %1 个带参数声明的算子，口径已失效").arg(r.nodesWithSpecs)));

    QVERIFY2(r.dupNames.isEmpty(),
             qPrintable(QStringLiteral("U4-DUP: %1 个参数名在同一算子里被声明多次"
                                        "（明细见上方 [U4-SWEEP T6]）\n%2")
                            .arg(r.dupNames.size()).arg(r.dupNames.join(QStringLiteral("\n")))));
}

// ── T7（U-4）：本闸的牙齿自证——四个合成夹具各喂一种坏形态（或一种无人可检的口径）进同一套判定 ──
// 为什么用合成夹具而不是只改产品代码：产品面板 0 命中（T5／T6 当前全绿）时，"闸没报警"与
// "闸根本不会报警"在跑次里长得一样。四个臂各自钉一个桶的确切计数，空转的桶当场红。
namespace {

enum ProbeMode { OkProbe, DupNameProbe, MissingControlProbe, CompositeSpecProbe };

class U4ProbeNode : public HalconNode
{
public:
    explicit U4ProbeNode(ProbeMode mode) : m_mode(mode) {}

    void init() override
    {
        HalconNode::init();
        if (m_mode == CompositeSpecProbe) {
            // 本仓产品代码里 Point／Rect 声明条数为 0（T5 跑次的 pointRectSpecs），后缀口径
            // （_x／_y／_w／_h）没有产品目标可检 ⇒ 只能由本臂钉住，否则那条分支是空写的。
            ParamSpec pt;
            pt.name = QStringLiteral("center");
            pt.type = ParamType::Point;
            pt.defaultValue = QPointF(3.0, 4.0);
            pt.label = QStringLiteral("中心点");
            ParamSpec rc;
            rc.name = QStringLiteral("roi");
            rc.type = ParamType::Rect;
            rc.defaultValue = QRectF(1.0, 2.0, 30.0, 40.0);
            rc.label = QStringLiteral("区域");
            registerParams({pt, rc});
            return;
        }
        if (m_mode == DupNameProbe) {
            // 同一参数名声明两次（第二个本意是另一个字段）
            registerParams({makeDoubleParam(QStringLiteral("alpha"), 1.0, 0.0, 100.0,
                                            QStringLiteral("A")),
                            makeDoubleParam(QStringLiteral("alpha"), 2.0, 0.0, 100.0,
                                            QStringLiteral("B"))});
        } else {
            registerParams({makeDoubleParam(QStringLiteral("alpha"), 1.0, 0.0, 100.0,
                                            QStringLiteral("A")),
                            makeDoubleParam(QStringLiteral("beta"), 2.0, 0.0, 100.0,
                                            QStringLiteral("B"))});
        }
    }

    QWidget *createParamPanel() override
    {
        if (m_mode != MissingControlProbe) return createAutoParamPanel();
        // 坏形态：alpha 压根没建控件；beta 建了个**同名但类型不对**的裸 QWidget
        auto *panel = new QWidget();
        auto *wrongClass = new QWidget(panel);
        wrongClass->setObjectName(QStringLiteral("beta"));
        return panel;
    }

private:
    ProbeMode m_mode;
};

} // namespace

void ParamPanelBindingTest::testU4GateHasTeethOnSyntheticProbes()
{
    // 臂①对照：一切正常 ⇒ 任何桶都不许响（否则 T5／T6 的红可以是"闸见谁都咬"）
    {
        U4ProbeNode node(OkProbe);
        node.init();
        SweepResult r;
        sweepNodePanel(QStringLiteral("OkProbe"), &node, r);
        dumpSweep("T7-OK", r);
        QCOMPARE(r.nodesWithSpecs, 1);
        QCOMPARE(r.lookups, 2);
        QCOMPARE(r.misses.size(), 0);
        QCOMPARE(r.dupNames.size(), 0);
        QCOMPARE(r.threw.size(), 0);
    }
    // 臂②：重复声明 ⇒ dupNames 恰 1（且不误报 miss：两个同名控件确实都在面板里）
    {
        U4ProbeNode node(DupNameProbe);
        node.init();
        SweepResult r;
        sweepNodePanel(QStringLiteral("DupNameProbe"), &node, r);
        dumpSweep("T7-DUP", r);
        QCOMPARE(r.dupNames.size(), 1);
        QCOMPARE(r.misses.size(), 0);
    }
    // 臂③：控件缺失／同名不同类型 ⇒ misses 恰 2，其中 namedWrongClass 恰 1
    {
        U4ProbeNode node(MissingControlProbe);
        node.init();
        SweepResult r;
        sweepNodePanel(QStringLiteral("MissingControlProbe"), &node, r);
        dumpSweep("T7-MISS", r);
        QCOMPARE(r.misses.size(), 2);
        QCOMPARE(r.namedWrongClass, 1);
        QCOMPARE(r.dupNames.size(), 0);
    }
    // 臂④：Point／Rect 后缀口径（_x／_y 与 _x／_y／_w／_h）——一次声明贡献 2／4 次查找
    // 产品代码里这条口径 0 目标（T5 跑次 pointRectSpecs=0），只能由本臂钉住：计数不对或全 miss
    // 都当场红，免得那条分支在没人看的时候被改坏成空写。
    {
        U4ProbeNode node(CompositeSpecProbe);
        node.init();
        SweepResult r;
        sweepNodePanel(QStringLiteral("CompositeSpecProbe"), &node, r);
        dumpSweep("T7-COMPOSITE", r);
        QCOMPARE(r.specs, 2);
        QCOMPARE(r.pointRectSpecs, 2);
        QCOMPARE(r.lookups, 6);
        QCOMPARE(r.misses.size(), 0);
        QCOMPARE(r.namedWrongClass, 0);
    }
}

// ── T8（U-7）：刷新侧本身变成被检对象 ──
// §3.10 的 C4b／C4 实测过：只坏 updateAutoParamPanel 的查找名或写入值，T5～T7 全绿——因为那份
// (名字,类型) 清单是"闸单方面抄写"的。本条把抄写关系变成自检：仍只按同一份镜像清单
// controlsTheRefresherLooksFor 认出控件，然后走真实刷新路径（改参数表 → updateParamPanel）
// 回读**同一批控件实例**的显示值 ⇒ 刷新侧改查找名／换控件类型／写错值都当场红。
// 边界（不越界声明）：九臂是**合成夹具**、一次一个参数，不进 48 个真算子 ⇒ U-7 的②
// （真算子值级扫描）与手写面板仍归 §3.10 的 U-8／§3.7 表 1 那条路。
namespace {

enum U7Arm { ArmInt, ArmDouble, ArmBool, ArmString, ArmFilePath, ArmMultiLine,
             ArmEnum, ArmPoint, ArmRect };

const char *u7ArmTag(U7Arm arm)
{
    switch (arm) {
    case ArmInt: return "Int";
    case ArmDouble: return "Double";
    case ArmBool: return "Bool";
    case ArmString: return "String";
    case ArmFilePath: return "FilePath";
    case ArmMultiLine: return "MultiLine";
    case ArmEnum: return "Enum";
    case ArmPoint: return "Point";
    case ArmRect: return "Rect";
    }
    return "?";
}

QString u7BeforeText() { return QStringLiteral("u7before"); }
QString u7AfterText()  { return QStringLiteral("u7after"); }

ParamSpec u7Spec(const QString &name, ParamType type, const QVariant &def)
{
    ParamSpec s;
    s.name = name; s.type = type; s.defaultValue = def;
    s.label = QStringLiteral("U7");
    return s;
}

// 只认控件"显示的是什么"，按镜像清单点名的类各取一次
QString u7DisplayOf(QWidget *w)
{
    if (auto *x = qobject_cast<QSpinBox *>(w)) return QString::number(x->value());
    if (auto *x = qobject_cast<QDoubleSpinBox *>(w)) return QString::number(x->value(), 'f', 4);
    if (auto *x = qobject_cast<QCheckBox *>(w)) return x->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
    if (auto *x = qobject_cast<QLineEdit *>(w)) return x->text();
    if (auto *x = qobject_cast<QPlainTextEdit *>(w)) return x->toPlainText();
    if (auto *x = qobject_cast<QComboBox *>(w)) return QString::number(x->currentIndex());
    return QStringLiteral("<unhandled class>");
}

// T9（U-15）用两件小工具，口径逐条抄自 src/HalconNode.cpp 的 createAutoParamPanel：
//   u9SpyOf   ＝「产品把这个控件的哪条信号接回 setParam」（§3.14 表 0 的 13 条写回线）
//   u9ProbeWrite＝直接写同一个控件一次，用来证明 spy 确实挂在"那个对象的那条信号"上
QSignalSpy *u9SpyOf(QWidget *w)
{
    if (auto *x = qobject_cast<QSpinBox *>(w))
        return new QSignalSpy(x, qOverload<int>(&QSpinBox::valueChanged));
    if (auto *x = qobject_cast<QDoubleSpinBox *>(w))
        return new QSignalSpy(x, qOverload<double>(&QDoubleSpinBox::valueChanged));
    if (auto *x = qobject_cast<QCheckBox *>(w))
        return new QSignalSpy(x, &QCheckBox::toggled);
    if (auto *x = qobject_cast<QPlainTextEdit *>(w))
        return new QSignalSpy(x, &QPlainTextEdit::textChanged);
    if (auto *x = qobject_cast<QLineEdit *>(w))
        return new QSignalSpy(x, &QLineEdit::textChanged);
    if (auto *x = qobject_cast<QComboBox *>(w))
        return new QSignalSpy(x, qOverload<int>(&QComboBox::currentIndexChanged));
    return nullptr;
}

bool u9ProbeWrite(QWidget *w)
{
    if (auto *x = qobject_cast<QSpinBox *>(w)) { x->setValue(x->value() + 7); return true; }
    if (auto *x = qobject_cast<QDoubleSpinBox *>(w)) { x->setValue(x->value() + 7.0); return true; }
    if (auto *x = qobject_cast<QCheckBox *>(w)) { x->toggle(); return true; }
    if (auto *x = qobject_cast<QPlainTextEdit *>(w)) { x->setPlainText(QStringLiteral("u9probe")); return true; }
    if (auto *x = qobject_cast<QLineEdit *>(w)) { x->setText(QStringLiteral("u9probe")); return true; }
    if (auto *x = qobject_cast<QComboBox *>(w)) {
        x->setCurrentIndex((x->currentIndex() + 1) % qMax(1, x->count()));
        return true;
    }
    return false;
}

// T10（U-16）用：把控件"当前显示的值"原样再写一次（不包 blocker）。覆盖的类与 u9SpyOf 一一对应。
// 这条不是推断：§3.14 把"播的初值等于表里的当前值 ⇒ 看不出来"当**前提**写着，本轮把它跑成读数
// （同值写回到底发不发射、发射了会不会改表／点定时器），逐臂记进 u16 的 dump 行。
bool u10SameValueWrite(QWidget *w)
{
    if (auto *x = qobject_cast<QSpinBox *>(w)) { x->setValue(x->value()); return true; }
    if (auto *x = qobject_cast<QDoubleSpinBox *>(w)) { x->setValue(x->value()); return true; }
    if (auto *x = qobject_cast<QCheckBox *>(w)) { x->setChecked(x->isChecked()); return true; }
    if (auto *x = qobject_cast<QPlainTextEdit *>(w)) { x->setPlainText(x->toPlainText()); return true; }
    if (auto *x = qobject_cast<QLineEdit *>(w)) { x->setText(x->text()); return true; }
    if (auto *x = qobject_cast<QComboBox *>(w)) { x->setCurrentIndex(x->currentIndex()); return true; }
    return false;
}

// 两份参数表快照的差异 → 一行可读文字（失败消息里必须看得见"哪个键从什么变成什么"）
QString u10TableDiffText(const QMap<QString, QVariant> &before, const QMap<QString, QVariant> &after)
{
    QStringList out;
    const QStringList keys = QMap<QString, QVariant>(before).keys() + QMap<QString, QVariant>(after).keys();
    QSet<QString> seen;
    for (const QString &k : keys) {
        if (seen.contains(k)) continue;
        seen.insert(k);
        const QVariant b = before.value(k), a = after.value(k);
        if (b == a && before.contains(k) == after.contains(k)) continue;
        QString bs, as;
        QDebug(&bs) << b;
        QDebug(&as) << a;
        out << QStringLiteral("%1: %2 -> %3").arg(k, bs, as);
    }
    return out.join(QStringLiteral(" ; "));
}

// T11 用：往上一路爬到「父件挂着 QFormLayout」的那一层 ⇒ 这一层的 widget 就是 form 的 field 槽位上
// 的东西，也正是 createAutoParamPanel 里的局部变量 editor。用它就能实测「:982 的
// qobject_cast<QLineEdit *>(editor) 到底能不能成立」，而不是靠读盘推。
QWidget *fieldOfWidget(QWidget *w)
{
    QWidget *cur = w;
    while (cur && cur->parentWidget()) {
        if (qobject_cast<QFormLayout *>(cur->parentWidget()->layout()))
            return cur;
        cur = cur->parentWidget();
    }
    return nullptr;
}

// T11 用：**不限类**按 objectName 找后代里第一个叫这个名字的 widget。
// findChildByNameAndClass 问的是"有没有该类且具名的控件"，这里问的是"面板里到底存不存在
// 一个叫参数名本身的对象"——两把尺子合起来才能把 :950（单值类型具名覆盖）与 :946～:948
// （复合／容器类型不覆盖）这两条口径分别钉住。
QWidget *widgetNamedAnywhere(QWidget *root, const QString &name)
{
    const QList<QWidget *> all = root->findChildren<QWidget *>();
    for (QWidget *w : all) {
        if (w->objectName() == name) return w;
    }
    return nullptr;
}


// 一个臂＝一种 ParamType，只声明一个参数，控件全部由产品的自动面板建出来
class U7TypeFixture : public HalconNode
{
public:
    explicit U7TypeFixture(U7Arm arm) : m_arm(arm) {}

    void init() override
    {
        HalconNode::init();
        const QString name = QStringLiteral("alpha");
        switch (m_arm) {
        case ArmInt:
            registerParams({makeIntParam(name, 10, 0, 100, QStringLiteral("U7"))});
            break;
        case ArmDouble:
            registerParams({makeDoubleParam(name, 1.5, 0.0, 100.0, QStringLiteral("U7"))});
            break;
        case ArmBool:
            registerParams({makeBoolParam(name, false, QStringLiteral("U7"))});
            break;
        case ArmString:
            registerParams({makeStringParam(name, u7BeforeText(), QStringLiteral("U7"))});
            break;
        case ArmFilePath:
            registerParams({makeFilePathParam(name, u7BeforeText(), QStringLiteral("U7"))});
            break;
        case ArmMultiLine:
            registerParams({u7Spec(name, ParamType::MultiLine, u7BeforeText())});
            break;
        case ArmEnum:
            registerParams({makeEnumParam(name, 0,
                                          {QStringLiteral("e0"), QStringLiteral("e1"),
                                           QStringLiteral("e2")}, QStringLiteral("U7"))});
            break;
        case ArmPoint:
            registerParams({u7Spec(name, ParamType::Point, QPointF(1.0, 2.0))});
            break;
        case ArmRect:
            registerParams({u7Spec(name, ParamType::Rect, QRectF(1.0, 2.0, 3.0, 4.0))});
            break;
        }
    }

    // 面板建好后只改参数表（不碰任何控件），制造"面板已存在、参数随后变了"的常规场景
    void applyAfterValues()
    {
        const QString name = QStringLiteral("alpha");
        switch (m_arm) {
        case ArmInt: setParam(name, 20); break;
        case ArmDouble: setParam(name, 2.5); break;
        case ArmBool: setParam(name, true); break;
        case ArmString: case ArmFilePath: case ArmMultiLine:
            setParam(name, u7AfterText()); break;
        case ArmEnum: setParam(name, 2); break;
        case ArmPoint: setParam(name, QPointF(5.0, 6.0)); break;
        case ArmRect: setParam(name, QRectF(7.0, 8.0, 9.0, 10.0)); break;
        }
    }

    QStringList beforeDisplays() const
    {
        switch (m_arm) {
        case ArmInt: return {QStringLiteral("10")};
        case ArmDouble: return {QStringLiteral("1.5000")};
        case ArmBool: return {QStringLiteral("0")};
        case ArmString: case ArmFilePath: case ArmMultiLine: return {u7BeforeText()};
        case ArmEnum: return {QStringLiteral("0")};
        case ArmPoint: return {QStringLiteral("1.0000"), QStringLiteral("2.0000")};
        case ArmRect: return {QStringLiteral("1.0000"), QStringLiteral("2.0000"),
                              QStringLiteral("3.0000"), QStringLiteral("4.0000")};
        }
        return {};
    }

    QStringList afterDisplays() const
    {
        switch (m_arm) {
        case ArmInt: return {QStringLiteral("20")};
        case ArmDouble: return {QStringLiteral("2.5000")};
        case ArmBool: return {QStringLiteral("1")};
        case ArmString: case ArmFilePath: case ArmMultiLine: return {u7AfterText()};
        case ArmEnum: return {QStringLiteral("2")};
        case ArmPoint: return {QStringLiteral("5.0000"), QStringLiteral("6.0000")};
        case ArmRect: return {QStringLiteral("7.0000"), QStringLiteral("8.0000"),
                              QStringLiteral("9.0000"), QStringLiteral("10.0000")};
        }
        return {};
    }

    // T9（U-15）用：把"刷新一旦发写回信号会造成什么"变成可观察量。
    // m_autoPreviewEnabled 默认 false（include/HalconNode.h:133），预览防抖定时器在构造函数里
    // 建好并设成 300ms 单次触发（src/HalconNode.cpp:39），所以"刷新有没有回头踩 setParam"
    // 就看这一步之后定时器是否又被点起来。
    bool previewTimerActive() const { return m_previewTimer && m_previewTimer->isActive(); }
    void stopPreviewTimer()
    {
        if (m_previewTimer)
            m_previewTimer->stop();
    }

    // T10（U-16）用：建面板期间「有没有人写回参数表」＝比较这两次快照
    QMap<QString, QVariant> tableSnapshot() const { return m_params.snapshot(); }
    // T11 用：绕开 setParam 的范围钳制直写表，造"表里本来就存着越界值"的现场
    //（生产上这条路真实存在：m_params 可被直接写下发／旧方案载入的值不会被 spec 夹住）
    void writeTableDirect(const QVariant &v) { setParamDirect(QStringLiteral("alpha"), v); }
    // 该臂能否做"越界读数"：给不出（无效 QVariant）的臂跳过这一条腿
    QVariant outOfRangeValue() const
    {
        switch (m_arm) {
        case ArmInt: return 300;            // spec 声明 0..100
        case ArmDouble: return 1234.5;      // spec 声明 0.0..100.0
        case ArmEnum: return 7;             // 只有 e0／e1／e2 三项
        default: return QVariant();
        }
    }
    // 越界表值在面板上会被夹成什么（运行期回读要等于它；参数表侧仍应留着越界值）
    QString outOfRangeDisplay() const
    {
        switch (m_arm) {
        case ArmInt: return QStringLiteral("100");
        case ArmDouble: return QStringLiteral("100.0000");
        case ArmEnum: return QStringLiteral("2");
        default: return QString();
        }
    }

private:
    U7Arm m_arm;
};

} // namespace

void ParamPanelBindingTest::testRefreshSideWritesIntoMirrorNamedControl()
{
    const QList<U7Arm> arms = {ArmInt, ArmDouble, ArmBool, ArmString, ArmFilePath,
                               ArmMultiLine, ArmEnum, ArmPoint, ArmRect};
    QStringList problems;
    int lookups = 0;
    for (U7Arm arm : arms) {
        const QString tag = QString::fromLatin1(u7ArmTag(arm));
        U7TypeFixture node(arm);
        node.init();
        QWidget *panel = node.createParamPanel();
        if (!panel) {
            problems << QStringLiteral("%1 :: createParamPanel returned null").arg(tag);
            continue;
        }

        const QStringList before = node.beforeDisplays();
        const QStringList after = node.afterDisplays();
        QList<QWidget *> mirrorControls;
        for (const ParamSpec &s : node.paramSpecs()) {
            for (const NamedControl &c : controlsTheRefresherLooksFor(s)) {
                ++lookups;
                QWidget *w = findChildByNameAndClass(panel, c.cls, c.name);
                if (!w) {
                    problems << QStringLiteral("%1 :: mirror control objectName=\"%2\" class=%3 not found")
                                    .arg(tag, c.name, QString::fromLatin1(c.cls));
                    continue;
                }
                mirrorControls << w;
            }
        }
        if (mirrorControls.size() != before.size()) {
            problems << QStringLiteral("%1 :: found %2 controls, expected %3")
                            .arg(tag).arg(mirrorControls.size()).arg(before.size());
            delete panel;
            continue;
        }

        // 识别前置：创建期读数必须等于该参数的当前值 ⇒ 认错控件先红在这里，
        // 不会把"闸点错了控件"伪装成"刷新没生效"。
        for (int i = 0; i < mirrorControls.size(); ++i) {
            const QString got = u7DisplayOf(mirrorControls.at(i));
            if (got != before.at(i))
                problems << QStringLiteral("%1 :: at-creation readback #%2 = \"%3\", expected \"%4\"")
                                .arg(tag).arg(i).arg(got, before.at(i));
        }

        node.applyAfterValues();
        node.updateParamPanel(panel);

        for (int i = 0; i < mirrorControls.size(); ++i) {
            const QString got = u7DisplayOf(mirrorControls.at(i));
            if (got != after.at(i))
                problems << QStringLiteral("%1 :: after-refresh readback #%2 = \"%3\", expected \"%4\""
                                           " (updateAutoParamPanel did not write this control)")
                                .arg(tag).arg(i).arg(got, after.at(i));
        }
        delete panel;
    }
    // 九臂应产生 13 次查找（Point 2／Rect 4／其余各 1）⇒ 摘掉任一臂当场红
    QCOMPARE(lookups, 13);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("U7-REFRESH: 刷新侧有 %1 处没写到镜像清单点名的控件（明细见上）\n%2")
                            .arg(problems.size()).arg(problems.join(QStringLiteral("\n")))));
}

// T9（推进计划 §3.14，U-15 收口）：刷新侧不得让"镜像清单点名的控件"发出**写回信号**。
// 为什么这是契约不是风格：createAutoParamPanel 把每个控件的变更信号都接回 setParam
// （§3.14 表 0 逐条核到 13 条线），所以刷新一旦发信号，就是"面板回填 → 反写参数表 →
// 重启预览防抖"的回路；宿主侧本来就承认这条契约——src/ModuleEditorDialog.cpp 在三处
// updateParamPanel 前后挂了 m_paramHookBusy，onParamWidgetEdited（:407）靠它早退，注释写着
// "程序化回填参数不应触发自动重算（否则会与用户操作形成回路）"。⇒ 产品里有第二层防呆，
//   但节点侧的 QSignalBlocker 撤掉后没有任何测试会红（§3.14 表 0 的三条负对照实测）。
// 每臂四步：① 创建期回读＝该参数当前值（认错控件先红在这里，不会伪装成"没发信号"）；
//          ② 只改参数表 → updateParamPanel → **回读显示**，证明刷新确实写过（否则第③步的
//             "零发射"是空转）；③ 13 个 spy 一条信号都不许收到，且预览防抖定时器没被重新点起来、
//             参数表没被写回信号反灌（③c 只在 Point／Rect 撤单个子件时才会红，另两腿逐臂都红）；
//          ④ 逐个直接写同一个控件（不包 blocker），spy 必须收到 ⇒ 排除"spy 挂错对象／信号"的假绿。
// 与 T8 的分工：T8 管"写没写到、写对没有"（值），T9 管"写的过程中有没有对外发声"（信号）。
void ParamPanelBindingTest::testRefreshSideEmitsNoWriteBackSignal()
{
    const QList<U7Arm> arms = {ArmInt, ArmDouble, ArmBool, ArmString, ArmFilePath,
                               ArmMultiLine, ArmEnum, ArmPoint, ArmRect};
    QStringList problems;
    int spies = 0;
    for (U7Arm arm : arms) {
        const QString tag = QString::fromLatin1(u7ArmTag(arm));
        U7TypeFixture node(arm);
        node.init();
        node.setAutoPreviewEnabled(true);   // 让"刷新→反写参数表"留下可观察痕迹
        QWidget *panel = node.createParamPanel();
        if (!panel) {
            problems << QStringLiteral("%1 :: createParamPanel returned null").arg(tag);
            continue;
        }

        const QStringList before = node.beforeDisplays();
        const QStringList after = node.afterDisplays();
        QList<QWidget *> controls;
        QList<QSignalSpy *> live;
        for (const ParamSpec &s : node.paramSpecs()) {
            for (const NamedControl &c : controlsTheRefresherLooksFor(s)) {
                QWidget *w = findChildByNameAndClass(panel, c.cls, c.name);
                if (!w) {
                    problems << QStringLiteral("%1 :: mirror control objectName=\"%2\" class=%3 not found")
                                    .arg(tag, c.name, QString::fromLatin1(c.cls));
                    continue;
                }
                QSignalSpy *spy = u9SpyOf(w);
                if (!spy) {
                    problems << QStringLiteral("%1 :: class=%2 has no write-back signal defined in u9SpyOf")
                                    .arg(tag, QString::fromLatin1(c.cls));
                    continue;
                }
                ++spies;
                controls << w;
                live << spy;
            }
        }

        if (controls.size() != before.size()) {
            problems << QStringLiteral("%1 :: found %2 controls, expected %3")
                            .arg(tag).arg(controls.size()).arg(before.size());
        } else {
            for (int i = 0; i < controls.size(); ++i) {
                const QString got = u7DisplayOf(controls.at(i));
                if (got != before.at(i))
                    problems << QStringLiteral("%1 :: at-creation readback #%2 = \"%3\", expected \"%4\"")
                                    .arg(tag).arg(i).arg(got, before.at(i));
            }

            node.applyAfterValues();     // 用户侧改参数（合法地会重启防抖定时器）
            node.stopPreviewTimer();     // 归零，令"定时器又跑起来"只能由刷新这一步造成
            const QString pname = node.paramSpecs().constFirst().name;
            const QVariant target = node.getParam(pname);   // 刷新期间参数表不该再变
            for (QSignalSpy *sp : live)
                sp->clear();

            node.updateParamPanel(panel);

            // ② 刷新必须真的写过值，否则下面的"零发射"毫无意义
            for (int i = 0; i < controls.size(); ++i) {
                const QString got = u7DisplayOf(controls.at(i));
                if (got != after.at(i))
                    problems << QStringLiteral("%1 :: after-refresh readback #%2 = \"%3\", expected \"%4\""
                                               " (刷新没写值 ⇒ 发射断言是空转)")
                                    .arg(tag).arg(i).arg(got, after.at(i));
            }
            // ③ 发射计数 ＋ 预览防抖定时器
            for (int i = 0; i < live.size(); ++i) {
                if (live.at(i)->count() != 0)
                    problems << QStringLiteral("%1 :: control #%2 (objectName=\"%3\") emitted its"
                                               " write-back signal %4 times during refresh")
                                    .arg(tag).arg(i)
                                    .arg(controls.at(i)->objectName())
                                    .arg(live.at(i)->count());
            }
            if (node.previewTimerActive())
                problems << QStringLiteral("%1 :: refresh restarted the preview debounce timer"
                                           "（刷新经写回信号反改了参数表）").arg(tag);
            // ③c 参数表不得被写回信号反改（Point／Rect 只撤一个子件 blocker 时会留下"半刷新"值）
            const QVariant now = node.getParam(pname);
            if (now != target) {
                QString from, to;
                QDebug(&from) << target;
                QDebug(&to) << now;
                problems << QStringLiteral("%1 :: param table changed during refresh: %2 -> %3"
                                           "（控件写回信号把面板值反灌进参数表）")
                                .arg(tag, from, to);
            }
            // ④ spy 自己得是活的：直接写同一个控件必须发信号
            for (int i = 0; i < live.size(); ++i) {
                const int before_count = live.at(i)->count();
                if (!u9ProbeWrite(controls.at(i))) {
                    problems << QStringLiteral("%1 :: control #%2 has no probe write defined")
                                    .arg(tag).arg(i);
                } else if (live.at(i)->count() == before_count) {
                    problems << QStringLiteral("%1 :: control #%2 (objectName=\"%3\") stayed silent"
                                               " on a direct write ⇒ spy 挂错了对象或信号")
                                    .arg(tag).arg(i).arg(controls.at(i)->objectName());
                }
            }
        }
        qDeleteAll(live);
        delete panel;
    }
    // 九臂应产生 13 个 spy（Point 2／Rect 4／其余各 1）⇒ 摘掉任一臂当场红
    QCOMPARE(spies, 13);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("U9-SILENCE: 刷新侧有 %1 处越界（明细逐条随附）\n%2")
                            .arg(problems.size()).arg(problems.join(QStringLiteral("\n")))));
}


// ── T10（U-16）：建面板这一步本身受检 ──
// §3.14 实测过：撤掉 createAutoParamPanel 播种段那 13 处 QSignalBlocker（:964～:1020）没有任何测试会红，
// 并把原因写成三条**前提**（spy 挂在建面板之后／创建时自动预览还没开／播的初值等于表值）。本条不复制
// T9 的口径（T9 管刷新），而是把「建面板期间不得留下对外写回痕迹」立成闸，两个场景各跑一遍：
//   S0 表值 == 控件默认值（新建节点，播种是"同值赋值"）
//   S1 表值 ≠ 控件默认值（先改参数表再开面板＝载入方案后操作工打开面板的真实顺序 ⇒ 播种是一次真值变化）
// 只有 S1 才有资格说"这里如果连着线就会出事"，所以两场景都要跑；空转由 ④ 的两条探针排除。
// 边界（不越界声明）：九臂是合成夹具、一次一个参数，走的仍是产品的 createAutoParamPanel；真算子与
//                    手写面板那两面照旧归 U-14／U-8。
void ParamPanelBindingTest::testCreationPhaseLeavesNoWriteBackTrace()
{
    const QList<U7Arm> arms = {ArmInt, ArmDouble, ArmBool, ArmString, ArmFilePath,
                               ArmMultiLine, ArmEnum, ArmPoint, ArmRect};
    QStringList problems;
    int readbacks = 0;      // 创建期回读次数（应 13 控件 × 2 场景 = 26）
    int sameWrites = 0;     // 同值写回探针次数（每臂每场景一次 = 18）
    int diffWrites = 0;     // 异值直写探针次数（同上 = 18）

    for (int scenario = 0; scenario < 2; ++scenario) {
        const QString stag = scenario == 0 ? QStringLiteral("S0") : QStringLiteral("S1");
        for (U7Arm arm : arms) {
            const QString tag = QStringLiteral("%1/%2").arg(stag, QString::fromLatin1(u7ArmTag(arm)));
            U7TypeFixture node(arm);
            node.init();
            node.setAutoPreviewEnabled(true);   // 让任何一次经 setParam 的写回都留下定时器痕迹
            const QStringList expected = scenario == 0 ? node.beforeDisplays() : node.afterDisplays();
            if (scenario == 1) {
                node.applyAfterValues();        // 合法改表（本身会点定时器）
                node.stopPreviewTimer();        // 归零 ⇒ 之后定时器再起来只能由建面板造成
            }
            if (node.previewTimerActive())
                problems << QStringLiteral("%1 :: 建面板之前定时器就没归零，计时器腿无从判定").arg(tag);

            const QMap<QString, QVariant> table0 = node.tableSnapshot();
            QWidget *panel = node.createParamPanel();
            if (!panel) {
                problems << QStringLiteral("%1 :: createParamPanel 返回空").arg(tag);
                continue;
            }

            // ① 先钉住"播种落到了镜像清单点名的那批控件上"——认错控件必须在这里红，
            //     不能伪装成下面②③的"零痕迹"
            QList<QWidget *> controls;
            for (const ParamSpec &s : node.paramSpecs()) {
                for (const NamedControl &c : controlsTheRefresherLooksFor(s)) {
                    QWidget *w = findChildByNameAndClass(panel, c.cls, c.name);
                    if (!w) {
                        problems << QStringLiteral("%1 :: 镜像清单点名的控件 objectName=\"%2\" class=%3 没建出来")
                                        .arg(tag, c.name, QString::fromLatin1(c.cls));
                        continue;
                    }
                    controls << w;
                }
            }
            if (controls.size() != expected.size()) {
                problems << QStringLiteral("%1 :: 找到 %2 个控件，应为 %3 个")
                                .arg(tag).arg(controls.size()).arg(expected.size());
            } else {
                for (int i = 0; i < controls.size(); ++i) {
                    const QString got = u7DisplayOf(controls.at(i));
                    if (got != expected.at(i))
                        problems << QStringLiteral("%1 :: 创建期回读 #%2 = \"%3\"，应为 \"%4\""
                                                   "（播种没落到这里 ⇒ ②③的零痕迹是空转）")
                                        .arg(tag).arg(i).arg(got, expected.at(i));
                    ++readbacks;
                }
            }

            // ② 建面板期间参数表逐键不得变
            const QString diff = u10TableDiffText(table0, node.tableSnapshot());
            if (!diff.isEmpty())
                problems << QStringLiteral("%1 :: 建面板期间参数表被改写：%2").arg(tag, diff);

            // ③ 建面板期间预览防抖定时器不得被点起来
            const bool timerAfter = node.previewTimerActive();
            if (timerAfter)
                problems << QStringLiteral("%1 :: 建面板期间预览防抖定时器被点起（创建期写回留下了痕迹）").arg(tag);

            qDebug().noquote() << QStringLiteral("u16|t10 arm=%1 scenario=%2 controls=%3"
                                                 " table_diff_empty=%4 timer_after_create=%5")
                                    .arg(QString::fromLatin1(u7ArmTag(arm)), stag)
                                    .arg(controls.size())
                                    .arg(diff.isEmpty() ? 1 : 0)
                                    .arg(timerAfter ? 1 : 0);

            if (controls.isEmpty()) {
                problems << QStringLiteral("%1 :: 一个控件都没找到，两条探针无从执行").arg(tag);
                delete panel;
                continue;
            }
            QWidget *w = controls.first();

            // ④a 同值写回的运行期语义（§3.14 把它当前提，这里跑成读数）：不改表是硬要求；
            //      定时器起没起按臂记录，不预设结论
            {
                const QMap<QString, QVariant> base = node.tableSnapshot();
                if (!u10SameValueWrite(w)) {
                    problems << QStringLiteral("%1 :: 同值写回探针没有覆盖该类控件").arg(tag);
                } else {
                    ++sameWrites;
                    const QString d = u10TableDiffText(base, node.tableSnapshot());
                    const bool t = node.previewTimerActive();
                    node.stopPreviewTimer();
                    qDebug().noquote() << QStringLiteral("u16|t10-same arm=%1 scenario=%2"
                                                         " changed_table=%3 started_timer=%4")
                                            .arg(QString::fromLatin1(u7ArmTag(arm)), stag)
                                            .arg(d.isEmpty() ? 0 : 1).arg(t ? 1 : 0);
                    if (!d.isEmpty())
                        problems << QStringLiteral("%1 :: 同值写回竟然改了参数表：%2").arg(tag, d);
                }
            }

            // ④b 异值直写必须留痕：否则 ②③ 的"零痕迹"是空转（写回线压根没接上）
            {
                const QMap<QString, QVariant> base = node.tableSnapshot();
                if (!u9ProbeWrite(w)) {
                    problems << QStringLiteral("%1 :: 异值直写探针没有覆盖该类控件").arg(tag);
                } else {
                    ++diffWrites;
                    const QString d = u10TableDiffText(base, node.tableSnapshot());
                    const bool t = node.previewTimerActive();
                    node.stopPreviewTimer();
                    qDebug().noquote() << QStringLiteral("u16|t10-probe arm=%1 scenario=%2"
                                                        " changed_table=%3 started_timer=%4")
                                            .arg(QString::fromLatin1(u7ArmTag(arm)), stag)
                                            .arg(d.isEmpty() ? 0 : 1).arg(t ? 1 : 0);
                    if (d.isEmpty())
                        problems << QStringLiteral("%1 :: 直写控件没改参数表 ⇒ ② 是空转"
                                                   "（写回线没接到 setParam）").arg(tag);
                    if (!t)
                        problems << QStringLiteral("%1 :: 直写控件没点起预览定时器 ⇒ ③ 是空转").arg(tag);
                }
            }
            delete panel;
        }
    }
    // 九臂 × 两场景：回读 26 次、两条探针各 18 次 ⇒ 少一臂或整个场景被跳过，当场红
    QCOMPARE(readbacks, 26);
    QCOMPARE(sameWrites, 18);
    QCOMPARE(diffWrites, 18);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("U16-CREATION: 创建期有 %1 处越界或空转（明细逐条随附）\n%2")
                            .arg(problems.size()).arg(problems.join(QStringLiteral("\n")))));
}

// ── T11（U-16 路线②）：播种到底落在哪条腿／哪个对象 ──
// §3.14 表 3 里"createAutoParamPanel 对 String／FilePath 走的是 else 分支（:987 findChild），
// :982～:984 那条腿连同它的 blocker 不可达"是**读盘推导**。本条把它换成运行期读数：
// 行内控件（QFormLayout 的 field）到底是不是 QLineEdit、参数名到底挂在容器还是子件上、
// Point／Rect 的"参数名"对象是否存在。外加越界表值的夹取读数（只夹显示、不回写、不报警）。
void ParamPanelBindingTest::testCreationSeedLandingLegIsPinned()
{
    const QList<U7Arm> arms = {ArmInt, ArmDouble, ArmBool, ArmString, ArmFilePath,
                               ArmMultiLine, ArmEnum, ArmPoint, ArmRect};
    QStringList problems;
    int legReadings = 0;
    int rangeReadings = 0;

    for (U7Arm arm : arms) {
        const QString tag = QString::fromLatin1(u7ArmTag(arm));
        U7TypeFixture node(arm);
        node.init();
        QWidget *panel = node.createParamPanel();
        if (!panel) {
            problems << QStringLiteral("%1 :: createParamPanel 返回空").arg(tag);
            continue;
        }
        const QString pname = node.paramSpecs().constFirst().name;
        QWidget *namedQLine = findChildByNameAndClass(panel, "QLineEdit", pname);   // :987 那条 findChild 的落点
        QWidget *namedAny = widgetNamedAnywhere(panel, pname);                       // :950 具名覆盖的落点
        const bool hasNamedLineEdit = namedQLine != nullptr;
        const bool hasNamedAny = namedAny != nullptr;

        // 播种落点的三类形态（逐类抄自 createAutoParamPanel 的 :946～:951 与 :961～:1023）：
        //  单值类型（Int/Double/Bool/MultiLine/Enum）：editor 自身被覆盖成参数名 ⇒ 直接命中该类的 editor
        //  String／FilePath：editor 是容器（不覆盖 objectName），参数名挂在**子件** QLineEdit 上
        //  Point／Rect：子件叫 "<名>_x" 等 ⇒ 叫"参数名"本身的对象根本不存在
        QList<QWidget *> controls;
        for (const ParamSpec &s : node.paramSpecs()) {
            for (const NamedControl &c : controlsTheRefresherLooksFor(s)) {
                QWidget *w = findChildByNameAndClass(panel, c.cls, c.name);
                if (!w) {
                    problems << QStringLiteral("%1 :: 镜像清单点名的控件 objectName=\"%2\" class=%3 没建出来")
                                    .arg(tag, c.name, QString::fromLatin1(c.cls));
                    continue;
                }
                controls << w;
            }
        }
        if (controls.isEmpty()) {
            problems << QStringLiteral("%1 :: 一个控件都没找到").arg(tag);
            delete panel;
            continue;
        }

        const bool composite = (arm == ArmPoint || arm == ArmRect);
        const bool lineFamily = (arm == ArmString || arm == ArmFilePath);
        QWidget *first = controls.first();
        QWidget *field = fieldOfWidget(first);
        if (!field) {
            problems << QStringLiteral("%1 :: 爬不到挂 QFormLayout 的那一层，落点无从判定").arg(tag);
            delete panel;
            continue;
        }
        const bool fieldEqualsControl = (field == first);
        const bool fieldIsLineEdit = (qobject_cast<QLineEdit *>(field) != nullptr);

        // 口径：单值类型的 field 就是控件本身（:950 把 editor 的 objectName 覆盖成参数名）；
        //       String／FilePath 与 Point／Rect 的 field 是容器 ⇒ 具名对象只能是容器里的子件
        if (!lineFamily && !composite) {
            if (!fieldEqualsControl)
                problems << QStringLiteral("%1 :: 单值类型的 field 竟然不是被点名的控件本身（field class=%2）")
                                .arg(tag, QString::fromLatin1(field->metaObject()->className()));
            if (!hasNamedAny)
                problems << QStringLiteral("%1 :: 没有任何对象的 objectName 等于参数名 ⇒ :950 的具名覆盖变了").arg(tag);
        }
        if (lineFamily) {
            // 这一条就是 :982～:984 那条腿不可达的运行期依据：field（＝产品代码里的 editor）
            // 不是 QLineEdit ⇒ 那个 qobject_cast 恒假 ⇒ 真正执行的是 :987 的 findChild 分支
            if (fieldIsLineEdit)
                problems << QStringLiteral("%1 :: field 本身就是 QLineEdit ⇒ :982 那条腿变成活腿"
                                           "（:983 的 blocker 不再是死代码）").arg(tag);
            if (!hasNamedLineEdit)
                problems << QStringLiteral("%1 :: 子件没挂上参数名 ⇒ :987 那条分支的落点变了").arg(tag);
        }
        if (composite) {
            if (fieldEqualsControl)
                problems << QStringLiteral("%1 :: 复合参数的 field 竟然直接就是被点名的子件").arg(tag);
            if (hasNamedAny)
                problems << QStringLiteral("%1 :: 复合参数不该存在一个叫参数名本身的对象（口径：只有带后缀的子件）").arg(tag);
        }
        ++legReadings;
        qDebug().noquote() << QStringLiteral("u16|t11 arm=%1 controls=%2 named_lineedit=%3 named_any=%4"
                                             " field_class=%5 field_equals_control=%6 field_is_lineedit=%7")
                                .arg(tag).arg(controls.size())
                                .arg(hasNamedLineEdit ? 1 : 0)
                                .arg(hasNamedAny ? 1 : 0)
                                .arg(QString::fromLatin1(field->metaObject()->className()))
                                .arg(fieldEqualsControl ? 1 : 0)
                                .arg(fieldIsLineEdit ? 1 : 0);

        // 越界表值读数：只夹显示，参数表原值不动（也没有任何人报警）
        const QVariant oor = node.outOfRangeValue();
        if (oor.isValid()) {
            node.writeTableDirect(oor);
            QWidget *panel2 = node.createParamPanel();
            if (!panel2) {
                problems << QStringLiteral("%1 :: 越界臂 createParamPanel 返回空").arg(tag);
            } else {
                QWidget *w2 = findChildByNameAndClass(panel2, controlsTheRefresherLooksFor(
                                          node.paramSpecs().constFirst()).constFirst().cls,
                                      pname);
                const QString shown = w2 ? u7DisplayOf(w2) : QStringLiteral("<not found>");
                const QVariant kept = node.getParam(pname);
                const QString want = node.outOfRangeDisplay();
                qDebug().noquote() << QStringLiteral("u16|t11-range arm=%1 table_value=%2"
                                                     " displayed=%3 expected_display=%4")
                                        .arg(tag).arg(kept.toString(), shown, want);
                if (shown != want)
                    problems << QStringLiteral("%1 :: 越界表值 %2 的显示夹取读数 = \"%3\"，应为 \"%4\"")
                                    .arg(tag).arg(oor.toString(), shown, want);
                if (kept != oor)
                    problems << QStringLiteral("%1 :: 创建期把越界值夹好后回写进了参数表（%2 -> %3）"
                                               "——与本用例钉住的口径相反")
                                    .arg(tag).arg(oor.toString(), kept.toString());
                ++rangeReadings;
                delete panel2;
            }
        }
        delete panel;
    }
    QCOMPARE(legReadings, 9);
    QCOMPARE(rangeReadings, 3);
    QVERIFY2(problems.isEmpty(),
             qPrintable(QStringLiteral("U16-LEGS: 播种落点有 %1 处与口径不符（明细逐条随附）\n%2")
                            .arg(problems.size()).arg(problems.join(QStringLiteral("\n")))));
}


QTEST_MAIN(ParamPanelBindingTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "param_panel_binding_test.moc"

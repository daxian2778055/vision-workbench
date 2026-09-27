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
// ⚠️ 边界（推进计划 §3.10 的 C4b ⇒ 登记为 U-7）：那份 (名字,类型) 清单是本文件里**手抄的镜像**，
//      不产品代码读。改坏"建控件"侧的命名可检（C1／C2 实测红），只改"刷新"侧的查找名检不出——
//      要闭上这条，需要刷新后回读控件值（那是另一轮）。
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

QTEST_MAIN(ParamPanelBindingTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "param_panel_binding_test.moc"

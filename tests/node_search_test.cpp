#include <QtTest>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>

#include "NodeRegistry.h"
#include "NodeSearchWidget.h"
#include "FlowScene.h"
#include "NodeBase.h"

// FR1.7「按中英文名/别名/模块号搜索」的口径测试（U-100）。
//
// 这一条挂了三周，形状很特别：不是代码写错了，是**文档说有、代码其实没有**。SRS FR1.7 与 FR18.4
// 写着"按中英文名/别名搜索"，而注册表里 `englishName`/`aliases` 两个键确实有（清点件读 active VFP_REG
// 73 行、addAliases 24 处；非空英文名与别名条数由下面 testSearchSurfaceDenominators 自己打出来），
// 只是两处搜索框一个都不读——`englishName`/`aliases` 全仓只被
// NodeRegistry::findByName 用来解析 .vfp 里的类型，与搜索框无关。
//
// 本文件钉四件事：
//  ① 口径只有一处定义（NodeRegistry::matchesSearchKeyword），不是两处站点各抄一份；
//  ② 中文名 / 英文名 / 别名三条都真的命中，且大小写不敏感、能命中子串（现场打"thresh"也要找得到）；
//  ③ 类型 id 与模块号**不进**这枚谓词——工具箱那条要求关键字 >=2 字符、Ctrl+F 判的是节点自带的模块号，
//     两处规则不同，留在各自调用点；谓词把空关键字判 false，"空即全显"由调用方自己判。
//  ④ 画布 Ctrl+F 那一侧走真控件：算子被用户改过名（name 里再没有 "Threshold"）之后，打英文 token
//     仍要能定位到它——这一条是本轮改前的红腿，也是"谓词接进了站点、不是只存在着一枚谓词"的证据。
class NodeSearchTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testSearchSurfaceDenominators();
    void testPredicateCoversAllThreeNames();
    void testPredicateKeepsTypeIdAndModuleNumberOut();
    void testEveryRegistrationMatchesItsOwnEnglishNameAndAliases();
    void testCtrlFFindsRenamedNodeByEnglishToken();
    void testCtrlFFindsRenamedNodeByAlias();
    void testCtrlFStillFindsByNodeNameAndModuleNumber();
    void testCtrlFWithEmptyKeywordShowsNothing();

private:
    void makeSearchWithThresholdNode(FlowScene *scene, NodeSearchWidget **searchOut,
                                     NodeBase **nodeOut);
};

namespace {

const QString kThresholdId = QStringLiteral("OpencvThresholdNode");
const QString kChineseName = QStringLiteral("二值化");
const QString kEnglishName = QStringLiteral("Threshold");
const QString kAliasWithChinese = QStringLiteral("OpenCV二值化");

const NodeRegistration *registration(const QString &id)
{
    return NodeRegistry::instance().findById(id);
}

int aliasTotal()
{
    int n = 0;
    for (const NodeRegistration &reg : NodeRegistry::instance().all())
        n += reg.aliases.size();
    return n;
}

}   // namespace

void NodeSearchTest::initTestCase()
{
    registerAllNodes();
}

/// 分母先钉住：后面几条腿找的是"某一枚算子的英文名/别名"，若注册表本身空了，那些腿会一起变成空转。
void NodeSearchTest::testSearchSurfaceDenominators()
{
    const QList<NodeRegistration> &regs = NodeRegistry::instance().all();
    int withEnglish = 0;
    int withAlias = 0;
    for (const NodeRegistration &reg : regs) {
        if (!reg.englishName.isEmpty()) ++withEnglish;
        if (!reg.aliases.isEmpty()) ++withAlias;
    }
    qInfo().noquote() << QStringLiteral("[U100-NS-DATA] regs=%1 with_english=%2 with_alias=%3 "
                                       "alias_total=%4")
                             .arg(regs.size()).arg(withEnglish).arg(withAlias).arg(aliasTotal());
    QVERIFY2(regs.size() >= 70, "registry thinner than the 70 this test's denominators were taken at");
    QVERIFY2(withEnglish >= regs.size() - 2,
             "englishName coverage collapsed: the search half of FR1.7 has nothing to match");
    QVERIFY2(withAlias >= 20, "alias coverage collapsed");
}

/// 谓词本体：三种称呼都命中，大小写与子串都命中，不认识的东西不命中。
void NodeSearchTest::testPredicateCoversAllThreeNames()
{
    const NodeRegistration *reg = registration(kThresholdId);
    QVERIFY2(reg != nullptr, "OpencvThresholdNode must stay registered or these legs test nothing");
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, kChineseName));
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, kEnglishName));
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, kEnglishName.toLower()));
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, QStringLiteral("thresh")));
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, kAliasWithChinese));
    QVERIFY(NodeRegistry::matchesSearchKeyword(*reg, QStringLiteral("opencv threshold")));
    QVERIFY(!NodeRegistry::matchesSearchKeyword(*reg, QStringLiteral("zzqx-not-a-term")));
}

/// 谓词的边界也是契约：类型 id 与模块号不在这里判（两处站点各自判、规则不同），空关键字判 false。
void NodeSearchTest::testPredicateKeepsTypeIdAndModuleNumberOut()
{
    const NodeRegistration *reg = registration(kThresholdId);
    QVERIFY(reg != nullptr);
    QVERIFY2(!NodeRegistry::matchesSearchKeyword(*reg, kThresholdId),
             "the predicate must not quietly grow a fourth term; the id rule lives at the call sites "
             "because the toolbox asks for >= 2 characters and Ctrl+F asks for the node's module number");
    QVERIFY(!NodeRegistry::matchesSearchKeyword(*reg, QStringLiteral("42")));
    QVERIFY2(!NodeRegistry::matchesSearchKeyword(*reg, QString()),
             "empty keyword is the caller's \"show everything\" branch, not a hit here");
    QVERIFY(!NodeRegistry::matchesSearchKeyword(*reg, QStringLiteral("   ")));
}

/// 逐枚核：每一枚算子用自己的英文名、自己的每一条别名都该搜得到。0 项不算绿。
void NodeSearchTest::testEveryRegistrationMatchesItsOwnEnglishNameAndAliases()
{
    int englishChecked = 0;
    int aliasChecked = 0;
    QStringList failures;
    for (const NodeRegistration &reg : NodeRegistry::instance().all()) {
        if (!reg.englishName.isEmpty()) {
            ++englishChecked;
            if (!NodeRegistry::matchesSearchKeyword(reg, reg.englishName))
                failures << QStringLiteral("english:%1").arg(reg.id);
        }
        for (const QString &alias : reg.aliases) {
            ++aliasChecked;
            if (!NodeRegistry::matchesSearchKeyword(reg, alias))
                failures << QStringLiteral("alias:%1").arg(reg.id);
        }
    }
    qInfo().noquote() << QStringLiteral("[U100-NS-SWEEP] english_checked=%1 alias_checked=%2 "
                                        "failures=%3 list=%4")
                             .arg(englishChecked).arg(aliasChecked).arg(failures.size())
                             .arg(failures.join(QLatin1Char(',')));
    QVERIFY2(englishChecked >= 70 && aliasChecked >= 40,
             "the sweep walked almost nothing, so a green here would mean nothing");
    QCOMPARE(failures.join(QLatin1Char(',')), QString());
}

/// 真控件：把 Ctrl+F 那一侧接进搜索框——算子改过名之后，英文名/别名仍要能定位到它。
/// NodeSearchWidget 的搜索框与结果表在 U-100 具了名（nodeSearchEdit / nodeSearchResults /
/// nodeSearchCount），否则这一条腿只能靠读源码的措辞相信。
void NodeSearchTest::makeSearchWithThresholdNode(FlowScene *scene, NodeSearchWidget **searchOut,
                                                 NodeBase **nodeOut)
{
    NodeBase *node = scene->createNodeByTypeIdOrName(kThresholdId, NodeBase::LOGIC, kThresholdId);
    QVERIFY2(node != nullptr, "the fixture operator must come out of the registry");
    scene->adoptNode(node, QPointF(0, 0));
    // 用户改过名：改完之后 name 里既没有 "Threshold" 也没有别名，只有中文"我的阈值"。
    node->setName(QStringLiteral("我的阈值"));
    *nodeOut = node;

    auto *search = new NodeSearchWidget();
    search->setFlowScene(scene);
    *searchOut = search;
}

void NodeSearchTest::testCtrlFFindsRenamedNodeByEnglishToken()
{
    FlowScene scene;
    NodeBase *node = nullptr;
    NodeSearchWidget *search = nullptr;
    makeSearchWithThresholdNode(&scene, &search, &node);
    QVERIFY(node->name() == QStringLiteral("我的阈值"));

    auto *edit = search->findChild<QLineEdit *>(QStringLiteral("nodeSearchEdit"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("nodeSearchResults"));
    QVERIFY2(edit && results, "Ctrl+F widgets must stay named or this leg silently stops measuring");
    QVERIFY(results->count() == 0);

    edit->setText(kEnglishName);
    QTRY_COMPARE(results->count(), 1);
    QCOMPARE(results->item(0)->data(Qt::UserRole).value<qulonglong>(),
             reinterpret_cast<qulonglong>(node));
    delete search;
}

void NodeSearchTest::testCtrlFFindsRenamedNodeByAlias()
{
    FlowScene scene;
    NodeBase *node = nullptr;
    NodeSearchWidget *search = nullptr;
    makeSearchWithThresholdNode(&scene, &search, &node);

    auto *edit = search->findChild<QLineEdit *>(QStringLiteral("nodeSearchEdit"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("nodeSearchResults"));
    QVERIFY(edit && results && node);

    edit->setText(kAliasWithChinese);
    QTRY_COMPARE(results->count(), 1);
    edit->setText(QStringLiteral("opencv threshold"));   // 另一条别名的大小写两种写法
    QTRY_COMPARE(results->count(), 1);
    delete search;
}

/// 回归：改前的两条老路（节点自带名、模块号）不能被新口径顶掉。
void NodeSearchTest::testCtrlFStillFindsByNodeNameAndModuleNumber()
{
    FlowScene scene;
    NodeBase *node = nullptr;
    NodeSearchWidget *search = nullptr;
    makeSearchWithThresholdNode(&scene, &search, &node);

    auto *edit = search->findChild<QLineEdit *>(QStringLiteral("nodeSearchEdit"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("nodeSearchResults"));
    QVERIFY(edit && results && node);

    edit->setText(QStringLiteral("阈值"));             // 节点自己改出来的名
    QTRY_COMPARE(results->count(), 1);
    edit->setText(QString::number(node->moduleId()));   // 模块号
    QTRY_COMPARE(results->count(), 1);
    delete search;
}

void NodeSearchTest::testCtrlFWithEmptyKeywordShowsNothing()
{
    FlowScene scene;
    NodeBase *node = nullptr;
    NodeSearchWidget *search = nullptr;
    makeSearchWithThresholdNode(&scene, &search, &node);

    auto *edit = search->findChild<QLineEdit *>(QStringLiteral("nodeSearchEdit"));
    auto *results = search->findChild<QListWidget *>(QStringLiteral("nodeSearchResults"));
    auto *count = search->findChild<QLabel *>(QStringLiteral("nodeSearchCount"));
    QVERIFY(edit && results && count);

    edit->setText(kEnglishName);
    QTRY_COMPARE(results->count(), 1);
    edit->setText(QString());                           // 清空 = 撤销定位，不是"全显"
    QTRY_COMPARE(results->count(), 0);
    delete search;
}

QTEST_MAIN(NodeSearchTest)

// 源文件内定义 Q_OBJECT 类时，AUTOMOC 要求显式包含生成的 moc
#include "node_search_test.moc"

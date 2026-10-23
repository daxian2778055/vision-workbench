// ProjectPathsTest（179 行）：方案内文件路径面的纯行为座。
// 不碰文件内容与网络，只判形态：空参 / 相对形态 / 方案目录内绝对 / 目录外绝对、
// 相对解析锚定方案目录（未落盘方案回落 cwd）、拾取换相对形态、载入期审查计数。
// 红臂破坏位 = 目录内判定取反（InSchemeDir↔OutsideSchemeDir 换位）——分类腿与
// 审查计数腿当场红。
//
// U-201 增三腿（载入门禁：目录外绝对路径须显式确认）：未确认即拒载（场景清单清空、
// 留痕一句、当前方案保持不变）；确认后放行并留痕；无目录外路径时确认器不得被叫起。
// 红臂破坏位 = 绕过确认器（确认结果一律当放行）——拒载腿当场红，放行腿与负对照腿原样绿。
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "FlowScene.h"
#include "HalconNode.h"
#include "NodeBase.h"
#include "ParamSpec.h"
#include "ProjectManager.h"
#include "ProjectPaths.h"
#include "SchemePackage.h"

class ProjectPathsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void testClassifyShapes();
    void testResolveShapes();
    void testMakeRelativeIfInside();
    void testSchemeBaseFunnel();
    void testReviewScenesCounting();
    void testPathReviewSummaryText();
    // U-201 载入门禁三腿
    void testOutsideSchemePathNeedsConfirmation();        // 未确认即拒载（清单清空 + 留痕）
    void testOutsideSchemePathAllowedAfterConfirm();       // 确认后放行（并留痕）
    void testSchemeWithoutOutsidePathSkipsConfirmation();  // 负对照：无目录外路径不叫确认器
    // U-214 失败回滚腿
    void testFailedLoadRestoresSnapshotAndReadonly();      // r11 P2-9：失败支回滚三件套（成功才提交）

private:
    /// 造一份带指定 filePath 的方案文件（走真写侧序列化 buildProjectJson），返回是否落盘。
    bool writeSchemeWithFilePath(const QString &filePathValue);

    QTemporaryDir m_tmp;           // 方案目录：plan.vfp 落此
    QString m_scheme;              // 方案文件绝对路径
    QString m_savedCwd;            // 测试期 cwd 回恢复位
};

void ProjectPathsTest::initTestCase()
{
    QVERIFY2(m_tmp.isValid(), "临时目录取不到，本座失去前提");
    m_scheme = m_tmp.filePath(QStringLiteral("plan.vfp"));
    m_savedCwd = QDir::currentPath();
    QDir::setCurrent(m_tmp.path());   // 未落盘方案回落 cwd 的腿需要确定锚点
}

void ProjectPathsTest::cleanupTestCase()
{
    QDir::setCurrent(m_savedCwd);
    ProjectPaths::setCurrentSchemeFilePath(QString());
}

void ProjectPathsTest::testClassifyShapes()
{
    using ProjectPaths::classifySchemePath;
    using ProjectPaths::PathScope;
    QCOMPARE(int(classifySchemePath(QString(), m_scheme)), int(PathScope::Empty));
    QCOMPARE(int(classifySchemePath(QStringLiteral("   "), m_scheme)), int(PathScope::Empty));
    QCOMPARE(int(classifySchemePath(QStringLiteral("images/a.png"), m_scheme)),
             int(PathScope::RelativeRaw));
    QCOMPARE(int(classifySchemePath(QStringLiteral("./images/a.png"), m_scheme)),
             int(PathScope::RelativeRaw));
    const QString inside = m_tmp.filePath(QStringLiteral("template/t1.png"));
    QCOMPARE(int(classifySchemePath(inside, m_scheme)), int(PathScope::InSchemeDir));
    const QString outside = QDir(QDir::tempPath()).filePath(QStringLiteral("vfp163_outside/x.png"));
    QCOMPARE(int(classifySchemePath(outside, m_scheme)), int(PathScope::OutsideSchemeDir));
    // 未落盘方案：基目录回落 cwd——cwd 已由 initTestCase 钉在 m_tmp
    const QString cwdInside = m_tmp.filePath(QStringLiteral("cwd_inside.png"));
    QCOMPARE(int(classifySchemePath(cwdInside, QString())), int(PathScope::InSchemeDir));
    // 兄弟目录不得沾边（前缀判断红线：基目录名的纯字符串拼接非同界）
    const QString sibling = m_tmp.path() + QStringLiteral("_other/x.png");
    QCOMPARE(int(classifySchemePath(sibling, m_scheme)), int(PathScope::OutsideSchemeDir));
}

void ProjectPathsTest::testResolveShapes()
{
    using ProjectPaths::resolveSchemePath;
    const QString resolved = resolveSchemePath(QStringLiteral("assets/t.png"), m_scheme);
    const QString want = m_tmp.filePath(QStringLiteral("assets/t.png"));
    QVERIFY2(QFileInfo(resolved).absoluteFilePath()
                 .compare(QFileInfo(want).absoluteFilePath(), Qt::CaseInsensitive) == 0,
             qPrintable(QStringLiteral("相对解析错位：%1 ≠ %2").arg(resolved, want)));
    // 绝对形态原样透传（cleanPath 统一斜杠方向；存量方案行为不变）
    const QString back = QDir::fromNativeSeparators(want).replace(QLatin1Char('/'), QLatin1Char('\\'));
    QCOMPARE(resolveSchemePath(back, m_scheme), QDir::cleanPath(want));
    QVERIFY(resolveSchemePath(QString(), m_scheme).isEmpty());
    // 未落盘方案（空串路径）→ 锚 cwd（initTestCase 已钉）
    QCOMPARE(resolveSchemePath(QStringLiteral("rel/x.txt"), QString()),
             QDir::cleanPath(m_tmp.filePath(QStringLiteral("rel/x.txt"))));
    // 空参仅判定 Empty，解析仍给真实锚定值（读数不虚造）
    QVERIFY(!resolveSchemePath(QStringLiteral("images/a.png"), m_scheme).isEmpty());
}

void ProjectPathsTest::testMakeRelativeIfInside()
{
    using ProjectPaths::makeSchemeRelativeIfInside;
    using ProjectPaths::resolveSchemePath;
    const QString inside = m_tmp.filePath(QStringLiteral("inside/m.png"));
    const QString rel = makeSchemeRelativeIfInside(m_scheme, inside);
    QCOMPARE(rel, QStringLiteral("inside/m.png"));
    // 往返闭合：换相对后再解析，回到同一绝对（本拍板自洽性的主钉）
    QVERIFY2(QFileInfo(resolveSchemePath(rel, m_scheme)).absoluteFilePath()
                 .compare(QFileInfo(inside).absoluteFilePath(), Qt::CaseInsensitive) == 0,
             qPrintable(QStringLiteral("相对往返不闭合：%1 → %2").arg(rel,
                 resolveSchemePath(rel, m_scheme))));
    // 目录外拾取原样保留（现场绝对路径方案不被动）
    const QString outside = QDir(QDir::tempPath()).filePath(QStringLiteral("vfp163_outside/y.png"));
    QCOMPARE(makeSchemeRelativeIfInside(m_scheme, outside), outside);
    // 未落盘方案（无锚）不做转换——保持旧行为
    QCOMPARE(makeSchemeRelativeIfInside(QString(), inside), inside);
    // 已经是相对形态的入参原样保留（拾取入口只会到绝对拾取）
    QCOMPARE(makeSchemeRelativeIfInside(m_scheme, QStringLiteral("already/rel.png")),
             QStringLiteral("already/rel.png"));
}

void ProjectPathsTest::testSchemeBaseFunnel()
{
    // 记录点唯一漏斗：ProjectManager::setLastFilePath 同步喂数进程级快照位，
    // 节点执行线程不接触 ProjectManager 成员本身
    ProjectManager pm;
    pm.setLastFilePath(m_scheme);
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), m_scheme);
    pm.setLastFilePath(QString());
    QVERIFY2(ProjectPaths::currentSchemeFilePath().isEmpty(),
             "漏斗复位后快照位应当清空——否则下一次载入前的执行还会读上一份基目录");
    // 便捷位往返：resolveExecutionPath 走快照位
    pm.setLastFilePath(m_scheme);
    QVERIFY2(ProjectPaths::resolveExecutionPath(QStringLiteral("funnel/t.png"))
                 .endsWith(QStringLiteral("funnel/t.png")),
             "便捷位解析应落到方案目录锚定值");
    pm.setLastFilePath(QString());
}

void ProjectPathsTest::testReviewScenesCounting()
{
    FlowScene scene;
    const QList<FlowScene *> scenes{ &scene };

    // ① 遗传键目录外绝对（ImageReadNode 工厂槽位）
    NodeBase *reader = scene.createNode(NodeBase::IMAGE_ACQUISITION,
                                        QPointF(100, 200), QStringLiteral("读旧外"));
    QVERIFY2(reader != nullptr, "工厂没给读取节点，本腿失去前提");
    reader->setParam(QStringLiteral("filePath"),
                     QDir(QDir::tempPath()).filePath(QStringLiteral("vfp163_outside/r1.png")));

    // ② 遗传键相对形态（手写相对 Param 不经拾取也可能存在）
    NodeBase *reader2 = scene.createNode(NodeBase::IMAGE_ACQUISITION,
                                         QPointF(100, 400), QStringLiteral("读旧相"));
    reader2->setParam(QStringLiteral("filePath"), QStringLiteral("images/rel.png"));

    // ③ 规格键目录外绝对：给共用基节点补一枚 FilePath 规格（模板识别族真形态）
    NodeBase *tpl = scene.createNode(NodeBase::OUTPUT, QPointF(300, 200), QStringLiteral("模板探"));

    // ④ 空参数不计数
    NodeBase *plain = scene.createNode(NodeBase::OUTPUT, QPointF(300, 400), QStringLiteral("空参"));
    plain->setParam(QStringLiteral("filePath"), QString());

    auto *tplHn = qobject_cast<HalconNode *>(tpl);
    QVERIFY2(tplHn != nullptr, "输出工厂槽位不是 HalconNode，本腿失去前提");
    tplHn->registerParams({makeFilePathParam(QStringLiteral("templatePath"), QString(),
                                             QStringLiteral("模板"))});
    tplHn->setParam(QStringLiteral("templatePath"),
                    QDir(QDir::tempPath()).filePath(QStringLiteral("vfp163_outside/tpl.onnx")));

    const ProjectPaths::PathReview review =
        ProjectPaths::reviewScenes(scenes, m_scheme);
    QCOMPARE(review.outsideCount, 2);
    QCOMPARE(review.relativeCount, 1);
    QCOMPARE(review.outsideHits.size(), 2);
    QVERIFY2(review.outsideHits.at(0).paramName == QStringLiteral("filePath")
                 || review.outsideHits.at(1).paramName == QStringLiteral("filePath"),
             "命中条目应携带参数名");
    QVERIFY2(review.outsideHits.at(0).rawPath.contains(QStringLiteral("vfp163_outside")),
             "命中载荷应携带原路径");
}

void ProjectPathsTest::testPathReviewSummaryText()
{
    ProjectPaths::PathReview empty;
    QVERIFY(ProjectPaths::pathReviewSummary(empty).isEmpty());

    ProjectPaths::PathReview some;
    some.outsideCount = 2;
    some.relativeCount = 1;
    ProjectPaths::PathReviewHit hit;
    hit.nodeName = QStringLiteral("n");
    hit.paramName = QStringLiteral("filePath");
    hit.rawPath = QStringLiteral("C:/somewhere/x.png");
    hit.scope = ProjectPaths::PathScope::OutsideSchemeDir;
    some.outsideHits.append(hit);
    const QString text = ProjectPaths::pathReviewSummary(some);
    QVERIFY2(text.contains(QStringLiteral("2 处")), qPrintable(QStringLiteral("总纲句应携带处数：%1").arg(text)));
    QVERIFY2(text.contains(QStringLiteral("相对路径")), "总纲句应把相对形态点名");
}

bool ProjectPathsTest::writeSchemeWithFilePath(const QString &filePathValue)
{
    // 走真写侧的序列化（buildProjectJson）：座里不另写一套拼装，免得与产线形状分叉。
    FlowScene scene;
    NodeBase *reader = scene.createNode(NodeBase::IMAGE_ACQUISITION, QPointF(100, 200),
                                        QStringLiteral("读旧外"));
    if (!reader)
        return false;
    reader->setParam(QStringLiteral("filePath"), filePathValue);
    ProjectManager pm;
    QJsonObject root = pm.buildProjectJson(QList<FlowScene *>{ &scene });
    root.remove(QStringLiteral("runtimeLayout"));   // 座内不碰运行界面布局文件（免得写应用目录）
    QFile file(m_scheme);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QByteArray payload = QJsonDocument(root).toJson();
    return file.write(payload) == payload.size();
}

void ProjectPathsTest::testOutsideSchemePathNeedsConfirmation()
{
    // U-201：含方案目录外绝对路径的方案，载入前必须取得一次显式确认；未确认即拒载。
    const QString outside =
        QDir(QDir::tempPath()).filePath(QStringLiteral("vfp201_outside/r1.png"));
    QVERIFY2(writeSchemeWithFilePath(outside), "方案素材落盘失败");

    ProjectManager loader;
    int asked = 0;
    loader.setOutsidePathConfirmer([&asked](const ProjectPaths::PathReview &) {
        ++asked;
        return false;   // 操作员不确认
    });
    QStringList notes;
    QObject::connect(&loader, &ProjectManager::loadNote,
                     [&notes](const QString &note) { notes.append(note); });

    QList<FlowScene *> reopened;
    const bool ok = loader.loadProject(m_scheme, reopened);
    QVERIFY2(!ok, "未确认的目录外绝对路径方案不得载入");
    QCOMPARE(asked, 1);                          // 确认器必须被咨询（不得静默放行）
    QVERIFY2(reopened.isEmpty(), "拒载后不得留半装载的场景清单");
    QVERIFY2(notes.join(QStringLiteral("|")).contains(QStringLiteral("未获确认")),
             qPrintable(QStringLiteral("拒载须留痕：%1").arg(notes.join(QStringLiteral("|")))));
    QVERIFY(loader.lastLoadRefusedForOutsidePaths());
    qDeleteAll(reopened);
}

void ProjectPathsTest::testOutsideSchemePathAllowedAfterConfirm()
{
    // U-201：操作员确认后放行——载入成功，且留痕写明已确认放行。
    const QString outside =
        QDir(QDir::tempPath()).filePath(QStringLiteral("vfp201_outside/r1.png"));
    QVERIFY(writeSchemeWithFilePath(outside));

    ProjectManager loader;
    int asked = 0;
    loader.setOutsidePathConfirmer([&asked](const ProjectPaths::PathReview &review) {
        ++asked;
        return review.outsideCount > 0;   // 操作员看过清单后放行
    });
    QStringList notes;
    QObject::connect(&loader, &ProjectManager::loadNote,
                     [&notes](const QString &note) { notes.append(note); });

    QList<FlowScene *> reopened;
    const bool ok = loader.loadProject(m_scheme, reopened);
    QVERIFY2(ok, "确认后应当放行载入");
    QCOMPARE(asked, 1);
    QVERIFY(!reopened.isEmpty());
    QVERIFY2(notes.join(QStringLiteral("|")).contains(QStringLiteral("已确认放行")),
             qPrintable(QStringLiteral("放行须留痕：%1").arg(notes.join(QStringLiteral("|")))));
    qDeleteAll(reopened);
}

void ProjectPathsTest::testSchemeWithoutOutsidePathSkipsConfirmation()
{
    // 负对照：方案内只有相对形态路径时不得打扰操作员（确认器一次都不能被叫起）。
    QVERIFY(writeSchemeWithFilePath(QStringLiteral("images/rel.png")));

    ProjectManager loader;
    int asked = 0;
    loader.setOutsidePathConfirmer([&asked](const ProjectPaths::PathReview &) {
        ++asked;
        return false;   // 即便这个确认器会拒，也轮不到它说话
    });
    QList<FlowScene *> reopened;
    const bool ok = loader.loadProject(m_scheme, reopened);
    QVERIFY2(ok, "无目录外绝对路径的方案不得被门禁拦下");
    QCOMPARE(asked, 0);
    QVERIFY(!reopened.isEmpty());
    qDeleteAll(reopened);
}

void ProjectPathsTest::testFailedLoadRestoresSnapshotAndReadonly()
{
    // U-214（r11 第三方复核 P2-9）：快照提交纪律的运行期钉——**成功才提交，失败支回滚**。
    // 场景按复核原述：先成功载入一份带相对路径的方案（快照＝方案目录、明文可编辑），
    // 再依次打开别处的四条失败形状（v1 旧明文打包、口令错误的只读信封、确认拒载、JSON 语法
    // 坏件），每一条退回后断言「基目录快照、只读旗、载入附注」三件都回到原值——
    // 旧行为只收回确认拒载的路径、其余失败支干脆不收回，且确认组两支漏只读旗。
    ProjectPaths::setCurrentSchemeFilePath(QString());   // 座给腿净态（快照位不接上一腿）
    ProjectManager loader;
    loader.setOutsidePathConfirmer([](const ProjectPaths::PathReview &) {
        return false;   // 拒认器：供确认拒载形状用（相对路径方案不会叫起它）
    });
    QList<FlowScene *> opened;

    // 先成功载入一次（带相对路径的明文方案）：给三件立住原值
    QVERIFY2(writeSchemeWithFilePath(QStringLiteral("images/rel.png")), "方案素材落盘失败");
    QVERIFY2(loader.loadProject(m_scheme, opened), "先成功载入一次，为三件立住原值");
    QCOMPARE(opened.size(), 1);
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), m_scheme);
    QVERIFY2(!loader.isReadonlyLoaded(), "明文方案为可编辑：只读旗应清除");
    QVERIFY2(loader.lastIntegrityNote().contains(QStringLiteral("旧明文")),
             "明文载入后附注应写明旧明文");
    qDeleteAll(opened);
    opened.clear();

    // 信封载荷（真写侧序列化；无文件参数 ⇒ 全程不叫确认器）
    FlowScene payloadScene;
    QVERIFY2(payloadScene.createNode(NodeBase::OUTPUT, QPointF(100, 100), QStringLiteral("空载荷")),
             "工厂没给输出节点，本腿失去前提");
    ProjectManager writer;
    QJsonObject envRoot = writer.buildProjectJson(QList<FlowScene *>{ &payloadScene });
    envRoot.remove(QStringLiteral("runtimeLayout"));
    const QByteArray envJson = QJsonDocument(envRoot).toJson();

    // 失败形状①：v1 旧明文打包（integrityMissing 拒收支）
    QByteArray v1;
    v1.append(SchemePackage::kMagic, 4);
    v1.append(static_cast<char>(SchemePackage::kFormatVersion));
    v1.append(static_cast<char>(0x00));
    v1.append(qCompress(envJson));
    const QString legacyPath = m_tmp.filePath(QStringLiteral("legacy-v1.vfp"));
    {
        QFile f(legacyPath);
        QVERIFY2(f.open(QIODevice::WriteOnly), "旧格式方案落盘失败");
        f.write(v1);
        f.close();
    }
    QVERIFY2(!loader.loadProject(legacyPath, opened), "v1 旧明文打包必须被拒收");
    QVERIFY(loader.lastLoadRefusedForIntegrity());
    QVERIFY2(opened.isEmpty(), "拒载后不得留半装载的场景清单");
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), m_scheme);   // 锚不得留在被拒文件上
    QVERIFY2(!loader.isReadonlyLoaded(), "只读旗不得被失败的载入改写");
    QVERIFY2(loader.lastIntegrityNote().contains(QStringLiteral("旧明文")),
             "附注描述的是当值方案，失败载入不得清掉它");
    qDeleteAll(opened);
    opened.clear();

    // 成功形状：只读 v2 信封（成功才提交 ⇒ 锚换到信封所在、只读旗立真）
    const QString roEnvelope = m_tmp.filePath(QStringLiteral("readonly.vfp"));
    {
        QFile f(roEnvelope);
        QVERIFY2(f.open(QIODevice::WriteOnly), "只读信封落盘失败");
        f.write(SchemePackage::exportScheme(envJson, QStringLiteral("pw"), true));
        f.close();
    }
    QVERIFY2(loader.loadProject(roEnvelope, opened, QStringLiteral("pw")),
             "带完整性位且口令正确的信封须照常载入");
    QCOMPARE(opened.size(), 1);
    QVERIFY2(loader.isReadonlyLoaded(), "只读打包方案的旗应立真");
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), roEnvelope);
    QVERIFY2(loader.lastIntegrityNote().isEmpty(),
             "信封方案的附注应为空（附注描述当值方案的格式，信封不是旧明文）");
    qDeleteAll(opened);
    opened.clear();

    // 失败形状②：口令错误（信封 !ok 分支）
    QVERIFY2(!loader.loadProject(roEnvelope, opened, QStringLiteral("wrong")), "错口令必须被拒");
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), roEnvelope);   // 锚回到信封
    QVERIFY2(loader.isReadonlyLoaded(), "只读旗不得被失败的载入清掉");
    QVERIFY2(loader.lastIntegrityNote().isEmpty(),
             "附注回滚到信封原值（空）；失败载入不得注入新附注");
    qDeleteAll(opened);
    opened.clear();

    // 失败形状③：确认拒载（只读旗原值＝真——旧行为两支确认拒载只收路径、漏只读旗）
    const QString outside =
        QDir(QDir::tempPath()).filePath(QStringLiteral("vfp214_outside/x.png"));
    QVERIFY2(writeSchemeWithFilePath(outside), "目录外路径方案落盘失败");
    QVERIFY2(!loader.loadProject(m_scheme, opened), "未确认的目录外绝对路径必须拒");
    QVERIFY(loader.lastLoadRefusedForOutsidePaths());
    QVERIFY2(opened.isEmpty(), "拒载后不得留半装载的场景清单");
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), roEnvelope);   // 锚回到信封
    QVERIFY2(loader.isReadonlyLoaded(), "只读旗在确认拒载后也必须回到原值");
    qDeleteAll(opened);
    opened.clear();

    // 失败形状④：JSON 语法坏件（解析失败支）
    const QString junk = m_tmp.filePath(QStringLiteral("junk.vfp"));
    {
        QFile f(junk);
        QVERIFY2(f.open(QIODevice::WriteOnly), "语法坏件落盘失败");
        f.write("{ not json");
        f.close();
    }
    QVERIFY2(!loader.loadProject(junk, opened), "语法坏件必须被拒");
    QCOMPARE(ProjectPaths::currentSchemeFilePath(), roEnvelope);
    QVERIFY2(loader.isReadonlyLoaded(), "只读旗不得被解析失败支清掉");
    QVERIFY2(loader.lastIntegrityNote().isEmpty(),
             "附注在三件回滚之列（红臂破坏位落在第一条失败支：integrity 支）");
    qDeleteAll(opened);
    opened.clear();

    ProjectPaths::setCurrentSchemeFilePath(QString());   // 腿尾净态（cleanupTestCase 兜底再清一次）
}

QTEST_MAIN(ProjectPathsTest)
#include "project_paths_test.moc"

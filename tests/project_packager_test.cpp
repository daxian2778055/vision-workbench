// 方案依赖打包（2026-10-28）：把方案目录之外的模型/类别表收进 <方案>/assets 并改写为相对路径。
// 这条链路的目标是"拷走方案就能在另一台机器跑"，所以三条都必须成立：
//   ① 外部文件被复制进 assets、参数改成相对路径；
//   ② 已在方案内的文件不重复复制；
//   ③ 同名不同源自动加后缀，不互相覆盖。
#include "ProjectPackager.h"
#include "FlowScene.h"
#include "ObjectCountingNode.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QtTest>

class ProjectPackagerTest : public QObject
{
    Q_OBJECT
private slots:
    void copiesExternalModelAndRewritesPath();
    void alreadyInsideIsNotCopiedAgain();
    void sameNameFromDifferentSourceGetsSuffix();
    void sameSourceIsCopiedOnce();

private:
    static void writeFile(const QString &path, const QByteArray &body);
    static QByteArray readFile(const QString &path);
};

void ProjectPackagerTest::writeFile(const QString &path, const QByteArray &body)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(body);
}

QByteArray ProjectPackagerTest::readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

void ProjectPackagerTest::copiesExternalModelAndRewritesPath()
{
    QTemporaryDir schemeDir;
    QTemporaryDir extDir;
    QVERIFY(schemeDir.isValid() && extDir.isValid());

    const QString model = extDir.path() + QStringLiteral("/best.onnx");
    writeFile(model, QByteArray("dummy-onnx"));

    FlowScene scene;
    auto *counting = new ObjectCountingNode;
    counting->init();
    scene.adoptNode(counting, QPointF(10, 10));
    counting->setParam(QStringLiteral("modelPath"), model);

    const ProjectPackager::Result r =
        ProjectPackager::pack(&scene, schemeDir.path() + QStringLiteral("/scheme.vfp"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY2(!r.copied.isEmpty(), "外部模型没有被打包");

    const QString packed = schemeDir.path() + QStringLiteral("/assets/best.onnx");
    QVERIFY2(QFile::exists(packed), "assets 下没有模型文件");
    QCOMPARE(readFile(packed), QByteArray("dummy-onnx"));
    QCOMPARE(counting->getParam(QStringLiteral("modelPath")).toString(),
             QStringLiteral("assets/best.onnx"));
}

void ProjectPackagerTest::alreadyInsideIsNotCopiedAgain()
{
    QTemporaryDir schemeDir;
    QVERIFY(schemeDir.isValid());
    QVERIFY(QDir().mkpath(schemeDir.path() + QStringLiteral("/assets")));
    const QString inside = schemeDir.path() + QStringLiteral("/assets/own.onnx");
    writeFile(inside, QByteArray("x"));

    FlowScene scene;
    auto *counting = new ObjectCountingNode;
    counting->init();
    scene.adoptNode(counting, QPointF(10, 10));
    // 用绝对路径指向方案内文件：应只是改写成相对路径，不复制
    counting->setParam(QStringLiteral("modelPath"), inside);

    const ProjectPackager::Result r =
        ProjectPackager::pack(&scene, schemeDir.path() + QStringLiteral("/scheme.vfp"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY2(r.copied.isEmpty(), "方案内文件被重复复制");
    QCOMPARE(counting->getParam(QStringLiteral("modelPath")).toString(),
             QStringLiteral("assets/own.onnx"));
}

void ProjectPackagerTest::sameNameFromDifferentSourceGetsSuffix()
{
    QTemporaryDir schemeDir;
    QTemporaryDir extA;
    QTemporaryDir extB;
    QVERIFY(schemeDir.isValid() && extA.isValid() && extB.isValid());
    writeFile(extA.path() + QStringLiteral("/best.onnx"), QByteArray("A"));
    writeFile(extB.path() + QStringLiteral("/best.onnx"), QByteArray("B"));

    FlowScene scene;
    for (const QString &src : {extA.path(), extB.path()}) {
        auto *n = new ObjectCountingNode;
        n->init();
        scene.adoptNode(n, QPointF(10, 10));
        n->setParam(QStringLiteral("modelPath"), src + QStringLiteral("/best.onnx"));
    }

    const ProjectPackager::Result r =
        ProjectPackager::pack(&scene, schemeDir.path() + QStringLiteral("/scheme.vfp"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.copied.size(), 2);
    // 2026-10-29 P2-2：不钉"谁拿基础名"（那取决于遍历顺序，实测 20% 抖动）；
    // 只钉"每个节点最终指向的文件内容 = 它自己的源"，并且两个源内容都没被覆盖。
    int seenA = 0, seenB = 0;
    for (NodeBase *n : scene.nodes()) {
        const QString rel = n->getParam(QStringLiteral("modelPath")).toString();
        const QByteArray body = readFile(schemeDir.path() + QLatin1Char('/') + rel);
        if (body == QByteArray("A")) ++seenA;
        if (body == QByteArray("B")) ++seenB;
    }
    QCOMPARE(seenA, 1);
    QCOMPARE(seenB, 1);
    // assets 里应当恰好两个文件、内容不同（不被互相覆盖）
    const auto files = QDir(schemeDir.path() + QStringLiteral("/assets"))
                           .entryList(QDir::Files);
    QCOMPARE(files.size(), 2);
}

void ProjectPackagerTest::sameSourceIsCopiedOnce()
{
    // 2026-10-28 复核 P3：两个节点引用同一个外部模型 → 只应复制一份
    QTemporaryDir schemeDir;
    QTemporaryDir ext;
    QVERIFY(schemeDir.isValid() && ext.isValid());
    writeFile(ext.path() + QStringLiteral("/best.onnx"), QByteArray("SAME"));

    FlowScene scene;
    for (int i = 0; i < 2; ++i) {
        auto *n = new ObjectCountingNode;
        n->init();
        scene.adoptNode(n, QPointF(10, 10));
        n->setParam(QStringLiteral("modelPath"), ext.path() + QStringLiteral("/best.onnx"));
    }
    const ProjectPackager::Result r =
        ProjectPackager::pack(&scene, schemeDir.path() + QStringLiteral("/scheme.vfp"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.copied.size(), 1);   // 只复制一份
    QVERIFY2(!QFile::exists(schemeDir.path() + QStringLiteral("/assets/best_2.onnx")),
             "同源被复制成了两份（best_2.onnx 不应存在）");
}

QTEST_MAIN(ProjectPackagerTest)
#include "project_packager_test.moc"

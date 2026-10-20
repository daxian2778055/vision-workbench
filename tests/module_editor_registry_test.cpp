// ModuleEditorRegistryTest（词条切片 B 对话框族首抽）：模块编辑器登记簿的纯簿面座。
// 键用悬垂假指针（键用即止——登记簿从不解引用，全程不构造 NodeBase，FlowCachePurgeLedger
// 座先例同口径）；窗位用裸 QDialog 假体；工厂与命中刷新全替身探针。
// 红臂破坏位 = destroyed 摘除位失效（摘除 lambda 改删空键：键永不摘除）——
// 摘后重建腿当场红，命中刷新与闸收窗两腿臂内原样绿。
#include <QDialog>
#include <QtTest/QtTest>

#include "ModuleEditorRegistry.h"

namespace {

struct Probe {
    int factories = 0;
    QList<QDialog *> refreshed;
};

ModuleEditorRegistry::EditorFactory countingFactory(Probe &p)
{
    return [&p]() -> QDialog * {
        ++p.factories;
        return new QDialog();
    };
}

ModuleEditorRegistry::HitRefresh recordingRefresh(Probe &p)
{
    return [&p](QDialog *dlg) { p.refreshed.append(dlg); };
}

// 假键：登记簿只比键值不解引用（与账簿同口径）
NodeBase *fakeKey(quintptr seed)
{
    return reinterpret_cast<NodeBase *>(seed);
}

} // namespace

class ModuleEditorRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void testCreateOnceThenHitRefresh();   // 缺失建窗一次、命中不重建且刷新原窗
    void testDestroyedRemovesKey();        // 窗销毁即摘位：再入 = 全新窗（红臂靶腿）
    void testGateCloseKeepsBook();         // 闸收窗：close 不改簿位（销毁由 WA_DeleteOnClose 管）
    void testLookupOnlyNoBuild();          // lookup 只查不建
};

void ModuleEditorRegistryTest::testCreateOnceThenHitRefresh()
{
    Probe probe;
    ModuleEditorRegistry book;

    NodeBase *keyA = fakeKey(0xA1);
    NodeBase *keyB = fakeKey(0xB2);
    QDialog *firstDialog = book.editorFor(keyA, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 1);
    QVERIFY(firstDialog != nullptr);
    QVERIFY(book.lookup(keyA) == firstDialog);

    // 命中：不重建，刷新原窗，原窗返回
    QDialog *hitDialog = book.editorFor(keyA, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 1);
    QVERIFY(hitDialog == firstDialog);
    QCOMPARE(probe.refreshed, QList<QDialog *>({ firstDialog }));

    // 另一键：缺失建新窗
    QDialog *otherDialog = book.editorFor(keyB, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 2);
    QVERIFY(otherDialog != firstDialog);

    delete otherDialog;
    delete firstDialog;
}

void ModuleEditorRegistryTest::testDestroyedRemovesKey()
{
    Probe probe;
    ModuleEditorRegistry book;

    NodeBase *key = fakeKey(0xC3);
    QDialog *window = book.editorFor(key, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 1);

    delete window;   // destroyed → 摘位
    QDialog *rebuilt = book.editorFor(key, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 2);   // 摘后全新窗（红臂破坏位下此腿报 1 即红）
    QVERIFY(book.lookup(key) == rebuilt);   // 重建窗已接回原键（址复用下指针等值不做指望）
    QCOMPARE(probe.refreshed.count(), 0);   // 缺失路径不走刷新

    delete rebuilt;
}

void ModuleEditorRegistryTest::testGateCloseKeepsBook()
{
    Probe probe;
    ModuleEditorRegistry book;

    NodeBase *key = fakeKey(0xD4);
    QDialog *window = book.editorFor(key, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(probe.factories, 1);

    // 闸收窗：close 本身不摘位（真窗由 WA_DeleteOnClose 在关窗时销毁后走摘位连接）
    book.closeIfPresent(key);
    QVERIFY(book.lookup(key) == window);
    QCOMPARE(probe.factories, 1);
    QCOMPARE(probe.refreshed.count(), 0);   // 收窗不触发命中刷新

    // 簿上既有窗再入仍是命中（不走工厂）
    QDialog *again = book.editorFor(key, countingFactory(probe), recordingRefresh(probe));
    QVERIFY(again == window);
    QCOMPARE(probe.factories, 1);
    QCOMPARE(probe.refreshed, QList<QDialog *>({ window }));

    delete window;
}

void ModuleEditorRegistryTest::testLookupOnlyNoBuild()
{
    Probe probe;
    ModuleEditorRegistry book;

    NodeBase *key = fakeKey(0xE5);
    QVERIFY(book.lookup(key) == nullptr);   // 查无返回空，且绝不建窗
    QCOMPARE(probe.factories, 0);

    QDialog *window = book.editorFor(key, countingFactory(probe), recordingRefresh(probe));
    QCOMPARE(book.lookup(key), window);     // 查有返回同窗
    QCOMPARE(probe.factories, 1);

    delete window;
}

QTEST_MAIN(ModuleEditorRegistryTest)
#include "module_editor_registry_test.moc"

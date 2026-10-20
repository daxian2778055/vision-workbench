#include "ModuleEditorRegistry.h"

#include <QDialog>
#include <QObject>

// 模块编辑器登记簿（词条切片 B 对话框族首抽）：
//  · 键用即止——NodeBase* 只比键值，从不解除引用；
//  · 摘除连接的接收位换窗自身（连接随窗生灭，发时序与摘除效果同原体）；
//  · 命中刷新经回调交回窗侧（reloadFromNode 与前置的 GUI 序留在 MainWindow）。

QDialog *ModuleEditorRegistry::editorFor(NodeBase *node, const EditorFactory &make,
                                         const HitRefresh &refresh)
{
    if (QDialog *existing = m_editors.value(node, nullptr)) {
        if (refresh)
            refresh(existing);
        return existing;
    }
    QDialog *dlg = make ? make() : nullptr;
    if (!dlg)
        return nullptr;
    m_editors.insert(node, dlg);
    QObject::connect(dlg, &QObject::destroyed, dlg, [this, node]() { m_editors.remove(node); });
    return dlg;
}

void ModuleEditorRegistry::closeIfPresent(NodeBase *node)
{
    if (QDialog *existing = m_editors.value(node, nullptr))
        existing->close();
}

QDialog *ModuleEditorRegistry::lookup(NodeBase *node) const
{
    return m_editors.value(node, nullptr);
}

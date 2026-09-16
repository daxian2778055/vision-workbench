#pragma once

#include <QWidget>
#include <QVariantMap>
#include <QHash>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;

/// 变量面板：把「当前可被引用的变量」集中列出来，并给出可直接复制的引用表达式。
/// 覆盖 resolveParamRefs() 支持的两种命名空间：
///   - 模块输出  {模块号.参数名}   例如 {3.foregroundPixels}
///   - 全局变量  {global.名称}     例如 {global.triggerCount}
/// 目的是让「表达式联动」可发现、可复制：改下游参数时不必手敲模块号与参数名。
class VariablePanel : public QWidget
{
    Q_OBJECT

public:
    explicit VariablePanel(QWidget *parent = nullptr);

    /// 用某模块的输出变量更新「模块输出」分组（同一模块原地更新，不新增重复结点）
    void setModuleVars(int moduleId, const QString &moduleName, const QVariantMap &vars);
    /// 重新读取全局变量（GlobalVariableManager 单例）
    void refreshGlobalVariables();
    /// 清空全部变量行
    void clearAll();

    int moduleCount() const;
    int globalVariableCount() const;

    /// 引用表达式文本（与 FlowExecutor::resolveParamRefs 的语法一致）
    static QString moduleRefText(int moduleId, const QString &name);
    static QString globalRefText(const QString &name);
    /// 把引用插入目标编辑控件的光标处（QLineEdit / 其内嵌编辑框 / QTextEdit）
    static bool insertReferenceInto(QWidget *target, const QString &ref);

private:
    void copyCurrentReference();
    void updateSummary();
    QTreeWidgetItem *findOrCreateModuleRow(int moduleId, const QString &moduleName);
    QTreeWidgetItem *appendValueRow(QTreeWidgetItem *parent, const QString &name,
                                    const QString &valueText, const QString &ref);

    QTreeWidget *m_tree = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_hint = nullptr;
    QTreeWidgetItem *m_moduleGroup = nullptr;
    QTreeWidgetItem *m_globalGroup = nullptr;
    QHash<int, QTreeWidgetItem *> m_moduleRows;   ///< 模块号 -> 模块行
};

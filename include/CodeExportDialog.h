#pragma once

#include <QDialog>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QList>

class FlowScene;
class NodeBase;

/// 算子代码导出 — 将所选流程导出为 HALCON/C++ 代码
class CodeExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CodeExportDialog(const QList<FlowScene *> &flows,
                              QWidget *parent = nullptr);
    ~CodeExportDialog() override;

private slots:
    void onFlowChanged(int index);
    void onGenerate();
    void onCopy();
    void onSave();

private:
    void setupUI();
    void generateCode(int flowIndex);
    QString nodeToHalcon(NodeBase *node) const;

    QList<FlowScene *> m_flows;
    QComboBox *m_flowCombo = nullptr;
    QComboBox *m_langCombo = nullptr;   ///< Halcon / C++
    QPlainTextEdit *m_codeView = nullptr;
    QPushButton *m_generateBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
};

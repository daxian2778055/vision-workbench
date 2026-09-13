#pragma once

#include <QDialog>
#include <QMap>
#include "FlowScene.h"

class QTableWidget;
class QLineEdit;
class QComboBox;

/// 当前流程的变量表 + Fixture 列表
class FlowVariableDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FlowVariableDialog(FlowScene *scene, QWidget *parent = nullptr);

private slots:
    void addVariable();
    void removeVariable();
    void refreshTables();

private:
    FlowScene *m_scene = nullptr;
    QTableWidget *m_varTable = nullptr;
    QTableWidget *m_fixTable = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QLineEdit *m_valueEdit = nullptr;
};

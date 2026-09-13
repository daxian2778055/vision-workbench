#ifndef GLOBALVARIABLEDIALOG_H
#define GLOBALVARIABLEDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QDialogButtonBox>

class GlobalVariableDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GlobalVariableDialog(QWidget *parent = nullptr);
    ~GlobalVariableDialog();

private slots:
    void addVariable();
    void removeVariable();
    void editVariable();
    void saveVariables();
    void loadVariables();
    void updateVariableTable();

private:
    QTableWidget *m_variableTable;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_editButton;
    QPushButton *m_saveButton;
    QPushButton *m_loadButton;
    QDialogButtonBox *m_buttonBox;

    void setupUI();
    void setupConnections();
};

#endif // GLOBALVARIABLEDIALOG_H
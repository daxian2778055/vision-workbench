#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>

class UserManagementDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UserManagementDialog(QWidget *parent = nullptr);
    ~UserManagementDialog();

private slots:
    void onAddUser();
    void onRemoveUser();
    void refreshUserTable();

private:
    void setupUI();

    QTableWidget *m_userTable;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QPushButton *m_closeButton;
};

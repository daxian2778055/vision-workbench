#pragma once

#include <QDialog>
#include <QVector>

#include "CalibrationWizard.h"

class QLabel;
class QListWidget;
class QPushButton;
class QRadioButton;
class QStackedWidget;

/// VM 4.4「工具 → 标定配置引导」：选择标定类型 → 创建 → 进入引导配置流程。
/// 官方 GUID-9D4AEEFC-ED21-437E-B20D-9DEB5C85288D。
class CalibrationWizardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CalibrationWizardDialog(QWidget *parent = nullptr);
    CalibrationWizardType selectedType() const;

private slots:
    void onCreate();

private:
    QVector<QRadioButton *> m_typeButtons;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_unsupported = nullptr;
    QListWidget *m_steps = nullptr;
    QPushButton *m_createBtn = nullptr;
};

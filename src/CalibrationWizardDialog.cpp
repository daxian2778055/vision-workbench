#include "CalibrationWizardDialog.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QVBoxLayout>

CalibrationWizardDialog::CalibrationWizardDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("标定配置引导"));
    setObjectName(QStringLiteral("calibrationWizardDialog"));
    resize(560, 420);

    auto *root = new QVBoxLayout(this);
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // 第 1 页：按官方原文选择标定类型
    auto *choose = new QWidget(this);
    auto *chooseLay = new QVBoxLayout(choose);
    auto *hint = new QLabel(
        tr("根据实际需求选择标定类型后，点击创建即可进入标定的引导配置流程。"), choose);
    hint->setWordWrap(true);
    chooseLay->addWidget(hint);

    auto *group = new QButtonGroup(choose);
    const QVector<CalibrationWizardSpec> specs = CalibrationWizard::allSpecs();
    for (const CalibrationWizardSpec &s : specs) {
        auto *radio = new QRadioButton(s.name, choose);
        radio->setObjectName(
            QStringLiteral("wizardType%1").arg(static_cast<int>(s.type)));
        group->addButton(radio, static_cast<int>(s.type));
        m_typeButtons.append(radio);
        chooseLay->addWidget(radio);

        auto *desc = new QLabel(s.description, choose);
        desc->setWordWrap(true);
        desc->setIndent(20);
        chooseLay->addWidget(desc);
        auto *module = new QLabel(tr("官方落地模块：%1").arg(s.targetModule), choose);
        module->setIndent(20);
        module->setWordWrap(true);
        chooseLay->addWidget(module);
    }
    if (!m_typeButtons.isEmpty())
        m_typeButtons.first()->setChecked(true);
    chooseLay->addStretch();
    m_stack->addWidget(choose);

    // 第 2 页：该类型的引导步骤；缺算子的类型只给说明，不伪造步骤
    auto *guide = new QWidget(this);
    auto *guideLay = new QVBoxLayout(guide);
    m_summary = new QLabel(guide);
    m_summary->setWordWrap(true);
    guideLay->addWidget(m_summary);
    m_unsupported = new QLabel(guide);
    m_unsupported->setObjectName(QStringLiteral("wizardUnsupported"));
    m_unsupported->setWordWrap(true);
    m_unsupported->setStyleSheet(QStringLiteral("color:#c0603a;"));
    guideLay->addWidget(m_unsupported);
    m_steps = new QListWidget(guide);
    m_steps->setObjectName(QStringLiteral("wizardSteps"));
    guideLay->addWidget(m_steps, 1);
    m_stack->addWidget(guide);

    // ActionRole 不会自动 accept：第一次点「创建」只是切页，第二次才完成
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_createBtn = buttons->addButton(tr("创建"), QDialogButtonBox::ActionRole);
    m_createBtn->setObjectName(QStringLiteral("wizardCreateButton"));
    connect(m_createBtn, &QPushButton::clicked, this,
            &CalibrationWizardDialog::onCreate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

CalibrationWizardType CalibrationWizardDialog::selectedType() const
{
    for (int i = 0; i < m_typeButtons.size(); ++i) {
        if (m_typeButtons.at(i)->isChecked())
            return static_cast<CalibrationWizardType>(i);
    }
    return CalibrationWizardType::Static;
}

void CalibrationWizardDialog::onCreate()
{
    if (m_stack->currentIndex() == 1) {
        accept();
        return;
    }
    const CalibrationWizardSpec spec = CalibrationWizard::specFor(selectedType());
    m_summary->setText(tr("%1：%2").arg(spec.name, spec.description));
    m_steps->clear();
    int n = 1;
    for (const QString &step : spec.steps)
        m_steps->addItem(tr("%1. %2").arg(n++).arg(step));
    m_steps->setHidden(spec.steps.isEmpty());
    m_unsupported->setText(spec.unsupportedReason);
    m_unsupported->setHidden(spec.supported);
    m_stack->setCurrentIndex(1);
    m_createBtn->setText(tr("完成"));
}

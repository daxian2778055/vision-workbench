#include "CommEventEditDialogs.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QRegularExpression>

// ==================== ReceiveEventEditDialog ====================

ReceiveEventEditDialog::ReceiveEventEditDialog(ReceiveEvent *event, QWidget *parent)
    : QDialog(parent), m_event(event)
{
    setWindowTitle(QStringLiteral("编辑接收事件 — %1").arg(event ? event->eventId() : QString()));
    resize(620, 460);

    auto *root = new QVBoxLayout(this);

    m_enabledCheck = new QCheckBox(QStringLiteral("启用此事件"), this);
    root->addWidget(m_enabledCheck);

    auto *info = new QLabel(this);
    if (m_event) {
        info->setText(QStringLiteral("设备: %1    类型: %2")
                          .arg(m_event->deviceName(),
                               m_event->eventType() == ReceiveEvent::TEXT_PROTOCOL
                                   ? QStringLiteral("文本-协议解析")
                                   : QStringLiteral("字节匹配-协议组装")));
    }
    info->setStyleSheet("color: gray;");
    root->addWidget(info);

    if (auto *tev = qobject_cast<TextProtocolReceiveEvent *>(m_event)) {
        Q_UNUSED(tev)
        auto *gb = new QGroupBox(QStringLiteral("文本解析"), this);
        auto *form = new QFormLayout(gb);
        m_modeCombo = new QComboBox(gb);
        m_modeCombo->addItem(QStringLiteral("分隔符拆分"),
                             int(TextProtocolReceiveEvent::Delimiter));
        m_modeCombo->addItem(QStringLiteral("正则表达式"),
                             int(TextProtocolReceiveEvent::Regex));
        m_delimiterEdit = new QLineEdit(gb);
        m_regexEdit = new QLineEdit(gb);
        m_regexEdit->setPlaceholderText(
            QStringLiteral("如 X(-?\\d+),Y(-?\\d+)；捕获组依次作为字段，无捕获组取整体"));
        form->addRow(QStringLiteral("解析模式"), m_modeCombo);
        form->addRow(QStringLiteral("分隔符"), m_delimiterEdit);
        form->addRow(QStringLiteral("正则"), m_regexEdit);
        root->addWidget(gb);
        connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &ReceiveEventEditDialog::onModeChanged);
    } else {
        auto *gb = new QGroupBox(QStringLiteral("字节匹配规则（任一规则命中即触发）"), this);
        auto *v = new QVBoxLayout(gb);
        m_rulesTable = new QTableWidget(gb);
        m_rulesTable->setColumnCount(6);
        m_rulesTable->setHorizontalHeaderLabels({ QStringLiteral("偏移"), QStringLiteral("长度"),
                                                  QStringLiteral("类型"), QStringLiteral("字节序"),
                                                  QStringLiteral("比较值"), QStringLiteral("条件") });
        m_rulesTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_rulesTable->verticalHeader()->setVisible(false);
        v->addWidget(m_rulesTable);

        auto *btnRow = new QHBoxLayout();
        auto *addBtn = new QPushButton(QStringLiteral("＋添加规则"), gb);
        auto *delBtn = new QPushButton(QStringLiteral("－删除选中"), gb);
        connect(addBtn, &QPushButton::clicked, this, &ReceiveEventEditDialog::onAddRule);
        connect(delBtn, &QPushButton::clicked, this, &ReceiveEventEditDialog::onRemoveRule);
        btnRow->addWidget(addBtn);
        btnRow->addWidget(delBtn);
        btnRow->addStretch();
        v->addLayout(btnRow);
        root->addWidget(gb, 1);
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &ReceiveEventEditDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    loadFromEvent();
}

void ReceiveEventEditDialog::loadFromEvent()
{
    if (!m_event) return;
    m_enabledCheck->setChecked(m_event->enabled());

    if (auto *tev = qobject_cast<TextProtocolReceiveEvent *>(m_event)) {
        const int idx = m_modeCombo->findData(int(tev->parseMode()));
        m_modeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        m_delimiterEdit->setText(tev->delimiter());
        m_regexEdit->setText(tev->regex());
        onModeChanged();
    } else if (auto *bev = qobject_cast<ByteMatchReceiveEvent *>(m_event)) {
        m_rulesTable->setRowCount(0);
        for (const ByteMatchRule &r : bev->rules()) {
            const int row = m_rulesTable->rowCount();
            m_rulesTable->insertRow(row);
            m_rulesTable->setItem(row, 0, new QTableWidgetItem(QString::number(r.byteOffset)));
            m_rulesTable->setItem(row, 1, new QTableWidgetItem(QString::number(r.byteLength)));
            auto *typeCombo = new QComboBox(m_rulesTable);
            typeCombo->addItems({ QStringLiteral("int16"), QStringLiteral("int32"),
                                  QStringLiteral("float") });
            typeCombo->setCurrentText(r.dataType);
            m_rulesTable->setCellWidget(row, 2, typeCombo);
            auto *orderCombo = new QComboBox(m_rulesTable);
            orderCombo->addItems({ QStringLiteral("ABCD"), QStringLiteral("CDAB"),
                                   QStringLiteral("BADC"), QStringLiteral("DCBA") });
            orderCombo->setCurrentText(r.byteOrder);
            m_rulesTable->setCellWidget(row, 3, orderCombo);
            m_rulesTable->setItem(row, 4, new QTableWidgetItem(QString::number(r.compareValue)));
            auto *condCombo = new QComboBox(m_rulesTable);
            condCombo->addItems({ QStringLiteral("等于"), QStringLiteral("上升沿"),
                                  QStringLiteral("下降沿") });
            condCombo->setCurrentIndex(r.useRisingEdge ? 1 : (r.useFallingEdge ? 2 : 0));
            m_rulesTable->setCellWidget(row, 5, condCombo);
        }
    }
}

void ReceiveEventEditDialog::onModeChanged()
{
    if (!m_modeCombo || !m_delimiterEdit || !m_regexEdit) return;
    const bool isRegex =
        (m_modeCombo->currentData().toInt() == int(TextProtocolReceiveEvent::Regex));
    m_delimiterEdit->setEnabled(!isRegex);
    m_regexEdit->setEnabled(isRegex);
}

void ReceiveEventEditDialog::onAddRule()
{
    if (!m_rulesTable) return;
    const int row = m_rulesTable->rowCount();
    m_rulesTable->insertRow(row);
    m_rulesTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0")));
    m_rulesTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("2")));
    auto *typeCombo = new QComboBox(m_rulesTable);
    typeCombo->addItems({ QStringLiteral("int16"), QStringLiteral("int32"),
                          QStringLiteral("float") });
    m_rulesTable->setCellWidget(row, 2, typeCombo);
    auto *orderCombo = new QComboBox(m_rulesTable);
    orderCombo->addItems({ QStringLiteral("ABCD"), QStringLiteral("CDAB"),
                           QStringLiteral("BADC"), QStringLiteral("DCBA") });
    m_rulesTable->setCellWidget(row, 3, orderCombo);
    m_rulesTable->setItem(row, 4, new QTableWidgetItem(QStringLiteral("0")));
    auto *condCombo = new QComboBox(m_rulesTable);
    condCombo->addItems({ QStringLiteral("等于"), QStringLiteral("上升沿"),
                          QStringLiteral("下降沿") });
    m_rulesTable->setCellWidget(row, 5, condCombo);
}

void ReceiveEventEditDialog::onRemoveRule()
{
    if (!m_rulesTable) return;
    const int row = m_rulesTable->currentRow();
    if (row >= 0)
        m_rulesTable->removeRow(row);
}

void ReceiveEventEditDialog::onAccept()
{
    if (!m_event) { accept(); return; }

    if (auto *tev = qobject_cast<TextProtocolReceiveEvent *>(m_event)) {
        const auto mode =
            static_cast<TextProtocolReceiveEvent::ParseMode>(m_modeCombo->currentData().toInt());
        if (mode == TextProtocolReceiveEvent::Regex) {
            const QString re = m_regexEdit->text().trimmed();
            if (re.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("正则不能为空"),
                                     QStringLiteral("请填写正则表达式（捕获组作为字段）"));
                return;
            }
            const QRegularExpression probe(re);
            if (!probe.isValid()) {
                QMessageBox::warning(this, QStringLiteral("正则无效"),
                                     QStringLiteral("正则编译失败：%1\n位置 %2")
                                         .arg(probe.errorString())
                                         .arg(probe.patternErrorOffset()));
                return;
            }
            tev->setParseMode(mode);
            tev->setRegex(re);
        } else {
            if (m_delimiterEdit->text().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("分隔符不能为空"),
                                     QStringLiteral("请填写分隔符（如英文逗号）"));
                return;
            }
            tev->setParseMode(mode);
            tev->setDelimiter(m_delimiterEdit->text());
        }
    } else if (auto *bev = qobject_cast<ByteMatchReceiveEvent *>(m_event)) {
        bev->clearRules();
        for (int r = 0; r < m_rulesTable->rowCount(); ++r) {
            ByteMatchRule rule;
            if (auto *it = m_rulesTable->item(r, 0)) rule.byteOffset = it->text().toInt();
            if (auto *it = m_rulesTable->item(r, 1)) rule.byteLength = qMax(1, it->text().toInt());
            if (auto *c = qobject_cast<QComboBox *>(m_rulesTable->cellWidget(r, 2)))
                rule.dataType = c->currentText();
            if (auto *c = qobject_cast<QComboBox *>(m_rulesTable->cellWidget(r, 3)))
                rule.byteOrder = c->currentText();
            if (auto *it = m_rulesTable->item(r, 4)) rule.compareValue = it->text().toDouble();
            if (auto *c = qobject_cast<QComboBox *>(m_rulesTable->cellWidget(r, 5))) {
                rule.useEquals = (c->currentIndex() == 0);
                rule.useRisingEdge = (c->currentIndex() == 1);
                rule.useFallingEdge = (c->currentIndex() == 2);
            }
            bev->addRule(rule);
        }
    }

    m_event->setEnabled(m_enabledCheck->isChecked());
    accept();
}

// ==================== SendEventEditDialog ====================

SendEventEditDialog::SendEventEditDialog(SendEvent *event, QWidget *parent)
    : QDialog(parent), m_event(event)
{
    setWindowTitle(QStringLiteral("编辑发送事件 — %1").arg(event ? event->eventId() : QString()));
    resize(620, 420);

    auto *root = new QVBoxLayout(this);

    m_enabledCheck = new QCheckBox(QStringLiteral("启用此事件（每轮结束自动上报）"), this);
    root->addWidget(m_enabledCheck);

    auto *info = new QLabel(this);
    if (m_event) {
        info->setText(QStringLiteral("设备: %1    类型: %2")
                          .arg(m_event->deviceName(),
                               m_event->sendType() == SendEvent::TEXT_DIRECT
                                   ? QStringLiteral("文本-直接输出")
                                   : QStringLiteral("字节组包")));
    }
    info->setStyleSheet("color: gray;");
    root->addWidget(info);

    if (qobject_cast<TextDirectSendEvent *>(m_event)) {
        auto *gb = new QGroupBox(QStringLiteral("文本模板"), this);
        auto *form = new QFormLayout(gb);
        m_templateEdit = new QLineEdit(gb);
        m_templateEdit->setPlaceholderText(
            QStringLiteral("支持占位符：{模块号.参数名} / {global.变量名}，如 {1.结果},{global.计数}"));
        m_suffixEdit = new QLineEdit(gb);
        m_suffixEdit->setPlaceholderText(QStringLiteral("行尾后缀，如 \\r\\n（可留空）"));
        form->addRow(QStringLiteral("模板"), m_templateEdit);
        form->addRow(QStringLiteral("后缀"), m_suffixEdit);
        auto *hint = new QLabel(QStringLiteral(
            "每轮结束时自动把本轮结果填入占位符后上报；写错的占位符会原样保留便于排查。\n"
            "也可以用 {} 表示单个整体值（旧模板兼容）。"), gb);
        hint->setWordWrap(true);
        hint->setStyleSheet("color: gray; font-size: 11px;");
        form->addRow(hint);
        root->addWidget(gb);
    } else {
        auto *gb = new QGroupBox(QStringLiteral("字节组包字段（无固定值 = 取流程发送值）"), this);
        auto *v = new QVBoxLayout(gb);
        m_fieldsTable = new QTableWidget(gb);
        m_fieldsTable->setColumnCount(4);
        m_fieldsTable->setHorizontalHeaderLabels({ QStringLiteral("偏移"), QStringLiteral("长度"),
                                                   QStringLiteral("类型"), QStringLiteral("固定值") });
        m_fieldsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        m_fieldsTable->verticalHeader()->setVisible(false);
        v->addWidget(m_fieldsTable);

        auto *btnRow = new QHBoxLayout();
        auto *addBtn = new QPushButton(QStringLiteral("＋添加字段"), gb);
        auto *delBtn = new QPushButton(QStringLiteral("－删除选中"), gb);
        connect(addBtn, &QPushButton::clicked, this, &SendEventEditDialog::onAddField);
        connect(delBtn, &QPushButton::clicked, this, &SendEventEditDialog::onRemoveField);
        btnRow->addWidget(addBtn);
        btnRow->addWidget(delBtn);
        btnRow->addStretch();
        v->addLayout(btnRow);
        root->addWidget(gb, 1);
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &SendEventEditDialog::onAccept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(box);

    loadFromEvent();
}

void SendEventEditDialog::loadFromEvent()
{
    if (!m_event) return;
    m_enabledCheck->setChecked(m_event->enabled());

    if (auto *tev = qobject_cast<TextDirectSendEvent *>(m_event)) {
        m_templateEdit->setText(tev->templateText());
        m_suffixEdit->setText(tev->suffix());
    } else if (auto *bev = qobject_cast<BytePackSendEvent *>(m_event)) {
        m_fieldsTable->setRowCount(0);
        for (const BytePackField &f : bev->fields()) {
            const int row = m_fieldsTable->rowCount();
            m_fieldsTable->insertRow(row);
            m_fieldsTable->setItem(row, 0, new QTableWidgetItem(QString::number(f.offset)));
            m_fieldsTable->setItem(row, 1, new QTableWidgetItem(QString::number(f.length)));
            auto *typeCombo = new QComboBox(m_fieldsTable);
            typeCombo->addItems({ QStringLiteral("int16"), QStringLiteral("int32"),
                                  QStringLiteral("float") });
            typeCombo->setCurrentText(f.dataType);
            m_fieldsTable->setCellWidget(row, 2, typeCombo);
            m_fieldsTable->setItem(row, 3, new QTableWidgetItem(
                f.fixedValue.isValid() ? f.fixedValue.toString() : QString()));
        }
    }
}

void SendEventEditDialog::onAddField()
{
    if (!m_fieldsTable) return;
    const int row = m_fieldsTable->rowCount();
    m_fieldsTable->insertRow(row);
    m_fieldsTable->setItem(row, 0, new QTableWidgetItem(QStringLiteral("0")));
    m_fieldsTable->setItem(row, 1, new QTableWidgetItem(QStringLiteral("2")));
    auto *typeCombo = new QComboBox(m_fieldsTable);
    typeCombo->addItems({ QStringLiteral("int16"), QStringLiteral("int32"),
                          QStringLiteral("float") });
    m_fieldsTable->setCellWidget(row, 2, typeCombo);
    m_fieldsTable->setItem(row, 3, new QTableWidgetItem(QString()));
}

void SendEventEditDialog::onRemoveField()
{
    if (!m_fieldsTable) return;
    const int row = m_fieldsTable->currentRow();
    if (row >= 0)
        m_fieldsTable->removeRow(row);
}

void SendEventEditDialog::onAccept()
{
    if (!m_event) { accept(); return; }

    if (auto *tev = qobject_cast<TextDirectSendEvent *>(m_event)) {
        tev->setTemplate(m_templateEdit->text());
        tev->setSuffix(m_suffixEdit->text());
    } else if (auto *bev = qobject_cast<BytePackSendEvent *>(m_event)) {
        bev->clearFields();
        for (int r = 0; r < m_fieldsTable->rowCount(); ++r) {
            BytePackField f;
            if (auto *it = m_fieldsTable->item(r, 0)) f.offset = it->text().toInt();
            if (auto *it = m_fieldsTable->item(r, 1)) f.length = qMax(1, it->text().toInt());
            if (auto *c = qobject_cast<QComboBox *>(m_fieldsTable->cellWidget(r, 2)))
                f.dataType = c->currentText();
            const QString fixedText =
                m_fieldsTable->item(r, 3) ? m_fieldsTable->item(r, 3)->text().trimmed() : QString();
            if (!fixedText.isEmpty()) {
                f.fixedValue = (f.dataType == QStringLiteral("float"))
                                   ? QVariant(fixedText.toDouble())
                                   : QVariant(fixedText.toInt());
            }
            bev->addField(f);
        }
    }

    m_event->setEnabled(m_enabledCheck->isChecked());
    accept();
}

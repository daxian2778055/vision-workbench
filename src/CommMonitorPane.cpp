#include "CommMonitorPane.h"

#include "CommunicationManager.h"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

CommMonitorPane::CommMonitorPane(QWidget *parent)
    : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    auto *top = new QHBoxLayout();
    auto *title = new QLabel(QStringLiteral("\u6536\u53D1\u6570\u636E"), this);
    top->addWidget(title);
    top->addStretch();
    m_hexCheck = new QCheckBox(QStringLiteral("16\u8FDB\u5236\u663E\u793A"), this);
    m_hexCheck->setObjectName(QStringLiteral("commPaneHexCheck"));
    m_clearBtn = new QPushButton(QStringLiteral("\u6E05\u7A7A\u6570\u636E"), this);
    m_clearBtn->setObjectName(QStringLiteral("commPaneClear"));
    top->addWidget(m_hexCheck);
    top->addWidget(m_clearBtn);
    lay->addLayout(top);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("commPaneTabs"));
    m_rxView = new QPlainTextEdit(this);
    m_rxView->setObjectName(QStringLiteral("commPaneRxView"));
    m_rxView->setReadOnly(true);
    m_rxView->setMaximumBlockCount(kMaxLines);
    m_txView = new QPlainTextEdit(this);
    m_txView->setObjectName(QStringLiteral("commPaneTxView"));
    m_txView->setReadOnly(true);
    m_txView->setMaximumBlockCount(kMaxLines);
    m_tabs->addTab(m_rxView, QStringLiteral("\u63A5\u6536\u6570\u636E"));
    m_tabs->addTab(m_txView, QStringLiteral("\u53D1\u9001\u6570\u636E"));
    lay->addWidget(m_tabs, 1);

    auto *sendRow = new QHBoxLayout();
    m_targetLabel = new QLabel(this);
    m_targetLabel->setObjectName(QStringLiteral("commPaneTarget"));
    m_sendEdit = new QLineEdit(this);
    m_sendEdit->setObjectName(QStringLiteral("commPaneSendEdit"));
    m_sendEdit->setPlaceholderText(
        QStringLiteral("\u8F93\u5165\u5F85\u53D1\u9001\u5185\u5BB9\uFF0C\u56DE\u8F66\u6216\u70B9\u53D1\u9001"));
    m_sendBtn = new QPushButton(QStringLiteral("\u53D1\u9001"), this);
    m_sendBtn->setObjectName(QStringLiteral("commPaneSend"));
    sendRow->addWidget(m_targetLabel);
    sendRow->addWidget(m_sendEdit, 1);
    sendRow->addWidget(m_sendBtn);
    lay->addLayout(sendRow);

    // 高频收发时合并重绘（与独立监视器同款 200ms 节流），避免每帧全量重绘
    // 把通信管理窗口和主界面一起拖卡。
    m_throttle = new QTimer(this);
    m_throttle->setSingleShot(true);
    m_throttle->setInterval(kThrottleMs);

    connect(m_throttle, &QTimer::timeout, this, &CommMonitorPane::flushRefresh);
    connect(m_hexCheck, &QCheckBox::toggled, this, &CommMonitorPane::onHexToggled);
    connect(m_clearBtn, &QPushButton::clicked, this, &CommMonitorPane::onClear);
    connect(m_sendBtn, &QPushButton::clicked, this, &CommMonitorPane::onSend);
    connect(m_sendEdit, &QLineEdit::returnPressed, this, &CommMonitorPane::onSend);

    auto *mgr = CommunicationManager::instance();
    connect(mgr, &CommunicationManager::dataReceived,
            this, &CommMonitorPane::onDataReceived);
    connect(mgr, &CommunicationManager::dataSent,
            this, &CommMonitorPane::onDataSent);

    setTargetDevice(QString());
}

void CommMonitorPane::setTargetDevice(const QString &device)
{
    m_device = device;
    const bool usable = !device.isEmpty();
    m_targetLabel->setText(usable ? QStringLiteral("\u2192 %1").arg(device)
                                  : QStringLiteral("\u672A\u9009\u62E9\u8BBE\u5907"));
    m_sendEdit->setEnabled(usable);
    m_sendBtn->setEnabled(usable);
}

QString CommMonitorPane::receiveText() const
{
    return m_rxView ? m_rxView->toPlainText() : QString();
}

QString CommMonitorPane::sendText() const
{
    return m_txView ? m_txView->toPlainText() : QString();
}

QString CommMonitorPane::formatLine(const QString &device, const QByteArray &data, bool hex)
{
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString body;
    if (hex) {
        body = data.isEmpty() ? QStringLiteral("(empty)")
                              : QString::fromLatin1(data.toHex(' ').toUpper());
    } else {
        for (char c : data)
            body += (c >= 32 && c <= 126) ? QChar::fromLatin1(c) : QChar('.');
    }
    return QStringLiteral("[%1] [%2] %3").arg(ts, device, body);
}

void CommMonitorPane::push(QVector<Entry> &list, const QString &device,
                           const QByteArray &data)
{
    list.append({device, data});
    while (list.size() > kMaxLines)
        list.removeFirst();
    m_dirty = true;
    if (m_throttle && !m_throttle->isActive())
        m_throttle->start();
}

void CommMonitorPane::render(QPlainTextEdit *view, const QVector<Entry> &list, bool hex) const
{
    if (!view) return;
    QStringList lines;
    lines.reserve(list.size());
    for (const Entry &e : list)
        lines << formatLine(e.device, e.data, hex);
    view->setPlainText(lines.join(QLatin1Char('\n')));
    QTextCursor cursor = view->textCursor();
    cursor.movePosition(QTextCursor::End);
    view->setTextCursor(cursor);
}

void CommMonitorPane::flushRefresh()
{
    if (!m_dirty) return;
    m_dirty = false;
    const bool hex = m_hexCheck && m_hexCheck->isChecked();
    render(m_rxView, m_rx, hex);
    render(m_txView, m_tx, hex);
}

void CommMonitorPane::onDataReceived(const QString &deviceName, const QByteArray &data)
{
    push(m_rx, deviceName, data);
}

void CommMonitorPane::onDataSent(const QString &deviceName, const QByteArray &data)
{
    push(m_tx, deviceName, data);
}

void CommMonitorPane::onHexToggled(bool)
{
    m_dirty = true;
    flushRefresh();   // 切换显示格式立即重绘已有记录，不用等下一个 200ms
}

void CommMonitorPane::onClear()
{
    m_rx.clear();
    m_tx.clear();
    m_dirty = true;
    flushRefresh();
}

void CommMonitorPane::onSend()
{
    if (m_device.isEmpty() || !m_sendEdit) return;
    const QString text = m_sendEdit->text().trimmed();
    if (text.isEmpty()) return;

    QByteArray data;
    if (m_hexCheck && m_hexCheck->isChecked()) {
        data = QByteArray::fromHex(text.toLatin1());
        if (data.isEmpty()) return;   // 非法十六进制串不发送
    } else {
        data = text.toUtf8();
    }
    if (CommunicationManager::instance()->sendData(m_device, data))
        m_sendEdit->clear();
}

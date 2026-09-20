#include "CommMonitorDialog.h"
#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "AppLog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QColor>
#include <QBrush>
#include <QFile>
#include <QTimer>

CommMonitorDialog::CommMonitorDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();

    auto *mgr = CommunicationManager::instance();

    connect(mgr, &CommunicationManager::dataReceived,
            this, &CommMonitorDialog::onDataReceived);
    connect(mgr, &CommunicationManager::dataSent,
            this, &CommMonitorDialog::onDataSent);
    connect(mgr, &CommunicationManager::deviceConnected,
            this, &CommMonitorDialog::onDeviceConnected);
    connect(mgr, &CommunicationManager::deviceDisconnected,
            this, &CommMonitorDialog::onDeviceDisconnected);
    connect(mgr, &CommunicationManager::deviceAdded,
            this, &CommMonitorDialog::onDeviceAdded);
    connect(mgr, &CommunicationManager::deviceRemoved,
            this, &CommMonitorDialog::onDeviceRemoved);

    // 初始化已有设备
    const QStringList names = mgr->deviceNames();
    for (const QString &name : names) {
        const CommDeviceInfo info = mgr->deviceInfo(name);
        DeviceStats st;
        st.type = info.type;
        st.connected = info.isConnected;
        m_stats.insert(name, st);
        m_deviceOrder.append(name);
        ensureDeviceRow(name);
    }
    m_deviceTable->selectRow(0);
    updateStatus();

    // 日志重绘节流：高频通讯时合并刷新（否则每帧全量重绘拖慢监视窗与主界面）
    m_refreshThrottle = new QTimer(this);
    m_refreshThrottle->setSingleShot(true);
    m_refreshThrottle->setInterval(200);
    connect(m_refreshThrottle, &QTimer::timeout, this, &CommMonitorDialog::flushRefresh);
}

void CommMonitorDialog::flushRefresh()
{
    if (!m_refreshDirty)
        return;
    m_refreshDirty = false;
    refreshDetailView();
    updateStatus();
}

void CommMonitorDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u901A\u8BAF\u6570\u636E\u76D1\u89C6"));
    setMinimumSize(820, 560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto *mainLayout = new QVBoxLayout(this);

    // 顶部：设备统计表格
    m_deviceTable = new QTableWidget();
    m_deviceTable->setColumnCount(6);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u72B6\u6001"),
        QStringLiteral("\u6536\u5B57\u8282"),
        QStringLiteral("\u53D1\u5B57\u8282"),
        QStringLiteral("\u5E27\u6570(\u6536/\u53D1)")
    });
    m_deviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->setMaximumHeight(170);
    connect(m_deviceTable, &QTableWidget::itemSelectionChanged,
            this, &CommMonitorDialog::onDeviceSelectionChanged);
    mainLayout->addWidget(m_deviceTable);

    // 中部：工具栏
    auto *toolLayout = new QHBoxLayout();
    toolLayout->addWidget(new QLabel(QStringLiteral("\u663E\u793A\u6A21\u5F0F:")));
    m_viewModeCombo = new QComboBox();
    m_viewModeCombo->addItems({
        QStringLiteral("\u6DF7\u5408(HEX+ASCII)"),
        QStringLiteral("\u4EC5\u5341\u516D\u8FDB\u5236"),
        QStringLiteral("\u4EC5ASCII")
    });
    m_viewModeCombo->setCurrentIndex(0);
    connect(m_viewModeCombo, &QComboBox::currentIndexChanged,
            this, &CommMonitorDialog::refreshDetailView);
    toolLayout->addWidget(m_viewModeCombo);

    toolLayout->addStretch();

    m_clearLogBtn = new QPushButton(QStringLiteral("\u6E05\u7A7A\u65E5\u5FD7"));
    connect(m_clearLogBtn, &QPushButton::clicked, this, &CommMonitorDialog::onClearLog);
    toolLayout->addWidget(m_clearLogBtn);

    m_clearStatsBtn = new QPushButton(QStringLiteral("\u5F52\u96F6\u7EDF\u8BA1"));
    connect(m_clearStatsBtn, &QPushButton::clicked, this, &CommMonitorDialog::onClearStats);
    toolLayout->addWidget(m_clearStatsBtn);

    auto *saveBtn = new QPushButton(QStringLiteral("\u5BFC\u51FA\u65E5\u5FD7"));
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("\u5BFC\u51FA\u901A\u8BAF\u65E5\u5FD7"),
            QStringLiteral("comm_monitor.txt"), QStringLiteral("Text (*.txt)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << m_logView->toPlainText();
            f.close();
        }
    });
    toolLayout->addWidget(saveBtn);
    mainLayout->addLayout(toolLayout);

    // 下部：日志视图
    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(kMaxLogLines);
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    m_logView->setFont(mono);
    mainLayout->addWidget(m_logView);

    // 底部：状态栏
    m_statusLabel = new QLabel();
    mainLayout->addWidget(m_statusLabel);
}

void CommMonitorDialog::ensureDeviceRow(const QString &name)
{
    for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
        if (m_deviceTable->item(row, 0)
            && m_deviceTable->item(row, 0)->text() == name) {
            return;
        }
    }
    int row = m_deviceTable->rowCount();
    m_deviceTable->insertRow(row);
    auto *nameItem = new QTableWidgetItem(name);
    m_deviceTable->setItem(row, 0, nameItem);

    const auto &st = m_stats.value(name);
    m_deviceTable->setItem(row, 1, new QTableWidgetItem(st.type));
    auto *stateItem = new QTableWidgetItem(
        st.connected ? QStringLiteral("\u5DF2\u8FDE\u63A5") : QStringLiteral("\u672A\u8FDE\u63A5"));
    stateItem->setForeground(st.connected ? QBrush(QColor(0, 150, 0))
                                          : QBrush(QColor(180, 180, 180)));
    m_deviceTable->setItem(row, 2, stateItem);
    m_deviceTable->setItem(row, 3, new QTableWidgetItem(QString::number(st.bytesReceived)));
    m_deviceTable->setItem(row, 4, new QTableWidgetItem(QString::number(st.bytesSent)));
    m_deviceTable->setItem(row, 5, new QTableWidgetItem(
        QStringLiteral("%1 / %2").arg(st.framesReceived).arg(st.framesSent)));
}

void CommMonitorDialog::onDataReceived(const QString &deviceName, const QByteArray &data)
{
    auto &st = m_stats[deviceName];
    st.bytesReceived += static_cast<quint64>(data.size());
    st.framesReceived++;
    ensureDeviceRow(deviceName);
    appendLog(deviceName, QStringLiteral("\u6536"), data);
}

void CommMonitorDialog::onDataSent(const QString &deviceName, const QByteArray &data)
{
    auto &st = m_stats[deviceName];
    st.bytesSent += static_cast<quint64>(data.size());
    st.framesSent++;
    ensureDeviceRow(deviceName);
    appendLog(deviceName, QStringLiteral("\u53D1"), data);
}

void CommMonitorDialog::onDeviceConnected(const QString &name)
{
    if (m_stats.contains(name)) {
        m_stats[name].connected = true;
        ensureDeviceRow(name);
        for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
            if (m_deviceTable->item(row, 0) && m_deviceTable->item(row, 0)->text() == name) {
                auto *it = m_deviceTable->item(row, 2);
                it->setText(QStringLiteral("\u5DF2\u8FDE\u63A5"));
                it->setForeground(QBrush(QColor(0, 150, 0)));
                break;
            }
        }
    }
}

void CommMonitorDialog::onDeviceDisconnected(const QString &name)
{
    if (m_stats.contains(name)) {
        m_stats[name].connected = false;
        ensureDeviceRow(name);
        for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
            if (m_deviceTable->item(row, 0) && m_deviceTable->item(row, 0)->text() == name) {
                auto *it = m_deviceTable->item(row, 2);
                it->setText(QStringLiteral("\u672A\u8FDE\u63A5"));
                it->setForeground(QBrush(QColor(180, 180, 180)));
                break;
            }
        }
    }
}

void CommMonitorDialog::onDeviceAdded(const QString &name, const QString &type)
{
    DeviceStats st;
    st.type = type;
    m_stats.insert(name, st);
    m_deviceOrder.append(name);
    ensureDeviceRow(name);
}

void CommMonitorDialog::onDeviceRemoved(const QString &name)
{
    m_stats.remove(name);
    m_deviceOrder.removeAll(name);
    for (int row = m_deviceTable->rowCount() - 1; row >= 0; --row) {
        if (m_deviceTable->item(row, 0) && m_deviceTable->item(row, 0)->text() == name) {
            m_deviceTable->removeRow(row);
        }
    }
}

void CommMonitorDialog::appendLog(const QString &deviceName, const QString &direction,
                                  const QByteArray &data)
{
    auto &st = m_stats[deviceName];
    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString dirStr = (direction == QStringLiteral("\u53D1"))
                         ? QStringLiteral("\u2192 \u53D1\u9001")
                         : QStringLiteral("\u2190 \u63A5\u6536");

    QString hex = data.isEmpty() ? QStringLiteral("(empty)")
                                 : data.toHex(' ').toUpper();
    QString ascii;
    for (char c : data) {
        ascii += (c >= 32 && c <= 126) ? QChar::fromLatin1(c) : QChar('.');
    }
    QString line = QStringLiteral("[%1] [%2] %3\n    HEX  : %4\n    ASCII: %5")
                       .arg(ts, deviceName + QLatin1String(" ") + dirStr, hex, ascii);
    st.logLines.append(line);
    if (st.logLines.size() > kMaxLogLines) {
        st.logLines.removeFirst();
    }
    // 节流：高频收发时合并重绘（200ms 一次），避免每帧全量重绘拖慢监视窗与主界面
    m_refreshDirty = true;
    if (m_refreshThrottle && !m_refreshThrottle->isActive())
        m_refreshThrottle->start();
}

QString CommMonitorDialog::formatHex(const QByteArray &data) const
{
    return data.isEmpty() ? QStringLiteral("(empty)") : data.toHex(' ').toUpper();
}

QString CommMonitorDialog::formatAscii(const QByteArray &data) const
{
    QString ascii;
    for (char c : data) {
        ascii += (c >= 32 && c <= 126) ? QChar::fromLatin1(c) : QChar('.');
    }
    return ascii;
}

void CommMonitorDialog::refreshDetailView()
{
    // 找到当前选中的设备
    QString selected;
    int row = m_deviceTable->currentRow();
    if (row >= 0 && m_deviceTable->item(row, 0)) {
        selected = m_deviceTable->item(row, 0)->text();
    }
    if (selected.isEmpty() || !m_stats.contains(selected)) {
        m_logView->clear();
        return;
    }

    const auto &st = m_stats[selected];
    int mode = m_viewModeCombo->currentIndex();

    QStringList out;
    out << QStringLiteral("\u2500\u2500 %1 (%2) \u2500\u2500")
               .arg(selected, st.type);

    for (const QString &rawLine : st.logLines) {
        // 每行日志是三行的块；按模式裁剪显示
        QStringList parts = rawLine.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            if (p.startsWith(QLatin1String("    "))) {
                bool hexLine = p.startsWith(QLatin1String("    HEX"));
                if (mode == 1 && !hexLine) continue;
                if (mode == 2 && hexLine) continue;
            }
            out << p;
        }
    }
    m_logView->setPlainText(out.join(QLatin1Char('\n')));
}

void CommMonitorDialog::onDeviceSelectionChanged()
{
    refreshDetailView();
}

void CommMonitorDialog::onClearLog()
{
    for (auto it = m_stats.begin(); it != m_stats.end(); ++it) {
        it.value().logLines.clear();
        it.value().logLineCount = 0;
    }
    m_logView->clear();
}

void CommMonitorDialog::onClearStats()
{
    for (auto it = m_stats.begin(); it != m_stats.end(); ++it) {
        it.value().bytesReceived = 0;
        it.value().bytesSent = 0;
        it.value().framesReceived = 0;
        it.value().framesSent = 0;
        it.value().logLines.clear();
        it.value().logLineCount = 0;
    }
    for (int row = 0; row < m_deviceTable->rowCount(); ++row) {
        if (m_deviceTable->item(row, 3)) {
            m_deviceTable->item(row, 3)->setText(QStringLiteral("0"));
            m_deviceTable->item(row, 4)->setText(QStringLiteral("0"));
            m_deviceTable->item(row, 5)->setText(QStringLiteral("0 / 0"));
        }
    }
    m_logView->clear();
    updateStatus();
}

void CommMonitorDialog::updateStatus()
{
    quint64 rx = 0, tx = 0;
    for (const auto &st : m_stats) {
        rx += st.bytesReceived;
        tx += st.bytesSent;
    }
    m_statusLabel->setText(
        QStringLiteral("\u603B\u6536: %1 B | \u603B\u53D1: %2 B | \u8BBE\u5907\u6570: %3")
            .arg(rx).arg(tx).arg(m_stats.size()));
}

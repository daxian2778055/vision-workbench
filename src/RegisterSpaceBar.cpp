#include "RegisterSpaceBar.h"
#include <QTableWidget>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialog>
#include <QKeyEvent>
#include <QSet>

RegisterSpaceBar::RegisterSpaceBar(QTableWidget *table, QWidget *space,
                                   QWidget *spaceHome, QWidget *parent)
    : QWidget(parent), m_table(table), m_space(space), m_spaceHome(spaceHome)
{
    m_homeLayout = qobject_cast<QVBoxLayout *>(spaceHome->layout());

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);

    auto *startLabel = new QLabel(QStringLiteral("\u9996\u5730\u5740"), this);
    m_startSpin = new QSpinBox(this);
    m_startSpin->setObjectName(QStringLiteral("regSpaceStart"));
    m_startSpin->setRange(0, 65535);
    auto *countLabel = new QLabel(QStringLiteral("\u5BC4\u5B58\u5668\u4E2A\u6570"), this);
    m_countSpin = new QSpinBox(this);
    m_countSpin->setObjectName(QStringLiteral("regSpaceCount"));
    m_countSpin->setRange(1, 1024);
    m_countSpin->setValue(8);
    auto *addBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5730\u5740"), this);
    addBtn->setObjectName(QStringLiteral("regSpaceAdd"));
    row->addWidget(startLabel);
    row->addWidget(m_startSpin);
    row->addWidget(countLabel);
    row->addWidget(m_countSpin);
    row->addWidget(addBtn);

    auto *syncBtn = new QPushButton(QStringLiteral("\u6570\u636E\u540C\u6B65"), this);
    syncBtn->setObjectName(QStringLiteral("regSpaceSync"));
    row->addWidget(syncBtn);

    m_testAddrCombo = new QComboBox(this);
    m_testAddrCombo->setObjectName(QStringLiteral("regSpaceTestAddr"));
    m_testAddrCombo->setMinimumWidth(70);
    m_testValueEdit = new QLineEdit(this);
    m_testValueEdit->setObjectName(QStringLiteral("regSpaceTestValue"));
    m_testValueEdit->setPlaceholderText(QStringLiteral("\u503C"));
    m_testValueEdit->setMaximumWidth(90);
    auto *testBtn = new QPushButton(QStringLiteral("\u53D1\u9001\u6D4B\u8BD5"), this);
    testBtn->setObjectName(QStringLiteral("regSpaceTestSend"));
    row->addWidget(m_testAddrCombo);
    row->addWidget(m_testValueEdit);
    row->addWidget(testBtn);

    auto *maxBtn = new QPushButton(QStringLiteral("\u6700\u5927\u5316"), this);
    maxBtn->setObjectName(QStringLiteral("regSpaceMax"));
    maxBtn->setCheckable(true);
    row->addWidget(maxBtn);

    auto *blankBtn = new QPushButton(QStringLiteral("\u9690\u85CF\u7A7A\u767D\u884C"), this);
    blankBtn->setObjectName(QStringLiteral("regSpaceBlankToggle"));
    blankBtn->setCheckable(true);
    row->addWidget(blankBtn);

    auto *clearBtn = new QPushButton(QStringLiteral("\u6E05\u7A7A"), this);
    clearBtn->setObjectName(QStringLiteral("regSpaceClear"));
    row->addWidget(clearBtn);
    row->addStretch();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("regSpaceStatus"));
    m_statusLabel->setStyleSheet("color: gray; font-size: 11px;");
    row->addWidget(m_statusLabel);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        // 状态提示由 addAddressesInSpace 内部统一给出（含越界跳过数量），
        // 这里再覆盖一次会把「跳过 N 个越界地址」这条信息抹掉。
        addAddressesInSpace();
    });
    connect(syncBtn, &QPushButton::clicked, this, [this]() { emit syncRequested(); });
    connect(testBtn, &QPushButton::clicked, this, [this]() {
        emit testSendRequested(m_testAddrCombo->currentText(), m_testValueEdit->text());
    });
    connect(maxBtn, &QPushButton::clicked, this, [this, maxBtn](bool on) {
        if (on) { maximizeSpace(); if (!isMaximized()) maxBtn->setChecked(false); }
        else restoreSpace();
    });
    connect(blankBtn, &QPushButton::clicked, this, [this, blankBtn](bool on) {
        const int n = setBlankRowsHidden(on);
        blankBtn->setText(on ? QStringLiteral("\u663E\u793A\u7A7A\u767D\u884C")
                             : QStringLiteral("\u9690\u85CF\u7A7A\u767D\u884C"));
        setStatus(on ? QStringLiteral("\u5DF2\u9690\u85CF %1 \u884C\u7A7A\u767D\u5730\u5740").arg(n)
                     : QStringLiteral("\u5DF2\u6062\u590D\u5168\u90E8\u5730\u5740"));
    });
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        clearRows();
        setStatus(QStringLiteral("\u5730\u5740\u7A7A\u95F4\u5DF2\u6E05\u7A7A"));
    });
    connect(this, &RegisterSpaceBar::rowsChanged, this, &RegisterSpaceBar::refreshAddressChoices);
    refreshAddressChoices();
}

int RegisterSpaceBar::addAddressesInSpace()
{
    QSet<int> existing;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (auto *it = m_table->item(r, 0)) existing.insert(it->text().toInt());

    // 16 位寄存器地址空间是 0..65535（2026-11-11）：`start + i` 不钳上界时，
    // 首地址 65532 + 8 个会产出 65539，而协议层按 quint16 打包 → 越界值回绕到低地址，
    // 等于往**错误的寄存器**写值。现场表现是「明明写 65539，结果改坏了 3 号寄存器」，
    // 极难反查。这里跳过越界项，且跳过数量必须在状态栏说清——静默少加同样是坑。
    constexpr int kMaxRegisterAddress = 65535;
    // Qt 的 QTableWidget::setItem 在列不存在时会**静默丢弃**该 item：表格若还是 0 列，
    // 下面每行都 insertRow 成功、setItem 全被丢掉，added 却照数上报 —— 状态栏说
    // 「已补齐 6 个地址」而表里一行都没有（2026-11-11 实测：夹具表格未设列即复现）。
    // 这里补一列自愈，保证 added 与实际可见行数一致。
    if (m_table->columnCount() == 0)
        m_table->setColumnCount(1);
    int added = 0;
    int skipped = 0;
    const int start = m_startSpin->value();
    for (int i = 0; i < m_countSpin->value(); ++i) {
        const int addr = start + i;
        if (addr > kMaxRegisterAddress) {
            ++skipped;
            continue;
        }
        if (existing.contains(addr)) continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *addrItem = new QTableWidgetItem(QString::number(addr));
        addrItem->setFlags(addrItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, addrItem);   // 其余列留空 = 空白行（未配置）
        // m_blankHidden 此前**只写不读**（纯摆设）：新行天生就是空白行，
        // 隐藏态要跟着保持，否则「隐藏空白行」开着却越添加越多可见空白行。
        if (m_blankHidden)
            m_table->setRowHidden(row, true);
        ++added;
    }
    if (added > 0) emit rowsChanged();

    if (skipped > 0) {
        setStatus(QStringLiteral("已补齐 %1 个地址；地址空间到 %2 为止，跳过 %3 个越界地址")
                      .arg(added).arg(kMaxRegisterAddress).arg(skipped));
    } else {
        setStatus(QStringLiteral("已补齐 %1 个地址").arg(added));
    }
    return added;
}

void RegisterSpaceBar::clearRows()
{
    m_table->setRowCount(0);
    emit rowsChanged();
}

int RegisterSpaceBar::setBlankRowsHidden(bool hidden)
{
    m_blankHidden = hidden;
    int hiddenCount = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        // 空白行 = 「数据类型」列没有配置控件（仅有地址）
        const bool blank = m_table->cellWidget(r, 1) == nullptr;
        if (hidden && blank) {
            m_table->setRowHidden(r, true);
            ++hiddenCount;
        } else {
            m_table->setRowHidden(r, false);
        }
    }
    return hiddenCount;
}

void RegisterSpaceBar::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}

void RegisterSpaceBar::showNotConnectedStatus()
{
    setStatus(QStringLiteral("未连接，未发起读取"));
}

void RegisterSpaceBar::showSyncIssuedStatus(int count)
{
    setStatus(QStringLiteral("已发起 %1 条读取").arg(count));
}

void RegisterSpaceBar::showNoNodeStatus()
{
    setStatus(QStringLiteral("尚未关联通信节点"));
}

void RegisterSpaceBar::showInvalidAddressStatus()
{
    setStatus(QStringLiteral("测试地址无效"));
}

void RegisterSpaceBar::showInvalidValueStatus()
{
    setStatus(QStringLiteral("测试值无效"));
}

void RegisterSpaceBar::showTestSentStatus(int address, double value)
{
    setStatus(QStringLiteral("已发送测试写入 %1 = %2").arg(address).arg(value));
}

void RegisterSpaceBar::showTestNotSentStatus()
{
    setStatus(QStringLiteral("发送测试未发出（未连接或地址未配置）"));
}

void RegisterSpaceBar::refreshAddressChoices()
{
    const QString keep = m_testAddrCombo->currentText();
    m_testAddrCombo->clear();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (auto *it = m_table->item(r, 0)) m_testAddrCombo->addItem(it->text());
    }
    if (!keep.isEmpty()) {
        const int idx = m_testAddrCombo->findText(keep);
        if (idx >= 0) m_testAddrCombo->setCurrentIndex(idx);
    }
}

void RegisterSpaceBar::maximizeSpace()
{
    if (m_maxWindow) return;
    QWidget *win = m_space->window();
    if (!win || win == m_space) return;
    m_maxWindow = win;
    m_savedVisibility.clear();
    // 藏掉地址空间以外的全部子控件（记录原可见态，退出时逐件还原）
    const auto children = win->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : children) {
        if (w == m_space || m_space->isAncestorOf(w))
            continue;
        m_savedVisibility.append({w, w->isVisible()});
        w->hide();
    }
    win->installEventFilter(this);
    win->showFullScreen();
    setStatus(QStringLiteral("\u5DF2\u5168\u5C4F\u663E\u793A\u5730\u5740\u7A7A\u95F4\uFF08Esc \u9000\u51FA\uFF09"));
}

void RegisterSpaceBar::restoreSpace()
{
    if (!m_maxWindow) return;
    QWidget *win = m_maxWindow;
    m_maxWindow = nullptr;
    win->removeEventFilter(this);
    for (const auto &pair : m_savedVisibility) {
        if (pair.first) pair.first->setVisible(pair.second);
    }
    m_savedVisibility.clear();
    win->showNormal();
    setStatus(QStringLiteral("\u5DF2\u9000\u51FA\u5168\u5C4F"));
}

bool RegisterSpaceBar::eventFilter(QObject *watched, QEvent *event)
{
    if (m_maxWindow && watched == m_maxWindow && event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            restoreSpace();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

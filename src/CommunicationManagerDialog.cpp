#include "CommunicationManagerDialog.h"
#include "CommunicationManager.h"
#include "CommunicationNodeBase.h"
#include "GlobalTriggerDialog.h"
#include "ModbusNode.h"
#include "ModbusConfigDialog.h"
#include "FinsConfigDialog.h"
#include "PlcCommNode.h"
#include "PlcConfigDialog.h"
#include "ReceiveEvent.h"
#include "SendEvent.h"
#include "CommEventEditDialogs.h"
#include "CommDeviceConfigDialog.h"
#include "CommMonitorDialog.h"
#include "CommMonitorPane.h"
#include "VmWidgets.h"
#include "VmDeviceCreateDialog.h"
#include "HeartbeatManager.h"
#include "GlobalTriggerManager.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFormLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QFile>
#include <QSaveFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <QColor>
#include <QTimer>
#include <QListWidget>
#include <QStackedWidget>
#include <QMenu>
#include <QAction>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QMouseEvent>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QListView>
#include <QIcon>
#include <QGridLayout>
#include <QIntValidator>
#include <QFrame>
#include <cmath>

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// VM4.4 通信管理形态复刻（2026-11-13）
//
// 截图口径：深灰顶栏 + 92px 图标导航（图标在上、文字在下、选中橙色）、
// 设备列表是「序号 + 名称 + 小字类型 + 右侧开关」的卡片、右侧参数内联编辑。
// 这些是纯呈现件：不改变 CommunicationManager 的数据模型，也不替换既有控件，
// 否则 comm_writeback_test 里对 deviceTable / cellWidget(0,2) 的真实点击会失效。
// ─────────────────────────────────────────────────────────────────────────────

const QColor kVmOrange("#f26b21");

enum class NavIconKind { Device, Receive, Send, Trigger, Response, Heartbeat };

/// 线性简笔图标（32×32）。不引入 qrc：项目出包时资源清单固定，新增资源会动登记。
QPixmap makeNavIcon(NavIconKind kind, const QColor &color)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case NavIconKind::Device:                 // 两台叠放的显示器
        p.drawRoundedRect(2, 4, 19, 14, 2, 2);
        p.drawLine(11, 18, 11, 22);
        p.drawLine(6, 22, 17, 22);
        p.drawRoundedRect(12, 10, 18, 14, 2, 2);
        p.drawLine(21, 24, 21, 27);
        p.drawLine(16, 27, 26, 27);
        break;
    case NavIconKind::Receive:                // 文件 + 向右箭头（接收）
        p.drawRect(5, 4, 18, 24);
        p.drawLine(5, 4, 17, 4);
        p.drawLine(17, 4, 23, 10);
        p.drawLine(23, 10, 23, 12);
        p.drawLine(9, 17, 19, 17);
        p.drawLine(16, 14, 19, 17);
        p.drawLine(16, 20, 19, 17);
        break;
    case NavIconKind::Send:                   // 文件 + 向左箭头（发送）
        p.drawRect(5, 4, 18, 24);
        p.drawLine(5, 4, 17, 4);
        p.drawLine(17, 4, 23, 10);
        p.drawLine(23, 10, 23, 12);
        p.drawLine(9, 17, 19, 17);
        p.drawLine(12, 14, 9, 17);
        p.drawLine(12, 20, 9, 17);
        break;
    case NavIconKind::Trigger:                // 齿轮（简化为圆 + 六齿）
        p.drawEllipse(9, 9, 14, 14);
        p.drawEllipse(14, 14, 4, 4);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 3.14159265358979323846 / 4.0;
            p.drawLine(QPointF(16 + 8 * std::cos(a), 16 + 8 * std::sin(a)),
                       QPointF(16 + 11 * std::cos(a), 16 + 11 * std::sin(a)));
        }
        break;
    case NavIconKind::Response:               // 沙漏 / 响应
        p.drawLine(7, 5, 25, 5);
        p.drawLine(7, 27, 25, 27);
        p.drawLine(9, 6, 16, 16);
        p.drawLine(23, 6, 16, 16);
        p.drawLine(9, 26, 16, 16);
        p.drawLine(23, 26, 16, 16);
        break;
    case NavIconKind::Heartbeat:              // 设备 + 小齿轮
        p.drawRect(4, 7, 18, 15);
        p.drawLine(4, 12, 22, 12);
        p.drawEllipse(19, 17, 10, 10);
        p.drawEllipse(22, 20, 4, 4);
        break;
    }
    p.end();
    return pm;
}

/// 设备列表卡片：序号 + 名称（大字）、类型（小字）、选中态浅橙底 + 2px 橙色下划线。
/// 只画第 0 列；第 2 列仍是 cellWidget 的开关按钮，测试的真实点击路径不变。
class DeviceCardDelegate final : public QStyledItemDelegate
{
public:
    explicit DeviceCardDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const QRect r = option.rect;
        const bool selected = (option.state & QStyle::State_Selected);
        painter->save();
        painter->fillRect(r, selected ? QColor("#fff2e9") : QColor("#ffffff"));
        if (selected) {
            painter->fillRect(QRect(r.left(), r.bottom() - 2, r.width(), 2), kVmOrange);
        } else {
            painter->setPen(QColor("#ededed"));
            painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());
        }
        const QString name = index.data(Qt::DisplayRole).toString();
        const QString type = index.data(Qt::UserRole).toString();
        QFont f = option.font;
        f.setPointSize(option.font.pointSize() + 1);
        painter->setFont(f);
        painter->setPen(selected ? QColor("#e8611a") : QColor("#333333"));
        painter->drawText(QRect(r.left() + 12, r.top() + 7, r.width() - 18, 22),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("%1  %2").arg(index.row() + 1).arg(name));
        QFont smallFont = option.font;   // 不能用 small：Windows SDK 有 #define small char
        smallFont.setPointSize(qMax(8, option.font.pointSize() - 1));
        painter->setFont(smallFont);
        painter->setPen(QColor("#999999"));
        painter->drawText(QRect(r.left() + 12, r.top() + 29, r.width() - 18, 16),
                          Qt::AlignLeft | Qt::AlignVCenter, type);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(200, 56);
    }
};

// VmSwitch 已提取到 include/VmWidgets.h：创建设备弹窗也要用同款滑块，
// 两处各写一份会让形态慢慢漂移。

}   // namespace

CommunicationManagerDialog::CommunicationManagerDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUI();
    refreshDeviceTable();
    refreshReceiveEventTable();
    refreshSendEventTable();
    refreshHeartbeatTable();
    refreshResponseDeviceCombos();

    // 响应配置页回显：把已保存的配置灌回控件（阻断信号，避免回显本身触发覆盖写）
    loadResponseConfigToUi();

    // 重命名可能来自本对话框之外（测试/脚本/其它入口）：信号驱动各表同步，
    // 设备表重建时内部会连带刷新控制器页
    auto *cm = CommunicationManager::instance();
    connect(cm, &CommunicationManager::deviceRenamed, this,
            [this](const QString &, const QString &) {
        refreshDeviceTable();
        refreshReceiveEventTable();
        refreshSendEventTable();
        refreshHeartbeatTable();
        refreshResponseDeviceCombos();
    });
    connect(cm, &CommunicationManager::deviceAdded, this,
            [this](const QString &, const QString &) { refreshResponseDeviceCombos(); });
    connect(cm, &CommunicationManager::deviceRemoved, this,
            [this](const QString &) { refreshResponseDeviceCombos(); });
    connect(cm, &CommunicationManager::receiveEventRenamed, this,
            [this](const QString &, const QString &) { refreshReceiveEventTable(); });
    connect(cm, &CommunicationManager::sendEventRenamed, this,
            [this](const QString &, const QString &) { refreshSendEventTable(); });
}

CommunicationManagerDialog::~CommunicationManagerDialog()
{
}

void CommunicationManagerDialog::loadResponseConfigToUi()
{
    auto *cm = CommunicationManager::instance();
    const QJsonObject root = cm->responseConfig();
    const QJsonObject scheme = root.value(QStringLiteral("schemeLoad")).toObject();
    const QJsonObject flow = root.value(QStringLiteral("flowControl")).toObject();
    const QJsonObject camera = root.value(QStringLiteral("cameraResponse")).toObject();
    const QList<QWidget *> all{m_respSchemeEnable, m_respSchemeDevice, m_respSchemeDelay,
                               m_respSchemeTerm, m_respSchemeContent,
                               m_respFlowEnable, m_respFlowDevice, m_respFlowTerm,
                               m_respFlowSep, m_respFlowIdle, m_respFlowBusy,
                               m_respCameraEnable, m_respCameraDevice, m_respCameraSep,
                               m_respCameraOn, m_respCameraOff};
    if (!m_respSchemeEnable) return;   // 响应页尚未建（构造早期防御）
    for (QWidget *w : all) w->blockSignals(true);
    m_respSchemeEnable->setChecked(scheme.value(QStringLiteral("enabled")).toBool());
    if (!scheme.value(QStringLiteral("device")).toString().isEmpty())
        m_respSchemeDevice->setCurrentText(scheme.value(QStringLiteral("device")).toString());
    m_respSchemeDelay->setValue(scheme.value(QStringLiteral("delaySec")).toInt());
    if (!scheme.value(QStringLiteral("terminator")).toString().isEmpty())
        m_respSchemeTerm->setCurrentText(scheme.value(QStringLiteral("terminator")).toString());
    m_respSchemeContent->setText(scheme.value(QStringLiteral("content")).toString());

    m_respFlowEnable->setChecked(flow.value(QStringLiteral("enabled")).toBool());
    if (!flow.value(QStringLiteral("device")).toString().isEmpty())
        m_respFlowDevice->setCurrentText(flow.value(QStringLiteral("device")).toString());
    if (!flow.value(QStringLiteral("terminator")).toString().isEmpty())
        m_respFlowTerm->setCurrentText(flow.value(QStringLiteral("terminator")).toString());
    if (!flow.value(QStringLiteral("separator")).toString().isEmpty())
        m_respFlowSep->setCurrentText(flow.value(QStringLiteral("separator")).toString());
    m_respFlowIdle->setText(flow.value(QStringLiteral("idleContent")).toString());
    m_respFlowBusy->setText(flow.value(QStringLiteral("busyContent")).toString());

    m_respCameraEnable->setChecked(camera.value(QStringLiteral("enabled")).toBool());
    if (!camera.value(QStringLiteral("device")).toString().isEmpty())
        m_respCameraDevice->setCurrentText(camera.value(QStringLiteral("device")).toString());
    if (!camera.value(QStringLiteral("separator")).toString().isEmpty())
        m_respCameraSep->setCurrentText(camera.value(QStringLiteral("separator")).toString());
    m_respCameraOn->setText(camera.value(QStringLiteral("connectedContent")).toString());
    m_respCameraOff->setText(camera.value(QStringLiteral("disconnectedContent")).toString());
    for (QWidget *w : all) w->blockSignals(false);
}

void CommunicationManagerDialog::updateParamSummary()
{
    // 右下收发数据区的手动发送目标跟随当前选中的通信设备
    //（即使参数控件尚未建好，这一步也要做）
    const int selectedRow = m_deviceTable ? m_deviceTable->currentRow() : -1;
    if (m_monitorPane) {
        m_monitorPane->setTargetDevice(
            selectedRow >= 0 && m_deviceTable->item(selectedRow, 0)
                ? m_deviceTable->item(selectedRow, 0)->text()
                : QString());
    }

    // VM 形态：参数就在右侧内联编辑，只有选中真实通信设备时才可用。
    const bool hasDevice = (selectedRow >= 0 && m_deviceTable->item(selectedRow, 0));
    if (m_paramForm) m_paramForm->setEnabled(hasDevice);
    if (!m_paramSummary) return;

    if (hasDevice) {
        const QString name = m_deviceTable->item(selectedRow, 0)->text();
        const CommDeviceInfo info = CommunicationManager::instance()->deviceInfo(name);
        syncInlineParamsFromSelection(info);
        m_paramSummary->setText(QStringLiteral("%1  \u00B7  %2  \u00B7  %3")
            .arg(name, info.type,
                 info.isConnected ? QStringLiteral("\u5DF2\u8FDE\u63A5")
                                  : QStringLiteral("\u672A\u8FDE\u63A5")));
        return;
    }
    const int ctlRow = m_controllerTable ? m_controllerTable->currentRow() : -1;
    if (ctlRow >= 0 && m_controllerTable->item(ctlRow, 0)) {
        m_paramSummary->setText(QStringLiteral(
            "\u63A7\u5236\u5668\u5217\u8868\u7531\u901A\u4FE1\u8BBE\u5907\u6D3E\u751F"));
        return;
    }
    m_paramSummary->setText(QStringLiteral("\u672A\u9009\u4E2D\u8BBE\u5907"));
}

void CommunicationManagerDialog::syncInlineParamsFromSelection(const CommDeviceInfo &info)
{
    if (!m_paramForm) return;
    // 回显期间所有 setChecked/setValue 都会发信号，用守卫让写回直接返回。
    m_inlineLoading = true;
    const QString ip = info.config.value(QStringLiteral("serverIp"))
                           .toString(QStringLiteral("127.0.0.1"));
    const QStringList parts = ip.split(QLatin1Char('.'));
    for (int i = 0; i < 4; ++i) {
        if (m_ipEdits[i])
            m_ipEdits[i]->setText(parts.value(i, QStringLiteral("0")));
    }
    if (m_inlinePort)
        m_inlinePort->setValue(info.config.value(QStringLiteral("port")).toInt(502));
    if (m_inlineDataUpload)
        m_inlineDataUpload->setChecked(
            info.config.value(QStringLiteral("dataUpload")).toBool(true));
    if (m_inlineAutoReconnect)
        m_inlineAutoReconnect->setChecked(
            info.config.value(QStringLiteral("autoReconnect")).toBool(true));
    if (m_inlineRxTerm)
        m_inlineRxTerm->setChecked(
            !info.config.value(QStringLiteral("frameTerminator")).toString().isEmpty());
    m_inlineLoading = false;
}

void CommunicationManagerDialog::applyInlineParamChange()
{
    if (m_inlineLoading || !m_paramForm || !m_paramForm->isEnabled()) return;
    const int row = m_deviceTable ? m_deviceTable->currentRow() : -1;
    if (row < 0 || !m_deviceTable->item(row, 0)) return;

    const QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    QJsonObject cfg = cm->deviceInfo(name).config;

    // 四段都填了才写 IP：编辑到一半的 "127..0.1" 不能覆盖原配置。
    bool complete = true;
    QStringList segs;
    for (int i = 0; i < 4; ++i) {
        const QString seg = m_ipEdits[i] ? m_ipEdits[i]->text().trimmed() : QString();
        if (seg.isEmpty()) complete = false;
        segs << seg;
    }
    if (complete)
        cfg[QStringLiteral("serverIp")] = segs.join(QLatin1Char('.'));
    if (m_inlinePort)
        cfg[QStringLiteral("port")] = m_inlinePort->value();
    if (m_inlineDataUpload)
        cfg[QStringLiteral("dataUpload")] = m_inlineDataUpload->isChecked();
    if (m_inlineAutoReconnect)
        cfg[QStringLiteral("autoReconnect")] = m_inlineAutoReconnect->isChecked();
    if (m_inlineRxTerm)
        cfg[QStringLiteral("frameTerminator")] =
            m_inlineRxTerm->isChecked() ? QStringLiteral("\r\n") : QString();

    cm->updateDeviceConfig(name, cfg);
    if (m_paramSummary)
        m_paramSummary->setText(QStringLiteral("\u53C2\u6570\u5DF2\u5373\u65F6\u4FDD\u5B58"));
}

// ==================== gtd import / export ====================

void CommunicationManagerDialog::onExportGtd()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出通信配置"),
        QStringLiteral("通信配置.gtd"),
        QStringLiteral("通信配置 (*.gtd);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!exportGtdTo(path))
        QMessageBox::warning(this, QStringLiteral("导出失败"),
                             QStringLiteral("无法写入 gtd 文件：%1").arg(path));
}

bool CommunicationManagerDialog::exportGtdTo(const QString &path)
{
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("visionflow-communication");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("communication")] = CommunicationManager::instance()->toJson();
    root[QStringLiteral("globalTriggers")] = GlobalTriggerManager::instance()->toJson();
    root[QStringLiteral("heartbeat")] = HeartbeatManager::instance()->toJson();

    // 原子写 + 检查实际写入字节：QFile::write 成功不代表落盘成功，
    // 半截的 gtd 会反过来让「导入」把现场配置清掉（2026-11-10 审核）。
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (f.write(bytes) != bytes.size())
        return false;
    return f.commit();
}

void CommunicationManagerDialog::onImportGtd()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入通信配置"), QString(),
        QStringLiteral("通信配置 (*.gtd);;所有文件 (*)"));
    if (path.isEmpty()) return;
    // 导入是整包覆盖：先确认，避免误点一次就抹掉现场全部通信配置
    if (QMessageBox::question(
            this, QStringLiteral("导入通信配置"),
            QStringLiteral("导入会整包覆盖当前的通信设备、接收/发送事件、触发配置、"
                           "响应配置与心跳设置。\n确定继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    if (!importGtdFrom(path)) {
        QMessageBox::warning(this, QStringLiteral("导入失败"),
                             QStringLiteral("文件不是有效的 gtd（JSON）通信配置。"));
        return;
    }
    if (!m_gtdImportRejected.isEmpty())
        QMessageBox::information(this, QStringLiteral("导入完成"),
                                 QStringLiteral("部分触发条目未导入：\n")
                                     + m_gtdImportRejected.join("\n"));
}

bool CommunicationManagerDialog::importGtdFrom(const QString &path)
{
    m_gtdImportRejected.clear();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString()
        != QStringLiteral("visionflow-communication"))
        return false;
    // 完整性校验（2026-11-10 审核）：旧版只校验 format，半残文件
    // {"format":"visionflow-communication"} 会通过，随后 fromJson({}) 无条件清空
    // 设备/接收事件/发送事件/响应配置，GlobalTrigger/Heartbeat 同样被清空
    // ⇒ 用户点一次「导入」就静默丢掉全部现场通信配置。
    if (root.value(QStringLiteral("version")).toInt(0) != 1)
        return false;
    const QJsonObject comm = root.value(QStringLiteral("communication")).toObject();
    const QJsonObject triggers = root.value(QStringLiteral("globalTriggers")).toObject();
    const QJsonObject heartbeat = root.value(QStringLiteral("heartbeat")).toObject();
    const auto hasArray = [](const QJsonObject &o, const QString &key) {
        return o.contains(key) && o.value(key).isArray();
    };
    if (!hasArray(comm, QStringLiteral("devices"))
        || !hasArray(comm, QStringLiteral("receiveEvents"))
        || !hasArray(comm, QStringLiteral("sendEvents"))
        || !hasArray(triggers, QStringLiteral("triggers"))
        || !hasArray(heartbeat, QStringLiteral("heartbeats")))
        return false;

    CommunicationManager::instance()->fromJson(comm);
    GlobalTriggerManager::instance()->fromJson(triggers, &m_gtdImportRejected);
    HeartbeatManager::instance()->fromJson(heartbeat);

    refreshDeviceTable();
    refreshReceiveEventTable();
    refreshSendEventTable();
    refreshHeartbeatTable();
    refreshResponseDeviceCombos();
    loadResponseConfigToUi();
    if (m_triggerDialog) m_triggerDialog->refreshTriggerTable();
    return true;
}

void CommunicationManagerDialog::setupUI()
{
    setWindowTitle(QStringLiteral("\u901A\u4FE1\u7BA1\u7406"));
    setMinimumSize(980, 620);
    // VM4.4 截图：深灰自定义标题栏、右上角两个图标 + 关闭，没有系统标题栏。
    // 用 FramelessWindowHint 才能让顶栏真正贴边；拖动在三个 mouse*Event 里自己实现。
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // ─────────────── 自定义顶栏 ───────────────
    m_titleBar = new QWidget(this);
    m_titleBar->setObjectName(QStringLiteral("commTitleBar"));
    m_titleBar->setFixedHeight(34);
    m_titleBar->setStyleSheet(QStringLiteral(
        "QWidget#commTitleBar { background-color: #2b2b2b; }"));
    auto *titleRow = new QHBoxLayout(m_titleBar);
    titleRow->setContentsMargins(12, 0, 6, 0);
    titleRow->setSpacing(8);

    auto *titleText = new QLabel(
        QStringLiteral("\u901A\u4FE1\u7BA1\u7406"), m_titleBar);
    titleText->setStyleSheet(QStringLiteral(
        "color: #ffffff; font-size: 13px; font-weight: bold;"));
    auto *helpMark = new QLabel(QStringLiteral("?"), m_titleBar);
    helpMark->setFixedSize(16, 16);
    helpMark->setAlignment(Qt::AlignCenter);
    helpMark->setStyleSheet(QStringLiteral(
        "color: #ffffff; background-color: #f26b21; border-radius: 8px; font-weight: bold;"));
    titleRow->addWidget(titleText);
    titleRow->addWidget(helpMark);
    titleRow->addStretch();

    auto makeTitleButton = [this](const QString &objectName, const QString &tip,
                                  const QString &glyph) {
        auto *b = new QPushButton(glyph, m_titleBar);
        b->setObjectName(objectName);
        b->setToolTip(tip);
        b->setFixedSize(30, 26);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { color: #d8d8d8; background: transparent; border: none; font-size: 16px; }"
            "QPushButton:hover { color: #ffffff; background-color: #444444; }"));
        return b;
    };
    m_exportGtdBtn = makeTitleButton(QStringLiteral("commGtdExport"),
        QStringLiteral("把设备/接收事件/发送事件/触发/心跳/响应配置导出为 gtd 文件（VM 同位置图标）"),
        QStringLiteral("\u21A7"));
    m_importGtdBtn = makeTitleButton(QStringLiteral("commGtdImport"),
        QStringLiteral("从 gtd 文件导入整包通信配置（覆盖当前配置）"),
        QStringLiteral("\u21A5"));
    m_closeBtn = makeTitleButton(QStringLiteral("commCloseBtn"),
        QStringLiteral("\u5173\u95ED"), QStringLiteral("\u00D7"));
    titleRow->addWidget(m_exportGtdBtn);
    titleRow->addWidget(m_importGtdBtn);
    titleRow->addWidget(m_closeBtn);
    mainLayout->addWidget(m_titleBar);

    connect(m_exportGtdBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onExportGtd);
    connect(m_importGtdBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onImportGtd);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // ─────────────── 主体：左图标导航 + 右堆叠页 ───────────────
    auto *bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_navList = new QListWidget();
    m_navList->setObjectName(QStringLiteral("commNavList"));   // 供回归测试按名定位
    m_navList->setFixedWidth(92);
    // IconMode 让图标在上、文字在下——正是截图里的形态；六项文案与索引都不变。
    m_navList->setViewMode(QListView::IconMode);
    m_navList->setIconSize(QSize(32, 32));
    m_navList->setGridSize(QSize(90, 84));
    m_navList->setMovement(QListView::Static);
    m_navList->setResizeMode(QListView::Adjust);
    m_navList->setUniformItemSizes(true);
    m_navList->setSpacing(0);
    m_navList->setStyleSheet(QStringLiteral(
        "QListWidget#commNavList { background-color: #f1f1f1; border: none; "
        "border-right: 1px solid #dddddd; }"
        "QListWidget#commNavList::item { color: #555555; background-color: #f1f1f1; "
        "border-bottom: 1px solid #e5e5e5; }"
        "QListWidget#commNavList::item:selected { color: #f26b21; background-color: #ffffff; "
        "border-left: 3px solid #f26b21; }"));

    m_pageStack = new QStackedWidget();
    m_pageStack->setObjectName(QStringLiteral("commPageStack"));
    bodyLayout->addWidget(m_navList);
    bodyLayout->addWidget(m_pageStack, 1);
    mainLayout->addLayout(bodyLayout, 1);

    // 页面添加顺序必须与导航项顺序严格一致（索引一一对应；VM 5.2 官方六项）
    setupDeviceTab(m_pageStack);          // 0 设备管理（含控制器列表；默认页，既有测试依赖）
    setupReceiveEventTab(m_pageStack);    // 1 接收事件
    setupSendEventTab(m_pageStack);       // 2 发送事件
    setupTriggerTab(m_pageStack);         // 3 触发配置（全局触发）
    setupResponseTab(m_pageStack);        // 4 响应配置
    setupHeartbeatTab(m_pageStack);       // 5 心跳管理

    const QStringList navTitles{
        QStringLiteral("\u8BBE\u5907\u7BA1\u7406"),
        QStringLiteral("\u63A5\u6536\u4E8B\u4EF6"),
        QStringLiteral("\u53D1\u9001\u4E8B\u4EF6"),
        QStringLiteral("\u89E6\u53D1\u914D\u7F6E"),
        QStringLiteral("\u54CD\u5E94\u914D\u7F6E"),
        QStringLiteral("\u5FC3\u8DF3\u7BA1\u7406")
    };
    m_navList->addItems(navTitles);
    for (int i = 0; i < m_navList->count(); ++i) {
        QListWidgetItem *item = m_navList->item(i);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setIcon(QIcon(makeNavIcon(static_cast<NavIconKind>(i), QColor("#8a8a8a"))));
    }
    connect(m_navList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_pageStack->count())
            m_pageStack->setCurrentIndex(row);
        // 选中项图标也转橙，未选中转灰（QSS 只管得了文字与背景）
        for (int i = 0; i < m_navList->count(); ++i) {
            m_navList->item(i)->setIcon(QIcon(makeNavIcon(
                static_cast<NavIconKind>(i),
                i == row ? kVmOrange : QColor("#8a8a8a"))));
        }
    });
    m_navList->setCurrentRow(0);   // 与 stack 默认页一致：设备页默认可见

    // 建页即填一次（表为空时就是空表；构造末尾的 refreshDeviceTable 还会再同步一次）
    refreshControllerTable();
}

void CommunicationManagerDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_titleBar
        && m_titleBar->geometry().contains(event->pos())) {
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void CommunicationManagerDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragPos.isNull()) {
        move(event->globalPosition().toPoint() - m_dragPos);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void CommunicationManagerDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragPos = QPoint();
    QDialog::mouseReleaseEvent(event);
}

// ==================== Device Tab ====================

void CommunicationManagerDialog::setupDeviceTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *pageLayout = new QVBoxLayout(tab);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    // VM4.4 截图：三栏 = 92px 图标导航(setupUI) / 240px 设备列表 / 剩余参数区。
    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // ─────────────── 左列：通信设备列表（卡片）+ 控制器列表 ───────────────
    auto *left = new QWidget();
    left->setFixedWidth(240);
    left->setStyleSheet(QStringLiteral("QWidget { background-color: #fafafa; }"));
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(12, 14, 10, 10);
    leftLayout->setSpacing(8);

    auto *devHeader = new QHBoxLayout();
    auto *devTitle = new QLabel(
        QStringLiteral("\u901A\u4FE1\u8BBE\u5907\u5217\u8868"), left);
    devTitle->setStyleSheet(QStringLiteral("font-size: 12px; color: #333333;"));
    devHeader->addWidget(devTitle);
    devHeader->addStretch();
    m_addDeviceBtn = new QPushButton(QStringLiteral("+"));
    m_addDeviceBtn->setObjectName(QStringLiteral("commAddDevice"));
    m_addDeviceBtn->setFixedSize(22, 22);
    m_addDeviceBtn->setToolTip(QStringLiteral("\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
    m_addDeviceBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #f26b21; border: 1px solid #f26b21; background: #ffffff; "
        "font-size: 15px; font-weight: bold; }"
        "QPushButton:hover { background-color: #fff2e9; }"));
    devHeader->addWidget(m_addDeviceBtn);
    leftLayout->addLayout(devHeader);

    // 仍是 QTableWidget（既有回归按名定位、且要真实点击 cellWidget(0,2)），
    // 但隐藏表头/网格/多余列，第 0 列委托绘制成 VM 卡片，第 2 列放小滑块开关。
    m_deviceTable = new QTableWidget();
    m_deviceTable->setObjectName(QStringLiteral("deviceTable"));
    m_deviceTable->setColumnCount(5);
    m_deviceTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8BBE\u5907\u540D\u79F0"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8FDE\u63A5\u72B6\u6001"),
        QStringLiteral("\u914D\u7F6E"),
        QStringLiteral("\u5BC4\u5B58\u5668\u503C")
    });
    m_deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deviceTable->horizontalHeader()->setVisible(false);
    m_deviceTable->verticalHeader()->setVisible(false);
    m_deviceTable->verticalHeader()->setDefaultSectionSize(56);
    m_deviceTable->setShowGrid(false);
    m_deviceTable->setFrameShape(QFrame::NoFrame);
    m_deviceTable->setFocusPolicy(Qt::NoFocus);
    m_deviceTable->setStyleSheet(QStringLiteral(
        "QTableWidget { background-color: #fafafa; border: none; }"
        "QTableWidget::item { border: none; }"));
    m_deviceTable->setItemDelegateForColumn(0, new DeviceCardDelegate(m_deviceTable));
    m_deviceTable->horizontalHeader()->setStretchLastSection(false);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_deviceTable->setColumnWidth(2, 56);
    m_deviceTable->setColumnHidden(1, true);
    m_deviceTable->setColumnHidden(3, true);
    m_deviceTable->setColumnHidden(4, true);
    leftLayout->addWidget(m_deviceTable, 1);

    // VM 用右键做重命名/删除，但高级参数（串口/UDP/FINS 专属字段）仍需入口，
    // 保留为左列底部的紧凑次要按钮，不占主视觉。
    auto *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(4);
    m_removeDeviceBtn = new QPushButton(QStringLiteral("\u5220\u9664\u8BBE\u5907"));
    m_configBtn = new QPushButton(QStringLiteral("\u914D\u7F6E"));
    m_readBtn = new QPushButton(QStringLiteral("\u5237\u65B0\u5BC4\u5B58\u5668"));
    for (QPushButton *b : { m_removeDeviceBtn, m_configBtn, m_readBtn }) {
        b->setMaximumHeight(24);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { color: #666666; border: 1px solid #d5d5d5; background: #ffffff; "
            "font-size: 11px; padding: 2px 6px; }"
            "QPushButton:hover { color: #f26b21; border-color: #f26b21; }"));
        btnLayout->addWidget(b);
    }
    btnLayout->addStretch();
    leftLayout->addLayout(btnLayout);

    connect(m_addDeviceBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddDevice);
    connect(m_removeDeviceBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveDevice);
    connect(m_configBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onConfigDevice);
    connect(m_readBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onReadRegisters);

    // VM 5.2.1：控制器列表并进设备管理页（官方布局：设备列表在上、控制器列表在下）
    buildControllerSection(leftLayout);

    // ─────────────── 右列：内联可编辑通信参数 + 收发数据区 ───────────────
    auto *right = new QWidget();
    right->setStyleSheet(QStringLiteral("QWidget { background-color: #ffffff; }"));
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(16, 14, 14, 12);
    rightLayout->setSpacing(8);

    auto *paramTitle = new QLabel(
        QStringLiteral("\u901A\u4FE1\u53C2\u6570"), right);
    paramTitle->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #333333; font-weight: bold;"));
    rightLayout->addWidget(paramTitle);

    // 官方没有「点配置弹窗」这一步：参数就在右侧直接改，改动即时写回。
    m_paramForm = new QWidget(right);
    m_paramForm->setObjectName(QStringLiteral("commParamForm"));
    auto *form = new QGridLayout(m_paramForm);
    form->setContentsMargins(0, 6, 0, 6);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(12);

    auto fieldLabel = [](const QString &text, QWidget *parent) {
        auto *l = new QLabel(text, parent);
        l->setStyleSheet(QStringLiteral("color: #555555; font-size: 12px;"));
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    auto *ipHost = new QWidget(m_paramForm);
    auto *ipRow = new QHBoxLayout(ipHost);
    ipRow->setContentsMargins(0, 0, 0, 0);
    ipRow->setSpacing(0);
    for (int i = 0; i < 4; ++i) {
        m_ipEdits[i] = new QLineEdit(ipHost);
        m_ipEdits[i]->setObjectName(QStringLiteral("commParamIp%1").arg(i));
        m_ipEdits[i]->setFixedWidth(46);
        m_ipEdits[i]->setMaxLength(3);
        m_ipEdits[i]->setAlignment(Qt::AlignCenter);
        m_ipEdits[i]->setValidator(new QIntValidator(0, 255, m_ipEdits[i]));
        m_ipEdits[i]->setStyleSheet(QStringLiteral(
            "QLineEdit { border: 1px solid #cfcfcf; padding: 3px; }"));
        ipRow->addWidget(m_ipEdits[i]);
        if (i < 3) {
            auto *dot = new QLabel(QStringLiteral("."), ipHost);
            dot->setFixedWidth(9);
            dot->setAlignment(Qt::AlignCenter);
            ipRow->addWidget(dot);
        }
    }
    ipRow->addStretch();

    form->addWidget(fieldLabel(QStringLiteral("\u76EE\u6807IP"), m_paramForm), 0, 0);
    form->addWidget(ipHost, 0, 1);
    form->addWidget(fieldLabel(QStringLiteral("\u76EE\u6807\u7AEF\u53E3"), m_paramForm), 0, 2);
    m_inlinePort = new QSpinBox(m_paramForm);
    m_inlinePort->setObjectName(QStringLiteral("commParamPort"));
    m_inlinePort->setRange(1, 65535);
    m_inlinePort->setFixedWidth(110);
    form->addWidget(m_inlinePort, 0, 3);

    m_inlineDataUpload = new VmSwitch(m_paramForm);
    m_inlineDataUpload->setObjectName(QStringLiteral("commParamDataUpload"));
    m_inlineAutoReconnect = new VmSwitch(m_paramForm);
    m_inlineAutoReconnect->setObjectName(QStringLiteral("commParamAutoReconnect"));
    m_inlineRxTerm = new VmSwitch(m_paramForm);
    m_inlineRxTerm->setObjectName(QStringLiteral("commParamRxTerm"));

    form->addWidget(fieldLabel(QStringLiteral("\u6570\u636E\u4E0A\u4F20"), m_paramForm), 1, 0);
    form->addWidget(m_inlineDataUpload, 1, 1, Qt::AlignLeft);
    form->addWidget(fieldLabel(QStringLiteral("\u81EA\u52A8\u91CD\u8FDE"), m_paramForm), 1, 2);
    form->addWidget(m_inlineAutoReconnect, 1, 3, Qt::AlignLeft);
    form->addWidget(fieldLabel(QStringLiteral("\u63A5\u6536\u7ED3\u675F\u7B26"), m_paramForm), 2, 0);
    form->addWidget(m_inlineRxTerm, 2, 1, Qt::AlignLeft);
    form->setColumnStretch(1, 1);
    m_paramForm->setEnabled(false);   // 未选中设备时不可编辑
    rightLayout->addWidget(m_paramForm);

    for (int i = 0; i < 4; ++i) {
        // editingFinished（回车/失焦）而非 textChanged：逐字符写回会把
        // "127.0.0.1" 编到一半就存成残缺 IP，也会打断输入焦点。
        connect(m_ipEdits[i], &QLineEdit::editingFinished, this,
                &CommunicationManagerDialog::applyInlineParamChange);
    }
    connect(m_inlinePort, &QSpinBox::valueChanged, this,
            [this](int) { applyInlineParamChange(); });
    for (QAbstractButton *sw : { m_inlineDataUpload, m_inlineAutoReconnect, m_inlineRxTerm }) {
        connect(sw, &QAbstractButton::toggled, this,
                [this](bool) { applyInlineParamChange(); });
    }

    // 保留既有 commParamSummary 契约；现在只作一行状态提示，不再是参数正文。
    m_paramSummary = new QLabel(right);
    m_paramSummary->setObjectName(QStringLiteral("commParamSummary"));
    m_paramSummary->setWordWrap(true);
    m_paramSummary->setStyleSheet(QStringLiteral("color: #888888; font-size: 11px;"));
    m_paramSummary->setText(QStringLiteral("\u672A\u9009\u4E2D\u8BBE\u5907"));
    rightLayout->addWidget(m_paramSummary);

    // VM 5.2 官方设备管理页右下：Tab「接收数据 / 发送数据」+ 大数据区 +
    // 「16 进制显示」+「清空」+ 手动发送（内嵌形态此前已补齐）。
    m_monitorPane = new CommMonitorPane(right);
    m_monitorPane->setObjectName(QStringLiteral("commMonitorPane"));
    rightLayout->addWidget(m_monitorPane, 1);

    body->addWidget(left);
    body->addWidget(right, 1);
    pageLayout->addLayout(body, 1);

    // 选中行切换 → 右侧内联参数回显 + 收发区目标设备跟随
    connect(m_deviceTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) { updateParamSummary(); });
    connect(m_controllerTable, &QTableWidget::currentCellChanged, this,
            [this](int, int, int, int) {
        if (m_controllerTable->currentRow() >= 0) {
            m_deviceTable->clearSelection();
            m_deviceTable->setCurrentCell(-1, -1);
        }
        updateParamSummary();
    });

    // VM 5.2.1：设备列表右键 → 重命名 / 删除
    m_deviceTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_deviceTable, &QTableWidget::customContextMenuRequested,
            this, &CommunicationManagerDialog::onDeviceContextMenu);

    // 连接状态实时刷新：自动重连/对端断开/重连成功都会更新表格。
    // 关键：必须**异步合并**刷新——openDevice/closeDevice 会同步 emit
    // deviceConnected/deviceDisconnected，若在信号里同步重建表格，
    // 发起操作的 onToggleConnection 栈上仍持有该行「连接开关」按钮裸指针，
    // 表格重建销毁按钮后再访问即为 use-after-free（点"连接开关"即崩的根因）。
    connect(CommunicationManager::instance(), &CommunicationManager::deviceConnected, this,
            [this](const QString &) { scheduleDeviceTableRefresh(); });
    connect(CommunicationManager::instance(), &CommunicationManager::deviceDisconnected, this,
            [this](const QString &) { scheduleDeviceTableRefresh(); });

    stack->addWidget(tab);
}

// ==================== Controller List Page ====================

void CommunicationManagerDialog::buildControllerSection(QVBoxLayout *layout)
{
    auto *label = new QLabel(QStringLiteral("<b>\u63A7\u5236\u5668\u5217\u8868</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *hint = new QLabel(QStringLiteral(
        "\u5F53\u524D\u63A7\u5236\u5668\u5217\u8868\u7531\u901A\u4FE1\u8BBE\u5907\u6D3E\u751F\uFF1B"
        "\u72EC\u7ACB\u63A7\u5236\u5668\u6A21\u578B\u5F85\u63A5\u5165\u3002"));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    m_controllerTable = new QTableWidget();
    m_controllerTable->setObjectName(QStringLiteral("controllerTable"));   // 供回归测试按名定位
    m_controllerTable->setColumnCount(3);
    m_controllerTable->setHorizontalHeaderLabels({
        QStringLiteral("\u63A7\u5236\u5668"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u72B6\u6001")
    });
    m_controllerTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_controllerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_controllerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);   // 只读
    m_controllerTable->horizontalHeader()->setStretchLastSection(true);
    m_controllerTable->setMaximumHeight(150);
    layout->addWidget(m_controllerTable);

    // VM 5.2.1：控制器列表右键 → 重命名 / 删除（控制器由设备派生，操作落到底层设备）
    m_controllerTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_controllerTable, &QTableWidget::customContextMenuRequested,
            this, &CommunicationManagerDialog::onControllerContextMenu);
}

void CommunicationManagerDialog::refreshDeviceTable()
{
    // 保留当前选中设备：连接状态实时刷新（自动重连/对端断开）时避免选中行跳动
    QString selectedName;
    {
        const int selRow = m_deviceTable->currentRow();
        if (selRow >= 0 && m_deviceTable->item(selRow, 0))
            selectedName = m_deviceTable->item(selRow, 0)->text();
    }
    m_deviceTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &name : cm->deviceNames()) {
        CommDeviceInfo info = cm->deviceInfo(name);
        int row = m_deviceTable->rowCount();
        m_deviceTable->insertRow(row);
        auto *nameItem = new QTableWidgetItem(name);
        nameItem->setData(Qt::UserRole, info.type);   // 卡片委托画第二行小字
        m_deviceTable->setItem(row, 0, nameItem);
        m_deviceTable->setItem(row, 1, new QTableWidgetItem(info.type));

        // 列 2：VM 小滑块开关。仍是 checkable QPushButton，既有回归对
        // cellWidget(0,2) 的 qobject_cast 与 QTest::mouseClick 都不变。
        auto *toggleBtn = new VmSwitch();
        toggleBtn->setChecked(info.isConnected);

        bool isModbusServer = false;
        if (info.type == QStringLiteral("Modbus")) {
            auto *mn = qobject_cast<ModbusNode *>(cm->deviceNode(name));
            if (mn) isModbusServer = (mn->role() == ModbusNode::MODBUS_SERVER);
        }
        if (isModbusServer) {
            toggleBtn->setToolTip(info.isConnected
                ? QStringLiteral("\u670D\u52A1\u5668\u5DF2\u542F\u52A8")
                : QStringLiteral("\u542F\u52A8\u670D\u52A1\u5668"));
        } else {
            toggleBtn->setToolTip(info.isConnected
                ? QStringLiteral("\u5DF2\u8FDE\u63A5\uFF0C\u70B9\u51FB\u65AD\u5F00")
                : QStringLiteral("\u672A\u8FDE\u63A5\uFF0C\u70B9\u51FB\u8FDE\u63A5"));
        }
        connect(toggleBtn, &QPushButton::clicked, this, [this, row]() {
            onToggleConnection(row);
        });
        m_deviceTable->setCellWidget(row, 2, toggleBtn);

        // 列 3: 配置摘要
        QString configStr;
        if (info.config.contains(QStringLiteral("portName")))
            configStr = info.config[QStringLiteral("portName")].toString();
        else if (info.config.contains(QStringLiteral("serverIp")))
            configStr = info.config[QStringLiteral("serverIp")].toString() + QStringLiteral(":") +
                       QString::number(info.config[QStringLiteral("port")].toInt());
        else if (info.config.contains(QStringLiteral("host")))
            configStr = info.config[QStringLiteral("host")].toString();
        m_deviceTable->setItem(row, 3, new QTableWidgetItem(configStr));

        // 列 4: 寄存器实时值（仅 Modbus 显示）
        auto *regValueWidget = new QWidget();
        auto *regLayout = new QHBoxLayout(regValueWidget);
        regLayout->setContentsMargins(2, 2, 2, 2);
        auto *regLabel = new QLabel(QStringLiteral("--"));
        regLabel->setStyleSheet("color: gray; font-size: 11px;");
        regLayout->addWidget(regLabel);
        regLayout->addStretch();
        m_deviceTable->setCellWidget(row, 4, regValueWidget);

        if (info.type == QStringLiteral("Modbus")) {
            auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
            if (modbusNode) {
                // 连接实时寄存器值更新。
                // context 必须是 regLabel（而不是 this）：表格重建会销毁行内 QLabel，
                // 连接随接收者销毁自动断开；若挂在 this 上，每次刷新都会累积一条
                // 指向已销毁 QLabel 的活跃连接（自动重连抖动下每 3s 刷新一次 → 悬空写必崩）
                connect(modbusNode, &ModbusNode::registerCurrentValueChanged,
                        regLabel, [regLabel](int addr, double val, const QString &displayText) {
                    Q_UNUSED(val)
                    QString current = regLabel->text();
                    if (current == QStringLiteral("--")) current.clear();
                    else if (current.length() > 80) current.clear();

                    if (!current.isEmpty()) current += QStringLiteral(" | ");
                    current += QStringLiteral("D%1=%2").arg(addr).arg(displayText);

                    // 仅保留最近几个
                    QStringList parts = current.split(QStringLiteral(" | "));
                    while (parts.size() > 6) parts.removeFirst();
                    regLabel->setText(parts.join(QStringLiteral(" | ")));
                    regLabel->setStyleSheet("color: #0066cc; font-size: 11px;");
                }, Qt::QueuedConnection);
            }
        }
    }
    m_deviceTable->resizeColumnsToContents();
    // 保证连接开关列和寄存器列有合适宽度
    m_deviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_deviceTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);

    // 恢复选中设备
    if (!selectedName.isEmpty()) {
        for (int r = 0; r < m_deviceTable->rowCount(); ++r) {
            if (m_deviceTable->item(r, 0) && m_deviceTable->item(r, 0)->text() == selectedName) {
                m_deviceTable->setCurrentCell(r, 0);
                break;
            }
        }
    }

    // 设备数据是控制器列表的唯一来源：设备表每次重建后同步控制器页
    refreshControllerTable();
}

void CommunicationManagerDialog::refreshControllerTable()
{
    // 建页顺序/析构时可能尚未存在；表不存在时安全返回
    if (!m_controllerTable) return;

    m_controllerTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();
    for (const QString &name : cm->deviceNames()) {
        const CommDeviceInfo info = cm->deviceInfo(name);
        const int row = m_controllerTable->rowCount();
        m_controllerTable->insertRow(row);
        m_controllerTable->setItem(row, 0, new QTableWidgetItem(name));
        m_controllerTable->setItem(row, 1, new QTableWidgetItem(info.type));
        auto *statusItem = new QTableWidgetItem(
            info.isConnected ? QStringLiteral("\u5DF2\u8FDE\u63A5")
                             : QStringLiteral("\u672A\u8FDE\u63A5"));
        statusItem->setForeground(info.isConnected ? QColor(0, 128, 0) : Qt::gray);
        m_controllerTable->setItem(row, 2, statusItem);
    }
    m_controllerTable->resizeColumnsToContents();
    updateParamSummary();
}

void CommunicationManagerDialog::scheduleDeviceTableRefresh()
{
    if (m_deviceTableRefreshPending) return;
    m_deviceTableRefreshPending = true;
    // 0ms 单发：推迟到当前调用栈展开后再重建，且把连续多个状态信号合并为一次刷新
    // （context 为 this：对话框销毁后自动取消，不会打到已析构对象）
    QTimer::singleShot(0, this, [this]() {
        m_deviceTableRefreshPending = false;
        refreshDeviceTable();
    });
}

void CommunicationManagerDialog::onToggleConnection(int row)
{
    if (row < 0 || row >= m_deviceTable->rowCount()) return;
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    CommDeviceInfo info = cm->deviceInfo(name);

    // 判断是否为 Modbus 服务器角色
    bool isModbusServer = false;
    if (info.type == QStringLiteral("Modbus")) {
        auto *mn = qobject_cast<ModbusNode *>(cm->deviceNode(name));
        if (mn) isModbusServer = (mn->role() == ModbusNode::MODBUS_SERVER);
    }
    QString onText = isModbusServer ? QStringLiteral("\u2714 \u670D\u52A1\u5668\u5DF2\u542F\u52A8")
                                    : QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5");
    QString offText = isModbusServer ? QStringLiteral("\u25B6 \u542F\u52A8\u670D\u52A1\u5668")
                                     : QStringLiteral("\u25B6 \u672A\u8FDE\u63A5");

    // 获取该行的开关按钮
    auto *toggleBtn = qobject_cast<QPushButton *>(m_deviceTable->cellWidget(row, 2));
    if (!toggleBtn) return;

    if (info.isConnected) {
        // 当前已连接，点击切换为断开
        cm->closeDevice(name);
        toggleBtn->setChecked(false);
        toggleBtn->setText(offText);
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #f44336; color: white; border: none; "
            "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #da190b; }"
            "QPushButton:checked { background-color: #4CAF50; }");
    } else {
        // 当前未连接，点击切换为连接
        toggleBtn->setText(QStringLiteral("\u8FDE\u63A5\u4E2D..."));
        toggleBtn->setStyleSheet(
            "QPushButton { background-color: #FF9800; color: white; border: none; "
            "border-radius: 4px; padding: 4px 12px; font-weight: bold; }");
        toggleBtn->setEnabled(false);

        bool ok = cm->openDevice(name);
        toggleBtn->setEnabled(true);

        if (ok) {
            toggleBtn->setChecked(true);
            toggleBtn->setText(onText);
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #4CAF50; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #45a049; }");
        } else {
            toggleBtn->setChecked(false);
            toggleBtn->setText(QStringLiteral("\u2716 \u8FDE\u63A5\u5931\u8D25"));
            toggleBtn->setStyleSheet(
                "QPushButton { background-color: #f44336; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:hover { background-color: #da190b; }"
                "QPushButton:checked { background-color: #4CAF50; }");
        }
    }
    // 关键修复（A1 半修收口）：**不得**在此 clicked() 栈内同步重建整张表——
    // 表格重建会销毁正被点击的「连接开关」按钮，是其 clicked() 处理栈上的悬垂指针（UAF，点开关即崩）。
    // 改为异步合并刷新（0ms 单发，上下文为 this：对话框析构后自动取消），与状态信号路径一致。
    scheduleDeviceTableRefresh();
}

void CommunicationManagerDialog::onAddDevice()
{
    // VM4.4（2026-11-13）：官方点「+」只弹一个「设备管理」窗口——
    // 协议类型 + 设备名称 + 通信参数 + 右下「创建」。旧实现是两个连续
    // QInputDialog 再按类型弹四个不同的配置窗，共三段，操作性与官方差别最大。
    // 四个专用配置对话框全部保留给「配置」入口（寄存器表等高级参数）。
    VmDeviceCreateDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString type = dlg.deviceType();
    const QString name = dlg.deviceName();
    if (type.isEmpty() || name.isEmpty()) return;
    const QJsonObject config = dlg.config();

    if (CommunicationManager::instance()->addDevice(name, type, config)) {
        refreshDeviceTable();
    } else {
        QMessageBox::warning(this, QStringLiteral("\u5931\u8D25"),
            QStringLiteral("\u8BBE\u5907\u540D\u79F0\u5DF2\u5B58\u5728"));
    }
}

void CommunicationManagerDialog::onRemoveDevice()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u8981\u5220\u9664\u7684\u8BBE\u5907"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    if (QMessageBox::question(this, QStringLiteral("\u786E\u8BA4"),
        QStringLiteral("\u786E\u5B9A\u8981\u5220\u9664\u8BBE\u5907 %1 \u5417\uFF1F\n"
                       "\u540C\u65F6\u4F1A\u5220\u9664\u5F15\u7528\u8BE5\u8BBE\u5907\u7684\u63A5\u6536/\u53D1\u9001\u4E8B\u4EF6\u4E0E\u5FC3\u8DF3\u914D\u7F6E\uFF0C"
                       "\u5E76\u6E05\u7A7A\u54CD\u5E94\u914D\u7F6E\u91CC\u7684\u8BBE\u5907\u9009\u62E9\u3002").arg(name))
        == QMessageBox::Yes) {
        // true = 用户主动删除，清理悬空引用；改配置时的 remove+add 重建走默认 false
        CommunicationManager::instance()->removeDevice(name, true);
        refreshDeviceTable();
    }
}

void CommunicationManagerDialog::onConfigDevice()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u8981\u914D\u7F6E\u7684\u8BBE\u5907"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    CommDeviceInfo info = cm->deviceInfo(name);
    const bool wasConnected = info.isConnected;   // TCP/串口/UDP 热更新后恢复连接用

    if (info.type == QStringLiteral("Modbus")) {
        auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
        ModbusConfigDialog dlg(QStringLiteral("Modbus\u914D\u7F6E - %1").arg(name),
                               modbusNode, this);
        dlg.setConfig(info.config);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newConfig = dlg.config();
            // N13：断开必须放在 Accepted 分支内——旧实现在弹窗之前就 closeDevice，
            // 用户点"取消"也会白白掉线；且应用后需按原状态恢复，否则删重建后永久离线。
            if (wasConnected) cm->closeDevice(name);
            // 更新设备配置（移除旧的 + 重新创建设备节点）
            cm->removeDevice(name);
            cm->addDevice(name, info.type, newConfig);
            if (wasConnected) cm->openDevice(name);
        }
    } else if (info.type == QStringLiteral("TCP") || info.type == QStringLiteral("UDP")
               || info.type == QStringLiteral("\u4E32\u53E3") || info.type == QStringLiteral("Serial")) {
        // 单页表单 + 热更新：直接改现节点参数后重开连接，不再"删设备→重建"（避免连接被无谓打断）
        const QString formType = (info.type == QStringLiteral("\u4E32\u53E3")
                                  || info.type == QStringLiteral("Serial"))
                                     ? QStringLiteral("Serial") : info.type;
        CommDeviceConfigDialog dlg(formType, info.config, this);
        if (dlg.exec() != QDialog::Accepted) { refreshDeviceTable(); return; }
        const QJsonObject newConfig = dlg.config();
        auto *node = cm->deviceNode(name);
        if (!node) { refreshDeviceTable(); return; }
        if (wasConnected) node->closeConnection();
        for (auto it = newConfig.constBegin(); it != newConfig.constEnd(); ++it)
            node->setParam(it.key(), it.value().toVariant());
        if (wasConnected) node->openConnection();
        cm->updateDeviceConfig(name, newConfig);   // 方案保存时写的是新配置
    } else if (info.type == QStringLiteral("PLC")) {
        auto *plcNode = qobject_cast<PlcCommNode *>(cm->deviceNode(name));
        PlcConfigDialog dlg(QStringLiteral("PLC\u914D\u7F6E - %1").arg(name),
                            plcNode, this);
        dlg.setConfig(info.config);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newConfig = dlg.config();
            // N13：同 Modbus——断开移入 Accepted 分支（取消不掉线），应用后按原状态恢复连接。
            if (wasConnected) cm->closeDevice(name);
            cm->removeDevice(name);
            cm->addDevice(name, info.type, newConfig);
            if (wasConnected) cm->openDevice(name);
        }
    } else if (info.type == QStringLiteral("FINS TCP") || info.type == QStringLiteral("FINS UDP")) {
        // B5-2：与 Modbus/PLC 同款 remove+add（寄存器表长度/节点号都可能变，不能就地改参数）
        FinsConfigDialog dlg(info.type, info.config, this);
        if (dlg.exec() == QDialog::Accepted) {
            QJsonObject newConfig = dlg.config();
            if (wasConnected) cm->closeDevice(name);
            cm->removeDevice(name);
            cm->addDevice(name, info.type, newConfig);
            if (wasConnected) cm->openDevice(name);
        }
    } else {
        QMessageBox::information(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("%1 \u8BBE\u5907\u914D\u7F6E\u4E0D\u652F\u6301\u6B64\u64CD\u4F5C\u3002").arg(info.type));
    }
    refreshDeviceTable();
}

void CommunicationManagerDialog::onReadRegisters()
{
    int row = m_deviceTable->currentRow();
    if (row < 0) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u9009\u62E9\u4E00\u4E2A Modbus \u8BBE\u5907\u3002"));
        return;
    }
    QString name = m_deviceTable->item(row, 0)->text();
    auto *cm = CommunicationManager::instance();
    auto *modbusNode = qobject_cast<ModbusNode *>(cm->deviceNode(name));
    if (!modbusNode) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BE5\u8BBE\u5907\u4E0D\u662F Modbus \u8BBE\u5907\u3002"));
        return;
    }

    if (!modbusNode->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("Modbus \u8BBE\u5907\u672A\u8FDE\u63A5\uFF0C\u8BF7\u5148\u6253\u5F00\u8FDE\u63A5\u3002"));
        return;
    }

    // 打开 ModbusConfigDialog 查询实时值（只读模式）
    CommDeviceInfo info = cm->deviceInfo(name);
    ModbusConfigDialog dlg(QStringLiteral("Modbus\u5BC4\u5B58\u5668\u76D1\u63A7 - %1").arg(name),
                           modbusNode, this);
    dlg.setConfig(info.config);
    dlg.exec();
    // 用户关闭对话框即可
}

// ==================== Receive Event Tab ====================

void CommunicationManagerDialog::setupTriggerTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u89E6\u53D1\u914D\u7F6E\uFF08\u5168\u5C40\u89E6\u53D1\uFF09</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    // 内嵌既有全局触发配置（字符串触发 / 事件触发 → 流程映射），与主菜单入口共用同一管理器
    m_triggerDialog = new GlobalTriggerDialog(tab);
    m_triggerDialog->setObjectName(QStringLiteral("commTriggerConfig"));
    m_triggerDialog->setWindowFlags(Qt::Widget);
    m_triggerDialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_triggerDialog->setMinimumSize(0, 0);
    m_triggerDialog->show();   // setWindowFlags 会隐藏控件，需显式恢复可见
    // 内嵌形态无「关闭」语义：该按钮绑定 accept()，点了会把整页藏掉
    if (auto *closeBtn = m_triggerDialog->findChild<QPushButton *>(
            QStringLiteral("globalTriggerCloseBtn")))
        closeBtn->hide();
    layout->addWidget(m_triggerDialog);

    stack->addWidget(tab);
}

void CommunicationManagerDialog::setupResponseTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u54CD\u5E94\u914D\u7F6E</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);
    auto *hint = new QLabel(QStringLiteral(
        "\u8BBE\u7F6E\u5B8C\u6210\u5373\u751F\u6548\uFF08\u968F\u65B9\u6848\u4FDD\u5B58\uFF09\u3002"
        "\u6D41\u7A0B\u72B6\u6001\u62A5\u6587 = \u5185\u5BB9 + \u5206\u9694\u7B26 + \u6D41\u7A0B\u540D\uFF1B"
        "\u76F8\u673A\u62A5\u6587 = \u5185\u5BB9 + \u5206\u9694\u7B26 + \u76F8\u673A\u540D\u3002"));
    hint->setWordWrap(true);
    hint->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(hint);

    auto makeTermCombo = [this](QWidget *host, const QString &objName, const QString &def) {
        auto *combo = new QComboBox(host);
        combo->setObjectName(objName);
        combo->setEditable(true);
        combo->addItems({QStringLiteral("\\r"), QStringLiteral("\\n"), QStringLiteral("\\r\\n")});
        combo->setCurrentText(def);
        connect(combo, &QComboBox::currentTextChanged, this, [this](const QString &) {
            applyResponseConfig();
        });
        return combo;
    };
    auto makeDeviceCombo = [this](QWidget *host, const QString &objName) {
        auto *combo = new QComboBox(host);
        combo->setObjectName(objName);
        combo->setMinimumWidth(140);
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { applyResponseConfig(); });
        return combo;
    };

    // ── 方案加载 ──
    auto *schemeGroup = new QGroupBox(QStringLiteral("\u65B9\u6848\u52A0\u8F7D"), tab);
    schemeGroup->setObjectName(QStringLiteral("respSchemeLoadGroup"));
    auto *schemeForm = new QFormLayout(schemeGroup);
    m_respSchemeEnable = new QCheckBox(QStringLiteral("\u542F\u7528"), schemeGroup);
    m_respSchemeEnable->setObjectName(QStringLiteral("respSchemeLoadEnable"));
    schemeForm->addRow(m_respSchemeEnable);
    m_respSchemeDevice = makeDeviceCombo(schemeGroup, QStringLiteral("respSchemeLoadDevice"));
    schemeForm->addRow(QStringLiteral("\u901A\u4FE1\u8BBE\u5907"), m_respSchemeDevice);
    m_respSchemeDelay = new QSpinBox(schemeGroup);
    m_respSchemeDelay->setObjectName(QStringLiteral("respSchemeLoadDelay"));
    m_respSchemeDelay->setRange(0, 60);
    m_respSchemeDelay->setSuffix(QStringLiteral(" s"));
    schemeForm->addRow(QStringLiteral("\u901A\u4FE1\u8D85\u65F6\u65F6\u95F4"), m_respSchemeDelay);
    m_respSchemeTerm = makeTermCombo(schemeGroup, QStringLiteral("respSchemeLoadTerm"),
                                     QStringLiteral("\\r\\n"));
    schemeForm->addRow(QStringLiteral("\u7ED3\u675F\u7B26"), m_respSchemeTerm);
    m_respSchemeContent = new QLineEdit(schemeGroup);
    m_respSchemeContent->setObjectName(QStringLiteral("respSchemeLoadContent"));
    m_respSchemeContent->setPlaceholderText(QStringLiteral("\u89E6\u53D1\u5B57\u7B26"));
    schemeForm->addRow(QStringLiteral("\u89E6\u53D1\u5B57\u7B26"), m_respSchemeContent);
    connect(m_respSchemeEnable, &QCheckBox::toggled, this, [this](bool) { applyResponseConfig(); });
    connect(m_respSchemeDelay, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) { applyResponseConfig(); });
    connect(m_respSchemeContent, &QLineEdit::textChanged,
            this, [this](const QString &) { applyResponseConfig(); });
    layout->addWidget(schemeGroup);

    // ── 流程控制 ──
    auto *flowGroup = new QGroupBox(QStringLiteral("\u6D41\u7A0B\u63A7\u5236"), tab);
    flowGroup->setObjectName(QStringLiteral("respFlowGroup"));
    auto *flowForm = new QFormLayout(flowGroup);
    m_respFlowEnable = new QCheckBox(QStringLiteral("\u542F\u7528"), flowGroup);
    m_respFlowEnable->setObjectName(QStringLiteral("respFlowEnable"));
    flowForm->addRow(m_respFlowEnable);
    m_respFlowDevice = makeDeviceCombo(flowGroup, QStringLiteral("respFlowDevice"));
    flowForm->addRow(QStringLiteral("\u901A\u4FE1\u8BBE\u5907"), m_respFlowDevice);
    m_respFlowTerm = makeTermCombo(flowGroup, QStringLiteral("respFlowTerm"), QStringLiteral("\\r\\n"));
    flowForm->addRow(QStringLiteral("\u7ED3\u675F\u7B26"), m_respFlowTerm);
    m_respFlowSep = makeTermCombo(flowGroup, QStringLiteral("respFlowSep"), QStringLiteral(","));
    flowForm->addRow(QStringLiteral("\u5206\u9694\u7B26"), m_respFlowSep);
    m_respFlowIdle = new QLineEdit(flowGroup);
    m_respFlowIdle->setObjectName(QStringLiteral("respFlowIdle"));
    flowForm->addRow(QStringLiteral("\u7A7A\u95F2\u65F6\u89E6\u53D1"), m_respFlowIdle);
    m_respFlowBusy = new QLineEdit(flowGroup);
    m_respFlowBusy->setObjectName(QStringLiteral("respFlowBusy"));
    flowForm->addRow(QStringLiteral("\u5FD9\u788C\u65F6\u89E6\u53D1"), m_respFlowBusy);
    connect(m_respFlowEnable, &QCheckBox::toggled, this, [this](bool) { applyResponseConfig(); });
    connect(m_respFlowIdle, &QLineEdit::textChanged,
            this, [this](const QString &) { applyResponseConfig(); });
    connect(m_respFlowBusy, &QLineEdit::textChanged,
            this, [this](const QString &) { applyResponseConfig(); });
    layout->addWidget(flowGroup);

    // ── 相机响应 ──
    auto *camGroup = new QGroupBox(QStringLiteral("\u76F8\u673A\u54CD\u5E94\u914D\u7F6E"), tab);
    camGroup->setObjectName(QStringLiteral("respCameraGroup"));
    auto *camForm = new QFormLayout(camGroup);
    m_respCameraEnable = new QCheckBox(QStringLiteral("\u542F\u7528"), camGroup);
    m_respCameraEnable->setObjectName(QStringLiteral("respCameraEnable"));
    camForm->addRow(m_respCameraEnable);
    m_respCameraDevice = makeDeviceCombo(camGroup, QStringLiteral("respCameraDevice"));
    camForm->addRow(QStringLiteral("\u901A\u4FE1\u8BBE\u5907"), m_respCameraDevice);
    m_respCameraSep = makeTermCombo(camGroup, QStringLiteral("respCameraSep"), QStringLiteral(","));
    camForm->addRow(QStringLiteral("\u5206\u9694\u7B26"), m_respCameraSep);
    m_respCameraOn = new QLineEdit(camGroup);
    m_respCameraOn->setObjectName(QStringLiteral("respCameraOn"));
    camForm->addRow(QStringLiteral("\u8FDE\u63A5\u65F6\u89E6\u53D1"), m_respCameraOn);
    m_respCameraOff = new QLineEdit(camGroup);
    m_respCameraOff->setObjectName(QStringLiteral("respCameraOff"));
    camForm->addRow(QStringLiteral("\u6389\u7EBF\u65F6\u89E6\u53D1"), m_respCameraOff);
    connect(m_respCameraEnable, &QCheckBox::toggled, this, [this](bool) { applyResponseConfig(); });
    connect(m_respCameraOn, &QLineEdit::textChanged,
            this, [this](const QString &) { applyResponseConfig(); });
    connect(m_respCameraOff, &QLineEdit::textChanged,
            this, [this](const QString &) { applyResponseConfig(); });
    layout->addWidget(camGroup);
    layout->addStretch();

    stack->addWidget(tab);
}

void CommunicationManagerDialog::refreshResponseDeviceCombos()
{
    if (!m_respSchemeDevice) return;   // 建页之前调用：安全的空操作
    auto *cm = CommunicationManager::instance();
    const QStringList devices = cm->deviceNames();
    const QList<QComboBox *> combos{m_respSchemeDevice, m_respFlowDevice, m_respCameraDevice};
    for (QComboBox *combo : combos) {
        const QString keep = combo->currentText();
        QSignalBlocker blocker(combo);   // 重建不下发"用户改动"，避免把空选择写回配置
        combo->clear();
        combo->addItems(devices);
        const int idx = combo->findText(keep);
        if (idx >= 0) combo->setCurrentIndex(idx);
        else if (!devices.isEmpty()) combo->setCurrentIndex(0);
    }
}

void CommunicationManagerDialog::applyResponseConfig()
{
    if (!m_respSchemeEnable || !m_respFlowEnable || !m_respCameraEnable) return;
    auto *cm = CommunicationManager::instance();

    QJsonObject root = cm->responseConfig();
    QJsonObject scheme;
    scheme[QStringLiteral("enabled")] = m_respSchemeEnable->isChecked();
    scheme[QStringLiteral("device")] = m_respSchemeDevice->currentText();
    scheme[QStringLiteral("delaySec")] = m_respSchemeDelay->value();
    scheme[QStringLiteral("terminator")] = m_respSchemeTerm->currentText();
    scheme[QStringLiteral("content")] = m_respSchemeContent->text();
    root[QStringLiteral("schemeLoad")] = scheme;

    QJsonObject flow;
    flow[QStringLiteral("enabled")] = m_respFlowEnable->isChecked();
    flow[QStringLiteral("device")] = m_respFlowDevice->currentText();
    flow[QStringLiteral("terminator")] = m_respFlowTerm->currentText();
    flow[QStringLiteral("separator")] = m_respFlowSep->currentText();
    flow[QStringLiteral("idleContent")] = m_respFlowIdle->text();
    flow[QStringLiteral("busyContent")] = m_respFlowBusy->text();
    root[QStringLiteral("flowControl")] = flow;

    QJsonObject camera;
    camera[QStringLiteral("enabled")] = m_respCameraEnable->isChecked();
    camera[QStringLiteral("device")] = m_respCameraDevice->currentText();
    camera[QStringLiteral("separator")] = m_respCameraSep->currentText();
    camera[QStringLiteral("connectedContent")] = m_respCameraOn->text();
    camera[QStringLiteral("disconnectedContent")] = m_respCameraOff->text();
    root[QStringLiteral("cameraResponse")] = camera;

    cm->setResponseConfig(root);
}

void CommunicationManagerDialog::setupReceiveEventTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u63A5\u6536\u4E8B\u4EF6</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u63A5\u6536\u4E8B\u4EF6\u53EF\u5C06\u901A\u4FE1\u63A5\u6536\u5230\u7684\u6570\u636E\u8FDB\u884C\u89E3\u6790,\u4ECE\u800C\u5C06\u4E00\u6BB5\u6570\u636E\u89E3\u6790\u4E3A\u9700\u8981\u7684\u503C\u3002\n"
        "\u652F\u6301\u4E24\u79CD\u6A21\u5F0F: \u6587\u672C-\u534F\u8BAE\u89E3\u6790(\u6309\u5206\u9694\u7B26\u62C6\u5206) / \u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5(\u5BC4\u5B58\u5668\u503C\u5339\u914D)"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(infoLabel);

    m_receiveEventTable = new QTableWidget();
    m_receiveEventTable->setObjectName(QStringLiteral("receiveEventTable"));   // 供回归测试按名定位
    m_receiveEventTable->setColumnCount(5);
    m_receiveEventTable->setHorizontalHeaderLabels({
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u914D\u7F6E"),
        QStringLiteral("\u72B6\u6001")
    });
    m_receiveEventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_receiveEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_receiveEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_receiveEventTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_receiveEventTable);

    auto *btnLayout = new QHBoxLayout();
    m_addReceiveEventBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u63A5\u6536\u4E8B\u4EF6"));
    m_removeReceiveEventBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    m_editReceiveEventBtn = new QPushButton(QStringLiteral("\u7F16\u8F91"));
    btnLayout->addWidget(m_addReceiveEventBtn);
    btnLayout->addWidget(m_removeReceiveEventBtn);
    btnLayout->addWidget(m_editReceiveEventBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddReceiveEvent);
    connect(m_removeReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveReceiveEvent);
    connect(m_editReceiveEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onEditReceiveEvent);

    // VM 5.2.2：接收事件列表右键 → 重命名 / 删除
    m_receiveEventTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_receiveEventTable, &QTableWidget::customContextMenuRequested,
            this, &CommunicationManagerDialog::onReceiveEventContextMenu);

    stack->addWidget(tab);
}

void CommunicationManagerDialog::refreshReceiveEventTable()
{
    m_receiveEventTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &id : cm->receiveEventIds()) {
        auto *ev = cm->receiveEvent(id);
        if (!ev) continue;

        int row = m_receiveEventTable->rowCount();
        m_receiveEventTable->insertRow(row);
        m_receiveEventTable->setItem(row, 0, new QTableWidgetItem(id));
        m_receiveEventTable->setItem(row, 1, new QTableWidgetItem(
            ev->eventType() == ReceiveEvent::TEXT_PROTOCOL
                ? QStringLiteral("\u6587\u672C-\u534F\u8BAE\u89E3\u6790")
                : QStringLiteral("\u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5")));
        m_receiveEventTable->setItem(row, 2, new QTableWidgetItem(ev->deviceName()));

        QTableWidgetItem *statusItem = new QTableWidgetItem(
            ev->enabled() ? QStringLiteral("\u542F\u7528") : QStringLiteral("\u7981\u7528"));
        statusItem->setForeground(ev->enabled() ? QColor(0, 128, 0) : Qt::gray);
        m_receiveEventTable->setItem(row, 4, statusItem);
    }
    m_receiveEventTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddReceiveEvent()
{
    QStringList types = {
        QStringLiteral("\u6587\u672C-\u534F\u8BAE\u89E3\u6790"),
        QStringLiteral("\u5B57\u8282\u5339\u914D-\u534F\u8BAE\u7EC4\u88C5")
    };
    bool ok = false;
    QString type = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u63A5\u6536\u4E8B\u4EF6"),
        QStringLiteral("\u4E8B\u4EF6\u7C7B\u578B:"),
        types, 0, false, &ok);
    if (!ok) return;

    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907"),
        QStringLiteral("\u7ED1\u5B9A\u901A\u4FE1\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    QString id = QInputDialog::getText(this,
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u4E8B\u4EF6\u540D\u79F0:"),
        QLineEdit::Normal,
        QStringLiteral("Event_") + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000),
        &ok);
    if (!ok || id.trimmed().isEmpty()) return;

    ReceiveEvent *ev = nullptr;
    if (type == types[0]) {
        auto *tev = new TextProtocolReceiveEvent(id.trimmed(), device, CommunicationManager::instance());
        QStringList modes = { QStringLiteral("分隔符拆分"), QStringLiteral("正则表达式") };
        QString mode = QInputDialog::getItem(this, QStringLiteral("解析模式"),
            QStringLiteral("文本解析模式:"), modes, 0, false, &ok);
        if (!ok) return;
        if (mode == modes[1]) {
            QString re = QInputDialog::getText(this, QStringLiteral("正则表达式"),
                QStringLiteral("正则(捕获组作为字段，如 (-?\\d+\\.?\\d*)):"), QLineEdit::Normal,
                QStringLiteral("(-?\\d+\\.?\\d*)"), &ok);
            if (!ok) return;
            tev->setParseMode(TextProtocolReceiveEvent::Regex);
            tev->setRegex(re.trimmed());
        } else {
            QString delim = QInputDialog::getText(this, QStringLiteral("\u5206\u9694\u7B26"),
                QStringLiteral("\u534F\u8BAE\u5206\u9694\u7B26:"), QLineEdit::Normal,
                QStringLiteral(","), &ok);
            if (ok) tev->setDelimiter(delim);
        }
        ev = tev;
    } else {
        auto *bev = new ByteMatchReceiveEvent(id.trimmed(), device, CommunicationManager::instance());
        int addr = QInputDialog::getInt(this, QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740"),
            QStringLiteral("\u5BC4\u5B58\u5668\u5730\u5740:"), 0, 0, 65535, 1, &ok);
        if (ok) bev->setRegisterAddress(addr);
        ByteMatchRule rule;
        rule.byteOffset = 0;
        rule.byteLength = 2;
        ev = bev;
        bev->addRule(rule);
    }

    if (ev) {
        CommunicationManager::instance()->addReceiveEvent(ev);
        refreshReceiveEventTable();
    }
}

void CommunicationManagerDialog::onRemoveReceiveEvent()
{
    int row = m_receiveEventTable->currentRow();
    if (row < 0) return;
    QString id = m_receiveEventTable->item(row, 0)->text();
    CommunicationManager::instance()->removeReceiveEvent(id);
    refreshReceiveEventTable();
}

void CommunicationManagerDialog::onEditReceiveEvent()
{
    int row = m_receiveEventTable->currentRow();
    if (row < 0) return;
    QString id = m_receiveEventTable->item(row, 0)->text();
    auto *ev = CommunicationManager::instance()->receiveEvent(id);
    if (!ev) return;

    // 全参数可编辑（分隔符/正则/字节规则），不再"只能启停、改参数必须删重建"
    ReceiveEventEditDialog dlg(ev, this);
    if (dlg.exec() == QDialog::Accepted)
        refreshReceiveEventTable();
}

// ==================== Send Event Tab ====================

void CommunicationManagerDialog::setupSendEventTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u53D1\u9001\u4E8B\u4EF6</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *tmplInfo = new QLabel(QStringLiteral(
        "发送事件把数据格式化为报文发给外部设备。\n"
        "文本模板支持命名占位符：{模块号.参数名} / {global.变量名}，"
        "例如 \"{1.结果},{global.计数}\"。\n"
        "流程每轮结束会把本轮结果自动填入占位符后上报（对标 VM 的每轮上报）。"));
    tmplInfo->setWordWrap(true);
    tmplInfo->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(tmplInfo);

    m_sendEventTable = new QTableWidget();
    m_sendEventTable->setObjectName(QStringLiteral("sendEventTable"));   // 供回归测试按名定位
    m_sendEventTable->setColumnCount(4);
    m_sendEventTable->setHorizontalHeaderLabels({
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u7C7B\u578B"),
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u72B6\u6001")
    });
    m_sendEventTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_sendEventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_sendEventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sendEventTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_sendEventTable);

    auto *btnLayout = new QHBoxLayout();
    m_addSendEventBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u53D1\u9001\u4E8B\u4EF6"));
    m_removeSendEventBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    m_editSendEventBtn = new QPushButton(QStringLiteral("\u7F16\u8F91"));
    btnLayout->addWidget(m_addSendEventBtn);
    btnLayout->addWidget(m_removeSendEventBtn);
    btnLayout->addWidget(m_editSendEventBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddSendEvent);
    connect(m_removeSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveSendEvent);
    connect(m_editSendEventBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onEditSendEvent);

    // VM 5.2.3：发送事件列表右键 → 重命名 / 删除
    m_sendEventTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sendEventTable, &QTableWidget::customContextMenuRequested,
            this, &CommunicationManagerDialog::onSendEventContextMenu);

    stack->addWidget(tab);
}

void CommunicationManagerDialog::refreshSendEventTable()
{
    m_sendEventTable->setRowCount(0);
    auto *cm = CommunicationManager::instance();

    for (const QString &id : cm->sendEventIds()) {
        auto *ev = cm->sendEvent(id);
        if (!ev) continue;

        int row = m_sendEventTable->rowCount();
        m_sendEventTable->insertRow(row);
        m_sendEventTable->setItem(row, 0, new QTableWidgetItem(id));
        m_sendEventTable->setItem(row, 1, new QTableWidgetItem(
            ev->sendType() == SendEvent::TEXT_DIRECT
                ? QStringLiteral("\u6587\u672C-\u76F4\u63A5\u8F93\u51FA")
                : QStringLiteral("\u5B57\u8282\u7EC4\u5305")));
        m_sendEventTable->setItem(row, 2, new QTableWidgetItem(ev->deviceName()));

        auto *statusItem = new QTableWidgetItem(
            ev->enabled() ? QStringLiteral("\u542F\u7528") : QStringLiteral("\u7981\u7528"));
        statusItem->setForeground(ev->enabled() ? QColor(0, 128, 0) : Qt::gray);
        m_sendEventTable->setItem(row, 3, statusItem);
    }
    m_sendEventTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddSendEvent()
{
    QStringList types = {
        QStringLiteral("\u6587\u672C-\u76F4\u63A5\u8F93\u51FA"),
        QStringLiteral("\u5B57\u8282\u7EC4\u5305")
    };
    bool ok = false;
    QString type = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u53D1\u9001\u4E8B\u4EF6"),
        QStringLiteral("\u4E8B\u4EF6\u7C7B\u578B:"),
        types, 0, false, &ok);
    if (!ok) return;

    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u7ED1\u5B9A\u8BBE\u5907"),
        QStringLiteral("\u7ED1\u5B9A\u901A\u4FE1\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    QString id = QInputDialog::getText(this,
        QStringLiteral("\u4E8B\u4EF6ID"),
        QStringLiteral("\u4E8B\u4EF6\u540D\u79F0:"),
        QLineEdit::Normal,
        QStringLiteral("Send_") + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000),
        &ok);
    if (!ok || id.trimmed().isEmpty()) return;

    SendEvent *ev = nullptr;
    if (type == types[0]) {
        ev = new TextDirectSendEvent(id.trimmed(), device, CommunicationManager::instance());
    } else {
        ev = new BytePackSendEvent(id.trimmed(), device, CommunicationManager::instance());
    }

    if (ev) {
        CommunicationManager::instance()->addSendEvent(ev);
        refreshSendEventTable();
    }
}

void CommunicationManagerDialog::onRemoveSendEvent()
{
    int row = m_sendEventTable->currentRow();
    if (row < 0) return;
    QString id = m_sendEventTable->item(row, 0)->text();
    CommunicationManager::instance()->removeSendEvent(id);
    refreshSendEventTable();
}

void CommunicationManagerDialog::onEditSendEvent()
{
    int row = m_sendEventTable->currentRow();
    if (row < 0) return;
    QString id = m_sendEventTable->item(row, 0)->text();
    auto *ev = CommunicationManager::instance()->sendEvent(id);
    if (!ev) return;

    // 全参数可编辑（模板/后缀/字段表），不再"只能启停、改参数必须删重建"
    SendEventEditDialog dlg(ev, this);
    if (dlg.exec() == QDialog::Accepted)
        refreshSendEventTable();
}

// ==================== Heartbeat Tab ====================

void CommunicationManagerDialog::setupHeartbeatTab(QStackedWidget *stack)
{
    auto *tab = new QWidget();
    auto *layout = new QVBoxLayout(tab);

    auto *label = new QLabel(QStringLiteral("<b>\u5FC3\u8DF3\u7BA1\u7406</b>"));
    label->setStyleSheet("font-size: 13px;");
    layout->addWidget(label);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u5FC3\u8DF3\u7BA1\u7406\u7528\u4E8E\u76D1\u6D4B\u4E0E\u5916\u90E8\u8BBE\u5907\u7684\u8FDE\u63A5\u72B6\u6001\u3002\n"
        "\u7CFB\u7EDF\u5B9A\u65F6\u5411\u5916\u90E8\u8BBE\u5907\u53D1\u9001\u5FC3\u8DF3\u6570\u636E\uFF0C\u91C7\u7528\u4EA4\u66FF\u53D8\u5316\u7684\u6570\u636E\u6A21\u5F0F\u3002"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(infoLabel);

    m_heartbeatTable = new QTableWidget();
    m_heartbeatTable->setObjectName(QStringLiteral("heartbeatTable"));   // 供回归测试按名定位
    m_heartbeatTable->setColumnCount(5);
    m_heartbeatTable->setHorizontalHeaderLabels({
        QStringLiteral("\u8BBE\u5907"),
        QStringLiteral("\u95F4\u9694(ms)"),
        QStringLiteral("\u6A21\u5F0F0"),
        QStringLiteral("\u6A21\u5F0F1"),
        QStringLiteral("\u72B6\u6001")
    });
    m_heartbeatTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_heartbeatTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_heartbeatTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_heartbeatTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_heartbeatTable);

    auto *btnLayout = new QHBoxLayout();
    m_addHeartbeatBtn = new QPushButton(QStringLiteral("\u6DFB\u52A0\u5FC3\u8DF3"));
    m_removeHeartbeatBtn = new QPushButton(QStringLiteral("\u5220\u9664"));
    btnLayout->addWidget(m_addHeartbeatBtn);
    btnLayout->addWidget(m_removeHeartbeatBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    connect(m_addHeartbeatBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onAddHeartbeat);
    connect(m_removeHeartbeatBtn, &QPushButton::clicked, this, &CommunicationManagerDialog::onRemoveHeartbeat);

    stack->addWidget(tab);
}

// ==================== Communication Monitor Page ====================


void CommunicationManagerDialog::refreshHeartbeatTable()
{
    m_heartbeatTable->setRowCount(0);
    auto entries = HeartbeatManager::instance()->entries();
    for (const auto &e : entries) {
        int row = m_heartbeatTable->rowCount();
        m_heartbeatTable->insertRow(row);
        m_heartbeatTable->setItem(row, 0, new QTableWidgetItem(e.deviceName));
        m_heartbeatTable->setItem(row, 1, new QTableWidgetItem(QString::number(e.intervalMs)));
        m_heartbeatTable->setItem(row, 2, new QTableWidgetItem(e.pattern0));
        m_heartbeatTable->setItem(row, 3, new QTableWidgetItem(e.pattern1));

        auto *statusItem = new QTableWidgetItem(
            e.active ? QStringLiteral("\u542F\u52A8") : QStringLiteral("\u505C\u6B62"));
        statusItem->setForeground(e.active ? QColor(0, 128, 0) : Qt::gray);
        m_heartbeatTable->setItem(row, 4, statusItem);
    }
    m_heartbeatTable->resizeColumnsToContents();
}

void CommunicationManagerDialog::onAddHeartbeat()
{
    QStringList devices = CommunicationManager::instance()->deviceNames();
    if (devices.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("\u63D0\u793A"),
            QStringLiteral("\u8BF7\u5148\u6DFB\u52A0\u901A\u4FE1\u8BBE\u5907"));
        return;
    }

    bool ok = false;
    QString device = QInputDialog::getItem(this,
        QStringLiteral("\u6DFB\u52A0\u5FC3\u8DF3"),
        QStringLiteral("\u9009\u62E9\u8BBE\u5907:"),
        devices, 0, false, &ok);
    if (!ok) return;

    int interval = QInputDialog::getInt(this,
        QStringLiteral("\u5FC3\u8DF3\u95F4\u9694"),
        QStringLiteral("\u95F4\u9694(ms):"), 1000, 500, 60000, 100, &ok);
    if (!ok) return;

    HeartbeatManager::instance()->registerHeartbeat(device, interval);
    HeartbeatManager::instance()->startHeartbeat(device);
    refreshHeartbeatTable();
}

void CommunicationManagerDialog::onRemoveHeartbeat()
{
    int row = m_heartbeatTable->currentRow();
    if (row < 0) return;
    QString device = m_heartbeatTable->item(row, 0)->text();
    HeartbeatManager::instance()->unregisterHeartbeat(device);
    refreshHeartbeatTable();
}

// ==================== 右键菜单（VM 5.2：重命名 / 删除） ====================

int CommunicationManagerDialog::execRowContextMenu(QTableWidget *table, const QPoint &pos)
{
    QTableWidgetItem *item = table->itemAt(pos);
    if (!item) return 0;
    table->setCurrentCell(item->row(), 0);

    QMenu menu(this);
    auto *renameAct = menu.addAction(QStringLiteral("\u91CD\u547D\u540D"));
    renameAct->setObjectName(QStringLiteral("commCtxRename"));
    auto *deleteAct = menu.addAction(QStringLiteral("\u5220\u9664"));
    deleteAct->setObjectName(QStringLiteral("commCtxDelete"));
    QAction *chosen = menu.exec(table->viewport()->mapToGlobal(pos));
    if (chosen == renameAct) return 1;
    if (chosen == deleteAct) return 2;
    return 0;
}

void CommunicationManagerDialog::onDeviceContextMenu(const QPoint &pos)
{
    const int choice = execRowContextMenu(m_deviceTable, pos);
    if (choice == 1) {
        const int row = m_deviceTable->currentRow();
        if (row >= 0 && m_deviceTable->item(row, 0))
            promptAndApplyDeviceRename(m_deviceTable->item(row, 0)->text());
    } else if (choice == 2) {
        onRemoveDevice();
    }
}

void CommunicationManagerDialog::onControllerContextMenu(const QPoint &pos)
{
    const int choice = execRowContextMenu(m_controllerTable, pos);
    if (choice == 0) return;
    const int row = m_controllerTable->currentRow();
    if (row < 0 || !m_controllerTable->item(row, 0)) return;
    const QString name = m_controllerTable->item(row, 0)->text();
    if (choice == 1) {
        promptAndApplyDeviceRename(name);
    } else {
        selectDeviceRowByName(name);
        onRemoveDevice();
    }
}

void CommunicationManagerDialog::onReceiveEventContextMenu(const QPoint &pos)
{
    const int choice = execRowContextMenu(m_receiveEventTable, pos);
    if (choice == 1) {
        const int row = m_receiveEventTable->currentRow();
        if (row >= 0 && m_receiveEventTable->item(row, 0))
            promptAndApplyReceiveEventRename(m_receiveEventTable->item(row, 0)->text());
    } else if (choice == 2) {
        onRemoveReceiveEvent();
    }
}

void CommunicationManagerDialog::onSendEventContextMenu(const QPoint &pos)
{
    const int choice = execRowContextMenu(m_sendEventTable, pos);
    if (choice == 1) {
        const int row = m_sendEventTable->currentRow();
        if (row >= 0 && m_sendEventTable->item(row, 0))
            promptAndApplySendEventRename(m_sendEventTable->item(row, 0)->text());
    } else if (choice == 2) {
        onRemoveSendEvent();
    }
}

void CommunicationManagerDialog::selectDeviceRowByName(const QString &name)
{
    for (int r = 0; r < m_deviceTable->rowCount(); ++r) {
        if (m_deviceTable->item(r, 0) && m_deviceTable->item(r, 0)->text() == name) {
            m_deviceTable->setCurrentCell(r, 0);
            return;
        }
    }
}

bool CommunicationManagerDialog::promptAndApplyDeviceRename(const QString &oldName)
{
    bool ok = false;
    const QString newName = QInputDialog::getText(this,
        QStringLiteral("\u91CD\u547D\u540D"),
        QStringLiteral("\u65B0\u540D\u79F0:"),
        QLineEdit::Normal, oldName, &ok);
    if (!ok) return false;
    return applyDeviceRename(oldName, newName);
}

bool CommunicationManagerDialog::promptAndApplyReceiveEventRename(const QString &oldId)
{
    bool ok = false;
    const QString newId = QInputDialog::getText(this,
        QStringLiteral("\u91CD\u547D\u540D"),
        QStringLiteral("\u65B0\u540D\u79F0:"),
        QLineEdit::Normal, oldId, &ok);
    if (!ok) return false;
    return applyReceiveEventRename(oldId, newId);
}

bool CommunicationManagerDialog::promptAndApplySendEventRename(const QString &oldId)
{
    bool ok = false;
    const QString newId = QInputDialog::getText(this,
        QStringLiteral("\u91CD\u547D\u540D"),
        QStringLiteral("\u65B0\u540D\u79F0:"),
        QLineEdit::Normal, oldId, &ok);
    if (!ok) return false;
    return applySendEventRename(oldId, newId);
}

bool CommunicationManagerDialog::applyDeviceRename(const QString &oldName, const QString &newName)
{
    if (!CommunicationManager::instance()->renameDevice(oldName, newName)) {
        QMessageBox::warning(this, QStringLiteral("\u91CD\u547D\u540D"),
            QStringLiteral("\u91CD\u547D\u540D\u5931\u8D25\uFF1A\u540D\u79F0\u4E3A\u7A7A\u3001"
                           "\u4E0E\u539F\u540D\u76F8\u540C\u6216\u65B0\u540D\u79F0\u5DF2\u88AB\u5360\u7528\u3002"));
        return false;
    }
    return true;   // 各表刷新由 deviceRenamed 信号驱动（构造期已接）
}

bool CommunicationManagerDialog::applyReceiveEventRename(const QString &oldId, const QString &newId)
{
    if (!CommunicationManager::instance()->renameReceiveEvent(oldId, newId)) {
        QMessageBox::warning(this, QStringLiteral("\u91CD\u547D\u540D"),
            QStringLiteral("\u91CD\u547D\u540D\u5931\u8D25\uFF1A\u540D\u79F0\u4E3A\u7A7A\u3001"
                           "\u4E0E\u539F\u540D\u76F8\u540C\u6216\u65B0\u540D\u79F0\u5DF2\u88AB\u5360\u7528\u3002"));
        return false;
    }
    return true;
}

bool CommunicationManagerDialog::applySendEventRename(const QString &oldId, const QString &newId)
{
    if (!CommunicationManager::instance()->renameSendEvent(oldId, newId)) {
        QMessageBox::warning(this, QStringLiteral("\u91CD\u547D\u540D"),
            QStringLiteral("\u91CD\u547D\u540D\u5931\u8D25\uFF1A\u540D\u79F0\u4E3A\u7A7A\u3001"
                           "\u4E0E\u539F\u540D\u76F8\u540C\u6216\u65B0\u540D\u79F0\u5DF2\u88AB\u5360\u7528\u3002"));
        return false;
    }
    return true;
}

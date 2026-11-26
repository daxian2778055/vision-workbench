#include "CommResponseManager.h"
#include "CommunicationManager.h"
#include "GlobalCameraManager.h"
#include "AppLog.h"
#include <QTimer>

CommResponseManager *CommResponseManager::instance()
{
    static CommResponseManager *inst = new CommResponseManager();
    return inst;
}

CommResponseManager::CommResponseManager(QObject *parent)
    : QObject(parent)
{
    // 相机响应：全局相机管理器是唯一状态源（连接/掉线边沿各发一次）
    connect(GlobalCameraManager::instance(), &GlobalCameraManager::cameraStatusChanged,
            this, &CommResponseManager::handleCameraState);
}

QByteArray CommResponseManager::terminator(const QJsonObject &section)
{
    return CommunicationManager::decodeEscapes(
        section.value(QStringLiteral("terminator")).toString());
}

void CommResponseManager::sendResponse(const QJsonObject &section, const QByteArray &payload)
{
    const QString device = section.value(QStringLiteral("device")).toString();
    if (device.isEmpty()) {
        VFP_DEBUG << "Response config skipped: empty device";
        return;
    }
    const bool ok = CommunicationManager::instance()->sendData(device, payload);
    if (!ok)
        VFP_DEBUG << "Response config send failed (device missing/disconnected):" << device;
    emit responseSent(device, payload, ok);
}

void CommResponseManager::clearBusyFlows()
{
    m_busyFlows.clear();
}

void CommResponseManager::notifySchemeLoaded()
{
    clearBusyFlows();   // 方案切换：旧流程的 BUSY 边沿不再有效
    const QJsonObject cfg = CommunicationManager::instance()->responseConfig();
    const QJsonObject sec = cfg.value(QStringLiteral("schemeLoad")).toObject();
    if (!sec.value(QStringLiteral("enabled")).toBool())
        return;
    QByteArray payload = sec.value(QStringLiteral("content")).toString().toUtf8()
                         + terminator(sec);
    const int delaySec = qMax(0, sec.value(QStringLiteral("delaySec")).toInt());
    if (delaySec <= 0) {
        sendResponse(sec, payload);
        return;
    }
    // 快速连续切换方案：上一份带延迟的报文必须在下一次 notify 时取消，
    // 否则旧方案的内容会在新方案已生效后才发到 PLC（且新方案还发一份）。
    if (m_schemeLoadTimer) {
        m_schemeLoadTimer->stop();
        m_schemeLoadTimer->deleteLater();
        m_schemeLoadTimer = nullptr;
    }
    // 通信超时时间：方案加载后延迟发送（context=this，管理器随进程常驻）
    m_schemeLoadTimer = new QTimer(this);
    m_schemeLoadTimer->setSingleShot(true);
    QTimer *timer = m_schemeLoadTimer;
    connect(timer, &QTimer::timeout, this, [this, timer, sec, payload]() {
        if (timer != m_schemeLoadTimer)
            return;   // 已被新方案的定时器取代
        m_schemeLoadTimer = nullptr;
        timer->deleteLater();
        sendResponse(sec, payload);
    });
    m_schemeLoadTimer->start(delaySec * 1000);
}

void CommResponseManager::handleFlowState(const QString &flowName, bool busy)
{
    const QJsonObject cfg = CommunicationManager::instance()->responseConfig();
    const QJsonObject sec = cfg.value(QStringLiteral("flowControl")).toObject();
    if (!sec.value(QStringLiteral("enabled")).toBool())
        return;
    // 边沿语义（2026-11-10 审核）：同一个流程重复的同状态不上报。
    // 补全 stopped / error / abandoned 终态后，一次停止可能连发多个终态信号，
    // 不去重会让 PLC 连收多条 IDLE；从没进过忙碌态的终态也不该凭空发 IDLE。
    if (busy) {
        if (m_busyFlows.contains(flowName))
            return;
        m_busyFlows.insert(flowName);
    } else {
        if (!m_busyFlows.remove(flowName))
            return;
    }
    const QString content = sec.value(busy ? QStringLiteral("busyContent")
                                           : QStringLiteral("idleContent")).toString();
    const QString sep = sec.value(QStringLiteral("separator")).toString();
    QByteArray payload = (content + sep + flowName).toUtf8() + terminator(sec);
    sendResponse(sec, payload);
}

void CommResponseManager::handleCameraState(const QString &cameraName, bool open)
{
    const QJsonObject cfg = CommunicationManager::instance()->responseConfig();
    const QJsonObject sec = cfg.value(QStringLiteral("cameraResponse")).toObject();
    if (!sec.value(QStringLiteral("enabled")).toBool())
        return;
    const QString content = sec.value(open ? QStringLiteral("connectedContent")
                                           : QStringLiteral("disconnectedContent")).toString();
    const QString sep = sec.value(QStringLiteral("separator")).toString();
    QByteArray payload = (content + sep + cameraName).toUtf8() + terminator(sec);
    sendResponse(sec, payload);
}

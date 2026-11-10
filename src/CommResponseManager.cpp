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

void CommResponseManager::notifySchemeLoaded()
{
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
    // 通信超时时间：方案加载后延迟发送（context=this，管理器随进程常驻）
    QTimer::singleShot(delaySec * 1000, this, [this, sec, payload]() {
        sendResponse(sec, payload);
    });
}

void CommResponseManager::handleFlowState(const QString &flowName, bool busy)
{
    const QJsonObject cfg = CommunicationManager::instance()->responseConfig();
    const QJsonObject sec = cfg.value(QStringLiteral("flowControl")).toObject();
    if (!sec.value(QStringLiteral("enabled")).toBool())
        return;
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

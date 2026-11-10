#pragma once

#include <QObject>
#include <QJsonObject>
#include <QSet>
#include <QString>

/// VM 5.2.5 响应配置运行期：方案加载 / 流程控制 / 相机响应 → 向指定通信设备发送指定内容。
/// 配置本体由 CommunicationManager::responseConfig() 持有（随方案保存），本类只做事件到报文的组装与发送。
/// 消息规则（手册原文）：流程 = {配置内容}{分隔符}{流程ID}；相机 = {配置内容}{分隔符}{相机ID}。
/// 本工程的流程ID取流程名、相机ID取全局相机名（无独立数字 ID）。
class CommResponseManager : public QObject
{
    Q_OBJECT
public:
    static CommResponseManager *instance();

    /// 方案加载成功（MainWindow 载入完成后调用）：按 schemeLoad 配置延迟发送。
    void notifySchemeLoaded();
    /// 流程状态边沿（executionStarted=忙碌 / executionFinished=空闲）。
    void handleFlowState(const QString &flowName, bool busy);
    /// 相机状态边沿（连接 / 掉线）。
    void handleCameraState(const QString &cameraName, bool open);

signals:
    /// 一条响应已交给 CommunicationManager（ok=false 表示设备不存在/未连接，报文未发出）
    void responseSent(const QString &device, const QByteArray &data, bool ok);

private:
    explicit CommResponseManager(QObject *parent = nullptr);

    /// 读 section["device"] 并发送（空设备/未连接只留痕，不伪造成功）
    void sendResponse(const QJsonObject &section, const QByteArray &payload);
    static QByteArray terminator(const QJsonObject &section);

    /// 当前处于忙碌态的流程名。流程状态是**边沿**语义：补了 stopped/error/abandoned
    /// 终态后，一次停止可能连发多个终态信号，必须去重否则 PLC 收到多条 IDLE。
    QSet<QString> m_busyFlows;
};

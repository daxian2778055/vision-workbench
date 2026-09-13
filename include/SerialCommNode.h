#pragma once

#include "CommunicationNodeBase.h"
#include <QSerialPort>

/// 串口通信节点
class SerialCommNode : public CommunicationNodeBase
{
    Q_OBJECT
public:
    explicit SerialCommNode(QObject *parent = nullptr);

    void init() override;
    void run(bool autoSwitch = true) override;
    bool openConnection() override;
    void closeConnection() override;
    bool isConnected() const override;
    QWidget *createParamPanel() override;
    void updateParamPanel(QWidget *panel) override;

private:
    QSerialPort *m_serial = nullptr;

protected slots:
    void onSendRequested(const QByteArray &data) override;

private slots:
    void onDataReceived();
};

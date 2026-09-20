#include "PlcCommNode.h"
#include "ModbusNode.h"  // ModbusRegisterItem
#include "AppLog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QJsonObject>
#include <QDataStream>

PlcCommNode::PlcCommNode(QObject *parent) : CommunicationNodeBase(parent)
{
    setName(QStringLiteral("PLC\u901A\u4FE1"));
    m_commType = PLC;
    m_type = OUTPUT;
}

void PlcCommNode::init()
{
    CommunicationNodeBase::init();
    m_params[QStringLiteral("plcBrand")] = QStringLiteral("Siemens");
    m_params[QStringLiteral("host")] = QStringLiteral("192.168.0.1");
    m_params[QStringLiteral("port")] = 502;
    m_params[QStringLiteral("slaveAddress")] = 1;
    m_params[QStringLiteral("autoReconnect")] = true;
    m_params[QStringLiteral("reconnectInterval")] = 3000;
    m_params[QStringLiteral("pollInterval")] = 100;

    // 自动重连定时器
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_connected && m_autoReconnect) {
            VFP_DEBUG << "PLC auto-reconnecting...";
            openConnection();
        }
    });

    // 轮询定时器
    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &PlcCommNode::onPollTimeout);
}

bool PlcCommNode::openConnection()
{
    closeConnection();

    m_modbus = new QModbusTcpClient(this);
    if (!m_modbus) return false;

    m_modbus->setConnectionParameter(QModbusDevice::NetworkPortParameter,
                                      m_params.value(QStringLiteral("port"), 502).toInt());
    m_modbus->setConnectionParameter(QModbusDevice::NetworkAddressParameter,
                                      m_params.value(QStringLiteral("host")).toString());

    connect(m_modbus, &QModbusClient::stateChanged,
            this, &PlcCommNode::onModbusStateChanged);

    if (!m_modbus->connectDevice()) {
        m_connected = false;
        setParamDirect(QStringLiteral("connected"), false);
        emit communicationError(QStringLiteral("PLC\u8FDE\u63A5\u5931\u8D25: %1").arg(m_modbus->errorString()));
        if (m_autoReconnect && m_reconnectTimer) {
            m_reconnectTimer->start(m_reconnectInterval);
        }
        return false;
    }

    m_connected = true;
    setParamDirect(QStringLiteral("connected"), true);
    m_slaveAddress = m_params.value(QStringLiteral("slaveAddress"), 1).toInt();
    emit connectionOpened();
    startPolling();
    return true;
}

void PlcCommNode::closeConnection()
{
    stopPolling();
    m_pendingQueue.clear();
    if (m_reconnectTimer) m_reconnectTimer->stop();

    // 只在"确实连过"时上报断开：重复 close / removeDevice 不得发假"断开"（同 TCP/串口/UDP 的抖动修复）
    const bool wasConnected = m_connected;
    if (m_modbus) {
        m_modbus->disconnectDevice();
        m_modbus->deleteLater();
        m_modbus = nullptr;
    }
    m_connected = false;
    setParamDirect(QStringLiteral("connected"), false);   // 参数照旧写（不emit），保持界面数值真实
    if (!wasConnected)
        return;
    emit connectionClosed();
}

bool PlcCommNode::isConnected() const
{
    return m_connected;
}

void PlcCommNode::run(bool /*autoSwitch*/)
{
    // PLC 轮询数据在 onPollTimeout 中异步完成
}

void PlcCommNode::setParam(const QString &name, const QVariant &value)
{
    if (name == QStringLiteral("autoReconnect")) {
        m_autoReconnect = value.toBool();
    } else if (name == QStringLiteral("reconnectInterval")) {
        m_reconnectInterval = qMax(500, value.toInt());
    } else if (name == QStringLiteral("pollInterval")) {
        m_pollInterval = qMax(10, value.toInt());
        if (m_pollTimer) m_pollTimer->setInterval(m_pollInterval);
    } else if (name == QStringLiteral("slaveAddress")) {
        m_slaveAddress = value.toInt();
    }
    CommunicationNodeBase::setParam(name, value);
}

QVariant PlcCommNode::getParam(const QString &name) const
{
    return CommunicationNodeBase::getParam(name);
}

void PlcCommNode::setRegisters(const QList<ModbusRegisterItem> &regs)
{
    m_registers = regs;
    m_currentRegIdx = 0;
}

bool PlcCommNode::writeRegister(int address, quint16 value)
{
    if (!m_connected || !m_modbus) return false;

    QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, address, 1);
    writeUnit.setValue(0, value);

    QModbusReply *reply = m_modbus->sendWriteRequest(writeUnit, m_slaveAddress);
    if (reply) {
        if (!reply->isFinished()) {
            connect(reply, &QModbusReply::finished, this, [this, reply, address]() {
                if (reply->error() != QModbusDevice::NoError) {
                    VFP_DEBUG << "PLC write error at address" << address << ":" << reply->errorString();
                    emit communicationError(
                        QStringLiteral("PLC\u5199\u5BC4\u5B58\u5668\u5931\u8D25(\u5730\u5740%1):%2")
                            .arg(address).arg(reply->errorString()));
                }
                reply->deleteLater();
            });
        } else {
            reply->deleteLater();
        }
        return true;
    }
    return false;
}

double PlcCommNode::registerCurrentValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address) return r.currentValue;
    }
    return 0.0;
}

QString PlcCommNode::registerDisplayValue(int address) const
{
    for (const auto &r : m_registers) {
        if (r.address == address) return r.displayValue;
    }
    return QString();
}

// ---- 轮询 ----

void PlcCommNode::startPolling()
{
    if (!m_pollTimer) return;
    m_currentRegIdx = 0;
    m_pollTimer->setInterval(m_pollInterval);
    m_pollTimer->start();
}

void PlcCommNode::stopPolling()
{
    if (m_pollTimer) m_pollTimer->stop();
}

void PlcCommNode::onPollTimeout()
{
    if (!m_connected || !m_modbus || m_registers.isEmpty()) return;
    if (!m_pendingQueue.isEmpty()) return;

    int startIdx = m_currentRegIdx;
    for (int i = 0; i < m_registers.size(); ++i) {
        int idx = (startIdx + i) % m_registers.size();
        const auto &reg = m_registers[idx];
        if (!reg.enabled) continue;

        int byteCount = 2;
        if (reg.dataType == QStringLiteral("int32") || reg.dataType == QStringLiteral("float"))
            byteCount = 4;

        int regCount = byteCount / 2;

        PendingRead pr;
        pr.slaveAddr = m_slaveAddress;
        pr.regAddr = reg.address;
        pr.count = regCount;
        pr.regIndex = idx;

        m_pendingQueue.append(pr);
        if (!readRegister(m_slaveAddress, reg.address, regCount)) {
            // 请求未能发出（离线/忙）：立刻回滚队列条目，避免 pendingQueue 永久非空
            // 导致轮询入口直接 return（轮询冻结且无报警）
            m_pendingQueue.removeLast();
            VFP_DEBUG << "PLC read dispatch failed at address" << reg.address;
        }
        // 只推进一步（历史缺陷：函数末尾又无条件推进一次 → 偶数个寄存器时一半永不刷新）
        m_currentRegIdx = (idx + 1) % m_registers.size();
        break;
    }
}

bool PlcCommNode::readRegister(int slaveAddr, int regAddr, int count)
{
    if (!m_modbus) return false;

    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, regAddr, count);
    QModbusReply *reply = m_modbus->sendReadRequest(readUnit, slaveAddr);
    if (!reply) {
        // 请求未发出（设备离线/忙）：返回失败，调用方回滚 pendingQueue 条目。
        // 否则该条目永远无人消费 → 轮询入口 !isEmpty 检查让轮询永久冻结且无报警。
        return false;
    }

    auto handler = [this, reply, regAddr]() {
                int regIndex = -1;
                for (int i = 0; i < m_pendingQueue.size(); ++i) {
                    if (m_pendingQueue[i].regAddr == regAddr) {
                        regIndex = m_pendingQueue[i].regIndex;
                        m_pendingQueue.removeAt(i);
                        break;
                    }
                }

                if (reply->error() == QModbusDevice::NoError) {
                    const QModbusDataUnit unit = reply->result();
                    const QVector<quint16> values = unit.values();

                    if (!values.isEmpty() && regIndex >= 0 && regIndex < m_registers.size()) {
                        auto &reg = m_registers[regIndex];

                        QByteArray raw;
                        QDataStream stream(&raw, QIODevice::WriteOnly);
                        stream.setByteOrder(QDataStream::BigEndian);

                        if (reg.byteOrder == QStringLiteral("ABCD") || values.size() == 1) {
                            for (quint16 v : values) stream << v;
                        } else if (reg.byteOrder == QStringLiteral("CDAB") && values.size() >= 2) {
                            stream << values[1] << values[0];
                        } else if (reg.byteOrder == QStringLiteral("BADC") && values.size() >= 2) {
                            for (int i = 0; i < values.size(); ++i) {
                                quint16 swapped = ((values[i] & 0xFF) << 8) | ((values[i] >> 8) & 0xFF);
                                stream << swapped;
                            }
                        } else {
                            for (int i = values.size() - 1; i >= 0; --i) {
                                quint16 swapped = ((values[i] & 0xFF) << 8) | ((values[i] >> 8) & 0xFF);
                                stream << swapped;
                            }
                        }

                        double parsed = parseRawToValue(raw, reg.dataType, reg.byteOrder);
                        reg.currentValue = parsed;
                        reg.displayValue = QString::number(parsed, 'f',
                            reg.dataType == QStringLiteral("float") ? 4 : 0);
                        emit registerCurrentValueChanged(reg.address, parsed, reg.displayValue);

                        if (!reg.hasLastValue || raw != reg.lastRaw) {
                            reg.lastRaw = raw;
                            reg.hasLastValue = true;
                            emit plcRegisterChanged(m_deviceName, reg.address, raw, reg.dataType);
                        }
                    }
                } else {
                    VFP_DEBUG << "PLC read error at address" << regAddr << ":" << reply->errorString();
                }
                reply->deleteLater();
    };
    if (reply->isFinished())
        handler();   // 同步完成（罕见）：必须立即消费，否则 pendingQueue 条目永久滞留
    else
        connect(reply, &QModbusReply::finished, this, handler);
    return true;
}

void PlcCommNode::onModbusStateChanged(int state)
{
    if (state == QModbusDevice::ConnectedState) {
        if (!m_connected) {
            m_connected = true;
            setParamDirect(QStringLiteral("connected"), true);
            emit connectionOpened();
            startPolling();
        }
        if (m_reconnectTimer) m_reconnectTimer->stop();
    } else if (state == QModbusDevice::UnconnectedState) {
        if (m_connected) {
            m_connected = false;
            setParamDirect(QStringLiteral("connected"), false);
            emit connectionClosed();
            stopPolling();
        }
        if (m_autoReconnect && m_reconnectTimer) {
            m_reconnectTimer->start(m_reconnectInterval);
        }
    }
}

double PlcCommNode::parseRawToValue(const QByteArray &raw, const QString &dataType, const QString &byteOrder) const
{
    if (raw.isEmpty()) return 0.0;

    QDataStream::ByteOrder order = QDataStream::BigEndian;
    if (byteOrder == QStringLiteral("DCBA") || byteOrder == QStringLiteral("BADC"))
        order = QDataStream::LittleEndian;

    if (dataType == QStringLiteral("int16")) {
        if (raw.size() < 2) return 0.0;
        qint16 val;
        QDataStream s(raw);
        s.setByteOrder(order);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("uint16")) {
        if (raw.size() < 2) return 0.0;
        quint16 val;
        QDataStream s(raw);
        s.setByteOrder(order);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("int32")) {
        if (raw.size() < 4) return 0.0;
        qint32 val;
        QDataStream s(raw);
        s.setByteOrder(order);
        s >> val;
        return static_cast<double>(val);
    } else if (dataType == QStringLiteral("float")) {
        if (raw.size() < 4) return 0.0;
        float val;
        QDataStream s(raw);
        s.setByteOrder(order);
        s >> val;
        return static_cast<double>(val);
    }
    return 0.0;
}

QJsonObject PlcCommNode::toJson() const
{
    QJsonObject obj = CommunicationNodeBase::toJson();
    obj[QStringLiteral("plcBrand")] = m_params.value(QStringLiteral("plcBrand")).toString();
    obj[QStringLiteral("host")] = m_params.value(QStringLiteral("host")).toString();
    obj[QStringLiteral("port")] = m_params.value(QStringLiteral("port")).toInt();
    obj[QStringLiteral("slaveAddress")] = m_slaveAddress;
    obj[QStringLiteral("autoReconnect")] = m_autoReconnect;
    obj[QStringLiteral("reconnectInterval")] = m_reconnectInterval;
    obj[QStringLiteral("pollInterval")] = m_pollInterval;

    QJsonArray regsArr;
    for (const auto &r : m_registers) {
        QJsonObject ro;
        ro[QStringLiteral("address")] = r.address;
        ro[QStringLiteral("dataType")] = r.dataType;
        ro[QStringLiteral("byteOrder")] = r.byteOrder;
        ro[QStringLiteral("accessMode")] = r.accessMode;
        ro[QStringLiteral("enabled")] = r.enabled;
        ro[QStringLiteral("description")] = r.description;
        regsArr.append(ro);
    }
    obj[QStringLiteral("registers")] = regsArr;
    return obj;
}

void PlcCommNode::fromJson(const QJsonObject &json)
{
    CommunicationNodeBase::fromJson(json);
    if (json.contains(QStringLiteral("plcBrand")))
        m_params[QStringLiteral("plcBrand")] = json[QStringLiteral("plcBrand")].toString();
    if (json.contains(QStringLiteral("host")))
        m_params[QStringLiteral("host")] = json[QStringLiteral("host")].toString();
    if (json.contains(QStringLiteral("port")))
        m_params[QStringLiteral("port")] = json[QStringLiteral("port")].toInt();
    m_autoReconnect = json[QStringLiteral("autoReconnect")].toBool(true);
    m_reconnectInterval = json[QStringLiteral("reconnectInterval")].toInt(3000);
    m_pollInterval = json[QStringLiteral("pollInterval")].toInt(100);
    m_slaveAddress = json[QStringLiteral("slaveAddress")].toInt(1);

    m_registers.clear();
    QJsonArray regsArr = json[QStringLiteral("registers")].toArray();
    for (const auto &v : regsArr) {
        QJsonObject ro = v.toObject();
        ModbusRegisterItem r;
        r.address = ro[QStringLiteral("address")].toInt();
        r.dataType = ro[QStringLiteral("dataType")].toString(QStringLiteral("int16"));
        r.byteOrder = ro[QStringLiteral("byteOrder")].toString(QStringLiteral("ABCD"));
        r.accessMode = ro[QStringLiteral("accessMode")].toString(QStringLiteral("Read"));
        r.enabled = ro[QStringLiteral("enabled")].toBool(true);
        r.description = ro[QStringLiteral("description")].toString();
        m_registers.append(r);
    }

    m_params[QStringLiteral("autoReconnect")] = m_autoReconnect;
    m_params[QStringLiteral("reconnectInterval")] = m_reconnectInterval;
    m_params[QStringLiteral("pollInterval")] = m_pollInterval;
    m_params[QStringLiteral("slaveAddress")] = m_slaveAddress;
}

QWidget *PlcCommNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>PLC \u901A\u4FE1</b>")));

    // PLC 品牌
    layout->addWidget(new QLabel(QStringLiteral("\u54C1\u724C:")));
    auto *brandCombo = new QComboBox();
    brandCombo->setObjectName(QStringLiteral("plcBrand"));
    brandCombo->addItems({QStringLiteral("Siemens"), QStringLiteral("Mitsubishi"),
                          QStringLiteral("Omron"), QStringLiteral("Keyence"),
                          QStringLiteral("Panasonic"), QStringLiteral("\u901A\u7528")});
    brandCombo->setCurrentText(m_params.value(QStringLiteral("plcBrand")).toString());
    connect(brandCombo, &QComboBox::currentTextChanged, this, [this](const QString &b) {
        setParam(QStringLiteral("plcBrand"), b);
    });
    layout->addWidget(brandCombo);

    // IP 地址
    layout->addWidget(new QLabel(QStringLiteral("\u4E3B\u673A\u5730\u5740:")));
    auto *ipEdit = new QLineEdit();
    ipEdit->setObjectName(QStringLiteral("plcIp"));
    ipEdit->setText(m_params.value(QStringLiteral("host")).toString());
    connect(ipEdit, &QLineEdit::editingFinished, this, [this, ipEdit]() {
        setParam(QStringLiteral("host"), ipEdit->text());
    });
    layout->addWidget(ipEdit);

    // 端口
    layout->addWidget(new QLabel(QStringLiteral("\u7AEF\u53E3:")));
    auto *portSpin = new QSpinBox();
    portSpin->setObjectName(QStringLiteral("plcPort"));
    portSpin->setRange(1, 65535);
    portSpin->setValue(m_params.value(QStringLiteral("port"), 502).toInt());
    connect(portSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int p) {
        setParam(QStringLiteral("port"), p);
    });
    layout->addWidget(portSpin);

    // 从站地址
    layout->addWidget(new QLabel(QStringLiteral("\u4ECE\u7AD9\u5730\u5740:")));
    auto *slaveSpin = new QSpinBox();
    slaveSpin->setObjectName(QStringLiteral("plcSlave"));
    slaveSpin->setRange(1, 247);
    slaveSpin->setValue(m_params.value(QStringLiteral("slaveAddress"), 1).toInt());
    connect(slaveSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        setParam(QStringLiteral("slaveAddress"), v);
    });
    layout->addWidget(slaveSpin);

    // 连接切换开关
    auto *toggleBtn = new QPushButton();
    toggleBtn->setObjectName(QStringLiteral("plcToggle"));
    toggleBtn->setCheckable(true);
    toggleBtn->setMinimumHeight(30);
    connect(toggleBtn, &QPushButton::clicked, this, [this, toggleBtn]() {
        if (m_connected) {
            closeConnection();
        } else {
            openConnection();
        }
        updateParamPanel(toggleBtn->parentWidget() ? toggleBtn->parentWidget() : nullptr);
    });
    layout->addWidget(toggleBtn);

    auto *infoLabel = new QLabel(QStringLiteral(
        "\u8BF4\u660E: PLC \u901A\u8FC7 Modbus TCP \u534F\u8BAE\u8FDE\u63A5\uFF0C\u901A\u7528\u4E8E\u652F\u6301 "
        "Modbus TCP \u7684\u5404\u54C1\u724C PLC\u3002\n"
        "\u5BC4\u5B58\u5668\u914D\u7F6E\u8BF7\u5728\u5168\u5C40\u901A\u4FE1\u7BA1\u7406\u7684\u8BBE\u5907\u914D\u7F6E\u4E2D\u5B8C\u6210\u3002"));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(infoLabel);

    layout->addStretch();
    updateParamPanel(panel);
    return panel;
}

void PlcCommNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("plcBrand"))) {
        QSignalBlocker b(cb);
        cb->setCurrentText(m_params.value(QStringLiteral("plcBrand")).toString());
    }
    if (auto *le = panel->findChild<QLineEdit *>(QStringLiteral("plcIp"))) {
        QSignalBlocker b(le);
        le->setText(m_params.value(QStringLiteral("host")).toString());
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("plcPort"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_params.value(QStringLiteral("port"), 502).toInt());
    }
    if (auto *sp = panel->findChild<QSpinBox *>(QStringLiteral("plcSlave"))) {
        QSignalBlocker b(sp);
        sp->setValue(m_params.value(QStringLiteral("slaveAddress"), 1).toInt());
    }
    if (auto *btn = panel->findChild<QPushButton *>(QStringLiteral("plcToggle"))) {
        btn->setChecked(m_connected);
        if (m_connected) {
            btn->setText(QStringLiteral("\u2714 \u5DF2\u8FDE\u63A5"));
            btn->setStyleSheet(
                "QPushButton { background-color: #4CAF50; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }");
        } else {
            btn->setText(QStringLiteral("\u25B6 \u8FDE\u63A5 PLC"));
            btn->setStyleSheet(
                "QPushButton { background-color: #f44336; color: white; border: none; "
                "border-radius: 4px; padding: 4px 12px; font-weight: bold; }"
                "QPushButton:checked { background-color: #4CAF50; }");
        }
    }
}

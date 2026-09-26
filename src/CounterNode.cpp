#include "CounterNode.h"
#include "DataObject.h"
#include "Port.h"
#include "PortDataType.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QSignalBlocker>

CounterNode::CounterNode(QObject *parent)
    : HalconNode(parent)
{
    setName(QStringLiteral("\u6761\u4EF6\u8BA1\u6570"));
    m_type = LOGIC;
}

void CounterNode::init()
{
    addInputPort(QStringLiteral("\u6761\u4EF6"), PortDataType::Number);
    addOutputPort(QStringLiteral("\u7D2F\u8BA1\u6B21\u6570"), PortDataType::Number);

    m_params[QStringLiteral("conditionMode")] = QStringLiteral("bool");   // bool / number
    m_params[QStringLiteral("threshold")] = 0.0;
}

bool CounterNode::process()
{
    // W-2：空载不得绿灯——结果必须由「条件」端口算出，统一走基类数据端口契约
    return processDataOutputs();
}

void CounterNode::run(bool /*autoSwitch*/)
{
    // 参数唯一来源：本轮取一次（局部快照）——避免逐次加锁，并保证同一轮内判据一致
    const QString conditionMode = getParam(QStringLiteral("conditionMode")).toString();
    const double threshold = getParam(QStringLiteral("threshold")).toDouble();

    bool condition = false;
    auto data = getInputData(0);
    if (data) {
        QVariant var = data->getData();
        if (conditionMode == QStringLiteral("number")) {
            bool ok = false;
            double v = var.toDouble(&ok);
            condition = ok && (v >= threshold);
        } else {
            // bool 模式：输入布尔或非零数值
            condition = var.toBool();
            if (var.canConvert<double>() && var.typeId() != QMetaType::Bool) {
                bool ok = false;
                double v = var.toDouble(&ok);
                if (ok) condition = v != 0.0;
            }
        }
    }

    if (condition) {
        m_count.fetchAndAddRelaxed(1);   // 运行期状态：原子自增（口径见头文件）
    }

    auto obj = QSharedPointer<DataObject>::create(DataObject::DataType::Number, QVariant(count()));
    setOutputData(0, obj);
}

void CounterNode::setParam(const QString &name, const QVariant &value)
{
    // 只写参数表（基类加锁 + 校验），不再维护无锁成员镜像
    HalconNode::setParam(name, value);
}

QVariant CounterNode::getParam(const QString &name) const
{
    return HalconNode::getParam(name);
}

QWidget *CounterNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6761\u4EF6\u8BA1\u6570</b>")));
    layout->addWidget(new QLabel(QStringLiteral("\u6761\u4EF6\u6A21\u5F0F:")));

    m_modeCombo = new QComboBox();
    m_modeCombo->setObjectName(QStringLiteral("counterMode"));
    m_modeCombo->addItem(QStringLiteral("bool (\u8F93\u5165\u975E\u96F6\u5373\u8BA1\u6570)"), QStringLiteral("bool"));
    m_modeCombo->addItem(QStringLiteral("number (\u8F93\u5165\u2265\u9608\u503C\u5373\u8BA1\u6570)"), QStringLiteral("number"));
    int idx = getParam(QStringLiteral("conditionMode")).toString() == QStringLiteral("number") ? 1 : 0;
    m_modeCombo->setCurrentIndex(idx);
    layout->addWidget(m_modeCombo);

    layout->addWidget(new QLabel(QStringLiteral("\u9608\u503C:")));
    m_thresholdSpin = new QDoubleSpinBox();
    m_thresholdSpin->setObjectName(QStringLiteral("counterThreshold"));
    m_thresholdSpin->setRange(-1e12, 1e12);
    m_thresholdSpin->setDecimals(6);
    m_thresholdSpin->setValue(getParam(QStringLiteral("threshold")).toDouble());
    layout->addWidget(m_thresholdSpin);

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        setParam(QStringLiteral("conditionMode"), m_modeCombo->itemData(i).toString());
    });
    connect(m_thresholdSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        setParam(QStringLiteral("threshold"), v);
    });

    layout->addStretch();
    return panel;
}

void CounterNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *combo = panel->findChild<QComboBox *>(QStringLiteral("counterMode"))) {
        QSignalBlocker b(combo);
        combo->setCurrentIndex(
            getParam(QStringLiteral("conditionMode")).toString() == QStringLiteral("number") ? 1 : 0);
    }
    if (auto *spin = panel->findChild<QDoubleSpinBox *>(QStringLiteral("counterThreshold"))) {
        QSignalBlocker b(spin);
        spin->setValue(getParam(QStringLiteral("threshold")).toDouble());
    }
}

QJsonObject CounterNode::toJson() const
{
    QJsonObject obj = HalconNode::toJson();
    // 顶层键保留（老读取方兼容），值一律取自参数表（唯一来源）
    obj[QStringLiteral("conditionMode")] =
        QJsonValue::fromVariant(getParam(QStringLiteral("conditionMode")));
    obj[QStringLiteral("threshold")] = QJsonValue::fromVariant(getParam(QStringLiteral("threshold")));
    return obj;
}

void CounterNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);   // conditionMode/threshold 由基类从 params 恢复（唯一来源）
    // 兼容更早方案：这两个键曾只存在顶层（无 params 段）
    if (!json.contains(QStringLiteral("params"))) {
        if (json.contains(QStringLiteral("conditionMode")))
            setParam(QStringLiteral("conditionMode"),
                     json.value(QStringLiteral("conditionMode")).toVariant());
        if (json.contains(QStringLiteral("threshold")))
            setParam(QStringLiteral("threshold"),
                     json.value(QStringLiteral("threshold")).toVariant());
    }
}

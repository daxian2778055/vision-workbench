#include "ChannelMergeNode.h"
#include "Port.h"
#include "DataObject.h"
#include "AppLog.h"
#include <halconcpp/HalconCpp.h>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QJsonObject>
#include <QSignalBlocker>

using namespace HalconCpp;

ChannelMergeNode::ChannelMergeNode(QObject *parent)
    : HalconNode(parent)
{
}

ChannelMergeNode::~ChannelMergeNode() = default;

void ChannelMergeNode::init()
{
    setName(QStringLiteral("通道合并"));
    m_type = IMAGE_PROCESSING;

    // B2 契约：画布上最多 1 个可见输入点。第 2..4 通道走逻辑槽（inputSlot），
    // 由 setInputBinding 在执行前注入数据，下游用 m_inputData[1..3] 统一读取。
    addInputPort(QStringLiteral("通道 1"));
    addInputSlot(QStringLiteral("通道 2"), PortDataType::Image);
    addInputSlot(QStringLiteral("通道 3"), PortDataType::Image);
    addInputSlot(QStringLiteral("通道 4"), PortDataType::Image);

    addOutputPort(QStringLiteral("输出图像"), PortDataType::Image);

    m_params["moduleStatus"] = false;
}

bool ChannelMergeNode::process()
{
    try {
        // 收集有效输入通道
        QVector<HImage> channels;
        for (int i = 0; i < kMaxChannels; ++i) {
            if (m_inputData.contains(i) && m_inputData[i]) {
                HImage img = m_inputData[i]->getHImage();
                if (img.IsInitialized()) {
                    channels.append(img);
                }
            }
        }

        if (channels.size() < 2) {
            VFP_DEBUG << "ChannelMergeNode: need at least 2 channels, got" << channels.size();
            return false;
        }

        // HALCON 24.11：ComposeN 是 HImage 成员函数，`this` 即第 1 通道
        HImage merged;
        switch (channels.size()) {
        case 2:
            merged = channels[0].Compose2(channels[1]);
            break;
        case 3:
            merged = channels[0].Compose3(channels[1], channels[2]);
            break;
        case 4:
            merged = channels[0].Compose4(channels[1], channels[2], channels[3]);
            break;
        default:
            return false;
        }

        auto outObj = QSharedPointer<DataObject>(new DataObject());
        outObj->setHImage(merged);
        m_outputData[0] = outObj;
        m_outputImage = (HObject)merged;

        m_params["moduleStatus"] = true;
        return true;
    } catch (HOperatorException &e) {
        VFP_DEBUG << "ChannelMergeNode HALCON error:" << e.ErrorMessage().Text();
        m_params["moduleStatus"] = false;
        return false;
    } catch (std::exception &e) {
        VFP_DEBUG << "ChannelMergeNode error:" << e.what();
        m_params["moduleStatus"] = false;
        return false;
    } catch (...) {
        m_params["moduleStatus"] = false;
        return false;
    }
}

bool ChannelMergeNode::execute()
{
    return HalconNode::execute();
}

QJsonObject ChannelMergeNode::toJson() const
{
    QJsonObject json = HalconNode::toJson();
    json["executionSuccess"] = m_executionSuccess.load(std::memory_order_relaxed);
    return json;
}

void ChannelMergeNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    if (json.contains("executionSuccess")) {
        m_executionSuccess = json["executionSuccess"].toBool();
    }
}

QWidget *ChannelMergeNode::createParamPanel()
{
    QWidget *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    auto *info = new QLabel(QStringLiteral("接 2–4 个单通道输入，合并为多通道图像。\n"
                                           "通道 1 接画布输入点；通道 2/3/4 通过逻辑槽绑定接入，未绑定的通道自动跳过。"));
    info->setWordWrap(true);
    info->setStyleSheet("color: gray; font-size: 11px;");
    layout->addWidget(info);
    return panel;
}

void ChannelMergeNode::updateParamPanel(QWidget *)
{
}

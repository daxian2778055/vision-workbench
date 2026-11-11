#include "ChannelSplitNode.h"
#include "Port.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <halconcpp/HalconCpp.h>
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QJsonObject>
#include <QSignalBlocker>
#include <QPointer>
#include <QTimer>

using namespace HalconCpp;

ChannelSplitNode::ChannelSplitNode(QObject *parent)
    : HalconNode(parent)
{
}

ChannelSplitNode::~ChannelSplitNode() = default;

void ChannelSplitNode::init()
{
    setName(QStringLiteral("通道拆分"));
    m_type = IMAGE_PROCESSING;

    addInputPort(QStringLiteral("输入图像"));

    // 主端口（通道 1）+ 3 个逻辑子输出（通道 2/3/4）
    addOutputPort(QStringLiteral("通道 1"), PortDataType::Image);
    addOutputPort(QStringLiteral("通道 2"), PortDataType::Image);
    addOutputPort(QStringLiteral("通道 3"), PortDataType::Image);
    addOutputPort(QStringLiteral("通道 4"), PortDataType::Image);

    m_params["colorSpace"] = 0;
    m_params["moduleStatus"] = false;

    registerParams({
        makeEnumParam(QStringLiteral("colorSpace"), 0,
                      {QStringLiteral("RGB（红/绿/蓝）"), QStringLiteral("HSV（色相/饱和度/明度）")},
                      QStringLiteral("色彩空间"))
    });
}

bool ChannelSplitNode::process()
{
    try {
        HImage inputImage;
        bool valid = false;

        if (m_inputData.contains(0) && m_inputData[0]) {
            inputImage = m_inputData[0]->getHImage();
            valid = inputImage.IsInitialized();
        }
        if (!valid) {
            VFP_DEBUG << "ChannelSplitNode: no valid input image";
            return false;
        }

        HTuple chTuple;
        CountChannels(inputImage, &chTuple);
        const int ch = chTuple.I();

        const int colorSpace = getParam(QStringLiteral("colorSpace")).toInt();

        // HSV 模式用 OpenCV（HALCON TransFromRgb 的 H 通道有已知计算偏差）
        HImage workImage = inputImage;
        if (colorSpace == 1 && ch >= 3) {
            cv::Mat src = OpencvUtil::himageToMat(inputImage);
            if (!src.empty() && src.channels() >= 3) {
                cv::Mat hsv;
                cv::cvtColor(src, hsv, cv::COLOR_BGR2HSV_FULL);
                workImage = OpencvUtil::matToHimage(hsv);
            }
        }

        // 逐通道提取（HALCON AccessChannel 为 1-based）
        for (int i = 0; i < ch && i < kMaxChannels; ++i) {
            HImage channel = workImage.AccessChannel(i + 1);
            auto obj = QSharedPointer<DataObject>(new DataObject());
            obj->setHImage(channel);
            m_outputData[i] = obj;
        }

        // 清空未使用端口
        for (int i = ch; i < kMaxChannels; ++i) {
            m_outputData.remove(i);
        }

        m_params["moduleStatus"] = true;
        return true;
    } catch (HOperatorException &e) {
        VFP_DEBUG << "ChannelSplitNode HALCON error:" << e.ErrorMessage().Text();
        m_params["moduleStatus"] = false;
        return false;
    } catch (std::exception &e) {
        VFP_DEBUG << "ChannelSplitNode error:" << e.what();
        m_params["moduleStatus"] = false;
        return false;
    } catch (...) {
        m_params["moduleStatus"] = false;
        return false;
    }
}

bool ChannelSplitNode::execute()
{
    return HalconNode::execute();
}

QJsonObject ChannelSplitNode::toJson() const
{
    QJsonObject json = HalconNode::toJson();
    json["colorSpace"] = QJsonValue::fromVariant(getParam(QStringLiteral("colorSpace")));
    json["executionSuccess"] = m_executionSuccess.load(std::memory_order_relaxed);
    return json;
}

void ChannelSplitNode::fromJson(const QJsonObject &json)
{
    HalconNode::fromJson(json);
    if (!json.contains(QStringLiteral("params")) && json.contains("colorSpace")) {
        setParam(QStringLiteral("colorSpace"), json.value("colorSpace").toVariant());
    }
    if (json.contains("executionSuccess")) {
        m_executionSuccess = json["executionSuccess"].toBool();
    }
}

QWidget *ChannelSplitNode::createParamPanel()
{
    QWidget *panel = new QWidget();
    auto *layout = new QFormLayout(panel);

    auto *combo = new QComboBox();
    combo->setObjectName(QStringLiteral("colorSpace"));
    combo->addItem(QStringLiteral("RGB（红/绿/蓝）"));
    combo->addItem(QStringLiteral("HSV（色相/饱和度/明度）"));
    combo->setCurrentIndex(getParam(QStringLiteral("colorSpace")).toInt());

    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        setParam(QStringLiteral("colorSpace"), idx);
    });

    layout->addRow(QStringLiteral("色彩空间:"), combo);
    return panel;
}

void ChannelSplitNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    auto *combo = panel->findChild<QComboBox *>(QStringLiteral("colorSpace"));
    if (combo) {
        QSignalBlocker blocker(combo);
        combo->setCurrentIndex(getParam(QStringLiteral("colorSpace")).toInt());
    }
}

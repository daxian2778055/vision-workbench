#include "OpencvQrNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <QWidget>

OpencvQrNode::OpencvQrNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("二维码解码"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvQrNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("解码内容"), PortDataType::String);
    registerParams({
        makeIntParam(QStringLiteral("minMargin"), 0, 0, 100,
                     QStringLiteral("QR 最小边距（像素）")),
    });
    m_params[QStringLiteral("qrText")] = QString();
    m_params[QStringLiteral("decodeCount")] = 0;
}

void OpencvQrNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3) {
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);
        }
        const int margin = m_params.value(QStringLiteral("minMargin"), 0).toInt();
        cv::QRCodeDetector qr;
        if (margin > 0) {
            qr.setEpsX(margin * 0.01);
            qr.setEpsY(margin * 0.01);
        }

        std::vector<cv::Point> points;
        std::string text = qr.detectAndDecode(gray, points);
        // 小图检测器常失败：放大 4 倍重试（QR 为二值图，最近邻放大不失真）
        if (text.empty() && gray.cols < 200) {
            cv::Mat big;
            cv::resize(gray, big, cv::Size(gray.cols * 4, gray.rows * 4), 0, 0, cv::INTER_NEAREST);
            text = qr.detectAndDecode(big, points);
            if (!text.empty()) {
                for (auto &p : points) {
                    p.x /= 4;
                    p.y /= 4;
                }
            }
        }
        const bool ok = !text.empty();

        m_params[QStringLiteral("qrText")] = QString::fromStdString(text);
        m_params["moduleStatus"] = ok;
        m_outputImage = m_inputImage;

        // 输出解码内容
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QString::fromStdString(text));
        setOutputData(1, strObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvQrNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvQrNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvQrNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

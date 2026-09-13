#include "OpencvImageArithNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvImageArithNode::OpencvImageArithNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("图像运算"));
    m_type = IMAGE_PROCESSING;
}

void OpencvImageArithNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("结果图"), PortDataType::Image);
    registerParams({
        makeEnumParam(QStringLiteral("op"), 0, QStringList{"加法", "减法", "乘法", "除法"},
                      QStringLiteral("运算类型")),
        makeEnumParam(QStringLiteral("operandType"), 0, QStringList{"常数", "图像"},
                      QStringLiteral("运算对象")),
        makeDoubleParam(QStringLiteral("constant"), 0.0, -1000.0, 1000.0,
                        QStringLiteral("常数（operandType=常数 时有效）")),
    });
    m_params[QStringLiteral("moduleStatus")] = true;
}

void OpencvImageArithNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat a = OpencvUtil::himageToMat(input);
        if (a.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (a.channels() == 3) cv::cvtColor(a, a, cv::COLOR_BGR2GRAY);

        const int op = m_params.value(QStringLiteral("op"), 0).toInt();
        const int operandType = m_params.value(QStringLiteral("operandType"), 0).toInt();
        const double c = m_params.value(QStringLiteral("constant"), 0.0).toDouble();

        cv::Mat b;
        if (operandType == 1) {
            // 图像运算对象：取端口 1（第二输入图像）
            if (m_inputData.contains(1) && m_inputData[1]) {
                b = OpencvUtil::himageToMat(m_inputData[1]->getHImage());
                if (b.empty() || b.channels() == 3) {
                    if (!b.empty()) cv::cvtColor(b, b, cv::COLOR_BGR2GRAY);
                }
            } else {
                m_params[QStringLiteral("moduleStatus")] = false;
                return;
            }
        }

        cv::Mat out;
        switch (op) {
        case 0:  // 加
            if (operandType == 1) cv::add(a, b, out);
            else { cv::Mat k = cv::Mat(a.size(), a.type(), cv::Scalar(c)); cv::add(a, k, out); }
            break;
        case 1:  // 减
            if (operandType == 1) cv::subtract(a, b, out);
            else { cv::Mat k = cv::Mat(a.size(), a.type(), cv::Scalar(c)); cv::subtract(a, k, out); }
            break;
        case 2:  // 乘
            if (operandType == 1) cv::multiply(a, b, out);
            else cv::multiply(a, c, out);
            break;
        case 3:  // 除
            if (operandType == 1) {
                // 图像除图像：检查除数是否有零值
                cv::Mat divisor = b.clone();
                // 将零值替换为 1 避免除零，结果图中这些像素会是 255（饱和）
                cv::Mat mask = (divisor == 0);
                divisor.setTo(1, mask);
                cv::divide(a, divisor, out);
            } else {
                // 常数除法：检查常数是否为零
                if (qFuzzyIsNull(c)) {
                    VFP_DEBUG << "OpencvImageArithNode: 除数常数为零，跳过除法运算";
                    m_params[QStringLiteral("moduleStatus")] = false;
                    return;
                }
                cv::divide(a, c, out);
            }
            break;
        }
        m_params["moduleStatus"] = !out.empty();
        m_outputImage = m_inputImage;
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(OpencvUtil::matToHimage(out));
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvImageArithNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvImageArithNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvImageArithNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

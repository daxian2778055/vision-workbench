#include "OpencvImageArithNode.h"
#include "NodeResultFields.h"
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
    // Y1：运算对象=图像时第二路输入走逻辑槽（画布仍只 1 个可见输入点）。
    // 旧版没有这个槽 ⇒ setInputBinding 直接 return，m_inputData[1] 永不注入，图像分支恒判红。
    addInputSlot(QStringLiteral("图像2"), PortDataType::Image);
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
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("图像运算：输入图像无法转换为 OpenCV 矩阵");
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
            if (!m_inputData.contains(1) || !m_inputData[1]
                || !m_inputData[1]->getHImage().IsInitialized()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("图像运算：缺少第二图像输入（逻辑槽「图像2」未连接）");
                m_params["moduleStatus"] = false;
                return;
            }
            b = OpencvUtil::himageToMat(m_inputData[1]->getHImage());
            if (b.empty()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("图像运算：第二图像无法转换为 OpenCV 矩阵");
                m_params["moduleStatus"] = false;
                return;
            }
            if (b.channels() == 3)
                cv::cvtColor(b, b, cv::COLOR_BGR2GRAY);
            if (b.size() != a.size()) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("图像运算：两幅图像尺寸不一致（%1×%2 vs %3×%4）")
                        .arg(a.cols).arg(a.rows).arg(b.cols).arg(b.rows);
                m_params["moduleStatus"] = false;
                return;
            }
            if (b.type() != a.type())
                b.convertTo(b, a.type());
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
                // Y1：除数图含 0 时不再静默把 0 改成 1 返回分子（与注释也不符），
                // 而是显式判红——常数除零早就判红，两条通路必须同口径。
                cv::Mat zeroMask;
                cv::compare(b, 0, zeroMask, cv::CMP_EQ);
                if (cv::countNonZero(zeroMask) > 0) {
                    m_params[QStringLiteral("lastError")] =
                        QStringLiteral("图像运算：除数图像含 0 像素，无法做除法");
                    m_params["moduleStatus"] = false;
                    return;
                }
                cv::divide(a, b, out);
            } else {
                // 常数除法：检查常数是否为零
                if (qFuzzyIsNull(c)) {
                    m_params[QStringLiteral("lastError")] =
                        QStringLiteral("图像运算：除数常数为 0");
                    VFP_DEBUG << "OpencvImageArithNode: 除数常数为零，跳过除法运算";
                    m_params["moduleStatus"] = false;
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
        m_params[QStringLiteral("lastError")] =
            QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        VFP_DEBUG << "OpencvImageArithNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
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

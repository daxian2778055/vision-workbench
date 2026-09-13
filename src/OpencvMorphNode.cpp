#include "OpencvMorphNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvMorphNode::OpencvMorphNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("形态学"));
    m_type = IMAGE_PROCESSING;
}

void OpencvMorphNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("结果图"), PortDataType::Image);
    registerParams({
        makeEnumParam(QStringLiteral("op"), 0, QStringList{"腐蚀", "膨胀", "开运算", "闭运算"},
                      QStringLiteral("形态学操作")),
        makeIntParam(QStringLiteral("kernelSize"), 3, 1, 31,
                     QStringLiteral("核大小（奇数）")),
        makeIntParam(QStringLiteral("iterations"), 1, 1, 10,
                     QStringLiteral("迭代次数")),
    });
}

void OpencvMorphNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::Mat mat = OpencvUtil::himageToMat(input);
        if (mat.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (mat.channels() == 3) {
            cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);
        }
        const int op = m_params.value(QStringLiteral("op"), 0).toInt();
        const int ksize = m_params.value(QStringLiteral("kernelSize"), 3).toInt();
        const int iters = m_params.value(QStringLiteral("iterations"), 1).toInt();

        const int k = std::max(1, ksize | 1);  // 强制奇数
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(k, k));

        cv::Mat out;
        switch (op) {
        case 1:
            cv::dilate(mat, out, kernel, cv::Point(-1, -1), iters);
            break;
        case 2:
            cv::morphologyEx(mat, out, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), iters);
            break;
        case 3:
            cv::morphologyEx(mat, out, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), iters);
            break;
        case 0:
        default:
            cv::erode(mat, out, kernel, cv::Point(-1, -1), iters);
            break;
        }

        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(OpencvUtil::matToHimage(out));
        setOutputData(1, outObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvMorphNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvMorphNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvMorphNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

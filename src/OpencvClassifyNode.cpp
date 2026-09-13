#include "OpencvClassifyNode.h"
#include "ClassifierTrainer.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>
#include <opencv2/imgproc.hpp>

OpencvClassifyNode::OpencvClassifyNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("分类推理"));
    m_type = IMAGE_PROCESSING;
}

void OpencvClassifyNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("分类结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("modelPath"), QString(),
                          QStringLiteral("模型文件 (.yaml，由 OpenCV分类器训练 生成)")),
    });
    m_params[QStringLiteral("classId")] = -1;
    m_params[QStringLiteral("className")] = QString();
    m_params[QStringLiteral("confidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
}

void OpencvClassifyNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("classId")] = -1;
    m_params[QStringLiteral("className")] = QString();
    m_params[QStringLiteral("confidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        const QString modelPath =
            m_params.value(QStringLiteral("modelPath"), QString()).toString().trimmed();
        if (modelPath.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置模型文件路径");
            m_params["moduleStatus"] = false;
            return;
        }
        ClsTrainer::Model m =
            ClsTrainer::loadModel(modelPath.toLocal8Bit().constData());
        if (!m.loaded) {
            m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(m.error.c_str());
            m_params["moduleStatus"] = false;
            return;
        }
        cv::Mat gray = OpencvUtil::himageToMat(input);
        if (gray.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (gray.channels() == 3) cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

        ClsTrainer::PredictResult r = ClsTrainer::predict(m, gray);
        if (!r.ok) {
            m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(r.error.c_str());
            m_params["moduleStatus"] = false;
            return;
        }
        m_params[QStringLiteral("classId")] = r.classId;
        m_params[QStringLiteral("className")] = QString::fromLocal8Bit(r.className.c_str());
        m_params[QStringLiteral("confidence")] = r.confidence;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        MeasureResult res;
        res.type = QStringLiteral("classify");
        res.valueName = QStringLiteral("类别");
        res.valid = true;
        res.value = r.classId;
        res.extraValues = QVector<double>({r.confidence});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvClassifyNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvClassifyNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvClassifyNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

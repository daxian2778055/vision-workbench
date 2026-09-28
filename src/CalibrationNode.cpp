#include "CalibrationNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>

using namespace HalconCpp;

CalibrationNode::CalibrationNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("相机标定"));
    m_type = SHAPE_ANALYSIS;
}

void CalibrationNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("标定结果"), PortDataType::String);
    addOutputPort(QStringLiteral("内参"), PortDataType::Matrix);
    registerParams({
        makeStringParam(QStringLiteral("descrPath"), QStringLiteral(""),
                        QStringLiteral("标定板描述文件 (.descr)")),
        makeDoubleParam(QStringLiteral("focus"), 16.0, 0.1, 1000.0, QStringLiteral("焦距 f"), QStringLiteral("mm")),
        makeDoubleParam(QStringLiteral("kappa"), 0.0, -10.0, 10.0, QStringLiteral("畸变系数 κ")),
        makeDoubleParam(QStringLiteral("sx"), 0.000005, 1e-9, 0.1, QStringLiteral("像元宽 Sx"), QStringLiteral("mm")),
        makeDoubleParam(QStringLiteral("sy"), 0.000005, 1e-9, 0.1, QStringLiteral("像元高 Sy"), QStringLiteral("mm")),
        makeDoubleParam(QStringLiteral("cx"), 640.0, 0.0, 100000.0, QStringLiteral("主点列 Cx"), QStringLiteral("px")),
        makeDoubleParam(QStringLiteral("cy"), 512.0, 0.0, 100000.0, QStringLiteral("主点行 Cy"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("imgWidth"), 1280, 1, 100000, QStringLiteral("图像宽"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("imgHeight"), 1024, 1, 100000, QStringLiteral("图像高"), QStringLiteral("px")),
    });
    m_params[QStringLiteral("calibrated")] = false;
}

void CalibrationNode::run(bool /*autoSwitch*/)
{
    try {
        if (!m_inputImage.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        const QString descr = m_params.value(QStringLiteral("descrPath")).toString();
        if (descr.isEmpty()) {
            auto warnObj = QSharedPointer<DataObject>::create();
            warnObj->setType(DataObject::DataType::String);
            warnObj->setData(QStringLiteral("请先选择标定板描述文件 (.descr)"));
            setOutputData(1, warnObj);
            m_outputImage = m_inputImage;
            m_params[QStringLiteral("calibrated")] = false;
            return;
        }
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        auto pi = [this](const QString &n, int d) { return m_params.value(n, d).toInt(); };

        // 与 CalibrationBoardNode 相同的标定链路：
        // CreateCalibData -> SetCalibDataCamParam -> SetCalibDataCalibObject
        // -> FindCalibObject -> CalibrateCameras -> GetCalibData
        HTuple calibID;
        CreateCalibData("calibration_object", 1, 1, &calibID);

        HTuple camParams;
        camParams.Append(pv(QStringLiteral("focus"), 16));
        camParams.Append(pv(QStringLiteral("kappa"), 0));
        camParams.Append(pv(QStringLiteral("sx"), 0.000005));
        camParams.Append(pv(QStringLiteral("sy"), 0.000005));
        camParams.Append(pv(QStringLiteral("cx"), 640));
        camParams.Append(pv(QStringLiteral("cy"), 512));
        camParams.Append(pi(QStringLiteral("imgWidth"), 1280));
        camParams.Append(pi(QStringLiteral("imgHeight"), 1024));
        SetCalibDataCamParam(calibID, 0, "area_scan_division", camParams);

        SetCalibDataCalibObject(calibID, 0, descr.toStdString().c_str());

        HImage img(m_inputImage);
        FindCalibObject(img, calibID, 0, 0, 0, HTuple(), HTuple());
        HTuple error;
        CalibrateCameras(calibID, &error);

        HTuple finalParams;
        GetCalibData(calibID, "camera", 0, "params", &finalParams);
        ClearCalibData(calibID);

        QVector<double> paramsVec;
        for (int i = 0; i < finalParams.Length(); ++i)
            paramsVec.append(finalParams[i].D());
        if (!CalibrationManager::instance()->setHomography(QStringLiteral("cam_params"), paramsVec)) {
            // R-5：键 cam_params 上已有另一种项数的载荷 ⇒ 单例拒绝整条顶掉。此时不得报"标定成功"。
            const auto held = CalibrationManager::instance()->homography(QStringLiteral("cam_params")).size();
            const QString why = QStringLiteral("标定结果无法存入：键 cam_params 上已有 %1 项的另一种载荷，本次 HALCON 内参是 %2 项")
                                    .arg(held).arg(paramsVec.size());
            auto failObj = QSharedPointer<DataObject>::create();
            failObj->setType(DataObject::DataType::String);
            failObj->setData(why);
            setOutputData(1, failObj);
            m_params[QStringLiteral("calibNote")] = why;  // 判红会清空端口，原因另存结果字段（§3.4 口径）
            m_params[QStringLiteral("calibrated")] = false;
            m_params["moduleStatus"] = false;
            m_outputImage.Clear();
            return;
        }

        QStringList parts;
        for (int i = 0; i < finalParams.Length(); ++i)
            parts << QString::number(finalParams[i].D(), 'f', 4);

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定成功\n标定误差: %1\n内参: [%2]")
                            .arg(error.D(), 0, 'f', 4)
                            .arg(parts.join(QStringLiteral(", "))));
        setOutputData(1, strObj);

        auto matObj = QSharedPointer<DataObject>::create();
        matObj->setType(DataObject::DataType::Matrix);
        matObj->setData(QVariant::fromValue(paramsVec));
        setOutputData(2, matObj);

        m_params[QStringLiteral("calibrated")] = true;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;
    } catch (const HException &e) {
        m_outputImage.Clear();
        const QString why = QStringLiteral("标定失败: %1")
                                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(why);
        setOutputData(1, strObj);
        m_params[QStringLiteral("calibNote")] = why;  // 判红会清空端口，原因另存结果字段（§3.4 口径）
        m_params[QStringLiteral("calibrated")] = false;
        m_params["moduleStatus"] = false;
    }
}

QWidget *CalibrationNode::createParamPanel()
{
    // 声明式参数系统自动生成面板
    return createAutoParamPanel();
}

void CalibrationNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

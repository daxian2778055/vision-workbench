#include "CalibrationBoardNode.h"
#include "DataObject.h"
#include "Port.h"
#include "CalibrationManager.h"

using namespace HalconCpp;

CalibrationBoardNode::CalibrationBoardNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("标定板标定"));
    m_type = SHAPE_ANALYSIS;
}

void CalibrationBoardNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("标定结果"), PortDataType::String);
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
}

void CalibrationBoardNode::run(bool)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        const QString descr = m_params.value(QStringLiteral("descrPath")).toString();
        if (descr.isEmpty()) {
            auto strObj = QSharedPointer<DataObject>::create();
            strObj->setType(DataObject::DataType::String);
            strObj->setData(QStringLiteral("请先选择标定板描述文件 (.descr)"));
            setOutputData(1, strObj);
            m_outputImage = m_inputImage;
            return;
        }
        auto pv = [this](const QString &n, double d) { return m_params.value(n, d).toDouble(); };
        auto pi = [this](const QString &n, int d) { return m_params.value(n, d).toInt(); };

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
        CalibrationManager::instance()->setHomography(QStringLiteral("cam_params"), paramsVec);

        QStringList parts;
        for (int i = 0; i < finalParams.Length(); ++i)
            parts << QString::number(finalParams[i].D(), 'f', 4);
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定成功\n标定误差: %1\n内参: [%2]")
                            .arg(error.D(), 0, 'f', 4)
                            .arg(parts.join(QStringLiteral(", "))));
        setOutputData(1, strObj);

        m_outputImage = m_inputImage;
    } catch (const HException &e) {
        m_outputImage.Clear();
        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(QStringLiteral("标定失败: %1").arg(QString::fromStdString(e.ErrorMessage().Text())));
        setOutputData(1, strObj);
    }
}

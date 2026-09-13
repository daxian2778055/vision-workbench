#include "DeepOcrNode.h"
#include <algorithm>
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>

using namespace HalconCpp;

DeepOcrNode::DeepOcrNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("HALCON DeepOCR"));
    m_type = SHAPE_ANALYSIS;
}

void DeepOcrNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别文本"), PortDataType::String);
    addOutputPort(QStringLiteral("OCR结果"), PortDataType::Measure);
    registerParams({
        makeEnumParam(QStringLiteral("mode"), 0,
                      {QStringLiteral("auto"), QStringLiteral("detection"),
                       QStringLiteral("recognition")},
                      QStringLiteral("模式：auto=检测+识别；detection=只定位文字；recognition=只识别裁剪文字")),
        makeEnumParam(QStringLiteral("runtime"), 0,
                      {QStringLiteral("cpu"), QStringLiteral("auto")},
                      QStringLiteral("推理设备（无 GPU 环境用 cpu）")),
        makeDoubleParam(QStringLiteral("minConfidence"), 0.3, 0.0, 1.0,
                        QStringLiteral("最低置信度（低于此值的词丢弃）")),
    });
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("ocrCount")] = 0;
    m_params[QStringLiteral("ocrConfidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
}

void DeepOcrNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("ocrCount")] = 0;
    m_params[QStringLiteral("ocrConfidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        const int modeIdx = m_params.value(QStringLiteral("mode"), 0).toInt();
        const QString mode = (modeIdx == 1)   ? QStringLiteral("detection")
                             : (modeIdx == 2) ? QStringLiteral("recognition")
                                              : QStringLiteral("auto");
        const int runtimeIdx = m_params.value(QStringLiteral("runtime"), 0).toInt();
        const QString runtime =
            (runtimeIdx == 1) ? QStringLiteral("auto") : QStringLiteral("cpu");
        const double minConf =
            m_params.value(QStringLiteral("minConfidence"), 0.3).toDouble();

        HTuple ocrH;
        try {
            CreateDeepOcr(HTuple("mode"), HTuple(mode.toStdString().c_str()), &ocrH);
        } catch (const HException &e) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 创建失败（需 HALCONROOT/dl 下的官方预训练模型）: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
            m_params["moduleStatus"] = false;
            return;
        }
        // CPU 推理（无 GPU/cuBLAS 环境最稳）
        try {
            SetDeepOcrParam(ocrH, "runtime", HTuple(runtime.toStdString().c_str()));
        } catch (const HException &e) {
            // 配置失败不中断流程，但记录原因便于定位（避免静默吞异常）
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 设置 runtime 失败: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
        }

        HTuple results;
        try {
            ApplyDeepOcr(input, ocrH, HTuple(mode.toStdString().c_str()), &results);
        } catch (const HException &e) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 推理失败: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
            m_params["moduleStatus"] = false;
            try { ClearDlModel(ocrH); }
            catch (const HException &ce) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("DeepOCR 推理失败后清理模型也失败: %1")
                        .arg(QString::fromStdString(ce.ErrorMessage().Text()));
            }
            return;
        }
        try { ClearDlModel(ocrH); }
        catch (const HException &ce) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 清理模型失败: %1")
                    .arg(QString::fromStdString(ce.ErrorMessage().Text()));
        }

        // 解析结果 dict（deep_ocr 结果：单个 dict）
        QStringList words;
        double confSum = 0.0;
        int confN = 0;
        bool haveResult = false;
        try {
            if (results.Length() > 0) {
                const HDict d(results.H());
                if (d.GetDictParam("key_exists", "word").I() == 1) {
                    const HTuple ws = d.GetDictTuple("word");
                    HTuple cs;
                    double maxConf = 0.0;
                    if (d.GetDictParam("key_exists", "confidence").I() == 1) {
                        cs = d.GetDictTuple("confidence");
                    }
                    for (Hlong i = 0; i < ws.Length(); ++i) {
                        QString w = QString::fromStdString(ws[i].S().Text());
                        if (w.isEmpty()) continue;
                        const double c = (cs.Length() > i) ? cs[i].D() : 0.0;
                        if (c < minConf) continue;
                        words << w;
                        confSum += c;
                        ++confN;
                        maxConf = (c > maxConf) ? c : maxConf;
                    }
                    m_params[QStringLiteral("ocrConfidence")] = maxConf;
                }
                haveResult = true;
            }
        } catch (const HException &) {
        }
        if (!haveResult) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("OCR 无返回结果");
            m_params["moduleStatus"] = false;
            return;
        }

        m_params[QStringLiteral("ocrText")] = words.join(QStringLiteral(" "));
        m_params[QStringLiteral("ocrCount")] = words.size();
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(m_params[QStringLiteral("ocrText")].toString());
        setOutputData(1, strObj);

        MeasureResult res;
        res.type = QStringLiteral("ocr");
        res.valueName = QStringLiteral("词数");
        res.valid = true;
        res.value = words.size();
        res.extraValues = QVector<double>({m_params[QStringLiteral("ocrConfidence")].toDouble()});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(2, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "DeepOcrNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *DeepOcrNode::createParamPanel()
{
    return createAutoParamPanel();
}

void DeepOcrNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

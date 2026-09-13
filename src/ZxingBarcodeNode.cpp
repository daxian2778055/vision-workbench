#include "ZxingBarcodeNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <zxing/ZXingCpp.h>
#include <opencv2/imgproc.hpp>
#include <QWidget>

ZxingBarcodeNode::ZxingBarcodeNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ZXing条码解码"));
    m_type = SHAPE_ANALYSIS;
}

void ZxingBarcodeNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("解码内容"), PortDataType::String);
    registerParams({
        makeBoolParam(QStringLiteral("tryHarder"), true,
                      QStringLiteral("尝试更努力（对模糊/低对比度条码更鲁棒）")),
    });
    m_params[QStringLiteral("barcodeText")] = QString();
    m_params[QStringLiteral("barcodeFormat")] = QString();
}

void ZxingBarcodeNode::run(bool /*autoSwitch*/)
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
        const bool harder = m_params.value(QStringLiteral("tryHarder"), true).toBool();

        // 灰度图 → ZXing ImageView（CV_8UC1，行主序）
        ZXing::ImageView view(gray.data, gray.cols, gray.rows, ZXing::ImageFormat::Lum);
        ZXing::ReaderOptions opts;
        opts.setTryHarder(harder);
        opts.setTryRotate(true);
        // 默认全部格式：一维（EAN/UPC/Code128/Code39/ITF/Codabar）+ 二维（QR/DataMatrix/Aztec/PDF417）
        const auto results = ZXing::ReadBarcodes(view, opts);

        QString text;
        QString fmt;
        if (!results.empty()) {
            const auto &r = results.front();
            text = QString::fromStdString(r.text());
            fmt = QString::fromStdString(ZXing::ToString(r.format()));
        }
        const bool ok = !text.isEmpty();

        m_params[QStringLiteral("barcodeText")] = text;
        m_params[QStringLiteral("barcodeFormat")] = fmt;
        m_params["moduleStatus"] = ok;
        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(text);
        setOutputData(1, strObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "ZxingBarcodeNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *ZxingBarcodeNode::createParamPanel()
{
    return createAutoParamPanel();
}

void ZxingBarcodeNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

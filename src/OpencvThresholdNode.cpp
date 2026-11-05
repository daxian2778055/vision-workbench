#include "OpencvThresholdNode.h"
#include "NodeResultFields.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>
#include <QComboBox>
#include <QFormLayout>
#include <QRectF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

/// 均值/高斯二值化的差值比较：满足比较类型的像素置 255。
cv::Mat compareDiffMask(const cv::Mat &diff, int compareType, int offset)
{
    cv::Mat mask = cv::Mat::zeros(diff.size(), CV_8U);
    switch (compareType) {
    case 1:   // 小于等于
        cv::compare(diff, offset, mask, cv::CMP_LE);
        break;
    case 2:   // 等于
        cv::compare(diff, offset, mask, cv::CMP_EQ);
        break;
    case 3:   // 不等于
        cv::compare(diff, offset, mask, cv::CMP_NE);
        break;
    default:  // 大于等于
        cv::compare(diff, offset, mask, cv::CMP_GE);
        break;
    }
    return mask;
}

int oddKernel(int value)
{
    const int v = std::max(1, value);
    return (v % 2 == 0) ? v + 1 : v;
}

}   // namespace

OpencvThresholdNode::OpencvThresholdNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("二值化"));
    m_type = IMAGE_PROCESSING;
}

void OpencvThresholdNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("二值图"), PortDataType::Image);
    addOutputPort(QStringLiteral("前景像素数"), PortDataType::Number);

    // 字段名、顺序、默认值对齐 VM 4.4 图像二值化（IMVSBinaryModuAlgorithmTab.xml）。
    const QString runGroup = QStringLiteral("运行参数");
    ParamSpecList specs;
    auto addSpec = [&](ParamSpec spec, const QString &tip) {
        spec.group = runGroup;
        spec.tooltip = tip;
        specs.append(spec);
    };
    addSpec(makeEnumParam(QStringLiteral("nonRoiProcess"), 0,
                          {QStringLiteral("黑色"), QStringLiteral("原图")},
                          QStringLiteral("ROI外处理")),
            QStringLiteral("ROI 外的输出：黑色=置 0；原图=保留输入灰度"));
    addSpec(makeEnumParam(QStringLiteral("binaryType"), 1,
                          {QStringLiteral("硬阈值二值化"), QStringLiteral("均值二值化"),
                           QStringLiteral("高斯二值化"), QStringLiteral("Sauvola二值化"),
                           QStringLiteral("自动")},
                          QStringLiteral("二值化类型")),
            QStringLiteral("硬阈值：固定高低阈值；均值/高斯：原图减背景图；"
                           "Sauvola：局部均值+标准差；自动：OTSU"));
    addSpec(makeIntParam(QStringLiteral("lowThreshold"), 100, 0, 255,
                         QStringLiteral("低阈值")),
            QStringLiteral("硬阈值二值化的低灰度阈值（0~255）"));
    addSpec(makeIntParam(QStringLiteral("highThreshold"), 255, 0, 255,
                         QStringLiteral("高阈值")),
            QStringLiteral("硬阈值二值化的高灰度阈值（0~255）"));
    addSpec(makeIntParam(QStringLiteral("kernelWidth"), 3, 1, 255,
                         QStringLiteral("滤波核宽度（均值）"), QStringLiteral("px")),
            QStringLiteral("均值二值化的矩形滤波窗口宽度"));
    addSpec(makeIntParam(QStringLiteral("kernelHeight"), 3, 1, 255,
                         QStringLiteral("滤波核高度（均值）"), QStringLiteral("px")),
            QStringLiteral("均值二值化的矩形滤波窗口高度"));
    addSpec(makeIntParam(QStringLiteral("gaussKernelSize"), 3, 1, 255,
                         QStringLiteral("高斯滤波核"), QStringLiteral("px")),
            QStringLiteral("高斯二值化的滤波核尺寸（内部按奇数取值）"));
    addSpec(makeDoubleParam(QStringLiteral("gaussSigma"), 1.0, 0.1, 100.0,
                            QStringLiteral("高斯标准差")),
            QStringLiteral("高斯二值化的标准差，越大平滑越强"));
    addSpec(makeEnumParam(QStringLiteral("compareType"), 0,
                          {QStringLiteral("大于等于"), QStringLiteral("小于等于"),
                           QStringLiteral("等于"), QStringLiteral("不等于")},
                          QStringLiteral("比较类型")),
            QStringLiteral("原图与背景图差值的比较方式，满足则置 255"));
    addSpec(makeIntParam(QStringLiteral("thresholdOffset"), 0, -255, 255,
                         QStringLiteral("阈值偏移量")),
            QStringLiteral("与背景差异的可容忍阈值"));
    addSpec(makeDoubleParam(QStringLiteral("sauvolaAdjustCoef"), 0.2, 0.0, 1.0,
                            QStringLiteral("校正系数")),
            QStringLiteral("Sauvola 的 k 系数，越大阈值越大"));
    addSpec(makeDoubleParam(QStringLiteral("sauvolaRange"), 128.0, 1.0, 255.0,
                            QStringLiteral("动态范围")),
            QStringLiteral("Sauvola 的 r 动态范围，越大阈值越小"));
    addSpec(makeEnumParam(QStringLiteral("sauvolaSegType"), 0,
                          {QStringLiteral("暗于背景"), QStringLiteral("亮于背景")},
                          QStringLiteral("分割类型")),
            QStringLiteral("暗于背景=目标比背景暗；亮于背景=目标比背景亮"));
    addSpec(makeIntParam(QStringLiteral("sauvolaWinWidth"), 15, 1, 255,
                         QStringLiteral("滤波核宽度（Sauvola）"), QStringLiteral("px")),
            QStringLiteral("Sauvola 局部均值/标准差的窗口宽度"));
    addSpec(makeIntParam(QStringLiteral("sauvolaWinHeight"), 15, 1, 255,
                         QStringLiteral("滤波核高度（Sauvola）"), QStringLiteral("px")),
            QStringLiteral("Sauvola 局部均值/标准差的窗口高度"));
    registerParams(specs);

    m_params[QStringLiteral("foregroundPixels")] = 0;
}

void OpencvThresholdNode::run(bool /*autoSwitch*/)
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
        // 非 8 位输入的显式拒绝：阈值 0-255 / at<uchar> 都按 8 位写死——静默错值比判红更糟。
        {
            QString depthWhy;
            if (!OpencvUtil::requireGray8(mat, &depthWhy))
                throw std::runtime_error(depthWhy.toStdString());
        }
        if (mat.channels() == 3)
            cv::cvtColor(mat, mat, cv::COLOR_BGR2GRAY);

        const int binaryType = m_params.value(QStringLiteral("binaryType"), 1).toInt();
        const int nonRoiProcess = m_params.value(QStringLiteral("nonRoiProcess"), 0).toInt();
        const int low = m_params.value(QStringLiteral("lowThreshold"), 100).toInt();
        const int high = m_params.value(QStringLiteral("highThreshold"), 255).toInt();
        const int compareType = m_params.value(QStringLiteral("compareType"), 0).toInt();
        const int offset = m_params.value(QStringLiteral("thresholdOffset"), 0).toInt();

        cv::Mat bin;
        switch (binaryType) {
        case 0: {   // 硬阈值：低<高 → 区间内置 255；低>高 → 区间外置 255
            const int lo = std::min(low, high);
            const int hi = std::max(low, high);
            cv::Mat inRangeMask;
            cv::inRange(mat, cv::Scalar(lo), cv::Scalar(hi), inRangeMask);
            if (low > high)
                cv::bitwise_not(inRangeMask, inRangeMask);
            bin = inRangeMask;
            break;
        }
        case 1: {   // 均值二值化：原图 − 均值背景图
            const int kw = std::max(1, m_params.value(QStringLiteral("kernelWidth"), 3).toInt());
            const int kh = std::max(1, m_params.value(QStringLiteral("kernelHeight"), 3).toInt());
            cv::Mat background;
            cv::boxFilter(mat, background, CV_8U, cv::Size(kw, kh),
                          cv::Point(-1, -1), true, cv::BORDER_REFLECT);
            cv::Mat diff;
            cv::subtract(mat, background, diff, cv::noArray(), CV_16S);
            bin = compareDiffMask(diff, compareType, offset);
            break;
        }
        case 2: {   // 高斯二值化：原图 − 高斯背景图
            const int gk = oddKernel(m_params.value(QStringLiteral("gaussKernelSize"), 3).toInt());
            const double sigma = std::max(0.1, m_params.value(QStringLiteral("gaussSigma"), 1.0).toDouble());
            cv::Mat background;
            cv::GaussianBlur(mat, background, cv::Size(gk, gk), sigma, sigma, cv::BORDER_REFLECT);
            cv::Mat diff;
            cv::subtract(mat, background, diff, cv::noArray(), CV_16S);
            bin = compareDiffMask(diff, compareType, offset);
            break;
        }
        case 3: {   // Sauvola：T = m * (1 + k*(std/r − 1))
            const int ww = std::max(1, m_params.value(QStringLiteral("sauvolaWinWidth"), 15).toInt());
            const int wh = std::max(1, m_params.value(QStringLiteral("sauvolaWinHeight"), 15).toInt());
            const double k = m_params.value(QStringLiteral("sauvolaAdjustCoef"), 0.2).toDouble();
            const double r = std::max(1.0, m_params.value(QStringLiteral("sauvolaRange"), 128.0).toDouble());
            const int segType = m_params.value(QStringLiteral("sauvolaSegType"), 0).toInt();

            cv::Mat grayF;
            mat.convertTo(grayF, CV_32F);
            cv::Mat mean, meanSq;
            cv::boxFilter(grayF, mean, CV_32F, cv::Size(ww, wh),
                          cv::Point(-1, -1), true, cv::BORDER_REFLECT);
            cv::boxFilter(grayF.mul(grayF), meanSq, CV_32F, cv::Size(ww, wh),
                          cv::Point(-1, -1), true, cv::BORDER_REFLECT);
            cv::Mat var = meanSq - mean.mul(mean);
            var = cv::max(var, 0.0);
            cv::Mat stddev;
            cv::sqrt(var, stddev);
            cv::Mat ratio = stddev / r - 1.0;
            cv::Mat factor = 1.0 + k * ratio;
            cv::Mat threshold = mean.mul(factor);

            bin = cv::Mat::zeros(mat.size(), CV_8U);
            for (int y = 0; y < mat.rows; ++y) {
                const float *vRow = grayF.ptr<float>(y);
                const float *tRow = threshold.ptr<float>(y);
                uchar *oRow = bin.ptr<uchar>(y);
                for (int x = 0; x < mat.cols; ++x) {
                    const bool target = (segType == 1) ? (vRow[x] >= tRow[x])
                                                       : (vRow[x] < tRow[x]);
                    oRow[x] = target ? 255 : 0;
                }
            }
            break;
        }
        default:   // 自动（OTSU）
            cv::threshold(mat, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
            break;
        }

        // ROI：矩形几何 ROI（无 ROI 时全图）+ 掩膜（白=参与）。
        cv::Mat allowed(mat.size(), CV_8U, cv::Scalar(255));
        const RoiShape roi = geometryRoi();
        if (roi.type == RoiType::Rect) {
            QRectF box = QRectF(roi.p1, roi.p2).normalized();
            if (box.width() > 1.0 && box.height() > 1.0) {
                allowed.setTo(0);
                cv::rectangle(allowed,
                              cv::Rect(int(box.x()), int(box.y()),
                                       int(box.width()), int(box.height())),
                              cv::Scalar(255), cv::FILLED);
            }
        }
        OpencvUtil::applyGrayMask(allowed, editMask());

        cv::Mat outside;
        cv::compare(allowed, 0, outside, cv::CMP_EQ);
        cv::Mat output = bin.clone();
        if (nonRoiProcess == 0) {
            output.setTo(0, outside);                  // ROI 外：黑色
        } else {
            mat.copyTo(output, outside);               // ROI 外：原图
        }

        cv::Mat binInRoi;
        cv::bitwise_and(bin, allowed, binInRoi);
        const int fg = static_cast<int>(cv::countNonZero(binInRoi));
        m_params[QStringLiteral("foregroundPixels")] = fg;
        m_params["moduleStatus"] = true;

        // 输出：主图=最终二值图（ROI 外按设置），子结果保留同图 + 前景像素数。
        m_outputImage = OpencvUtil::matToHimage(output);
        auto binObj = QSharedPointer<DataObject>::create();
        binObj->setHImage(m_outputImage);
        setOutputData(1, binObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(fg));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvThresholdNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromUtf8(e.what());   // 原因到面板（2026-10-26）
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params[QStringLiteral("lastError")] = unknownExceptionReason();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvThresholdNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    if (auto *typeCombo = panel->findChild<QComboBox *>(QStringLiteral("binaryType"))) {
        connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, panel](int) { applyParamVisibility(panel); });
    }
    applyParamVisibility(panel);
    return panel;
}

void OpencvThresholdNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
    applyParamVisibility(panel);
}

void OpencvThresholdNode::applyParamVisibility(QWidget *panel)
{
    if (!panel)
        return;
    QComboBox *typeCombo = panel->findChild<QComboBox *>(QStringLiteral("binaryType"));
    const int binaryType = typeCombo ? typeCombo->currentIndex()
                                     : m_params.value(QStringLiteral("binaryType"), 1).toInt();

    QSet<QString> visible;
    switch (binaryType) {
    case 0:
        visible = {QStringLiteral("lowThreshold"), QStringLiteral("highThreshold")};
        break;
    case 1:
        visible = {QStringLiteral("kernelWidth"), QStringLiteral("kernelHeight"),
                   QStringLiteral("compareType"), QStringLiteral("thresholdOffset")};
        break;
    case 2:
        visible = {QStringLiteral("gaussKernelSize"), QStringLiteral("gaussSigma"),
                   QStringLiteral("compareType"), QStringLiteral("thresholdOffset")};
        break;
    case 3:
        visible = {QStringLiteral("sauvolaAdjustCoef"), QStringLiteral("sauvolaRange"),
                   QStringLiteral("sauvolaSegType"), QStringLiteral("sauvolaWinWidth"),
                   QStringLiteral("sauvolaWinHeight")};
        break;
    default:
        break;   // 自动二值化没有额外参数
    }

    static const QSet<QString> dependent {
        QStringLiteral("lowThreshold"), QStringLiteral("highThreshold"),
        QStringLiteral("kernelWidth"), QStringLiteral("kernelHeight"),
        QStringLiteral("gaussKernelSize"), QStringLiteral("gaussSigma"),
        QStringLiteral("compareType"), QStringLiteral("thresholdOffset"),
        QStringLiteral("sauvolaAdjustCoef"), QStringLiteral("sauvolaRange"),
        QStringLiteral("sauvolaSegType"), QStringLiteral("sauvolaWinWidth"),
        QStringLiteral("sauvolaWinHeight")
    };

    // 找到包含 binaryType 下拉的 QFormLayout，按参数 objectName 折叠行。
    QFormLayout *form = nullptr;
    for (QFormLayout *f : panel->findChildren<QFormLayout *>()) {
        if (typeCombo && f->indexOf(typeCombo) >= 0) {
            form = f;
            break;
        }
    }
    if (!form)
        return;
    for (int row = 0; row < form->rowCount(); ++row) {
        QLayoutItem *fieldItem = form->itemAt(row, QFormLayout::FieldRole);
        QWidget *field = fieldItem ? fieldItem->widget() : nullptr;
        const QString name = field ? field->objectName() : QString();
        if (!dependent.contains(name))
            continue;
        form->setRowVisible(row, visible.contains(name));
    }
}

RoiShape OpencvThresholdNode::geometryRoi() const
{
    const int row = m_params.value(QStringLiteral("roiRectRow"), 0).toInt();
    const int col = m_params.value(QStringLiteral("roiRectCol"), 0).toInt();
    const int width = m_params.value(QStringLiteral("roiRectWidth"), 0).toInt();
    const int height = m_params.value(QStringLiteral("roiRectHeight"), 0).toInt();
    RoiShape shape;
    if (width <= 1 || height <= 1)
        return shape;
    shape.type = RoiType::Rect;
    shape.p1 = QPointF(col, row);
    shape.p2 = QPointF(col + width, row + height);
    return shape;
}

void OpencvThresholdNode::applyGeometryRoi(const RoiShape &shape)
{
    if (shape.type == RoiType::Rect) {
        const QRectF box = QRectF(shape.p1, shape.p2).normalized();
        setParam(QStringLiteral("roiRectRow"), int(box.y()));
        setParam(QStringLiteral("roiRectCol"), int(box.x()));
        setParam(QStringLiteral("roiRectWidth"), qMax(1, int(box.width())));
        setParam(QStringLiteral("roiRectHeight"), qMax(1, int(box.height())));
    } else if (shape.type == RoiType::None) {
        setParam(QStringLiteral("roiRectRow"), 0);
        setParam(QStringLiteral("roiRectCol"), 0);
        setParam(QStringLiteral("roiRectWidth"), 0);
        setParam(QStringLiteral("roiRectHeight"), 0);
    }
}

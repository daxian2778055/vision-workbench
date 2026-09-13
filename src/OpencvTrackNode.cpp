#include "OpencvTrackNode.h"
#include "OpenCvTracker.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

OpencvTrackNode::OpencvTrackNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("目标跟踪"));
    m_type = IMAGE_PROCESSING;
}

void OpencvTrackNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("跟踪结果"), PortDataType::Measure);
    registerParams({
        makeDoubleParam(QStringLiteral("initX"), 0.0, 0.0, 1e6,
                        QStringLiteral("初始目标区域 X（像素列）")),
        makeDoubleParam(QStringLiteral("initY"), 0.0, 0.0, 1e6,
                        QStringLiteral("初始目标区域 Y（像素行）")),
        makeDoubleParam(QStringLiteral("initW"), 64.0, 4.0, 1e6,
                        QStringLiteral("初始目标区域宽")),
        makeDoubleParam(QStringLiteral("initH"), 64.0, 4.0, 1e6,
                        QStringLiteral("初始目标区域高")),
        makeIntParam(QStringLiteral("searchMargin"), 50, 4, 2000,
                     QStringLiteral("搜索窗口扩展（像素）")),
        makeDoubleParam(QStringLiteral("minScore"), 0.5, 0.0, 1.0,
                        QStringLiteral("最低匹配得分（低于则跟踪丢失）")),
        makeBoolParam(QStringLiteral("drawBox"), true,
                      QStringLiteral("输出图像绘制跟踪框")),
    });
    m_params[QStringLiteral("trackRow")] = 0.0;
    m_params[QStringLiteral("trackCol")] = 0.0;
    m_params[QStringLiteral("trackScore")] = 0.0;
    m_params[QStringLiteral("trackLost")] = false;
    m_params[QStringLiteral("lastError")] = QString();
}

void OpencvTrackNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("trackLost")] = false;
    m_params[QStringLiteral("lastError")] = QString();
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
        if (gray.channels() == 3)
            cv::cvtColor(gray, gray, cv::COLOR_BGR2GRAY);

        const double ix = m_params.value(QStringLiteral("initX"), 0.0).toDouble();
        const double iy = m_params.value(QStringLiteral("initY"), 0.0).toDouble();
        const double iw = m_params.value(QStringLiteral("initW"), 64.0).toDouble();
        const double ih = m_params.value(QStringLiteral("initH"), 64.0).toDouble();
        const int margin = m_params.value(QStringLiteral("searchMargin"), 50).toInt();
        const double minScore = m_params.value(QStringLiteral("minScore"), 0.5).toDouble();
        const bool drawBox = m_params.value(QStringLiteral("drawBox"), true).toBool();

        const cv::Rect desiredRect(cvRound(ix), cvRound(iy), cvRound(iw), cvRound(ih));
        // 初始区域变化或未初始化时重新初始化
        if (!m_tracker.initialized() || desiredRect != m_lastInitRect) {
            if (!m_tracker.init(gray, desiredRect)) {
                m_params[QStringLiteral("lastError")] =
                    QStringLiteral("跟踪区域无效或超出图像范围");
                m_params["moduleStatus"] = false;
                return;
            }
            m_lastInitRect = desiredRect;
        }

        cv::Rect outRect;
        double score = 0.0;
        if (!m_tracker.update(gray, outRect, score, margin)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("跟踪更新失败");
            m_params["moduleStatus"] = false;
            return;
        }
        if (score < minScore) {
            m_params[QStringLiteral("trackLost")] = true;
            m_params[QStringLiteral("trackScore")] = score;
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("目标丢失（得分 %1 < %2）")
                    .arg(score, 0, 'f', 3)
                    .arg(minScore, 0, 'f', 2);
            m_params["moduleStatus"] = false;
            m_outputImage = m_inputImage;
            return;
        }

        const double centerRow = outRect.y + outRect.height / 2.0;
        const double centerCol = outRect.x + outRect.width / 2.0;
        m_params[QStringLiteral("trackRow")] = centerRow;
        m_params[QStringLiteral("trackCol")] = centerCol;
        m_params[QStringLiteral("trackScore")] = score;
        m_params["moduleStatus"] = true;

        // 输出图像：绘制跟踪框
        if (drawBox) {
            cv::Mat bgr = OpencvUtil::himageToMat(input);
            if (bgr.channels() == 1)
                cv::cvtColor(bgr, bgr, cv::COLOR_GRAY2BGR);
            cv::rectangle(bgr, outRect, cv::Scalar(0, 255, 0), 2);
            m_outputImage = OpencvUtil::matToHimage(bgr);
        } else {
            m_outputImage = m_inputImage;
        }

        MeasureResult res;
        res.type = QStringLiteral("track");
        res.valueName = QStringLiteral("匹配得分");
        res.valid = true;
        res.value = score;
        res.point1 = QPointF(centerCol, centerRow);
        res.extraValues = QVector<double>({centerRow, centerCol,
                                           double(outRect.width),
                                           double(outRect.height)});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvTrackNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvTrackNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvTrackNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

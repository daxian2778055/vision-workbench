#include "OpencvBlobNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <opencv2/imgproc.hpp>
#include <QWidget>

OpencvBlobNode::OpencvBlobNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("Blob分析"));
    m_type = SHAPE_ANALYSIS;
}

void OpencvBlobNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("标记图"), PortDataType::Image);
    addOutputPort(QStringLiteral("Blob数量"), PortDataType::Number);
    registerParams({
        makeIntParam(QStringLiteral("minArea"), 0, 0, 1000000,
                     QStringLiteral("最小面积过滤")),
        makeIntParam(QStringLiteral("maxArea"), 100000000, 0, 100000000,
                     QStringLiteral("最大面积过滤")),
    });
    m_params[QStringLiteral("blobCount")] = 0;
    m_params[QStringLiteral("maxBlobArea")] = 0.0;
    m_params[QStringLiteral("maxBlobCentroidRow")] = 0.0;
    m_params[QStringLiteral("maxBlobCentroidCol")] = 0.0;
}

void OpencvBlobNode::run(bool /*autoSwitch*/)
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
        const int minArea = m_params.value(QStringLiteral("minArea"), 0).toInt();
        const int maxArea = m_params.value(QStringLiteral("maxArea"), 100000000).toInt();

        cv::Mat bin;
        cv::threshold(mat, bin, 1, 255, cv::THRESH_BINARY);
        OpencvUtil::applyGrayMask(bin, editMask());

        cv::Mat labels, stats, centroids;
        const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8);

        int validCount = 0;
        double maxBlobArea = 0.0;
        double maxRow = 0.0, maxCol = 0.0;
        for (int i = 1; i < n; ++i) {  // 0 = 背景
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minArea || area > maxArea) continue;
            ++validCount;
            if (area > maxBlobArea) {
                maxBlobArea = area;
                maxCol = centroids.at<double>(i, 0);
                maxRow = centroids.at<double>(i, 1);
            }
        }

        m_params[QStringLiteral("blobCount")] = validCount;
        m_params[QStringLiteral("maxBlobArea")] = maxBlobArea;
        m_params[QStringLiteral("maxBlobCentroidRow")] = maxRow;
        m_params[QStringLiteral("maxBlobCentroidCol")] = maxCol;
        m_params["moduleStatus"] = true;

        // 标记图：每个 blob 按标签映射为灰阶（背景黑）
        cv::Mat vis = cv::Mat::zeros(bin.size(), CV_8UC1);
        for (int i = 1; i < n; ++i) {
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minArea || area > maxArea) continue;
            const uchar gray = static_cast<uchar>(20 + (i % 12) * 20);
            for (int y = 0; y < labels.rows; ++y) {
                const int *lp = labels.ptr<int>(y);
                uchar *vp = vis.ptr<uchar>(y);
                for (int x = 0; x < labels.cols; ++x) {
                    if (lp[x] == i) vp[x] = gray;
                }
            }
        }

        m_outputImage = m_inputImage;
        auto visObj = QSharedPointer<DataObject>::create();
        visObj->setHImage(OpencvUtil::matToHimage(vis));
        setOutputData(1, visObj);
        auto numObj = QSharedPointer<DataObject>::create();
        numObj->setValue(double(validCount));
        setOutputData(2, numObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvBlobNode error:" << e.what();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvBlobNode::createParamPanel()
{
    return createAutoParamPanel();
}

void OpencvBlobNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

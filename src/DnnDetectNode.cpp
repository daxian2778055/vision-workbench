#include "DnnDetectNode.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <QWidget>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <cmath>
#include <algorithm>

// 静态模型缓存（与 DnnInferNode 隔离，避免不同任务互相淘汰）
QMap<QString, cv::dnn::Net> DnnDetectNode::s_netCache;

DnnDetectNode::DnnDetectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ONNX目标检测"));
    m_type = IMAGE_PROCESSING;
}

DnnDetectNode::~DnnDetectNode()
{
}

cv::dnn::Net DnnDetectNode::getOrLoadNet(const QString &modelPath)
{
    if (s_netCache.contains(modelPath)) {
        VFP_DEBUG << "DnnDetectNode: 使用缓存模型" << modelPath;
        return s_netCache[modelPath];
    }
    VFP_DEBUG << "DnnDetectNode: 加载新模型" << modelPath;
    cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath.toLocal8Bit().constData());
    if (!net.empty()) {
        if (s_netCache.size() >= 10) {
            s_netCache.erase(s_netCache.begin());
        }
        s_netCache[modelPath] = net;
    }
    return net;
}

QStringList DnnDetectNode::loadClassNames(const QString &modelPath,
                                          const QString &explicitPath) const
{
    QString clsPath = explicitPath.trimmed();
    if (clsPath.isEmpty())
        clsPath = QFileInfo(modelPath).absolutePath() + QStringLiteral("/classes.txt");
    QStringList names;
    QFile f(clsPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        while (!ts.atEnd()) {
            const QString line = ts.readLine().trimmed();
            if (!line.isEmpty())
                names.append(line);
        }
    }
    return names;
}

bool DnnDetectNode::parseYoloOutput(const cv::Mat &out, int inputSize, const cv::Size &orig,
                                    double confThresh, double nmsThresh,
                                    QVector<DetectionBox> &result)
{
    result.clear();
    if (out.empty() || inputSize <= 0 || orig.width <= 0 || orig.height <= 0)
        return false;

    // 1) 整理成 [numBoxes, 4+numClasses] 的 2D 浮点矩阵
    cv::Mat m;
    if (out.dims == 3) {
        const int A = out.size[1], B = out.size[2];
        if (A < B)                       // [1, C, N] -> v8：reshape 后转置
            m = out.reshape(1, A).t();
        else                             // [1, N, C] -> v5：直接 reshape
            m = out.reshape(1, A);
    } else if (out.dims == 2) {
        if (out.channels() > 1) {        // [1, N*C] 通道打包 -> reshape 重排为 [N, C]
            const int C = out.channels();
            const int N = static_cast<int>(out.total()) / C;
            if (N * C != static_cast<int>(out.total()))
                return false;
            m = out.reshape(1, N);
        } else if (out.cols >= 5) {       // 已是 [N, C]
            m = out;
        } else {                          // 单行向量 [1, N*C]
            const int C = 4;              // 无法推断类别数，保守按 4 处理（极少触发）
            const int N = static_cast<int>(out.total()) / C;
            m = out.reshape(1, N);
        }
    } else {
        return false;
    }
    if (m.empty()) return false;
    cv::Mat fm;
    m.convertTo(fm, CV_32F);
    m = fm;
    const int cols = m.cols;
    const int numClasses = cols - 4;
    if (numClasses < 1) return false;

    // 2) letterbox 映射参数（原图 -> 输入）
    const double scale = std::min(double(inputSize) / orig.width,
                                  double(inputSize) / orig.height);
    const double padX = (inputSize - orig.width * scale) / 2.0;
    const double padY = (inputSize - orig.height * scale) / 2.0;

    // 3) 置信过滤 + 取最优类别
    std::vector<cv::Rect> rects;
    std::vector<float> scores;
    std::vector<int> clsIds;
    for (int i = 0; i < m.rows; ++i) {
        int bestC = 0;
        float bestS = m.at<float>(i, 4);
        for (int c = 5; c < cols; ++c)
            if (m.at<float>(i, c) > bestS) { bestS = m.at<float>(i, c); bestC = c - 4; }
        if (bestS < confThresh) continue;
        const double cx = m.at<float>(i, 0), cy = m.at<float>(i, 1),
                     w = m.at<float>(i, 2), h = m.at<float>(i, 3);
        const double x0 = cx - w / 2.0, y0 = cy - h / 2.0;
        const double ox = (x0 - padX) / scale;
        const double oy = (y0 - padY) / scale;
        const double ow = w / scale;
        const double oh = h / scale;
        rects.emplace_back(cvRound(ox), cvRound(oy),
                           std::max(1, cvRound(ow)), std::max(1, cvRound(oh)));
        scores.push_back(bestS);
        clsIds.push_back(bestC);
    }

    // 4) NMS
    std::vector<int> keep;
    if (!rects.empty())
        cv::dnn::NMSBoxes(rects, scores, static_cast<float>(confThresh),
                          static_cast<float>(nmsThresh), keep);

    result.reserve(keep.size());
    for (int k : keep) {
        DetectionBox b;
        b.classId = clsIds[k];
        b.confidence = scores[k];
        b.x = rects[k].x;
        b.y = rects[k].y;
        b.w = rects[k].width;
        b.h = rects[k].height;
        result.append(b);
    }
    return true;
}

void DnnDetectNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("检测框"), PortDataType::Array);
    addOutputPort(QStringLiteral("数量"), PortDataType::Number);
    addOutputPort(QStringLiteral("图像(带框)"), PortDataType::Image);
    registerParams({
        makeFilePathParam(QStringLiteral("modelPath"), QString(),
                          QStringLiteral("ONNX 模型文件 (.onnx)")),
        makeFilePathParam(QStringLiteral("classNamesPath"), QString(),
                          QStringLiteral("类别名文件 classes.txt（空则模型同目录）")),
        makeIntParam(QStringLiteral("inputSize"), 640, 32, 2048,
                     QStringLiteral("输入尺寸（宽=高，正方形）")),
        makeDoubleParam(QStringLiteral("confThresh"), 0.25, 0.0, 1.0,
                        QStringLiteral("置信度阈值")),
        makeDoubleParam(QStringLiteral("nmsThresh"), 0.45, 0.0, 1.0,
                        QStringLiteral("NMS 阈值")),
        makeStringParam(QStringLiteral("mean"), QStringLiteral("0,0,0"),
                        QStringLiteral("均值（R,G,B）")),
        makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,
                        QStringLiteral("缩放系数")),
        makeBoolParam(QStringLiteral("swapRB"), true,
                      QStringLiteral("BGR/RGB 交换")),
    });
    m_params[QStringLiteral("detCount")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
}

void DnnDetectNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("detCount")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
    setParamDirect(QStringLiteral("moduleStatus"), false);
    try {
        const QString modelPath =
            m_params.value(QStringLiteral("modelPath"), QString()).toString().trimmed();
        if (modelPath.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置 ONNX 模型路径");
            return;
        }
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        cv::dnn::Net net = getOrLoadNet(modelPath);
        if (net.empty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("ONNX 模型加载失败");
            return;
        }

        const int size = m_params.value(QStringLiteral("inputSize"), 640).toInt();
        const double scale = m_params.value(QStringLiteral("scale"), 1.0 / 255.0).toDouble();
        const bool swapRB = m_params.value(QStringLiteral("swapRB"), true).toBool();
        const QString meanStr = m_params.value(QStringLiteral("mean"), QStringLiteral("0,0,0"))
                                    .toString();
        cv::Scalar mean(0, 0, 0);
        const QStringList parts = meanStr.split(',');
        if (parts.size() == 3)
            mean = cv::Scalar(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());

        cv::Mat img = OpencvUtil::himageToMat(input);
        if (img.empty()) return;
        if (img.channels() == 1)
            cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        else if (img.channels() == 4)
            cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);

        // letterbox 预处理（保持长宽比，灰边填充 114）
        const double lscale = std::min(double(size) / img.cols, double(size) / img.rows);
        const int nw = cvRound(img.cols * lscale), nh = cvRound(img.rows * lscale);
        cv::Mat resized;
        cv::resize(img, resized, cv::Size(nw, nh));
        cv::Mat letter(size, size, CV_8UC3, cv::Scalar(114, 114, 114));
        resized.copyTo(letter(cv::Rect((size - nw) / 2, (size - nh) / 2, nw, nh)));

        cv::Mat blob = cv::dnn::blobFromImage(letter, scale, cv::Size(size, size),
                                              mean, swapRB, false);
        net.setInput(blob);
        cv::Mat out = net.forward();

        const double conf = m_params.value(QStringLiteral("confThresh"), 0.25).toDouble();
        const double nms = m_params.value(QStringLiteral("nmsThresh"), 0.45).toDouble();
        QVector<DetectionBox> boxes;
        if (!parseYoloOutput(out, size, cv::Size(img.cols, img.rows), conf, nms, boxes)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("YOLO 输出解析失败");
            return;
        }

        const QString clsPath =
            m_params.value(QStringLiteral("classNamesPath")).toString().trimmed();
        const QStringList classNames = loadClassNames(modelPath, clsPath);
        for (auto &b : boxes)
            if (b.classId >= 0 && b.classId < classNames.size())
                b.className = classNames[b.classId];

        // 端口1：检测框
        DetectionResult dr;
        dr.boxes = boxes;
        dr.imageWidth = img.cols;
        dr.imageHeight = img.rows;
        auto detObj = QSharedPointer<DataObject>::create();
        detObj->setDetectionResult(dr);
        setOutputData(1, detObj);

        // 端口2：数量
        auto cntObj = QSharedPointer<DataObject>::create();
        cntObj->setValue(double(boxes.size()));
        setOutputData(2, cntObj);

        // 端口3：图像（带框）
        cv::Mat vis = img.clone();
        for (const auto &b : boxes) {
            cv::Rect r(cvRound(b.x), cvRound(b.y), cvRound(b.w), cvRound(b.h));
            cv::rectangle(vis, r, cv::Scalar(0, 220, 0), 2);
            const QString label = QStringLiteral("%1 %2")
                                      .arg(b.className.isEmpty()
                                               ? QString::number(b.classId)
                                               : b.className)
                                      .arg(b.confidence, 0, 'f', 2);
            cv::putText(vis, label.toLocal8Bit().constData(), cv::Point(r.x, std::max(0, r.y - 4)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 220, 0), 1);
        }
        auto imgObj = QSharedPointer<DataObject>::create();
        imgObj->setHImage(OpencvUtil::matToHimage(vis));
        setOutputData(3, imgObj);

        m_params[QStringLiteral("detCount")] = boxes.size();
        setParamDirect(QStringLiteral("moduleStatus"), true);
        m_outputImage = m_inputImage;
    } catch (const std::exception &e) {
        VFP_DEBUG << "DnnDetectNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_outputImage.Clear();
    } catch (...) {
        m_outputImage.Clear();
    }
}

QWidget *DnnDetectNode::createParamPanel()
{
    return createAutoParamPanel();
}

void DnnDetectNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

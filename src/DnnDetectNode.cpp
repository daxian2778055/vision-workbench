#include <QHash>
#include <QMutex>
#include "DnnDetectNode.h"
#include "DnnNetCache.h"
#include <QMutexLocker>
#include "OpencvUtil.h"
#include "AppLog.h"
#include "ProjectPaths.h"   // 179 行：文件路径参数执行侧锚定方案目录
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

QString DnnDetectNode::labelScale()
{
    return QStringLiteral("缩放系数");
}

QString DnnDetectNode::labelSwapRB()
{
    return QStringLiteral("BGR/RGB 交换");
}

QString DnnDetectNode::labelMean()
{
    return QStringLiteral("均值（R,G,B）");
}

QString DnnDetectNode::msgYoloPreprocessFailed()
{
    return QStringLiteral("YOLO 前处理失败：请检查缩放系数（须 > 0）与输入尺寸");
}

bool DnnDetectNode::yoloBlob(const cv::Mat &bgr, int inputSize, double scale, bool swapRB,
                             const cv::Scalar &mean, cv::Mat *blob)
{
    if (!blob || bgr.empty() || inputSize <= 0)
        return false;
    if (!(scale > 0.0))
        return false;   // P1-7：scale=0 会得到全零 blob，直接判失败
    cv::Mat img;
    if (bgr.channels() == 1)
        cv::cvtColor(bgr, img, cv::COLOR_GRAY2BGR);
    else if (bgr.channels() == 4)
        cv::cvtColor(bgr, img, cv::COLOR_BGRA2BGR);
    else
        img = bgr;
    // letterbox：保持长宽比，灰边 114 —— 与 parseYoloOutput 里的 padX/padY 是同一套几何
    const double lscale = std::min(double(inputSize) / img.cols, double(inputSize) / img.rows);
    const int nw = cvRound(img.cols * lscale), nh = cvRound(img.rows * lscale);
    cv::Mat resized;
    cv::resize(img, resized, cv::Size(nw, nh));
    cv::Mat letter(inputSize, inputSize, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(letter(cv::Rect((inputSize - nw) / 2, (inputSize - nh) / 2, nw, nh)));
    *blob = cv::dnn::blobFromImage(letter, scale, cv::Size(inputSize, inputSize),
                                   mean, swapRB, false);
    return !blob->empty();
}

DnnDetectNode::DnnDetectNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ONNX目标检测"));
    m_type = IMAGE_PROCESSING;
}

DnnDetectNode::~DnnDetectNode()
{
}


QStringList DnnDetectNode::loadClassNames(const QString &modelPath,
                                          const QString &explicitPath)
{
    QString clsPath = explicitPath.trimmed();
    if (clsPath.isEmpty())
        clsPath = QFileInfo(modelPath).absolutePath() + QStringLiteral("/classes.txt");

    // 每帧重读 classes.txt 是浪费（r19 P2）：按 (路径, mtime) 缓存，换文件自动重载
    {
        static QMutex s_mu;
        static QHash<QString, QStringList> s_cache;
        const QFileInfo fi(clsPath);
        const QString key = fi.exists()
                                ? (QDir::cleanPath(clsPath) + QLatin1Char('|')
                                   + QString::number(fi.lastModified().toMSecsSinceEpoch()))
                                : (QDir::cleanPath(clsPath) + QStringLiteral("|missing"));
        QMutexLocker<QMutex> locker(&s_mu);
        QHash<QString, QStringList>::const_iterator hit = s_cache.constFind(key);
        if (hit != s_cache.constEnd())
            return hit.value();
        QStringList loaded;
        QFile cf(clsPath);
        if (cf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream cts(&cf);
            while (!cts.atEnd()) {
                const QString line = cts.readLine().trimmed();
                if (!line.isEmpty())
                    loaded.append(line);
            }
        }
        if (s_cache.size() > 32)   // 换型频繁时不无限增长
            s_cache.clear();
        s_cache.insert(key, loaded);
        return loaded;
    }

}

bool DnnDetectNode::parseYoloOutput(const cv::Mat &out, int inputSize, const cv::Size &orig,
                                    double confThresh, double nmsThresh,
                                    QVector<DetectionBox> &result, int yoloLayout)
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
    // 布局（2026-10-28）：v8/v11 = [N, 4+类数]；v5 / YOLOX = [N, 5+类数]（第 4 列是 objectness）。
    // 选错布局的现场表现是"框对了、类别全错"——objectness 被当成第一个类别的分数。
    const bool withObjectness = (yoloLayout == 1);
    const int numClasses = cols - (withObjectness ? 5 : 4);
    if (numClasses < 1) return false;

    // 2) letterbox 映射参数（原图 -> 输入）
    // 必须与 yoloBlob 的放置用同一套整数口径（2026-10-30 r15/r16 余量）：那边先
    // cvRound 出整数边长 nw/nh，再用整数除法定位左上角；这里若用未取整的浮点 pad，
    // 框会有不到 1 个网络像素的系统偏差（实测最大 ~0.75px）。
    const double scale = std::min(double(inputSize) / orig.width,
                                  double(inputSize) / orig.height);
    const int nw = cvRound(orig.width * scale);
    const int nh = cvRound(orig.height * scale);
    const int padXi = (inputSize - nw) / 2;   // 整数除法，与放置一致
    const int padYi = (inputSize - nh) / 2;
    const double padX = padXi;
    const double padY = padYi;

    // 3) 置信过滤 + 取最优类别
    std::vector<cv::Rect> rects;
    std::vector<float> scores;
    std::vector<int> clsIds;
    for (int i = 0; i < m.rows; ++i) {
        int bestC = 0;
        // 无 objectness：第 4 列即类别分数；有 objectness：第 4 列是置信度，
        // 最终分数 = objectness × 类别分数（与 YOLOv5/YOLOX 官方后处理一致）。
        const float obj = withObjectness ? m.at<float>(i, 4) : 1.0f;
        const int firstClsCol = withObjectness ? 5 : 4;
        if (withObjectness && obj < confThresh) continue;
        float bestClsScore = m.at<float>(i, firstClsCol);
        for (int c = firstClsCol + 1; c < cols; ++c)
            if (m.at<float>(i, c) > bestClsScore) {
                bestClsScore = m.at<float>(i, c);
                bestC = c - firstClsCol;
            }
        const float bestS = obj * bestClsScore;
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
                        labelMean()),
        makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,
                        labelScale()),
        makeBoolParam(QStringLiteral("swapRB"), true,
                      labelSwapRB()),
        makeEnumParam(QStringLiteral("dnnBackend"), 0,
                      {QStringLiteral("CPU"), QStringLiteral("OpenCL（GPU）")},
                      QStringLiteral("推理后端")),
        makeEnumParam(QStringLiteral("yoloLayout"), 0,
                      yoloLayoutLabels(),
                      QStringLiteral("输出布局")),
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
        const QString modelPath = ProjectPaths::resolveExecutionPath(
            m_params.value(QStringLiteral("modelPath"), QString()).toString()).trimmed();
        if (modelPath.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置 ONNX 模型路径");
            return;
        }
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        // 模型缓存（2026-10-30 r19 P1-9）：按模型分锁、后端只在加载时设、键含 mtime。
        // 锁只包 setInput/forward —— 前后处理（letterbox/NMS）在锁外，两个流程可并发。
        const bool useOpenCL =
            m_params.value(QStringLiteral("dnnBackend"), 0).toInt() == 1;
        QString netErr;
        std::shared_ptr<DnnNetEntry> entry = DnnNetCache::acquire(modelPath, useOpenCL, &netErr);
        if (!entry || entry->net.empty()) {
            m_params[QStringLiteral("lastError")] =
                netErr.isEmpty() ? DnnNetCache::textLoadFailed() : netErr;
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

        // 前处理收进 yoloBlob（2026-10-29 复核 P1-7）：检测与计数共用同一套 letterbox + blob
        cv::Mat blob;
        if (!yoloBlob(img, size, scale, swapRB, mean, &blob)) {
            m_params[QStringLiteral("lastError")] = msgYoloPreprocessFailed();
            m_outputImage.Clear();
            return;
        }
        cv::Mat out;
        {
            QMutexLocker netLocker(&entry->mutex);   // 同模型串行、不同模型并发
            entry->net.setInput(blob);
            out = entry->net.forward();
        }

        const double conf = m_params.value(QStringLiteral("confThresh"), 0.25).toDouble();
        const double nms = m_params.value(QStringLiteral("nmsThresh"), 0.45).toDouble();
        QVector<DetectionBox> boxes;
        const int layout = m_params.value(QStringLiteral("yoloLayout"), 0).toInt();
        if (!parseYoloOutput(out, size, cv::Size(img.cols, img.rows), conf, nms, boxes, layout)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("YOLO 输出解析失败");
            return;
        }

        const QString clsPath = ProjectPaths::resolveExecutionPath(
            m_params.value(QStringLiteral("classNamesPath")).toString()).trimmed();
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

QStringList DnnDetectNode::yoloLayoutLabels()
{
    return QStringList{QStringLiteral("v8/v11（无 objectness）"),
                       QStringLiteral("v5/YOLOX（带 objectness）")};
}

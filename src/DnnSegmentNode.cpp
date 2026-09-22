#include "DnnSegmentNode.h"
#include "OpencvUtil.h"
#include "AppLog.h"
#include <QWidget>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <algorithm>
#include <vector>

// 静态模型缓存（与 DnnDetectNode / DnnInferNode 隔离：不同任务的特征不同，避免互相淘汰）
QMap<QString, cv::dnn::Net> DnnSegmentNode::s_netCache;

namespace {

/// 把"网络输入空间"的掩膜逆映射回原图尺寸。
/// keepRatio（letterbox）时必须**先裁掉灰边**再缩放：预处理把原图按 s = min(W/w, H/h) 缩放后
/// 居中贴在 (W-nw)/2, (H-nh)/2，此处严格按同一公式反算，否则掩膜会整体错位。
cv::Mat unletterboxMask(const cv::Mat &maskIn, int inputW, int inputH,
                        const cv::Size &orig, bool keepRatio)
{
    if (maskIn.empty() || orig.width <= 0 || orig.height <= 0)
        return cv::Mat();
    cv::Mat full;
    if (maskIn.size() != cv::Size(inputW, inputH))
        cv::resize(maskIn, full, cv::Size(inputW, inputH), 0, 0, cv::INTER_NEAREST);
    else
        full = maskIn;

    cv::Mat cropped = full;
    if (keepRatio) {
        const double s = std::min(double(inputW) / orig.width, double(inputH) / orig.height);
        const int nw = std::max(1, cvRound(orig.width * s));
        const int nh = std::max(1, cvRound(orig.height * s));
        const cv::Rect r = cv::Rect((inputW - nw) / 2, (inputH - nh) / 2, nw, nh)
                           & cv::Rect(0, 0, inputW, inputH);
        if (r.empty())
            return cv::Mat();
        cropped = full(r).clone();
    }
    cv::Mat out;
    cv::resize(cropped, out, orig, 0, 0, cv::INTER_NEAREST);
    return out;
}

/// 8U 单通道数值输出
QSharedPointer<DataObject> numberObject(double v)
{
    auto obj = QSharedPointer<DataObject>::create();
    obj->setValue(v);
    return obj;
}

} // namespace

DnnSegmentNode::DnnSegmentNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ONNX分割"));
    m_type = IMAGE_PROCESSING;
}

DnnSegmentNode::~DnnSegmentNode()
{
}

cv::dnn::Net DnnSegmentNode::getOrLoadNet(const QString &modelPath)
{
    if (s_netCache.contains(modelPath)) {
        VFP_DEBUG << "DnnSegmentNode: 使用缓存模型" << modelPath;
        return s_netCache[modelPath];
    }
    VFP_DEBUG << "DnnSegmentNode: 加载新模型" << modelPath;
    cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath.toLocal8Bit().constData());
    if (!net.empty()) {
        if (s_netCache.size() >= 10) {
            s_netCache.erase(s_netCache.begin());
        }
        s_netCache[modelPath] = net;
    }
    return net;
}

QStringList DnnSegmentNode::loadClassNames(const QString &modelPath,
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

bool DnnSegmentNode::resolveSigmoid(const cv::Mat &out, int mode)
{
    if (mode == 1)
        return true;
    if (mode == 2)
        return false;
    if (out.empty())
        return false;

    // 只对单通道语义输出有意义；多类走 argmax（对单调变换不变），sigmoid 与结果无关
    const cv::Mat src = out.isContinuous() ? out : out.clone();
    int C = 1;
    cv::Mat planes;
    if (src.dims == 4) {
        if (src.size[0] != 1)
            return false;
        C = src.size[1];
        planes = src.reshape(1, C);
    } else if (src.dims == 3) {
        C = src.size[0];
        planes = src.reshape(1, C);
    } else if (src.dims == 2) {
        C = 1;
        planes = src.reshape(1, 1);
    } else {
        return false;
    }
    if (C != 1)
        return false;

    cv::Mat f;
    planes.convertTo(f, CV_32F);
    double mn = 0.0, mx = 0.0;
    cv::minMaxLoc(f, &mn, &mx);
    const double eps = 1e-6;
    return (mn < -eps || mx > 1.0 + eps);   // 越出 [0,1] ⇒ 未压缩的 logits
}

bool DnnSegmentNode::parseSemanticOutput(const cv::Mat &out, int classIndex, double thresh,
                                         bool useSigmoid, cv::Mat &mask)
{
    mask = cv::Mat();
    if (out.empty())
        return false;

    // 统一整理为 [C, H*W] 的浮点平面栈（非连续时先克隆，保证 reshape 与 ptr 逐行可用）
    const cv::Mat src = out.isContinuous() ? out : out.clone();
    int C = 1, H = 0, W = 0;
    cv::Mat planes;
    if (src.dims == 4) {
        if (src.size[0] != 1)             // 仅支持 batch = 1（推理场景即如此）
            return false;
        C = src.size[1]; H = src.size[2]; W = src.size[3];
        planes = src.reshape(1, C);
    } else if (src.dims == 3) {
        C = src.size[0]; H = src.size[1]; W = src.size[2];
        planes = src.reshape(1, C);
    } else if (src.dims == 2) {
        C = 1; H = src.rows; W = src.cols;
        planes = src.reshape(1, 1);
    } else {
        return false;
    }
    if (C < 1 || H <= 0 || W <= 0)
        return false;
    if (src.channels() > 1) {            // 通道被打包成像素通道（非常规导出）→ 不猜
        return false;
    }

    cv::Mat f;
    planes.convertTo(f, CV_32F);
    const int total = H * W;

    cv::Mat m8(H, W, CV_8U);
    if (C == 1) {
        // 二值前景/背景：sigmoid（可关）+ 阈值。thresh=0.5 且开 sigmoid 等价于 logit > 0。
        const float *p = f.ptr<float>(0);
        for (int i = 0; i < total; ++i) {
            double v = double(p[i]);
            if (useSigmoid)
                v = 1.0 / (1.0 + std::exp(-v));
            m8.data[i] = (v > thresh) ? 255 : 0;
        }
        mask = m8;
        return true;
    }

    // 多类：逐像素 argmax（argmax 对逐元素单调变换不变，logits 或 softmax 均可直接吃）
    if (classIndex < 0 || classIndex >= C)
        return false;
    std::vector<const float *> ch(static_cast<size_t>(C));
    for (int c = 0; c < C; ++c)
        ch[static_cast<size_t>(c)] = f.ptr<float>(c);
    for (int i = 0; i < total; ++i) {
        int best = 0;
        float bestV = ch[0][i];
        for (int c = 1; c < C; ++c) {
            if (ch[static_cast<size_t>(c)][i] > bestV) {
                bestV = ch[static_cast<size_t>(c)][i];
                best = c;
            }
        }
        m8.data[i] = (best == classIndex) ? 255 : 0;
    }
    mask = m8;
    return true;
}

bool DnnSegmentNode::parseYoloSegOutput(const cv::Mat &out0, const cv::Mat &out1,
                                        int inputW, int inputH, double confThresh, double nmsThresh,
                                        cv::Mat &mask, QVector<DetectionBox> &boxes)
{
    mask = cv::Mat();
    boxes.clear();
    if (out0.empty() || out1.empty() || inputW <= 0 || inputH <= 0)
        return false;

    // 1) 掩膜原型 [1, K, mh, mw]（也接受 [K, mh, mw]）→ [K, mh*mw]
    const cv::Mat proto = out1.isContinuous() ? out1 : out1.clone();
    int K = 0, mh = 0, mw = 0;
    if (proto.dims == 4) {
        if (proto.size[0] != 1)
            return false;
        K = proto.size[1]; mh = proto.size[2]; mw = proto.size[3];
    } else if (proto.dims == 3) {
        K = proto.size[0]; mh = proto.size[1]; mw = proto.size[2];
    } else {
        return false;
    }
    if (K < 1 || mh <= 0 || mw <= 0)
        return false;
    cv::Mat p32;
    proto.reshape(1, K).convertTo(p32, CV_32F);

    // 2) 系数 + 框 [1, R, N] → [N, R]
    const cv::Mat c0 = out0.isContinuous() ? out0 : out0.clone();
    if (c0.dims != 3)
        return false;
    const int d1 = c0.size[1], d2 = c0.size[2];
    const int R = std::min(d1, d2);      // 通道维（真实模型 116 << 8400）
    const int N = std::max(d1, d2);      // 锚点数
    if (R <= 4 + K)
        return false;
    const int nc = R - 4 - K;            // 类别数
    const cv::Mat transposed = c0.reshape(1, R).t();   // MatExpr 先落成 Mat（MatExpr 无 convertTo）
    cv::Mat f;
    transposed.convertTo(f, CV_32F);

    // 3) 置信过滤 + 全局 NMS（与 DnnDetectNode 同口径）
    std::vector<cv::Rect> rects;
    std::vector<float> scores;
    std::vector<int> clsIds;
    std::vector<std::vector<float>> coeffs;
    for (int i = 0; i < N; ++i) {
        const float *row = f.ptr<float>(i);
        int bestC = 0;
        float bestS = row[4];
        for (int c = 1; c < nc; ++c) {
            if (row[4 + c] > bestS) {
                bestS = row[4 + c];
                bestC = c;
            }
        }
        if (bestS < confThresh)
            continue;
        const double cx = row[0], cy = row[1], w = row[2], h = row[3];
        rects.emplace_back(cvRound(cx - w / 2.0), cvRound(cy - h / 2.0),
                           std::max(1, cvRound(w)), std::max(1, cvRound(h)));
        scores.push_back(bestS);
        clsIds.push_back(bestC);
        coeffs.emplace_back(row + 4 + nc, row + R);
    }

    std::vector<int> keep;
    if (!rects.empty())
        cv::dnn::NMSBoxes(rects, scores, static_cast<float>(confThresh),
                          static_cast<float>(nmsThresh), keep);

    // 4) 逐实例：系数 · 原型 > 0 → 按框裁剪 → 缩放贴回输入空间掩膜
    mask = cv::Mat::zeros(inputH, inputW, CV_8U);
    const double sx = double(mw) / double(inputW);
    const double sy = double(mh) / double(inputH);
    std::vector<const float *> protoCh(static_cast<size_t>(K));
    for (int c = 0; c < K; ++c)
        protoCh[static_cast<size_t>(c)] = p32.ptr<float>(c);

    for (int k : keep) {
        cv::Mat inst(mh, mw, CV_8U);
        for (int y = 0; y < mh; ++y) {
            const int base = y * mw;
            for (int x = 0; x < mw; ++x) {
                float v = 0.f;
                for (int c = 0; c < K; ++c)
                    v += coeffs[static_cast<size_t>(k)][static_cast<size_t>(c)]
                         * protoCh[static_cast<size_t>(c)][base + x];
                inst.at<uchar>(y, x) = (v > 0.f) ? 255 : 0;   // 严格大于：= 0 视为背景
            }
        }

        const cv::Rect &r = rects[static_cast<size_t>(k)];
        const cv::Rect pr = cv::Rect(cvRound(r.x * sx), cvRound(r.y * sy),
                                     std::max(1, cvRound(r.width * sx)),
                                     std::max(1, cvRound(r.height * sy)))
                            & cv::Rect(0, 0, mw, mh);
        if (pr.empty())
            continue;
        cv::Mat crop = inst(pr).clone();
        cv::resize(crop, crop, cv::Size(r.width, r.height), 0, 0, cv::INTER_NEAREST);

        const cv::Rect dst = r & cv::Rect(0, 0, inputW, inputH);
        if (dst.empty())
            continue;
        cv::Mat sub = crop(cv::Rect(dst.x - r.x, dst.y - r.y, dst.width, dst.height));
        cv::Mat roi = mask(dst);
        cv::bitwise_or(roi, sub, roi);

        DetectionBox b;
        b.classId = clsIds[static_cast<size_t>(k)];
        b.confidence = scores[static_cast<size_t>(k)];
        b.x = dst.x; b.y = dst.y; b.w = dst.width; b.h = dst.height;
        boxes.append(b);
    }
    return true;
}

int DnnSegmentNode::maskToBoxes(cv::Mat &mask, int minArea, QVector<DetectionBox> &boxes)
{
    boxes.clear();
    if (mask.empty() || mask.type() != CV_8U)
        return 0;

    cv::Mat bin;
    cv::threshold(mask, bin, 0, 255, cv::THRESH_BINARY);
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8);

    cv::Mat cleaned = cv::Mat::zeros(mask.size(), CV_8U);
    int kept = 0;
    if (n > 1) {
        for (int i = 1; i < n; ++i) {   // 0 号是背景
            const int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area < minArea)
                continue;
            const cv::Rect r(stats.at<int>(i, cv::CC_STAT_LEFT),
                             stats.at<int>(i, cv::CC_STAT_TOP),
                             stats.at<int>(i, cv::CC_STAT_WIDTH),
                             stats.at<int>(i, cv::CC_STAT_HEIGHT));
            const cv::Rect rr = r & cv::Rect(0, 0, mask.cols, mask.rows);
            if (rr.empty())
                continue;
            // 连通域外接框之外不可能属于该标签，故只在该 ROI 内比较即等价于全图比较
            cv::Mat roi = labels(rr);
            cv::Mat one = (roi == i);
            cv::Mat dst = cleaned(rr);
            cv::bitwise_or(dst, one, dst);

            DetectionBox b;
            b.classId = -1;
            b.confidence = area;    // 连通域模式下 confidence 承载面积（见头文件约定）
            b.x = rr.x; b.y = rr.y; b.w = rr.width; b.h = rr.height;
            boxes.append(b);
            ++kept;
        }
    }
    mask = cleaned;                 // 原地回写：不达标的小域从掩膜里一并去掉，四者保持一致
    return kept;
}

void DnnSegmentNode::init()
{
    HalconNode::init();
    // 端口 0（基类输出图）＝ 掩膜图像：8U 0/255、原图尺寸
    addOutputPort(QStringLiteral("面积"), PortDataType::Number);
    addOutputPort(QStringLiteral("数量"), PortDataType::Number);
    addOutputPort(QStringLiteral("目标框"), PortDataType::Array);
    addOutputPort(QStringLiteral("图像(叠加)"), PortDataType::Image);
    registerParams({
        makeFilePathParam(QStringLiteral("modelPath"), QString(),
                          QStringLiteral("ONNX 模型文件 (.onnx)")),
        makeFilePathParam(QStringLiteral("classNamesPath"), QString(),
                          QStringLiteral("类别名文件 classes.txt（空则模型同目录；仅实例模式用）")),
        makeEnumParam(QStringLiteral("segMode"), 0,
                      {QStringLiteral("自动"), QStringLiteral("语义分割"), QStringLiteral("实例分割(YOLO-Seg)")},
                      QStringLiteral("分割模式")),
        makeIntParam(QStringLiteral("inputWidth"), 640, 32, 4096,
                     QStringLiteral("输入宽"), QStringLiteral("px")),
        makeIntParam(QStringLiteral("inputHeight"), 640, 32, 4096,
                     QStringLiteral("输入高"), QStringLiteral("px")),
        makeBoolParam(QStringLiteral("keepRatio"), true,
                      QStringLiteral("保持长宽比（letterbox，关则拉伸）")),
        makeIntParam(QStringLiteral("classIndex"), 1, 0, 255,
                     QStringLiteral("前景类索引（语义多类：argmax 等于它即前景）")),
        makeDoubleParam(QStringLiteral("binaryThresh"), 0.5, 0.0, 1.0,
                        QStringLiteral("二值阈值（单通道语义）")),
        makeEnumParam(QStringLiteral("sigmoidMode"), 0,
                      {QStringLiteral("自动"), QStringLiteral("强制开"), QStringLiteral("强制关")},
                      QStringLiteral("单通道输出 sigmoid（自动=按值域判断模型是否已内置）")),
        makeDoubleParam(QStringLiteral("confThresh"), 0.25, 0.0, 1.0,
                        QStringLiteral("置信度阈值（实例模式）")),
        makeDoubleParam(QStringLiteral("nmsThresh"), 0.45, 0.0, 1.0,
                        QStringLiteral("NMS 阈值（实例模式）")),
        makeIntParam(QStringLiteral("minArea"), 0, 0, 100000000,
                     QStringLiteral("最小连通域面积（低于则从掩膜剔除）"), QStringLiteral("px")),
        makeStringParam(QStringLiteral("mean"), QStringLiteral("0,0,0"),
                        QStringLiteral("均值（R,G,B）")),
        makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,
                        QStringLiteral("缩放系数")),
        makeBoolParam(QStringLiteral("swapRB"), true,
                      QStringLiteral("BGR/RGB 交换")),
    });
    m_params[QStringLiteral("segmentCount")] = 0;
    m_params[QStringLiteral("maskArea")] = 0;
    m_params[QStringLiteral("sigmoidApplied")] = false;
    m_params[QStringLiteral("lastError")] = QString();
}

void DnnSegmentNode::run(bool /*autoSwitch*/)
{
    // 失败/空输入都走同一条收口：清输出 + 清统计，避免下游读到上一轮残留（E2 同类问题）
    auto fail = [&](const QString &msg) {
        m_params[QStringLiteral("lastError")] = msg;
        m_params[QStringLiteral("segmentCount")] = 0;
        m_params[QStringLiteral("maskArea")] = 0;
        m_params[QStringLiteral("sigmoidApplied")] = false;
        m_outputImage.Clear();
        for (int p = 1; p <= 4; ++p)
            setOutputData(p, QSharedPointer<DataObject>());
        setParamDirect(QStringLiteral("moduleStatus"), false);
    };

    m_params[QStringLiteral("segmentCount")] = 0;
    m_params[QStringLiteral("maskArea")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
    setParamDirect(QStringLiteral("moduleStatus"), false);

    try {
        const QString modelPath =
            m_params.value(QStringLiteral("modelPath"), QString()).toString().trimmed();
        if (modelPath.isEmpty()) {
            fail(QStringLiteral("请设置 ONNX 模型路径"));
            return;
        }
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            // 无输入图像：不算失败（上游未产出图像），只清输出，与 DnnDetectNode 同口径
            m_outputImage.Clear();
            for (int p = 1; p <= 4; ++p)
                setOutputData(p, QSharedPointer<DataObject>());
            return;
        }
        cv::dnn::Net net = getOrLoadNet(modelPath);
        if (net.empty()) {
            fail(QStringLiteral("ONNX 模型加载失败"));
            return;
        }

        cv::Mat img = OpencvUtil::himageToMat(input);
        if (img.empty()) {
            fail(QStringLiteral("输入图像读取失败"));
            return;
        }
        if (img.channels() == 1)
            cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        else if (img.channels() == 4)
            cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);

        const int inputW = std::max(32, m_params.value(QStringLiteral("inputWidth"), 640).toInt());
        const int inputH = std::max(32, m_params.value(QStringLiteral("inputHeight"), 640).toInt());
        const bool keepRatio = m_params.value(QStringLiteral("keepRatio"), true).toBool();
        const double scale = m_params.value(QStringLiteral("scale"), 1.0 / 255.0).toDouble();
        const bool swapRB = m_params.value(QStringLiteral("swapRB"), true).toBool();
        cv::Scalar mean(0, 0, 0);
        const QStringList parts =
            m_params.value(QStringLiteral("mean"), QStringLiteral("0,0,0")).toString().split(',');
        if (parts.size() == 3)
            mean = cv::Scalar(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());

        // 预处理：与 unletterboxMask 严格同一套几何（keepRatio → letterbox 灰边 114）
        cv::Mat blobSrc;
        if (keepRatio) {
            const double s = std::min(double(inputW) / img.cols, double(inputH) / img.rows);
            const int nw = std::max(1, cvRound(img.cols * s));
            const int nh = std::max(1, cvRound(img.rows * s));
            cv::Mat resized;
            cv::resize(img, resized, cv::Size(nw, nh));
            cv::Mat letter(inputH, inputW, CV_8UC3, cv::Scalar(114, 114, 114));
            resized.copyTo(letter(cv::Rect((inputW - nw) / 2, (inputH - nh) / 2, nw, nh)));
            blobSrc = letter;
        } else {
            cv::resize(img, blobSrc, cv::Size(inputW, inputH));
        }

        cv::Mat blob = cv::dnn::blobFromImage(blobSrc, scale, cv::Size(inputW, inputH),
                                              mean, swapRB, false);
        net.setInput(blob);
        std::vector<cv::String> outNames = net.getUnconnectedOutLayersNames();
        std::vector<cv::Mat> outs;
        net.forward(outs, outNames);      // 单/双输出都走同一条路
        if (outs.empty()) {
            fail(QStringLiteral("模型未产出任何输出"));
            return;
        }

        // 模式判定：4D 且通道数较小（掩膜原型）＋ 3D（系数/框）→ 实例分割
        const int mode = m_params.value(QStringLiteral("segMode"), 0).toInt();   // 0 自动 1 语义 2 实例
        int protoIdx = -1, coeffIdx = -1;
        for (int i = 0; i < static_cast<int>(outs.size()); ++i) {
            if (outs[static_cast<size_t>(i)].dims == 4 && outs[static_cast<size_t>(i)].size[0] == 1
                && outs[static_cast<size_t>(i)].size[1] <= 64) {
                protoIdx = i;
            } else if (outs[static_cast<size_t>(i)].dims == 3) {
                coeffIdx = i;
            }
        }
        const bool useInstance = (mode == 2) || (mode == 0 && protoIdx >= 0 && coeffIdx >= 0);

        const int classIndex = m_params.value(QStringLiteral("classIndex"), 1).toInt();
        const double binaryThresh = m_params.value(QStringLiteral("binaryThresh"), 0.5).toDouble();
        const int sigmoidMode = m_params.value(QStringLiteral("sigmoidMode"), 0).toInt();
        const double conf = m_params.value(QStringLiteral("confThresh"), 0.25).toDouble();
        const double nms = m_params.value(QStringLiteral("nmsThresh"), 0.45).toDouble();

        cv::Mat maskInput;                // 网络输入空间掩膜
        QVector<DetectionBox> instBoxes;  // 网络输入空间框（仅实例模式）
        if (useInstance) {
            if (protoIdx < 0 || coeffIdx < 0) {
                fail(QStringLiteral("未找到 YOLO-Seg 双输出（系数 + 原型）；请改选语义分割模式或核对模型"));
                return;
            }
            if (!parseYoloSegOutput(outs[static_cast<size_t>(coeffIdx)],
                                    outs[static_cast<size_t>(protoIdx)],
                                    inputW, inputH, conf, nms, maskInput, instBoxes)) {
                fail(QStringLiteral("YOLO-Seg 输出解析失败（检查输入尺寸/类别数与模型是否匹配）"));
                return;
            }
        } else {
            int si = -1;
            for (int i = 0; i < static_cast<int>(outs.size()); ++i) {
                if (outs[static_cast<size_t>(i)].dims == 4) {
                    si = i;
                    break;
                }
            }
            if (si < 0)
                si = 0;
            // sigmoid 三态：自动按值域判定模型是否已内置 Sigmoid
            // （实测教训：已含 Sigmoid 的模型再压一次 → 概率背景 0.5 被判成前景 → 掩膜整幅变亮）
            const bool useSigmoid = resolveSigmoid(outs[static_cast<size_t>(si)], sigmoidMode);
            m_params[QStringLiteral("sigmoidApplied")] = useSigmoid;
            if (!parseSemanticOutput(outs[static_cast<size_t>(si)], classIndex, binaryThresh,
                                     useSigmoid, maskInput)) {
                fail(QStringLiteral("语义分割输出解析失败（检查输出通道数/前景类索引）"));
                return;
            }
        }

        // 逆映射回原图（先裁灰边再缩放，与预处理同一几何）→ minArea 清理
        cv::Mat maskOrig = unletterboxMask(maskInput, inputW, inputH,
                                           cv::Size(img.cols, img.rows), keepRatio);
        if (maskOrig.empty()) {
            fail(QStringLiteral("掩膜逆映射失败"));
            return;
        }
        const int minArea = m_params.value(QStringLiteral("minArea"), 0).toInt();
        QVector<DetectionBox> compBoxes;
        const int compCount = maskToBoxes(maskOrig, minArea, compBoxes);
        const int maskArea = cv::countNonZero(maskOrig);

        QVector<DetectionBox> finalBoxes = compBoxes;
        int outCount = compCount;
        if (useInstance) {
            // 实例模式：框数组用实例框（携带类别/置信度），面积门槛按"框内掩膜像素数"过滤
            const QString clsPath =
                m_params.value(QStringLiteral("classNamesPath")).toString().trimmed();
            const QStringList classNames = loadClassNames(modelPath, clsPath);
            QVector<DetectionBox> keptInst;
            const cv::Rect imgRect(0, 0, img.cols, img.rows);
            for (const DetectionBox &b : instBoxes) {
                const cv::Rect r(cvRound(b.x), cvRound(b.y), cvRound(b.w), cvRound(b.h));
                const cv::Rect ir = r & imgRect;
                if (ir.empty())
                    continue;
                if (cv::countNonZero(maskOrig(ir)) < minArea)
                    continue;
                DetectionBox kb = b;
                kb.x = ir.x; kb.y = ir.y; kb.w = ir.width; kb.h = ir.height;
                if (kb.classId >= 0 && kb.classId < classNames.size())
                    kb.className = classNames[kb.classId];
                keptInst.append(kb);
            }
            finalBoxes = keptInst;
            outCount = keptInst.size();
        }

        // 端口 1/2：面积、数量
        setOutputData(1, numberObject(maskArea));
        setOutputData(2, numberObject(outCount));

        // 端口 3：目标框数组（语义模式＝连通域框，实例模式＝实例框）
        DetectionResult dr;
        dr.boxes = finalBoxes;
        dr.imageWidth = img.cols;
        dr.imageHeight = img.rows;
        auto arrObj = QSharedPointer<DataObject>::create();
        arrObj->setDetectionResult(dr);
        setOutputData(3, arrObj);

        // 端口 4：叠加图（半透明着色 + 外轮廓）
        cv::Mat vis = img.clone();
        cv::Mat colored = vis.clone();
        colored.setTo(cv::Scalar(0, 180, 255), maskOrig);
        cv::addWeighted(colored, 0.45, vis, 0.55, 0.0, vis);
        if (maskArea > 0) {
            std::vector<std::vector<cv::Point>> contours;
            cv::Mat bin = maskOrig.clone();
            cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            cv::drawContours(vis, contours, -1, cv::Scalar(0, 220, 0), 2);
        }
        auto visObj = QSharedPointer<DataObject>::create();
        visObj->setHImage(OpencvUtil::matToHimage(vis));
        setOutputData(4, visObj);

        // 端口 0（基类输出图）：掩膜图像
        m_outputImage = OpencvUtil::matToHimage(maskOrig);

        m_params[QStringLiteral("segmentCount")] = outCount;
        m_params[QStringLiteral("maskArea")] = maskArea;
        setParamDirect(QStringLiteral("moduleStatus"), true);
    } catch (const std::exception &e) {
        VFP_DEBUG << "DnnSegmentNode error:" << e.what();
        fail(QString::fromLocal8Bit(e.what()));
    } catch (...) {
        fail(QStringLiteral("未知异常"));
    }
}

QWidget *DnnSegmentNode::createParamPanel()
{
    return createAutoParamPanel();
}

void DnnSegmentNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

#include "DnnInferNode.h"
#include "OpencvUtil.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDir>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

// 静态模型缓存
QMap<QString, cv::dnn::Net> DnnInferNode::s_netCache;

DnnInferNode::DnnInferNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("ONNX深度学习推理"));
    m_type = IMAGE_PROCESSING;
}

DnnInferNode::~DnnInferNode()
{
    // 析构时清除当前节点的缓存引用
    // 注意：不删除静态缓存，因为其他节点可能也在使用
}

cv::dnn::Net DnnInferNode::getOrLoadNet(const QString &modelPath)
{
    // 检查缓存中是否已有该模型
    if (s_netCache.contains(modelPath)) {
        VFP_DEBUG << "DnnInferNode: 使用缓存模型" << modelPath;
        return s_netCache[modelPath];
    }

    // 加载新模型
    VFP_DEBUG << "DnnInferNode: 加载新模型" << modelPath;
    cv::dnn::Net net = cv::dnn::readNetFromONNX(modelPath.toLocal8Bit().constData());

    if (!net.empty()) {
        // 缓存模型（限制缓存大小，最多保存 10 个模型）
        if (s_netCache.size() >= 10) {
            // 移除最旧的缓存项
            auto it = s_netCache.begin();
            s_netCache.erase(it);
        }
        s_netCache[modelPath] = net;
    }

    return net;
}

void DnnInferNode::clearNetCache()
{
    s_netCache.clear();
}

void DnnInferNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("推理结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("modelPath"), QString(),
                          QStringLiteral("ONNX 模型文件 (.onnx)")),
        makeIntParam(QStringLiteral("inputSize"), 224, 32, 1024,
                     QStringLiteral("输入尺寸（宽=高）")),
        makeStringParam(QStringLiteral("mean"), QStringLiteral("0,0,0"),
                        QStringLiteral("均值（R,G,B）")),
        makeDoubleParam(QStringLiteral("scale"), 1.0 / 255.0, 0.0001, 10.0,
                        QStringLiteral("缩放系数")),
        makeBoolParam(QStringLiteral("swapRB"), true,
                      QStringLiteral("BGR/RGB 交换（HALCON 图为 RGB 序）")),
        makeIntParam(QStringLiteral("topK"), 1, 1, 10, QStringLiteral("输出前 K 个类别")),
        makeFilePathParam(QStringLiteral("classNamesPath"), QString(),
                          QStringLiteral("类别名文件 classes.txt（空则模型同目录）")),
    });
    m_params[QStringLiteral("topClass")] = -1;
    m_params[QStringLiteral("topClassName")] = QString();
    m_params[QStringLiteral("topConfidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
}

void DnnInferNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("topClass")] = -1;
    m_params[QStringLiteral("topConfidence")] = 0.0;
    m_params[QStringLiteral("lastError")] = QString();
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        const QString modelPath =
            m_params.value(QStringLiteral("modelPath"), QString()).toString().trimmed();
        if (modelPath.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置 ONNX 模型路径");
            m_params["moduleStatus"] = false;
            return;
        }

        // 使用缓存加载模型
        cv::dnn::Net net = getOrLoadNet(modelPath);
        if (net.empty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("ONNX 模型加载失败");
            m_params["moduleStatus"] = false;
            return;
        }

        const int size = m_params.value(QStringLiteral("inputSize"), 224).toInt();
        const double scale = m_params.value(QStringLiteral("scale"), 1.0 / 255.0).toDouble();
        const bool swapRB = m_params.value(QStringLiteral("swapRB"), true).toBool();
        const QString meanStr = m_params.value(QStringLiteral("mean"), QStringLiteral("0,0,0"))
                                    .toString();
        cv::Scalar mean(0, 0, 0);
        const QStringList parts = meanStr.split(',');
        if (parts.size() == 3) {
            mean = cv::Scalar(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());
        }

        cv::Mat img = OpencvUtil::himageToMat(input);
        if (img.empty()) {
            m_params["moduleStatus"] = false;
            return;
        }
        if (img.channels() == 1)
            cv::cvtColor(img, img, cv::COLOR_GRAY2BGR);
        else if (img.channels() == 4)
            cv::cvtColor(img, img, cv::COLOR_BGRA2BGR);

        cv::Mat blob = cv::dnn::blobFromImage(img, scale, cv::Size(size, size), mean,
                                              swapRB, false);
        net.setInput(blob);
        cv::Mat out = net.forward();
        // 输出展平为一维向量
        cv::Mat flat = out.reshape(1, 1);
        if (flat.cols < 2) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("模型输出维度异常");
            m_params["moduleStatus"] = false;
            return;
        }
        // softmax
        float mx = -1e30f;
        for (int i = 0; i < flat.cols; ++i)
            mx = std::max(mx, flat.at<float>(0, i));
        std::vector<float> prob(flat.cols);
        float sum = 0.f;
        for (int i = 0; i < flat.cols; ++i) {
            prob[i] = std::exp(flat.at<float>(0, i) - mx);
            sum += prob[i];
        }
        if (sum > 0.f)
            for (auto &p : prob) p /= sum;
        // topK
        const int topK = std::max(1, m_params.value(QStringLiteral("topK"), 1).toInt());
        std::vector<int> idx(prob.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
        std::partial_sort(idx.begin(), idx.begin() + std::min(topK, (int)idx.size()),
                          idx.end(), [&](int a, int b) { return prob[a] > prob[b]; });

        QStringList classNames;
        QString clsPath = m_params.value(QStringLiteral("classNamesPath")).toString().trimmed();
        if (clsPath.isEmpty())
            clsPath = QFileInfo(modelPath).absolutePath() + QStringLiteral("/classes.txt");
        QFile clsFile(clsPath);
        if (clsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&clsFile);
            while (!ts.atEnd()) {
                const QString line = ts.readLine().trimmed();
                if (!line.isEmpty())
                    classNames.append(line);
            }
        }
        const QString className = (idx[0] >= 0 && idx[0] < classNames.size())
            ? classNames[idx[0]] : QString::number(idx[0]);

        m_params[QStringLiteral("topClass")] = idx[0];
        m_params[QStringLiteral("topClassName")] = className;
        m_params[QStringLiteral("topConfidence")] = prob[idx[0]];
        m_params[QStringLiteral("topKClasses")] = className;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        MeasureResult res;
        res.type = QStringLiteral("dnn");
        res.valueName = className.isEmpty() ? QStringLiteral("类别") : className;
        res.valid = true;
        res.value = idx[0];
        res.extraValues = QVector<double>({prob[idx[0]]});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "DnnInferNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *DnnInferNode::createParamPanel()
{
    return createAutoParamPanel();
}

void DnnInferNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

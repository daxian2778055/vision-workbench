#include "ObjectCountingNode.h"

#include "DataObject.h"
#include "DnnDetectNode.h"      // 复用 YOLO 的加载与解码
#include "OpencvUtil.h"
#include "PathSafety.h"
#include "ProjectPaths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

ObjectCountingNode::ObjectCountingNode(QObject *parent) : HalconNode(parent)
{
    m_type = OUTPUT;
}

void ObjectCountingNode::init()
{
    HalconNode::init();
    m_tracker.reset();
    resetCounting();

    {
        ParamSpec sp;
        sp.name = QStringLiteral("modelPath");
        sp.type = ParamType::FilePath;
        sp.defaultValue = QString();
        sp.label = QStringLiteral("YOLO ONNX 模型");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("classNamesPath");
        sp.type = ParamType::FilePath;
        sp.defaultValue = QString();
        sp.label = QStringLiteral("类别名文件 classes.txt（空则取模型同目录）");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("inputSize");
        sp.type = ParamType::Int;
        sp.defaultValue = 640;
        sp.minValue = 32;
        sp.maxValue = 2048;
        sp.hasRange = true;
        sp.label = QStringLiteral("网络输入尺寸");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("confThresh");
        sp.type = ParamType::Double;
        sp.defaultValue = 0.25;
        sp.minValue = 0.0;
        sp.maxValue = 1.0;
        sp.hasRange = true;
        sp.label = QStringLiteral("置信度阈值");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("nmsThresh");
        sp.type = ParamType::Double;
        sp.defaultValue = 0.45;
        sp.minValue = 0.0;
        sp.maxValue = 1.0;
        sp.hasRange = true;
        sp.label = QStringLiteral("NMS 阈值");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("lineA_x");
        sp.type = ParamType::Int;
        sp.defaultValue = 200;
        sp.label = QStringLiteral("计数线 A 点 X");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("lineA_y");
        sp.type = ParamType::Int;
        sp.defaultValue = 0;
        sp.label = QStringLiteral("计数线 A 点 Y");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("lineB_x");
        sp.type = ParamType::Int;
        sp.defaultValue = 200;
        sp.label = QStringLiteral("计数线 B 点 X");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("lineB_y");
        sp.type = ParamType::Int;
        sp.defaultValue = 480;
        sp.label = QStringLiteral("计数线 B 点 Y");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("direction");
        sp.type = ParamType::Enum;
        sp.defaultValue = 0;
        sp.enumValues = QStringList{QStringLiteral("双向"), QStringLiteral("A→B 侧"),
                                    QStringLiteral("B→A 侧")};
        sp.label = QStringLiteral("计数方向");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("minHits");
        sp.type = ParamType::Int;
        sp.defaultValue = 3;
        sp.minValue = 1;
        sp.maxValue = 50;
        sp.hasRange = true;
        sp.label = QStringLiteral("确认所需帧数（去抖）");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("maxMissed");
        sp.type = ParamType::Int;
        sp.defaultValue = 15;
        sp.minValue = 1;
        sp.maxValue = 200;
        sp.hasRange = true;
        sp.label = QStringLiteral("允许连续漏检帧数");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("dnnBackend");
        sp.type = ParamType::Enum;
        sp.defaultValue = 0;
        sp.enumValues = QStringList{QStringLiteral("CPU"), QStringLiteral("OpenCL（GPU）")};
        sp.label = QStringLiteral("推理后端");
        sp.tooltip = QStringLiteral("OpenCL 需 OpenCV 支持；无可用设备时自动回退 CPU");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("yoloLayout");
        sp.type = ParamType::Enum;
        sp.defaultValue = 0;
        sp.enumValues = DnnDetectNode::yoloLayoutLabels();
        sp.label = QStringLiteral("输出布局");
        sp.tooltip = QStringLiteral("与导出模型对应：选错会表现为框对了但类别全错");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("resetOnStart");
        sp.type = ParamType::Bool;
        sp.defaultValue = true;
        sp.label = QStringLiteral("每次开始执行时清零累计");
        sp.tooltip = QStringLiteral("按批次计数时保持开启；要跨批次累计就关掉");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("resetCounter");
        sp.type = ParamType::Bool;
        sp.defaultValue = false;
        sp.label = QStringLiteral("下一次运行前清零累计");
        registerParam(sp);
    }
    m_params[QStringLiteral("countNote")] = QString();
}

void ObjectCountingNode::resetCounting()
{
    m_totalCount = 0;
    m_perClassCount.clear();
    m_tracker.reset();
    m_lineCounter = LineCounter();
    m_lastLineKey.clear();
    m_initedLine = false;
}

void ObjectCountingNode::onRoundStart()
{
    if (m_params.value(QStringLiteral("resetOnStart"), true).toBool())
        resetCounting();
}

void ObjectCountingNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("lastError")] = QString();
    setParamDirect(QStringLiteral("moduleStatus"), false);

    if (m_params.value(QStringLiteral("resetCounter")).toBool()) {
        resetCounting();
        setParamDirect(QStringLiteral("resetCounter"), false);
    }

    if (!m_inputImage.IsInitialized()) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("没有输入图像");
        return;
    }
    const cv::Mat img = OpencvUtil::himageToMat(m_inputImage);
    if (img.empty()) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("计数节点：输入图像不可用（为空或桥接失败）");
        return;
    }

    // 计数线变化才重建（参数面板改了就重建，避免用旧线计数）
    const QString lineKey = QStringLiteral("%1,%2,%3,%4")
                                .arg(m_params.value(QStringLiteral("lineA_x")).toInt())
                                .arg(m_params.value(QStringLiteral("lineA_y")).toInt())
                                .arg(m_params.value(QStringLiteral("lineB_x")).toInt())
                                .arg(m_params.value(QStringLiteral("lineB_y")).toInt());
    if (!m_initedLine || lineKey != m_lastLineKey) {
        const cv::Point2f a(float(m_params.value(QStringLiteral("lineA_x")).toInt()),
                            float(m_params.value(QStringLiteral("lineA_y")).toInt()));
        const cv::Point2f b(float(m_params.value(QStringLiteral("lineB_x")).toInt()),
                            float(m_params.value(QStringLiteral("lineB_y")).toInt()));
        m_lineCounter.setLine(a, b);
        m_lastLineKey = lineKey;
        m_initedLine = true;
    }
    if (!m_lineCounter.valid()) {
        m_params[QStringLiteral("lastError")] =
            QStringLiteral("计数线长度为 0：请设置不同的 A/B 点");
        return;
    }

    const QString modelPath = ProjectPaths::resolveExecutionPath(
        m_params.value(QStringLiteral("modelPath")).toString()).trimmed();
    if (modelPath.isEmpty()) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("未配置 YOLO 模型");
        return;
    }


    // ── 检测（复用 DnnDetectNode 的加载与解码，避免复制 YOLO 前处理）──
    QVector<DetectionBox> boxes;
    try {
        cv::dnn::Net net = DnnDetectNode::getOrLoadNet(modelPath);
        if (net.empty()) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("模型加载失败：%1").arg(modelPath);
            return;
        }
        // 推理后端（2026-10-28）：产线逐帧跑 YOLO，CPU 常是瓶颈；OpenCL 目标可能被
        // OpenCV 静默回退，这里如实记下"实际用了哪种"。
        const int backend = m_params.value(QStringLiteral("dnnBackend")).toInt();
        if (backend == 1) {
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_OPENCL);
        } else {
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        }
        const int inputSize = m_params.value(QStringLiteral("inputSize")).toInt();
        const double scale = m_params.value(QStringLiteral("scale")).toDouble();
        const bool swapRB = m_params.value(QStringLiteral("swapRB")).toBool();
        // mean 形如 "0,0,0"
        cv::Scalar mean(0, 0, 0);
        const QStringList parts =
            m_params.value(QStringLiteral("mean")).toString().split(QLatin1Char(','));
        if (parts.size() == 3)
            mean = cv::Scalar(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble());

        cv::Mat blob = cv::dnn::blobFromImage(img, scale, cv::Size(inputSize, inputSize),
                                              mean, swapRB, false, CV_32F);
        net.setInput(blob);
        const cv::Mat out = net.forward();

        const double conf = m_params.value(QStringLiteral("confThresh")).toDouble();
        const double nms = m_params.value(QStringLiteral("nmsThresh")).toDouble();
        const int layout = m_params.value(QStringLiteral("yoloLayout"), 0).toInt();
        if (!DnnDetectNode::parseYoloOutput(out, inputSize, cv::Size(img.cols, img.rows),
                                            conf, nms, boxes, layout)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("检测输出无法解析（模型与输入尺寸不匹配？）");
            return;
        }
        const QStringList classNames = DnnDetectNode::loadClassNames(
            modelPath, m_params.value(QStringLiteral("classNamesPath")).toString());
        for (auto &b : boxes)
            if (b.classId >= 0 && b.classId < classNames.size())
                b.className = classNames[b.classId];
        // 类别校验（2026-10-28）：classes.txt 缺失/行数不足是现场最高频的配置错，
        // 直接把"几个框没名字"说出来，比让人对着一张全是数字的图猜要快
        int unnamed = 0;
        for (const auto &b : boxes)
            if (b.className.isEmpty() && b.classId >= 0)
                ++unnamed;
        if (unnamed > 0) {
            m_params[QStringLiteral("countNote")] =
                QStringLiteral("模型输出 %1 类，classes.txt 只有 %2 行：%3 个框无类名")
                    .arg(classNames.isEmpty() ? 0 : classNames.size())
                    .arg(classNames.size())
                    .arg(unnamed);
        }
    } catch (const cv::Exception &e) {
        m_params[QStringLiteral("lastError")] =
            QStringLiteral("推理异常：%1").arg(QString::fromUtf8(e.what()));
        return;
    }

    // ── 跟踪 + 过线 ──
    ObjectTracker::Config cfg;
    cfg.minHits = m_params.value(QStringLiteral("minHits")).toInt();
    cfg.maxMissed = m_params.value(QStringLiteral("maxMissed")).toInt();
    // 只同步配置、**不清轨迹**（2026-10-28）：每帧重建跟踪器会把 ID 归零，
    // 同一个物体每次都被当新目标——那正是计数翻倍最常见的原因。
    m_tracker.setConfig(cfg);

    const QVector<ObjectTrack> tracks = m_tracker.update(boxes);
    const int dirMode = m_params.value(QStringLiteral("direction")).toInt();
    const QVector<LineCounter::Event> events =
        m_lineCounter.update(tracks, /*confirmedOnly=*/true,
                             [this](const ObjectTrack &t) { return m_tracker.confirmed(t); });
    int added = 0;
    for (const LineCounter::Event &e : events) {
        if (dirMode == 1 && e.direction < 0)
            continue;
        if (dirMode == 2 && e.direction > 0)
            continue;
        ++m_totalCount;
        ++added;
        const QString cls = e.className.isEmpty() ? QStringLiteral("(未分类)") : e.className;
        m_perClassCount[cls] += 1;
    }

    // ── 输出 ──
    cv::Mat vis = img.clone();
    for (const ObjectTrack &t : tracks) {
        const cv::Rect r(cvRound(t.box.x), cvRound(t.box.y), cvRound(t.box.width),
                         cvRound(t.box.height));
        const bool conf = m_tracker.confirmed(t);
        cv::rectangle(vis, r, conf ? cv::Scalar(0, 220, 0) : cv::Scalar(0, 165, 255), 2);
        cv::putText(vis, cv::format("#%d %s", t.id, t.className.toStdString().c_str()),
                    cv::Point(r.x, std::max(12, r.y - 6)), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    conf ? cv::Scalar(0, 220, 0) : cv::Scalar(0, 165, 255), 1);
    }
    // 计数线 + 累计值
    cv::line(vis,
             cv::Point(m_params.value(QStringLiteral("lineA_x")).toInt(),
                       m_params.value(QStringLiteral("lineA_y")).toInt()),
             cv::Point(m_params.value(QStringLiteral("lineB_x")).toInt(),
                       m_params.value(QStringLiteral("lineB_y")).toInt()),
             cv::Scalar(255, 128, 0), 2);
    cv::putText(vis, cv::format("count=%d (+%d)", m_totalCount, added), cv::Point(8, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 255), 2);

    m_outputImage = OpencvUtil::matToHimage(vis);
    if (m_outputImage.IsInitialized()) {
        auto imgObj = QSharedPointer<DataObject>::create();
        imgObj->setHImage(m_outputImage);
        setOutputData(0, imgObj);
    }

    auto cntObj = QSharedPointer<DataObject>::create();
    cntObj->setValue(double(m_totalCount));
    setOutputData(1, cntObj);

    QJsonObject root;
    root[QStringLiteral("total")] = m_totalCount;
    root[QStringLiteral("addedThisFrame")] = added;
    root[QStringLiteral("activeTracks")] = tracks.size();
    QJsonObject perClass;
    for (auto it = m_perClassCount.constBegin(); it != m_perClassCount.constEnd(); ++it)
        perClass[it.key()] = it.value();
    root[QStringLiteral("perClass")] = perClass;
    QJsonArray evArr;
    for (const LineCounter::Event &e : events) {
        QJsonObject o;
        o[QStringLiteral("id")] = e.trackId;
        o[QStringLiteral("class")] = e.className;
        o[QStringLiteral("dir")] = e.direction;
        evArr.append(o);
    }
    root[QStringLiteral("events")] = evArr;
    auto jsonObj = QSharedPointer<DataObject>::create();
    jsonObj->setType(DataObject::DataType::String);
    jsonObj->setData(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    setOutputData(2, jsonObj);

    m_params[QStringLiteral("countNote")] =
        QStringLiteral("累计 %1（本帧 +%2，在跟 %3）").arg(m_totalCount).arg(added).arg(tracks.size());
    setParamDirect(QStringLiteral("moduleStatus"), true);
}

QWidget *ObjectCountingNode::createParamPanel()
{
    return createAutoParamPanel();
}

void ObjectCountingNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

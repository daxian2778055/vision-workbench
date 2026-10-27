#include "FeatureSearchNode.h"
#include "DataObject.h"
#include "FeatureIndex.h"
#include "AppDatabase.h"
#include "OpencvUtil.h"
#include "NodeResultFields.h"
#include <opencv2/imgcodecs.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FeatureSearchNode::FeatureSearchNode(QObject *parent) : HalconNode(parent)
{
    m_type = OUTPUT;
}

void FeatureSearchNode::init()
{
    HalconNode::init();
    // 声明式参数（2026-10-27）：自动面板与序列化范围校验都靠 paramSpecs；
    // Enum 存 int 索引（与 ParamType::Enum 约定一致），run() 里映射成行为名。
    {
        ParamSpec sp;
        sp.name = QStringLiteral("mode");
        sp.type = ParamType::Enum;
        sp.defaultValue = 0;
        sp.enumValues = QStringList{QStringLiteral("检索"), QStringLiteral("入库")};
        sp.label = QStringLiteral("工作方式");
        sp.tooltip = QStringLiteral("检索=在库里找最像的样本；入库=把当前图的特征存进库");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("detector");
        sp.type = ParamType::Enum;
        sp.defaultValue = 0;
        sp.enumValues = QStringList{QStringLiteral("ORB"), QStringLiteral("SIFT")};
        sp.label = QStringLiteral("检测器");
        sp.tooltip = QStringLiteral("ORB 快且无专利；SIFT 更稳但慢");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("topK");
        sp.type = ParamType::Int;
        sp.defaultValue = 5;
        sp.minValue = 1;
        sp.maxValue = 100;
        sp.hasRange = true;
        sp.label = QStringLiteral("返回前 K 名");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("ratio");
        sp.type = ParamType::Double;
        sp.defaultValue = 0.75;
        sp.minValue = 0.1;
        sp.maxValue = 0.95;
        sp.hasRange = true;
        sp.label = QStringLiteral("比值检验阈值");
        sp.tooltip = QStringLiteral("越小越严格（Lowe ratio test）");
        registerParam(sp);
    }
    {
        ParamSpec sp;
        sp.name = QStringLiteral("sampleName");
        sp.type = ParamType::String;
        sp.defaultValue = QString();
        sp.label = QStringLiteral("样本名（入库用）");
        registerParam(sp);
    }
    m_params[QStringLiteral("searchNote")] = QString();
}

void FeatureSearchNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("lastError")] = QString();
    m_params[QStringLiteral("searchNote")] = QString();
    setParamDirect(QStringLiteral("moduleStatus"), false);

    HImage input(m_inputImage);
    if (!input.IsInitialized()) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("没有输入图像");
        return;
    }
    const cv::Mat mat = OpencvUtil::himageToMat(input);
    if (mat.empty()) {
        m_params[QStringLiteral("lastError")] = QStringLiteral("输入图像为空或桥接失败");
        return;
    }

    // Enum 存 int 索引（0/1）；也兼容旧工程里存字符串的情形
    const QVariant detVar = m_params.value(QStringLiteral("detector"));
    const QString detector = detVar.toString().isEmpty() || detVar.typeId() == QMetaType::Int
        ? (detVar.toInt() == 1 ? QStringLiteral("SIFT") : QStringLiteral("ORB"))
        : detVar.toString();
    const QVariant modeVar = m_params.value(QStringLiteral("mode"));
    const bool isAdd = modeVar.typeId() == QMetaType::Int
                           ? modeVar.toInt() == 1
                           : modeVar.toString() == QStringLiteral("Add");

    if (isAdd) {
        // 入库：特征提取 + 序列化 + 持久化
        std::vector<cv::KeyPoint> kps;
        cv::Mat desc;
        if (!FeatureIndex::extract(detector, mat, kps, desc)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("提取不到特征（图像纹理不足？）");
            return;
        }
        FeatureSample s;
        QString name = m_params.value(QStringLiteral("sampleName")).toString().trimmed();
        if (name.isEmpty())
            name = QStringLiteral("sample_%1")
                       .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz")));
        // 缩略图存 PNG（图库管理界面预览用，2026-10-27）
        QByteArray png;
        {
            std::vector<uchar> buf;
            if (cv::imencode(".png", mat, buf))
                png = QByteArray(reinterpret_cast<const char *>(buf.data()), int(buf.size()));
        }
        s.setFrom(name, detector, kps, desc, png);
        if (!AppDatabase::instance()->saveFeatureSample(s)) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("特征样本入库失败（写操作闸/数据库？）");
            return;
        }
        m_params[QStringLiteral("searchNote")] =
            QStringLiteral("已入库：%1（%2 特征点，detector=%3）").arg(name).arg(kps.size()).arg(detector);
        m_outputImage = m_inputImage;
        if (m_outputImage.IsInitialized()) {
            auto outObj = QSharedPointer<DataObject>::create();
            outObj->setHImage(m_outputImage);
            setOutputData(0, outObj);
        }
        setParamDirect(QStringLiteral("moduleStatus"), true);
        return;
    }

    // 检索
    const int topK = qMax(1, m_params.value(QStringLiteral("topK")).toInt());
    const double ratio = qBound(0.1, m_params.value(QStringLiteral("ratio")).toDouble(), 0.95);
    QStringList errs;
    cv::Mat annotated;
    const QVector<FeatureMatchHit> hits =
        FeatureIndex::search(detector, mat, topK, ratio, &errs, &annotated);

    QJsonArray arr;
    for (const FeatureMatchHit &h : hits) {
        QJsonObject o;
        o[QStringLiteral("id")] = h.sampleId;
        o[QStringLiteral("name")] = h.name;
        o[QStringLiteral("goodMatches")] = h.goodMatches;
        o[QStringLiteral("inliers")] = h.inliers;
        o[QStringLiteral("score")] = h.score;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("detector")] = detector;
    root[QStringLiteral("hits")] = arr;
    if (!errs.isEmpty())
        root[QStringLiteral("warnings")] = QJsonArray::fromStringList(errs);

    auto strObj = QSharedPointer<DataObject>::create();
    strObj->setType(DataObject::DataType::String);
    strObj->setData(QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    setOutputData(1, strObj);

    MeasureResult res;
    res.type = QStringLiteral("featureScore");
    res.valueName = QStringLiteral("最高相似度");
    res.value = hits.isEmpty() ? 0.0 : hits.first().score;
    res.valid = !hits.isEmpty();
    auto resObj = QSharedPointer<DataObject>::create();
    resObj->setMeasureResult(res);
    setOutputData(2, resObj);

    m_params[QStringLiteral("searchNote")] =
        hits.isEmpty() ? QStringLiteral("未命中（%1）").arg(errs.join(QStringLiteral("；")))
                       : QStringLiteral("Top1：%1（分 %2，匹配 %3，内点 %4）")
                             .arg(hits.first().name)
                             .arg(hits.first().score, 0, 'f', 3)
                             .arg(hits.first().goodMatches)
                             .arg(hits.first().inliers);
    if (hits.isEmpty())
        m_params[QStringLiteral("lastError")] = m_params[QStringLiteral("searchNote")];

    // 有命中就把可视化图（Top1 内点 + 得分）作为输出图，便于现场调 ratio/topK；
    // 没命中或画不出来则透传输入图
    m_outputImage = m_inputImage;
    if (!annotated.empty()) {
        HImage viz = OpencvUtil::matToHimage(annotated);
        if (viz.IsInitialized())
            m_outputImage = viz;
    }
    if (m_outputImage.IsInitialized()) {
        auto outObj = QSharedPointer<DataObject>::create();
        outObj->setHImage(m_outputImage);
        setOutputData(0, outObj);
    }
    // 空库/无命中判红（与"识别不到内容"同口径：读不到就是没测到）
    setParamDirect(QStringLiteral("moduleStatus"), !hits.isEmpty());
}

QWidget *FeatureSearchNode::createParamPanel()
{
    return createAutoParamPanel();
}

void FeatureSearchNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

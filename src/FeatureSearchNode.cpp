#include "FeatureSearchNode.h"
#include "DataObject.h"
#include "FeatureIndex.h"
#include "AppDatabase.h"
#include "OpencvUtil.h"
#include "NodeResultFields.h"

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
    m_params[QStringLiteral("mode")] = QStringLiteral("Search");   // Search | Add
    m_params[QStringLiteral("detector")] = QStringLiteral("ORB");   // ORB | SIFT
    m_params[QStringLiteral("topK")] = 5;
    m_params[QStringLiteral("ratio")] = 0.75;
    m_params[QStringLiteral("sampleName")] = QString();
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

    const QString detector = m_params.value(QStringLiteral("detector")).toString();
    const QString mode = m_params.value(QStringLiteral("mode")).toString();

    if (mode == QLatin1String("Add")) {
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
        s.setFrom(name, detector, kps, desc, QByteArray());
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
    const QVector<FeatureMatchHit> hits =
        FeatureIndex::search(detector, mat, topK, ratio, &errs);

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

    m_outputImage = m_inputImage;
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

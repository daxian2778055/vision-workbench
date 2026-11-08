#include <QMutex>
#include <QMap>
#include "DeepOcrNode.h"
#include <algorithm>
#include "DataObject.h"
#include "AppLog.h"
#include <QWidget>

using namespace HalconCpp;

DeepOcrNode::DeepOcrNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("HALCON DeepOCR"));
    m_type = SHAPE_ANALYSIS;
}

namespace {

/// DeepOCR 引擎互斥量。**必须由调用方持有到推理结束**：
/// HALCON 的 DL 引擎句柄不保证线程安全，且 SetDeepOcrParam 会改写共享句柄；
/// 只锁缓存查找、把 ApplyDeepOcr 放在锁外时，两个流程并发会同时操作同一引擎。
QMutex &deepOcrMutex()
{
    static QMutex mu;
    return mu;
}

/// DeepOCR 引擎缓存（r19 P1-7）：每帧 CreateDeepOcr + ClearDlModel 是纯浪费。
/// 缓存键为 mode|runtime —— 旧实现只按 mode 缓存，GPU/CPU 两个节点会共用同一
/// 引擎并互相改写 runtime，导致配置随机失效。
/// 调用方必须已持有 deepOcrMutex()。
HTuple cachedDeepOcrUnlocked(const QString &mode, const QString &runtime)
{
    static QMap<QString, HTuple> s_engines;
    const QString key = mode + QLatin1Char('|') + runtime;
    const auto it = s_engines.constFind(key);
    if (it != s_engines.constEnd())
        return it.value();
    HTuple ocrH;
    CreateDeepOcr(HTuple("mode"), HTuple(mode.toStdString().c_str()), &ocrH);
    s_engines.insert(key, ocrH);
    return ocrH;
}

}   // namespace

void DeepOcrNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("识别文本"), PortDataType::String);
    addOutputPort(QStringLiteral("OCR结果"), PortDataType::Measure);
    registerParams({
        makeEnumParam(QStringLiteral("mode"), 0,
                      {QStringLiteral("auto"), QStringLiteral("detection"),
                       QStringLiteral("recognition")},
                      QStringLiteral("模式：auto=检测+识别；detection=只定位文字；recognition=只识别裁剪文字")),
        makeEnumParam(QStringLiteral("runtime"), 0,
                      {QStringLiteral("cpu"), QStringLiteral("auto")},
                      QStringLiteral("推理设备（无 GPU 环境用 cpu）")),
        makeDoubleParam(QStringLiteral("minConfidence"), 0.3, 0.0, 1.0,
                        QStringLiteral("最低置信度（低于此值的词丢弃）")),
        makeIntParam(QStringLiteral("minWordLength"), 0, 0, 50,
                     QStringLiteral("最短词长度（0=不限）")),
        makeIntParam(QStringLiteral("maxWords"), 0, 0, 10000,
                     QStringLiteral("最多输出词数（0=不限）")),
        makeStringParam(QStringLiteral("joinSeparator"), QStringLiteral(" "),
                        QStringLiteral("词间分隔符")),
    });
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("ocrCount")] = 0;
    m_params[QStringLiteral("ocrConfidence")] = 0.0;
    m_params[QStringLiteral("ocrAvgConfidence")] = 0.0;
    m_params[QStringLiteral("ocrRawWordCount")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
}

void DeepOcrNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("ocrText")] = QString();
    m_params[QStringLiteral("ocrCount")] = 0;
    m_params[QStringLiteral("ocrConfidence")] = 0.0;
    m_params[QStringLiteral("ocrAvgConfidence")] = 0.0;
    m_params[QStringLiteral("ocrRawWordCount")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }
        const int modeIdx = m_params.value(QStringLiteral("mode"), 0).toInt();
        const QString mode = (modeIdx == 1)   ? QStringLiteral("detection")
                             : (modeIdx == 2) ? QStringLiteral("recognition")
                                              : QStringLiteral("auto");
        const int runtimeIdx = m_params.value(QStringLiteral("runtime"), 0).toInt();
        const QString runtime =
            (runtimeIdx == 1) ? QStringLiteral("auto") : QStringLiteral("cpu");
        const double minConf =
            m_params.value(QStringLiteral("minConfidence"), 0.3).toDouble();
        const int minWordLength = m_params.value(QStringLiteral("minWordLength"), 0).toInt();
        const int maxWords = m_params.value(QStringLiteral("maxWords"), 0).toInt();
        const QString separator =
            m_params.value(QStringLiteral("joinSeparator"), QStringLiteral(" ")).toString();

        // 「取引擎 + 推理」同一临界区：HALCON DL 引擎句柄不保证线程安全，
        // 两个流程/页签并发跑 DeepOcr 时不能同时操作同一引擎。
        // runtime 已在建引擎时设好（缓存键含 runtime），不再每帧改写共享句柄。
        QMutexLocker<QMutex> engineLocker(&deepOcrMutex());
        HTuple ocrH;
        try {
            ocrH = cachedDeepOcrUnlocked(mode, runtime);
        } catch (const HException &e) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 创建失败（需 HALCONROOT/dl 下的官方预训练模型）: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
            m_params["moduleStatus"] = false;
            return;
        }

        // runtime 仍在每次推理前设置（保持原有行为），但已处于同一临界区内，
        // 不会与其它流程并发改写同一引擎句柄。
        try {
            SetDeepOcrParam(ocrH, "runtime", HTuple(runtime.toStdString().c_str()));
        } catch (const HException &e) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 设置 runtime 失败: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
        }

        HTuple results;
        try {
            ApplyDeepOcr(input, ocrH, HTuple(mode.toStdString().c_str()), &results);
        } catch (const HException &e) {
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("DeepOCR 推理失败: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
            m_params["moduleStatus"] = false;
            // 引擎进缓存复用（r19 P1-7）：这里不再 ClearDlModel，
            // 否则下一次取到的是已被释放的句柄。
            return;
        }
        // 不再 ClearDlModel（r19 P1-7）：引擎进缓存复用

        // 解析结果 dict（deep_ocr 结果：单个 dict）
        QStringList words;
        double confSum = 0.0;
        int confN = 0;
        int rawWordCount = 0;   // 过滤前的词数（结果宽度参考）
        bool haveResult = false;
        try {
            if (results.Length() > 0) {
                const HDict d(results.H());
                // 只有真的取到 word 数组才算"有结果"：旧实现把 haveResult 放在
                // word 键判断之外，dict 缺 word 时输出空文本却 moduleStatus=true，
                // 等于静默判 OK。
                if (d.GetDictParam("key_exists", "word").I() == 1) {
                    const HTuple ws = d.GetDictTuple("word");
                    HTuple cs;
                    if (d.GetDictParam("key_exists", "confidence").I() == 1)
                        cs = d.GetDictTuple("confidence");
                    // 「结果里没有置信度」≠「置信度为 0」：recognition 模式的返回实测
                    // 不含 confidence 键，旧实现把缺失值当 0.0，再被默认 minConfidence=0.3
                    // 全部过滤掉 ⇒ 默认参数下永远读不到字（只有显式传 0.0 才出文本）。
                    // 现在：没有置信度就不过滤，并把 avg/max 置 0 表示"不可用"。
                    const bool hasConfidence = (cs.Length() == ws.Length()) && (ws.Length() > 0);
                    double maxConf = 0.0;
                    for (Hlong i = 0; i < ws.Length(); ++i) {
                        QString w = QString::fromStdString(ws[i].S().Text());
                        if (w.isEmpty()) continue;
                        ++rawWordCount;
                        const double c = hasConfidence ? cs[i].D() : 0.0;
                        if (hasConfidence && c < minConf) continue;
                        if (minWordLength > 0 && w.size() < minWordLength) continue;
                        if (maxWords > 0 && words.size() >= maxWords) continue;
                        words << w;
                        if (hasConfidence) {
                            confSum += c;
                            ++confN;
                            maxConf = (c > maxConf) ? c : maxConf;
                        }
                    }
                    m_params[QStringLiteral("ocrConfidence")] = maxConf;
                }
                // 零词不能判绿（2026-11-08 复核 P1）：空白图也会让 HALCON 返回非空
                // dict，旧实现无条件 haveResult=true ⇒ moduleStatus=true ⇒ 自检 [OK]、
                // 部署门禁 0 FAIL，形成"一个字都读不出也算通过"的假绿链。
                // 正确口径：recognition 模式必须有词；detection/auto 只做检测，
                // 没有 word 数组属正常。
                const bool hasWordKey = (d.GetDictParam("key_exists", "word").I() == 1);
                if (hasWordKey) {
                    // rawWordCount 是**过滤前**的词数：有字但被 minConfidence/minWordLength
                    // 过滤掉属于用户自设阈值的正常结果，不算失败。
                    haveResult = (rawWordCount > 0);
                    if (!haveResult)
                        m_params[QStringLiteral("lastError")] =
                            QStringLiteral("未识别到文字（word 数组为空）");
                } else if (mode == QStringLiteral("recognition")) {
                    m_params[QStringLiteral("lastError")] =
                        QStringLiteral("recognition 模式未返回 word 数组");
                } else {
                    haveResult = true;   // detection / auto：只返回检测结果，属正常
                }
            }
        } catch (const HException &e) {
            // 解析异常不能静默吞掉：否则现场只看到"识别为空"、查不到原因
            m_params[QStringLiteral("lastError")] =
                QStringLiteral("OCR 结果解析失败: %1")
                    .arg(QString::fromStdString(e.ErrorMessage().Text()));
        }
        if (!haveResult) {
            if (m_params.value(QStringLiteral("lastError")).toString().isEmpty())
                m_params[QStringLiteral("lastError")] = QStringLiteral("OCR 无返回结果");
            m_params["moduleStatus"] = false;
            return;
        }

        m_params[QStringLiteral("ocrText")] = words.join(separator);
        m_params[QStringLiteral("ocrCount")] = words.size();
        m_params[QStringLiteral("ocrAvgConfidence")] =
            confN > 0 ? confSum / static_cast<double>(confN) : 0.0;
        m_params[QStringLiteral("ocrRawWordCount")] = rawWordCount;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        auto strObj = QSharedPointer<DataObject>::create();
        strObj->setType(DataObject::DataType::String);
        strObj->setData(m_params[QStringLiteral("ocrText")].toString());
        setOutputData(1, strObj);

        MeasureResult res;
        res.type = QStringLiteral("ocr");
        res.valueName = QStringLiteral("词数");
        res.valid = true;
        res.value = words.size();
        res.extraValues = QVector<double>(
            {m_params[QStringLiteral("ocrConfidence")].toDouble(),
             m_params[QStringLiteral("ocrAvgConfidence")].toDouble()});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(2, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "DeepOcrNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *DeepOcrNode::createParamPanel()
{
    return createAutoParamPanel();
}

void DeepOcrNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

QVector<NodeResultField> DeepOcrNode::resultDisplayFields() const
{
    // VM「模块结果」口径：模块级 耗时/模块状态 + 识别文本/单词数/置信度。
    QVector<NodeResultField> fields = HalconNode::resultDisplayFields();
    fields << resultValue(QStringLiteral("ocrText"), QStringLiteral("识别文本"))
           << resultValue(QStringLiteral("ocrCount"), QStringLiteral("单词数"),
                          QString(), 0)
           << resultValue(QStringLiteral("ocrConfidence"), QStringLiteral("置信度"),
                          QString(), 4)
           << resultValue(QStringLiteral("ocrAvgConfidence"), QStringLiteral("平均置信度"),
                          QString(), 4)
           << resultValue(QStringLiteral("ocrRawWordCount"), QStringLiteral("原始词数"),
                          QString(), 0);
    return fields;
}

#include "OpencvTrainClassifierNode.h"
#include "ClassifierTrainer.h"
#include "DataObject.h"
#include "AppLog.h"
#include "AnnotationDialog.h"
#include <QWidget>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QPushButton>
#include <QVBoxLayout>

OpencvTrainClassifierNode::OpencvTrainClassifierNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("分类器训练"));
    m_type = IMAGE_PROCESSING;
}

void OpencvTrainClassifierNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("训练结果"), PortDataType::Measure);
    registerParams({
        makeFilePathParam(QStringLiteral("trainDir"), QString(),
                          QStringLiteral("训练图片目录（子目录名=类别名，如 0/1 或 good/bad）")),
        makeFilePathParam(QStringLiteral("saveModelPath"), QString(),
                          QStringLiteral("模型保存路径 (.yaml)")),
        makeIntParam(QStringLiteral("featureMode"), 0, 0, 1,
                     QStringLiteral("特征：0=HOG 64x64，1=32x32 灰度")),
        makeIntParam(QStringLiteral("hiddenNeurons"), 32, 4, 1024,
                     QStringLiteral("隐藏层神经元数")),
        makeIntParam(QStringLiteral("maxIterations"), 800, 50, 20000,
                     QStringLiteral("最大迭代次数")),
    });
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("sampleCount")] = 0;
    m_params[QStringLiteral("classCount")] = 0;
    m_params[QStringLiteral("featDim")] = 0;
    m_params[QStringLiteral("lastError")] = QString();
}

void OpencvTrainClassifierNode::run(bool /*autoSwitch*/)
{
    m_params[QStringLiteral("trainStatus")] = QString();
    m_params[QStringLiteral("lastError")] = QString();
    try {
        const QString trainDir =
            m_params.value(QStringLiteral("trainDir"), QString()).toString().trimmed();
        const QString savePath =
            m_params.value(QStringLiteral("saveModelPath"), QString()).toString().trimmed();
        const int featureMode = m_params.value(QStringLiteral("featureMode"), 0).toInt();
        const int hidden = m_params.value(QStringLiteral("hiddenNeurons"), 32).toInt();
        const int maxIter = m_params.value(QStringLiteral("maxIterations"), 800).toInt();

        if (trainDir.isEmpty() || savePath.isEmpty()) {
            m_params[QStringLiteral("lastError")] = QStringLiteral("请设置训练目录与模型保存路径");
            m_params["moduleStatus"] = false;
            return;
        }
        // 确保保存目录存在
        QDir().mkpath(QFileInfo(savePath).absolutePath());

        ClsTrainer::TrainStats st = ClsTrainer::trainFromDir(
            trainDir.toLocal8Bit().constData(), savePath.toLocal8Bit().constData(),
            featureMode, hidden, maxIter);
        if (!st.ok) {
            m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(st.error.c_str());
            m_params["moduleStatus"] = false;
            return;
        }

        m_params[QStringLiteral("trainStatus")] =
            QStringLiteral("训练完成: %1 样本 / %2 类 / %3 维特征")
                .arg(st.samples).arg(st.classes).arg(st.featDim);
        m_params[QStringLiteral("sampleCount")] = st.samples;
        m_params[QStringLiteral("classCount")] = st.classes;
        m_params[QStringLiteral("featDim")] = st.featDim;
        m_params["moduleStatus"] = true;
        m_outputImage = m_inputImage;

        MeasureResult res;
        res.type = QStringLiteral("train");
        res.valueName = QStringLiteral("样本数");
        res.valid = true;
        res.value = st.samples;
        res.extraValues = QVector<double>({double(st.classes), double(st.featDim)});
        auto resObj = QSharedPointer<DataObject>::create();
        resObj->setMeasureResult(res);
        setOutputData(1, resObj);
    } catch (const std::exception &e) {
        VFP_DEBUG << "OpencvTrainClassifierNode error:" << e.what();
        m_params[QStringLiteral("lastError")] = QString::fromLocal8Bit(e.what());
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    } catch (...) {
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

QWidget *OpencvTrainClassifierNode::createParamPanel()
{
    QWidget *panel = createAutoParamPanel();
    if (auto *lay = qobject_cast<QVBoxLayout *>(panel->layout())) {
        auto *btn = new QPushButton(QStringLiteral("打开标注窗…"));
        lay->addWidget(btn);
        QObject::connect(btn, &QPushButton::clicked, panel, [this, panel]() {
            QString dir = m_params.value(QStringLiteral("trainDir")).toString().trimmed();
            if (dir.isEmpty())
                dir = QDir::currentPath() + QStringLiteral("/annotations");
            AnnotationDialog dlg(this, dir, panel);
            dlg.exec();
            if (m_params.value(QStringLiteral("trainDir")).toString().trimmed().isEmpty())
                setParam(QStringLiteral("trainDir"), dlg.annotationDir());
            updateParamPanel(panel);
        });
    }
    return panel;
}

void OpencvTrainClassifierNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

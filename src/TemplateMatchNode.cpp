#include "TemplateMatchNode.h"
#include "DataObject.h"
#include "AppLog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSignalBlocker>

using namespace HalconCpp;

TemplateMatchNode::TemplateMatchNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("模板匹配"));
    m_type = SHAPE_ANALYSIS;
}

void TemplateMatchNode::init()
{
    HalconNode::init();
    // 结果端口：得分/行/列（追加在末尾，不破坏旧项目端口索引）
    addOutputPort(QStringLiteral("匹配结果"), PortDataType::Measure);

    registerParams({
        makeDoubleParam(QStringLiteral("minScore"), 0.5, 0.0, 1.0,
                        QStringLiteral("最低匹配度")),
        makeIntParam(QStringLiteral("numMatches"), 1, 1, 20,
                     QStringLiteral("最大匹配数")),
        makeDoubleParam(QStringLiteral("angleStart"), 0.0, -360.0, 360.0,
                        QStringLiteral("起始角度"), QStringLiteral("°")),
        makeDoubleParam(QStringLiteral("angleEnd"), 360.0, -360.0, 360.0,
                        QStringLiteral("终止角度"), QStringLiteral("°")),
        makeFilePathParam(QStringLiteral("templatePath"), QString(),
                          QStringLiteral("模板文件(.shm)")),
        makeBoolParam(QStringLiteral("trainFromImage"), true,
                      QStringLiteral("运行时从图像训练（勾选时忽略已存模板，自动重建并保存）")),
        makeIntParam(QStringLiteral("pyramidLevel"), 4, 0, 10,
                     QStringLiteral("金字塔层数")),
        makeDoubleParam(QStringLiteral("greediness"), 0.9, 0.0, 1.0,
                        QStringLiteral("重叠抑制（越大越快但易漏检）")),
        makeEnumParam(QStringLiteral("subpixel"), 2,
                      QStringList{"none", "interpolation", "least_squares", "least_squares_high"},
                      QStringLiteral("亚像素精度")),
        makeEnumParam(QStringLiteral("polarity"), 0,
                      QStringList{"use_polarity", "ignore_global_polarity", "ignore_local_polarity"},
                      QStringLiteral("极性模式")),
    });

    m_params[QStringLiteral("score")] = 0.0;
    m_params[QStringLiteral("matchedRow")] = 0.0;
    m_params[QStringLiteral("matchedCol")] = 0.0;
    m_params[QStringLiteral("matchedAngle")] = 0.0;
    m_params[QStringLiteral("trainStatus")] = QString();
}

void TemplateMatchNode::run(bool /*autoSwitch*/)
{
    try {
        HImage input(m_inputImage);
        if (!input.IsInitialized()) {
            m_outputImage.Clear();
            return;
        }

        HImage gray;
        HTuple ch;
        CountChannels(input, &ch);
        if (ch.I() > 1) {
            HObject g;
            Rgb1ToGray(input, &g);
            gray = HImage(g);
        } else {
            gray = input;
        }

        const QString templatePath =
            m_params.value(QStringLiteral("templatePath"), QString()).toString().trimmed();
        const bool trainFromImage =
            m_params.value(QStringLiteral("trainFromImage"), true).toBool();

        const double angleStart = m_params.value(QStringLiteral("angleStart"), 0.0).toDouble();
        const double angleEnd = m_params.value(QStringLiteral("angleEnd"), 360.0).toDouble();
        const double minScore = m_params.value(QStringLiteral("minScore"), 0.5).toDouble();
        const int numMatches = m_params.value(QStringLiteral("numMatches"), 1).toInt();
        const int pyramidLevel = m_params.value(QStringLiteral("pyramidLevel"), 4).toInt();
        const double greediness = m_params.value(QStringLiteral("greediness"), 0.9).toDouble();
        const QString subpixel =
            m_params.value(QStringLiteral("subpixel"), QStringLiteral("least_squares")).toString();
        const QString polarity =
            m_params.value(QStringLiteral("polarity"), QStringLiteral("use_polarity")).toString();

        bool modelReady = false;

        if (trainFromImage) {
            // ---- 训练模式：从图像中心 50% 区域重建模板 ----
            HTuple w, h;
            GetImageSize(gray, &w, &h);
            HObject modelRegion;
            GenRectangle1(&modelRegion, h.D() * 0.25, w.D() * 0.25, h.D() * 0.75, w.D() * 0.75);
            HObject modelImage;
            ReduceDomain(gray, modelRegion, &modelImage);

            try {
                if (m_modelCreated) {
                    ClearShapeModel(m_modelId);
                    m_modelCreated = false;
                }
            } catch (const HException &e) {
                // 训练前清除旧模板失败不能静默，需记录并标记失败
                m_params[QStringLiteral("trainStatus")] =
                    QStringLiteral("清除旧模板失败: %1")
                        .arg(QString::fromStdString(e.ErrorMessage().Text()));
                m_outputImage = gray;
                setMatchOutput(gray, 0.0, 0.0, 0.0, 0, false);
                m_params["moduleStatus"] = false;
                return;
            }

            CreateShapeModel(modelImage, "auto", HTuple(angleStart).TupleRad(),
                             HTuple(angleEnd).TupleRad(),
                             "auto", "auto", polarity.toStdString().c_str(),
                             "auto", "auto", &m_modelId);
            m_modelCreated = true;
            modelReady = true;

            // 训练结果持久化
            if (!templatePath.isEmpty()) {
                try {
                    QFileInfo fi(templatePath);
                    if (!fi.dir().exists())
                        QDir().mkpath(fi.absolutePath());
                    WriteShapeModel(m_modelId, templatePath.toStdString().c_str());
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("已训练并保存: %1").arg(templatePath);
                } catch (const HException &e) {
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("模板保存失败: %1").arg(QString::fromLocal8Bit(e.ErrorMessage().TextA()));
                }
            } else {
                m_params[QStringLiteral("trainStatus")] = QStringLiteral("已训练（未保存模板文件）");
            }
        } else {
            // ---- 匹配模式：加载外部模板 ----
            if (templatePath.isEmpty()) {
                m_params[QStringLiteral("trainStatus")] =
                    QStringLiteral("匹配模式需要设置模板文件路径");
                m_outputImage = gray;
                setMatchOutput(gray, 0.0, 0.0, 0.0, 0, false);
                return;
            }
            if (m_loadedTemplatePath != templatePath || !m_modelCreated) {
                try {
                    if (m_modelCreated) {
                        ClearShapeModel(m_modelId);
                        m_modelCreated = false;
                    }
                    ReadShapeModel(templatePath.toStdString().c_str(), &m_modelId);
                    m_modelCreated = true;
                    m_loadedTemplatePath = templatePath;
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("已加载模板: %1").arg(templatePath);
                } catch (const HException &e) {
                    m_params[QStringLiteral("trainStatus")] =
                        QStringLiteral("模板加载失败: %1").arg(QString::fromLocal8Bit(e.ErrorMessage().TextA()));
                    m_outputImage = gray;
                    setMatchOutput(gray, 0.0, 0.0, 0.0, 0, false);
                    return;
                }
            }
            modelReady = m_modelCreated;
        }

        if (!modelReady) {
            m_outputImage = gray;
            setMatchOutput(gray, 0.0, 0.0, 0.0, 0, false);
            return;
        }

        // ---- 匹配 ----
        HTuple row, col, angle, score;
        FindShapeModel(gray, m_modelId, HTuple(angleStart).TupleRad(),
                       HTuple(angleEnd).TupleRad(), minScore, numMatches,
                       pyramidLevel, subpixel.toStdString().c_str(), 0, greediness,
                       &row, &col, &angle, &score);

        if (score.Length() > 0) {
            m_params[QStringLiteral("score")] = score[0].D();
            m_params[QStringLiteral("matchedRow")] = row[0].D();
            m_params[QStringLiteral("matchedCol")] = col[0].D();
            m_params[QStringLiteral("matchedAngle")] = angle[0].D();
            m_params["moduleStatus"] = true;
            setMatchOutput(gray, row[0].D(), col[0].D(), score[0].D(), score.Length(), true);
        } else {
            m_params[QStringLiteral("score")] = 0.0;
            m_params[QStringLiteral("matchedRow")] = 0.0;
            m_params[QStringLiteral("matchedCol")] = 0.0;
            m_params[QStringLiteral("matchedAngle")] = 0.0;
            m_params["moduleStatus"] = false;
            setMatchOutput(gray, 0.0, 0.0, 0.0, 0, false);
        }
    } catch (const HException &e) {
        VFP_DEBUG << "TemplateMatchNode error:" << e.ErrorMessage().TextA();
        m_params["moduleStatus"] = false;
        m_outputImage.Clear();
    }
}

/// 输出匹配结果：图像 + Measure 端口（匹配到 N 个时输出最高分）
void TemplateMatchNode::setMatchOutput(const HImage &gray, double row, double col,
                                       double score, int numFound, bool found)
{
    m_outputImage = gray;

    MeasureResult res;
    res.type = QStringLiteral("template");
    res.valueName = QStringLiteral("匹配得分");
    res.valid = found;
    res.value = score;
    res.point1 = QPointF(col, row);
    res.extraValues = QVector<double>({row, col, score, double(numFound)});
    auto resObj = QSharedPointer<DataObject>::create();
    resObj->setMeasureResult(res);
    setOutputData(1, resObj);
}

QWidget *TemplateMatchNode::createParamPanel()
{
    // 声明式参数系统自动生成面板
    return createAutoParamPanel();
}

void TemplateMatchNode::updateParamPanel(QWidget *panel)
{
    updateAutoParamPanel(panel);
}

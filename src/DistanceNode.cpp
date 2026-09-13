#include "DistanceNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <cmath>

using namespace HalconCpp;

DistanceNode::DistanceNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u8DDD\u79BB"));
    m_type = MEASUREMENT;
}

void DistanceNode::init()
{
    HalconNode::init();
    addOutputPort(QStringLiteral("距离"), PortDataType::Number);
    m_params[QStringLiteral("row1")] = 0.0;
    m_params[QStringLiteral("col1")] = 0.0;
    m_params[QStringLiteral("row2")] = 100.0;
    m_params[QStringLiteral("col2")] = 100.0;
    // true：使用图像对角两点（默认值语义）；false：使用用户显式设置的两点
    m_params[QStringLiteral("useImageDiag")] = true;
}

void DistanceNode::run(bool /*autoSwitch*/)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }

        double r1 = m_params.value(QStringLiteral("row1")).toDouble();
        double c1 = m_params.value(QStringLiteral("col1")).toDouble();
        double r2 = m_params.value(QStringLiteral("row2")).toDouble();
        double c2 = m_params.value(QStringLiteral("col2")).toDouble();

        if (m_params.value(QStringLiteral("useImageDiag"), true).toBool()) {
            r1 = src.rows / 4.0;
            c1 = src.cols / 4.0;
            r2 = src.rows * 3.0 / 4.0;
            c2 = src.cols * 3.0 / 4.0;
        }

        const double dist = std::hypot(r2 - r1, c2 - c1);
        m_params[QStringLiteral("distancePixel")] = dist;
        m_outputImage = m_inputImage;

        auto distObj = QSharedPointer<DataObject>::create();
        distObj->setValue(dist);
        setOutputData(1, distObj);
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}

QWidget *DistanceNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u4E24\u70B9\u8DDD\u79BB\uFF08\u50CF\u7D20\uFF09</b>")));

    struct Field { const char *objName; QString label; QString key; double def; };
    const Field fields[] = {
        {"dstR1", QStringLiteral("\u8D77\u70B9\u884C"), "row1", 0.0},
        {"dstC1", QStringLiteral("\u8D77\u70B9\u5217"), "col1", 0.0},
        {"dstR2", QStringLiteral("\u7EC8\u70B9\u884C"), "row2", 100.0},
        {"dstC2", QStringLiteral("\u7EC8\u70B9\u5217"), "col2", 100.0},
    };
    constexpr double mx = 1e7;

    for (const auto &f : fields) {
        auto *sp = new QDoubleSpinBox();
        sp->setObjectName(QLatin1String(f.objName));
        sp->setRange(0.0, mx);
        sp->setDecimals(2);
        {
            QSignalBlocker b(sp);
            sp->setValue(m_params.value(f.key, f.def).toDouble());
        }
        connect(sp, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, panel]() {
            auto getVal = [panel](const char *name) -> double {
                auto *w = panel->findChild<QDoubleSpinBox *>(QLatin1String(name));
                return w ? w->value() : 0.0;
            };
            setParam(QStringLiteral("row1"), getVal("dstR1"));
            setParam(QStringLiteral("col1"), getVal("dstC1"));
            setParam(QStringLiteral("row2"), getVal("dstR2"));
            setParam(QStringLiteral("col2"), getVal("dstC2"));
        });
        layout->addWidget(new QLabel(f.label));
        layout->addWidget(sp);
    }

    layout->addWidget(new QLabel(QStringLiteral(
        "\u5F53\u53C2\u6570\u4E3A\u9ED8\u8BA4\u503C (0,0)-(100,100) \u65F6\uFF0C\u5355\u6B21\u8FD0\u884C\u5C06\u6539\u7528\u56FE\u50CF\u5BF9\u89D2\u7EBF\u4E24\u7AEF\u70B9\u4F30\u7B97\u8DDD\u79BB\u3002")));

    auto *resultLabel = new QLabel();
    resultLabel->setObjectName(QStringLiteral("distanceResult"));
    double dist = m_params.value(QStringLiteral("distancePixel"), 0.0).toDouble();
    resultLabel->setText(QStringLiteral("\u8DDD\u79BB: %1 \u50CF\u7D20").arg(dist, 0, 'f', 2));
    layout->addWidget(resultLabel);

    layout->addStretch();
    return panel;
}

void DistanceNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    const char *objs[] = {"dstR1", "dstC1", "dstR2", "dstC2"};
    const QString keys[] = {"row1", "col1", "row2", "col2"};
    for (int i = 0; i < 4; ++i) {
        if (auto *w = panel->findChild<QDoubleSpinBox *>(QLatin1String(objs[i]))) {
            QSignalBlocker b(w);
            w->setValue(m_params.value(keys[i]).toDouble());
        }
    }
    if (auto *lbl = panel->findChild<QLabel *>(QStringLiteral("distanceResult"))) {
        double dist = m_params.value(QStringLiteral("distancePixel"), 0.0).toDouble();
        lbl->setText(QStringLiteral("\u8DDD\u79BB: %1 \u50CF\u7D20").arg(dist, 0, 'f', 2));
    }
}

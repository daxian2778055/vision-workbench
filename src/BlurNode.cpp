#include "BlurNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgproc.hpp>
#include <QVBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QSignalBlocker>

using namespace HalconCpp;

BlurNode::BlurNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u6A21\u7CCA"));
    m_type = IMAGE_PROCESSING;
}

void BlurNode::init()
{
    HalconNode::init();
    m_params[QStringLiteral("gaussSize")] = 5;
}

void BlurNode::run(bool /*autoSwitch*/)
{
    // OpenCV 高斯模糊（不再走 HALCON Rgb1ToGray / GaussImage）
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            m_params["moduleStatus"] = false;
            return;
        }

        const int size = m_params.value(QStringLiteral("gaussSize"), 5).toInt();
        const int ksize = qBound(3, size | 1, 11);
        cv::Mat dst;
        cv::GaussianBlur(src, dst, cv::Size(ksize, ksize), 0);
        m_outputImage = OpencvUtil::matToHimage(dst);
    } catch (const std::exception &e) {
        m_outputImage.Clear();
        m_params["moduleStatus"] = false;
    }
}

QWidget *BlurNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u6A21\u7CCA\uFF08\u9AD8\u65AF\u6EE4\u6CE2\uFF09</b>")));

    auto *combo = new QComboBox();
    combo->setObjectName(QStringLiteral("blurMask"));
    for (int s : {3, 5, 7, 9, 11})
        combo->addItem(QString::number(s), s);

    const int g = m_params.value(QStringLiteral("gaussSize"), 5).toInt();
    const int idx = combo->findData(qBound(3, g | 1, 11));
    if (idx >= 0) {
        QSignalBlocker br(combo);
        combo->setCurrentIndex(idx);
    }
    connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, combo](int) {
        setParam(QStringLiteral("gaussSize"), combo->currentData().toInt());
    });

    layout->addWidget(new QLabel(QStringLiteral("\u6838\u5927\u5C0F:")));
    layout->addWidget(combo);
    layout->addStretch();
    return panel;
}

void BlurNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *cb = panel->findChild<QComboBox *>(QStringLiteral("blurMask"))) {
        QSignalBlocker g(cb);
        const int gv = qBound(3, m_params.value(QStringLiteral("gaussSize"), 5).toInt() | 1, 11);
        const int idx = cb->findData(gv);
        if (idx >= 0) cb->setCurrentIndex(idx);
    }
}

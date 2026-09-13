#include "WriteFileNode.h"
#include "DataObject.h"
#include "OpencvUtil.h"
#include <opencv2/imgcodecs.hpp>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QSignalBlocker>

using namespace HalconCpp;

WriteFileNode::WriteFileNode(QObject *parent) : HalconNode(parent)
{
    setName(QStringLiteral("\u5199\u5165\u6587\u4EF6"));
    m_type = OUTPUT;
}

void WriteFileNode::init()
{
    HalconNode::init();
    m_params[QStringLiteral("filePath")] = QStringLiteral("output.png");
}

void WriteFileNode::run(bool /*autoSwitch*/)
{
    try {
        if (!m_inputImage.IsInitialized()) return;
        cv::Mat src = OpencvUtil::himageToMat(HImage(m_inputImage));
        if (src.empty()) {
            m_outputImage.Clear();
            return;
        }
        const QString path = m_params.value(QStringLiteral("filePath")).toString();
        if (!cv::imwrite(path.toLocal8Bit().constData(), src)) {
            m_outputImage.Clear();
            return;
        }
        m_outputImage = m_inputImage;
        m_params[QStringLiteral("lastSavedPath")] = path;
    } catch (const std::exception &) {
        m_outputImage.Clear();
    }
}

QWidget *WriteFileNode::createParamPanel()
{
    auto *panel = new QWidget();
    auto *layout = new QVBoxLayout(panel);
    layout->addWidget(new QLabel(QStringLiteral("<b>\u5199\u5165\u6587\u4EF6</b>")));

    auto *hb = new QHBoxLayout();
    auto *le = new QLineEdit();
    le->setObjectName(QStringLiteral("writeFilePath"));
    le->setText(m_params.value(QStringLiteral("filePath"), QStringLiteral("output.png")).toString());
    auto *browse = new QPushButton(QStringLiteral("\u6D4F\u89C8\u2026"));
    hb->addWidget(le, 1);
    hb->addWidget(browse);
    layout->addLayout(hb);

    connect(le, &QLineEdit::editingFinished, this, [this, le]() {
        setParam(QStringLiteral("filePath"), le->text());
    });
    connect(browse, &QPushButton::clicked, this, [this, le]() {
        QString p = QFileDialog::getSaveFileName(nullptr,
            QStringLiteral("\u4FDD\u5B58\u56FE\u50CF"),
            le->text(),
            QStringLiteral("PNG (*.png);;\u6240\u6709\u6587\u4EF6 (*)"));
        if (!p.isEmpty()) {
            le->setText(p);
            setParam(QStringLiteral("filePath"), p);
        }
    });

    layout->addWidget(new QLabel(QStringLiteral("\u6267\u884C\u65F6\u4F7F\u7528 OpenCV imwrite \u4FDD\u5B58\u56FE\u50CF\u3002\u786E\u4FDD\u8DEF\u5F84\u53EF\u5199\u3002")));
    layout->addStretch();
    return panel;
}

void WriteFileNode::updateParamPanel(QWidget *panel)
{
    if (!panel) return;
    if (auto *le = panel->findChild<QLineEdit *>(QStringLiteral("writeFilePath"))) {
        QSignalBlocker b(le);
        le->setText(m_params.value(QStringLiteral("filePath"), QStringLiteral("output.png")).toString());
    }
}

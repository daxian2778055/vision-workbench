#include "OutputDataViewer.h"
#include "NodeBase.h"
#include "Port.h"
#include "DataObject.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QGroupBox>
#include <QPushButton>
#include <QMessageBox>
#include <QImage>
#include <QPixmap>
#include <HalconCpp.h>

OutputDataViewer::OutputDataViewer(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
}

void OutputDataViewer::setNode(NodeBase *node)
{
    if (m_currentNode == node) return;

    m_currentNode = node;
    clear();

    if (!node) {
        m_nodeNameLabel->setText(QStringLiteral("未选择节点"));
        m_statusLabel->setText("");
        return;
    }

    m_nodeNameLabel->setText(node->name());
    m_statusLabel->setText(node->isEnabled() ? QStringLiteral("已启用") : QStringLiteral("已禁用"));

    // 为每个输出端口创建标签页
    QList<Port*> outputPorts = node->outputPorts();
    for (int i = 0; i < outputPorts.size(); ++i) {
        Port *port = outputPorts[i];
        QString tabName = port->name().isEmpty() ? QStringLiteral("输出 %1").arg(i + 1) : port->name();

        auto *portWidget = new QWidget();
        auto *layout = new QVBoxLayout(portWidget);
        layout->setContentsMargins(4, 4, 4, 4);

        m_tabWidget->addTab(portWidget, tabName);
        m_portWidgets[i] = portWidget;

        // 显示端口数据
        updatePortData(i, node->getOutputData(i));
    }

    if (outputPorts.isEmpty()) {
        auto *noPortLabel = new QLabel(QStringLiteral("该节点没有输出端口"));
        noPortLabel->setAlignment(Qt::AlignCenter);
        m_tabWidget->addTab(noPortLabel, QStringLiteral("无输出"));
    }
}

void OutputDataViewer::clear()
{
    m_tabWidget->clear();
    m_portWidgets.clear();
    m_currentNode = nullptr;
}

void OutputDataViewer::refresh()
{
    if (!m_currentNode) return;

    // 刷新所有端口数据
    QList<Port*> outputPorts = m_currentNode->outputPorts();
    for (int i = 0; i < outputPorts.size(); ++i) {
        updatePortData(i, m_currentNode->getOutputData(i));
    }
}

void OutputDataViewer::onNodeExecuted(NodeBase *node, bool success)
{
    if (node != m_currentNode) return;

    if (success) {
        refresh();
    } else {
        m_statusLabel->setText(QStringLiteral("执行失败"));
        m_statusLabel->setStyleSheet(QStringLiteral("color: red;"));
    }
}

void OutputDataViewer::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    // 节点信息
    auto *infoLayout = new QHBoxLayout();
    m_nodeNameLabel = new QLabel(QStringLiteral("未选择节点"));
    m_nodeNameLabel->setStyleSheet(QStringLiteral("font-weight: bold; font-size: 13px;"));
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(QStringLiteral("color: #666;"));

    infoLayout->addWidget(m_nodeNameLabel);
    infoLayout->addStretch();
    infoLayout->addWidget(m_statusLabel);
    mainLayout->addLayout(infoLayout);

    // 分隔线
    auto *line = new QLabel;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background-color: #ddd;"));
    mainLayout->addWidget(line);

    // 标签页
    m_tabWidget = new QTabWidget;
    m_tabWidget->setTabPosition(QTabWidget::North);
    mainLayout->addWidget(m_tabWidget);

    // 刷新按钮
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新"));
    refreshBtn->setFixedWidth(60);
    connect(refreshBtn, &QPushButton::clicked, this, &OutputDataViewer::refresh);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(refreshBtn);
    mainLayout->addLayout(btnLayout);
}

void OutputDataViewer::updatePortData(int portIndex, QSharedPointer<DataObject> data)
{
    if (!m_portWidgets.contains(portIndex)) return;

    QWidget *portWidget = m_portWidgets[portIndex];
    // 清空旧内容
    if (portWidget->layout()) {
        QLayoutItem *item;
        while ((item = portWidget->layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
    }

    if (!data) {
        auto *noDataLabel = new QLabel(QStringLiteral("无数据"));
        noDataLabel->setAlignment(Qt::AlignCenter);
        noDataLabel->setStyleSheet(QStringLiteral("color: #999;"));
        portWidget->layout()->addWidget(noDataLabel);
        return;
    }

    // 根据数据类型显示
    DataObject::DataType dataType = data->getType();
    switch (dataType) {
    case DataObject::DataType::Image:
        displayImageData(portIndex, data);
        break;
    case DataObject::DataType::Region:
        displayRegionData(portIndex, data);
        break;
    case DataObject::DataType::Number:
        displayNumberData(portIndex, data);
        break;
    case DataObject::DataType::String:
        displayStringData(portIndex, data);
        break;
    default:
    {
        auto *typeLabel = new QLabel(
            QStringLiteral("数据类型: %1").arg(data->getTypeString()));
        typeLabel->setStyleSheet(QStringLiteral("color: #666;"));
        portWidget->layout()->addWidget(typeLabel);
        break;
    }
    }
}

void OutputDataViewer::displayImageData(int portIndex, QSharedPointer<DataObject> data)
{
    if (!data || !m_portWidgets.contains(portIndex)) return;

    QWidget *portWidget = m_portWidgets[portIndex];
    QLayout *layout = portWidget->layout();
    if (!layout) return;

    try {
        HalconCpp::HImage image = data->getHImage();
        if (!image.IsInitialized()) {
            auto *noImageLabel = new QLabel(QStringLiteral("图像未初始化"));
            noImageLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(noImageLabel);
            return;
        }

        // 获取图像信息
        HalconCpp::HTuple width, height;
        image.GetImageSize(&width, &height);
        HalconCpp::HTuple channels = image.CountChannels();

        // 图像信息
        auto *infoLabel = new QLabel(
            QStringLiteral("尺寸: %1 x %2\n通道: %3")
                .arg(width.I())
                .arg(height.I())
                .arg(channels.I()));
        infoLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #666;"));
        layout->addWidget(infoLabel);

        QImage preview;
        try {
            HalconCpp::HTuple pointer, type, wPtr, hPtr;
            if (channels.I() == 1) {
                HalconCpp::GetImagePointer1(image, &pointer, &type, &wPtr, &hPtr);
                if (QString::fromLatin1(type.S().Text()) == QLatin1String("byte")) {
                    preview = QImage(reinterpret_cast<uchar *>(pointer.L()),
                                     wPtr.I(), hPtr.I(), wPtr.I(), QImage::Format_Grayscale8).copy();
                }
            } else if (channels.I() == 3) {
                HalconCpp::HTuple pr, pg, pb, type3, w3, h3;
                HalconCpp::GetImagePointer3(image, &pr, &pg, &pb, &type3, &w3, &h3);
                if (QString::fromLatin1(type3.S().Text()) == QLatin1String("byte")) {
                    const int w = w3.I();
                    const int h = h3.I();
                    preview = QImage(w, h, QImage::Format_RGB888);
                    const uchar *r = reinterpret_cast<const uchar *>(pr.L());
                    const uchar *g = reinterpret_cast<const uchar *>(pg.L());
                    const uchar *b = reinterpret_cast<const uchar *>(pb.L());
                    for (int y = 0; y < h; ++y) {
                        uchar *out = preview.scanLine(y);
                        const uchar *rr = r + y * w;
                        const uchar *gg = g + y * w;
                        const uchar *bb = b + y * w;
                        for (int x = 0; x < w; ++x) {
                            out[x * 3 + 0] = rr[x];
                            out[x * 3 + 1] = gg[x];
                            out[x * 3 + 2] = bb[x];
                        }
                    }
                }
            }
        } catch (const HalconCpp::HException &) {
        }

        auto *imageLabel = new QLabel;
        imageLabel->setAlignment(Qt::AlignCenter);
        imageLabel->setMinimumSize(200, 150);
        imageLabel->setStyleSheet(QStringLiteral("border: 1px solid #444; background-color: #1e1e21;"));
        if (!preview.isNull()) {
            imageLabel->setPixmap(QPixmap::fromImage(preview).scaled(
                280, 200, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            imageLabel->setText(QStringLiteral("无法生成预览"));
        }
        layout->addWidget(imageLabel);

        // 放大按钮
        auto *zoomBtn = new QPushButton(QStringLiteral("放大查看"));
        zoomBtn->setFixedWidth(80);
        connect(zoomBtn, &QPushButton::clicked, this, [this, portIndex]() {
            emit imageClicked(m_currentNode, portIndex);
        });
        layout->addWidget(zoomBtn);

    } catch (const HalconCpp::HException &e) {
        auto *errorLabel = new QLabel(
            QStringLiteral("图像读取错误: %1").arg(QString::fromStdString(e.ErrorMessage().Text())));
        errorLabel->setStyleSheet(QStringLiteral("color: red;"));
        layout->addWidget(errorLabel);
    }
}

void OutputDataViewer::displayRegionData(int portIndex, QSharedPointer<DataObject> data)
{
    if (!data || !m_portWidgets.contains(portIndex)) return;

    QWidget *portWidget = m_portWidgets[portIndex];
    QLayout *layout = portWidget->layout();
    if (!layout) return;

    try {
        HalconCpp::HRegion region = data->getHRegion();
        if (!region.IsInitialized()) {
            auto *noRegionLabel = new QLabel(QStringLiteral("区域未初始化"));
            noRegionLabel->setAlignment(Qt::AlignCenter);
            layout->addWidget(noRegionLabel);
            return;
        }

        // 获取区域信息
        HalconCpp::HTuple area, row, column;
        HalconCpp::AreaCenter(region, &area, &row, &column);

        HalconCpp::HTuple row1, col1, row2, col2;
        HalconCpp::SmallestRectangle1(region, &row1, &col1, &row2, &col2);

        // 区域信息
        auto *infoLabel = new QLabel(
            QStringLiteral("面积: %1, 中心: (%2, %3)\n外接矩形: (%4, %5) - (%6, %7)")
                .arg(area.I())
                .arg(row.D(), 0, 'f', 1)
                .arg(column.D(), 0, 'f', 1)
                .arg(row1.D(), 0, 'f', 1)
                .arg(col1.D(), 0, 'f', 1)
                .arg(row2.D(), 0, 'f', 1)
                .arg(col2.D(), 0, 'f', 1));
        infoLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #666;"));
        layout->addWidget(infoLabel);

        // TODO: 区域可视化显示
        auto *regionLabel = new QLabel(QStringLiteral("区域可视化\n(待实现)"));
        regionLabel->setAlignment(Qt::AlignCenter);
        regionLabel->setMinimumSize(200, 150);
        regionLabel->setStyleSheet(QStringLiteral("border: 1px solid #ccc; background-color: #f0f0f0;"));
        layout->addWidget(regionLabel);

    } catch (const HalconCpp::HException &e) {
        auto *errorLabel = new QLabel(
            QStringLiteral("区域读取错误: %1").arg(QString::fromStdString(e.ErrorMessage().Text())));
        errorLabel->setStyleSheet(QStringLiteral("color: red;"));
        layout->addWidget(errorLabel);
    }
}

void OutputDataViewer::displayNumberData(int portIndex, QSharedPointer<DataObject> data)
{
    if (!data || !m_portWidgets.contains(portIndex)) return;

    QWidget *portWidget = m_portWidgets[portIndex];
    QLayout *layout = portWidget->layout();
    if (!layout) return;

    QVariant value = data->getData();
    auto *valueLabel = new QLabel(
        QStringLiteral("数值: <b>%1</b>").arg(value.toString()));
    valueLabel->setTextFormat(Qt::RichText);
    valueLabel->setStyleSheet(QStringLiteral("font-size: 14px;"));
    layout->addWidget(valueLabel);

    // 数值详情
    if (value.canConvert<double>()) {
        auto *detailLabel = new QLabel(
            QStringLiteral("浮点值: %1\n整数值: %2")
                .arg(value.toDouble(), 0, 'f', 6)
                .arg(value.toLongLong()));
        detailLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #666;"));
        layout->addWidget(detailLabel);
    }
}

void OutputDataViewer::displayStringData(int portIndex, QSharedPointer<DataObject> data)
{
    if (!data || !m_portWidgets.contains(portIndex)) return;

    QWidget *portWidget = m_portWidgets[portIndex];
    QLayout *layout = portWidget->layout();
    if (!layout) return;

    QString text = data->getData().toString();
    auto *textLabel = new QLabel(text);
    textLabel->setWordWrap(true);
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textLabel->setStyleSheet(QStringLiteral("font-size: 12px; padding: 8px; background-color: #f5f5f5; border-radius: 4px;"));
    layout->addWidget(textLabel);

    // 文本长度信息
    auto *lengthLabel = new QLabel(
        QStringLiteral("长度: %1 字符").arg(text.length()));
    lengthLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #666;"));
    layout->addWidget(lengthLabel);
}

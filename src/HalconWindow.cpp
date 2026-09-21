#include "HalconWindow.h"
#include "halconcpp/HalconCpp.h"
#include <QResizeEvent>
#include <QDebug>
#include <QPainter>
#include <QImage>
#include <QLineF>
#include <QPolygonF>
#include <QVector>
#include <string>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QFileDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QApplication>
#include <QSizePolicy>
#include "AppLog.h"

using namespace HalconCpp;

HalconWindow::HalconWindow(QWidget *parent)
    : QWidget(parent)
    , m_hWindow(0)
    , m_qImage(nullptr)
    , m_scaleFactor(1.0)
    , m_fitToWindow(true)
    , m_contextMenu(nullptr)
    , m_fitToWindowAction(nullptr)
    , m_zoomInAction(nullptr)
    , m_zoomOutAction(nullptr)
    , m_saveImageAction(nullptr)
    , m_pixelInfoLabel(nullptr)
    , m_imageOffsetX(0)
    , m_imageOffsetY(0)
    , m_isPanning(false)
{
    setMinimumSize(240, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setObjectName(QStringLiteral("HalconImagePanel"));
    setStyleSheet(QStringLiteral("QWidget#HalconImagePanel { background-color: #1e1e21; border: none; }"));
    setBackgroundRole(QPalette::Base);
    setAutoFillBackground(true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // 像素坐标/值提示（左上角）
    m_pixelInfoLabel = new QLabel(this);
    m_pixelInfoLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  padding: 5px;"
        "  font-family: Consolas;"
        "  font-size: 12px;"
        "  border-radius: 3px;"
        "}"
    );
    m_pixelInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_pixelInfoLabel->setFixedWidth(200);
    m_pixelInfoLabel->setFixedHeight(60);
    m_pixelInfoLabel->move(5, 5);
    m_pixelInfoLabel->raise();
    m_pixelInfoLabel->hide();

    // 缩放比例指示（右上角）
    m_zoomLabel = new QLabel(this);
    m_zoomLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: #80ff80;"
        "  padding: 4px 8px;"
        "  font-family: Consolas;"
        "  font-size: 12px;"
        "  font-weight: bold;"
        "  border-radius: 3px;"
        "}"
    );
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_zoomLabel->setFixedWidth(80);
    m_zoomLabel->setFixedHeight(24);
    m_zoomLabel->raise();
    m_zoomLabel->hide();

    // 字幕标签（底部居中）
    m_captionLabel = new QLabel(this);
    m_captionLabel->setStyleSheet(
        "QLabel {"
        "  background-color: rgba(0, 0, 0, 180);"
        "  color: white;"
        "  padding: 5px;"
        "  font-family: Consolas;"
        "  font-size: 12px;"
        "  border-radius: 3px;"
        "}"
    );
    m_captionLabel->setAlignment(Qt::AlignCenter);
    m_captionLabel->setFixedWidth(400);
    m_captionLabel->setFixedHeight(30);
    m_captionLabel->move(5, height() - 35);
    m_captionLabel->raise();
    m_captionLabel->hide();

    // 右键上下文菜单
    m_contextMenu = new QMenu(this);

    m_fitToWindowAction = new QAction("自适应窗口", this);
    m_fitToWindowAction->setCheckable(true);
    m_fitToWindowAction->setChecked(m_fitToWindow);
    connect(m_fitToWindowAction, &QAction::triggered, this, &HalconWindow::onFitToWindow);
    m_contextMenu->addAction(m_fitToWindowAction);

    m_contextMenu->addSeparator();

    m_zoomInAction = new QAction("放大 (滚轮↑)", this);
    connect(m_zoomInAction, &QAction::triggered, this, &HalconWindow::onZoomIn);
    m_contextMenu->addAction(m_zoomInAction);

    m_zoomOutAction = new QAction("缩小 (滚轮↓)", this);
    connect(m_zoomOutAction, &QAction::triggered, this, &HalconWindow::onZoomOut);
    m_contextMenu->addAction(m_zoomOutAction);

    m_contextMenu->addSeparator();

    m_saveImageAction = new QAction("保存图像", this);
    connect(m_saveImageAction, &QAction::triggered, this, &HalconWindow::onSaveImage);
    m_contextMenu->addAction(m_saveImageAction);

    VFP_DEBUG << "HalconWindow created, minimum size set to 100x100";
}

HalconWindow::~HalconWindow()
{
    if (m_hWindow.IsInitialized()) {
        try {
            m_hWindow.ClearWindow();
            VFP_DEBUG << "Halcon window cleared";
        } catch (HException &e) {
            VFP_DEBUG << "Exception during window destruction:" << e.ErrorMessage().Text();
        }
    }

    if (m_qImage) {
        delete m_qImage;
        m_qImage = nullptr;
    }

    if (m_contextMenu) {
        delete m_contextMenu;
        m_contextMenu = nullptr;
    }

    if (m_captionLabel) {
        delete m_captionLabel;
        m_captionLabel = nullptr;
    }
}

QImage HalconWindow::convertHImageToQImage(const HImage &image)
{
    if (!image.IsInitialized()) {
        VFP_DEBUG << "Error: Image is not initialized";
        return QImage();
    }

    try {
        HTuple width, height;
        GetImageSize(image, &width, &height);
        int w = width.I();
        int h = height.I();

        HTuple channels;
        CountChannels(image, &channels);
        int channelCount = channels.I();

        if (channelCount == 1) {
            HTuple pointer, type, widthByte, heightPixel;
            GetImagePointer1(image, &pointer, &type, &widthByte, &heightPixel);
            std::string typeStr = type.S().Text();

            if (typeStr == "byte") {
                // 灰度8位：按 HALCON 的**实际行距**包装，再 copy() 成 Qt 自有内存（一次深拷贝）。
                // 注意：HALCON 行是 4 字节对齐的，widthByte 可能大于 w——此前写死 w，
                // 宽度不是 4 的倍数时会错行（显示为斜纹）。
                const int stride = widthByte.I();
                QImage qImage((uchar*)pointer.L(), w, h, stride, QImage::Format_Grayscale8);
                return qImage.copy();
            }
            if (typeStr == "uint2") {
                // 16位无符号：归一化到8位显示（保留高8位）。
                // 同样必须按行距逐行处理：uint2 的行距也按 4 字节对齐（宽度为奇数时多 2 字节填充），
                // 而且 Qt 侧的 bytesPerLine 也可能有对齐填充，两端都不能按 w*h 线性访问。
                const int srcStrideShorts = widthByte.I() / 2;
                const ushort *src = (const ushort *)pointer.L();
                QImage qImage(w, h, QImage::Format_Grayscale8);
                for (int y = 0; y < h; ++y) {
                    const ushort *srcRow = src + static_cast<size_t>(y) * srcStrideShorts;
                    uchar *dstRow = qImage.scanLine(y);
                    for (int x = 0; x < w; ++x) dstRow[x] = (uchar)(srcRow[x] >> 8);
                }
                return qImage;
            }
        } else if (channelCount == 3) {
            HTuple pointerR, pointerG, pointerB, type, widthByte, heightPixel;
            GetImagePointer3(image, &pointerR, &pointerG, &pointerB, &type, &widthByte, &heightPixel);
            std::string typeStr = type.S().Text();

            if (typeStr == "byte") {
                // RGB三通道：逐行交错一次拷贝（行缓存友好，避免逐像素三层随机访问）
                const uchar *rData = (const uchar *)pointerR.L();
                const uchar *gData = (const uchar *)pointerG.L();
                const uchar *bData = (const uchar *)pointerB.L();
                const int stride = w * 3;
                QImage qImage(w, h, QImage::Format_RGB888);
                uchar *data = qImage.bits();

                for (int y = 0; y < h; ++y) {
                    uchar *out = data + y * stride;
                    const uchar *r = rData + y * w;
                    const uchar *g = gData + y * w;
                    const uchar *b = bData + y * w;
                    int x = 0;
                    // 4像素一块展开，减少循环开销
                    for (; x + 4 <= w; x += 4) {
                        out[0] = r[0]; out[1] = g[0]; out[2] = b[0];
                        out[3] = r[1]; out[4] = g[1]; out[5] = b[1];
                        out[6] = r[2]; out[7] = g[2]; out[8] = b[2];
                        out[9] = r[3]; out[10] = g[3]; out[11] = b[3];
                        out += 12; r += 4; g += 4; b += 4;
                    }
                    for (; x < w; ++x) {
                        out[0] = r[0]; out[1] = g[0]; out[2] = b[0];
                        out += 3; r += 1; g += 1; b += 1;
                    }
                }
                return qImage;
            }
        }

        VFP_DEBUG << "Unsupported image format";
        return QImage();
    } catch (HException &e) {
        VFP_DEBUG << "Error converting image to QImage:" << e.ErrorMessage().Text();
        return QImage();
    }
}

void HalconWindow::requestImage(const HImage &image, const QString &caption)
{
    if (!image.IsInitialized())
        return;
    const bool hadPending = m_pendingValid;
    m_pendingImage = image;          // 覆盖旧帧：latest wins（被合并的中间帧不做转换，直接丢弃）
    m_pendingCaption = caption;
    m_pendingValid = true;
    // 同一批只排一次兑现：已有待兑现时无需再排（多余的兑现调用会因 pendingValid=false 空转）
    if (!hadPending)
        QMetaObject::invokeMethod(this, &HalconWindow::flushPendingImage, Qt::QueuedConnection);
}

void HalconWindow::flushPendingImage()
{
    if (!m_pendingValid)
        return;
    const HImage image = m_pendingImage;
    const QString caption = m_pendingCaption;
    m_pendingValid = false;
    m_pendingImage = HImage();       // 释放待显示引用，避免长久持有大图
    m_pendingCaption.clear();
    setImage(image, caption);
}

void HalconWindow::setImage(const HImage &image)
{
    setImage(image, "");
}

void HalconWindow::setImage(const HImage &image, const QString &caption)
{
    VFP_DEBUG << "HalconWindow::setImage called";
    VFP_DEBUG << "Image initialized:" << image.IsInitialized();
    VFP_DEBUG << "Caption:" << caption;

    if (!image.IsInitialized()) {
        VFP_DEBUG << "Error: Image is not initialized";
        return;
    }

    QImage newImage = convertHImageToQImage(image);
    if (newImage.isNull()) {
        VFP_DEBUG << "Error: Failed to convert HImage to QImage";
        return;
    }

    bool sizeChanged = false;
    if (m_qImage) {
        sizeChanged = (m_qImage->size() != newImage.size());
        delete m_qImage;
    }
    m_qImage = new QImage(newImage);

    // 首次显示或图像尺寸变化时自动适应，否则保持用户的缩放/平移状态
    if (m_fitToWindow || sizeChanged) {
        fitToWindowSize();
        m_fitToWindow = true;
        m_fitToWindowAction->setChecked(true);
        // 尺寸变化时复位平移
        m_imageOffsetX = 0;
        m_imageOffsetY = 0;
    }
    // 否则保持用户当前的缩放和平移状态不变

    // 设置字幕
    setCaption(caption);
    
    m_pixelInfoLabel->hide();
    updateZoomLabel();
    update();
    VFP_DEBUG << "Image displayed successfully using QImage";
}

void HalconWindow::setOverlay(const QVector<OverlayShape> &shapes)
{
    m_overlay = shapes;
    update();
}

void HalconWindow::clearOverlay()
{
    m_overlay.clear();
    update();
}

void HalconWindow::setCaption(const QString &caption)
{
    if (m_captionLabel) {
        if (!caption.isEmpty()) {
            m_captionLabel->setText(caption);
            m_captionLabel->move(5, height() - 35);
            m_captionLabel->show();
        } else {
            m_captionLabel->hide();
        }
    }
}

void HalconWindow::clear()
{
    if (m_qImage) {
        delete m_qImage;
        m_qImage = nullptr;
    }
    m_overlay.clear();
    m_scaleFactor = 1.0;
    m_imageOffsetX = 0;
    m_imageOffsetY = 0;
    m_fitToWindow = true;
    m_pixelInfoLabel->hide();
    m_zoomLabel->hide();
    update();
}

HWindow HalconWindow::getHWindow() const
{
    return m_hWindow;
}

void HalconWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    VFP_DEBUG << "HalconWindow::resizeEvent called, new size:" << event->size();
    
    if (m_fitToWindow && m_qImage) {
        fitToWindowSize();
    }
}

void HalconWindow::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);

    if (m_qImage) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        int imageWidth = m_qImage->width() * m_scaleFactor;
        int imageHeight = m_qImage->height() * m_scaleFactor;

        // 在自适应模式下强制居中
        if (m_fitToWindow) {
            m_imageOffsetX = (std::max)(0, (width() - imageWidth) / 2);
            m_imageOffsetY = (std::max)(0, (height() - imageHeight) / 2);
        }

        painter.drawImage(QRect(m_imageOffsetX, m_imageOffsetY, imageWidth, imageHeight), *m_qImage);

        // 中心十字线
        if (m_showCrosshair) {
            const QColor chColor(255, 60, 60);
            painter.setPen(QPen(chColor, 1, Qt::DashLine));
            const int cx = m_imageOffsetX + imageWidth / 2;
            const int cy = m_imageOffsetY + imageHeight / 2;
            painter.drawLine(cx, m_imageOffsetY, cx, m_imageOffsetY + imageHeight);
            painter.drawLine(m_imageOffsetX, cy, m_imageOffsetX + imageWidth, cy);
        }

        // 结果叠加层（测量线/圆/点/标注，图像坐标系 → 视图坐标系）
        if (!m_overlay.isEmpty()) {
            const double sx = imageWidth / static_cast<double>(m_qImage->width());
            const double sy = imageHeight / static_cast<double>(m_qImage->height());
            auto toView = [&](const QPointF &p) {
                return QPointF(m_imageOffsetX + p.x() * sx, m_imageOffsetY + p.y() * sy);
            };
            for (const OverlayShape &s : m_overlay) {
                painter.setPen(QPen(s.color, 2));
                switch (s.type) {
                case OverlayShape::Type::Line:
                    painter.drawLine(toView(s.p1), toView(s.p2));
                    break;
                case OverlayShape::Type::Circle: {
                    const QPointF c = toView(s.p1);
                    const double r = s.radius > 0 ? s.radius * sx
                                                  : std::hypot((s.p2.x() - s.p1.x()) * sx,
                                                               (s.p2.y() - s.p1.y()) * sy);
                    painter.drawEllipse(c, r, r);
                    break;
                }
                case OverlayShape::Type::Point: {
                    const QPointF c = toView(s.p1);
                    painter.drawLine(c.x() - 8, c.y(), c.x() + 8, c.y());
                    painter.drawLine(c.x(), c.y() - 8, c.x(), c.y() + 8);
                    if (!s.text.isEmpty()) {
                        painter.setPen(QPen(s.color, 1));
                        painter.drawText(QPointF(c.x() + 10, c.y() - 8), s.text);
                    }
                    break;
                }
                case OverlayShape::Type::Points: {
                    for (const QPointF &p : s.points) {
                        const QPointF c = toView(p);
                        painter.drawLine(c.x() - 4, c.y(), c.x() + 4, c.y());
                        painter.drawLine(c.x(), c.y() - 4, c.x(), c.y() + 4);
                    }
                    break;
                }
                case OverlayShape::Type::Text:
                    painter.setPen(QPen(s.color, 1));
                    painter.drawText(toView(s.p1), s.text);
                    break;
                case OverlayShape::Type::RotatedRect: {
                    RoiShape rs;
                    rs.type = RoiType::RotatedRect;
                    rs.p1 = s.p1;
                    rs.width = s.width;
                    rs.height = s.height;
                    rs.angleDeg = s.angleDeg;
                    const auto corners = rotatedRectCorners(rs);
                    if (corners.size() == 4) {
                        QPolygonF poly;
                        for (const QPointF &p : corners)
                            poly.append(toView(p));
                        painter.drawPolygon(poly);
                    }
                    if (!s.text.isEmpty()) {
                        painter.setPen(QPen(s.color, 1));
                        painter.drawText(toView(s.p1) + QPointF(8, -8), s.text);
                    }
                    break;
                }
                }
            }
        }

        // 掩膜覆盖层（排除区半透明品红）
        drawMaskOverlay(painter);

        // ROI 覆盖层
        drawRoiOverlay(painter);
    }
}

void HalconWindow::wheelEvent(QWheelEvent *event)
{
    if (!m_qImage) {
        return;
    }

    double delta = event->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    updateScaleFactor(delta, event->position().toPoint());
}

void HalconWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_maskEditable) {
        if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
            m_maskPainting = true;
            m_maskPaintKeep = (event->button() == Qt::LeftButton);
            paintMaskAt(event->pos(), m_maskPaintKeep);
            return;
        }
    }

    if (roiInteractionActive() && event->button() == Qt::RightButton) {
        m_roiDrawing = false;
        m_activeHandle = RoiHandle::None;
        m_roiDraft = RoiShape();
        update();
        emit roiEditCanceled();
        return;
    }

    if (roiInteractionActive() && event->button() == Qt::LeftButton
        && m_roiShape.type != RoiType::None) {
        const RoiHandle hit = hitTestRoiHandle(event->pos());
        if (hit != RoiHandle::None) {
            QPointF imgPos;
            imagePosFromWidget(event->pos(), imgPos);
            m_activeHandle = hit;
            m_handleStartShape = m_roiShape;
            m_handleStartImg = imgPos;
            setCursor(Qt::SizeAllCursor);
            return;
        }
    }

    // ROI 绘制模式优先
    if (m_roiEditable) {
        if (event->button() == Qt::LeftButton) {
            QPointF imgPos;
            if (!imagePosFromWidget(event->pos(), imgPos)) {
                return;
            }
            if (m_roiType == RoiType::Point) {
                // 点：单击即完成
                m_roiShape = RoiShape();
                m_roiShape.type = RoiType::Point;
                m_roiShape.p1 = imgPos;
                m_roiDrawing = false;
                emit roiEdited(m_roiShape);
                update();
                return;
            }
            if (m_roiType == RoiType::Polygon) {
                // 多边形：每次单击追加顶点
                if (!m_roiDrawing) {
                    m_roiDraft = RoiShape();
                    m_roiDraft.type = RoiType::Polygon;
                    m_roiDrawing = true;
                }
                if (!m_roiDraft.points.contains(imgPos))
                    m_roiDraft.points.append(imgPos);
                update();
                return;
            }
            // 矩形/圆/线：按下开始绘制
            m_roiDrawing = true;
            m_roiDraft = RoiShape();
            m_roiDraft.type = m_roiType;
            m_roiDraft.p1 = imgPos;
            m_roiDraft.p2 = imgPos;
            setCursor(Qt::CrossCursor);
            update();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        m_fitToWindowAction->setChecked(m_fitToWindow);
        m_contextMenu->exec(event->globalPos());
    } else if (event->button() == Qt::LeftButton) {
        // 开始拖拽平移（仅在非自适应模式下）
        if (!m_fitToWindow && m_qImage) {
            m_isPanning = true;
            m_panStartPos = event->pos();
            m_panStartOffsetX = m_imageOffsetX;
            m_panStartOffsetY = m_imageOffsetY;
            setCursor(Qt::ClosedHandCursor);
        }
    }
}

void HalconWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_maskEditable && m_maskPainting) {
        paintMaskAt(event->pos(), m_maskPaintKeep);
        return;
    }

    if (m_activeHandle != RoiHandle::None) {
        QPointF imgPos;
        if (imagePosFromWidget(event->pos(), imgPos)) {
            applyHandleDrag(imgPos);
            update();
        }
        return;
    }

    if (roiInteractionActive() && m_roiShape.type != RoiType::None && !m_roiDrawing) {
        const RoiHandle hit = hitTestRoiHandle(event->pos());
        if (hit == RoiHandle::Rotate)
            setCursor(Qt::SizeHorCursor);
        else if (hit != RoiHandle::None)
            setCursor(Qt::SizeAllCursor);
        else if (m_roiEditable)
            setCursor(Qt::CrossCursor);
        else
            setCursor(Qt::ArrowCursor);
    }

    if (m_roiEditable && m_roiDrawing) {
        QPointF imgPos;
        if (imagePosFromWidget(event->pos(), imgPos)) {
            if (m_roiDraft.type == RoiType::Polygon) {
                // 多边形：预览最后一点跟随光标
                if (!m_roiDraft.points.isEmpty()) {
                    if (m_roiDraft.points.size() > 1 && m_roiDraft.points.last() != imgPos) {
                        // 用 points 末尾跟踪预览：简单起见直接追加并最后替换
                    }
                }
            } else {
                m_roiDraft.p2 = imgPos;
            }
            update();
        }
        return;
    }

    if (m_isPanning) {
        // 拖拽平移
        QPoint delta = event->pos() - m_panStartPos;
        m_imageOffsetX = m_panStartOffsetX + delta.x();
        m_imageOffsetY = m_panStartOffsetY + delta.y();
        m_fitToWindow = false;
        m_fitToWindowAction->setChecked(false);
        m_pixelInfoLabel->hide();
        update();
        return;
    }

    if (m_qImage) {
        updatePixelInfo(event->pos());
    }
    QWidget::mouseMoveEvent(event);
}

void HalconWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_maskEditable && m_maskPainting
        && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        m_maskPainting = false;
        emit maskEdited();
        return;
    }

    if (m_activeHandle != RoiHandle::None && event->button() == Qt::LeftButton) {
        m_activeHandle = RoiHandle::None;
        emit roiEdited(m_roiShape);
        setCursor(Qt::CrossCursor);
        update();
        return;
    }

    if (m_roiEditable && m_roiDrawing && event->button() == Qt::LeftButton) {
        finalizeRoi();
        return;
    }

    if (event->button() == Qt::LeftButton && m_isPanning) {
        m_isPanning = false;
        setCursor(Qt::CrossCursor);
    }
}

void HalconWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_roiEditable && m_roiType == RoiType::Polygon && m_roiDrawing
        && event->button() == Qt::LeftButton) {
        // 双击完成多边形
        finalizeRoi();
        return;
    }
    if (event->button() == Qt::LeftButton && m_qImage) {
        // 双击恢复自适应窗口
        m_fitToWindow = true;
        fitToWindowSize();
        m_imageOffsetX = 0;
        m_imageOffsetY = 0;
        m_fitToWindowAction->setChecked(true);
        updateZoomLabel();
        update();
    }
}

void HalconWindow::onFitToWindow()
{
    if (!m_qImage) {
        return;
    }

    m_fitToWindow = !m_fitToWindow;
    m_fitToWindowAction->setChecked(m_fitToWindow);
    
    if (m_fitToWindow) {
        fitToWindowSize();
        m_imageOffsetX = 0;
        m_imageOffsetY = 0;
    } else {
        m_scaleFactor = 1.0;
    }
    
    updateZoomLabel();
    update();
}

void HalconWindow::onZoomIn()
{
    if (!m_qImage) return;
    updateScaleFactor(1.5);
}

void HalconWindow::onZoomOut()
{
    if (!m_qImage) return;
    updateScaleFactor(1.0 / 1.5);
}

void HalconWindow::onSaveImage()
{
    if (!m_qImage) {
        VFP_DEBUG << "No image to save";
        return;
    }

    QString fileName = QFileDialog::getSaveFileName(
        this,
        "保存图像",
        "",
        "Images (*.png *.bmp *.jpg *.tif)"
    );

    if (!fileName.isEmpty()) {
        if (m_qImage->save(fileName)) {
            VFP_DEBUG << "Image saved to:" << fileName;
        } else {
            VFP_DEBUG << "Failed to save image:" << fileName;
        }
    }
}

// ==================== 图像显示工具栏操作 ====================

void HalconWindow::fitToWindow()
{
    if (!m_qImage) return;
    m_fitToWindow = true;
    fitToWindowSize();
    m_imageOffsetX = 0;
    m_imageOffsetY = 0;
    m_fitToWindowAction->setChecked(true);
    updateZoomLabel();
    update();
}

void HalconWindow::zoomIn()
{
    if (!m_qImage) return;
    updateScaleFactor(1.5);
}

void HalconWindow::zoomOut()
{
    if (!m_qImage) return;
    updateScaleFactor(1.0 / 1.5);
}

void HalconWindow::setActualSize()
{
    if (!m_qImage) return;
    m_scaleFactor = 1.0;
    m_fitToWindow = false;
    m_fitToWindowAction->setChecked(false);
    m_imageOffsetX = (width() - m_qImage->width()) / 2;
    m_imageOffsetY = (height() - m_qImage->height()) / 2;
    updateZoomLabel();
    update();
}

void HalconWindow::saveImage()
{
    onSaveImage();
}

void HalconWindow::setCrosshairVisible(bool on)
{
    m_showCrosshair = on;
    update();
}

void HalconWindow::updateScaleFactor(double delta, QPoint zoomCenter)
{
    if (!m_qImage) {
        return;
    }

    // 计算缩放前鼠标指向的图像坐标
    double imgXBefore = 0, imgYBefore = 0;
    if (zoomCenter.x() >= 0) {
        imgXBefore = (zoomCenter.x() - m_imageOffsetX) / m_scaleFactor;
        imgYBefore = (zoomCenter.y() - m_imageOffsetY) / m_scaleFactor;
    }

    double newScale = m_scaleFactor * delta;
    if (newScale < 0.05 || newScale > 50.0) {
        return;
    }
    
    m_scaleFactor = newScale;
    m_fitToWindow = false;
    m_fitToWindowAction->setChecked(false);

    // 缩放后调整偏移，使鼠标指向的图像坐标保持不变（缩放跟随光标）
    if (zoomCenter.x() >= 0) {
        m_imageOffsetX = zoomCenter.x() - static_cast<int>(imgXBefore * m_scaleFactor);
        m_imageOffsetY = zoomCenter.y() - static_cast<int>(imgYBefore * m_scaleFactor);
    }

    updateZoomLabel();
    update();
}

void HalconWindow::fitToWindowSize()
{
    if (!m_qImage) {
        return;
    }

    double scaleX = (double)width() / m_qImage->width();
    double scaleY = (double)height() / m_qImage->height();
    m_scaleFactor = (std::min)(scaleX, scaleY);

    updateZoomLabel();
}

bool HalconWindow::getImagePixelPos(const QPoint &widgetPos, int &imgX, int &imgY)
{
    if (!m_qImage) {
        return false;
    }

    int imageWidth = m_qImage->width() * m_scaleFactor;
    int imageHeight = m_qImage->height() * m_scaleFactor;

    int relX = widgetPos.x() - m_imageOffsetX;
    int relY = widgetPos.y() - m_imageOffsetY;

    if (relX < 0 || relX >= imageWidth || relY < 0 || relY >= imageHeight) {
        return false;
    }

    imgX = (int)(relX / m_scaleFactor);
    imgY = (int)(relY / m_scaleFactor);

    if (imgX >= m_qImage->width()) {
        imgX = m_qImage->width() - 1;
    }
    if (imgY >= m_qImage->height()) {
        imgY = m_qImage->height() - 1;
    }

    return true;
}

void HalconWindow::updatePixelInfo(const QPoint &pos)
{
    int imgX, imgY;
    
    if (!getImagePixelPos(pos, imgX, imgY)) {
        m_pixelInfoLabel->hide();
        return;
    }

    QColor pixelColor = m_qImage->pixelColor(imgX, imgY);
    
    QString info;
    if (m_qImage->format() == QImage::Format_Grayscale8) {
        int gray = qGray(m_qImage->pixel(imgX, imgY));
        info = QString("坐标: (%1, %2)\n灰度: %3")
            .arg(imgX)
            .arg(imgY)
            .arg(gray);
    } else {
        info = QString("坐标: (%1, %2)\nR: %3  G: %4  B: %5")
            .arg(imgX)
            .arg(imgY)
            .arg(pixelColor.red())
            .arg(pixelColor.green())
            .arg(pixelColor.blue());
    }

    m_pixelInfoLabel->setText(info);
    m_pixelInfoLabel->show();
}

void HalconWindow::updateZoomLabel()
{
    if (!m_zoomLabel) return;
    if (!m_qImage) {
        m_zoomLabel->hide();
        return;
    }

    int percent = static_cast<int>(m_scaleFactor * 100 + 0.5);
    m_zoomLabel->setText(QString("%1%").arg(percent));
    m_zoomLabel->adjustSize();
    m_zoomLabel->move(width() - m_zoomLabel->width() - 10, 10);
    m_zoomLabel->show();
}

// ==================== ROI 交互框架 ====================

void HalconWindow::setRoiEditable(bool editable, RoiType type)
{
    m_roiEditable = editable;
    m_roiType = type;
    m_roiDrawing = false;
    m_activeHandle = RoiHandle::None;
    m_roiDraft = RoiShape();
    if (editable)
        m_roiHandleEdit = true;
    setCursor(Qt::CrossCursor);
    update();
}

void HalconWindow::setRoiShape(const RoiShape &shape)
{
    m_roiShape = shape;
    if (shape.type != RoiType::None)
        m_roiHandleEdit = true;
    update();
}

void HalconWindow::clearRoi()
{
    m_roiShape = RoiShape();
    m_roiDraft = RoiShape();
    m_roiDrawing = false;
    m_activeHandle = RoiHandle::None;
    update();
}

void HalconWindow::setRoiHandleEditEnabled(bool on)
{
    m_roiHandleEdit = on;
    if (!on)
        m_activeHandle = RoiHandle::None;
    update();
}

void HalconWindow::setMaskEditable(bool editable)
{
    m_maskEditable = editable;
    m_maskPainting = false;
    if (editable) {
        m_roiEditable = false;
        ensureMaskForCurrentImage();
        setCursor(Qt::CrossCursor);
    }
    update();
}

void HalconWindow::setMaskImage(const QImage &mask)
{
    if (mask.isNull()) {
        m_maskImage = QImage();
    } else {
        m_maskImage = mask.convertToFormat(QImage::Format_Grayscale8);
    }
    update();
}

void HalconWindow::setMaskBrushRadius(int radius)
{
    m_maskBrushRadius = qBound(2, radius, 80);
}

void HalconWindow::ensureMaskForCurrentImage()
{
    if (!m_qImage)
        return;
    if (m_maskImage.isNull()
        || m_maskImage.width() != m_qImage->width()
        || m_maskImage.height() != m_qImage->height()) {
        m_maskImage = QImage(m_qImage->size(), QImage::Format_Grayscale8);
        m_maskImage.fill(255);
    }
}

void HalconWindow::paintMaskAt(const QPoint &widgetPos, bool keep)
{
    ensureMaskForCurrentImage();
    if (m_maskImage.isNull())
        return;
    QPointF imgPos;
    if (!imagePosFromWidget(widgetPos, imgPos))
        return;
    QPainter p(&m_maskImage);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(keep ? QColor(255, 255, 255) : QColor(0, 0, 0));
    p.drawEllipse(QPointF(imgPos), m_maskBrushRadius, m_maskBrushRadius);
    update();
}

void HalconWindow::drawMaskOverlay(QPainter &painter)
{
    if (m_maskImage.isNull() || !m_qImage)
        return;
    if (m_maskImage.size() != m_qImage->size())
        return;
    QImage overlay(m_qImage->size(), QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    for (int y = 0; y < m_maskImage.height(); ++y) {
        const uchar *src = m_maskImage.constScanLine(y);
        QRgb *dst = reinterpret_cast<QRgb *>(overlay.scanLine(y));
        for (int x = 0; x < m_maskImage.width(); ++x) {
            if (src[x] < 128)
                dst[x] = qRgba(200, 40, 140, 90);
        }
    }
    const int imageWidth = int(m_qImage->width() * m_scaleFactor);
    const int imageHeight = int(m_qImage->height() * m_scaleFactor);
    painter.drawImage(QRect(m_imageOffsetX, m_imageOffsetY, imageWidth, imageHeight), overlay);
}

QPoint HalconWindow::widgetPosFromImage(const QPointF &imgPos) const
{
    return QPoint(static_cast<int>(m_imageOffsetX + imgPos.x() * m_scaleFactor),
                  static_cast<int>(m_imageOffsetY + imgPos.y() * m_scaleFactor));
}

bool HalconWindow::imagePosFromWidget(const QPoint &widgetPos, QPointF &imgPos)
{
    if (!m_qImage) return false;
    int imgX, imgY;
    if (!getImagePixelPos(widgetPos, imgX, imgY))
        return false;
    imgPos = QPointF(imgX, imgY);
    return true;
}

void HalconWindow::drawRoiOverlay(QPainter &painter)
{
    QPen pen(QColor(0, 200, 80), 2);
    pen.setCosmetic(true);
    painter.setPen(pen);

    auto drawShape = [&](const RoiShape &shape) {
        if (shape.type == RoiType::None) return;
        switch (shape.type) {
        case RoiType::Rect: {
            QPointF tl(qMin(shape.p1.x(), shape.p2.x()), qMin(shape.p1.y(), shape.p2.y()));
            QPointF br(qMax(shape.p1.x(), shape.p2.x()), qMax(shape.p1.y(), shape.p2.y()));
            QPoint tlW = widgetPosFromImage(tl);
            QPoint brW = widgetPosFromImage(br);
            painter.drawRect(QRect(tlW, brW).normalized());
            break;
        }
        case RoiType::Circle: {
            QPointF c = widgetPosFromImage(shape.p1);
            QPointF e = widgetPosFromImage(shape.p2);
            double r = QLineF(c, e).length();
            painter.drawEllipse(c, r, r);
            painter.drawLine(c, e);
            break;
        }
        case RoiType::Line: {
            painter.drawLine(widgetPosFromImage(shape.p1), widgetPosFromImage(shape.p2));
            break;
        }
        case RoiType::Point: {
            QPoint c = widgetPosFromImage(shape.p1);
            painter.drawEllipse(c, 4, 4);
            painter.drawLine(c + QPoint(-6, 0), c + QPoint(6, 0));
            painter.drawLine(c + QPoint(0, -6), c + QPoint(0, 6));
            break;
        }
        case RoiType::Polygon: {
            if (shape.points.size() >= 2) {
                QPolygonF poly;
                for (const auto &p : shape.points)
                    poly.append(widgetPosFromImage(p));
                painter.drawPolyline(poly);
            }
            break;
        }
        case RoiType::RotatedRect: {
            const auto corners = rotatedRectCorners(shape);
            if (corners.size() == 4) {
                QPolygonF poly;
                for (const QPointF &p : corners)
                    poly.append(widgetPosFromImage(p));
                painter.drawPolygon(poly);
            }
            break;
        }
        default:
            break;
        }
    };

    // 草稿（正在绘制）优先，其次已确认 ROI
    if (m_roiDrawing && m_roiDraft.type != RoiType::None) {
        QPen dashedPen(QColor(255, 180, 60), 1, Qt::DashLine);
        dashedPen.setCosmetic(true);
        painter.setPen(dashedPen);
        RoiShape draft = m_roiDraft;
        if (m_roiType == RoiType::RotatedRect && draft.type == RoiType::RotatedRect) {
            draft.width = qAbs(draft.p2.x() - draft.p1.x());
            draft.height = qAbs(draft.p2.y() - draft.p1.y());
            draft.p1 = QPointF((m_roiDraft.p1.x() + m_roiDraft.p2.x()) * 0.5,
                               (m_roiDraft.p1.y() + m_roiDraft.p2.y()) * 0.5);
            draft.angleDeg = 0;
        }
        drawShape(draft);
    }
    painter.setPen(pen);
    drawShape(m_roiShape);
    if (!m_roiDrawing && m_roiShape.type != RoiType::None && roiInteractionActive())
        drawRoiHandles(painter, m_roiShape);
}

void HalconWindow::finalizeRoi()
{
    if (!m_roiDrawing) return;
    RoiShape done = m_roiDraft;
    if (m_roiType == RoiType::RotatedRect) {
        done.type = RoiType::RotatedRect;
        done.width = qAbs(m_roiDraft.p2.x() - m_roiDraft.p1.x());
        done.height = qAbs(m_roiDraft.p2.y() - m_roiDraft.p1.y());
        done.p1 = QPointF((m_roiDraft.p1.x() + m_roiDraft.p2.x()) * 0.5,
                          (m_roiDraft.p1.y() + m_roiDraft.p2.y()) * 0.5);
        done.p2 = QPointF();
        done.angleDeg = 0;
    }
    m_roiShape = done;
    m_roiDrawing = false;
    m_roiHandleEdit = true;
    setCursor(Qt::CrossCursor);
    emit roiEdited(m_roiShape);
    update();
}

void HalconWindow::drawRoiHandles(QPainter &painter, const RoiShape &shape)
{
    QVector<RoiHandle> hs;
    switch (shape.type) {
    case RoiType::Rect:
        hs = {RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3};
        break;
    case RoiType::RotatedRect:
        hs = {RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3, RoiHandle::Rotate};
        break;
    case RoiType::Line:
        hs = {RoiHandle::End1, RoiHandle::End2};
        break;
    case RoiType::Circle:
        hs = {RoiHandle::C0};
        break;
    case RoiType::Point:
        hs = {RoiHandle::C0};
        break;
    default:
        return;
    }

    if (shape.type == RoiType::RotatedRect && shape.width > 1 && shape.height > 1) {
        const double a = qDegreesToRadians(shape.angleDeg);
        const QPoint top = widgetPosFromImage(
            QPointF(shape.p1.x() + std::sin(a) * (shape.height * 0.5),
                    shape.p1.y() - std::cos(a) * (shape.height * 0.5)));
        const QPoint rot = widgetPosFromImage(handleImagePos(shape, RoiHandle::Rotate));
        painter.setPen(QPen(QColor(80, 180, 255), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(top, rot);
    }

    for (RoiHandle h : hs) {
        const QPoint w = widgetPosFromImage(handleImagePos(shape, h));
        if (h == RoiHandle::Rotate) {
            painter.setPen(QPen(QColor(80, 180, 255), 1));
            painter.setBrush(QColor(80, 180, 255));
            painter.drawEllipse(w, 5, 5);
        } else {
            painter.setPen(QPen(QColor(0, 200, 80), 1));
            painter.setBrush(QColor(255, 255, 255));
            painter.drawRect(QRect(w.x() - 4, w.y() - 4, 8, 8));
        }
    }
}

QPointF HalconWindow::handleImagePos(const RoiShape &shape, RoiHandle h) const
{
    if (shape.type == RoiType::Rect) {
        const QRectF r = roiAxisAlignedBounds(shape);
        if (h == RoiHandle::C0) return r.topLeft();
        if (h == RoiHandle::C1) return r.topRight();
        if (h == RoiHandle::C2) return r.bottomRight();
        if (h == RoiHandle::C3) return r.bottomLeft();
        if (h == RoiHandle::Move) return r.center();
    } else if (shape.type == RoiType::RotatedRect) {
        const auto c = rotatedRectCorners(shape);
        if (c.size() == 4) {
            if (h == RoiHandle::C0) return c[0];
            if (h == RoiHandle::C1) return c[1];
            if (h == RoiHandle::C2) return c[2];
            if (h == RoiHandle::C3) return c[3];
        }
        if (h == RoiHandle::Move) return shape.p1;
        if (h == RoiHandle::Rotate) {
            const double a = qDegreesToRadians(shape.angleDeg);
            const double lift = shape.height * 0.5 + 28.0;
            return QPointF(shape.p1.x() + std::sin(a) * lift,
                           shape.p1.y() - std::cos(a) * lift);
        }
    } else if (shape.type == RoiType::Line) {
        if (h == RoiHandle::End1) return shape.p1;
        if (h == RoiHandle::End2) return shape.p2;
        if (h == RoiHandle::Move) return (shape.p1 + shape.p2) * 0.5;
    } else if (shape.type == RoiType::Circle) {
        if (h == RoiHandle::Move) return shape.p1;
        if (h == RoiHandle::C0) return shape.p2;
    } else if (shape.type == RoiType::Point) {
        if (h == RoiHandle::Move || h == RoiHandle::C0) return shape.p1;
    }
    return QPointF();
}

HalconWindow::RoiHandle HalconWindow::hitTestRoiHandle(const QPoint &widgetPos) const
{
    if (m_roiShape.type == RoiType::None)
        return RoiHandle::None;

    QVector<RoiHandle> hs;
    switch (m_roiShape.type) {
    case RoiType::Rect:
        hs = {RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3};
        break;
    case RoiType::RotatedRect:
        hs = {RoiHandle::Rotate, RoiHandle::C0, RoiHandle::C1, RoiHandle::C2, RoiHandle::C3};
        break;
    case RoiType::Line:
        hs = {RoiHandle::End1, RoiHandle::End2};
        break;
    case RoiType::Circle:
        hs = {RoiHandle::C0};
        break;
    case RoiType::Point:
        hs = {RoiHandle::C0};
        break;
    default:
        break;
    }

    const double thresh2 = 8.0 * 8.0;
    for (RoiHandle h : hs) {
        const QPoint w = widgetPosFromImage(handleImagePos(m_roiShape, h));
        const double dx = double(w.x() - widgetPos.x());
        const double dy = double(w.y() - widgetPos.y());
        if (dx * dx + dy * dy <= thresh2)
            return h;
    }

    if (!m_qImage || m_scaleFactor <= 1e-9)
        return RoiHandle::None;
    const QPointF img((widgetPos.x() - m_imageOffsetX) / m_scaleFactor,
                      (widgetPos.y() - m_imageOffsetY) / m_scaleFactor);

    if (m_roiShape.type == RoiType::Rect) {
        if (roiAxisAlignedBounds(m_roiShape).contains(img))
            return RoiHandle::Move;
    } else if (m_roiShape.type == RoiType::RotatedRect && m_roiShape.width > 1 && m_roiShape.height > 1) {
        const double a = qDegreesToRadians(m_roiShape.angleDeg);
        const double c = std::cos(a);
        const double sn = std::sin(a);
        const double dx = img.x() - m_roiShape.p1.x();
        const double dy = img.y() - m_roiShape.p1.y();
        const double lx = c * dx + sn * dy;
        const double ly = -sn * dx + c * dy;
        if (std::abs(lx) <= m_roiShape.width * 0.5 && std::abs(ly) <= m_roiShape.height * 0.5)
            return RoiHandle::Move;
    } else if (m_roiShape.type == RoiType::Line) {
        const QPoint a = widgetPosFromImage(m_roiShape.p1);
        const QPoint b = widgetPosFromImage(m_roiShape.p2);
        const QPointF ab(b.x() - a.x(), b.y() - a.y());
        const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (len2 > 1.0) {
            const QPointF ap(widgetPos.x() - a.x(), widgetPos.y() - a.y());
            const double t = qBound(0.0, (ap.x() * ab.x() + ap.y() * ab.y()) / len2, 1.0);
            const QPointF proj(a.x() + ab.x() * t, a.y() + ab.y() * t);
            if (QLineF(QPointF(widgetPos), proj).length() <= 8.0)
                return RoiHandle::Move;
        }
    } else if (m_roiShape.type == RoiType::Circle) {
        const double r = QLineF(m_roiShape.p1, m_roiShape.p2).length();
        if (QLineF(m_roiShape.p1, img).length() <= r)
            return RoiHandle::Move;
    }
    return RoiHandle::None;
}

void HalconWindow::applyHandleDrag(const QPointF &imgPos)
{
    const QPointF delta = imgPos - m_handleStartImg;
    RoiShape s = m_handleStartShape;
    const RoiHandle h = m_activeHandle;

    if (s.type == RoiType::Rect) {
        QPointF tl(qMin(s.p1.x(), s.p2.x()), qMin(s.p1.y(), s.p2.y()));
        QPointF br(qMax(s.p1.x(), s.p2.x()), qMax(s.p1.y(), s.p2.y()));
        if (h == RoiHandle::Move) {
            tl += delta;
            br += delta;
        } else if (h == RoiHandle::C0) {
            tl = imgPos;
        } else if (h == RoiHandle::C1) {
            tl.setY(imgPos.y());
            br.setX(imgPos.x());
        } else if (h == RoiHandle::C2) {
            br = imgPos;
        } else if (h == RoiHandle::C3) {
            tl.setX(imgPos.x());
            br.setY(imgPos.y());
        }
        if (br.x() < tl.x() + 2.0)
            br.setX(tl.x() + 2.0);
        if (br.y() < tl.y() + 2.0)
            br.setY(tl.y() + 2.0);
        s.p1 = tl;
        s.p2 = br;
    } else if (s.type == RoiType::RotatedRect) {
        if (h == RoiHandle::Move) {
            s.p1 += delta;
        } else if (h == RoiHandle::Rotate) {
            s.angleDeg = qRadiansToDegrees(std::atan2(imgPos.x() - s.p1.x(),
                                                      s.p1.y() - imgPos.y()));
        } else if (h == RoiHandle::C0 || h == RoiHandle::C1
                   || h == RoiHandle::C2 || h == RoiHandle::C3) {
            const int i = int(h) - int(RoiHandle::C0);
            const auto corners = rotatedRectCorners(s);
            if (corners.size() == 4) {
                const QPointF opp = corners[(i + 2) % 4];
                const double a = qDegreesToRadians(s.angleDeg);
                const double c = std::cos(a);
                const double sn = std::sin(a);
                const QPointF vec = imgPos - opp;
                s.width = qMax(2.0, std::abs(c * vec.x() + sn * vec.y()));
                s.height = qMax(2.0, std::abs(-sn * vec.x() + c * vec.y()));
                s.p1 = (opp + imgPos) * 0.5;
            }
        }
    } else if (s.type == RoiType::Line) {
        if (h == RoiHandle::End1)
            s.p1 = imgPos;
        else if (h == RoiHandle::End2)
            s.p2 = imgPos;
        else if (h == RoiHandle::Move) {
            s.p1 += delta;
            s.p2 += delta;
        }
    } else if (s.type == RoiType::Circle) {
        if (h == RoiHandle::Move) {
            s.p1 += delta;
            s.p2 += delta;
        } else if (h == RoiHandle::C0) {
            s.p2 = imgPos;
        }
    } else if (s.type == RoiType::Point) {
        s.p1 = imgPos;
    }
    m_roiShape = s;
}

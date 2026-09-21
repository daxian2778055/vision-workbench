#pragma once

#include <QWidget>
#include <QImage>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <QVector>
#include <cmath>
#include <QtMath>
#include <HalconCpp.h>

using namespace HalconCpp;

/// ROI 形状类型
enum class RoiType {
    None,
    Rect,          /// 轴对齐矩形（p1=左上, p2=右下）
    Circle,        /// 圆（p1=圆心, p2=圆上一点）
    Line,          /// 直线（p1→p2）
    Point,         /// 点（p1）
    Polygon,       /// 多边形（points）
    RotatedRect,   /// 旋转矩形（p1=中心, width/height, angleDeg）
};

/// ROI 形状（图像坐标系，x=列, y=行）
struct RoiShape {
    RoiType type = RoiType::None;
    QPointF p1;
    QPointF p2;
    QVector<QPointF> points; /// Polygon 时使用
    double angleDeg = 0.0;   /// RotatedRect：逆时针角度（度）
    double width = 0.0;      /// RotatedRect：宽
    double height = 0.0;     /// RotatedRect：高
};
Q_DECLARE_METATYPE(RoiShape)

inline QVector<QPointF> rotatedRectCorners(const RoiShape &s)
{
    const double a = qDegreesToRadians(s.angleDeg);
    const double c = std::cos(a);
    const double sn = std::sin(a);
    const double hw = s.width * 0.5;
    const double hh = s.height * 0.5;
    const double xs[4] = {-hw, hw, hw, -hw};
    const double ys[4] = {-hh, -hh, hh, hh};
    QVector<QPointF> pts;
    pts.reserve(4);
    for (int i = 0; i < 4; ++i) {
        pts.append(QPointF(s.p1.x() + c * xs[i] - sn * ys[i],
                           s.p1.y() + sn * xs[i] + c * ys[i]));
    }
    return pts;
}

inline QRectF roiAxisAlignedBounds(const RoiShape &s)
{
    if (s.type == RoiType::RotatedRect && s.width > 1 && s.height > 1) {
        const auto pts = rotatedRectCorners(s);
        double x0 = pts[0].x(), x1 = pts[0].x(), y0 = pts[0].y(), y1 = pts[0].y();
        for (const QPointF &p : pts) {
            x0 = qMin(x0, p.x()); x1 = qMax(x1, p.x());
            y0 = qMin(y0, p.y()); y1 = qMax(y1, p.y());
        }
        return QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    if (s.type == RoiType::Rect) {
        return QRectF(QPointF(qMin(s.p1.x(), s.p2.x()), qMin(s.p1.y(), s.p2.y())),
                      QPointF(qMax(s.p1.x(), s.p2.x()), qMax(s.p1.y(), s.p2.y())));
    }
    return QRectF();
}

/// 结果叠加图元（图像坐标系，绘制在图像上方）
struct OverlayShape {
    enum class Type { Line, Circle, Point, Points, Text, RotatedRect };
    Type type = Type::Line;
    QPointF p1;                 /// Line: 起点；Circle: 圆心；Point/Text: 位置；RotatedRect: 中心
    QPointF p2;                 /// Line: 终点；Circle: 圆上一点（求半径）
    double radius = 0.0;        /// Circle: 半径（优先于 p2）
    double angleDeg = 0.0;      /// RotatedRect
    double width = 0.0;         /// RotatedRect
    double height = 0.0;        /// RotatedRect
    QVector<QPointF> points;    /// Points: 点集
    QString text;               /// Text/Point: 标注文本
    QColor color {0, 255, 0};   /// 默认绿色
};
Q_DECLARE_METATYPE(OverlayShape)

class HalconWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HalconWindow(QWidget *parent = nullptr);
    ~HalconWindow();

    // 设置图像
    void setImage(const HImage &image);
    void setImage(const HImage &image, const QString &caption);

    /// S6：合并式推送——只保留"最新一帧"，在同一轮事件循环内合并同一控件的多次推送后再转换/显示。
    /// 运行界面控件用它替代 setImage：高频轮次（尤其 imageAvailable 会按节点推送）下避免 UI 线程积压，
    /// 积压会表现为"显示旧帧 + 界面卡"。语义不变：最终显示的仍是最后一帧（与逐帧推送的末帧一致），
    /// 只是省掉被合并掉的中间帧的 HImage→QImage 深拷贝开销。
    void requestImage(const HImage &image, const QString &caption);

    /// 设置结果叠加图元（替换式；图像坐标系）
    void setOverlay(const QVector<OverlayShape> &shapes);
    /// 清空叠加
    void clearOverlay();
    
    // 清除图像
    void clear();
    
    // 获取Halcon窗口
    HWindow getHWindow() const;
    
    // 设置字幕
    void setCaption(const QString &caption);

    // ---- 图像显示工具栏操作 ----
    /// 自适应窗口（始终置为适应模式）
    void fitToWindow();
    /// 放大
    void zoomIn();
    /// 缩小
    void zoomOut();
    /// 实际大小（100%）
    void setActualSize();
    /// 保存当前图像
    void saveImage();
    /// 显示/隐藏中心十字线
    void setCrosshairVisible(bool on);
    bool crosshairVisible() const { return m_showCrosshair; }
    /// 当前缩放比例（像素显示倍率）
    double scaleFactor() const { return m_scaleFactor; }
    /// 是否有图像
    bool hasImage() const { return m_qImage != nullptr; }

    // ---- ROI 交互框架 ----
    /// 进入/退出 ROI 绘制模式（type 指定要绘制的形状）
    void setRoiEditable(bool editable, RoiType type = RoiType::Rect);
    bool isRoiEditable() const { return m_roiEditable; }
    /// 设置当前显示的 ROI（用于算子回显已配置的 ROI）
    void setRoiShape(const RoiShape &shape);
    RoiShape roiShape() const { return m_roiShape; }
    /// 清除 ROI
    void clearRoi();
    /// 允许拖改已有 ROI 控制点（不必先点「绘制」）
    void setRoiHandleEditEnabled(bool on);
    bool isRoiHandleEditEnabled() const { return m_roiHandleEdit; }

    /// 进入/退出掩膜涂抹（左键保留/涂白，右键擦除/涂黑）
    void setMaskEditable(bool editable);
    bool isMaskEditable() const { return m_maskEditable; }
    void setMaskImage(const QImage &mask);
    QImage maskImage() const { return m_maskImage; }
    void setMaskBrushRadius(int radius);
    void ensureMaskForCurrentImage();

signals:
    /// 用户完成 ROI 绘制时发出（图像坐标系）
    void roiEdited(const RoiShape &shape);
    void roiEditCanceled();
    void maskEdited();

protected:
    // 事件处理
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    // 上下文菜单槽函数
    void onFitToWindow();
    void onZoomIn();
    void onZoomOut();
    void onSaveImage();

private:
    // 内部方法
    QImage convertHImageToQImage(const HImage &image);
    /// S6：兑现待显示帧（合并推送的落地动作，由 requestImage 排入事件队列）
    void flushPendingImage();
    void updateScaleFactor(double delta, QPoint zoomCenter = QPoint(-1, -1));
    void fitToWindowSize();
    bool getImagePixelPos(const QPoint &widgetPos, int &imgX, int &imgY);
    void updatePixelInfo(const QPoint &pos);
    void updateZoomLabel();
    QPoint widgetPosFromImage(const QPointF &imgPos) const;
    bool imagePosFromWidget(const QPoint &widgetPos, QPointF &imgPos);
    enum class RoiHandle {
        None, Move,
        C0, C1, C2, C3,
        Rotate,
        End1, End2
    };

    void drawRoiOverlay(QPainter &painter);
    void drawMaskOverlay(QPainter &painter);
    void drawRoiHandles(QPainter &painter, const RoiShape &shape);
    void finalizeRoi();
    void paintMaskAt(const QPoint &widgetPos, bool keep);
    RoiHandle hitTestRoiHandle(const QPoint &widgetPos) const;
    QPointF handleImagePos(const RoiShape &shape, RoiHandle h) const;
    void applyHandleDrag(const QPointF &imgPos);
    bool roiInteractionActive() const { return m_roiEditable || m_roiHandleEdit; }

    // 成员变量
    HWindow m_hWindow;
    QImage *m_qImage;
    double m_scaleFactor;
    bool m_fitToWindow;
    QMenu *m_contextMenu;
    QAction *m_fitToWindowAction;
    QAction *m_zoomInAction;
    QAction *m_zoomOutAction;
    QAction *m_saveImageAction;
    QLabel *m_pixelInfoLabel;
    QLabel *m_captionLabel;
    QLabel *m_zoomLabel;
    // S6：合并推送的待显示帧（HImage 按值持有 = HALCON 引用计数保活；flush 后立即释放引用）
    HImage m_pendingImage;
    QString m_pendingCaption;
    bool m_pendingValid = false;
    int m_imageOffsetX;
    int m_imageOffsetY;
    bool m_showCrosshair = false;   /// 是否显示中心十字线

    // 拖拽平移
    bool m_isPanning;
    QPoint m_panStartPos;
    int m_panStartOffsetX;
    int m_panStartOffsetY;

    // ---- ROI 状态 ----
    bool m_roiEditable = false;    /// 是否处于 ROI 绘制模式
    RoiType m_roiType = RoiType::Rect;
    RoiShape m_roiShape;           /// 当前 ROI（显示用）
    RoiShape m_roiDraft;           /// 绘制中的草稿
    bool m_roiDrawing = false;     /// 是否正在绘制
    bool m_roiHandleEdit = false;  /// 允许拖改已有 ROI
    RoiHandle m_activeHandle = RoiHandle::None;
    RoiShape m_handleStartShape;
    QPointF m_handleStartImg;

    bool m_maskEditable = false;
    QImage m_maskImage;
    int m_maskBrushRadius = 16;
    bool m_maskPainting = false;
    bool m_maskPaintKeep = true;

    // ---- 结果叠加 ----
    QVector<OverlayShape> m_overlay;  /// 测量/拟合结果叠加图元
};

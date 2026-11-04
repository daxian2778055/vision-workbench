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
#include "RoiShape.h"   // ROI 三型与两个内联助手（切片 B 图元族首抽自此头拆出）
#include <HalconCpp.h>

using namespace HalconCpp;

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
    /// 实装 4.4：多选图像叠加（在 base 图上以 50% 透明度叠画，尺寸不同时拉伸到 base 区域）
    void setHImageOverlays(const QVector<HImage> &images);
    void clearHImageOverlays();
    int hImageOverlayCount() const { return m_imageOverlays.size(); }
    /// 叠加透明度（0~100），默认 50
    void setHImageOverlayOpacity(int percent);
    int hImageOverlayOpacity() const { return m_imageOverlayOpacity; }
    
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
    /// 十字辅助线配置（实装 4.4 口径：两条独立辅助线，坐标按图像像素）
    struct CrosshairConfig {
        bool visible = false;
        double x = -1.0;            ///< 图像 X 坐标（像素）；<0 表示首帧自动居中
        double y = -1.0;            ///< 图像 Y 坐标（像素）
        int thickness = 1;          ///< 线宽 1~10
        QColor color {255, 60, 60};
        bool locked = false;        ///< 锁定后不可在预览画面上拖动
    };
    /// 替换式设置辅助线（不足两条时补默认值，多余丢弃）
    void setCrosshairConfigs(const QVector<CrosshairConfig> &configs);
    QVector<CrosshairConfig> crosshairConfigs() const { return m_crosshairConfigs; }
    /// 工具栏开关：同时显示/隐藏全部辅助线
    void setCrosshairVisible(bool on);
    bool crosshairVisible() const;
    /// P2-5：当前显示图像 / 叠加图元的只读快照（全屏查看用）
    QImage currentQImage() const { return m_qImage ? *m_qImage : QImage(); }
    QVector<OverlayShape> overlayShapes() const { return m_overlay; }
    /// 用已转换的 QImage 直接设置画面（不经过 HALCON 转换；全屏查看/截图复用）
    void setQImage(const QImage &image, const QString &caption = QString());
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
    /// ROI 主色（模板框=绿、搜索区域=青；只影响绘制，不影响数据）
    void setRoiColor(const QColor &color);
    /// 模板特征点（图像坐标，绿点）：打开工具/选中算子时画出"这次匹配到底用了哪些点"
    void setFeaturePoints(const QVector<QPointF> &points);
    /// 自由绘制（套索）：进入后按下拖动即连续取点，松手收笔（仅对 Polygon 有效）
    void setRoiFreehand(bool on);
    /// 允许拖改已有 ROI 控制点（不必先点「绘制」）
    void setRoiHandleEditEnabled(bool on);
    bool isRoiHandleEditEnabled() const { return m_roiHandleEdit; }

    /// 进入/退出掩膜涂抹（左键保留/涂白，右键擦除/涂黑）
    void setMaskEditable(bool editable);
    bool isMaskEditable() const { return m_maskEditable; }
    void setMaskImage(const QImage &mask);
    QImage maskImage() const { return m_maskImage; }
    void setMaskBrushRadius(int radius);
    /// 掩膜模式（r19 补全清单第 2 项）：true=涂抹保留、false=擦除
    void setMaskPaintKeep(bool keep);
    void ensureMaskForCurrentImage();

signals:
    /// P2-5：画面内容（图像或叠加图元）发生变化后发出，供双画面副视图同步
    void displayContentChanged();
    /// 十字辅助线配置变化（位置/颜色/线宽/锁定），供双画面副视图同步
    void crosshairConfigsChanged();
    /// 用户完成 ROI 绘制时发出（图像坐标系）
    void roiEdited(const RoiShape &shape);
    void roiEditCanceled();
    void maskEdited();
    /// 非绘制状态下左键单击的图像坐标（魔术棒取点用）
    void imageClicked(const QPointF &imagePos);

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
    void drawRoiOverlay(QPainter &painter);
    void drawMaskOverlay(QPainter &painter);
    void drawRoiHandles(QPainter &painter, const RoiShape &shape);
    void finalizeRoi();
    void paintMaskAt(const QPoint &widgetPos, bool keep);
    RoiHandle hitTestRoiHandle(const QPoint &widgetPos) const;
    QPointF handleImagePos(const RoiShape &shape, RoiHandle h) const;
    void applyHandleDrag(const QPointF &imgPos);
    bool roiInteractionActive() const { return m_roiEditable || m_roiHandleEdit; }
    /// 辅助线命中测试（返回索引，-1 = 未命中）
    int hitTestCrosshair(const QPoint &widgetPos) const;
    /// 辅助线交叉点在视图坐标系中的位置（无图像/未启用返回无效点）
    QPointF crosshairViewPos(const CrosshairConfig &config) const;
    /// 首帧居中 / 图像尺寸变化后夹紧辅助线像素坐标
    void normalizeCrosshairPositions();

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
    QVector<CrosshairConfig> m_crosshairConfigs {CrosshairConfig(), CrosshairConfig()};
    int m_dragCrosshair = -1;       ///< 正在拖动的辅助线索引（-1 = 未拖动）

    // 拖拽平移
    bool m_isPanning;
    QPoint m_panStartPos;
    int m_panStartOffsetX;
    int m_panStartOffsetY;

    // ---- ROI 状态 ----
    bool m_roiEditable = false;    /// 是否处于 ROI 绘制模式
    RoiType m_roiType = RoiType::Rect;
    QColor m_roiColor {0, 200, 80}; /// ROI 主色（模板框绿 / 搜索区域青）
    QVector<QPointF> m_featurePoints;   /// 模板特征点（图像坐标）
    RoiShape m_roiShape;           /// 当前 ROI（显示用）
    RoiShape m_roiDraft;           /// 绘制中的草稿
    bool m_roiDrawing = false;     /// 是否正在绘制
    bool m_roiHandleEdit = false;  /// 允许拖改已有 ROI
    bool m_roiFreehand = false;    /// 自由绘制（套索）：拖动连续取点
    /// 多边形绘制：光标预览点（从最后一个顶点到光标的橡皮筋）
    QPointF m_roiHoverImg;
    bool m_roiHoverValid = false;
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
    QVector<QImage> m_imageOverlays;  /// 多选图像叠加（已转换为 QImage）
    int m_imageOverlayOpacity = 50;   /// 图像叠加透明度（%）
};

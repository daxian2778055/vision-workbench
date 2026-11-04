#pragma once

#include <QGraphicsView>

class FlowEagleEye;
class QLabel;
class QToolButton;
class QWidget;

/// 流程画布视图（P1-4，对标 VisionMaster 4.4 画布操作）：
/// - Ctrl + 滚轮缩放 / 右下角 −/＋/百分比/适应窗口
/// - 鹰眼：右下角缩略图显示整张流程，点击/拖动定位视图
/// 仅做视图层能力，不改变场景数据与执行语义。
class FlowCanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit FlowCanvasView(QGraphicsScene *scene, QWidget *parent = nullptr);

    /// 让整张流程（含留白）适应视口
    void fitToFlow();
    /// 按倍率缩放（1.0 = 100%），受 10%~800% 限制
    void zoomBy(double factor);
    void setZoomPercent(int percent);
    int zoomPercent() const;

    void setEagleEyeVisible(bool on);
    bool isEagleEyeVisible() const;

    /// P2-4：画布右下角耗时角标（流程耗时 + 当前选中模块耗时）
    void setFlowElapsedMs(qint64 elapsedMs);
    void setSelectedNodeElapsed(const QString &nodeName, qint64 elapsedMs);
    void clearSelectedNodeElapsed();

signals:
    void zoomPercentChanged(int percent);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void buildControls();
    void updateControlsPosition();
    void refreshZoomLabel();

    FlowEagleEye *m_eagle = nullptr;
    QWidget *m_controls = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QToolButton *m_eagleBtn = nullptr;
    QLabel *m_timingLabel = nullptr;      ///< P2-4 耗时角标
    qint64 m_flowElapsedMs = -1;
    QString m_selectedNodeName;
    qint64 m_selectedNodeElapsedMs = -1;
    void refreshTimingLabel();
};

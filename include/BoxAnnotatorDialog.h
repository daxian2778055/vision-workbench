#pragma once

#include "YoloAnnotation.h"

#include <QDialog>
#include <QCloseEvent>
#include <QImage>
#include <QPixmap>

class QComboBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QSpinBox;

/// 画框画布：显示图像、鼠标拖出矩形、叠加已有标注
class BoxAnnotationCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit BoxAnnotationCanvas(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    void setBoxes(const QVector<YoloBoxLabel> &boxes, const QStringList &classNames);
    void setCurrentClass(int cls) { m_currentClass = cls; }
    int selectedIndex() const { return m_selected; }

signals:
    /// 用户拖出一个新框（像素坐标，已按显示比例换算回原图）
    void boxDrawn(const QRectF &pixels, bool shiftKey);
    /// 点击选中某框（-1 = 空白处）
    void boxSelected(int index);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QRectF displayRect() const;
    QPointF toImage(const QPointF &widgetPos) const;

    QImage m_image;
    QVector<YoloBoxLabel> m_boxes;
    QStringList m_classNames;
    int m_currentClass = 0;
    int m_selected = -1;
    QPointF m_dragStart;
    QPointF m_dragCur;
    bool m_dragging = false;
};

/// 检测框标注器（2026-10-28，YOLOX 路线首片）
///
/// 只做一件事：把框画出来并存成 YOLO 格式的同名 txt（`cls cx cy w h` 归一化）。
/// 不做训练、不改类别体系——类名来自数据集目录下的 classes.txt，与训练工具共享同一份。
class BoxAnnotatorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BoxAnnotatorDialog(QWidget *parent = nullptr);
    BoxAnnotatorDialog(const QString &dir, QWidget *parent = nullptr);

protected:
    /// 关窗/拒绝前必须保存：标完一张就关是最常见用法（2026-10-28 P2-1）
    void closeEvent(QCloseEvent *e) override;
    void reject() override;

private slots:
    void onChooseDir();
    void onImageChanged(int row);
    void onBoxDrawn(const QRectF &px, bool shift);
    void onBoxSelected(int index);
    void onDeleteBox();
    void onClearImage();

private:
    void reloadImages();
    void loadCurrentImage();
    /// 保存当前图（2026-10-29 复核 P2-12：返回是否真的写成功——磁盘满时不能装作已存）
    bool saveCurrentImage();
    QString currentImagePath() const;
    void refreshBoxList();

    QString m_dir;
    QStringList m_images;
    QVector<YoloBoxLabel> m_boxes;
    QStringList m_classNames;

    QLabel *m_dirLabel = nullptr;
    QListWidget *m_imageList = nullptr;
    BoxAnnotationCanvas *m_canvas = nullptr;
    QListWidget *m_boxList = nullptr;
    QComboBox *m_classCombo = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_deleteBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    bool m_loading = false;
    int m_lastRow = -1;   /// 上一次成功载入的行（保存失败时回退到它）
};

#pragma once

#include <QDialog>
#include <QList>
#include <QPoint>
#include "RuntimeInterface.h"

class QListWidget;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QLabel;
class QScrollArea;
class RuntimeDesignerCanvas;

/// 自定义运行界面设计器（对齐 VisionMaster 4.4 运行界面设计）
class RuntimeInterfaceDesigner : public QDialog
{
    Q_OBJECT

public:
    explicit RuntimeInterfaceDesigner(
        const QStringList &nodeFullNames,
        QWidget *parent = nullptr);

    /// 设计结果（供应用/保存）
    const RuntimeInterface &resultInterface() const { return m_layout; }
    /// 默认布局文件路径（应用目录下）
    static QString defaultLayoutPath();

private slots:
    void addCurrentPaletteControl();
    void deleteSelected();
    void clearAll();
    void saveLayout();
    void loadLayout();
    void applyAndClose();
    void onControlSelected(int index);
    void onPropertyEdited();

private:
    void rebuildCanvas();
    void populateBindKeyCombo();
    void refreshPropertyPanel();
    RuntimeControl *selectedControl();

    RuntimeInterface m_layout;
    QStringList m_nodeNames;

    QListWidget *m_palette = nullptr;
    RuntimeDesignerCanvas *m_canvas = nullptr;
    QScrollArea *m_canvasScroll = nullptr;

    // 属性面板
    QLineEdit *m_titleEdit = nullptr;
    QLabel *m_typeLabel = nullptr;
    QComboBox *m_bindTypeCombo = nullptr;
    QComboBox *m_bindKeyCombo = nullptr;
    QLineEdit *m_colorEdit = nullptr;
    QSpinBox *m_fontSpin = nullptr;

    int m_selected = -1;
    bool m_updatingProps = false;
};

/// 设计画布：网格背景 + 可拖动/缩放的控件
class RuntimeDesignerCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit RuntimeDesignerCanvas(QWidget *parent = nullptr);
    void setInterface(RuntimeInterface *layout);
    void rebuild();
    void selectIndex(int index);
    int selectedIndex() const { return m_selected; }

signals:
    void controlSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

public:
    /// 刷新指定控件（属性修改后调用）
    void refreshControl(int index);

private:
    RuntimeInterface *m_layout = nullptr;
    QList<QWidget *> m_frames;
    int m_selected = -1;

    friend class DesignerControlFrame;
};

/// 设计器控件框（可拖动/右下角缩放）
class DesignerControlFrame : public QWidget
{
    Q_OBJECT

public:
    DesignerControlFrame(RuntimeDesignerCanvas *canvas, int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    RuntimeDesignerCanvas *m_canvas;
    int m_index;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_dragStartGlobal;
    QRect m_geoStart;
};

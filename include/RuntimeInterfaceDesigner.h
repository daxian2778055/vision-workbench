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
class QPushButton;
class QScrollArea;
class RuntimeDesignerCanvas;

/// 自定义运行界面设计器（对齐 VisionMaster 4.4 运行界面设计；多页 + 结果表格 + IO 状态）
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
    // 结果表格列交互
    void onColumnAdd();
    void onColumnDel();
    // 控件复制
    void duplicateSelected();
    // 撤销/重做（结构性操作）
    void undo();
    void redo();
    // 多选批量对齐
    void onAlignLeft();
    void onAlignTop();
    void onAlignHSpread();
    void onAlignVSpread();
    // 多页管理
    void addPage();
    void removePage();
    void duplicatePage();
    void movePageUp();
    void movePageDown();
    void renamePage();
    void onPageSelected(int index);

private:
    void rebuildCanvas();
    void populateBindKeyCombo();
    void refreshPropertyPanel();
    void refreshPageList();
    void pushUndo();
    void afterRestore();
    void movePage(int delta);
    RuntimeControl *selectedControl();

    RuntimeInterface m_layout;
    QStringList m_nodeNames;
    QList<RuntimeInterface> m_undoStack;   /// 结构性操作快照（值语义深拷贝）
    QList<RuntimeInterface> m_redoStack;

    QListWidget *m_pageList = nullptr;
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
    // 结果表格专用（列 = 下拉添加 / 列表删除）
    QListWidget *m_columnList = nullptr;
    QComboBox *m_columnSource = nullptr;
    QPushButton *m_columnAddBtn = nullptr;
    QPushButton *m_columnDelBtn = nullptr;
    QSpinBox *m_tableMaxRows = nullptr;

    int m_selected = -1;
    bool m_updatingProps = false;
    bool m_updatingPages = false;
};

/// 设计画布：网格背景 + 可拖动/缩放的控件（工作于当前页；支持 Ctrl+点击多选与批量对齐）
class RuntimeDesignerCanvas : public QWidget
{
    Q_OBJECT

public:
    enum AlignMode { AlignLeft, AlignTop, AlignHSpread, AlignVSpread };

    explicit RuntimeDesignerCanvas(QWidget *parent = nullptr);
    void setPage(RuntimeInterfacePage *page);
    void rebuild();
    void selectIndex(int index);
    void selectIndex(int index, bool additive);   /// additive=true 时切换多选
    int selectedIndex() const { return m_selected; }
    const QList<int> &selectedSet() const { return m_selectedSet; }
    void alignSelected(AlignMode mode);

signals:
    void controlSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

public:
    /// 刷新指定控件（属性修改后调用）
    void refreshControl(int index);

private:
    RuntimeInterfacePage *m_layout = nullptr;
    QList<QWidget *> m_frames;
    int m_selected = -1;          /// 最后选中（属性面板目标）
    QList<int> m_selectedSet;     /// 选中集合（多选）

    friend class DesignerControlFrame;
};

/// 设计器控件框（可拖动/右下角缩放/方向键微调）
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
    void keyPressEvent(QKeyEvent *event) override;

private:
    RuntimeDesignerCanvas *m_canvas;
    int m_index;
    bool m_dragging = false;
    bool m_resizing = false;
    QPoint m_dragStartGlobal;
    QRect m_geoStart;
};

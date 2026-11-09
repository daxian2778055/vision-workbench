#include "RuntimeInterfaceDesigner.h"
#include "RuntimeInterfaceView.h"
#include "GlobalVariableManager.h"
#include "FocusGuard.h"

#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFrame>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShortcut>
#include <QTimer>
#include <QTableWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QContextMenuEvent>
#include <QMenu>

namespace {
/// 控件库拖放用的 Qt 内建 item-model MIME（QListWidget 开启 drag 后自带）
const char *kItemModelMime = "application/x-qabstractitemmodeldatalist";
/// 剪贴板里放单个控件的序列化 JSON（官方 9.1 工具条注：复制粘贴仅 Ctrl+C / Ctrl+V）
const char *kControlMime = "application/x-vfp-runtime-control";

/// 控件库条目 → 控件类型（组标题的 UserRole 为空，返回 false）
bool paletteTypeOf(const QListWidgetItem *item, RuntimeControlType *out)
{
    if (!item || !out) return false;
    const QVariant v = item->data(Qt::UserRole);
    if (!v.isValid()) return false;
    const int t = v.toInt();
    if (t < 0 || t > int(RuntimeControlType::IoStatus)) return false;
    *out = RuntimeControlType(t);
    return true;
}
}   // namespace
#include <QShortcut>
#include <QColorDialog>
#include <QMessageBox>
#include <algorithm>
#include <climits>
#include <QFileDialog>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>

// ==================== RuntimeDesignerCanvas ====================

RuntimeDesignerCanvas::RuntimeDesignerCanvas(QWidget *parent)
: QWidget(parent)
{
setFixedSize(1200, 760);
setMouseTracking(true);
// 官方 9.2.2：控件库条目可按住左键拖入预览及编辑区后松开完成添加。
setAcceptDrops(true);
}

void RuntimeDesignerCanvas::dragEnterEvent(QDragEnterEvent *event)
{
if (event->mimeData() && event->mimeData()->hasFormat(QLatin1String(kItemModelMime)))
event->acceptProposedAction();
}

void RuntimeDesignerCanvas::dropEvent(QDropEvent *event)
{
if (!event->mimeData() || !event->mimeData()->hasFormat(QLatin1String(kItemModelMime)))
return;
event->acceptProposedAction();
// 行号由设计器从 m_palette->currentRow() 取（QListWidget 拖动时会把该项设为 current），
// 不去解析 Qt 内部 item-model MIME，避免依赖私有格式。
emit paletteDropped(event->position().toPoint());
}

void RuntimeDesignerCanvas::setPage(RuntimeInterfacePage *page)
{
    m_layout = page;
    rebuild();
}

void RuntimeDesignerCanvas::setEditInnerTab(int idx)
{
    if (m_layout)
        idx = qBound(-1, idx, m_layout->innerTabNames.size() - 1);
    else
        idx = -1;
    if (idx == m_editInnerTab) return;
    m_editInnerTab = idx;
    rebuild();
}

void RuntimeDesignerCanvas::setCanvasSize(const QSize &size)
{
    if (!size.isValid() || size.isEmpty()) return;
    setFixedSize(size);
    rebuild();
}

QRect RuntimeDesignerCanvas::toCanvasRect(const RuntimeControl &ctrl) const
{
    if (!m_layout) return ctrl.geometry;
    const int topH = qMax(0, m_layout->topBarHeight);
    const int botH = qMax(0, m_layout->bottomBarHeight);
    const int leftW = qMax(0, m_layout->leftBarWidth);
    const int rightW = qMax(0, m_layout->rightBarWidth);
    QPoint off;
    switch (ctrl.zone) {
    case RuntimeZone::TopBar:    off = QPoint(0, 0); break;
    case RuntimeZone::BottomBar: off = QPoint(0, height() - botH); break;
    case RuntimeZone::LeftBar:   off = QPoint(0, topH); break;
    case RuntimeZone::RightBar:  off = QPoint(width() - rightW, topH); break;
    case RuntimeZone::Canvas:    off = QPoint(leftW, topH); break;
    }
    // 页内二级 Tab：标签条占画布区顶部一条，画布控件（无论是否归属 Tab）
    // 整体下移一条标签高度，与运行态 controlGeometry 口径一致
    if (ctrl.zone == RuntimeZone::Canvas && !m_layout->innerTabNames.isEmpty())
        off.ry() += m_layout->innerTabHeight;
    return ctrl.geometry.translated(off);
}

QRect RuntimeDesignerCanvas::toStoredRect(const RuntimeControl &ctrl,
                                          const QRect &canvasRect) const
{
    if (!m_layout) return canvasRect;
    const int topH = qMax(0, m_layout->topBarHeight);
    const int botH = qMax(0, m_layout->bottomBarHeight);
    const int leftW = qMax(0, m_layout->leftBarWidth);
    const int rightW = qMax(0, m_layout->rightBarWidth);
    QPoint off;
    switch (ctrl.zone) {
    case RuntimeZone::TopBar:    off = QPoint(0, 0); break;
    case RuntimeZone::BottomBar: off = QPoint(0, height() - botH); break;
    case RuntimeZone::LeftBar:   off = QPoint(0, topH); break;
    case RuntimeZone::RightBar:  off = QPoint(width() - rightW, topH); break;
    case RuntimeZone::Canvas:    off = QPoint(leftW, topH); break;
    }
    // 与 toCanvasRect 对称：画布控件写回前先减掉标签条高度
    if (ctrl.zone == RuntimeZone::Canvas && !m_layout->innerTabNames.isEmpty())
        off.ry() += m_layout->innerTabHeight;
    QRect g = canvasRect.translated(-off);
    g.moveLeft(qMax(0, g.left()));
    g.moveTop(qMax(0, g.top()));
    return g;
}

void RuntimeInterfaceDesigner::rebuildCanvas()
{
    if (!m_canvas) return;
    // 画布存的是 RuntimeInterfacePage*（指向 m_layout.pages 内部）。m_layout 被复制进
    // 撤销栈后，Qt 容器的写时复制会在下一次非 const 访问时把页数组换块——画布里的旧指针
    // 就落到撤销栈那份**快照**上：重建出来是改前的页，拖动/方向键的几何写回也写进快照，
    // 撤销点因此永远建不出来。所以结构性改动之后必须重新绑一次当前页再重建。
    m_canvas->setPage(m_layout.currentPage());
}

void RuntimeInterfaceDesigner::setCanvasPage()
{
    if (!m_canvas) return;
    // 先按画布配置定尺寸，再挂页（setPage 内部会 rebuild）
    m_canvas->setCanvasSize(QSize(m_layout.canvas.width, m_layout.canvas.height));
    m_canvas->setPage(m_layout.currentPage());
    refreshEditInnerTabCombo();
    refreshCanvasConfigControls();
}

void RuntimeInterfaceDesigner::refreshCanvasConfigControls()
{
    m_updatingCanvasConfig = true;
    if (m_canvasWidthSpin) {
        QSignalBlocker block(m_canvasWidthSpin);
        m_canvasWidthSpin->setValue(qBound(m_canvasWidthSpin->minimum(),
                                           m_layout.canvas.width,
                                           m_canvasWidthSpin->maximum()));
    }
    if (m_canvasHeightSpin) {
        QSignalBlocker block(m_canvasHeightSpin);
        m_canvasHeightSpin->setValue(qBound(m_canvasHeightSpin->minimum(),
                                            m_layout.canvas.height,
                                            m_canvasHeightSpin->maximum()));
    }
    // 固定栏厚度是页级字段：没有当前页时显示 0
    RuntimeInterfacePage *pg = m_layout.currentPage();
    const int top = pg ? pg->topBarHeight : 0;
    const int bottom = pg ? pg->bottomBarHeight : 0;
    const int left = pg ? pg->leftBarWidth : 0;
    const int right = pg ? pg->rightBarWidth : 0;
    struct SpinSeed { QSpinBox *spin; int value; };
    const SpinSeed seeds[] = {{m_barTopSpin, top}, {m_barBottomSpin, bottom},
                              {m_barLeftSpin, left}, {m_barRightSpin, right}};
    for (const SpinSeed &s : seeds) {
        if (!s.spin) continue;
        QSignalBlocker block(s.spin);
        s.spin->setValue(qBound(s.spin->minimum(), s.value, s.spin->maximum()));
    }
    m_updatingCanvasConfig = false;
}

void RuntimeInterfaceDesigner::applyCanvasSize()
{
    if (!m_canvasWidthSpin || !m_canvasHeightSpin) return;
    const int newW = m_canvasWidthSpin->value();
    const int newH = m_canvasHeightSpin->value();
    const QSize oldSize(m_layout.canvas.width, m_layout.canvas.height);
    // 非法或没变化：不建撤销点、不重建画布
    if (newW <= 0 || newH <= 0) return;
    if (newW == oldSize.width() && newH == oldSize.height()) return;

    pushUndo();
    if (m_canvasScaleCheck && m_canvasScaleCheck->isChecked()) {
        if (RuntimeInterfacePage *pg = m_layout.currentPage())
            scaleCanvasControls(*pg, oldSize, QSize(newW, newH));
    }
    m_layout.canvas.width = newW;
    m_layout.canvas.height = newH;
    setCanvasPage();
}

void RuntimeInterfaceDesigner::onBarThicknessChanged()
{
    if (m_updatingCanvasConfig) return;
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg) return;
    const int top = m_barTopSpin ? m_barTopSpin->value() : pg->topBarHeight;
    const int bottom = m_barBottomSpin ? m_barBottomSpin->value() : pg->bottomBarHeight;
    const int left = m_barLeftSpin ? m_barLeftSpin->value() : pg->leftBarWidth;
    const int right = m_barRightSpin ? m_barRightSpin->value() : pg->rightBarWidth;
    // 值没变（或只是被 refresh 播种）不建撤销点
    if (top == pg->topBarHeight && bottom == pg->bottomBarHeight
        && left == pg->leftBarWidth && right == pg->rightBarWidth) {
        return;
    }
    pushUndo();
    pg->topBarHeight = top;
    pg->bottomBarHeight = bottom;
    pg->leftBarWidth = left;
    pg->rightBarWidth = right;
    setCanvasPage();
}

void RuntimeInterfaceDesigner::refreshEditInnerTabCombo()
{
    if (!m_editInnerTabCombo || !m_canvas) return;
    RuntimeInterfacePage *pg = m_layout.currentPage();
    {
        QSignalBlocker block(m_editInnerTabCombo);   // 重建候选不触发写回
        m_editInnerTabCombo->clear();
        m_editInnerTabCombo->addItem(QStringLiteral("全部画布"), -1);
        if (pg) {
            for (int i = 0; i < pg->innerTabNames.size(); ++i)
                m_editInnerTabCombo->addItem(pg->innerTabNames.at(i), i);
        }
        m_editInnerTabCombo->setEnabled(pg && !pg->innerTabNames.isEmpty());
        // 回选画布当前的编辑目标；不在候选里（切页/关 Tab）则退回「全部画布」
        const int cur = m_editInnerTabCombo->findData(m_canvas->editInnerTab());
        m_editInnerTabCombo->setCurrentIndex(cur >= 0 ? cur : 0);
    }
    m_canvas->setEditInnerTab(m_editInnerTabCombo->currentData().toInt());
}

void RuntimeDesignerCanvas::rebuild()
{
    for (QWidget *w : m_frames) {
        w->setParent(nullptr);
        w->deleteLater();
    }
    m_frames.clear();
    m_selected = -1;
    m_selectedSet.clear();

    if (!m_layout) return;
    for (int i = 0; i < m_layout->controls.size(); ++i) {
        auto *frame = new DesignerControlFrame(this, i);
        frame->setGeometry(toCanvasRect(m_layout->controls[i]));
        // 编辑聚焦：只看选中内层 Tab 的控件 + 不归属任何 Tab 的画布控件
        const RuntimeControl &c = m_layout->controls[i];
        const bool show = (m_editInnerTab < 0) || (c.innerTab < 0) || (c.innerTab == m_editInnerTab);
        frame->setVisible(show);
        m_frames.append(frame);
    }
    update();
}

void RuntimeDesignerCanvas::selectIndex(int index)
{
    selectIndex(index, false);
}

void RuntimeDesignerCanvas::selectIndex(int index, bool additive)
{
    if (additive) {
        if (m_selectedSet.contains(index))
            m_selectedSet.removeOne(index);
        else
            m_selectedSet.append(index);
    } else {
        m_selectedSet.clear();
        if (index >= 0)
            m_selectedSet.append(index);
    }
    m_selected = m_selectedSet.isEmpty() ? -1 : m_selectedSet.last();
    for (int i = 0; i < m_frames.size(); ++i) {
        m_frames[i]->update();
    }
    emit controlSelected(m_selected);
}

void RuntimeDesignerCanvas::alignSelected(AlignMode mode)
{
    if (!m_layout) return;
    if (m_selectedSet.size() < 2) return;   // 至少两个控件才有对齐意义

    // 收集选中几何（frames 与 controls 索引一一对应）
    QList<int> idx = m_selectedSet;
    QVector<QRect> rects;
    for (int i : idx) {
        if (i < 0 || i >= m_frames.size()) return;
        rects.append(m_frames[i]->geometry());
    }

    // 官方 9.1：以选中控件组成的**区域**为基准做对齐
    QRect bound = rects.first();
    for (const QRect &r : rects) bound |= r;

    if (mode != AlignHSpread && mode != AlignVSpread) {
        for (int k = 0; k < idx.size(); ++k) {
            QRect g = rects[k];
            switch (mode) {
            case AlignLeft:    g.moveLeft(bound.left()); break;
            case AlignHCenter: g.moveLeft(bound.center().x() - g.width() / 2); break;
            case AlignRight:   g.moveRight(bound.right()); break;
            case AlignTop:     g.moveTop(bound.top()); break;
            case AlignVCenter: g.moveTop(bound.center().y() - g.height() / 2); break;
            case AlignBottom:  g.moveBottom(bound.bottom()); break;
            default: return;
            }
            m_frames[idx[k]]->setGeometry(g);
            m_layout->controls[idx[k]].geometry = toStoredRect(m_layout->controls[idx[k]], g);
            m_frames[idx[k]]->update();
        }
        return;
    }

    // 分布：官方要求选中 3 个以上才起作用；按坐标排序，首尾固定，中间等距
    if (idx.size() < 3) return;
    QList<QPair<int, int>> order;   // (排序键, 选中序号)
    for (int k = 0; k < idx.size(); ++k)
        order.append({ mode == AlignHSpread ? rects[k].left() : rects[k].top(), k });
    std::sort(order.begin(), order.end());
    const int n = order.size();
    const int firstKey = order.first().first;
    const int lastKey = order.last().first;
    if (lastKey == firstKey) return;   // 无可分配间隙
    const double step = double(lastKey - firstKey) / (n - 1);
    for (int rank = 1; rank < n - 1; ++rank) {
        const int k = order[rank].second;
        const int want = qRound(firstKey + step * rank);
        QRect g = rects[k];
        if (mode == AlignHSpread) g.moveLeft(want); else g.moveTop(want);
        m_frames[idx[k]]->setGeometry(g);
        m_layout->controls[idx[k]].geometry = g;
        m_frames[idx[k]]->update();
    }
}

void RuntimeDesignerCanvas::refreshControl(int index)
{
    if (!m_layout) return;
    if (index < 0 || index >= m_frames.size()) return;
    m_frames[index]->setGeometry(toCanvasRect(m_layout->controls[index]));
    m_frames[index]->update();
}

void RuntimeDesignerCanvas::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x20));

    const RuntimeInterfacePage *pg = m_layout;
    const int topH = pg ? qMax(0, pg->topBarHeight) : 0;
    const int botH = pg ? qMax(0, pg->bottomBarHeight) : 0;
    const int leftW = pg ? qMax(0, pg->leftBarWidth) : 0;
    const int rightW = pg ? qMax(0, pg->rightBarWidth) : 0;
    const QRect canvasRect(leftW, topH, qMax(1, width() - leftW - rightW),
                           qMax(1, height() - topH - botH));

    // 网格只画在画布区：固定栏不是自由摆放区，视觉上要和画布分开
    p.save();
    p.setClipRect(canvasRect);
    p.setPen(QPen(QColor(0x28, 0x28, 0x32), 1));
    const int grid = 20;
    for (int x = canvasRect.left(); x <= canvasRect.right(); x += grid)
        p.drawLine(x, canvasRect.top(), x, canvasRect.bottom());
    for (int y = canvasRect.top(); y <= canvasRect.bottom(); y += grid)
        p.drawLine(canvasRect.left(), y, canvasRect.right(), y);
    p.restore();

    // 固定栏：底板 + 边框 + 分割格 + 标签（官方 9.1「顶部/底部/左侧/右侧固定栏」）
    // 注意：形参不能叫 slots——它是 Qt 的关键字宏（展开为空），会导致 qMax(1, ) 语法错误
    auto drawBar = [&p](const QRect &r, int slotCount, bool verticalSplit, const QString &name) {
        if (r.width() <= 0 || r.height() <= 0) return;
        p.fillRect(r, QColor(0x24, 0x24, 0x2e));
        p.setPen(QPen(QColor(0x44, 0x46, 0x58), 1, Qt::DashLine));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.setPen(QPen(QColor(0x44, 0x46, 0x58), 1, Qt::DotLine));
        const int n = qMax(1, slotCount);
        for (int i = 1; i < n; ++i) {
            if (verticalSplit) {
                const int x = r.left() + r.width() * i / n;
                p.drawLine(x, r.top(), x, r.bottom());
            } else {
                const int y = r.top() + r.height() * i / n;
                p.drawLine(r.left(), y, r.right(), y);
            }
        }
        QFont f = p.font();
        f.setPointSizeF(8.0);
        p.setFont(f);
        p.setPen(QColor(0x8a, 0x8e, 0x9c));
        p.drawText(r.adjusted(4, 1, -4, -1), Qt::AlignLeft | Qt::AlignTop,
                   n > 1 ? QStringLiteral("%1 ×%2").arg(name).arg(n) : name);
    };
    drawBar(QRect(0, 0, width(), topH), pg ? pg->topBarSlots : 1, true,
            QStringLiteral("顶部固定栏"));
    drawBar(QRect(0, height() - botH, width(), botH), pg ? pg->bottomBarSlots : 1, true,
            QStringLiteral("底部固定栏"));
    drawBar(QRect(0, topH, leftW, qMax(0, height() - topH - botH)),
            pg ? pg->leftBarSlots : 1, false, QStringLiteral("左侧固定栏"));
    drawBar(QRect(width() - rightW, topH, rightW, qMax(0, height() - topH - botH)),
            pg ? pg->rightBarSlots : 1, false, QStringLiteral("右侧固定栏"));

    // 画布区边框（明确「随窗口缩放的部分」边界）
    p.setPen(QPen(QColor(0x3a, 0x6e, 0xa5), 1));
    p.drawRect(canvasRect.adjusted(0, 0, -1, -1));

    // 页面提示
    QFont hf = p.font();
    hf.setPointSizeF(8.0);
    p.setFont(hf);
    p.setPen(QColor(0x60, 0x64, 0x72));
    p.drawText(canvasRect.adjusted(6, 0, -6, -6), Qt::AlignLeft | Qt::AlignBottom,
               QStringLiteral("画布 %1×%2 · 拖动移动，右下角缩放")
                   .arg(width()).arg(height()));

    // 页内二级 Tab 标签条（官方 9.3「Tab页」）
    if (pg && !pg->innerTabNames.isEmpty()) {
        const int stripH = qMin(pg->innerTabHeight, canvasRect.height());
        if (stripH > 0) {
            const QRect strip(canvasRect.left(), canvasRect.top(), canvasRect.width(), stripH);
            p.fillRect(strip, QColor(0x1c, 0x1c, 0x24));
            const int n = pg->innerTabNames.size();
            const int tabW = qMax(1, strip.width() / qMax(1, n));
            for (int i = 0; i < n; ++i) {
                const QRect r(strip.left() + i * tabW, strip.top(), tabW, stripH);
                const bool active = (i == pg->defaultInnerTab);
                if (active) p.fillRect(r, QColor(0x2e, 0x2e, 0x3c));
                p.setPen(QColor(0x44, 0x46, 0x58));
                p.drawRect(r.adjusted(0, 0, -1, -1));
                p.setPen(active ? QColor(0xd8, 0xd8, 0xe0) : QColor(0x8a, 0x8e, 0x9c));
                p.drawText(r, Qt::AlignCenter, pg->innerTabNames.at(i));
            }
            p.setPen(QColor(0x4a, 0x4c, 0x5a));
            p.drawText(QRect(canvasRect.left(), strip.bottom() + 2, canvasRect.width(), 16),
                       Qt::AlignHCenter | Qt::AlignTop,
                       QStringLiteral("↓ Tab 内容区：「内层Tab」设了标签的控件显示在这里"));
        }
    }
}

// ==================== DesignerControlFrame ====================

DesignerControlFrame::DesignerControlFrame(RuntimeDesignerCanvas *canvas, int index)
    : QWidget(canvas), m_canvas(canvas), m_index(index)
{
    setMouseTracking(true);
    setCursor(Qt::SizeAllCursor);
    setFocusPolicy(Qt::ClickFocus);   // 方向键微调需要焦点
}

void DesignerControlFrame::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    if (!m_canvas || !m_canvas->m_layout) return;
    if (m_index < 0 || m_index >= m_canvas->m_layout->controls.size()) return;
    const RuntimeControl &ctrl = m_canvas->m_layout->controls[m_index];

    const bool selected = (m_canvas->m_selected == m_index);

    // 背景
    p.fillRect(rect(), QColor(0x26, 0x28, 0x30));

    // 边框
    QColor border = selected ? QColor(0x40, 0xa9, 0xff) : QColor(0x4a, 0x4e, 0x5a);
    p.setPen(QPen(border, selected ? 2 : 1));
    p.drawRect(rect().adjusted(0, 0, -1, -1));

    // 标题
    p.setPen(QColor(0x9a, 0x9e, 0xac));
    QFont titleFont = p.font();
    titleFont.setPointSizeF(9);
    p.setFont(titleFont);
    p.drawText(QRect(6, 4, width() - 12, 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 · %2").arg(runtimeControlTypeName(ctrl.type), ctrl.displayTitle()));

    // 类型预览
    QRect body(8, 24, width() - 16, height() - 30);
    if (body.width() <= 0 || body.height() <= 0) return;

    switch (ctrl.type) {
    case RuntimeControlType::ImageView: {
        p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
        p.setBrush(QColor(0x11, 0x11, 0x14));
        p.drawRect(body);
        p.setPen(QColor(0x66, 0x6a, 0x78));
        QFont imgFont = p.font();
        imgFont.setPointSizeF(10);
        p.setFont(imgFont);
        p.drawText(body, Qt::AlignCenter, QStringLiteral("图像\n%1").arg(ctrl.bindKey));
        break;
    }
    case RuntimeControlType::ValueDisplay:
    case RuntimeControlType::TextLabel: {
        p.setPen(QColor(ctrl.color));
        QFont valFont = p.font();
        valFont.setPointSizeF(qMax(8, ctrl.fontSize / 2));
        valFont.setBold(true);
        p.setFont(valFont);
        p.drawText(body, Qt::AlignCenter,
                   ctrl.type == RuntimeControlType::TextLabel
                       ? ctrl.displayTitle()
                       : QStringLiteral("123.456"));
        break;
    }
    case RuntimeControlType::StatusLight: {
        const int r = qMin(16, body.height() / 2 - 4);
        QPoint c(body.left() + r + 4, body.center().y());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(ctrl.color));
        p.drawEllipse(c, r, r);
        p.setPen(QColor(0x9a, 0x9e, 0xac));
        QFont lightFont = p.font();
        lightFont.setPointSizeF(10);
        p.setFont(lightFont);
        p.drawText(QRect(c.x() + r + 6, body.top(), body.right() - c.x() - r - 6, body.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, ctrl.displayTitle());
        break;
    }
    case RuntimeControlType::Button: {
        QRect btnRect = body.adjusted(4, 4, -4, -4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(ctrl.color));
        p.drawRoundedRect(btnRect, 4, 4);
        p.setPen(Qt::white);
        QFont btnFont = p.font();
        btnFont.setPointSizeF(10);
        btnFont.setBold(true);
        p.setFont(btnFont);
        p.drawText(btnRect, Qt::AlignCenter, ctrl.displayTitle());
        break;
    }
    case RuntimeControlType::MultiLineChart:   // 多数据线图与线图阵列共用预览
    case RuntimeControlType::LineChart: {
        // 线图阵列预览：每个数据源一张小图（官方：执行次数 vs 当前值）
        p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
        p.setBrush(QColor(ctrl.chartBgColor.isEmpty() ? QStringLiteral("#1e1e24")
                                                      : ctrl.chartBgColor));
        p.drawRect(body);
        p.setBrush(Qt::NoBrush);
        if (ctrl.showChartTitle) {
            p.setPen(QColor(0xd8, 0xd8, 0xe0));
            p.drawText(QRect(body.left() + 6, body.top() + 3, body.width() - 12, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       ctrl.chartTitle.isEmpty() ? ctrl.displayTitle() : ctrl.chartTitle);
        }
        const int n = qMax(1, ctrl.chartSeries.size());
        const int gap = 6;
        const int plotTop = body.top() + (ctrl.showChartTitle ? 24 : 6);
        const int plotH = qMax(12, (body.bottom() - plotTop - gap * (n - 1)) / n);
        for (int i = 0; i < n; ++i) {
            const QRect plot(body.left() + 8, plotTop + i * (plotH + gap),
                             qMax(20, body.width() - 16), plotH);
            p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
            p.drawRect(plot);
            const QString name = ctrl.chartSeries.isEmpty()
                                     ? QStringLiteral("未配置数据系列")
                                     : ctrl.chartSeries.at(i).name;
            p.setPen(QColor(0x8a, 0x8e, 0x9c));
            p.drawText(plot.adjusted(4, 1, -4, -1), Qt::AlignLeft | Qt::AlignTop, name);
            QColor line = ctrl.chartSeries.isEmpty() ? QColor(QStringLiteral("#4FC3F7"))
                                                     : QColor(ctrl.chartSeries.at(i).color);
            if (!line.isValid()) line = QColor(QStringLiteral("#4FC3F7"));
            p.setPen(QPen(line, 2));
            QPolygonF poly;
            for (int k = 0; k <= 10; ++k) {
                const double x = plot.left() + 4 + (plot.width() - 8) * k / 10.0;
                const double y = plot.center().y()
                                 + (((k * 7 + i * 11) % 17) - 8) * plot.height() / 40.0;
                poly << QPointF(x, y);
            }
            p.drawPolyline(poly);
        }
        break;
    }
    case RuntimeControlType::PieChart: {
        // 饼图预览：按系列数均分圆（真实占比在运行态按当前值计算）
        p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
        p.setBrush(QColor(ctrl.chartBgColor.isEmpty() ? QStringLiteral("#1e1e24")
                                                      : ctrl.chartBgColor));
        p.drawRect(body);
        p.setBrush(Qt::NoBrush);
        if (ctrl.showChartTitle) {
            p.setPen(QColor(0xd8, 0xd8, 0xe0));
            p.drawText(QRect(body.left() + 6, body.top() + 3, body.width() - 12, 18),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       ctrl.chartTitle.isEmpty() ? ctrl.displayTitle() : ctrl.chartTitle);
        }
        const QRect pieArea = body.adjusted(8, ctrl.showChartTitle ? 24 : 6, -8, -8);
        const int d = qMax(12, qMin(pieArea.width(), pieArea.height()));
        const QRect pieRect(pieArea.center().x() - d / 2, pieArea.center().y() - d / 2, d, d);
        const int n = ctrl.chartSeries.size();
        if (n <= 0) {
            p.setPen(QPen(QColor(0x4a, 0x4e, 0x5a), 2));
            p.drawEllipse(pieRect);
            p.setPen(QColor(0x8a, 0x8e, 0x9c));
            p.drawText(pieRect, Qt::AlignCenter, QStringLiteral("未配置数据系列"));
        } else {
            p.setPen(QPen(QColor(0x1e, 0x1e, 0x24), 1));
            int start = 90 * 16;   // drawPie 角度单位 = 1/16 度
            for (int i = 0; i < n; ++i) {
                QColor c(ctrl.chartSeries.at(i).color);
                if (!c.isValid()) c = QColor(QStringLiteral("#4FC3F7"));
                p.setBrush(c);
                // 末块吃掉整除余量，保证拼满 360 度无缝隙
                const int span = (i == n - 1) ? (90 * 16 + 360 * 16 - start) : (360 * 16 / n);
                p.drawPie(pieRect, start, span);
                start += span;
            }
        }
        break;
    }
    case RuntimeControlType::ResultTable: {
        // 表格预览：表头 + 两行网格
        p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
        p.setBrush(QColor(0x11, 0x11, 0x14));
        p.drawRect(body);
        const int cols = qMax(1, ctrl.columns.size());
        const int cw = body.width() / cols;
        const int hh = 20;
        // 表头
        p.setBrush(QColor(0x2a, 0x2f, 0x40));
        p.drawRect(QRect(body.left(), body.top(), body.width(), hh));
        p.setPen(QColor(0x8a, 0x9a, 0xc0));
        for (int i = 0; i < cols && i < 8; ++i) {
            const QString h = ctrl.columns[i].header.isEmpty()
                                  ? QStringLiteral("列%1").arg(i + 1) : ctrl.columns[i].header;
            p.drawText(QRect(body.left() + i * cw, body.top(), cw, hh),
                       Qt::AlignCenter, h);
            p.setPen(QPen(QColor(0x3a, 0x3a, 0x4a), 1));
            p.drawLine(body.left() + (i + 1) * cw, body.top(),
                       body.left() + (i + 1) * cw, body.bottom());
            p.setPen(QColor(0x8a, 0x9a, 0xc0));
        }
        // 行线
        for (int r = 1; r <= 3; ++r) {
            const int y = body.top() + hh * r;
            if (y > body.bottom()) break;
            p.setPen(QPen(QColor(0x2e, 0x2e, 0x3a), 1));
            p.drawLine(body.left(), y, body.right(), y);
        }
        if (ctrl.columns.isEmpty()) {
            p.setPen(QColor(0x66, 0x6a, 0x78));
            p.drawText(body, Qt::AlignCenter, QStringLiteral("未配置列\n（属性面板填写列绑定）"));
        }
        break;
    }
    case RuntimeControlType::IoStatus: {
        const int r = qMin(16, body.height() / 2 - 4);
        QPoint c(body.left() + r + 4, body.center().y());
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x55, 0x58, 0x62));
        p.drawEllipse(c, r, r);
        p.setPen(QColor(0x9a, 0x9e, 0xac));
        QFont ioFont = p.font();
        ioFont.setPointSizeF(10);
        p.setFont(ioFont);
        p.drawText(QRect(c.x() + r + 6, body.top(), body.right() - c.x() - r - 6, body.height()),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("%1\n%2").arg(ctrl.displayTitle(), ctrl.bindKey));
        break;
    }
    }

    // 缩放指示（右下角）
    if (selected) {
        p.setPen(QColor(0x40, 0xa9, 0xff));
        const int g = 6;
        p.drawLine(width() - g, height() - 1, width() - 1, height() - g);
    }

    // 运行时隐藏的控件：设计画布仍保留（可选中/编辑/恢复），用虚线框 + 角标明确提示，
    // 避免"设计器看得见、运行态不显示"被误判为丢失控件。
    if (!ctrl.visible) {
        if (!selected) {
            p.setPen(QPen(QColor(0x8a, 0x8a, 0x8a), 1, Qt::DashLine));
            p.drawRect(rect().adjusted(0, 0, -1, -1));
        }
        p.setPen(QColor(0xd0, 0x8a, 0x3a));
        p.drawText(QRect(6, 4, width() - 12, 18), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("运行时隐藏"));
    }
}

void DesignerControlFrame::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_canvas->selectIndex(m_index, event->modifiers() & Qt::ControlModifier);
        // 改前快照必须在几何被改动之前记下（点选也算手势开始，结束时比对无变化则不建撤销点）
        emit m_canvas->geometryEditStarted();
        const QPoint local = event->pos();
        // 右下角 14px 区域 = 缩放
        if (local.x() >= width() - 14 && local.y() >= height() - 14) {
            m_resizing = true;
            m_geoStart = geometry();
        } else {
            m_dragging = true;
            m_geoStart = geometry();
        }
        m_dragStartGlobal = event->globalPosition().toPoint();
        event->accept();
    }
}

void DesignerControlFrame::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging && !m_resizing) {
        const QPoint local = event->pos();
        setCursor((local.x() >= width() - 14 && local.y() >= height() - 14)
                      ? Qt::SizeFDiagCursor : Qt::SizeAllCursor);
        return;
    }

    const QPoint delta = event->globalPosition().toPoint() - m_dragStartGlobal;
    if (m_resizing) {
        QRect g = m_geoStart;
        g.setWidth(qMax(80, m_geoStart.width() + delta.x()));
        g.setHeight(qMax(40, m_geoStart.height() + delta.y()));
        setGeometry(g);
    } else {
        QRect g = m_geoStart.translated(delta);
        // 10px 网格吸附（对齐画布网格，拖完自动落格）
        g.moveLeft(qMax(0, (g.left() + 5) / 10 * 10));
        g.moveTop(qMax(0, (g.top() + 5) / 10 * 10));
        setGeometry(g);

        // 多选整体平移：随主控件同步移动其余选中控件
        const QList<int> sel = m_canvas->m_selectedSet;
        if (sel.size() > 1 && sel.contains(m_index)) {
            for (int other : sel) {
                if (other == m_index) continue;
                if (other < 0 || other >= m_canvas->m_frames.size()) continue;
                QWidget *f = m_canvas->m_frames[other];
                QRect og = f->geometry().translated(delta);
                og.moveLeft(qMax(0, (og.left() + 5) / 10 * 10));
                og.moveTop(qMax(0, (og.top() + 5) / 10 * 10));
                f->setGeometry(og);
                f->update();
            }
        }
    }

    if (m_canvas->m_layout) {
        for (int i : m_canvas->m_selectedSet) {
            if (i >= 0 && i < m_canvas->m_frames.size() && i < m_canvas->m_layout->controls.size())
                m_canvas->m_layout->controls[i].geometry = m_canvas->toStoredRect(
                    m_canvas->m_layout->controls[i], m_canvas->m_frames[i]->geometry());
        }
    }
    event->accept();
}

void DesignerControlFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // 只有真的拖过/缩放过才结束一次手势（否则点了空白处也会触发一次收尾）
        const bool wasEditing = m_dragging || m_resizing;
        m_dragging = false;
        m_resizing = false;
        if (wasEditing)
            emit m_canvas->geometryEditFinished();
        event->accept();
    }
}

void DesignerControlFrame::contextMenuEvent(QContextMenuEvent *event)
{
    // 官方 9.1 工具条注：删除 / 撤销 / 重做 / 图层位置支持鼠标右键。
    // 先把被点的控件设为当前选中，否则右键菜单会作用到上一个选中的控件上。
    m_canvas->selectIndex(m_index);
    // friend 关系允许访问受保护的 signal
    emit m_canvas->controlContextMenu(m_index, event->globalPos());
    event->accept();
}

void DesignerControlFrame::keyPressEvent(QKeyEvent *event)
{
    // 方向键微调 1px，Shift+方向键 10px
    const int step = (event->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    int dx = 0, dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:  dx = -step; break;
    case Qt::Key_Right: dx =  step; break;
    case Qt::Key_Up:    dy = -step; break;
    case Qt::Key_Down:  dy =  step; break;
    default: QWidget::keyPressEvent(event); return;
    }
    // 自动重复（按住方向键）不算新手势：只有首次按键建一枚撤销点
    const bool record = !event->isAutoRepeat();
    if (record) emit m_canvas->geometryEditStarted();
    QRect g = geometry().translated(dx, dy);
    g.moveLeft(qMax(0, g.left()));
    g.moveTop(qMax(0, g.top()));
    setGeometry(g);
    if (m_canvas->m_layout && m_index >= 0 && m_index < m_canvas->m_layout->controls.size())
        m_canvas->m_layout->controls[m_index].geometry =
            m_canvas->toStoredRect(m_canvas->m_layout->controls[m_index], g);
    if (record) emit m_canvas->geometryEditFinished();
    event->accept();
}

// ==================== RuntimeInterfaceDesigner ====================

QString RuntimeInterfaceDesigner::defaultLayoutPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/runtime_interface.json");
}

RuntimeInterfaceDesigner::RuntimeInterfaceDesigner(const QStringList &nodeFullNames, QWidget *parent)
    : QDialog(parent)
    , m_nodeNames(nodeFullNames)
{
    setWindowTitle(QStringLiteral("运行界面设计"));
    resize(1400, 860);

    // 初始布局：从默认路径加载（若存在）
    m_layout.loadFromFile(defaultLayoutPath());
    m_layout.ensurePage();

    // 属性编辑撤销合并：600ms 内的连续输入只压一次快照
    m_propUndoTimer = new QTimer(this);
    m_propUndoTimer->setSingleShot(true);
    m_propUndoTimer->setInterval(600);

    // ---- 左上：页面管理 ----
    auto *pageGroup = new QGroupBox(QStringLiteral("页面"), this);
    m_pageList = new QListWidget(pageGroup);
    m_pageList->setMaximumHeight(110);
    auto *pageBtnRow = new QHBoxLayout();
    auto *addPageBtn = new QPushButton(QStringLiteral("＋页"), pageGroup);
    auto *dupPageBtn = new QPushButton(QStringLiteral("复制"), pageGroup);
    auto *delPageBtn = new QPushButton(QStringLiteral("－页"), pageGroup);
    auto *renPageBtn = new QPushButton(QStringLiteral("改名"), pageGroup);
    auto *upPageBtn = new QPushButton(QStringLiteral("↑"), pageGroup);
    auto *downPageBtn = new QPushButton(QStringLiteral("↓"), pageGroup);
    addPageBtn->setToolTip(QStringLiteral("添加运行界面页"));
    dupPageBtn->setToolTip(QStringLiteral("复制当前页（含全部控件）"));
    delPageBtn->setToolTip(QStringLiteral("删除当前页（至少保留一页）"));
    renPageBtn->setToolTip(QStringLiteral("重命名当前页"));
    upPageBtn->setToolTip(QStringLiteral("当前页上移"));
    downPageBtn->setToolTip(QStringLiteral("当前页下移"));
    pageBtnRow->addWidget(addPageBtn);
    pageBtnRow->addWidget(dupPageBtn);
    pageBtnRow->addWidget(delPageBtn);
    pageBtnRow->addWidget(renPageBtn);
    pageBtnRow->addWidget(upPageBtn);
    pageBtnRow->addWidget(downPageBtn);
    auto *pageLay = new QVBoxLayout(pageGroup);
    pageLay->addWidget(m_pageList, 1);
    pageLay->addLayout(pageBtnRow);
    connect(addPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::addPage);
    connect(dupPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::duplicatePage);
    connect(delPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::removePage);
    connect(renPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::renamePage);
    connect(upPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::movePageUp);
    connect(downPageBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::movePageDown);
    connect(m_pageList, &QListWidget::currentRowChanged, this, &RuntimeInterfaceDesigner::onPageSelected);
    refreshPageList();

    // ---- 左下：控件面板 ----
    auto *paletteGroup = new QGroupBox(QStringLiteral("控件库"), this);
    m_palette = new QListWidget(paletteGroup);
    // 官方 9.3 控件区按「基础 / 布局 / 图表 / 其它」分组，组标题右侧可折叠。
    // 这里只列**已实现**的控件（不放假条目），未实现的等 B3 逐种补齐；
    // 类型不再靠行号推断——行号里有不可选的组标题，改用 Qt::UserRole 存枚举。
    {
        struct PaletteItem { const char *group; const char *label; RuntimeControlType type; };
        const PaletteItem items[] = {
            { "基础", "图像",     RuntimeControlType::ImageView },
            { "基础", "字符设置", RuntimeControlType::ValueDisplay },
            { "基础", "文本框",   RuntimeControlType::TextLabel },
            { "基础", "信号灯",   RuntimeControlType::StatusLight },
            { "基础", "按钮",     RuntimeControlType::Button },
            { "基础", "参数设置", RuntimeControlType::ParamSetting },
            { "基础", "开关控制", RuntimeControlType::SwitchControl },
            { "布局", "组合框",   RuntimeControlType::ComboBox },
            { "布局", "Tab页",    RuntimeControlType::TabPage },
            { "图表", "表格",     RuntimeControlType::ResultTable },
            { "图表", "线图阵列", RuntimeControlType::LineChart },
            { "图表", "多数据线图", RuntimeControlType::MultiLineChart },
            { "图表", "饼图", RuntimeControlType::PieChart },
            { "其它", "IO状态",   RuntimeControlType::IoStatus },
            { "其它", "图片",     RuntimeControlType::ImageAsset },
            { "其它", "世界时钟", RuntimeControlType::WorldClock },
        };
        QString lastGroup;
        for (const PaletteItem &pi : items) {
            const QString group = QString::fromUtf8(pi.group);
            if (group != lastGroup) {
                auto *header = new QListWidgetItem(group, m_palette);
                header->setFlags(Qt::NoItemFlags);
                header->setData(Qt::UserRole, QVariant());   // 组标题不是控件
                QFont hf = header->font();
                hf.setBold(true);
                header->setFont(hf);
                header->setBackground(QColor(0x2a, 0x2a, 0x34));
                header->setForeground(QColor(0x9a, 0x9e, 0xac));
                lastGroup = group;
            }
            auto *it = new QListWidgetItem(QString::fromUtf8(pi.label), m_palette);
            it->setData(Qt::UserRole, int(pi.type));
            it->setFlags(it->flags() | Qt::ItemIsDragEnabled);
        }
    }
    // 选中第一个真正的控件（跳过组标题）
    for (int i = 0; i < m_palette->count(); ++i) {
        if (m_palette->item(i)->data(Qt::UserRole).isValid()) {
            m_palette->setCurrentRow(i);
            break;
        }
    }
    m_palette->setMaximumWidth(160);
    // 官方 9.2.2 的拖放添加：保留原有「添加控件按钮 / 双击」两条路径，另开拖拽。
    m_palette->setDragEnabled(true);
    m_palette->setDragDropMode(QAbstractItemView::DragOnly);

    auto *addBtn = new QPushButton(QStringLiteral("添加控件"), paletteGroup);
    addBtn->setStyleSheet("QPushButton{background:#3a6ea5;color:white;border:none;border-radius:4px;padding:6px;}");
    connect(addBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::addCurrentPaletteControl);
    connect(m_palette, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { addCurrentPaletteControl(); });

    auto *paletteLay = new QVBoxLayout(paletteGroup);
    paletteLay->addWidget(m_palette, 1);
    paletteLay->addWidget(addBtn);

    // ---- 中间：画布 ----
    auto *canvasGroup = new QGroupBox(QStringLiteral("画布"), this);
    m_canvasScroll = new QScrollArea(canvasGroup);
    m_canvas = new RuntimeDesignerCanvas(m_canvasScroll);
    // 几何编辑（拖动 / 右下角缩放 / 方向键）走「改前快照」撤销：一次手势只入栈一枚
    connect(m_canvas, &RuntimeDesignerCanvas::geometryEditStarted,
            this, &RuntimeInterfaceDesigner::onGeometryEditStarted);
    connect(m_canvas, &RuntimeDesignerCanvas::geometryEditFinished,
            this, &RuntimeInterfaceDesigner::onGeometryEditFinished);
    setCanvasPage();
    connect(m_canvas, &RuntimeDesignerCanvas::paletteDropped,
            this, &RuntimeInterfaceDesigner::addControlAt);
    connect(m_canvas, &RuntimeDesignerCanvas::controlContextMenu,
            this, &RuntimeInterfaceDesigner::onControlContextMenu);
    m_canvasScroll->setWidget(m_canvas);
    m_canvasScroll->setWidgetResizable(true);
    m_canvasScroll->setStyleSheet("QScrollArea{background:#1a1a20;border:1px solid #3a3a4a;}");

    auto *delBtn = new QPushButton(QStringLiteral("删除选中"), canvasGroup);
    auto *clearBtn = new QPushButton(QStringLiteral("清空本页"), canvasGroup);
    delBtn->setStyleSheet("QPushButton{background:#8a3a3a;color:white;border:none;border-radius:4px;padding:6px;}");
    clearBtn->setStyleSheet("QPushButton{background:#5a5a6a;color:white;border:none;border-radius:4px;padding:6px;}");
    connect(delBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::deleteSelected);
    connect(clearBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::clearAll);

    auto *dupBtn = new QPushButton(QStringLiteral("复制选中"), canvasGroup);
    dupBtn->setToolTip(QStringLiteral("复制选中控件（Ctrl+D），偏移 24px"));
    dupBtn->setStyleSheet("QPushButton{background:#3a6ea5;color:white;border:none;border-radius:4px;padding:6px;}");
    connect(dupBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::duplicateSelected);
    auto *dupShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this);
    connect(dupShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::duplicateSelected);
    // 官方 9.1 工具条注：复制粘贴仅支持 Ctrl+C / Ctrl+V。快捷键是 WindowShortcut 上下文，
    // 焦点在输入框内时由 FocusGuard 让位给文本编辑，不抢输入框的复制粘贴。
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, this);
    connect(copyShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::copySelected);
    auto *pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    connect(pasteShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::pasteControl);
    // Delete 删除选中控件（属性输入框内按 Delete 仍由输入框处理，不会误删）
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::deleteSelected);

    // 撤销/重做（结构性操作：加删控件/页/列、复制、清空）
    auto *undoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")), this);
    connect(undoShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::undo);
    auto *redoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Y")), this);
    connect(redoShortcut, &QShortcut::activated, this, &RuntimeInterfaceDesigner::redo);

    // 批量对齐（Ctrl+点击选 2 个以上后点用）
    // 官方 9.1：对齐 6 种（左/水平居中/右/顶/垂直居中/底）+ 分布 2 种（水平/垂直，需 ≥3 个）
    auto *alignRow = new QHBoxLayout();
    const QString alignStyle =
        QStringLiteral("QPushButton{background:#5a5a6a;color:white;border:none;border-radius:3px;padding:3px 6px;}");
    struct AlignBtn { const char *label; void (RuntimeInterfaceDesigner::*slot)(); bool spread; };
    const AlignBtn alignDefs[] = {
        { "左对齐", &RuntimeInterfaceDesigner::onAlignLeft, false },
        { "水平居中", &RuntimeInterfaceDesigner::onAlignHCenter, false },
        { "右对齐", &RuntimeInterfaceDesigner::onAlignRight, false },
        { "顶对齐", &RuntimeInterfaceDesigner::onAlignTop, false },
        { "垂直居中", &RuntimeInterfaceDesigner::onAlignVCenter, false },
        { "底对齐", &RuntimeInterfaceDesigner::onAlignBottom, false },
        { "水平分布", &RuntimeInterfaceDesigner::onAlignHSpread, true },
        { "垂直分布", &RuntimeInterfaceDesigner::onAlignVSpread, true },
    };
    for (const AlignBtn &def : alignDefs) {
        auto *b = new QPushButton(QString::fromUtf8(def.label), canvasGroup);
        b->setStyleSheet(alignStyle);
        if (def.spread) b->setToolTip(QStringLiteral("需选中 3 个以上控件"));
        alignRow->addWidget(b);
        connect(b, &QPushButton::clicked, this, def.slot);
    }
    alignRow->addStretch();

    auto *canvasToolRow = new QHBoxLayout();
    // 控件层级：运行时后创建者盖住先创建者；上移/下移改变控件在列表中的先后
    // 官方工具条 4 档：顶层 / 上一层 / 底层 / 下一层
    m_raiseBtn = new QPushButton(tr("上移一层"), canvasGroup);
    m_lowerBtn = new QPushButton(tr("下移一层"), canvasGroup);
    auto *toTopBtn = new QPushButton(tr("置于顶层"), canvasGroup);
    auto *toBottomBtn = new QPushButton(tr("置于底层"), canvasGroup);
    for (QPushButton *b : {m_raiseBtn, m_lowerBtn, toTopBtn, toBottomBtn}) {
        b->setStyleSheet("QPushButton{background:#5a5a6a;color:white;border:none;border-radius:4px;padding:4px 8px;}");
        b->setToolTip(tr("调整控件叠放顺序（运行时后创建者在上）"));
    }
    connect(m_raiseBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::raiseSelected);
    connect(m_lowerBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::lowerSelected);
    connect(toTopBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::raiseToTop);
    connect(toBottomBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::lowerToBottom);

    canvasToolRow->addStretch();
    canvasToolRow->addWidget(toBottomBtn);
    canvasToolRow->addWidget(m_lowerBtn);
    canvasToolRow->addWidget(m_raiseBtn);
    canvasToolRow->addWidget(toTopBtn);
    canvasToolRow->addWidget(dupBtn);
    canvasToolRow->addWidget(delBtn);
    canvasToolRow->addWidget(clearBtn);

    // 编辑内层Tab：聚焦当前页某个内层标签的控件（「全部画布」= 不过滤）
    auto *editTabRow = new QHBoxLayout();
    editTabRow->addWidget(new QLabel(QStringLiteral("编辑内层Tab："), canvasGroup));
    m_editInnerTabCombo = new QComboBox(canvasGroup);
    m_editInnerTabCombo->setObjectName(QStringLiteral("designerEditInnerTabCombo"));
    editTabRow->addWidget(m_editInnerTabCombo);
    editTabRow->addStretch();
    connect(m_editInnerTabCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_canvas->setEditInnerTab(m_editInnerTabCombo->currentData().toInt());
    });

    // 画布尺寸 + 「按比例适配控件」（官方 9.3.1 画布基础配置；B1 补齐设计器入口）
    auto *canvasSizeRow = new QHBoxLayout();
    canvasSizeRow->addWidget(new QLabel(QStringLiteral("画布尺寸："), canvasGroup));
    m_canvasWidthSpin = new QSpinBox(canvasGroup);
    m_canvasWidthSpin->setObjectName(QStringLiteral("designerCanvasWidthSpin"));
    m_canvasWidthSpin->setRange(200, 8000);
    m_canvasWidthSpin->setToolTip(QStringLiteral("画布宽度（像素）"));
    canvasSizeRow->addWidget(m_canvasWidthSpin);
    canvasSizeRow->addWidget(new QLabel(QStringLiteral("×"), canvasGroup));
    m_canvasHeightSpin = new QSpinBox(canvasGroup);
    m_canvasHeightSpin->setObjectName(QStringLiteral("designerCanvasHeightSpin"));
    m_canvasHeightSpin->setRange(200, 8000);
    m_canvasHeightSpin->setToolTip(QStringLiteral("画布高度（像素）"));
    canvasSizeRow->addWidget(m_canvasHeightSpin);
    m_canvasScaleCheck = new QCheckBox(QStringLiteral("按比例适配控件"), canvasGroup);
    m_canvasScaleCheck->setObjectName(QStringLiteral("designerCanvasScaleCheck"));
    m_canvasScaleCheck->setChecked(true);
    m_canvasScaleCheck->setToolTip(QStringLiteral("改画布尺寸时按新旧尺寸比例缩放画布区控件（固定栏控件 1:1 不动）"));
    canvasSizeRow->addWidget(m_canvasScaleCheck);
    auto *applyCanvasBtn = new QPushButton(QStringLiteral("应用画布尺寸"), canvasGroup);
    applyCanvasBtn->setObjectName(QStringLiteral("designerCanvasApplyBtn"));
    applyCanvasBtn->setStyleSheet("QPushButton{background:#3a6ea5;color:white;border:none;border-radius:4px;padding:4px 8px;}");
    connect(applyCanvasBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::applyCanvasSize);
    canvasSizeRow->addWidget(applyCanvasBtn);
    canvasSizeRow->addStretch();

    // 固定栏厚度（官方 9.1 四向固定栏；页级字段，运行期 1:1 不缩放）
    auto *barRow = new QHBoxLayout();
    barRow->addWidget(new QLabel(QStringLiteral("固定栏：上"), canvasGroup));
    m_barTopSpin = new QSpinBox(canvasGroup);
    m_barTopSpin->setObjectName(QStringLiteral("designerBarTopSpin"));
    m_barTopSpin->setRange(0, 300);
    barRow->addWidget(m_barTopSpin);
    barRow->addWidget(new QLabel(QStringLiteral("下"), canvasGroup));
    m_barBottomSpin = new QSpinBox(canvasGroup);
    m_barBottomSpin->setObjectName(QStringLiteral("designerBarBottomSpin"));
    m_barBottomSpin->setRange(0, 300);
    barRow->addWidget(m_barBottomSpin);
    barRow->addWidget(new QLabel(QStringLiteral("左"), canvasGroup));
    m_barLeftSpin = new QSpinBox(canvasGroup);
    m_barLeftSpin->setObjectName(QStringLiteral("designerBarLeftSpin"));
    m_barLeftSpin->setRange(0, 300);
    barRow->addWidget(m_barLeftSpin);
    barRow->addWidget(new QLabel(QStringLiteral("右"), canvasGroup));
    m_barRightSpin = new QSpinBox(canvasGroup);
    m_barRightSpin->setObjectName(QStringLiteral("designerBarRightSpin"));
    m_barRightSpin->setRange(0, 300);
    barRow->addWidget(m_barRightSpin);
    const QString barTip = QStringLiteral("固定栏厚度（像素，运行期 1:1 不缩放；应用到当前页）");
    for (QSpinBox *s : {m_barTopSpin, m_barBottomSpin, m_barLeftSpin, m_barRightSpin}) {
        s->setToolTip(barTip);
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { onBarThicknessChanged(); });
    }
    barRow->addStretch();

    auto *canvasLay = new QVBoxLayout(canvasGroup);
    canvasLay->addLayout(editTabRow);
    canvasLay->addLayout(canvasSizeRow);
    canvasLay->addLayout(barRow);
    canvasLay->addWidget(m_canvasScroll, 1);
    canvasLay->addLayout(alignRow);
    canvasLay->addLayout(canvasToolRow);
    refreshEditInnerTabCombo();
    refreshCanvasConfigControls();

    // ---- 右侧：属性面板 ----
    // 官方实机（选中「图像」控件）是 4 个可折叠分组：标识 / 数据源 / 布局 / 外观。
    // 手册 9.3 文字写「三大类」，以截图为准。B1 只对齐分组结构；布局组的
    // 自动填充、T/B/L/R 边距、九宫格对齐要扩展 RuntimeControl 模型，归 B2。
    auto *propGroup = new QGroupBox(QStringLiteral("基础设置"), this);
    auto *propOuter = new QVBoxLayout();
    auto makeSection = [this, propOuter](const QString &name) -> QFormLayout * {
    auto *box = new QGroupBox(name, this);
    box->setStyleSheet("QGroupBox{font-weight:bold;border:none;border-bottom:1px solid #3a3a4a;"
    "padding-top:6px;margin-top:2px;}");
    auto *f = new QFormLayout(box);
    f->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    propOuter->addWidget(box);
    return f;
    };
    QFormLayout *fIdentity = makeSection(QStringLiteral("标识"));
    QFormLayout *fData = makeSection(QStringLiteral("数据源"));
    QFormLayout *fLayout = makeSection(QStringLiteral("布局"));
    QFormLayout *fAppear = makeSection(QStringLiteral("外观"));

    m_titleEdit = new QLineEdit(propGroup);
    m_typeLabel = new QLabel(QStringLiteral("-"), propGroup);
    m_bindTypeCombo = new QComboBox(propGroup);
    m_bindKeyCombo = new QComboBox(propGroup);
    m_bindKeyCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_bindKeyCombo->setMinimumContentsLength(12);
    m_colorEdit = new QLineEdit(propGroup);
    m_fontSpin = new QSpinBox(propGroup);
    m_fontSpin->setRange(8, 96);
    m_columnSource = new QComboBox(propGroup);
    m_columnSource->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_columnSource->setMinimumContentsLength(12);
    m_columnList = new QListWidget(propGroup);
    m_columnList->setMaximumHeight(90);
    m_columnAddBtn = new QPushButton(QStringLiteral("＋添加列"), propGroup);
    m_columnDelBtn = new QPushButton(QStringLiteral("－删除列"), propGroup);
    m_tableMaxRows = new QSpinBox(propGroup);
    m_tableMaxRows->setRange(1, 10000);
    connect(m_columnAddBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::onColumnAdd);
    connect(m_columnDelBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::onColumnDel);
    connect(m_columnList, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *) { onColumnDel(); });

    fIdentity->addRow(QStringLiteral("类型"), m_typeLabel);
    fIdentity->addRow(QStringLiteral("标题"), m_titleEdit);

    // 官方：按钮的「数据源」是一个「配置」按钮（打开动作表）；其它控件仍是绑定方式/对象
    m_dataForm = fData;
    m_actionConfigBtn = new QPushButton(QStringLiteral("配置"), propGroup);
    m_actionConfigBtn->setToolTip(
        QStringLiteral("配置按钮的触发命令（触发命令类型 / 方法 / 输入参数）"));
    connect(m_actionConfigBtn, &QPushButton::clicked,
            this, &RuntimeInterfaceDesigner::configureButtonActions);
    fData->addRow(QStringLiteral("数据源"), m_actionConfigBtn);
    fData->addRow(QStringLiteral("绑定方式"), m_bindTypeCombo);
    fData->addRow(QStringLiteral("绑定对象"), m_bindKeyCombo);
    // 参数设置控件：参数名 + 数值范围（仅该类型显示）
    m_paramKeyEdit = new QLineEdit(propGroup);
    m_paramKeyEdit->setPlaceholderText(QStringLiteral("节点参数键，如 exposure"));
    fData->addRow(QStringLiteral("参数名"), m_paramKeyEdit);
    auto *rangeHost = new QWidget(propGroup);
    auto *rangeRow = new QHBoxLayout(rangeHost);
    rangeRow->setContentsMargins(0, 0, 0, 0);
    m_paramMinSpin = new QDoubleSpinBox(rangeHost);
    m_paramMaxSpin = new QDoubleSpinBox(rangeHost);
    for (QDoubleSpinBox *s : {m_paramMinSpin, m_paramMaxSpin}) {
        s->setRange(-1e9, 1e9);
        s->setDecimals(3);
    }
    m_paramMaxSpin->setValue(100.0);
    rangeRow->addWidget(m_paramMinSpin);
    rangeRow->addWidget(new QLabel(QStringLiteral("~"), rangeHost));
    rangeRow->addWidget(m_paramMaxSpin);
    m_paramRangeHost = rangeHost;
    fData->addRow(QStringLiteral("取值范围"), rangeHost);
    // 组合框候选值（仅该类型显示）
    m_comboOptionsEdit = new QLineEdit(propGroup);
    m_comboOptionsEdit->setPlaceholderText(QStringLiteral("候选值，逗号分隔，如 配方A,配方B"));
    fData->addRow(QStringLiteral("候选值"), m_comboOptionsEdit);

    // 图片（官方 9.3）：本地图片路径 + 浏览
    auto *imageHost = new QWidget(propGroup);
    auto *imageRow = new QHBoxLayout(imageHost);
    imageRow->setContentsMargins(0, 0, 0, 0);
    m_imagePathEdit = new QLineEdit(imageHost);
    m_imagePathEdit->setPlaceholderText(QStringLiteral("本地图片路径"));
    auto *browseBtn = new QPushButton(QStringLiteral("…"), imageHost);
    browseBtn->setFixedWidth(28);
    imageRow->addWidget(m_imagePathEdit, 1);
    imageRow->addWidget(browseBtn);
    m_imageHost = imageHost;
    fAppear->addRow(QStringLiteral("图片"), imageHost);
    connect(browseBtn, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(
            this, tr("选择图片"), QString(),
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.bmp)"));
        if (!f.isEmpty() && m_imagePathEdit) m_imagePathEdit->setText(f);
    });

    // 世界时钟（官方 9.3）：相对 UTC 的分钟偏移
    m_tzOffsetSpin = new QSpinBox(propGroup);
    m_tzOffsetSpin->setRange(-720, 840);
    m_tzOffsetSpin->setSingleStep(60);
    m_tzOffsetSpin->setValue(480);
    m_tzOffsetSpin->setSuffix(QStringLiteral(" 分钟（+480 = UTC+8）"));
    m_tzHost = m_tzOffsetSpin;   // 直接隐藏 spin 自身（整行只有一个控件）
    fAppear->addRow(QStringLiteral("时区偏移"), m_tzOffsetSpin);
    fData->addRow(QStringLiteral("新列来源"), m_columnSource);
    fData->addRow(QStringLiteral("已有列"), m_columnList);
    auto *colBtnRow = new QHBoxLayout();
    colBtnRow->addWidget(m_columnAddBtn);
    colBtnRow->addWidget(m_columnDelBtn);
    fData->addRow(QString(), colBtnRow);
    fData->addRow(QStringLiteral("最大行数"), m_tableMaxRows);

    // 布局：官方有自动填充 / 尺寸 / 边距 T·B·L·R + 同步 / 九宫格对齐。
    // B1 先给出只读尺寸与位置（拖放/拖动后实时刷新），可编辑字段随 B2 模型扩展补上。
    m_geometryLabel = new QLabel(QStringLiteral("-"), propGroup);
    m_geometryLabel->setStyleSheet("color:#c8c8d0;");
    fLayout->addRow(QStringLiteral("尺寸 / 位置"), m_geometryLabel);

    // 所属区域（官方 9.1 固定栏）：画布区随窗口缩放，固定栏 1:1 不缩放
    m_zoneCombo = new QComboBox(propGroup);
    m_zoneCombo->addItem(QStringLiteral("画布"), int(RuntimeZone::Canvas));
    m_zoneCombo->addItem(QStringLiteral("顶部固定栏"), int(RuntimeZone::TopBar));
    m_zoneCombo->addItem(QStringLiteral("底部固定栏"), int(RuntimeZone::BottomBar));
    m_zoneCombo->addItem(QStringLiteral("左侧固定栏"), int(RuntimeZone::LeftBar));
    m_zoneCombo->addItem(QStringLiteral("右侧固定栏"), int(RuntimeZone::RightBar));
    fLayout->addRow(QStringLiteral("所属区域"), m_zoneCombo);
    // 内层 Tab 归属（仅当前页启用了内层 Tab 时显示）
    m_innerTabCombo = new QComboBox(propGroup);
    fLayout->addRow(QStringLiteral("内层Tab"), m_innerTabCombo);
    m_innerTabLabel = qobject_cast<QLabel *>(fLayout->labelForField(m_innerTabCombo));
    auto *layoutHint = new QLabel(
        QStringLiteral("官方的自动填充、T/B/L/R 边距与九宫格对齐待布局模型扩展（B2）"),
        propGroup);
    layoutHint->setWordWrap(true);
    layoutHint->setStyleSheet("color:#8a8a9a;font-size:11px;");
    fLayout->addRow(layoutHint);

    // 颜色：取色器 + Hex 输入
    auto *colorRow = new QHBoxLayout();
    colorRow->addWidget(m_colorEdit, 1);
    auto *colorPick = new QPushButton(QStringLiteral("选色"), propGroup);
    colorPick->setFixedWidth(44);
    colorRow->addWidget(colorPick);
    fAppear->addRow(QStringLiteral("颜色"), colorRow);
    connect(colorPick, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(QColor(m_colorEdit->text()),
                                                this, QStringLiteral("选择颜色"));
        if (c.isValid()) m_colorEdit->setText(c.name());
    });
    // 官方「字体」是「大小 + 字形」组合；这里拆成字号 + 加粗，行为等价
    auto *fontRow = new QHBoxLayout();
    fontRow->addWidget(m_fontSpin, 1);
    m_boldCheck = new QCheckBox(QStringLiteral("加粗"), propGroup);
    fontRow->addWidget(m_boldCheck);
    fAppear->addRow(QStringLiteral("字体"), fontRow);

    // 官方三个颜色字段：背景 / 边框颜色 / 点击颜色。留空 = 不绘制，保持旧外观。
    auto makeColorRow = [this, propGroup](QLineEdit *&edit) {
        auto *row = new QHBoxLayout();
        edit = new QLineEdit(propGroup);
        edit->setPlaceholderText(QStringLiteral("留空 = 默认"));
        row->addWidget(edit, 1);
        auto *pick = new QPushButton(QStringLiteral("选色"), propGroup);
        pick->setFixedWidth(44);
        row->addWidget(pick);
        connect(pick, &QPushButton::clicked, this, [this, edit]() {
            const QColor c = QColorDialog::getColor(QColor(edit->text()),
                                                    this, QStringLiteral("选择颜色"));
            if (c.isValid()) edit->setText(c.name());
        });
        return row;
    };
    fAppear->addRow(QStringLiteral("背景"), makeColorRow(m_bgColorEdit));
    fAppear->addRow(QStringLiteral("边框颜色"), makeColorRow(m_borderColorEdit));
    fAppear->addRow(QStringLiteral("点击颜色"), makeColorRow(m_pressedColorEdit));

    m_visibleCheck = new QCheckBox(QStringLiteral("运行时显示该控件"), propGroup);
    m_visibleCheck->setToolTip(QStringLiteral("取消勾选后控件仍保留在设计画布上，但运行界面不显示"));
    fAppear->addRow(QStringLiteral("可见"), m_visibleCheck);

    // 线图阵列（官方 9.3 图表）：数据系列 + 外观
    m_chartGroup = new QGroupBox(QStringLiteral("线图阵列"), this);
    auto *chartForm = new QFormLayout(m_chartGroup);
    m_chartTitleEdit = new QLineEdit(m_chartGroup);
    m_chartTitleEdit->setPlaceholderText(QStringLiteral("空 = 用控件标题"));
    m_chartShowTitleCheck = new QCheckBox(QStringLiteral("显示标题栏"), m_chartGroup);
    m_chartHistorySpin = new QSpinBox(m_chartGroup);
    m_chartHistorySpin->setRange(1, 1000);
    m_chartHistorySpin->setValue(30);
    m_chartHistorySpin->setToolTip(QStringLiteral("官方「最新执行数据条数」：只保留最近 N 次执行"));
    m_chartBgEdit = new QLineEdit(m_chartGroup);
    m_chartAxisEdit = new QLineEdit(m_chartGroup);
    m_chartSource = new QComboBox(m_chartGroup);
    m_chartSource->setMinimumContentsLength(12);
    m_chartSource->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_chartSeriesList = new QListWidget(m_chartGroup);
    m_chartSeriesList->setMaximumHeight(90);
    m_chartSeriesAddBtn = new QPushButton(QStringLiteral("＋添加系列"), m_chartGroup);
    m_chartSeriesDelBtn = new QPushButton(QStringLiteral("－删除系列"), m_chartGroup);
    chartForm->addRow(QStringLiteral("标题"), m_chartTitleEdit);
    chartForm->addRow(QString(), m_chartShowTitleCheck);
    chartForm->addRow(QStringLiteral("最新执行条数"), m_chartHistorySpin);
    chartForm->addRow(QStringLiteral("背景色"), m_chartBgEdit);
    chartForm->addRow(QStringLiteral("坐标轴颜色"), m_chartAxisEdit);
    chartForm->addRow(QStringLiteral("新系列来源"), m_chartSource);
    chartForm->addRow(QStringLiteral("已有系列"), m_chartSeriesList);
    auto *chartBtnRow = new QHBoxLayout();
    chartBtnRow->addWidget(m_chartSeriesAddBtn);
    chartBtnRow->addWidget(m_chartSeriesDelBtn);
    chartForm->addRow(QString(), chartBtnRow);
    propOuter->addWidget(m_chartGroup);
    connect(m_chartSeriesAddBtn, &QPushButton::clicked,
            this, &RuntimeInterfaceDesigner::onChartSeriesAdd);
    connect(m_chartSeriesDelBtn, &QPushButton::clicked,
            this, &RuntimeInterfaceDesigner::onChartSeriesDel);
    connect(m_chartTitleEdit, &QLineEdit::textEdited,
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_chartShowTitleCheck, &QCheckBox::toggled,
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_chartHistorySpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_chartBgEdit, &QLineEdit::textEdited,
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_chartAxisEdit, &QLineEdit::textEdited,
            this, &RuntimeInterfaceDesigner::onPropertyEdited);

    auto *propHint = new QLabel(QStringLiteral("绑定方式说明:\n· 全局变量: 实时显示全局变量值\n· 节点输出: 绑定流程中算子的输出\n· 动作: 按钮点击触发的流程操作\n· 表格列: 每轮把各列当前值提交为一行\n  格式: 变量名,变量名 或 节点:模块完整名\n· IO状态: 绑定相机IO控制节点\n  灯=执行成功, 文本=线值/错误"), propGroup);
    propHint->setWordWrap(true);
    propHint->setStyleSheet("color:#8a8a9a;font-size:11px;");
    propOuter->addWidget(propHint);
    propOuter->addStretch();
    auto *propHost = new QWidget(propGroup);
    propHost->setLayout(propOuter);
    auto *propScroll = new QScrollArea(propGroup);
    propScroll->setWidget(propHost);
    propScroll->setWidgetResizable(true);
    propScroll->setFrameShape(QFrame::NoFrame);
    auto *propLay = new QVBoxLayout(propGroup);
    propLay->addWidget(propScroll);
    propGroup->setMaximumWidth(340);

    // ---- 底部按钮 ----
    auto *saveBtn = new QPushButton(tr("保存布局"), this);
    auto *previewBtn = new QPushButton(tr("预览"), this);
    auto *loadBtn = new QPushButton(tr("加载布局"), this);
    auto *applyBtn = new QPushButton(tr("应用并进入运行模式"), this);
    previewBtn->setToolTip(tr("按当前布局生成运行界面预览（不改动已保存的布局）"));
    applyBtn->setStyleSheet("QPushButton{background:#3d9a5a;color:white;border:none;border-radius:4px;padding:8px 16px;font-weight:bold;}");
    connect(saveBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::saveLayout);
    connect(previewBtn, &QPushButton::clicked, this, [this]() {
        if (m_layout.pages.isEmpty()) {
            QMessageBox::information(this, tr("预览"),
                                     QStringLiteral("当前没有可预览的布局"));
            return;
        }
        QDialog previewDialog(this);
        previewDialog.setWindowTitle(QStringLiteral("运行界面预览"));
        previewDialog.resize(960, 620);
        auto *layout = new QVBoxLayout(&previewDialog);
        auto *view = new RuntimeInterfaceView(&previewDialog);
        view->setInterface(m_layout);
        layout->addWidget(view);
        previewDialog.exec();
    });
    connect(loadBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::loadLayout);
    connect(applyBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::applyAndClose);

    // 模板（官方 9.1 工具条：应用已保存模板 / 把当前设计另存为模板）
    auto *tplSaveBtn = new QPushButton(tr("存为模板"), this);
    auto *tplApplyBtn = new QPushButton(tr("应用模板"), this);
    tplSaveBtn->setToolTip(tr("把当前设计保存到运行界面模板库（可复用）"));
    tplApplyBtn->setToolTip(tr("从模板库加载一套已保存的运行界面"));
    connect(tplSaveBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::saveAsTemplate);
    connect(tplApplyBtn, &QPushButton::clicked, this, &RuntimeInterfaceDesigner::applyTemplate);

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(previewBtn);
    btnRow->addWidget(loadBtn);
    btnRow->addWidget(tplSaveBtn);
    btnRow->addWidget(tplApplyBtn);
    btnRow->addStretch();
    btnRow->addWidget(applyBtn);

    // ---- 总布局 ----
    auto *leftLay = new QVBoxLayout();
    leftLay->addWidget(pageGroup);
    leftLay->addWidget(paletteGroup, 1);

    auto *mainLay = new QHBoxLayout();
    mainLay->addLayout(leftLay);
    mainLay->addWidget(canvasGroup, 1);
    mainLay->addWidget(propGroup);

    auto *root = new QVBoxLayout(this);
    root->addLayout(mainLay, 1);
    root->addLayout(btnRow);

    // ---- 信号 ----
    connect(m_canvas, &RuntimeDesignerCanvas::controlSelected,
            this, &RuntimeInterfaceDesigner::onControlSelected);

    connect(m_titleEdit, &QLineEdit::textChanged, this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_colorEdit, &QLineEdit::textChanged, this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_fontSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_visibleCheck, &QCheckBox::toggled, this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_boldCheck)
        connect(m_boldCheck, &QCheckBox::toggled, this, &RuntimeInterfaceDesigner::onPropertyEdited);
    for (QLineEdit *e : {m_bgColorEdit, m_borderColorEdit, m_pressedColorEdit}) {
        if (e)
            connect(e, &QLineEdit::textChanged, this, &RuntimeInterfaceDesigner::onPropertyEdited);
    }
    if (m_paramKeyEdit)
        connect(m_paramKeyEdit, &QLineEdit::textChanged,
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_comboOptionsEdit)
        connect(m_comboOptionsEdit, &QLineEdit::textChanged,
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_imagePathEdit)
        connect(m_imagePathEdit, &QLineEdit::textChanged,
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_tzOffsetSpin)
        connect(m_tzOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_zoneCombo)
        connect(m_zoneCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    if (m_innerTabCombo)
        connect(m_innerTabCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &RuntimeInterfaceDesigner::onPropertyEdited);
    for (QDoubleSpinBox *s : {m_paramMinSpin, m_paramMaxSpin}) {
        if (s)
            connect(s, &QDoubleSpinBox::valueChanged,
                    this, &RuntimeInterfaceDesigner::onPropertyEdited);
    }
    connect(m_tableMaxRows, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RuntimeInterfaceDesigner::onPropertyEdited);
    connect(m_bindTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (m_updatingProps) return;
        populateBindKeyCombo();
        onPropertyEdited();
    });
    connect(m_bindKeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onPropertyEdited(); });

    onControlSelected(-1);
}

RuntimeControl *RuntimeInterfaceDesigner::selectedControl()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg) return nullptr;
    if (m_selected < 0 || m_selected >= pg->controls.size()) return nullptr;
    return &pg->controls[m_selected];
}

void RuntimeInterfaceDesigner::refreshPageList()
{
    m_updatingPages = true;
    m_pageList->clear();
    for (const RuntimeInterfacePage &p : m_layout.pages)
        m_pageList->addItem(p.pageName);
    if (m_layout.currentPageIndex >= 0 && m_layout.currentPageIndex < m_pageList->count())
        m_pageList->setCurrentRow(m_layout.currentPageIndex);
    m_updatingPages = false;
}

void RuntimeInterfaceDesigner::addPage()
{
    pushUndo();
    m_layout.currentPageIndex = m_layout.addPage();
    refreshPageList();
    m_selected = -1;
    setCanvasPage();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::removePage()
{
    if (m_layout.pages.size() <= 1) {
        QMessageBox::information(this, QStringLiteral("删除页"),
                                 QStringLiteral("至少保留一页"));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("删除页"),
                              QStringLiteral("确定删除当前页「%1」及其全部控件？")
                                  .arg(m_layout.currentPage()->pageName)) != QMessageBox::Yes)
        return;
    pushUndo();
    m_layout.removePage(m_layout.currentPageIndex);
    refreshPageList();
    m_selected = -1;
    setCanvasPage();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::renamePage()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("重命名页面"),
                                               QStringLiteral("页面名称:"), QLineEdit::Normal,
                                               pg->pageName, &ok);
    if (!ok) return;
    pg->pageName = name.trimmed().isEmpty() ? pg->pageName : name.trimmed();
    refreshPageList();
}

void RuntimeInterfaceDesigner::onPageSelected(int index)
{
    if (m_updatingPages) return;
    if (index < 0 || index >= m_layout.pages.size()) return;
    m_layout.currentPageIndex = index;
    m_selected = -1;
    setCanvasPage();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::addCurrentPaletteControl()
{
    if (!m_palette) return;
    addControlByRow(m_palette->currentRow());
}

void RuntimeInterfaceDesigner::enableInnerTabs()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg) return;
    const QString current = pg->innerTabNames.isEmpty()
                                ? QStringLiteral("参数,结果")
                                : pg->innerTabNames.join(QLatin1Char(','));
    bool ok = false;
    const QString text = QInputDialog::getText(
        this, tr("页内 Tab"),
        tr("标签名称（逗号分隔；清空 = 关闭内层 Tab）："), QLineEdit::Normal, current, &ok);
    if (!ok) return;

    QStringList names;
    const QStringList raw = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &s : raw) {
        const QString t = s.trimmed();
        if (!t.isEmpty()) names << t;
    }

    pushUndo();
    pg->innerTabNames = names;
    if (names.isEmpty()) {
        // 关闭内层 Tab：控件全部退回画布（不留悬空归属）
        for (RuntimeControl &c : pg->controls) c.innerTab = -1;
        pg->defaultInnerTab = 0;
    } else if (pg->defaultInnerTab >= names.size()) {
        pg->defaultInnerTab = 0;
    }
    // 越界归属同样退回画布
    for (RuntimeControl &c : pg->controls) {
        if (c.innerTab >= names.size()) c.innerTab = -1;
    }
    setCanvasPage();
    refreshPropertyPanel();
    refreshEditInnerTabCombo();
}

void RuntimeInterfaceDesigner::onControlContextMenu(int index, const QPoint &globalPos)
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || index < 0 || index >= pg->controls.size()) return;
    m_selected = index;   // 右键即选中，避免菜单作用到上一个控件

    const int count = pg->controls.size();
    QMenu menu(this);
    QAction *delAct = menu.addAction(QStringLiteral("删除"));
    menu.addSeparator();
    QAction *undoAct = menu.addAction(QStringLiteral("撤销"));
    QAction *redoAct = menu.addAction(QStringLiteral("重做"));
    undoAct->setEnabled(!m_undoStack.isEmpty());
    redoAct->setEnabled(!m_redoStack.isEmpty());
    menu.addSeparator();
    QMenu *layerMenu = menu.addMenu(QStringLiteral("图层位置"));
    QAction *topAct = layerMenu->addAction(QStringLiteral("置于顶层"));
    QAction *upAct = layerMenu->addAction(QStringLiteral("上一层"));
    QAction *downAct = layerMenu->addAction(QStringLiteral("下一层"));
    QAction *bottomAct = layerMenu->addAction(QStringLiteral("置于底层"));
    topAct->setEnabled(index < count - 1);
    upAct->setEnabled(index < count - 1);
    downAct->setEnabled(index > 0);
    bottomAct->setEnabled(index > 0);

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == delAct) deleteSelected();
    else if (chosen == undoAct) undo();
    else if (chosen == redoAct) redo();
    else if (chosen == topAct) raiseToTop();
    else if (chosen == upAct) raiseSelected();
    else if (chosen == downAct) lowerSelected();
    else if (chosen == bottomAct) lowerToBottom();
}

void RuntimeInterfaceDesigner::addControlAt(const QPoint &pos)
{
    if (!m_palette) return;
    addControlByRow(m_palette->currentRow(), pos);
}

void RuntimeInterfaceDesigner::addControlByRow(int row, const QPoint &dropPos)
{
    // 控件库有组标题（不可选、UserRole 为空），类型不能按行号推断
    RuntimeControlType type;
    if (!paletteTypeOf(m_palette ? m_palette->item(row) : nullptr, &type))
        return;

    // Tab页是**页级**能力：拖入即启用当前页的内层 Tab，不创建控件对象。
    // 这样不会出现"子控件指向已删容器"的悬空引用，退化行为也可定义。
    if (type == RuntimeControlType::TabPage) {
        enableInnerTabs();
        return;
    }

    const int n = m_layout.currentPage()->controls.size();
const int cascade = (n % 8) * 24;
QRect geo(40 + cascade, 40 + cascade, 300, 190);
if (type == RuntimeControlType::StatusLight) geo.setHeight(70);
if (type == RuntimeControlType::Button)     { geo.setWidth(160); geo.setHeight(48); }
if (type == RuntimeControlType::TextLabel)  { geo.setWidth(240); geo.setHeight(60); }
if (type == RuntimeControlType::ValueDisplay){ geo.setHeight(90); }
if (type == RuntimeControlType::ResultTable){ geo.setWidth(460); geo.setHeight(260); }
if (type == RuntimeControlType::LineChart
    || type == RuntimeControlType::MultiLineChart
    || type == RuntimeControlType::PieChart){ geo.setWidth(420); geo.setHeight(300); }
if (type == RuntimeControlType::IoStatus)   { geo.setWidth(260); geo.setHeight(80); }

// 拖放：按落点判定所属区域（官方 9.1 固定栏），再在该区域的坐标系里定位。
// 画布区坐标相对画布区左上角、随窗口缩放；固定栏坐标相对本栏左上角、1:1 不缩放。
RuntimeZone zone = RuntimeZone::Canvas;
if (!dropPos.isNull()) {
    RuntimeInterfacePage *pg = m_layout.currentPage();
    const QSize canvasSize = m_canvas ? m_canvas->size() : QSize(1200, 760);
    const int topH = qMax(0, pg->topBarHeight);
    const int botH = qMax(0, pg->bottomBarHeight);
    const int leftW = qMax(0, pg->leftBarWidth);
    const int rightW = qMax(0, pg->rightBarWidth);

    QPoint local;
    zone = zoneAtCanvasPoint(*pg, canvasSize, dropPos, &local);
    QSize areaSize;
    switch (zone) {
    case RuntimeZone::TopBar:    areaSize = QSize(canvasSize.width(), topH); break;
    case RuntimeZone::BottomBar: areaSize = QSize(canvasSize.width(), botH); break;
    case RuntimeZone::LeftBar:
        areaSize = QSize(leftW, qMax(0, canvasSize.height() - topH - botH));
        break;
    case RuntimeZone::RightBar:
        areaSize = QSize(rightW, qMax(0, canvasSize.height() - topH - botH));
        break;
    case RuntimeZone::Canvas:
        areaSize = QSize(qMax(0, canvasSize.width() - leftW - rightW),
                         qMax(0, canvasSize.height() - topH - botH));
        break;
    }
    // 固定栏通常很窄/很矮：先把控件尺寸压进区域，再定位，否则会溢出栏外
    if (areaSize.width() > 0)
        geo.setWidth(qMin(geo.width(), areaSize.width()));
    if (areaSize.height() > 0)
        geo.setHeight(qMin(geo.height(), areaSize.height()));
    geo.moveLeft(qBound(0, local.x(), qMax(0, areaSize.width() - geo.width())));
    geo.moveTop(qBound(0, local.y(), qMax(0, areaSize.height() - geo.height())));
}

pushUndo();
RuntimeControl *ctrl = m_layout.currentPage()->addControl(type, geo);
ctrl->zone = zone;
rebuildCanvas();
const int idx = m_layout.currentPage()->indexOf(ctrl);
if (idx >= 0) m_canvas->selectIndex(idx);
}

void RuntimeInterfaceDesigner::copySelected()
{
// 输入框内的 Ctrl+C 归文本编辑，不抢
if (FocusGuard::isTextEditingWidget(QApplication::focusWidget())) return;
RuntimeControl *ctrl = selectedControl();
if (!ctrl) return;
// 复用 RuntimeInterfacePage 的序列化：单控件临时页 → JSON → 自定义 MIME。
RuntimeInterfacePage tmp;
tmp.controls.append(*ctrl);
const QByteArray data = QJsonDocument(tmp.toJson()).toJson(QJsonDocument::Compact);
auto *mime = new QMimeData();
mime->setData(QLatin1String(kControlMime), data);
QApplication::clipboard()->setMimeData(mime);
}

void RuntimeInterfaceDesigner::pasteControl()
{
if (FocusGuard::isTextEditingWidget(QApplication::focusWidget())) return;
const QMimeData *mime = QApplication::clipboard()->mimeData();
if (!mime || !mime->hasFormat(QLatin1String(kControlMime))) return;
QJsonParseError err;
const QJsonDocument doc =
QJsonDocument::fromJson(mime->data(QLatin1String(kControlMime)), &err);
if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
RuntimeInterfacePage tmp;
if (!tmp.fromJson(doc.object()) || tmp.controls.isEmpty()) return;
RuntimeControl copy = tmp.controls.first();
copy.geometry.translate(24, 24);   // 与 Ctrl+D 一致的级联偏移
pushUndo();
m_layout.currentPage()->controls.append(copy);
rebuildCanvas();
m_canvas->selectIndex(m_layout.currentPage()->controls.size() - 1);
}

void RuntimeInterfaceDesigner::deleteSelected()
{
    if (m_selected < 0) return;
    pushUndo();
    m_layout.currentPage()->removeControl(m_selected);
    m_selected = -1;
    rebuildCanvas();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::clearAll()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || pg->controls.isEmpty()) return;
    if (QMessageBox::question(this, QStringLiteral("清空本页"),
                              QStringLiteral("确定清空当前页全部控件？")) != QMessageBox::Yes)
        return;
    pushUndo();
    pg->clear();
    m_selected = -1;
    rebuildCanvas();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::saveLayout()
{
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存运行界面布局"), defaultLayoutPath(),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    if (m_layout.saveToFile(path)) {
        QMessageBox::information(this, tr("保存布局"),
                                 QStringLiteral("已保存到 %1").arg(path));
    } else {
        QMessageBox::warning(this, tr("保存布局"),
                             QStringLiteral("保存失败"));
    }
}

void RuntimeInterfaceDesigner::loadLayout()
{
    QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("加载运行界面布局"), defaultLayoutPath(),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) return;
    RuntimeInterface tmp;
    if (tmp.loadFromFile(path)) {
        m_layout = tmp;
        m_layout.ensurePage();
        m_selected = -1;
        refreshPageList();
        setCanvasPage();
        refreshPropertyPanel();
    } else {
        QMessageBox::warning(this, tr("加载布局"),
                             QStringLiteral("加载失败，文件格式不正确"));
    }
}

void RuntimeInterfaceDesigner::applyAndClose()
{
    m_layout.saveToFile(defaultLayoutPath());
    accept();
}

void RuntimeInterfaceDesigner::onColumnAdd()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl || ctrl->type != RuntimeControlType::ResultTable) return;
    const QString src = m_columnSource->currentText().trimmed();
    if (src.isEmpty()) return;

    ResultColumn col;
    if (src.startsWith(QStringLiteral("节点:"))) {
        col.bindType = QStringLiteral("node");
        col.bindKey = src.mid(3).trimmed();
    } else {
        col.bindType = QStringLiteral("global");
        col.bindKey = src;
    }
    col.header = col.bindKey;
    if (col.bindKey.isEmpty()) return;
    // 防重复列
    for (const ResultColumn &c : ctrl->columns) {
        if (c.bindKey == col.bindKey && c.bindType == col.bindType) return;
    }
    pushUndo();
    ctrl->columns.append(col);
    refreshPropertyPanel();          // 重填列列表（内部有 updating 保护）
    m_canvas->refreshControl(m_selected);
}

void RuntimeInterfaceDesigner::onColumnDel()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl || ctrl->type != RuntimeControlType::ResultTable) return;
    const int row = m_columnList->currentRow();
    if (row < 0 || row >= ctrl->columns.size()) return;
    pushUndo();
    ctrl->columns.removeAt(row);
    refreshPropertyPanel();
    m_canvas->refreshControl(m_selected);
}

void RuntimeInterfaceDesigner::onChartSeriesAdd()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl || !m_chartSource) return;
    if (ctrl->type != RuntimeControlType::LineChart
        && ctrl->type != RuntimeControlType::MultiLineChart
        && ctrl->type != RuntimeControlType::PieChart) return;
    const QString src = m_chartSource->currentText().trimmed();
    if (src.isEmpty()) return;
    ChartSeries series;
    if (src.startsWith(QStringLiteral("节点:"))) {
        series.bindType = QStringLiteral("node");
        series.bindKey = src.mid(3).trimmed();
    } else {
        series.bindType = QStringLiteral("global");
        series.bindKey = src;
    }
    if (series.bindKey.isEmpty()) return;
    for (const ChartSeries &existing : ctrl->chartSeries) {
        if (existing.bindKey == series.bindKey && existing.bindType == series.bindType)
            return;
    }
    pushUndo();
    static const char *kColors[] = {"#4FC3F7", "#FFB74D", "#81C784",
                                    "#E57373", "#BA68C8", "#4DD0E1"};
    series.name = series.bindKey;
    series.color = QString::fromLatin1(
        kColors[ctrl->chartSeries.size() % int(sizeof(kColors) / sizeof(kColors[0]))]);
    ctrl->chartSeries.append(series);
    refreshPropertyPanel();
    m_canvas->refreshControl(m_selected);
}

void RuntimeInterfaceDesigner::onChartSeriesDel()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl || !m_chartSeriesList) return;
    if (ctrl->type != RuntimeControlType::LineChart
        && ctrl->type != RuntimeControlType::MultiLineChart
        && ctrl->type != RuntimeControlType::PieChart) return;
    const int row = m_chartSeriesList->currentRow();
    if (row < 0 || row >= ctrl->chartSeries.size()) return;
    pushUndo();
    ctrl->chartSeries.removeAt(row);
    refreshPropertyPanel();
    m_canvas->refreshControl(m_selected);
}

void RuntimeInterfaceDesigner::duplicateSelected()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) return;
    RuntimeControl copy = *ctrl;
    copy.geometry = copy.geometry.translated(24, 24);   // 级联偏移避免完全重叠
    pushUndo();
    m_layout.currentPage()->controls.append(copy);
    rebuildCanvas();
    m_canvas->selectIndex(m_layout.currentPage()->controls.size() - 1);
}

void RuntimeInterfaceDesigner::raiseSelected()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected < 0 || m_selected >= pg->controls.size() - 1) return;
    pushUndo();
    pg->controls.swapItemsAt(m_selected, m_selected + 1);
    ++m_selected;
    rebuildCanvas();
    m_canvas->selectIndex(m_selected);
}

void RuntimeInterfaceDesigner::lowerSelected()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected <= 0 || m_selected >= pg->controls.size()) return;
    pushUndo();
    pg->controls.swapItemsAt(m_selected, m_selected - 1);
    --m_selected;
    rebuildCanvas();
    m_canvas->selectIndex(m_selected);
}

// 官方工具条 4 档中的顶层 / 底层：把选中控件直接移到列表首尾
void RuntimeInterfaceDesigner::raiseToTop()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected < 0 || m_selected >= pg->controls.size() - 1) return;
    pushUndo();
    pg->controls.append(pg->controls.takeAt(m_selected));
    m_selected = pg->controls.size() - 1;
    rebuildCanvas();
    m_canvas->selectIndex(m_selected);
}

void RuntimeInterfaceDesigner::lowerToBottom()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected <= 0 || m_selected >= pg->controls.size()) return;
    pushUndo();
    pg->controls.prepend(pg->controls.takeAt(m_selected));
    m_selected = 0;
    rebuildCanvas();
    m_canvas->selectIndex(m_selected);
}

// ==================== 撤销/重做 ====================

// 几何手势的「改前快照」：开始时深拷贝一份，结束时只在几何真的变了才入栈。
// 这样拖动 / 右下角缩放 / 方向键微调都进同一个撤销栈，一次手势一枚撤销点。
void RuntimeInterfaceDesigner::onGeometryEditStarted()
{
    if (m_geometryEditActive) return;   // 手势嵌套（多选同步移动等）只记最外层
    m_geometryUndoBefore = m_layout;    // QList/结构体值语义拷贝
    // QList 是写时复制：上面这行仍与 m_layout 共享同一块页存储。拖动时的几何写回会
    // 顺带改到共享页，快照于是跟着变（结束时比对「没变」⇒ 撤销点永远建不出来）。
    // 强制 detach 成真副本，快照才停在改前状态。
    m_geometryUndoBefore.pages.detach();
    m_geometryEditActive = true;
}

void RuntimeInterfaceDesigner::onGeometryEditFinished()
{
    if (!m_geometryEditActive) return;
    m_geometryEditActive = false;
    if (m_geometryUndoBefore.toJson() == m_layout.toJson())
        return;                         // 只点选没挪动：不建撤销点
    m_undoStack.append(m_geometryUndoBefore);
    while (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();                // 新操作截断重做分支（与 pushUndo 同口径）
}

void RuntimeInterfaceDesigner::pushUndo()
{
    m_undoStack.append(m_layout);          // QList 值语义，整体深拷贝
    while (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();                   // 新操作截断重做分支
}

void RuntimeInterfaceDesigner::afterRestore()
{
    m_layout.ensurePage();
    m_selected = -1;
    refreshPageList();
    setCanvasPage();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::undo()
{
    if (m_undoStack.isEmpty()) return;
    m_redoStack.append(m_layout);
    m_layout = m_undoStack.takeLast();
    afterRestore();
}

void RuntimeInterfaceDesigner::redo()
{
    if (m_redoStack.isEmpty()) return;
    m_undoStack.append(m_layout);
    m_layout = m_redoStack.takeLast();
    afterRestore();
}

// ==================== 页面复制/排序 ====================

void RuntimeInterfaceDesigner::duplicatePage()
{
    RuntimeInterfacePage *src = m_layout.currentPage();
    if (!src) return;
    pushUndo();
    RuntimeInterfacePage copy = *src;
    copy.pageName = QStringLiteral("%1副本").arg(src->pageName);
    m_layout.pages.append(copy);
    m_layout.currentPageIndex = m_layout.pages.size() - 1;
    refreshPageList();
    m_selected = -1;
    setCanvasPage();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::movePage(int delta)
{
    const int n = m_layout.pages.size();
    const int target = m_layout.currentPageIndex + delta;
    if (n <= 1 || target < 0 || target >= n) return;
    m_layout.pages.swapItemsAt(m_layout.currentPageIndex, target);
    m_layout.currentPageIndex = target;
    refreshPageList();                     // currentRowChanged → onPageSelected 重挂画布
}

void RuntimeInterfaceDesigner::movePageUp()
{
    movePage(-1);
}

void RuntimeInterfaceDesigner::movePageDown()
{
    movePage(1);
}

// ==================== 批量对齐 ====================

void RuntimeInterfaceDesigner::onAlignLeft()   { m_canvas->alignSelected(RuntimeDesignerCanvas::AlignLeft); }
void RuntimeInterfaceDesigner::onAlignHCenter(){ m_canvas->alignSelected(RuntimeDesignerCanvas::AlignHCenter); }
void RuntimeInterfaceDesigner::onAlignRight()  { m_canvas->alignSelected(RuntimeDesignerCanvas::AlignRight); }
void RuntimeInterfaceDesigner::onAlignTop()    { m_canvas->alignSelected(RuntimeDesignerCanvas::AlignTop); }
void RuntimeInterfaceDesigner::onAlignVCenter(){ m_canvas->alignSelected(RuntimeDesignerCanvas::AlignVCenter); }
void RuntimeInterfaceDesigner::onAlignBottom() { m_canvas->alignSelected(RuntimeDesignerCanvas::AlignBottom); }
void RuntimeInterfaceDesigner::onAlignHSpread(){ m_canvas->alignSelected(RuntimeDesignerCanvas::AlignHSpread); }
void RuntimeInterfaceDesigner::onAlignVSpread(){ m_canvas->alignSelected(RuntimeDesignerCanvas::AlignVSpread); }

void RuntimeInterfaceDesigner::onControlSelected(int index)
{
    m_selected = index;
    refreshPropertyPanel();
}

namespace {
QString runtimeTemplateDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/runtime_templates");
}
}   // namespace

void RuntimeInterfaceDesigner::saveAsTemplate()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("存为模板"), tr("模板名称："),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QDir dir(runtimeTemplateDir());
    if (!dir.exists() && !QDir().mkpath(dir.absolutePath())) {
        QMessageBox::warning(this, tr("存为模板"),
                             tr("无法创建模板目录：%1").arg(dir.absolutePath()));
        return;
    }
    const QString path = dir.filePath(name + QStringLiteral(".json"));
    if (!m_layout.saveToFile(path)) {
        QMessageBox::warning(this, tr("存为模板"), tr("写入失败：%1").arg(path));
        return;
    }
    QMessageBox::information(this, tr("存为模板"), tr("已保存到：%1").arg(path));
}

// 官方实机（第五张截图）：按钮的「数据源」是一个「配置」按钮，点开是
// 「数据源配置」对话框——三列动作表：触发命令类型 | 方法 | 输入参数，行尾可删。
// 类型下拉：界面链接 / 参数配置 / 方案 / 执行 / 重置。
// 说明：官方各类型下的精确方法清单未取证到，方法列做成**可编辑下拉**，
// 预填现有能力（执行类沿用 start/stop/single/trigger），也允许手填。
void RuntimeInterfaceDesigner::configureButtonActions()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl || ctrl->type != RuntimeControlType::Button) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("数据源配置"));
    dlg.resize(680, 420);
    auto *lay = new QVBoxLayout(&dlg);

    auto *table = new QTableWidget(0, 3, &dlg);
    table->setHorizontalHeaderLabels({tr("触发命令类型"), tr("方法"), tr("输入参数")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(table, 1);

    const auto methodsFor = [this](const QString &type) -> QStringList {
        if (type == QLatin1String("exec"))
            return {QStringLiteral("start"), QStringLiteral("stop"),
                    QStringLiteral("single"), QStringLiteral("trigger")};
        if (type == QLatin1String("ui")) {
            QStringList pages;
            for (const RuntimeInterfacePage &p : m_layout.pages) pages << p.pageName;
            return pages;
        }
        if (type == QLatin1String("param")) {
            // 参数配置动作：方法列选**目标节点**，输入参数列填「参数键=值」
            // （官方动作表只有三列，节点信息放方法列最贴近原形态）
            return m_nodeNames;
        }
        if (type == QLatin1String("scheme"))
            return {QStringLiteral("save"), QStringLiteral("load"), QStringLiteral("switch")};
        if (type == QLatin1String("reset"))
            return {QStringLiteral("counter"), QStringLiteral("statistics")};
        return {};
    };

    const auto addRow = [&table, methodsFor](const RuntimeAction &a) {
        const int row = table->rowCount();
        table->insertRow(row);
        auto *typeCombo = new QComboBox(table);
        typeCombo->addItem(tr("界面链接"), QStringLiteral("ui"));
        typeCombo->addItem(tr("参数配置"), QStringLiteral("param"));
        typeCombo->addItem(tr("方案"), QStringLiteral("scheme"));
        typeCombo->addItem(tr("执行"), QStringLiteral("exec"));
        typeCombo->addItem(tr("重置"), QStringLiteral("reset"));
        const QString want = a.commandType.isEmpty() ? QStringLiteral("exec") : a.commandType;
        const int ti = typeCombo->findData(want);
        typeCombo->setCurrentIndex(ti >= 0 ? ti : 3);

        auto *methodCombo = new QComboBox(table);
        methodCombo->setEditable(true);
        methodCombo->addItems(methodsFor(want));
        methodCombo->setCurrentText(a.method);
        // 类型变了就把方法候选换掉，用户已填的内容保留
        QObject::connect(typeCombo, &QComboBox::currentIndexChanged, methodCombo,
                         [typeCombo, methodCombo, methodsFor]() {
                             const QString cur = methodCombo->currentText();
                             methodCombo->clear();
                             methodCombo->addItems(methodsFor(typeCombo->currentData().toString()));
                             methodCombo->setCurrentText(cur);
                         });
        table->setCellWidget(row, 0, typeCombo);
        table->setCellWidget(row, 1, methodCombo);
        table->setItem(row, 2, new QTableWidgetItem(a.param));
    };

    for (const RuntimeAction &a : ctrl->actions) addRow(a);
    if (table->rowCount() == 0) {
        RuntimeAction a;
        a.commandType = QStringLiteral("exec");
        a.method = QStringLiteral("start");
        addRow(a);
    }

    auto *btnRow = new QHBoxLayout();
    auto *addBtn = new QPushButton(tr("＋添加一行"), &dlg);
    auto *delBtn = new QPushButton(tr("－删除选中行"), &dlg);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addStretch();
    lay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked, &dlg, [&table, &addRow]() {
        RuntimeAction a;
        a.commandType = QStringLiteral("exec");
        a.method = QStringLiteral("start");
        addRow(a);
    });
    connect(delBtn, &QPushButton::clicked, &dlg, [&table]() {
        const int row = table->currentRow();
        if (row >= 0) table->removeRow(row);
    });

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    box->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    QList<RuntimeAction> collected;
    for (int row = 0; row < table->rowCount(); ++row) {
        auto *typeCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
        auto *methodCombo = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        auto *paramItem = table->item(row, 2);
        if (!typeCombo || !methodCombo) continue;
        RuntimeAction a;
        a.commandType = typeCombo->currentData().toString();
        a.method = methodCombo->currentText().trimmed();
        a.param = paramItem ? paramItem->text().trimmed() : QString();
        if (a.commandType.isEmpty()) continue;
        collected.append(a);
    }

    pushUndo();
    ctrl->actions = collected;
    // 兼顾旧字段：第一行动作同步写回 bindKey，保证旧读取路径（actionTriggered）仍可用
    if (!collected.isEmpty() && collected.first().commandType == QLatin1String("exec")) {
        ctrl->bindType = QStringLiteral("action");
        ctrl->bindKey = collected.first().method;
    }
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::applyTemplate()
{
    QDir dir(runtimeTemplateDir());
    const QStringList files =
        dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("应用模板"),
                                 tr("模板库为空：%1").arg(dir.absolutePath()));
        return;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, tr("应用模板"), tr("选择模板："),
                                                 files, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;
    RuntimeInterface loaded;
    if (!loaded.loadFromFile(dir.filePath(chosen))) {
        QMessageBox::warning(this, tr("应用模板"), tr("模板读取失败：%1").arg(chosen));
        return;
    }
    pushUndo();
    m_layout = loaded;
    m_layout.ensurePage();
    m_selected = -1;
    setCanvasPage();
    refreshPageList();
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::populateBindKeyCombo()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) { m_bindKeyCombo->clear(); return; }

    // 参数设置固定绑节点（绑定方式行被隐藏，combo 里可能没有值）
    const QString bindType = (ctrl->type == RuntimeControlType::ParamSetting
                              || ctrl->type == RuntimeControlType::SwitchControl
                              || ctrl->type == RuntimeControlType::ComboBox)
                                 ? QStringLiteral("node")
                                 : m_bindTypeCombo->currentData().toString();
    m_bindKeyCombo->clear();
    m_bindKeyCombo->addItem(QStringLiteral("(无)"), QString());

    if (ctrl->type == RuntimeControlType::Button) {
        // 动作绑定
        m_bindKeyCombo->addItem(QStringLiteral("开始执行"), QStringLiteral("start"));
        m_bindKeyCombo->addItem(QStringLiteral("停止执行"), QStringLiteral("stop"));
        m_bindKeyCombo->addItem(QStringLiteral("单次执行"), QStringLiteral("single"));
        m_bindKeyCombo->addItem(QStringLiteral("触发流程"), QStringLiteral("trigger"));
        int idx = m_bindKeyCombo->findData(ctrl->bindKey);
        if (idx < 0 && !ctrl->bindKey.isEmpty()) {
            // 同上：动作绑定失效（触发流程被删）也不许静默丢
            m_bindKeyCombo->addItem(QStringLiteral("（已失效：%1）").arg(ctrl->bindKey),
                                    ctrl->bindKey);
            idx = m_bindKeyCombo->count() - 1;
        }
        if (idx < 0 && ctrl->bindKey.isEmpty())
            idx = m_bindKeyCombo->findData(QStringLiteral("start"));
        m_bindKeyCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        return;
    }

    if (bindType == QStringLiteral("global")) {
        const auto vars = GlobalVariableManager::instance()->variables();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it) {
            m_bindKeyCombo->addItem(it.key(), it.key());
        }
    } else if (bindType == QStringLiteral("node")) {
        for (const QString &n : m_nodeNames) {
            m_bindKeyCombo->addItem(n, n);
        }
    }
    int idx = m_bindKeyCombo->findData(ctrl->bindKey);
    if (idx < 0 && !ctrl->bindKey.isEmpty()) {
        // 绑定已失效（全局变量/节点被删）：显式保留为"(已失效)"，不让面板刷新把它静默清掉
        // （2026-10-27）：原逻辑 idx<0 → 选"(无)"，currentIndexChanged 会把空值写回控件
        // ——用户一刷新属性面板，绑定就没了，还查不出为什么。
        m_bindKeyCombo->addItem(QStringLiteral("（已失效：%1）").arg(ctrl->bindKey),
                                ctrl->bindKey);
        idx = m_bindKeyCombo->count() - 1;
    }
    m_bindKeyCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void RuntimeInterfaceDesigner::refreshPropertyPanel()
{
    m_updatingProps = true;
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) {
        m_titleEdit->setEnabled(false);
        m_colorEdit->setEnabled(false);
        m_fontSpin->setEnabled(false);
        m_visibleCheck->setEnabled(false);
        for (QWidget *w : {static_cast<QWidget *>(m_boldCheck),
                           static_cast<QWidget *>(m_bgColorEdit),
                           static_cast<QWidget *>(m_borderColorEdit),
                           static_cast<QWidget *>(m_pressedColorEdit)}) {
            if (w) w->setEnabled(false);
        }
        m_bindTypeCombo->setEnabled(false);
        m_bindKeyCombo->setEnabled(false);
        m_columnSource->setEnabled(false);
        m_columnList->setEnabled(false);
        m_columnAddBtn->setEnabled(false);
        m_columnDelBtn->setEnabled(false);
        m_tableMaxRows->setEnabled(false);
        for (QWidget *w : {static_cast<QWidget *>(m_chartGroup),
                           static_cast<QWidget *>(m_chartTitleEdit),
                           static_cast<QWidget *>(m_chartShowTitleCheck),
                           static_cast<QWidget *>(m_chartHistorySpin),
                           static_cast<QWidget *>(m_chartBgEdit),
                           static_cast<QWidget *>(m_chartAxisEdit),
                           static_cast<QWidget *>(m_chartSource),
                           static_cast<QWidget *>(m_chartSeriesList),
                           static_cast<QWidget *>(m_chartSeriesAddBtn),
                           static_cast<QWidget *>(m_chartSeriesDelBtn)}) {
            if (w) w->setEnabled(false);
        }
        if (m_chartGroup) m_chartGroup->setVisible(false);
        m_raiseBtn->setEnabled(false);
        m_lowerBtn->setEnabled(false);
        m_typeLabel->setText(QStringLiteral("-"));
        if (m_geometryLabel) m_geometryLabel->setText(QStringLiteral("-"));
        m_updatingProps = false;
        return;
        }

    m_titleEdit->setEnabled(true);
    m_colorEdit->setEnabled(true);
    m_fontSpin->setEnabled(true);
    m_visibleCheck->setEnabled(true);
    m_visibleCheck->setChecked(ctrl->visible);

    // 层级按钮：首控件不能下移、末控件不能上移
    RuntimeInterfacePage *pg = m_layout.currentPage();
    const int count = pg ? pg->controls.size() : 0;
    m_raiseBtn->setEnabled(m_selected >= 0 && m_selected < count - 1);
    m_lowerBtn->setEnabled(m_selected > 0 && m_selected < count);

    m_typeLabel->setText(runtimeControlTypeName(ctrl->type));
    if (m_geometryLabel) {
        const QRect g = ctrl->geometry;
        m_geometryLabel->setText(QStringLiteral("%1 × %2  @ (%3, %4)")
                                     .arg(g.width()).arg(g.height())
                                     .arg(g.x()).arg(g.y()));
    }
    m_titleEdit->setText(ctrl->title);
    m_colorEdit->setText(ctrl->color);
    m_fontSpin->setValue(ctrl->fontSize);
    if (m_boldCheck) m_boldCheck->setChecked(ctrl->bold);
    if (m_bgColorEdit) m_bgColorEdit->setText(ctrl->bgColor);
    if (m_borderColorEdit) m_borderColorEdit->setText(ctrl->borderColor);
    if (m_pressedColorEdit) m_pressedColorEdit->setText(ctrl->pressedColor);

    // 官方：按钮的数据源只有「配置」按钮；其它控件保持绑定方式/对象两行
    const bool isButton = (ctrl->type == RuntimeControlType::Button);
    const bool isParam = (ctrl->type == RuntimeControlType::ParamSetting);
    const bool isSwitch = (ctrl->type == RuntimeControlType::SwitchControl);
    const bool isCombo = (ctrl->type == RuntimeControlType::ComboBox);
    if (m_dataForm && m_actionConfigBtn) {
        m_dataForm->setRowVisible(m_actionConfigBtn, isButton);
        // 参数设置/开关控制/组合框固定绑节点，只藏「绑定方式」，仍需「绑定对象」选节点
        m_dataForm->setRowVisible(m_bindTypeCombo,
                                  !isButton && !isParam && !isSwitch && !isCombo);
        m_dataForm->setRowVisible(m_bindKeyCombo, !isButton);
        if (m_paramKeyEdit)
            m_dataForm->setRowVisible(m_paramKeyEdit, isParam || isSwitch || isCombo);
        // bool 参数没有取值范围；组合框改填候选值
        if (m_paramRangeHost) m_dataForm->setRowVisible(m_paramRangeHost, isParam);
        if (m_comboOptionsEdit) m_dataForm->setRowVisible(m_comboOptionsEdit, isCombo);
    }
    if (m_comboOptionsEdit)
        m_comboOptionsEdit->setText(ctrl->comboOptions.join(QStringLiteral(",")));

    // 图片 / 世界时钟（官方 9.3）：只在该类型时显示对应行
    const bool isImage = (ctrl->type == RuntimeControlType::ImageAsset);
    const bool isClock = (ctrl->type == RuntimeControlType::WorldClock);
    if (m_imageHost) m_imageHost->setVisible(isImage);
    if (m_tzHost) m_tzHost->setVisible(isClock);
    if (m_imagePathEdit) m_imagePathEdit->setText(ctrl->imagePath);
    if (m_tzOffsetSpin) m_tzOffsetSpin->setValue(ctrl->timeZoneOffsetMinutes);
    if (m_zoneCombo) {
        const int zi = m_zoneCombo->findData(int(ctrl->zone));
        m_zoneCombo->setCurrentIndex(zi >= 0 ? zi : 0);
    }

    // 内层 Tab：当前页未启用时整行隐藏；启用时列「不在 Tab」+ 各标签
    if (m_innerTabCombo) {
        const bool hasTabs = pg && !pg->innerTabNames.isEmpty();
        m_innerTabCombo->setVisible(hasTabs);
        if (m_innerTabLabel) m_innerTabLabel->setVisible(hasTabs);
        if (hasTabs) {
            m_innerTabCombo->clear();
            m_innerTabCombo->addItem(QStringLiteral("不在 Tab 内"), -1);
            for (int i = 0; i < pg->innerTabNames.size(); ++i)
                m_innerTabCombo->addItem(pg->innerTabNames.at(i), i);
            const int idx = m_innerTabCombo->findData(ctrl->innerTab);
            m_innerTabCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
    }
    if (m_paramKeyEdit) m_paramKeyEdit->setText(ctrl->paramKey);
    if (m_paramMinSpin) m_paramMinSpin->setValue(ctrl->paramMin);
    if (m_paramMaxSpin) m_paramMaxSpin->setValue(ctrl->paramMax);

    const bool isTable = (ctrl->type == RuntimeControlType::ResultTable);
    const bool isIo = (ctrl->type == RuntimeControlType::IoStatus);
    m_columnSource->setEnabled(isTable);
    m_columnList->setEnabled(isTable);
    m_columnAddBtn->setEnabled(isTable);
    m_columnDelBtn->setEnabled(isTable);
    m_tableMaxRows->setEnabled(isTable);
    if (isTable) {
        m_columnList->clear();
        for (const ResultColumn &col : ctrl->columns) {
            const QString tag = (col.bindType == QStringLiteral("node"))
                                    ? QStringLiteral("节点:%1").arg(col.bindKey)
                                    : QStringLiteral("变量:%1").arg(col.bindKey);
            m_columnList->addItem(QStringLiteral("%1  ←  %2").arg(col.header, tag));
        }
        // 新列来源：全局变量 + 节点输出（"节点:" 前缀）
        m_columnSource->clear();
        const auto vars = GlobalVariableManager::instance()->variables();
        for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
            m_columnSource->addItem(it.key());
        for (const QString &n : m_nodeNames)
            m_columnSource->addItem(QStringLiteral("节点:%1").arg(n));
        m_tableMaxRows->setValue(ctrl->maxRows);
    }

    // 线图阵列：分组只在选中该类型时出现；数据系列来源 = 全局变量 + 节点输出
    if (m_chartGroup) {
        const bool isChart = ctrl->type == RuntimeControlType::LineChart
                             || ctrl->type == RuntimeControlType::MultiLineChart
                             || ctrl->type == RuntimeControlType::PieChart;
        m_chartGroup->setVisible(isChart);
        m_chartGroup->setEnabled(isChart);
        if (isChart) {
            m_chartTitleEdit->setText(ctrl->chartTitle);
            m_chartShowTitleCheck->setChecked(ctrl->showChartTitle);
            m_chartHistorySpin->setValue(ctrl->chartHistoryLimit);
            m_chartBgEdit->setText(ctrl->chartBgColor);
            m_chartAxisEdit->setText(ctrl->chartAxisColor);
            m_chartSource->clear();
            const auto vars = GlobalVariableManager::instance()->variables();
            for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
                m_chartSource->addItem(it.key());
            for (const QString &nn : m_nodeNames)
                m_chartSource->addItem(QStringLiteral("节点:%1").arg(nn));
            m_chartSeriesList->clear();
            for (const ChartSeries &cs : ctrl->chartSeries) {
                const QString tag = cs.bindType == QStringLiteral("node")
                                        ? QStringLiteral("节点:%1").arg(cs.bindKey)
                                        : QStringLiteral("变量:%1").arg(cs.bindKey);
                m_chartSeriesList->addItem(QStringLiteral("%1  ←  %2").arg(cs.name, tag));
            }
        }
    }

    // 绑定方式选项
    m_bindTypeCombo->clear();
    if (ctrl->type == RuntimeControlType::Button) {
        m_bindTypeCombo->setEnabled(true);
        m_bindKeyCombo->setEnabled(true);
        m_bindTypeCombo->addItem(QStringLiteral("动作"), QStringLiteral("action"));
    } else if (ctrl->type == RuntimeControlType::ImageView || isIo) {
        m_bindTypeCombo->setEnabled(true);
        m_bindKeyCombo->setEnabled(true);
        m_bindTypeCombo->addItem(QStringLiteral("节点输出"), QStringLiteral("node"));
    } else if (ctrl->type == RuntimeControlType::ValueDisplay ||
               ctrl->type == RuntimeControlType::StatusLight) {
        m_bindTypeCombo->setEnabled(true);
        m_bindKeyCombo->setEnabled(true);
        m_bindTypeCombo->addItem(QStringLiteral("全局变量"), QStringLiteral("global"));
        m_bindTypeCombo->addItem(QStringLiteral("节点输出"), QStringLiteral("node"));
    } else if (isTable) {
        // 表格用列配置，不用整体绑定
        m_bindTypeCombo->setEnabled(false);
        m_bindKeyCombo->setEnabled(false);
        m_bindTypeCombo->addItem(QStringLiteral("(列配置)"), QString());
    } else {
        // TextLabel：仅文本
        m_bindTypeCombo->setEnabled(false);
        m_bindKeyCombo->setEnabled(false);
    }

    int idx = m_bindTypeCombo->findData(ctrl->bindType);
    m_bindTypeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    populateBindKeyCombo();

    m_updatingProps = false;
}

void RuntimeInterfaceDesigner::onPropertyEdited()
{
    if (m_updatingProps) return;
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) return;

    // 属性编辑也进撤销栈（此前只有结构性操作可撤销）。600ms 内的连续输入
    // 合并为一个撤销点，避免拖字号/改颜色时把栈冲满。
    if (m_propUndoTimer && !m_propUndoTimer->isActive()) {
        pushUndo();          // 此时 ctrl 尚未被下面的代码改写，快照是「改之前」
        m_propUndoTimer->start();
    }

    ctrl->title = m_titleEdit->text().trimmed();
    ctrl->color = m_colorEdit->text().trimmed().isEmpty()
                      ? QStringLiteral("#3a6ea5") : m_colorEdit->text().trimmed();
    ctrl->fontSize = m_fontSpin->value();
    // 官方外观分组（2026-11-09）：字形加粗 + 背景 / 边框 / 点击色
    ctrl->bold = m_boldCheck && m_boldCheck->isChecked();
    ctrl->bgColor = m_bgColorEdit ? m_bgColorEdit->text().trimmed() : QString();
    ctrl->borderColor = m_borderColorEdit ? m_borderColorEdit->text().trimmed() : QString();
    ctrl->pressedColor = m_pressedColorEdit ? m_pressedColorEdit->text().trimmed() : QString();
    ctrl->visible = m_visibleCheck->isChecked();
    // 所属区域对所有控件生效
    if (m_zoneCombo) {
        const int z = m_zoneCombo->currentData().toInt();
        if (z >= 0 && z <= int(RuntimeZone::RightBar) && z != int(ctrl->zone)) {
            // 换区时按「画布绝对位置不变」重映射存储坐标，控件不在画布上跳位
            const QRect canvasRect = m_canvas ? m_canvas->toCanvasRect(*ctrl) : ctrl->geometry;
            ctrl->zone = RuntimeZone(z);
            if (m_canvas) ctrl->geometry = m_canvas->toStoredRect(*ctrl, canvasRect);
        }
    }
    if (m_innerTabCombo && m_innerTabCombo->isVisible())
        ctrl->innerTab = m_innerTabCombo->currentData().toInt();
    if ((ctrl->type == RuntimeControlType::LineChart
         || ctrl->type == RuntimeControlType::MultiLineChart
         || ctrl->type == RuntimeControlType::PieChart) && m_chartGroup) {
        ctrl->chartTitle = m_chartTitleEdit ? m_chartTitleEdit->text() : QString();
        if (m_chartShowTitleCheck) ctrl->showChartTitle = m_chartShowTitleCheck->isChecked();
        if (m_chartHistorySpin) ctrl->chartHistoryLimit = m_chartHistorySpin->value();
        if (m_chartBgEdit && !m_chartBgEdit->text().trimmed().isEmpty())
            ctrl->chartBgColor = m_chartBgEdit->text().trimmed();
        if (m_chartAxisEdit && !m_chartAxisEdit->text().trimmed().isEmpty())
            ctrl->chartAxisColor = m_chartAxisEdit->text().trimmed();
    }

    if (ctrl->type == RuntimeControlType::ParamSetting) {
        ctrl->bindType = QStringLiteral("node");   // 参数设置固定绑节点
        ctrl->bindKey = m_bindKeyCombo->currentData().toString();
        if (m_paramKeyEdit) ctrl->paramKey = m_paramKeyEdit->text().trimmed();
        if (m_paramMinSpin) ctrl->paramMin = m_paramMinSpin->value();
        if (m_paramMaxSpin) ctrl->paramMax = m_paramMaxSpin->value();
        if (ctrl->paramMax <= ctrl->paramMin) ctrl->paramMax = ctrl->paramMin + 1.0;
    } else if (ctrl->type == RuntimeControlType::SwitchControl) {
        ctrl->bindType = QStringLiteral("node");   // 开关控制同样固定绑节点
        ctrl->bindKey = m_bindKeyCombo->currentData().toString();
        if (m_paramKeyEdit) ctrl->paramKey = m_paramKeyEdit->text().trimmed();
    } else if (ctrl->type == RuntimeControlType::ImageAsset) {
        if (m_imagePathEdit) ctrl->imagePath = m_imagePathEdit->text().trimmed();
    } else if (ctrl->type == RuntimeControlType::WorldClock) {
        if (m_tzOffsetSpin) ctrl->timeZoneOffsetMinutes = m_tzOffsetSpin->value();
    } else if (ctrl->type == RuntimeControlType::ComboBox) {
        ctrl->bindType = QStringLiteral("node");
        ctrl->bindKey = m_bindKeyCombo->currentData().toString();
        if (m_paramKeyEdit) ctrl->paramKey = m_paramKeyEdit->text().trimmed();
        if (m_comboOptionsEdit) {
            ctrl->comboOptions.clear();
            const QStringList raw = m_comboOptionsEdit->text().split(
                QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &s : raw) {
                const QString t = s.trimmed();
                if (!t.isEmpty()) ctrl->comboOptions << t;
            }
        }
    }

    if (ctrl->type == RuntimeControlType::ResultTable) {
        // 列由"＋添加列/－删除列"维护（见 onColumnAdd/onColumnDel），此处只同步行数
        ctrl->maxRows = m_tableMaxRows->value();
        m_canvas->refreshControl(m_selected);
        return;
    }

    if (ctrl->type != RuntimeControlType::Button) {
        ctrl->bindType = m_bindTypeCombo->currentData().toString();
        ctrl->bindKey = m_bindKeyCombo->currentData().toString();
    } else {
        ctrl->bindType = QStringLiteral("action");
        ctrl->bindKey = m_bindKeyCombo->currentData().toString();
    }

    m_canvas->refreshControl(m_selected);
}

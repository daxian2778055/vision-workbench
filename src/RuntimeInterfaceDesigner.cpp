#include "RuntimeInterfaceDesigner.h"
#include "RuntimeInterfaceView.h"
#include "GlobalVariableManager.h"
#include "FocusGuard.h"

#include <QListWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
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
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDragEnterEvent>
#include <QDropEvent>

namespace {
/// 控件库拖放用的 Qt 内建 item-model MIME（QListWidget 开启 drag 后自带）
const char *kItemModelMime = "application/x-qabstractitemmodeldatalist";
/// 剪贴板里放单个控件的序列化 JSON（官方 9.1 工具条注：复制粘贴仅 Ctrl+C / Ctrl+V）
const char *kControlMime = "application/x-vfp-runtime-control";
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
        frame->setGeometry(m_layout->controls[i].geometry);
        frame->show();
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
            m_layout->controls[idx[k]].geometry = g;
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
    m_frames[index]->setGeometry(m_layout->controls[index].geometry);
    m_frames[index]->update();
}

void RuntimeDesignerCanvas::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter p(this);
    p.fillRect(rect(), QColor(0x1a, 0x1a, 0x20));

    // 网格背景
    p.setPen(QPen(QColor(0x28, 0x28, 0x32), 1));
    const int grid = 20;
    for (int x = 0; x <= width(); x += grid) {
        p.drawLine(x, 0, x, height());
    }
    for (int y = 0; y <= height(); y += grid) {
        p.drawLine(0, y, width(), y);
    }

    // 页面提示
    p.setPen(QColor(0x60, 0x64, 0x72));
    p.drawText(10, height() - 10, QStringLiteral("画布 1200×760 · 拖动控件移动，右下角缩放"));
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
                m_canvas->m_layout->controls[i].geometry = m_canvas->m_frames[i]->geometry();
        }
    }
    event->accept();
}

void DesignerControlFrame::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_resizing = false;
        event->accept();
    }
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
    QRect g = geometry().translated(dx, dy);
    g.moveLeft(qMax(0, g.left()));
    g.moveTop(qMax(0, g.top()));
    setGeometry(g);
    if (m_canvas->m_layout && m_index >= 0 && m_index < m_canvas->m_layout->controls.size())
        m_canvas->m_layout->controls[m_index].geometry = g;
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
    const QStringList types = {
        QStringLiteral("图像显示"), QStringLiteral("数值显示"),
        QStringLiteral("文本标签"), QStringLiteral("状态灯"), QStringLiteral("按钮"),
        QStringLiteral("结果表格"), QStringLiteral("IO状态")
    };
    m_palette->addItems(types);
    m_palette->setCurrentRow(0);
    m_palette->setMaximumWidth(160);
    // 官方 9.2.2 的拖放添加：保留原有「添加控件按钮 / 双击」两条路径，另开拖拽。
    m_palette->setDragEnabled(true);
    m_palette->setDragDropMode(QAbstractItemView::DragOnly);
    for (int i = 0; i < m_palette->count(); ++i) {
    QListWidgetItem *it = m_palette->item(i);
    it->setFlags(it->flags() | Qt::ItemIsDragEnabled);
    }

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
    m_canvas->setPage(m_layout.currentPage());
    connect(m_canvas, &RuntimeDesignerCanvas::paletteDropped,
    this, &RuntimeInterfaceDesigner::addControlAt);
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

    auto *canvasLay = new QVBoxLayout(canvasGroup);
    canvasLay->addWidget(m_canvasScroll, 1);
    canvasLay->addLayout(alignRow);
    canvasLay->addLayout(canvasToolRow);

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

    fData->addRow(QStringLiteral("绑定方式"), m_bindTypeCombo);
    fData->addRow(QStringLiteral("绑定对象"), m_bindKeyCombo);
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

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(previewBtn);
    btnRow->addWidget(loadBtn);
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
    m_canvas->setPage(m_layout.currentPage());
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
    m_canvas->setPage(m_layout.currentPage());
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
    m_canvas->setPage(m_layout.currentPage());
    refreshPropertyPanel();
}

void RuntimeInterfaceDesigner::addCurrentPaletteControl()
{
addControlByRow(m_palette ? m_palette->currentRow() : 0);
}

void RuntimeInterfaceDesigner::addControlAt(const QPoint &pos)
{
addControlByRow(m_palette ? m_palette->currentRow() : 0, pos);
}

void RuntimeInterfaceDesigner::addControlByRow(int row, const QPoint &dropPos)
{
RuntimeControlType type;
switch (row) {
case 0: type = RuntimeControlType::ImageView; break;
case 1: type = RuntimeControlType::ValueDisplay; break;
case 2: type = RuntimeControlType::TextLabel; break;
case 3: type = RuntimeControlType::StatusLight; break;
case 4: type = RuntimeControlType::Button; break;
case 5: type = RuntimeControlType::ResultTable; break;
case 6: type = RuntimeControlType::IoStatus; break;
default: return;
}

const int n = m_layout.currentPage()->controls.size();
const int cascade = (n % 8) * 24;
QRect geo(40 + cascade, 40 + cascade, 300, 190);
if (type == RuntimeControlType::StatusLight) geo.setHeight(70);
if (type == RuntimeControlType::Button)     { geo.setWidth(160); geo.setHeight(48); }
if (type == RuntimeControlType::TextLabel)  { geo.setWidth(240); geo.setHeight(60); }
if (type == RuntimeControlType::ValueDisplay){ geo.setHeight(90); }
if (type == RuntimeControlType::ResultTable){ geo.setWidth(460); geo.setHeight(260); }
if (type == RuntimeControlType::IoStatus)   { geo.setWidth(260); geo.setHeight(80); }

// 拖放：控件左上角落在松手位置（略作钳制，别让控件跑到画布外）。
if (!dropPos.isNull()) {
const QSize canvasSize = m_canvas ? m_canvas->size() : QSize(1200, 760);
geo.moveLeft(qBound(0, dropPos.x(), qMax(0, canvasSize.width() - geo.width())));
geo.moveTop(qBound(0, dropPos.y(), qMax(0, canvasSize.height() - geo.height())));
}

pushUndo();
RuntimeControl *ctrl = m_layout.currentPage()->addControl(type, geo);
m_canvas->rebuild();
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
m_canvas->rebuild();
m_canvas->selectIndex(m_layout.currentPage()->controls.size() - 1);
}

void RuntimeInterfaceDesigner::deleteSelected()
{
    if (m_selected < 0) return;
    pushUndo();
    m_layout.currentPage()->removeControl(m_selected);
    m_selected = -1;
    m_canvas->rebuild();
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
    m_canvas->rebuild();
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
        m_canvas->setPage(m_layout.currentPage());
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

void RuntimeInterfaceDesigner::duplicateSelected()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) return;
    RuntimeControl copy = *ctrl;
    copy.geometry = copy.geometry.translated(24, 24);   // 级联偏移避免完全重叠
    pushUndo();
    m_layout.currentPage()->controls.append(copy);
    m_canvas->rebuild();
    m_canvas->selectIndex(m_layout.currentPage()->controls.size() - 1);
}

void RuntimeInterfaceDesigner::raiseSelected()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected < 0 || m_selected >= pg->controls.size() - 1) return;
    pushUndo();
    pg->controls.swapItemsAt(m_selected, m_selected + 1);
    ++m_selected;
    m_canvas->rebuild();
    m_canvas->selectIndex(m_selected);
}

void RuntimeInterfaceDesigner::lowerSelected()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected <= 0 || m_selected >= pg->controls.size()) return;
    pushUndo();
    pg->controls.swapItemsAt(m_selected, m_selected - 1);
    --m_selected;
    m_canvas->rebuild();
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
    m_canvas->rebuild();
    m_canvas->selectIndex(m_selected);
}

void RuntimeInterfaceDesigner::lowerToBottom()
{
    RuntimeInterfacePage *pg = m_layout.currentPage();
    if (!pg || m_selected <= 0 || m_selected >= pg->controls.size()) return;
    pushUndo();
    pg->controls.prepend(pg->controls.takeAt(m_selected));
    m_selected = 0;
    m_canvas->rebuild();
    m_canvas->selectIndex(m_selected);
}

// ==================== 撤销/重做 ====================

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
    m_canvas->setPage(m_layout.currentPage());
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
    m_canvas->setPage(m_layout.currentPage());
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

void RuntimeInterfaceDesigner::populateBindKeyCombo()
{
    RuntimeControl *ctrl = selectedControl();
    if (!ctrl) { m_bindKeyCombo->clear(); return; }

    const QString bindType = m_bindTypeCombo->currentData().toString();
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

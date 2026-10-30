#include "BoxAnnotatorDialog.h"
#include "ConfirmDestructive.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

// ---------------- Canvas ----------------

BoxAnnotationCanvas::BoxAnnotationCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(480, 360);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void BoxAnnotationCanvas::setImage(const QImage &img)
{
    m_image = img;
    m_selected = -1;
    update();
}

void BoxAnnotationCanvas::setBoxes(const QVector<YoloBoxLabel> &boxes, const QStringList &classNames)
{
    m_boxes = boxes;
    m_classNames = classNames;
    update();
}

QRectF BoxAnnotationCanvas::displayRect() const
{
    if (m_image.isNull())
        return QRectF();
    const QSize target = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const double x = (width() - target.width()) / 2.0;
    const double y = (height() - target.height()) / 2.0;
    return QRectF(x, y, target.width(), target.height());
}

QPointF BoxAnnotationCanvas::toImage(const QPointF &widgetPos) const
{
    const QRectF d = displayRect();
    if (d.isEmpty())
        return QPointF();
    const double sx = m_image.width() / d.width();
    const double sy = m_image.height() / d.height();
    return QPointF((widgetPos.x() - d.left()) * sx, (widgetPos.y() - d.top()) * sy);
}

void BoxAnnotationCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));
    if (m_image.isNull())
        return;
    const QRectF d = displayRect();
    p.drawImage(d, m_image);

    const double sx = d.width() / m_image.width();
    const double sy = d.height() / m_image.height();
    for (int i = 0; i < m_boxes.size(); ++i) {
        const YoloBoxLabel &b = m_boxes[i];
        const QRectF r(d.left() + b.x * m_image.width() * sx,
                       d.top() + b.y * m_image.height() * sy,
                       b.w * m_image.width() * sx, b.h * m_image.height() * sy);
        const bool sel = (i == m_selected);
        p.setPen(QPen(sel ? QColor(255, 200, 0) : QColor(0, 220, 90), sel ? 3 : 2));
        p.drawRect(r);
        const QString name = (b.classId >= 0 && b.classId < m_classNames.size())
                                 ? m_classNames[b.classId]
                                 : QString::number(b.classId);
        p.drawText(r.adjusted(2, -16, 0, 0), Qt::AlignLeft | Qt::AlignBottom, name);
    }
    if (m_dragging) {
        p.setPen(QPen(QColor(255, 120, 0), 2, Qt::DashLine));
        p.drawRect(QRectF(m_dragStart, m_dragCur).normalized());
    }
}

void BoxAnnotationCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_image.isNull())
        return;
    // 命中已有框 → 选中（便于删除）；否则开始拖新框
    const QPointF imgPt = toImage(e->pos());
    int hit = -1;
    for (int i = m_boxes.size() - 1; i >= 0; --i) {
        const QRectF r = m_boxes[i].toPixels(m_image.width(), m_image.height());
        if (r.contains(imgPt)) {
            hit = i;
            break;
        }
    }
    if (hit >= 0 && !m_boxes.isEmpty()) {
        m_selected = hit;
        emit boxSelected(hit);
        update();
        return;
    }
    m_dragging = true;
    m_dragStart = e->pos();
    m_dragCur = e->pos();
}

void BoxAnnotationCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging)
        return;
    m_dragCur = e->pos();
    update();
}

void BoxAnnotationCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging) {
        return;
    }
    m_dragging = false;
    const QRectF widgetRect = QRectF(m_dragStart, e->pos()).normalized();
    if (widgetRect.width() < 4 || widgetRect.height() < 4) {
        m_selected = -1;
        emit boxSelected(-1);
        update();
        return;
    }
    const QPointF a = toImage(widgetRect.topLeft());
    const QPointF b = toImage(widgetRect.bottomRight());
    emit boxDrawn(QRectF(a, b).normalized(), (e->modifiers() & Qt::ShiftModifier) != 0);
    update();
}

// ---------------- Dialog ----------------

BoxAnnotatorDialog::BoxAnnotatorDialog(QWidget *parent) : BoxAnnotatorDialog(QString(), parent)
{
}

BoxAnnotatorDialog::BoxAnnotatorDialog(const QString &dir, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("检测框标注"));
    resize(1180, 720);

    auto *root = new QVBoxLayout(this);

    auto *dirRow = new QHBoxLayout();
    m_dirLabel = new QLabel(dir.isEmpty() ? QStringLiteral("（未选择数据集目录）") : dir);
    dirRow->addWidget(m_dirLabel, 1);
    auto *chooseBtn = new QPushButton(QStringLiteral("选择数据集目录…"));
    connect(chooseBtn, &QPushButton::clicked, this, &BoxAnnotatorDialog::onChooseDir);
    dirRow->addWidget(chooseBtn);
    root->addLayout(dirRow);

    auto *split = new QSplitter(Qt::Horizontal);
    m_imageList = new QListWidget();
    m_imageList->setMinimumWidth(220);
    connect(m_imageList, &QListWidget::currentRowChanged, this, &BoxAnnotatorDialog::onImageChanged);
    split->addWidget(m_imageList);

    m_canvas = new BoxAnnotationCanvas();
    connect(m_canvas, &BoxAnnotationCanvas::boxDrawn, this, &BoxAnnotatorDialog::onBoxDrawn);
    connect(m_canvas, &BoxAnnotationCanvas::boxSelected, this, &BoxAnnotatorDialog::onBoxSelected);
    split->addWidget(m_canvas);

    auto *right = new QWidget();
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(new QLabel(QStringLiteral("类别")));
    m_classCombo = new QComboBox();
    rightLayout->addWidget(m_classCombo);
    rightLayout->addWidget(new QLabel(QStringLiteral("本图标注")));
    m_boxList = new QListWidget();
    connect(m_boxList, &QListWidget::currentRowChanged, this, &BoxAnnotatorDialog::onBoxSelected);
    rightLayout->addWidget(m_boxList, 1);
    m_deleteBtn = new QPushButton(QStringLiteral("删除所选框"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &BoxAnnotatorDialog::onDeleteBox);
    rightLayout->addWidget(m_deleteBtn);
    auto *saveBtn = new QPushButton(QStringLiteral("保存本图"));
    connect(saveBtn, &QPushButton::clicked, this, &BoxAnnotatorDialog::saveCurrentImage);
    rightLayout->addWidget(saveBtn);
    m_clearBtn = new QPushButton(QStringLiteral("清空本图"));
    connect(m_clearBtn, &QPushButton::clicked, this, &BoxAnnotatorDialog::onClearImage);
    rightLayout->addWidget(m_clearBtn);
    right->setMinimumWidth(240);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    root->addWidget(split, 1);

    m_status = new QLabel(QStringLiteral("拖拽画框；Shift+拖动可画到图外（自动裁剪）；切换图片自动保存"));
    root->addWidget(m_status);

    if (!dir.isEmpty()) {
        m_dir = dir;
        reloadImages();
    }
}

void BoxAnnotatorDialog::onChooseDir()
{
    const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("选择数据集目录"));
    if (d.isEmpty())
        return;
    if (!saveCurrentImage())
        return;   // 存不上就别换目录（2026-10-29 r15 P2-12：换目录会丢掉当前框）
    m_dir = d;
    m_dirLabel->setText(d);
    reloadImages();
}

void BoxAnnotatorDialog::reloadImages()
{
    m_loading = true;
    m_imageList->clear();
    m_images.clear();
    m_classNames = YoloAnnotation::loadClasses(m_dir);
    m_classCombo->clear();
    if (m_classNames.isEmpty()) {
        // 没 classes.txt：给个占位，提示去补
        m_status->setText(QStringLiteral("目录下没有 classes.txt：先建一个（每行一个类名），或直接在框上用编号"));
    }
    for (const QString &n : m_classNames)
        m_classCombo->addItem(n);

    const QDir dir(m_dir);
    const QStringList filters{QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                              QStringLiteral("*.jpeg"), QStringLiteral("*.bmp")};
    m_images = dir.entryList(filters, QDir::Files, QDir::Name);
    for (const QString &name : m_images)
        m_imageList->addItem(name);
    // 清掉"当前归属"（2026-10-29 r15 P1-9）：新列表的第 0 行不是刚才那张，
    // 保留旧 m_boxes/m_currentPath 会让 setCurrentRow(0) 把旧数据集的框写进新目录第一张。
    m_boxes.clear();
    m_currentPath.clear();
    m_lastRow = -1;
    m_loading = false;
    if (!m_images.isEmpty())
        m_imageList->setCurrentRow(0);
}

void BoxAnnotatorDialog::closeEvent(QCloseEvent *e)
{
    // 标完当前图直接点 X 是最常见用法——这一张的框必须存（2026-10-28 P2-1）。
    // 存失败时不能装作存上了（2026-10-29 复核 P2-12）：问一句，默认"取消"留在窗口里。
    if (!saveCurrentImage()
        && !confirmDestructive(this, QStringLiteral("标注保存失败"),
                               QStringLiteral("本图标注没能写入（磁盘满或权限？）。\n"
                                              "仍要关闭会丢失当前这张图上的框。"))) {
        e->ignore();
        return;
    }
    QDialog::closeEvent(e);
}

void BoxAnnotatorDialog::reject()
{
    // Esc 走这里，与关窗同一策略
    if (!saveCurrentImage()
        && !confirmDestructive(this, QStringLiteral("标注保存失败"),
                               QStringLiteral("本图标注没能写入（磁盘满或权限？）。\n"
                                              "仍要关闭会丢失当前这张图上的框。"))) {
        return;
    }
    QDialog::reject();
}

QString BoxAnnotatorDialog::currentImagePath() const
{
    const int row = m_imageList ? m_imageList->currentRow() : -1;
    if (row < 0 || row >= m_images.size())
        return QString();
    return QDir(m_dir).filePath(m_images[row]);
}

void BoxAnnotatorDialog::onImageChanged(int row)
{
    if (m_loading)
        return;
    // 切换即存；存不上就**不换图**（P2-12）。注意保存目标由 m_currentPath 决定（P1-9）。
    if (!saveCurrentImage()) {
        m_loading = true;
        if (m_lastRow >= 0 && m_lastRow < m_imageList->count())
            m_imageList->setCurrentRow(m_lastRow);
        m_loading = false;
        return;
    }
    if (row < 0 || row >= m_images.size()) {
        m_canvas->setImage(QImage());
        m_boxes.clear();
        m_currentPath.clear();
        m_lastRow = -1;
        refreshBoxList();
        return;
    }
    m_lastRow = row;
    loadCurrentImage();
}

void BoxAnnotatorDialog::loadCurrentImage()
{
    const QString path = currentImagePath();
    if (path.isEmpty())
        return;
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull()) {
        // 读不出就别认领这张（2026-10-29 r15 P1-9）：否则后续保存会用空框覆盖它的 txt
        m_boxes.clear();
        m_currentPath.clear();
        m_canvas->setImage(QImage());
        refreshBoxList();
        m_status->setText(QStringLiteral("读不出这张图：%1").arg(path));
        return;
    }
    int bad = 0;
    m_boxes = YoloAnnotation::load(path, &bad);
    m_currentPath = path;   // 从这一刻起，m_boxes 属于这个文件
    m_canvas->setImage(img);
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    m_status->setText(bad > 0 ? QStringLiteral("已载入 %1 个框（跳过 %2 行非法标注）")
                                    .arg(m_boxes.size()).arg(bad)
                              : QStringLiteral("已载入 %1 个框").arg(m_boxes.size()));
}

bool BoxAnnotatorDialog::saveCurrentImage()
{
    // 存的是"m_boxes 所属的那张"（2026-10-29 r15 P1-9）——不能读 currentRow：
    // currentRowChanged 进槽时行已经是新的，那样会把上一张的框写进下一张。
    const QString path = m_currentPath;
    if (path.isEmpty() || m_canvas == nullptr)
        return true;   // 一张都还没载入过：没有可存的，也不算失败
    // 只有真的动过（有图且 boxes 与磁盘一致与否不追）才写：这里简单起见每次切换都写，
    // 空标注写空文件（= 这张图没有目标），语义正确。
    if (!m_boxes.isEmpty() || QFileInfo::exists(YoloAnnotation::labelPath(path))) {
        // 返回值必须看（2026-10-29 复核 P2-12）：磁盘满/权限不足时 save 返回假，
        // 原先照样按"已经存了"关窗或换图——标注就这么静默丢了。
        if (!YoloAnnotation::save(path, m_boxes)) {
            m_status->setText(QStringLiteral("标注保存失败（磁盘满或权限？）：%1").arg(path));
            return false;
        }
    }
    return true;
}

void BoxAnnotatorDialog::onBoxDrawn(const QRectF &px, bool /*shift*/)
{
    const int row = m_imageList->currentRow();
    if (row < 0)
        return;
    QImageReader r(currentImagePath());
    const QImage img = r.read();
    if (img.isNull())
        return;
    YoloBoxLabel label;
    const int cls = qMax(0, m_classCombo->currentIndex());
    if (!YoloBoxLabel::fromPixels(px, img.width(), img.height(), cls, &label)) {
        m_status->setText(QStringLiteral("框太小或在图外，忽略"));
        return;
    }
    m_boxes.append(label);
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    m_status->setText(QStringLiteral("已标注 %1 个框（切换图片自动保存）").arg(m_boxes.size()));
}

void BoxAnnotatorDialog::onBoxSelected(int index)
{
    if (m_boxList && m_boxList->currentRow() != index && index >= 0
        && index < m_boxList->count())
        m_boxList->setCurrentRow(index);
}

void BoxAnnotatorDialog::onDeleteBox()
{
    int index = m_boxList ? m_boxList->currentRow() : -1;
    if (index < 0 || index >= m_boxes.size())
        index = m_canvas->selectedIndex();
    if (index < 0 || index >= m_boxes.size())
        return;
    const QVector<YoloBoxLabel> snapshot = m_boxes;   // 失败要回滚（2026-10-30 r17 余量）
    m_boxes.removeAt(index);
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    if (!saveCurrentImage()) {
        // 磁盘没变，画布也别变（r15 P2-12 + r17 余量）：否则现场看到的是"删掉了"，
        // 实际文件还在——两套状态比报错更难查
        m_boxes = snapshot;
        m_canvas->setBoxes(m_boxes, m_classNames);
        refreshBoxList();
        m_status->setText(QStringLiteral("保存失败（磁盘满或权限？），已撤回这次删除：%1")
                              .arg(m_currentPath));
        return;
    }
    m_status->setText(QStringLiteral("已删除，剩 %1 个框").arg(m_boxes.size()));
}

void BoxAnnotatorDialog::onClearImage()
{
    const QVector<YoloBoxLabel> snapshot = m_boxes;   // 同上
    m_boxes.clear();
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    if (!saveCurrentImage()) {
        m_boxes = snapshot;
        m_canvas->setBoxes(m_boxes, m_classNames);
        refreshBoxList();
        m_status->setText(QStringLiteral("保存失败（磁盘满或权限？），已撤回这次清空：%1")
                              .arg(m_currentPath));
        return;
    }
    m_status->setText(QStringLiteral("已清空本图（写出空 txt = 这张图没有目标）"));
}

void BoxAnnotatorDialog::refreshBoxList()
{
    if (!m_boxList)
        return;
    m_boxList->clear();
    for (const YoloBoxLabel &b : m_boxes) {
        const QString name = (b.classId >= 0 && b.classId < m_classNames.size())
                                 ? m_classNames[b.classId]
                                 : QString::number(b.classId);
        m_boxList->addItem(QStringLiteral("%1  (w=%2 h=%3)")
                               .arg(name)
                               .arg(b.w, 0, 'f', 3)
                               .arg(b.h, 0, 'f', 3));
    }
}

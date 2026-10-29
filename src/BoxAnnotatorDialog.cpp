#include "BoxAnnotatorDialog.h"

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
    saveCurrentImage();
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
    m_loading = false;
    if (!m_images.isEmpty())
        m_imageList->setCurrentRow(0);
}

void BoxAnnotatorDialog::closeEvent(QCloseEvent *e)
{
    // 标完当前图直接点 X 是最常见用法——这一张的框必须存（2026-10-28 P2-1）
    saveCurrentImage();
    QDialog::closeEvent(e);
}

void BoxAnnotatorDialog::reject()
{
    saveCurrentImage();
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
    saveCurrentImage();   // 切换即存，避免"标了一半换图丢了"
    if (row < 0 || row >= m_images.size()) {
        m_canvas->setImage(QImage());
        m_boxes.clear();
        refreshBoxList();
        return;
    }
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
        m_status->setText(QStringLiteral("读不出这张图：%1").arg(path));
        return;
    }
    int bad = 0;
    m_boxes = YoloAnnotation::load(path, &bad);
    m_canvas->setImage(img);
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    m_status->setText(bad > 0 ? QStringLiteral("已载入 %1 个框（跳过 %2 行非法标注）")
                                    .arg(m_boxes.size()).arg(bad)
                              : QStringLiteral("已载入 %1 个框").arg(m_boxes.size()));
}

void BoxAnnotatorDialog::saveCurrentImage()
{
    const QString path = currentImagePath();
    if (path.isEmpty() || m_canvas == nullptr)
        return;
    // 只有真的动过（有图且 boxes 与磁盘一致与否不追）才写：这里简单起见每次切换都写，
    // 空标注写空文件（= 这张图没有目标），语义正确。
    if (!m_boxes.isEmpty() || QFileInfo::exists(YoloAnnotation::labelPath(path)))
        YoloAnnotation::save(path, m_boxes);
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
    m_boxes.removeAt(index);
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    saveCurrentImage();
    m_status->setText(QStringLiteral("已删除，剩 %1 个框").arg(m_boxes.size()));
}

void BoxAnnotatorDialog::onClearImage()
{
    m_boxes.clear();
    m_canvas->setBoxes(m_boxes, m_classNames);
    refreshBoxList();
    saveCurrentImage();
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

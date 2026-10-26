#include "AnnotationDialog.h"
#include "AnnotationLabelRule.h"
#include "HalconNode.h"
#include "OpencvUtil.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

AnnotationDialog::AnnotationDialog(NodeBase *node, const QString &annotationDir, QWidget *parent)
    : QDialog(parent)
    , m_node(node)
    , m_dir(annotationDir)
{
    setWindowTitle(QStringLiteral("标注 — 框选裁块"));
    resize(960, 680);
    QDir().mkpath(m_dir);

    auto *root = new QVBoxLayout(this);
    auto *bar = new QHBoxLayout();
    m_classCombo = new QComboBox();
    m_classCombo->setEditable(true);
    m_classCombo->setMinimumWidth(160);
    auto *drawBtn = new QPushButton(QStringLiteral("在图上画框"));
    auto *refreshBtn = new QPushButton(QStringLiteral("刷新类别"));
    bar->addWidget(new QLabel(QStringLiteral("类别:")));
    bar->addWidget(m_classCombo);
    bar->addWidget(drawBtn);
    bar->addWidget(refreshBtn);
    bar->addStretch();
    root->addLayout(bar);

    m_hint = new QLabel(QStringLiteral("选择或输入类别名，画框后自动裁块保存到 annotations/类别/。至少 2 类。"));
    m_hint->setStyleSheet(QStringLiteral("color:#666;"));
    root->addWidget(m_hint);

    m_view = new HalconWindow(this);
    root->addWidget(m_view, 1);

    auto *done = new QPushButton(QStringLiteral("完成"));
    auto *bottom = new QHBoxLayout();
    bottom->addStretch();
    bottom->addWidget(done);
    root->addLayout(bottom);

    connect(drawBtn, &QPushButton::clicked, this, &AnnotationDialog::onDrawBox);
    connect(refreshBtn, &QPushButton::clicked, this, &AnnotationDialog::refreshClassList);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_view, &HalconWindow::roiEdited, this, &AnnotationDialog::onRoiEdited);

    loadImage();
    refreshClassList();
}

void AnnotationDialog::loadImage()
{
    auto *hn = qobject_cast<HalconNode *>(m_node);
    if (!hn || !m_view)
        return;
    HImage img(hn->getInputImage());
    if (!img.IsInitialized())
        img = HImage(hn->getOutputImage());
    if (img.IsInitialized())
        m_view->setImage(img, QStringLiteral("标注"));
}

void AnnotationDialog::onDrawBox()
{
    if (m_view)
        m_view->setRoiEditable(true, RoiType::Rect);
    m_hint->setText(QStringLiteral("在图上拖拽矩形，松手即按当前类别保存裁块。"));
}

void AnnotationDialog::refreshClassList()
{
    const QString cur = m_classCombo->currentText();
    m_classCombo->clear();
    QDir dir(m_dir);
    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    m_classCombo->addItems(subs);
    if (m_classCombo->findText(cur) < 0 && !cur.isEmpty())
        m_classCombo->addItem(cur);
    m_classCombo->setCurrentText(cur.isEmpty() && !subs.isEmpty() ? subs.first() : cur);

    QFile cls(m_dir + QStringLiteral("/classes.txt"));
    if (cls.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&cls);
        for (const QString &s : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            ts << s << '\n';
        ts.flush();
        // 写失败要有声（2026-10-26 复核 P3）：类别索引落后于实际目录，训练侧会漏类
        if (ts.status() != QTextStream::Ok || !cls.flush())
            m_hint->setText(QStringLiteral("classes.txt 写入失败：%1").arg(cls.errorString()));
    } else {
        m_hint->setText(QStringLiteral("classes.txt 打不开：%1").arg(cls.errorString()));
    }
}

void AnnotationDialog::onRoiEdited(const RoiShape &shape)
{
    auto *hn = qobject_cast<HalconNode *>(m_node);
    if (!hn || !m_view)
        return;
    m_view->setRoiEditable(false);
    const QString cls = m_classCombo->currentText().trimmed();
    if (cls.isEmpty()) {
        m_hint->setText(QStringLiteral("请先填写类别名。"));
        return;
    }
    HImage input(hn->getInputImage());
    if (!input.IsInitialized())
        input = HImage(hn->getOutputImage());
    cv::Mat mat = OpencvUtil::himageToMat(input);
    if (mat.empty())
        return;
    const QRectF r = roiAxisAlignedBounds(shape);
    if (r.width() < 2 || r.height() < 2)
        return;
    const int x = qBound(0, int(r.x()), mat.cols - 1);
    const int y = qBound(0, int(r.y()), mat.rows - 1);
    const int w = qBound(1, int(r.width()), mat.cols - x);
    const int h = qBound(1, int(r.height()), mat.rows - y);
    cv::Mat crop = mat(cv::Rect(x, y, w, h)).clone();

    // 类别名 + 目录落点一次解析（2026-10-26）：白名单 → mkpath → canonical 包含检查都收在
    // annotationClassDir 里，便于用临时目录单测"真的写不出去"（见 UiGuardsTest）。
    QString classDir;
    QString labelWhy;
    if (!annotationClassDir(m_dir, cls, &classDir, &labelWhy)) {
        m_hint->setText(QStringLiteral("类别不可用：%1").arg(labelWhy));
        return;
    }
    const QString path = classDir + QLatin1Char('/')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"))
        + QStringLiteral(".png");
    // 检查 cv::imwrite 的返回值（2026-10-25 复核 P3-6）：原来返回值直接丢掉，路径含中文、
    // 无写权限、磁盘满时静默失败，界面却照常提示"已保存"——样本凭空丢失，现场以为存上了。
    if (!cv::imwrite(path.toStdString(), crop)) {
        m_hint->setText(QStringLiteral("样本保存失败：%1（路径/权限/磁盘？）").arg(path));
        return;
    }
    refreshClassList();
    m_classCombo->setCurrentText(cls);
    m_hint->setText(QStringLiteral("已保存 %1").arg(path));
    m_view->setRoiShape(shape);
    m_view->setRoiHandleEditEnabled(true);
}

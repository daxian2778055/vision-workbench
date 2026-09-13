#include "BrandHeader.h"

#include <QPainter>
#include <QPainterPath>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLinearGradient>
#include <QStringList>

namespace {
// 与 VisionWorkbenchStyle 的暗色主题保持一致
const QColor kBackgroundTop(0x2b, 0x2b, 0x31);
const QColor kBackgroundBottom(0x24, 0x24, 0x29);
const QColor kBorder(0x3c, 0x3c, 0x42);
const QColor kAccent(0x40, 0xa9, 0xff);      // 与调色板 Link 色一致
const QColor kTitleColor(0xee, 0xee, 0xf2);
const QColor kTaglineColor(0x9a, 0x9a, 0xa6);

constexpr int kHeaderHeight = 44;
constexpr int kMarkSize = 26;
constexpr int kHorizontalPadding = 12;
constexpr int kMarkTextGap = 10;
} // namespace

BrandHeader::BrandHeader(QWidget *parent)
    : QWidget(parent)
    , m_productName(QStringLiteral("VisionFlowPlatform"))
    , m_tagline(QStringLiteral("机器视觉流程编排 · 方案工作台"))
{
    setObjectName(QStringLiteral("brandHeader"));
    setFixedHeight(kHeaderHeight);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAttribute(Qt::WA_StyledBackground, false);
    m_logo = loadExternalLogo();
}

void BrandHeader::setLogoPixmap(const QPixmap &pixmap)
{
    m_logo = pixmap;
    update();
}

QPixmap BrandHeader::loadExternalLogo()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList dirs = {
        appDir,
        appDir + QStringLiteral("/brand"),
    };
    const QStringList names = {
        QStringLiteral("logo.png"),
        QStringLiteral("logo.jpg"),
        QStringLiteral("logo.jpeg"),
        QStringLiteral("logo.bmp"),
        QStringLiteral("logo.svg"),
    };

    for (const QString &dir : dirs) {
        for (const QString &name : names) {
            const QString path = QDir(dir).absoluteFilePath(name);
            if (!QFileInfo::exists(path))
                continue;
            QPixmap pixmap(path);
            if (!pixmap.isNull())
                return pixmap;
        }
    }
    return QPixmap();
}

QSize BrandHeader::sizeHint() const
{
    return QSize(320, kHeaderHeight);
}

void BrandHeader::paintBuiltinMark(QPainter &painter, const QRectF &box)
{
    // 镜头准星：外圈 + 中心点 + 四向刻度，风格与工具库图标一致
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center = box.center();
    const qreal radius = box.width() / 2.0;

    QPainterPath ring;
    ring.addEllipse(center, radius * 0.78, radius * 0.78);
    painter.setPen(QPen(kAccent, 1.8));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(ring);

    painter.setBrush(kAccent);
    painter.drawEllipse(center, radius * 0.24, radius * 0.24);

    painter.setPen(QPen(kAccent, 1.6));
    const qreal outer = radius;
    const qreal inner = radius * 0.9;
    painter.drawLine(QPointF(center.x(), center.y() - outer),
                     QPointF(center.x(), center.y() - inner));
    painter.drawLine(QPointF(center.x(), center.y() + inner),
                     QPointF(center.x(), center.y() + outer));
    painter.drawLine(QPointF(center.x() - outer, center.y()),
                     QPointF(center.x() - inner, center.y()));
    painter.drawLine(QPointF(center.x() + inner, center.y()),
                     QPointF(center.x() + outer, center.y()));

    painter.restore();
}

void BrandHeader::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 背景：轻微竖向渐变 + 底部一条分隔线
    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0.0, kBackgroundTop);
    gradient.setColorAt(1.0, kBackgroundBottom);
    painter.fillRect(rect(), gradient);
    painter.setPen(kBorder);
    painter.drawLine(0, height() - 1, width(), height() - 1);

    const QRect content(0, 0, width(), height() - 1);
    qreal cursorX = kHorizontalPadding;

    // Logo：外部图片优先，否则内置绘制
    const QRectF logoBox(cursorX, content.center().y() - kMarkSize / 2.0,
                         kMarkSize, kMarkSize);
    if (!m_logo.isNull()) {
        const QPixmap scaled = m_logo.scaled(
            QSize(kMarkSize * 3, kMarkSize),
            Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QRectF target(logoBox.center().x() - scaled.width() / 2.0,
                            logoBox.center().y() - scaled.height() / 2.0,
                            scaled.width(), scaled.height());
        painter.drawPixmap(target, scaled, QRectF(scaled.rect()));
        cursorX = target.right() + kMarkTextGap;
    } else {
        paintBuiltinMark(painter, logoBox);
        cursorX = logoBox.right() + kMarkTextGap;
    }

    // 产品名 + 副标题
    QFont titleFont = font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(qMax(10.0, font().pointSizeF() + 1.5));
    painter.setFont(titleFont);
    painter.setPen(kTitleColor);
    const QFontMetrics titleMetrics(titleFont);
    const int titleWidth = titleMetrics.horizontalAdvance(m_productName);
    const int baseline = content.center().y() + titleMetrics.ascent() / 2 - 1;
    painter.drawText(QPointF(cursorX, baseline), m_productName);

    QFont taglineFont = font();
    taglineFont.setPointSizeF(qMax(8.0, font().pointSizeF() - 0.5));
    painter.setFont(taglineFont);
    painter.setPen(kTaglineColor);
    const QFontMetrics taglineMetrics(taglineFont);
    const qreal taglineX = cursorX + titleWidth + 12;
    if (taglineX + taglineMetrics.horizontalAdvance(m_tagline) < width() - kHorizontalPadding) {
        painter.drawText(QPointF(taglineX, baseline), m_tagline);
    }
}

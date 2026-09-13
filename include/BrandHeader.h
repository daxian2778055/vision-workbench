#pragma once

#include <QWidget>
#include <QPixmap>
#include <QString>

/// 流程编辑器上方的品牌栏（需求 FR4.4：流程编辑器上方显示软件 Logo）。
///
/// Logo 解析顺序（便于现场替换，无需重新编译）：
///   1. &lt;程序目录&gt;/logo.png|jpg|jpeg|bmp|svg
///   2. &lt;程序目录&gt;/brand/logo.*
///   3. 内置矢量绘制（镜头准星图形 + 产品名）
/// 第 3 条保证任何部署环境都一定有 Logo 可显示，不会出现空白条。
class BrandHeader : public QWidget
{
    Q_OBJECT

public:
    explicit BrandHeader(QWidget *parent = nullptr);

    /// 直接指定 Logo 图片（为空则回退到内置绘制）
    void setLogoPixmap(const QPixmap &pixmap);
    /// 当前是否在使用外部 Logo 图片（用于自检/排障）
    bool usingExternalLogo() const { return !m_logo.isNull(); }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    /// 在程序目录及 brand/ 子目录中查找 logo 图片
    static QPixmap loadExternalLogo();
    /// 内置矢量 Logo：镜头准星
    static void paintBuiltinMark(QPainter &painter, const QRectF &box);

    QPixmap m_logo;          ///< 外部 Logo（可能为空 → 使用内置绘制）
    QString m_productName;
    QString m_tagline;
};

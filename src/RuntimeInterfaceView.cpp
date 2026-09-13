#include "RuntimeInterfaceView.h"
#include "HalconWindow.h"
#include "GlobalVariableManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QResizeEvent>
#include <QMetaType>

RuntimeInterfaceView::RuntimeInterfaceView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RuntimeInterfaceView"));
    setStyleSheet(QStringLiteral(
        "QWidget#RuntimeInterfaceView { background-color: #17171c; }"
        "QLabel { color: #e0e0e0; }"
    ));
    setMinimumSize(400, 300);
}

void RuntimeInterfaceView::setInterface(const RuntimeInterface &layout)
{
    clearWidgets();
    m_current = layout;
    rebuildWidgets();
    update();
}

void RuntimeInterfaceView::clearWidgets()
{
    for (QWidget *w : m_widgets) {
        w->setParent(nullptr);
        w->deleteLater();
    }
    m_widgets.clear();
    m_imageViews.clear();
    m_imageControlIndex.clear();
    m_valueLabels.clear();
    m_valueControlIndex.clear();
    m_lightLabels.clear();
    m_lightControlIndex.clear();
    m_nodeImageMap.clear();
    m_nodeValueMap.clear();
    m_nodeLightMap.clear();
}

void RuntimeInterfaceView::rebuildWidgets()
{
    for (int i = 0; i < m_current.controls.size(); ++i) {
        const RuntimeControl &ctrl = m_current.controls[i];
        if (!ctrl.visible) continue;

        QWidget *w = createControlWidget(ctrl);
        if (!w) continue;
        w->setGeometry(ctrl.geometry);
        w->setParent(this);
        w->show();
        m_widgets.append(w);

        // 登记图像控件
        if (ctrl.type == RuntimeControlType::ImageView) {
            auto *hw = w->findChild<HalconWindow *>(QStringLiteral("RuntimeImageView"));
            if (hw) {
                m_imageViews.append(hw);
                m_imageControlIndex.append(i);
                if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                    m_nodeImageMap[ctrl.bindKey].append(static_cast<int>(m_imageViews.size() - 1));
            }
        }
        // 登记数值控件
        else if (ctrl.type == RuntimeControlType::ValueDisplay) {
            auto *val = w->findChild<QLabel *>(QStringLiteral("RuntimeValueLabel"));
            if (val) {
                m_valueLabels.append(val);
                m_valueControlIndex.append(i);
                if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                    m_nodeValueMap[ctrl.bindKey].append(static_cast<int>(m_valueLabels.size() - 1));
            }
        }
        // 登记状态灯
        else if (ctrl.type == RuntimeControlType::StatusLight) {
            auto *light = w->findChild<QLabel *>(QStringLiteral("RuntimeLightLabel"));
            if (light) {
                m_lightLabels.append(light);
                m_lightControlIndex.append(i);
                if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                    m_nodeLightMap[ctrl.bindKey].append(static_cast<int>(m_lightLabels.size() - 1));
            }
        }
    }

    // 初始值：全局变量
    auto *gv = GlobalVariableManager::instance();
    for (int k = 0; k < m_valueLabels.size(); ++k) {
        const int ci = m_valueControlIndex[k];
        const RuntimeControl &ctrl = m_current.controls[ci];
        if (ctrl.bindType == "global" && gv->variableExists(ctrl.bindKey)) {
            updateVariable(ctrl.bindKey, gv->getVariable(ctrl.bindKey));
        }
    }
    for (int k = 0; k < m_lightLabels.size(); ++k) {
        const int ci = m_lightControlIndex[k];
        const RuntimeControl &ctrl = m_current.controls[ci];
        if (ctrl.bindType == "global" && gv->variableExists(ctrl.bindKey)) {
            updateVariable(ctrl.bindKey, gv->getVariable(ctrl.bindKey));
        }
    }
}

QWidget *RuntimeInterfaceView::createControlWidget(const RuntimeControl &ctrl)
{
    switch (ctrl.type) {
    case RuntimeControlType::ImageView: {
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(1, 1, 1, 1);
        lay->setSpacing(0);

        auto *title = new QLabel(ctrl.displayTitle(), host);
        title->setStyleSheet(QStringLiteral(
            "QLabel { background-color: rgba(30,30,46,220); color: #d8d8e0;"
            "  padding: 2px 8px; font-size: 11px; border-bottom: 1px solid #3a3a5c; }"));
        lay->addWidget(title);

        auto *hw = new HalconWindow(host);
        hw->setObjectName(QStringLiteral("RuntimeImageView"));
        lay->addWidget(hw, 1);

        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #1e1e21; border: 1px solid #3a3a5c; }"));
        return host;
    }
    case RuntimeControlType::ValueDisplay:
    case RuntimeControlType::TextLabel: {
        return createLabelWidget(ctrl);
    }
    case RuntimeControlType::StatusLight: {
        auto *host = new QWidget();
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(8);

        auto *light = new QLabel(host);
        light->setObjectName(QStringLiteral("RuntimeLightLabel"));
        light->setFixedSize(26, 26);
        light->setAlignment(Qt::AlignCenter);
        light->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 13px;")
                                 .arg(ctrl.color));

        auto *lbl = new QLabel(ctrl.displayTitle(), host);
        lbl->setStyleSheet(QStringLiteral("font-size: %1px; color: #e0e0e0;").arg(ctrl.fontSize));
        lbl->setWordWrap(true);
        lay->addWidget(light);
        lay->addWidget(lbl, 1);

        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::Button: {
        auto *btn = new QPushButton(ctrl.displayTitle(), nullptr);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: white; border: none;"
            "  border-radius: 4px; font-size: %2px; }"
            "QPushButton:hover { background-color: #4a6a9c; }"
            "QPushButton:pressed { background-color: #2c4a75; }")
            .arg(ctrl.color).arg(ctrl.fontSize));
        const QString actionId = ctrl.bindKey;
        connect(btn, &QPushButton::clicked, this, [this, actionId]() {
            emit actionTriggered(actionId);
        });
        return btn;
    }
    }
    return nullptr;
}

QWidget *RuntimeInterfaceView::createLabelWidget(const RuntimeControl &ctrl)
{
    auto *host = new QWidget();
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(2);

    auto *title = new QLabel(ctrl.displayTitle(), host);
    title->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
    lay->addWidget(title);

    auto *value = new QLabel(QStringLiteral("--"), host);
    value->setObjectName(QStringLiteral("RuntimeValueLabel"));
    value->setAlignment(Qt::AlignCenter);
    value->setStyleSheet(QStringLiteral("font-size: %1px; font-weight: bold; color: %2;")
                             .arg(ctrl.fontSize).arg(ctrl.color));
    value->setWordWrap(true);
    lay->addWidget(value, 1);

    // 文本标签：直接把标题作为显示文本
    if (ctrl.type == RuntimeControlType::TextLabel) {
        value->setText(ctrl.displayTitle());
        value->setObjectName(QStringLiteral("RuntimeValueLabel"));
    }

    host->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
    return host;
}

void RuntimeInterfaceView::updateVariable(const QString &name, const QVariant &value)
{
    // 数值控件
    for (int k = 0; k < m_valueControlIndex.size(); ++k) {
        const int ci = m_valueControlIndex[k];
        const RuntimeControl &ctrl = m_current.controls[ci];
        if (ctrl.bindType == "global" && ctrl.bindKey == name && k < m_valueLabels.size()) {
            updateValueLabel(m_valueLabels[k], ctrl, value);
        }
    }
    // 状态灯控件
    for (int k = 0; k < m_lightControlIndex.size(); ++k) {
        const int ci = m_lightControlIndex[k];
        const RuntimeControl &ctrl = m_current.controls[ci];
        if (ctrl.bindType == "global" && ctrl.bindKey == name && k < m_lightLabels.size()) {
            updateStatusLight(m_lightLabels[k], ctrl, value);
        }
    }
}

void RuntimeInterfaceView::updateNodeOutput(const QString &nodeFullName, const QVariant &value)
{
    const auto it = m_nodeValueMap.constFind(nodeFullName);
    if (it != m_nodeValueMap.constEnd()) {
        for (int vi : it.value()) {
            if (vi < 0 || vi >= m_valueLabels.size()) continue;
            const int ci = m_valueControlIndex[vi];
            updateValueLabel(m_valueLabels[vi], m_current.controls[ci], value);
        }
    }
    const auto lit = m_nodeLightMap.constFind(nodeFullName);
    if (lit != m_nodeLightMap.constEnd()) {
        for (int vi : lit.value()) {
            if (vi < 0 || vi >= m_lightLabels.size()) continue;
            const int ci = m_lightControlIndex[vi];
            updateStatusLight(m_lightLabels[vi], m_current.controls[ci], value);
        }
    }
}

void RuntimeInterfaceView::pushImage(const QString &nodeFullName, const HalconCpp::HImage &image)
{
    const auto it = m_nodeImageMap.constFind(nodeFullName);
    if (it == m_nodeImageMap.constEnd()) return;
    if (!image.IsInitialized()) return;
    for (int vi : it.value()) {
        if (vi < 0 || vi >= m_imageViews.size()) continue;
        m_imageViews[vi]->setImage(image, nodeFullName);
    }
}

void RuntimeInterfaceView::updateValueLabel(QLabel *label, const RuntimeControl &ctrl, const QVariant &value)
{
    if (!label) return;
    QString text;
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        text = value.toString();
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        text = QString::number(value.toDouble(), 'f', 3);
        break;
    case QMetaType::Bool:
        text = value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
        break;
    default:
        text = value.toString();
        break;
    }
    label->setText(text);
    label->setStyleSheet(QStringLiteral("font-size: %1px; font-weight: bold; color: %2;")
                             .arg(ctrl.fontSize).arg(ctrl.color));
}

void RuntimeInterfaceView::updateStatusLight(QLabel *light, const RuntimeControl &ctrl, const QVariant &value)
{
    if (!light) return;
    bool ok = false;
    if (value.typeId() == QMetaType::Bool) {
        ok = value.toBool();
    } else {
        const QString s = value.toString().trimmed().toLower();
        ok = (s == QStringLiteral("ok") || s == QStringLiteral("true") || s == QStringLiteral("1")
              || s == QStringLiteral("pass"));
    }
    const QColor c = ok ? QColor(0x3d, 0xb9, 0x7a) : QColor(0xe7, 0x4c, 0x4c);
    light->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 13px;")
                             .arg(c.name()));
}

void RuntimeInterfaceView::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_current.controls.isEmpty()) {
        QPainter p(this);
        p.setPen(QColor(0x55, 0x58, 0x62));
        QFont f = p.font();
        f.setPointSizeF(13);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("未配置自定义运行界面\n请通过「视图 → 运行界面设计」添加控件"));
    }
}

void RuntimeInterfaceView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    for (QWidget *w : m_widgets) {
        // 保持控件相对画布的左上角锚定（控件尺寸由设计器指定，不随窗口缩放）
        Q_UNUSED(w);
    }
}

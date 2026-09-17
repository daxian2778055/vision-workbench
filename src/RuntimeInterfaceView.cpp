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
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>

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
    m_current.ensurePage();
    rebuildWidgets();
    update();
}

void RuntimeInterfaceView::clearWidgets()
{
    for (QTimer *t : m_tableTimers) {
        if (t) { t->stop(); t->deleteLater(); }
    }
    m_tableTimers.clear();
    // 页容器销毁会连带销毁其上的控件；多页时 Tab 页由 m_tabs 统一持有
    if (m_tabs) {
        m_tabs->deleteLater();
        m_tabs = nullptr;
    }
    for (QWidget *c : m_pageContainers) {
        c->deleteLater();
    }
    m_pageContainers.clear();
    m_widgets.clear();
    m_widgetCtrls.clear();
    m_imageViews.clear();
    m_imageCtrls.clear();
    m_valueLabels.clear();
    m_valueCtrls.clear();
    m_lightLabels.clear();
    m_lightCtrls.clear();
    m_tables.clear();
    m_tableCtrls.clear();
    m_tableCurrent.clear();
    m_tableDirty.clear();
    m_ioLights.clear();
    m_ioCtrls.clear();
    m_ioTexts.clear();
    m_nodeImageMap.clear();
    m_nodeValueMap.clear();
    m_nodeLightMap.clear();
    m_nodeIoMap.clear();
}

void RuntimeInterfaceView::rebuildWidgets()
{
    const int pageCount = m_current.pages.size();
    for (int pi = 0; pi < pageCount; ++pi) {
        const RuntimeInterfacePage &pg = m_current.pages[pi];

        QWidget *container = nullptr;
        if (pageCount > 1) {
            if (!m_tabs) {
                m_tabs = new QTabWidget(this);
                m_tabs->setGeometry(rect());
                m_tabs->setStyleSheet(QStringLiteral(
                    "QTabWidget::pane { border: 1px solid #3a3a5c; background: #17171c; }"
                    "QTabBar::tab { background: #22222a; color: #c0c0cc; padding: 5px 14px; }"
                    "QTabBar::tab:selected { background: #3a4a6a; color: white; }"));
            }
            container = new QWidget();
            m_tabs->addTab(container, pg.pageName);
        } else {
            container = this;
        }
        m_pageContainers.append(container);

        for (int ci = 0; ci < pg.controls.size(); ++ci) {
            const RuntimeControl &ctrl = pg.controls[ci];
            if (!ctrl.visible) continue;

            QWidget *w = createControlWidget(ctrl);
            if (!w) continue;
            w->setGeometry(scaledGeometry(ctrl.geometry));
            w->setParent(container);
            w->show();
            m_widgets.append(w);
            m_widgetCtrls.append(&ctrl);

            // 登记图像控件
            if (ctrl.type == RuntimeControlType::ImageView) {
                auto *hw = w->findChild<HalconWindow *>(QStringLiteral("RuntimeImageView"));
                if (hw) {
                    m_imageViews.append(hw);
                    m_imageCtrls.append(&ctrl);
                    if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                        m_nodeImageMap[ctrl.bindKey].append(m_imageViews.size() - 1);
                }
            }
            // 登记数值控件
            else if (ctrl.type == RuntimeControlType::ValueDisplay) {
                auto *val = w->findChild<QLabel *>(QStringLiteral("RuntimeValueLabel"));
                if (val) {
                    m_valueLabels.append(val);
                    m_valueCtrls.append(&ctrl);
                    if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                        m_nodeValueMap[ctrl.bindKey].append(m_valueLabels.size() - 1);
                }
            }
            // 登记状态灯
            else if (ctrl.type == RuntimeControlType::StatusLight) {
                auto *light = w->findChild<QLabel *>(QStringLiteral("RuntimeLightLabel"));
                if (light) {
                    m_lightLabels.append(light);
                    m_lightCtrls.append(&ctrl);
                    if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                        m_nodeLightMap[ctrl.bindKey].append(m_lightLabels.size() - 1);
                }
            }
            // 登记结果表格
            else if (ctrl.type == RuntimeControlType::ResultTable) {
                auto *table = w->findChild<QTableWidget *>(QStringLiteral("RuntimeResultTable"));
                if (table) {
                    m_tables.append(table);
                    m_tableCtrls.append(&ctrl);
                    m_tableCurrent.append(QVariantMap());
                    m_tableDirty.append(false);
                    auto *timer = new QTimer(table);
                    timer->setSingleShot(true);
                    timer->setInterval(150);
                    connect(timer, &QTimer::timeout, this, &RuntimeInterfaceView::commitTableRows);
                    m_tableTimers.append(timer);
                }
            }
            // 登记 IO 状态
            else if (ctrl.type == RuntimeControlType::IoStatus) {
                auto *light = w->findChild<QLabel *>(QStringLiteral("RuntimeIoLight"));
                auto *text = w->findChild<QLabel *>(QStringLiteral("RuntimeIoText"));
                if (light) {
                    m_ioLights.append(light);
                    m_ioCtrls.append(&ctrl);
                    m_ioTexts.append(text);
                    if (!ctrl.bindKey.isEmpty() && ctrl.bindType == "node")
                        m_nodeIoMap[ctrl.bindKey].append(m_ioLights.size() - 1);
                }
            }
        }
    }

    // 初始值：全局变量
    auto *gv = GlobalVariableManager::instance();
    for (int k = 0; k < m_valueCtrls.size(); ++k) {
        const RuntimeControl &ctrl = *m_valueCtrls[k];
        if (ctrl.bindType == "global" && gv->variableExists(ctrl.bindKey))
            updateVariable(ctrl.bindKey, gv->getVariable(ctrl.bindKey));
    }
    for (int k = 0; k < m_lightCtrls.size(); ++k) {
        const RuntimeControl &ctrl = *m_lightCtrls[k];
        if (ctrl.bindType == "global" && gv->variableExists(ctrl.bindKey))
            updateVariable(ctrl.bindKey, gv->getVariable(ctrl.bindKey));
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
    case RuntimeControlType::ResultTable:
        return createTableWidget(ctrl);
    case RuntimeControlType::IoStatus:
        return createIoStatusWidget(ctrl);
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
    }

    host->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
    return host;
}

QWidget *RuntimeInterfaceView::createTableWidget(const RuntimeControl &ctrl)
{
    auto *host = new QWidget();
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(1, 1, 1, 1);
    lay->setSpacing(0);

    auto *title = new QLabel(ctrl.displayTitle(), host);
    title->setStyleSheet(QStringLiteral(
        "QLabel { background-color: rgba(30,30,46,220); color: #d8d8e0;"
        "  padding: 2px 8px; font-size: 11px; border-bottom: 1px solid #3a3a5c; }"));
    lay->addWidget(title);

    auto *table = new QTableWidget(host);
    table->setObjectName(QStringLiteral("RuntimeResultTable"));
    table->setColumnCount(ctrl.columns.size());
    QStringList headers;
    for (const ResultColumn &col : ctrl.columns)
        headers << (col.header.isEmpty() ? col.bindKey : col.header);
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(QStringLiteral(
        "QTableWidget { background-color: #17171c; color: #d8d8e0;"
        "  gridline-color: #2e2e3a; font-size: 12px; }"
        "QTableWidget::item:selected { background-color: #3a4a6a; }"
        "QHeaderView::section { background-color: #22222a; color: #9aa4c0;"
        "  border: 1px solid #3a3a5c; padding: 3px; font-size: 11px; }"));
    lay->addWidget(table, 1);

    host->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1e1e21; border: 1px solid #3a3a5c; }"));
    return host;
}

QWidget *RuntimeInterfaceView::createIoStatusWidget(const RuntimeControl &ctrl)
{
    auto *host = new QWidget();
    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(8);

    auto *light = new QLabel(host);
    light->setObjectName(QStringLiteral("RuntimeIoLight"));
    light->setFixedSize(24, 24);
    light->setAlignment(Qt::AlignCenter);
    light->setStyleSheet(QStringLiteral(
        "background-color: #555862; border-radius: 12px;"));   // 未执行=灰

    auto *right = new QVBoxLayout();
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(1);
    auto *lbl = new QLabel(ctrl.displayTitle(), host);
    lbl->setStyleSheet(QStringLiteral("font-size: 12px; color: #c0c0cc;"));
    auto *text = new QLabel(QStringLiteral("--"), host);
    text->setObjectName(QStringLiteral("RuntimeIoText"));
    text->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
    right->addWidget(lbl);
    right->addWidget(text, 1);

    lay->addWidget(light);
    lay->addLayout(right, 1);

    host->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
    return host;
}

QString RuntimeInterfaceView::cellText(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
        return QString::number(value.toDouble(), 'f', 3);
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    default:
        return value.toString();
    }
}

void RuntimeInterfaceView::updateVariable(const QString &name, const QVariant &value)
{
    // 数值控件
    for (int k = 0; k < m_valueCtrls.size(); ++k) {
        const RuntimeControl &ctrl = *m_valueCtrls[k];
        if (ctrl.bindType == "global" && ctrl.bindKey == name)
            updateValueLabel(m_valueLabels[k], ctrl, value);
    }
    // 状态灯控件
    for (int k = 0; k < m_lightCtrls.size(); ++k) {
        const RuntimeControl &ctrl = *m_lightCtrls[k];
        if (ctrl.bindType == "global" && ctrl.bindKey == name)
            updateStatusLight(m_lightLabels[k], ctrl, value);
    }
    // 结果表格列
    if (!m_tables.isEmpty()) {
        for (int t = 0; t < m_tableCtrls.size(); ++t) {
            const RuntimeControl &ctrl = *m_tableCtrls[t];
            for (int c = 0; c < ctrl.columns.size(); ++c) {
                const ResultColumn &col = ctrl.columns[c];
                if (col.bindType == "global" && col.bindKey == name) {
                    m_tableCurrent[t][QString::number(c)] = value;
                    m_tableDirty[t] = true;
                    if (m_tableTimers[t]) m_tableTimers[t]->start();
                }
            }
        }
    }
}

void RuntimeInterfaceView::updateNodeOutput(const QString &nodeFullName, const QVariant &value)
{
    const auto it = m_nodeValueMap.constFind(nodeFullName);
    if (it != m_nodeValueMap.constEnd()) {
        for (int vi : it.value()) {
            if (vi < 0 || vi >= m_valueCtrls.size()) continue;
            updateValueLabel(m_valueLabels[vi], *m_valueCtrls[vi], value);
        }
    }
    const auto lit = m_nodeLightMap.constFind(nodeFullName);
    if (lit != m_nodeLightMap.constEnd()) {
        for (int vi : lit.value()) {
            if (vi < 0 || vi >= m_lightCtrls.size()) continue;
            updateStatusLight(m_lightLabels[vi], *m_lightCtrls[vi], value);
        }
    }
    // 结果表格的节点列
    if (!m_tables.isEmpty()) {
        for (int t = 0; t < m_tableCtrls.size(); ++t) {
            const RuntimeControl &ctrl = *m_tableCtrls[t];
            for (int c = 0; c < ctrl.columns.size(); ++c) {
                const ResultColumn &col = ctrl.columns[c];
                if (col.bindType == "node" && col.bindKey == nodeFullName) {
                    m_tableCurrent[t][QString::number(c)] = value;
                    m_tableDirty[t] = true;
                    if (m_tableTimers[t]) m_tableTimers[t]->start();
                }
            }
        }
    }
}

void RuntimeInterfaceView::updateNodePortMap(const QString &nodeFullName, const QVariantMap &ports)
{
    const auto iit = m_nodeIoMap.constFind(nodeFullName);
    if (iit == m_nodeIoMap.constEnd() || iit.value().isEmpty()) return;

    // 优先取语义端口（CameraIoNode：0=值, 1=成功, 2=错误）
    const QVariant succV = ports.value(QStringLiteral("成功"));
    const QVariant valV = ports.value(QStringLiteral("值"));
    const QString errS = ports.value(QStringLiteral("错误")).toString().trimmed();
    const bool hasSucc = ports.contains(QStringLiteral("成功"));

    for (int vi : iit.value()) {
        if (vi < 0 || vi >= m_ioLights.size()) continue;
        QLabel *light = m_ioLights[vi];
        QLabel *text = (vi < m_ioTexts.size()) ? m_ioTexts[vi] : nullptr;
        const RuntimeControl &ctrl = *m_ioCtrls[vi];

        // 灯：执行成功=绿，失败=红
        if (hasSucc && light) {
            const QColor c = succV.toBool() ? QColor(0x3d, 0xb9, 0x7a)
                                            : QColor(0xe7, 0x4c, 0x4c);
            light->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 12px;")
                                     .arg(c.name()));
        }
        // 文本：线值 / 错误
        if (text) {
            QString txt = QStringLiteral("--");
            QColor tc(0x8a, 0x8a, 0x9a);
            if (valV.isValid()) {
                txt = valV.typeId() == QMetaType::Bool
                          ? (valV.toBool() ? QStringLiteral("ON") : QStringLiteral("OFF"))
                          : cellText(valV);
            }
            if (!errS.isEmpty()) {
                txt += QStringLiteral("  [%1]").arg(errS);
                tc = QColor(0xe7, 0x4c, 0x4c);
            }
            text->setText(txt);
            text->setStyleSheet(QStringLiteral("font-size: %1px; color: %2;")
                                     .arg(qMin(ctrl.fontSize, 14)).arg(tc.name()));
        }
    }
}

void RuntimeInterfaceView::commitTableRows()
{
    for (int t = 0; t < m_tables.size(); ++t) {
        if (!m_tableDirty[t]) continue;
        QTableWidget *table = m_tables[t];
        const RuntimeControl &ctrl = *m_tableCtrls[t];
        if (!table) { m_tableDirty[t] = false; continue; }

        const int cols = ctrl.columns.size();
        const int row = table->rowCount();
        table->insertRow(row);
        for (int c = 0; c < cols; ++c) {
            const QVariant v = m_tableCurrent[t].value(QString::number(c));
            table->setItem(row, c, new QTableWidgetItem(cellText(v)));
        }
        m_tableDirty[t] = false;
        m_tableCurrent[t].clear();

        // 环形行数上限：超出后删最旧行
        const int maxRows = qMax(1, ctrl.maxRows);
        while (table->rowCount() > maxRows)
            table->removeRow(0);
        table->scrollToBottom();
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
    label->setText(cellText(value));
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
    if (m_current.totalControlCount() == 0) {
        QPainter p(this);
        p.setPen(QColor(0x55, 0x58, 0x62));
        QFont f = p.font();
        f.setPointSizeF(13);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("未配置自定义运行界面\n请通过「视图 → 运行界面设计」添加控件"));
    }
}

QRect RuntimeInterfaceView::scaledGeometry(const QRect &g) const
{
    // 设计画布 1200×760 → 当前窗口，等比映射；小控件给最小尺寸保护
    const double sx = double(width()) / qMax(1, m_designSize.width());
    const double sy = double(height()) / qMax(1, m_designSize.height());
    return QRect(qRound(g.x() * sx), qRound(g.y() * sy),
                 qMax(40, qRound(g.width() * sx)),
                 qMax(24, qRound(g.height() * sy)));
}

void RuntimeInterfaceView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_tabs)
        m_tabs->setGeometry(rect());
    // 控件按设计基准等比缩放（大屏/小屏不跑版）
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        if (m_widgets[i] && m_widgetCtrls[i])
            m_widgets[i]->setGeometry(scaledGeometry(m_widgetCtrls[i]->geometry));
    }
}

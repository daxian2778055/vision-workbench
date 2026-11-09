#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX   // windows.h 的 min/max 宏会打断 QtCharts 的 qreal min() 声明
#  endif
#endif
#include "RuntimeInterfaceView.h"
#include "HalconWindow.h"
#include "GlobalVariableManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QSet>
#include <QResizeEvent>
#include <QMetaType>
#include <QTabWidget>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QPixmap>
#include <QTimer>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

namespace {
/// 线图阵列控件（官方 9.3 图表）：每个数据源一张线图，横轴=执行次数，纵轴=当前值。
/// Qt Charts 只在本文件使用，对外仍是普通 QWidget（objectName=RuntimeLineChart）。
class RuntimeChartPanel : public QWidget
{
public:
    explicit RuntimeChartPanel(const RuntimeControl &ctrl, QWidget *parent = nullptr)
        : QWidget(parent), m_ctrl(ctrl)
    {
        setObjectName(QStringLiteral("RuntimeLineChart"));
        const QColor bg(ctrl.chartBgColor);
        const QColor axisColor = QColor(ctrl.chartAxisColor).isValid()
                                     ? QColor(ctrl.chartAxisColor)
                                     : QColor(QStringLiteral("#9aa0b0"));
        setStyleSheet(QStringLiteral("background:%1;").arg(
            bg.isValid() ? bg.name() : QStringLiteral("#1e1e24")));
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        if (ctrl.showChartTitle) {
            auto *title = new QLabel(
                ctrl.chartTitle.isEmpty() ? ctrl.displayTitle() : ctrl.chartTitle, this);
            title->setObjectName(QStringLiteral("RuntimeChartTitle"));
            title->setStyleSheet(QStringLiteral("color:%1; font-size:11px; font-weight:bold;"
                                                " padding:2px 4px;")
                                     .arg(axisColor.name()));
            lay->addWidget(title);
        }
        for (int i = 0; i < ctrl.chartSeries.size(); ++i) {
            const ChartSeries &series = ctrl.chartSeries.at(i);
            auto *name = new QLabel(
                series.name.isEmpty() ? series.bindKey : series.name, this);
            name->setObjectName(QStringLiteral("RuntimeLineChartName%1").arg(i));
            name->setStyleSheet(QStringLiteral("color:%1; font-size:10px; padding:0 4px;")
                                    .arg(axisColor.name()));
            lay->addWidget(name);
            auto *chart = new QChart();
            chart->setBackgroundVisible(false);
            chart->legend()->hide();
            auto *line = new QLineSeries(chart);
            QPen pen(QColor(series.color));
            pen.setWidth(2);
            line->setPen(pen);
            chart->addSeries(line);
            auto *axisX = new QValueAxis(chart);
            axisX->setRange(0, qMax(9, ctrl.chartHistoryLimit - 1));
            axisX->setLabelsColor(axisColor);
            axisX->setTitleText(QStringLiteral("执行次数"));
            chart->addAxis(axisX, Qt::AlignBottom);
            line->attachAxis(axisX);
            auto *axisY = new QValueAxis(chart);
            axisY->setRange(0, 100);
            axisY->setLabelsColor(axisColor);
            chart->addAxis(axisY, Qt::AlignLeft);
            line->attachAxis(axisY);
            auto *view = new QChartView(chart, this);
            view->setObjectName(QStringLiteral("RuntimeLineChartView%1").arg(i));
            view->setRenderHint(QPainter::Antialiasing);
            view->setMinimumHeight(48);
            lay->addWidget(view, 1);
            m_lines.append(line);
            m_axesX.append(axisX);
            m_axesY.append(axisY);
            m_points.append(QVector<QPointF>());
            QLineSeries *upper = nullptr;
            QLineSeries *lower = nullptr;
            if (series.showLimits) {
                QPen limitPen(QColor(series.color));
                limitPen.setStyle(Qt::DashLine);
                limitPen.setWidth(1);
                upper = new QLineSeries(chart);
                upper->setPen(limitPen);
                chart->addSeries(upper);
                upper->attachAxis(axisX);
                upper->attachAxis(axisY);
                lower = new QLineSeries(chart);
                lower->setPen(limitPen);
                chart->addSeries(lower);
                lower->attachAxis(axisX);
                lower->attachAxis(axisY);
            }
            m_upper.append(upper);
            m_lower.append(lower);
        }
        if (ctrl.chartSeries.isEmpty()) {
            auto *hint = new QLabel(QStringLiteral("未配置数据系列"), this);
            hint->setAlignment(Qt::AlignCenter);
            hint->setStyleSheet(QStringLiteral("color:#8a8a9a;"));
            lay->addWidget(hint);
        }
    }

    int seriesCount() const { return m_lines.size(); }

    void appendSample(int index, double value)
    {
        if (index < 0 || index >= m_lines.size())
            return;
        QVector<QPointF> &pts = m_points[index];
        const double x = pts.isEmpty() ? 0.0 : pts.last().x() + 1.0;
        pts.append(QPointF(x, value));
        const int limit = qBound(1, m_ctrl.chartHistoryLimit, 1000);
        while (pts.size() > limit)
            pts.removeFirst();
        m_lines[index]->replace(pts);

        const ChartSeries &series = m_ctrl.chartSeries.at(index);
        double lo = pts.first().y();
        double hi = pts.first().y();
        for (const QPointF &p : pts) {
            lo = qMin(lo, p.y());
            hi = qMax(hi, p.y());
        }
        if (series.showLimits) {
            lo = qMin(lo, series.lower);
            hi = qMax(hi, series.upper);
        }
        if (qAbs(hi - lo) < 1e-9) {
            lo -= 1.0;
            hi += 1.0;
        }
        const double pad = (hi - lo) * 0.1;
        m_axesY[index]->setRange(lo - pad, hi + pad);
        const double x0 = pts.first().x();
        const double x1 = qMax(x0 + 1.0, pts.last().x());
        m_axesX[index]->setRange(x0, x1);
        if (series.showLimits && m_upper[index] && m_lower[index]) {
            m_upper[index]->replace(QVector<QPointF>{QPointF(x0, series.upper),
                                                     QPointF(x1, series.upper)});
            m_lower[index]->replace(QVector<QPointF>{QPointF(x0, series.lower),
                                                     QPointF(x1, series.lower)});
        }
    }

private:
    RuntimeControl m_ctrl;
    QVector<QLineSeries *> m_lines;
    QVector<QValueAxis *> m_axesX;
    QVector<QValueAxis *> m_axesY;
    QVector<QVector<QPointF>> m_points;
    QVector<QLineSeries *> m_upper;
    QVector<QLineSeries *> m_lower;
};
}   // namespace

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
    m_current.currentPageIndex = qBound(0, m_current.currentPageIndex,
                                        m_current.pages.size() - 1);
    // 缩放基准改为布局里的画布配置（原先硬编码 1200×760，画布尺寸改了也不生效）
    m_designSize = QSize(m_current.canvas.width, m_current.canvas.height);
    rebuildWidgets();
    // currentPageIndex 此前只存不用（盘点发现），这里让运行态默认页真正生效
    if (m_tabs && m_tabs->count() > 0)
        m_tabs->setCurrentIndex(m_current.currentPageIndex);
    // 单页没有 QTabWidget 信号，统一走同一入口建内层标签条 + 显隐
    onCurrentPageChanged(m_current.currentPageIndex);
    update();
}

void RuntimeInterfaceView::setParameterValues(const QHash<QString, double> &values)
{
    m_paramValues = values;
    // 已建出的参数框直接刷新；回填不触发写回（否则会反向写节点参数）
    for (QDoubleSpinBox *spin : findChildren<QDoubleSpinBox *>()) {
        const QString node = spin->property("vfpNode").toString();
        const QString key = spin->property("vfpParam").toString();
        if (node.isEmpty() || key.isEmpty()) continue;
        const QString id = node + QLatin1Char('|') + key;
        if (!values.contains(id)) continue;
        QSignalBlocker block(spin);
        spin->setValue(values.value(id));
    }
    // 开关控制复用同一份回填表：bool 以 0/1 承载
    for (QCheckBox *box : findChildren<QCheckBox *>()) {
        const QString node = box->property("vfpNode").toString();
        const QString key = box->property("vfpParam").toString();
        if (node.isEmpty() || key.isEmpty()) continue;
        const QString id = node + QLatin1Char('|') + key;
        if (!values.contains(id)) continue;
        QSignalBlocker block(box);
        box->setChecked(values.value(id) != 0.0);
    }
}

void RuntimeInterfaceView::setComboSelections(const QHash<QString, QString> &selections)
{
    m_comboSelections = selections;
    for (QComboBox *combo : findChildren<QComboBox *>()) {
        const QString node = combo->property("vfpNode").toString();
        const QString key = combo->property("vfpParam").toString();
        if (node.isEmpty() || key.isEmpty()) continue;
        const QString id = node + QLatin1Char('|') + key;
        if (!selections.contains(id)) continue;
        const int idx = combo->findText(selections.value(id));
        if (idx < 0) continue;
        QSignalBlocker block(combo);   // 回填不触发写回
        combo->setCurrentIndex(idx);
    }
}

void RuntimeInterfaceView::onCurrentPageChanged(int index)
{
    if (index < 0 || index >= m_current.pages.size()) return;
    m_current.currentPageIndex = index;
    RuntimeInterfacePage *pg = m_current.page(index);
    if (pg && !pg->innerTabNames.isEmpty()) {
        // 每页记住自己的选择；首次进入用该页默认标签
        m_innerTab = m_innerTabByPage.value(
            index, qBound(0, pg->defaultInnerTab, pg->innerTabNames.size() - 1));
        QWidget *container = m_pageContainers.value(index);
        if (container)
            buildInnerTabBar(container, *pg);
    } else {
        m_innerTab = 0;
    }
    applyInnerTabVisibility();
}

void RuntimeInterfaceView::buildInnerTabBar(QWidget *container, const RuntimeInterfacePage &pg)
{
    if (!container || pg.innerTabNames.isEmpty()) return;
    RuntimeInterfacePage *cur = m_current.page(m_current.currentPageIndex);
    const int topH = cur ? qMax(0, cur->topBarHeight) : 0;
    const int botH = cur ? qMax(0, cur->bottomBarHeight) : 0;
    const int leftW = cur ? qMax(0, cur->leftBarWidth) : 0;
    const int rightW = cur ? qMax(0, cur->rightBarWidth) : 0;
    const int stripH = qMax(16, pg.innerTabHeight);
    const QRect barGeo(leftW, topH, qMax(1, width() - leftW - rightW), stripH);

    // 已建过（切页回来 / resize 刷新）：只更新几何与选中态，不重复建条
    if (QWidget *existing =
            container->findChild<QWidget *>(QStringLiteral("RuntimeInnerTabBar"))) {
        existing->setGeometry(barGeo);
        for (QPushButton *b : existing->findChildren<QPushButton *>())
            b->setChecked(b->property("vfpTabIdx").toInt() == m_innerTab);
        return;
    }

    auto *bar = new QWidget(container);
    bar->setObjectName(QStringLiteral("RuntimeInnerTabBar"));
    bar->setGeometry(barGeo);
    bar->setStyleSheet(QStringLiteral("background:#1c1c24;"));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    for (int i = 0; i < pg.innerTabNames.size(); ++i) {
        auto *b = new QPushButton(pg.innerTabNames.at(i), bar);
        b->setCheckable(true);
        b->setChecked(i == m_innerTab);
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background:#1c1c24; color:#8a8e9c; border:none;"
            " border-right:1px solid #3a3a4a; padding:4px 12px; }"
            "QPushButton:checked { background:#2e2e3c; color:#d8d8e0; }"));
        const int idx = i;
        connect(b, &QPushButton::clicked, this, [this, idx, bar]() {
            m_innerTabByPage[m_current.currentPageIndex] = idx;
            m_innerTab = idx;
            for (QPushButton *sibling : bar->findChildren<QPushButton *>())
                sibling->setChecked(sibling->property("vfpTabIdx").toInt() == idx);
            applyInnerTabVisibility();
        });
        b->setProperty("vfpTabIdx", i);
        lay->addWidget(b);
    }
    lay->addStretch();
    bar->show();
    bar->raise();
}

void RuntimeInterfaceView::applyInnerTabVisibility()
{
    QWidget *container = m_pageContainers.value(m_current.currentPageIndex);
    if (!container) return;
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        QWidget *w = m_widgets.at(i);
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!w || !ctrl) continue;
        if (w->parentWidget() != container) continue;   // 只动当前页的控件
        const bool match = (ctrl->innerTab < 0 || ctrl->innerTab == m_innerTab);
        w->setVisible(match && ctrl->visible);
    }
}

void RuntimeInterfaceView::showPage(const QString &pageName)
{
    if (!m_tabs || pageName.isEmpty()) return;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabText(i) == pageName) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
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
    m_innerTabByPage.clear();
    m_innerTab = 0;
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
            // 单页也必须用**独立**容器。旧实现直接把 this 当容器并 append 进
            // m_pageContainers，下一次 setInterface() → clearWidgets() 会对视图自身
            // deleteLater()，留下 MainWindow::m_runtimeView 非空野指针（UAF）。
            // 设计器「应用并进入运行模式」同一事件里会连续调两次 setInterface。
            auto *single = new QWidget(this);
            single->setGeometry(rect());
            container = single;
        }
        m_pageContainers.append(container);

        for (int ci = 0; ci < pg.controls.size(); ++ci) {
            const RuntimeControl &ctrl = pg.controls[ci];
            if (!ctrl.visible) continue;

            QWidget *w = createControlWidget(ctrl);
            if (!w) continue;
            w->setGeometry(controlGeometry(ctrl));
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

    // 多页：切页时同步 currentPageIndex 并按该页自己的内层 Tab 刷新。
    // 连接放在所有页容器建好之后，避免 addTab 触发的首次 currentChanged
    // 在容器尚未登记时跑进来。
    if (m_tabs) {
        connect(m_tabs, &QTabWidget::currentChanged,
                this, &RuntimeInterfaceView::onCurrentPageChanged);
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
        // 官方外观分组（2026-11-09）：背景 / 边框颜色 / 点击颜色 / 字形加粗。
        // 空值 = 保持旧外观（背景取主色、无边框、按压色沿用默认），旧布局零变化。
        const QString bg = ctrl.bgColor.isEmpty() ? ctrl.color : ctrl.bgColor;
        const QString pressed = ctrl.pressedColor.isEmpty()
                                    ? QStringLiteral("#2c4a75")
                                    : ctrl.pressedColor;
        const QString border = ctrl.borderColor.isEmpty()
                                   ? QStringLiteral("none")
                                   : QStringLiteral("1px solid %1").arg(ctrl.borderColor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: white; border: %2;"
            "  border-radius: 4px; font-size: %3px; font-weight: %4; }"
            "QPushButton:hover { background-color: #4a6a9c; }"
            "QPushButton:pressed { background-color: %5; }")
            .arg(bg, border).arg(ctrl.fontSize)
            .arg(ctrl.bold ? QStringLiteral("bold") : QStringLiteral("normal"))
            .arg(pressed));
        // 官方动作表：逐行上报给 MainWindow 分发。exec 类仍发旧 actionTriggered
        // （新信号侧对 exec 直接忽略，避免起跑被执行两次）；
        // 没有动作表时回落旧的 bindKey，保持旧方案可用。
        const QList<RuntimeAction> acts = ctrl.actions;
        const QString legacyId = ctrl.bindKey;
        connect(btn, &QPushButton::clicked, this, [this, acts, legacyId]() {
            if (acts.isEmpty()) {
                emit actionTriggered(legacyId);
                return;
            }
            for (const RuntimeAction &a : acts)
                emit runtimeActionTriggered(a.commandType, a.method, a.param);
            const RuntimeAction &first = acts.first();
            if (first.commandType == QLatin1String("exec") && !first.method.isEmpty())
                emit actionTriggered(first.method);
        });
        return btn;
    }
    case RuntimeControlType::ParamSetting: {
        // 官方 9.3：参数设置控件订阅模块 int/float 参数，运行界面直接改。
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(2);
        auto *title = new QLabel(ctrl.displayTitle(), host);
        title->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
        lay->addWidget(title);

        auto *spin = new QDoubleSpinBox(host);
        spin->setRange(ctrl.paramMin, ctrl.paramMax);
        spin->setDecimals(3);
        spin->setKeyboardTracking(false);   // 只在回车/失焦提交，避免每敲一键都写参数
        spin->setProperty("vfpNode", ctrl.bindKey);
        spin->setProperty("vfpParam", ctrl.paramKey);
        const double seed = m_paramValues.value(
            ctrl.bindKey + QLatin1Char('|') + ctrl.paramKey, ctrl.paramMin);
        {
            QSignalBlocker block(spin);     // 初值不应触发写回
            spin->setValue(seed);
        }
        const QString nodeName = ctrl.bindKey;
        const QString paramKey = ctrl.paramKey;
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, nodeName, paramKey](double v) {
                    emit parameterChangeRequested(nodeName, paramKey, v);
                });
        lay->addWidget(spin, 1);
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::SwitchControl: {
        // 官方 9.3：开关控制订阅模块 bool 参数，运行界面控制其是否启用。
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(2);
        auto *title = new QLabel(ctrl.displayTitle(), host);
        title->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
        lay->addWidget(title);

        auto *box = new QCheckBox(host);
        box->setProperty("vfpNode", ctrl.bindKey);
        box->setProperty("vfpParam", ctrl.paramKey);
        const QString id = ctrl.bindKey + QLatin1Char('|') + ctrl.paramKey;
        {
            QSignalBlocker block(box);   // 回填不触发写回
            box->setChecked(m_paramValues.value(id, 0.0) != 0.0);
        }
        const QString nodeName = ctrl.bindKey;
        const QString paramKey = ctrl.paramKey;
        connect(box, &QCheckBox::toggled, this,
                [this, nodeName, paramKey](bool on) {
                    emit switchChangeRequested(nodeName, paramKey, on);
                });
        lay->addWidget(box, 1);
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::ComboBox: {
        // 官方 9.3：组合框在候选值之间切换（切配方/模式）
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(2);
        auto *title = new QLabel(ctrl.displayTitle(), host);
        title->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
        lay->addWidget(title);

        auto *combo = new QComboBox(host);
        combo->addItems(ctrl.comboOptions);
        combo->setProperty("vfpNode", ctrl.bindKey);
        combo->setProperty("vfpParam", ctrl.paramKey);
        const QString id = ctrl.bindKey + QLatin1Char('|') + ctrl.paramKey;
        {
            QSignalBlocker block(combo);   // 回填不触发写回
            const int idx = combo->findText(m_comboSelections.value(id));
            if (idx >= 0) combo->setCurrentIndex(idx);
        }
        const QString nodeName = ctrl.bindKey;
        const QString paramKey = ctrl.paramKey;
        connect(combo, &QComboBox::currentTextChanged, this,
                [this, nodeName, paramKey](const QString &text) {
                    if (text.isEmpty()) return;
                    emit comboChangeRequested(nodeName, paramKey, text);
                });
        lay->addWidget(combo, 1);
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::ImageAsset: {
        // 官方 9.3：图片控件可添加本地图片在运行界面上显示
        auto *label = new QLabel();
        label->setAlignment(Qt::AlignCenter);
        if (ctrl.imagePath.isEmpty()) {
            label->setText(QStringLiteral("（未设置图片）"));
            label->setStyleSheet(QStringLiteral(
                "color:#8a8a9a; background:#22222a; border:1px dashed #3a3a5c;"));
            return label;
        }
        const QPixmap pm(ctrl.imagePath);
        if (pm.isNull()) {
            label->setText(QStringLiteral("图片无法加载：%1").arg(ctrl.imagePath));
            label->setWordWrap(true);
            label->setStyleSheet(QStringLiteral(
                "color:#d08a3a; background:#22222a; border:1px solid #3a3a5c;"));
            return label;
        }
        label->setPixmap(pm.scaled(ctrl.geometry.size(), Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation));
        label->setStyleSheet(QStringLiteral("background:#17171c;"));
        return label;
    }
    case RuntimeControlType::WorldClock: {
        // 官方 9.3：世界时钟实时显示所选时区当前时间
        auto *label = new QLabel();
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; background:#22222a;"
                                            " border:1px solid #3a3a5c; border-radius:4px;")
                                 .arg(ctrl.color).arg(ctrl.fontSize));
        const int offsetMin = ctrl.timeZoneOffsetMinutes;
        auto tick = [label, offsetMin]() {
            label->setText(QDateTime::currentDateTimeUtc()
                               .addSecs(offsetMin * 60)
                               .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        };
        tick();
        auto *timer = new QTimer(label);   // parent = label：控件销毁时定时器一起销毁
        timer->setInterval(1000);
        QObject::connect(timer, &QTimer::timeout, label, tick);
        timer->start();
        return label;
    }
    case RuntimeControlType::LineChart:
        return new RuntimeChartPanel(ctrl);
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
    updateCharts(QStringLiteral("global"), name, value);
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
    updateCharts(QStringLiteral("node"), nodeFullName, value);
}

void RuntimeInterfaceView::updateCharts(const QString &bindType, const QString &key,
                                        const QVariant &value)
{
    const int typeId = value.metaType().id();
    if (typeId != QMetaType::Int && typeId != QMetaType::UInt
        && typeId != QMetaType::LongLong && typeId != QMetaType::ULongLong
        && typeId != QMetaType::Double && typeId != QMetaType::Float
        && typeId != QMetaType::Bool) {
        return;   // 官方图表数据只接受 int/float
    }
    double sample = value.toDouble();
    if (typeId == QMetaType::Bool)
        sample = value.toBool() ? 1.0 : 0.0;
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        if (m_widgets.at(i)->objectName() != QLatin1String("RuntimeLineChart"))
            continue;
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!ctrl)
            continue;
        auto *panel = static_cast<RuntimeChartPanel *>(m_widgets.at(i));
        for (int s = 0; s < ctrl->chartSeries.size() && s < panel->seriesCount(); ++s) {
            const ChartSeries &series = ctrl->chartSeries.at(s);
            if (series.bindType == bindType && series.bindKey == key)
                panel->appendSample(s, sample);
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
        // S6：合并式推送（latest wins）——高频轮次下避免 UI 线程逐帧深拷贝转换而积压
        m_imageViews[vi]->requestImage(image, nodeFullName);
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
    // 固定栏底色：先于子控件绘制，作为栏区域的视觉底板（官方 9.1 固定栏）
    {
        RuntimeInterfacePage *pg = m_current.page(m_current.currentPageIndex);
        const int topH = pg ? qMax(0, pg->topBarHeight) : 0;
        const int botH = pg ? qMax(0, pg->bottomBarHeight) : 0;
        const int leftW = pg ? qMax(0, pg->leftBarWidth) : 0;
        const int rightW = pg ? qMax(0, pg->rightBarWidth) : 0;
        if (topH || botH || leftW || rightW) {
            QPainter p(this);
            p.fillRect(QRect(0, 0, width(), topH), QColor(0x1e, 0x1e, 0x26));
            p.fillRect(QRect(0, height() - botH, width(), botH), QColor(0x1e, 0x1e, 0x26));
            p.fillRect(QRect(0, topH, leftW, qMax(0, height() - topH - botH)),
                       QColor(0x1e, 0x1e, 0x26));
            p.fillRect(QRect(width() - rightW, topH, rightW,
                             qMax(0, height() - topH - botH)),
                       QColor(0x1e, 0x1e, 0x26));
            p.setPen(QColor(0x3a, 0x3a, 0x4a));
            if (topH) p.drawLine(0, topH - 1, width(), topH - 1);
            if (botH) p.drawLine(0, height() - botH, width(), height() - botH);
            if (leftW) p.drawLine(leftW - 1, topH, leftW - 1, height() - botH);
            if (rightW) p.drawLine(width() - rightW, topH, width() - rightW, height() - botH);
        }
    }

    if (!m_staleBindings.isEmpty()) {
        QPainter p(this);
        p.setPen(QColor(220, 40, 40));
        QFont f = p.font();
        f.setPointSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(rect().adjusted(8, 4, -8, -rect().height() + 30),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("绑定失效：%1").arg(m_staleBindings.join(QStringLiteral("、"))));
    }
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

QRect RuntimeInterfaceView::controlGeometry(const RuntimeControl &ctrl)
{
    RuntimeInterfacePage *pg = m_current.page(m_current.currentPageIndex);
    const int topH = pg ? pg->topBarHeight : 0;
    const int botH = pg ? pg->bottomBarHeight : 0;
    const int leftW = pg ? pg->leftBarWidth : 0;
    const int rightW = pg ? pg->rightBarWidth : 0;
    const QRect g = ctrl.geometry;

    // 固定栏：厚度固定，控件按 1:1 相对该栏左上角摆放（不参与缩放）
    switch (ctrl.zone) {
    case RuntimeZone::TopBar:
        return QRect(QPoint(0, 0) + g.topLeft(), g.size());
    case RuntimeZone::BottomBar:
        return QRect(QPoint(0, height() - botH) + g.topLeft(), g.size());
    case RuntimeZone::LeftBar:
        return QRect(QPoint(0, topH) + g.topLeft(), g.size());
    case RuntimeZone::RightBar:
        return QRect(QPoint(width() - rightW, topH) + g.topLeft(), g.size());
    case RuntimeZone::Canvas:
        break;
    }

    // 画布区：扣掉四向固定栏后等比缩放
    QRect canvasRect(leftW, topH, qMax(1, width() - leftW - rightW),
                     qMax(1, height() - topH - botH));
    int designW = qMax(1, m_designSize.width() - leftW - rightW);
    int designH = qMax(1, m_designSize.height() - topH - botH);
    // 页内二级 Tab：标签条占画布区顶部一条，内容区相应让出高度
    if (pg && !pg->innerTabNames.isEmpty()) {
        canvasRect.setTop(canvasRect.top() + pg->innerTabHeight);
        canvasRect.setHeight(qMax(1, canvasRect.height() - pg->innerTabHeight));
        designH = qMax(1, designH - pg->innerTabHeight);
    }
    const double sx = double(canvasRect.width()) / designW;
    const double sy = double(canvasRect.height()) / designH;
    return QRect(canvasRect.x() + qRound(g.x() * sx), canvasRect.y() + qRound(g.y() * sy),
                 qMax(40, qRound(g.width() * sx)), qMax(24, qRound(g.height() * sy)));
}

void RuntimeInterfaceView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_tabs)
        m_tabs->setGeometry(rect());
    // 与 rebuild 同一套映射：固定栏 1:1、画布扣栏缩放、内层 Tab 让出标签条。
    // 旧实现 resize 走纯画布缩放，一次 resize 就把区域/Tab 偏移打回原形。
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        if (m_widgets[i] && m_widgetCtrls[i])
            m_widgets[i]->setGeometry(controlGeometry(*m_widgetCtrls[i]));
    }
    // 标签条宽度随窗口刷新（buildInnerTabBar 对已存在的条只更新几何/选中态）
    if (RuntimeInterfacePage *pg = m_current.page(m_current.currentPageIndex)) {
        QWidget *container = m_pageContainers.value(m_current.currentPageIndex);
        if (container && !pg->innerTabNames.isEmpty())
            buildInnerTabBar(container, *pg);
    }
}

void RuntimeInterfaceView::setKnownNodeNames(const QStringList &names)
{
    m_knownNodeNames = names;
    // 绑定体检（2026-10-27）：现场最容易踩的坑是"控件一直空白/旧值，不知道是没跑
    // 还是绑定没了"。这里把失效绑定列在页面顶部（红字），一眼可见。
    QSet<QString> globals, nodes;
    const auto vars = GlobalVariableManager::instance()->variables();
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        globals.insert(it.key());
    for (const QString &n : m_knownNodeNames)
        nodes.insert(n);
    m_staleBindings = m_current.staleBindings(globals, nodes);
    update();
}

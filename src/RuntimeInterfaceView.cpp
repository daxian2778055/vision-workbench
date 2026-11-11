#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX   // windows.h 的 min/max 宏会打断 QtCharts 的 qreal min() 声明
#  endif
#endif
#include "RuntimeInterfaceView.h"
#include "HalconWindow.h"
#include "GlobalVariableManager.h"
#include "GlobalTriggerManager.h"
#include "FlowExecutor.h"
#include "UiLogBus.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
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
#include <QLineEdit>
#include <QDateTime>
#include <QPixmap>
#include <QTimer>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QTimer>
#include <QInputDialog>
#include <QMessageBox>
#include <QDialog>
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>

namespace {
/// 图表控件公共接口：updateCharts 按此多态喂数，不再认 objectName。
class RuntimeChartBase : public QWidget
{
public:
    explicit RuntimeChartBase(QWidget *parent = nullptr) : QWidget(parent) {}
    virtual int seriesCount() const = 0;
    virtual void appendSample(int index, double value) = 0;
};

/// 线图阵列控件（官方 9.3 图表）：每个数据源一张线图，横轴=执行次数，纵轴=当前值。
/// Qt Charts 只在本文件使用，对外仍是普通 QWidget（objectName=RuntimeLineChart）。
class RuntimeChartPanel : public RuntimeChartBase
{
public:
    explicit RuntimeChartPanel(const RuntimeControl &ctrl, QWidget *parent = nullptr)
        : RuntimeChartBase(parent), m_ctrl(ctrl)
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

    int seriesCount() const override { return m_lines.size(); }

    void appendSample(int index, double value) override
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

/// 多数据线图控件（官方 9.3 图表）：多个数据源同绘一张线图，共享执行次数横轴。
class RuntimeMultiLinePanel : public RuntimeChartBase
{
public:
    explicit RuntimeMultiLinePanel(const RuntimeControl &ctrl, QWidget *parent = nullptr)
        : RuntimeChartBase(parent), m_ctrl(ctrl)
    {
        setObjectName(QStringLiteral("RuntimeMultiLineChart"));
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
        auto *chart = new QChart();
        chart->setBackgroundVisible(false);
        chart->legend()->setVisible(!ctrl.chartSeries.isEmpty());
        chart->legend()->setLabelColor(axisColor);
        m_axisX = new QValueAxis(chart);
        m_axisX->setRange(0, qMax(9, ctrl.chartHistoryLimit - 1));
        m_axisX->setLabelsColor(axisColor);
        m_axisX->setTitleText(QStringLiteral("执行次数"));
        chart->addAxis(m_axisX, Qt::AlignBottom);
        m_axisY = new QValueAxis(chart);
        m_axisY->setRange(0, 100);
        m_axisY->setLabelsColor(axisColor);
        chart->addAxis(m_axisY, Qt::AlignLeft);
        for (const ChartSeries &series : ctrl.chartSeries) {
            auto *line = new QLineSeries(chart);
            line->setName(series.name.isEmpty() ? series.bindKey : series.name);
            QPen pen(QColor(series.color));
            pen.setWidth(2);
            line->setPen(pen);
            chart->addSeries(line);
            line->attachAxis(m_axisX);
            line->attachAxis(m_axisY);
            m_lines.append(line);
            m_points.append(QVector<QPointF>());
            m_lastX.append(-1.0);   // 首点 x=0，与线图阵列口径一致
        }
        auto *view = new QChartView(chart, this);
        view->setObjectName(QStringLiteral("RuntimeMultiLineChartView0"));
        view->setRenderHint(QPainter::Antialiasing);
        view->setMinimumHeight(48);
        lay->addWidget(view, 1);
        if (ctrl.chartSeries.isEmpty()) {
            auto *hint = new QLabel(QStringLiteral("未配置数据系列"), this);
            hint->setAlignment(Qt::AlignCenter);
            hint->setStyleSheet(QStringLiteral("color:#8a8a9a;"));
            lay->addWidget(hint);
        }
    }

    int seriesCount() const override { return m_lines.size(); }

    void appendSample(int index, double value) override
    {
        if (index < 0 || index >= m_lines.size())
            return;
        QVector<QPointF> &pts = m_points[index];
        const double x = m_lastX[index] + 1.0;   // 该系列的绝对执行次数
        m_lastX[index] = x;
        pts.append(QPointF(x, value));
        const int limit = qBound(1, m_ctrl.chartHistoryLimit, 1000);
        while (pts.size() > limit)
            pts.removeFirst();
        m_lines[index]->replace(pts);
        updateAxes();
    }

private:
    /// 共享坐标轴自动量程：Y 取全部系列数据（含上下限）包络，X 取 [最早首点, 最晚末点]
    void updateAxes()
    {
        double lo = 0.0, hi = 0.0;
        bool hasY = false;
        double x0 = 0.0, x1 = 0.0;
        bool hasX = false;
        for (int i = 0; i < m_points.size(); ++i) {
            const ChartSeries &series = m_ctrl.chartSeries.at(i);
            if (series.showLimits) {
                if (!hasY) { lo = series.lower; hi = series.upper; hasY = true; }
                else { lo = qMin(lo, series.lower); hi = qMax(hi, series.upper); }
            }
            const QVector<QPointF> &pts = m_points.at(i);
            if (pts.isEmpty()) continue;
            if (!hasX) { x0 = pts.first().x(); x1 = pts.last().x(); hasX = true; }
            else { x0 = qMin(x0, pts.first().x()); x1 = qMax(x1, pts.last().x()); }
            for (const QPointF &p : pts) {
                if (!hasY) { lo = p.y(); hi = p.y(); hasY = true; }
                else { lo = qMin(lo, p.y()); hi = qMax(hi, p.y()); }
            }
        }
        if (!hasY) { lo = 0.0; hi = 100.0; }
        if (qAbs(hi - lo) < 1e-9) { lo -= 1.0; hi += 1.0; }
        const double pad = (hi - lo) * 0.1;
        m_axisY->setRange(lo - pad, hi + pad);
        if (!hasX) { x0 = 0.0; x1 = qMax(9, m_ctrl.chartHistoryLimit - 1); }
        m_axisX->setRange(x0, qMax(x0 + 1.0, x1));
    }

    RuntimeControl m_ctrl;
    QVector<QLineSeries *> m_lines;
    QVector<QVector<QPointF>> m_points;
    QVector<double> m_lastX;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;
};

/// 饼图控件（官方 9.3 图表）：各数据源最新当前值占整体的比例。
class RuntimePiePanel : public RuntimeChartBase
{
public:
    explicit RuntimePiePanel(const RuntimeControl &ctrl, QWidget *parent = nullptr)
        : RuntimeChartBase(parent)
    {
        setObjectName(QStringLiteral("RuntimePieChart"));
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
        auto *chart = new QChart();
        chart->setBackgroundVisible(false);
        chart->legend()->setVisible(!ctrl.chartSeries.isEmpty());
        chart->legend()->setLabelColor(axisColor);
        m_pie = new QPieSeries(chart);
        for (const ChartSeries &series : ctrl.chartSeries) {
            QPieSlice *slice = m_pie->append(
                series.name.isEmpty() ? series.bindKey : series.name, 0.0);
            slice->setColor(QColor(series.color));
            slice->setLabelVisible(true);
            slice->setLabelColor(axisColor);
            m_slices.append(slice);
        }
        chart->addSeries(m_pie);
        auto *view = new QChartView(chart, this);
        view->setObjectName(QStringLiteral("RuntimePieChartView0"));
        view->setRenderHint(QPainter::Antialiasing);
        view->setMinimumHeight(48);
        lay->addWidget(view, 1);
        if (ctrl.chartSeries.isEmpty()) {
            auto *hint = new QLabel(QStringLiteral("未配置数据系列"), this);
            hint->setAlignment(Qt::AlignCenter);
            hint->setStyleSheet(QStringLiteral("color:#8a8a9a;"));
            lay->addWidget(hint);
        }
    }

    int seriesCount() const override { return m_slices.size(); }

    void appendSample(int index, double value) override
    {
        if (index < 0 || index >= m_slices.size())
            return;
        m_slices[index]->setValue(value);   // 饼图只显示最新当前值的占比
    }

private:
    QPieSeries *m_pie = nullptr;
    QVector<QPieSlice *> m_slices;
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
    liveViews().append(this);
}

RuntimeInterfaceView::~RuntimeInterfaceView()
{
    liveViews().removeAll(this);
}

QList<RuntimeInterfaceView *> &RuntimeInterfaceView::liveViews()
{
    static QList<RuntimeInterfaceView *> views;
    return views;
}

void RuntimeInterfaceView::broadcastVariable(const QString &name, const QVariant &value)
{
    for (RuntimeInterfaceView *v : liveViews())
        if (v) v->updateVariable(name, value);
}

void RuntimeInterfaceView::broadcastNodeOutput(const QString &nodeFullName, const QVariant &value)
{
    for (RuntimeInterfaceView *v : liveViews())
        if (v) v->updateNodeOutput(nodeFullName, value);
}

void RuntimeInterfaceView::broadcastNodePortMap(const QString &nodeFullName,
                                                const QVariantMap &ports)
{
    for (RuntimeInterfaceView *v : liveViews())
        if (v) v->updateNodePortMap(nodeFullName, ports);
}

void RuntimeInterfaceView::broadcastImage(const QString &nodeFullName,
                                          const HalconCpp::HImage &image)
{
    for (RuntimeInterfaceView *v : liveViews())
        if (v) v->pushImage(nodeFullName, image);
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
    const QRect barGeo(leftW, topH, qMax(1, container->width() - leftW - rightW), stripH);

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
    m_multiViews.clear();
    m_multiCtrls.clear();
    m_nodeMultiImageMap.clear();
    m_statusLabels.clear();
    m_statusCtrls.clear();
    m_textEntryLabels.clear();
    m_textEntryCtrls.clear();
    m_textEntryRows.clear();
    m_nodeTextEntryMap.clear();
    if (m_statusTimer) m_statusTimer->stop();
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
            // 登记多图像（每画面一个 HalconWindow）
            else if (ctrl.type == RuntimeControlType::MultiImage) {
                for (int i = 0; i < ctrl.imagePanels.size(); ++i) {
                    auto *hw = w->findChild<HalconWindow *>(
                        QStringLiteral("RuntimeMultiImage_%1").arg(i));
                    if (!hw) continue;
                    m_multiViews.append(hw);
                    m_multiCtrls.append(&ctrl);
                    const QString key = ctrl.imagePanels.at(i).bindKey;
                    if (!key.isEmpty())
                        m_nodeMultiImageMap[key].append(m_multiViews.size() - 1);
                }
            }
            // 登记运行状态：控件根本身就是 QLabel，findChild 不含自身，需先试根
            else if (ctrl.type == RuntimeControlType::RunStatus) {
                auto *lbl = qobject_cast<QLabel *>(w);
                if (!lbl)
                    lbl = w->findChild<QLabel *>(QStringLiteral("RuntimeRunStatusLabel"));
                if (lbl) {
                    m_statusLabels.append(lbl);
                    m_statusCtrls.append(&ctrl);
                }
            }
            // 登记文本框组合（每行一个标签）
            else if (ctrl.type == RuntimeControlType::TextEntryCombo) {
                for (int i = 0; i < ctrl.textEntries.size(); ++i) {
                    auto *lbl = w->findChild<QLabel *>(
                        QStringLiteral("RuntimeTextEntry_%1").arg(i));
                    if (!lbl) continue;
                    m_textEntryLabels.append(lbl);
                    m_textEntryCtrls.append(&ctrl);
                    m_textEntryRows.append(i);
                    const TextEntryItem &e = ctrl.textEntries.at(i);
                    if (e.bindType == QLatin1String("node") && !e.bindKey.isEmpty())
                        m_nodeTextEntryMap[e.bindKey].append(m_textEntryLabels.size() - 1);
                }
            }
        }
    }

    // 运行状态：500ms 轮询执行器状态（无控件时不空转）
    if (!m_statusTimer) {
        m_statusTimer = new QTimer(this);
        m_statusTimer->setInterval(500);
        connect(m_statusTimer, &QTimer::timeout,
                this, &RuntimeInterfaceView::updateRunStatusLabels);
    }
    if (m_statusLabels.isEmpty()) m_statusTimer->stop();
    else { m_statusTimer->start(); updateRunStatusLabels(); }

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
    // 文本框组合每行初值（全局变量绑定；节点输出由上游推送）
    for (int k = 0; k < m_textEntryLabels.size(); ++k) {
        const RuntimeControl &ctrl = *m_textEntryCtrls[k];
        const TextEntryItem e = ctrl.textEntries.value(m_textEntryRows[k]);
        if (e.bindType == "global" && gv->variableExists(e.bindKey))
            updateTextEntryRow(k, gv->getVariable(e.bindKey));
    }
    // OK/NG 双路初值 + 条件信号灯初值（全局变量绑定）
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!ctrl) continue;
        if (ctrl->type == RuntimeControlType::OkNgCount) {
            if (!ctrl->bindKey.isEmpty() && gv->variableExists(ctrl->bindKey))
                updateVariable(ctrl->bindKey, gv->getVariable(ctrl->bindKey));
            if (!ctrl->bindKey2.isEmpty() && gv->variableExists(ctrl->bindKey2))
                updateVariable(ctrl->bindKey2, gv->getVariable(ctrl->bindKey2));
        } else if (ctrl->type == RuntimeControlType::ConditionalLight
                   && (ctrl->bindType.isEmpty()
                       || ctrl->bindType == QLatin1String("global"))
                   && !ctrl->bindKey.isEmpty() && gv->variableExists(ctrl->bindKey)) {
            updateVariable(ctrl->bindKey, gv->getVariable(ctrl->bindKey));
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
    case RuntimeControlType::SetVariable: {
        // 官方 9.3「设置变量」：运行界面把全局变量写成指定值
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(2);
        auto *title = new QLabel(ctrl.displayTitle(), host);
        title->setStyleSheet(QStringLiteral("font-size: 11px; color: #8a8a9a;"));
        lay->addWidget(title);

        auto *current = new QLabel(QStringLiteral("--"), host);
        current->setObjectName(QStringLiteral("RuntimeSetterCurrent"));
        current->setStyleSheet(QStringLiteral("font-size: 11px; color: #c0c0cc;"));
        lay->addWidget(current);

        auto *row = new QHBoxLayout();
        auto *edit = new QLineEdit(host);
        edit->setObjectName(QStringLiteral("RuntimeSetterEdit"));
        edit->setProperty("vfpGlobal", ctrl.bindKey);
        auto *apply = new QPushButton(QStringLiteral("写入"), host);
        apply->setObjectName(QStringLiteral("RuntimeSetterApply"));
        apply->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: white; border: none;"
            "  border-radius: 3px; padding: 3px 10px; }").arg(ctrl.color));
        row->addWidget(edit, 1);
        row->addWidget(apply);
        lay->addLayout(row);

        const QString globalName = ctrl.bindKey;
        auto commit = [current, edit, globalName]() {
            auto *gv = GlobalVariableManager::instance();
            if (globalName.isEmpty() || !gv->variableExists(globalName)) {
                current->setText(QStringLiteral("变量不存在：%1")
                                     .arg(globalName.isEmpty() ? QStringLiteral("(未绑定)")
                                                               : globalName));
                return;
            }
            const QString text = edit->text().trimmed();
            QVariant value;
            bool ok = true;
            switch (gv->getVariableType(globalName)) {
            case GlobalVariableManager::IntType: {
                const int v = text.toInt(&ok);
                value = v;
                break;
            }
            case GlobalVariableManager::FloatType: {
                const double v = text.toDouble(&ok);
                value = v;
                break;
            }
            case GlobalVariableManager::BoolType: {
                const QString t = text.toLower();
                const bool isTrue = (t == QLatin1String("true") || t == QLatin1String("1")
                                     || t == QLatin1String("on") || t == QStringLiteral("是"));
                const bool isFalse = (t == QLatin1String("false") || t == QLatin1String("0")
                                      || t == QLatin1String("off") || t == QStringLiteral("否"));
                ok = isTrue || isFalse;
                value = isTrue;
                break;
            }
            case GlobalVariableManager::StringType:
                value = text;
                break;
            }
            if (!ok) {
                current->setText(QStringLiteral("输入无效：%1").arg(text));
                return;
            }
            if (gv->setVariable(globalName, value))
                current->setText(QStringLiteral("当前值：%1").arg(RuntimeInterfaceView::cellText(value)));
            else
                current->setText(QStringLiteral("写入失败：%1").arg(globalName));
        };
        connect(apply, &QPushButton::clicked, host, commit);
        connect(edit, &QLineEdit::returnPressed, host, commit);

        auto *gv = GlobalVariableManager::instance();
        if (!globalName.isEmpty() && gv->variableExists(globalName)) {
            const QVariant v = gv->getVariable(globalName);
            current->setText(QStringLiteral("当前值：%1").arg(cellText(v)));
            edit->setText(v.toString());
        } else if (!globalName.isEmpty()) {
            current->setText(QStringLiteral("变量不存在：%1").arg(globalName));
        }
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::RunControl: {
        // 官方 9.3「运行控制」：开始 / 停止 / 单次，直接触发流程动作
        auto *host = new QWidget();
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);
        const QString ids[3] = {QStringLiteral("start"), QStringLiteral("stop"),
                                QStringLiteral("single")};
        const QString names[3] = {QStringLiteral("开始"), QStringLiteral("停止"),
                                  QStringLiteral("单次")};
        const QString objects[3] = {QStringLiteral("RuntimeRunStart"),
                                    QStringLiteral("RuntimeRunStop"),
                                    QStringLiteral("RuntimeRunSingle")};
        for (int i = 0; i < 3; ++i) {
            auto *b = new QPushButton(names[i], host);
            b->setObjectName(objects[i]);
            const QString bg = (i == 1) ? QStringLiteral("#b04a4a") : ctrl.color;
            b->setStyleSheet(QStringLiteral(
                "QPushButton { background-color: %1; color: white; border: none;"
                "  border-radius: 4px; font-size: %2px; padding: 4px 10px; }")
                .arg(bg).arg(qMax(9, ctrl.fontSize - 4)));
            const QString id = ids[i];
            connect(b, &QPushButton::clicked, this,
                    [this, id]() { emit actionTriggered(id); });
            lay->addWidget(b, 1);
        }
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::OkNgCount: {
        // 官方 9.3「OK/NG」：两路计数（bindKey=OK，bindKey2=NG，均绑全局变量）
        auto *host = new QWidget();
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);
        auto makeSide = [host](const QString &objectName, const QString &caption,
                               const QString &color, const QString &globalName) {
            auto *box = new QLabel(caption + QStringLiteral("\n--"), host);
            box->setObjectName(objectName);
            box->setAlignment(Qt::AlignCenter);
            box->setStyleSheet(QStringLiteral(
                "QLabel { background:#20262a; color:%1; font-size:18px; font-weight:bold;"
                " border:1px solid #3a3a5c; border-radius:4px; }").arg(color));
            box->setProperty("vfpGlobal", globalName);
            return box;
        };
        lay->addWidget(makeSide(QStringLiteral("RuntimeOkValue"), QStringLiteral("OK"),
                                QStringLiteral("#3db97a"), ctrl.bindKey), 1);
        lay->addWidget(makeSide(QStringLiteral("RuntimeNgValue"), QStringLiteral("NG"),
                                QStringLiteral("#e74c4c"), ctrl.bindKey2), 1);
        return host;
    }
    case RuntimeControlType::ConditionalLight: {
        // 官方 9.3「条件信号灯」：按数值条件切换灯色与文本
        auto *host = new QWidget();
        auto *lay = new QHBoxLayout(host);
        lay->setContentsMargins(8, 4, 8, 4);
        lay->setSpacing(8);
        auto *light = new QLabel(host);
        light->setObjectName(QStringLiteral("RuntimeCondLight"));
        light->setFixedSize(26, 26);
        light->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 13px;")
                                 .arg(ctrl.color));
        auto *text = new QLabel(ctrl.displayTitle(), host);
        text->setObjectName(QStringLiteral("RuntimeCondText"));
        text->setWordWrap(true);
        text->setStyleSheet(QStringLiteral("font-size: %1px; color: #e0e0e0;")
                                .arg(ctrl.fontSize));
        lay->addWidget(light);
        lay->addWidget(text, 1);
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #22222a; border: 1px solid #3a3a5c; border-radius: 4px; }"));
        return host;
    }
    case RuntimeControlType::MultiImage: {
        auto *host = new QWidget();
        auto *grid = new QGridLayout(host);
        grid->setContentsMargins(2, 2, 2, 2);
        grid->setSpacing(2);
        const int n = qMax(1, ctrl.imagePanels.size());
        int cols = n;
        if (ctrl.multiImageLayout == 1) cols = 1;
        else if (ctrl.multiImageLayout == 2) cols = qMax(1, (n + 1) / 2);
        for (int i = 0; i < n; ++i) {
            auto *cell = new QWidget(host);
            auto *cl = new QVBoxLayout(cell);
            cl->setContentsMargins(0, 0, 0, 0);
            cl->setSpacing(0);
            // 空画面表（手工 JSON / addControl 直建）退化成单个占位画面，不越界取名字
            const QString name = multiImagePanelName(ctrl.imagePanels, i);
            auto *title = new QLabel(name, cell);
            title->setStyleSheet(QStringLiteral(
                "QLabel { background-color: rgba(30,30,46,220); color: #d8d8e0;"
                " padding: 1px 6px; font-size: 11px; }"));
            auto *hw = new HalconWindow(cell);
            hw->setObjectName(QStringLiteral("RuntimeMultiImage_%1").arg(i));
            cl->addWidget(title);
            cl->addWidget(hw, 1);
            grid->addWidget(cell, i / cols, i % cols);
        }
        host->setStyleSheet(QStringLiteral(
            "QWidget { background-color: #1e1e21; border: 1px solid #3a3a5c; }"));
        return host;
    }
    case RuntimeControlType::RunStatus: {
        auto *label = new QLabel(QStringLiteral("停止运行"), nullptr);
        label->setObjectName(QStringLiteral("RuntimeRunStatusLabel"));
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral(
            "QLabel { background-color: %1; color: %2; font-size: %3px; font-weight: bold;"
            " border: 1px solid #3a3a5c; border-radius: %4px; }")
            .arg(ctrl.idleBgColor, ctrl.idleFgColor)
            .arg(ctrl.fontSize).arg(ctrl.cornerRadius));
        return label;
    }
    case RuntimeControlType::UiLog: {
        auto *host = new QWidget();
        auto *lay = new QVBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(0);
        if (ctrl.logShowTitle) {
            auto *title = new QLabel(ctrl.displayTitle(), host);
            title->setStyleSheet(QStringLiteral(
                "QLabel { background-color: %1; color: %2; padding: 2px 8px; font-size: 11px; }")
                .arg(ctrl.logHeaderBgColor, ctrl.logFontColor));
            lay->addWidget(title);
        }
        auto *table = new QTableWidget(0, 1, host);
        table->setObjectName(QStringLiteral("RuntimeUiLogTable"));
        table->setHorizontalHeaderLabels({QStringLiteral("日志")});
        table->verticalHeader()->setVisible(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setAlternatingRowColors(true);
        table->horizontalHeader()->setStyleSheet(QStringLiteral(
            "QHeaderView::section { background-color: %1; color: %2; border: none; }")
            .arg(ctrl.logHeaderBgColor, ctrl.logFontColor));
        table->setStyleSheet(QStringLiteral(
            "QTableWidget { background-color: %1; alternate-background-color: %2;"
            " color: %3; border: 1px solid #3a3a5c; font-size: 11px; }")
            .arg(ctrl.logRowBgColor, ctrl.logAltRowBgColor, ctrl.logFontColor));
        for (const QString &line : UiLogBus::instance()->recent(ctrl.logMaxLines)) {
            const int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(line));
        }
        const int maxLines = qMax(10, ctrl.logMaxLines);
        connect(UiLogBus::instance(), &UiLogBus::messageLogged, table,
                [table, maxLines](const QString &msg) {
            const int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(msg));
            while (table->rowCount() > maxLines)
                table->removeRow(0);
            table->scrollToBottom();
        });
        lay->addWidget(table, 1);
        return host;
    }
    case RuntimeControlType::TextEntryCombo: {
        auto *host = new QWidget();
        QBoxLayout *lay = ctrl.textEntryOrientation == 1
                              ? static_cast<QBoxLayout *>(new QHBoxLayout(host))
                              : static_cast<QBoxLayout *>(new QVBoxLayout(host));
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(qMax(0, ctrl.textEntrySpacing));
        if (ctrl.showTextEntryTitle) {
            auto *title = new QLabel(ctrl.displayTitle(), host);
            title->setStyleSheet(QStringLiteral(
                "QLabel { background-color: %1; color: #d8d8e0; padding: 2px 8px;"
                " font-size: 11px; }").arg(ctrl.textEntryHeaderBgColor));
            lay->addWidget(title);
        }
        const int n = qMax(1, ctrl.textEntries.size());
        for (int i = 0; i < n; ++i) {
            const bool hasEntry = i < ctrl.textEntries.size();
            auto *row = new QLabel(host);
            row->setObjectName(QStringLiteral("RuntimeTextEntry_%1").arg(i));
            row->setWordWrap(true);
            if (hasEntry) {
                const TextEntryItem &e = ctrl.textEntries.at(i);
                row->setText(e.text.isEmpty() ? textEntryEmptyText() : e.text);
                row->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(e.color));
            } else {
                row->setText(textEntryEmptyText());
            }
            lay->addWidget(row, ctrl.textEntryOrientation == 1 ? 0 : 1);
        }
        QString sheet;
        if (!ctrl.textEntryBgColor.isEmpty())
            sheet += QStringLiteral("background-color: %1;").arg(ctrl.textEntryBgColor);
        if (!ctrl.textEntryBorderColor.isEmpty())
            sheet += QStringLiteral("border: 1px solid %1;").arg(ctrl.textEntryBorderColor);
        if (!sheet.isEmpty()) host->setStyleSheet(QStringLiteral("QWidget { %1 }").arg(sheet));
        return host;
    }
    case RuntimeControlType::SubInterface: {
        auto *btn = new QPushButton(ctrl.displayTitle());
        btn->setObjectName(QStringLiteral("RuntimeSubInterfaceButton"));
        if (ctrl.subIconVisible) btn->setText(QStringLiteral("▣  ") + ctrl.displayTitle());
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; color: %2; font-size: %3px;"
            " border: 1px solid %4; border-radius: %5px; }"
            "QPushButton:pressed { background-color: %6; }")
            .arg(ctrl.bgColor.isEmpty() ? QStringLiteral("#2a2a33") : ctrl.bgColor,
                 ctrl.color)
            .arg(ctrl.fontSize)
            .arg(ctrl.borderColor.isEmpty() ? QStringLiteral("#3a3a5c") : ctrl.borderColor)
            .arg(ctrl.cornerRadius)
            .arg(ctrl.pressedColor.isEmpty() ? QStringLiteral("#3a3a5c") : ctrl.pressedColor));
        const QString subId = ctrl.subPageId;
        const QString subPwd = ctrl.subPassword;
        const QString subTitle = ctrl.displayTitle();
        connect(btn, &QPushButton::clicked, this, [this, subId, subPwd, subTitle] {
            if (m_subLevel >= 3) {
                QMessageBox::warning(this, QStringLiteral("子界面"),
                                     QStringLiteral("运行界面最多嵌套 3 层子界面。"));
                return;
            }
            RuntimeInterfacePage *pg = m_current.subPage(subId);
            if (!pg) {
                QMessageBox::warning(this, QStringLiteral("子界面"),
                                     QStringLiteral("子界面页不存在（可能已被删除）。"));
                return;
            }
            if (!subPwd.isEmpty()) {
                bool ok = false;
                const QString input = QInputDialog::getText(
                    this, QStringLiteral("子界面密码"), QStringLiteral("请输入密码："),
                    QLineEdit::Password, QString(), &ok);
                // 常量时间校验摘要（2026-11-11）：布局文件里已不存明文密码
                if (!ok || !verifySubPassword(input, subPwd)) return;
            }
            RuntimeInterface sub;
            sub.pages = {*pg};
            sub.subPages = m_current.subPages;
            sub.canvas = m_current.canvas;
            auto *dlg = new QDialog(this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(subTitle);
            dlg->resize(qMin(1100, qMax(640, m_current.canvas.width)),
                        qMin(820, qMax(480, m_current.canvas.height)));
            auto *lay = new QVBoxLayout(dlg);
            lay->setContentsMargins(0, 0, 0, 0);
            auto *view = new RuntimeInterfaceView(dlg);
            view->setSubLevel(m_subLevel + 1);
            view->setKnownNodeNames(m_knownNodeNames);
            view->setInterface(sub);
            lay->addWidget(view);
            dlg->show();
        });
        return btn;
    }
    case RuntimeControlType::LineChart:
        return new RuntimeChartPanel(ctrl);
    case RuntimeControlType::MultiLineChart:
        return new RuntimeMultiLinePanel(ctrl);
    case RuntimeControlType::PieChart:
        return new RuntimePiePanel(ctrl);
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

namespace {
bool lightConditionMatches(const LightCondition &cond, double value)
{
    switch (cond.op) {
    case 0: return value >= cond.threshold;
    case 1: return value > cond.threshold;
    case 2: return value <= cond.threshold;
    case 3: return value < cond.threshold;
    case 4: return value == cond.threshold;
    case 5: return value != cond.threshold;
    default: return false;
    }
}

/// 条件信号灯求值：按顺序取首个满足的条件；都不满足则用控件主色与标题。
void applyConditionalLight(QLabel *light, QLabel *text, const RuntimeControl &ctrl,
                           const QVariant &value)
{
    bool ok = false;
    double v = value.toDouble(&ok);
    if (!ok && value.metaType().id() == QMetaType::Bool) {
        v = value.toBool() ? 1.0 : 0.0;
        ok = true;
    }
    QString color = ctrl.color;
    QString label = ctrl.displayTitle();
    if (ok) {
        for (const LightCondition &cond : ctrl.lightConditions) {
            if (!lightConditionMatches(cond, v)) continue;
            if (QColor(cond.color).isValid()) color = cond.color;
            if (!cond.text.isEmpty()) label = cond.text;
            break;
        }
    }
    if (light)
        light->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 13px;")
                                 .arg(color));
    if (text) text->setText(label);
}
}   // namespace

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
    // OK/NG、条件信号灯、设置变量回显（按控件扫描，类型各异不另建索引表）
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!ctrl) continue;
        if (ctrl->type == RuntimeControlType::OkNgCount) {
            if (ctrl->bindKey == name) {
                if (auto *ok = m_widgets.at(i)->findChild<QLabel *>(
                        QStringLiteral("RuntimeOkValue")))
                    ok->setText(QStringLiteral("OK\n%1").arg(cellText(value)));
            }
            if (!ctrl->bindKey2.isEmpty() && ctrl->bindKey2 == name) {
                if (auto *ng = m_widgets.at(i)->findChild<QLabel *>(
                        QStringLiteral("RuntimeNgValue")))
                    ng->setText(QStringLiteral("NG\n%1").arg(cellText(value)));
            }
        } else if (ctrl->type == RuntimeControlType::ConditionalLight
                   && (ctrl->bindType.isEmpty()
                       || ctrl->bindType == QLatin1String("global"))
                   && ctrl->bindKey == name) {
            applyConditionalLight(
                m_widgets.at(i)->findChild<QLabel *>(QStringLiteral("RuntimeCondLight")),
                m_widgets.at(i)->findChild<QLabel *>(QStringLiteral("RuntimeCondText")),
                *ctrl, value);
        } else if (ctrl->type == RuntimeControlType::SetVariable
                   && (ctrl->bindType.isEmpty()
                       || ctrl->bindType == QLatin1String("global"))
                   && ctrl->bindKey == name) {
            if (auto *cur = m_widgets.at(i)->findChild<QLabel *>(
                    QStringLiteral("RuntimeSetterCurrent")))
                cur->setText(QStringLiteral("当前值：%1").arg(cellText(value)));
        }
    }
    // 文本框组合（全局变量绑定的行）
    for (int k = 0; k < m_textEntryLabels.size(); ++k) {
        const RuntimeControl &ctrl = *m_textEntryCtrls[k];
        const TextEntryItem e = ctrl.textEntries.value(m_textEntryRows[k]);
        if (e.bindType == QLatin1String("global") && e.bindKey == name)
            updateTextEntryRow(k, value);
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
    // 条件信号灯（节点输出绑定）
    for (int i = 0; i < m_widgets.size() && i < m_widgetCtrls.size(); ++i) {
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!ctrl || ctrl->type != RuntimeControlType::ConditionalLight) continue;
        if (ctrl->bindType != QLatin1String("node") || ctrl->bindKey != nodeFullName) continue;
        applyConditionalLight(
            m_widgets.at(i)->findChild<QLabel *>(QStringLiteral("RuntimeCondLight")),
            m_widgets.at(i)->findChild<QLabel *>(QStringLiteral("RuntimeCondText")),
            *ctrl, value);
    }
    const auto tit = m_nodeTextEntryMap.constFind(nodeFullName);
    if (tit != m_nodeTextEntryMap.constEnd()) {
        for (int k : tit.value()) {
            if (k >= 0 && k < m_textEntryLabels.size()) updateTextEntryRow(k, value);
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
        auto *panel = dynamic_cast<RuntimeChartBase *>(m_widgets.at(i));
        if (!panel) continue;
        const RuntimeControl *ctrl = m_widgetCtrls.at(i);
        if (!ctrl)
            continue;
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
    if (!image.IsInitialized()) return;
    const auto it = m_nodeImageMap.constFind(nodeFullName);
    if (it != m_nodeImageMap.constEnd()) {
        for (int vi : it.value()) {
            if (vi < 0 || vi >= m_imageViews.size()) continue;
            // S6：合并式推送（latest wins）——高频轮次下避免 UI 线程逐帧深拷贝转换而积压
            m_imageViews[vi]->requestImage(image, nodeFullName);
        }
    }
    // 多图像：同一节点可同时供给多个画面
    const auto mit = m_nodeMultiImageMap.constFind(nodeFullName);
    if (mit != m_nodeMultiImageMap.constEnd()) {
        for (int vi : mit.value()) {
            if (vi < 0 || vi >= m_multiViews.size()) continue;
            m_multiViews[vi]->requestImage(image, nodeFullName);
        }
    }
}

void RuntimeInterfaceView::updateRunStatusLabels()
{
    if (m_statusLabels.isEmpty()) return;
    auto *gtm = GlobalTriggerManager::instance();
    for (int i = 0; i < m_statusCtrls.size() && i < m_statusLabels.size(); ++i) {
        const RuntimeControl &ctrl = *m_statusCtrls[i];
        auto busyState = [](ExecutionState st) {
            return st == ExecutionState::Running || st == ExecutionState::Paused;
        };
        bool busy = false;
        if (ctrl.statusSource == QLatin1String("flow") && !ctrl.statusFlow.isEmpty()) {
            if (FlowExecutor *ex = gtm->executorForFlow(ctrl.statusFlow))
                busy = busyState(ex->getState());
        } else {
            for (const QString &flow : gtm->registeredFlowNames()) {
                if (FlowExecutor *ex = gtm->executorForFlow(flow)) {
                    if (busyState(ex->getState())) { busy = true; break; }
                }
            }
        }
        QLabel *label = m_statusLabels[i];
        label->setText(busy ? QStringLiteral("运行中") : QStringLiteral("停止运行"));
        label->setStyleSheet(QStringLiteral(
            "QLabel { background-color: %1; color: %2; font-size: %3px; font-weight: bold;"
            " border: 1px solid #3a3a5c; border-radius: %4px; }")
            .arg(busy ? ctrl.busyBgColor : ctrl.idleBgColor,
                 busy ? ctrl.busyFgColor : ctrl.idleFgColor)
            .arg(ctrl.fontSize).arg(ctrl.cornerRadius));
    }
}

void RuntimeInterfaceView::updateValueLabel(QLabel *label, const RuntimeControl &ctrl, const QVariant &value)
{
    if (!label) return;
    label->setText(cellText(value));
    label->setStyleSheet(QStringLiteral("font-size: %1px; font-weight: bold; color: %2;")
                             .arg(ctrl.fontSize).arg(ctrl.color));
}

void RuntimeInterfaceView::updateTextEntryRow(int index, const QVariant &value)
{
    if (index < 0 || index >= m_textEntryLabels.size() || index >= m_textEntryCtrls.size())
        return;
    QLabel *label = m_textEntryLabels.at(index);
    const RuntimeControl &ctrl = *m_textEntryCtrls.at(index);
    const TextEntryItem e = ctrl.textEntries.value(m_textEntryRows.value(index));
    const bool bound = !e.bindType.isEmpty() && !e.bindKey.isEmpty();
    label->setText(formatTextEntry(e, value, bound));
    label->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                             .arg(textEntryDisplayColor(e, value, bound)));
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

QSize RuntimeInterfaceView::pageBaseSize() const
{
    if (m_tabs) {
        QWidget *c = m_pageContainers.value(m_current.currentPageIndex);
        if (c && !c->size().isEmpty())
            return c->size();
    }
    return size();
}

QRect RuntimeInterfaceView::controlGeometry(const RuntimeControl &ctrl)
{
    RuntimeInterfacePage *pg = m_current.page(m_current.currentPageIndex);
    const int topH = pg ? pg->topBarHeight : 0;
    const int botH = pg ? pg->bottomBarHeight : 0;
    const int leftW = pg ? pg->leftBarWidth : 0;
    const int rightW = pg ? pg->rightBarWidth : 0;
    const QRect g = ctrl.geometry;
    const QSize base = pageBaseSize();

    // 固定栏：厚度固定，控件按 1:1 相对该栏左上角摆放（不参与缩放）
    switch (ctrl.zone) {
    case RuntimeZone::TopBar:
        return QRect(QPoint(0, 0) + g.topLeft(), g.size());
    case RuntimeZone::BottomBar:
        return QRect(QPoint(0, base.height() - botH) + g.topLeft(), g.size());
    case RuntimeZone::LeftBar:
        return QRect(QPoint(0, topH) + g.topLeft(), g.size());
    case RuntimeZone::RightBar:
        return QRect(QPoint(base.width() - rightW, topH) + g.topLeft(), g.size());
    case RuntimeZone::Canvas:
        break;
    }

    // 画布区：扣掉四向固定栏后等比缩放
    QRect canvasRect(leftW, topH, qMax(1, base.width() - leftW - rightW),
                     qMax(1, base.height() - topH - botH));
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

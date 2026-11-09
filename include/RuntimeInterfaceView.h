#pragma once

#include <QWidget>
#include <QList>
#include <QMap>
#include <QHash>
#include <QVariantMap>

#include "RuntimeInterface.h"
#include <halconcpp/HalconCpp.h>

class QLabel;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;
class HalconWindow;

/// 运行模式下的自定义运行界面：按 RuntimeInterface 布局实时渲染控件（多页 Tab）
class RuntimeInterfaceView : public QWidget
{
    Q_OBJECT

public:
    explicit RuntimeInterfaceView(QWidget *parent = nullptr);

    /// 载入布局并重建控件（设计器应用/进入运行模式时调用）
    void setInterface(const RuntimeInterface &layout);

    /// 更新绑定到指定节点名的图像控件
    void pushImage(const QString &nodeFullName, const HalconCpp::HImage &image);
    /// 更新绑定到指定全局变量的值（自动匹配数值/状态灯/结果表格列）
    void updateVariable(const QString &name, const QVariant &value);
    /// 更新节点输出值（ValueDisplay/StatusLight 绑定 node；并喂给结果表格的节点列）
    void updateNodeOutput(const QString &nodeFullName, const QVariant &value);
    /// 更新节点全部输出端口（按端口名取数；IO 状态控件消费"成功/值/错误"）
    void updateNodePortMap(const QString &nodeFullName, const QVariantMap &ports);

    /// 控件数量（用于判断是否已配置运行界面）
    int controlCount() const { return m_current.totalControlCount(); }

    /// 切到指定名称的页面（官方「界面链接」动作用）；找不到则忽略。
    void showPage(const QString &pageName);

    /// 参数设置控件回填当前值：key = "节点完整名|参数键"。
    /// 回填时不触发 parameterChangeRequested（QSignalBlocker），避免写回循环。
    void setParameterValues(const QHash<QString, double> &values);
    /// 组合框回填：key = "节点完整名|参数键"，value 为应选中的候选文本。
    void setComboSelections(const QHash<QString, QString> &selections);

    /// 按控件所属区域算实际位置（纯计算，导出便于直接测）：
    /// 画布区扣掉四向固定栏后等比缩放；固定栏厚度固定、控件 1:1 不缩放。
    QRect controlGeometry(const RuntimeControl &ctrl);

    /// 提供当前流程的节点名（2026-10-27）：配合"绑定失效红显"。
    /// 设计器与运行态共用 RuntimeInterface::staleBindings 判据，避免两边口径不一。
    void setKnownNodeNames(const QStringList &names);

signals:
    /// 按钮动作触发：actionId 为 "start"/"stop"/"single"/"trigger:<流程名>"
    void actionTriggered(const QString &actionId);
    /// 官方动作表（B2-1）：逐行上报命令类型 / 方法 / 输入参数。
    /// commandType: ui / param / scheme / exec / reset；exec 仍由上面的旧信号处理。
    void runtimeActionTriggered(const QString &commandType, const QString &method,
                                const QString &param);
    /// 参数设置控件（官方 9.3）：nodeName + paramKey 定位节点参数，value 为新值。
    void parameterChangeRequested(const QString &nodeName, const QString &paramKey,
                                  double value);
    /// 开关控制（官方 9.3）：订阅节点 bool 参数，控制其是否启用。
    void switchChangeRequested(const QString &nodeName, const QString &paramKey,
                               bool enabled);
    /// 组合框（官方 9.3）：在候选值之间切换，选中后按字符串写入参数。
    void comboChangeRequested(const QString &nodeName, const QString &paramKey,
                              const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    /// 结果表格待提交行落表（150ms 合并窗口，约等于按轮一行）
    void commitTableRows();

private:
    QStringList m_knownNodeNames;   /// 已知节点名（绑定体检用）
    QHash<QString, double> m_paramValues;   /// 参数设置回填值，"节点完整名|参数键" → 值
    QHash<QString, QString> m_comboSelections;   /// 组合框回填，"节点完整名|参数键" → 选中文本
    /// 页内二级 Tab（官方 9.3「Tab页」，页级实现）：当前选中的内层标签
    int m_innerTab = 0;
    /// 每页各自记住选中的内层标签（页索引 → 标签索引），切页不串台
    QHash<int, int> m_innerTabByPage;
    /// 多页 QTabWidget 切页：同步 currentPageIndex 并按该页内层 Tab 刷新
    void onCurrentPageChanged(int index);
    /// 按当前内层标签显隐控件（只作用于当前页；其它页照常，切页自然生效）
    void applyInnerTabVisibility();
    /// 把一次执行的值追加到匹配的图表数据系列（线图/多数据线图/饼图；只接受 int/float/bool）
    void updateCharts(const QString &bindType, const QString &key, const QVariant &value);
    /// 在当前页顶部建内层标签条（页未启用内层 Tab 时什么都不做）
    void buildInnerTabBar(QWidget *container, const RuntimeInterfacePage &pg);
    QStringList m_staleBindings;    /// 失效绑定（绘制在页面顶部）
    void rebuildWidgets();
    void clearWidgets();
    QWidget *createControlWidget(const RuntimeControl &ctrl);
    QWidget *createLabelWidget(const RuntimeControl &ctrl);
    QWidget *createTableWidget(const RuntimeControl &ctrl);
    QWidget *createIoStatusWidget(const RuntimeControl &ctrl);
    void updateValueLabel(QLabel *label, const RuntimeControl &ctrl, const QVariant &value);
    void updateStatusLight(QLabel *light, const RuntimeControl &ctrl, const QVariant &value);
    static QString cellText(const QVariant &value);

    RuntimeInterface m_current;
    QTabWidget *m_tabs = nullptr;                /// 多页时使用（单页直接铺在本控件上）
    QList<QWidget *> m_pageContainers;           /// 每页一个容器（控件父级）
    QList<QWidget *> m_widgets;                  /// 全部控件（清理用）
    QList<const RuntimeControl *> m_widgetCtrls; /// 与 m_widgets 平行：等比缩放用
    QSize m_designSize{1200, 760};               /// 设计画布基准（缩放源）

    QList<HalconWindow *> m_imageViews;          /// 图像控件
    QList<const RuntimeControl *> m_imageCtrls;
    QList<QLabel *> m_valueLabels;               /// 数值/文本控件
    QList<const RuntimeControl *> m_valueCtrls;
    QList<QLabel *> m_lightLabels;               /// 状态灯控件
    QList<const RuntimeControl *> m_lightCtrls;

    QList<QTableWidget *> m_tables;              /// 结果表格控件
    QList<const RuntimeControl *> m_tableCtrls;
    QList<QVariantMap> m_tableCurrent;           /// 每表当前行（列索引→最新值）
    QList<bool> m_tableDirty;
    QList<QTimer *> m_tableTimers;

    QList<QLabel *> m_ioLights;                  /// IO 状态控件（灯 + 文本）
    QList<const RuntimeControl *> m_ioCtrls;
    QList<QLabel *> m_ioTexts;

    QMap<QString, QList<int>> m_nodeImageMap;    /// 节点名 -> 图像控件位置
    QMap<QString, QList<int>> m_nodeValueMap;    /// 节点名 -> 数值控件位置
    QMap<QString, QList<int>> m_nodeLightMap;    /// 节点名 -> 状态灯控件位置
    QMap<QString, QList<int>> m_nodeIoMap;       /// 节点名 -> IO 状态控件位置
};

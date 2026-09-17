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

signals:
    /// 按钮动作触发：actionId 为 "start"/"stop"/"single"/"trigger:<流程名>"
    void actionTriggered(const QString &actionId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    /// 结果表格待提交行落表（150ms 合并窗口，约等于按轮一行）
    void commitTableRows();

private:
    void rebuildWidgets();
    void clearWidgets();
    QWidget *createControlWidget(const RuntimeControl &ctrl);
    QWidget *createLabelWidget(const RuntimeControl &ctrl);
    QWidget *createTableWidget(const RuntimeControl &ctrl);
    QWidget *createIoStatusWidget(const RuntimeControl &ctrl);
    void updateValueLabel(QLabel *label, const RuntimeControl &ctrl, const QVariant &value);
    void updateStatusLight(QLabel *light, const RuntimeControl &ctrl, const QVariant &value);
    void feedTables(const RuntimeControl &ctrl, const QVariant &value);
    static QString cellText(const QVariant &value);

    RuntimeInterface m_current;
    QTabWidget *m_tabs = nullptr;                /// 多页时使用（单页直接铺在本控件上）
    QList<QWidget *> m_pageContainers;           /// 每页一个容器（控件父级）
    QList<QWidget *> m_widgets;                  /// 全部控件（清理用）

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

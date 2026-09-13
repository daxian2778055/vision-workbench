#pragma once

#include <QWidget>
#include <QList>
#include <QMap>
#include <QHash>

#include "RuntimeInterface.h"
#include <halconcpp/HalconCpp.h>

class QLabel;
class QPushButton;
class HalconWindow;

/// 运行模式下的自定义运行界面：按 RuntimeInterface 布局实时渲染控件
class RuntimeInterfaceView : public QWidget
{
    Q_OBJECT

public:
    explicit RuntimeInterfaceView(QWidget *parent = nullptr);

    /// 载入布局并重建控件（设计器应用/进入运行模式时调用）
    void setInterface(const RuntimeInterface &layout);

    /// 更新绑定到指定节点名的图像控件
    void pushImage(const QString &nodeFullName, const HalconCpp::HImage &image);
    /// 更新绑定到指定全局变量的值（自动匹配数值/状态灯控件）
    void updateVariable(const QString &name, const QVariant &value);
    /// 更新节点输出值（ValueDisplay/StatusLight 绑定 node）
    void updateNodeOutput(const QString &nodeFullName, const QVariant &value);

    /// 控件数量（用于判断是否已配置运行界面）
    int controlCount() const { return m_current.controls.size(); }

signals:
    /// 按钮动作触发：actionId 为 "start"/"stop"/"single"/"trigger:<流程名>"
    void actionTriggered(const QString &actionId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildWidgets();
    void clearWidgets();
    QWidget *createControlWidget(const RuntimeControl &ctrl);
    QWidget *createLabelWidget(const RuntimeControl &ctrl);
    void updateValueLabel(QLabel *label, const RuntimeControl &ctrl, const QVariant &value);
    void updateStatusLight(QLabel *light, const RuntimeControl &ctrl, const QVariant &value);
    void paintTextOnImage(QWidget *imgHost, const RuntimeControl &ctrl, const QString &text);

    RuntimeInterface m_current;
    QList<QWidget *> m_widgets;
    QList<HalconWindow *> m_imageViews;          /// 图像控件（按索引对应 m_current.controls）
    QList<int> m_imageControlIndex;              /// 图像控件在 m_current.controls 中的索引
    QList<QLabel *> m_valueLabels;               /// 数值/文本/状态灯控件（按索引对应）
    QList<int> m_valueControlIndex;              /// 值控件在 m_current.controls 中的索引
    QList<QLabel *> m_lightLabels;               /// 状态灯控件
    QList<int> m_lightControlIndex;
    QMap<QString, QList<int>> m_nodeImageMap;    /// 节点名 -> 图像控件索引
    QMap<QString, QList<int>> m_nodeValueMap;    /// 节点名 -> 数值控件索引
    QMap<QString, QList<int>> m_nodeLightMap;    /// 节点名 -> 状态灯控件索引
};

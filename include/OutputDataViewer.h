#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QMap>
#include <QSharedPointer>

#include "PortDataType.h"

class NodeBase;
class DataObject;
class HalconWidget;

/// 输出数据可视化查看器
class OutputDataViewer : public QWidget
{
    Q_OBJECT

public:
    explicit OutputDataViewer(QWidget *parent = nullptr);
    ~OutputDataViewer() override = default;

    /// 设置要查看的节点
    void setNode(NodeBase *node);

    /// 清空显示
    void clear();

    /// 刷新显示
    void refresh();

public slots:
    /// 节点执行完成时更新
    void onNodeExecuted(NodeBase *node, bool success);

signals:
    /// 点击图像时发出（供主窗口放大显示）
    void imageClicked(NodeBase *node, int portIndex);

private:
    void setupUi();
    void updatePortData(int portIndex, QSharedPointer<DataObject> data);
    void displayImageData(int portIndex, QSharedPointer<DataObject> data);
    void displayRegionData(int portIndex, QSharedPointer<DataObject> data);
    void displayNumberData(int portIndex, QSharedPointer<DataObject> data);
    void displayStringData(int portIndex, QSharedPointer<DataObject> data);

    QTabWidget *m_tabWidget = nullptr;
    QLabel *m_nodeNameLabel = nullptr;
    QLabel *m_statusLabel = nullptr;

    NodeBase *m_currentNode = nullptr;
    QMap<int, QWidget*> m_portWidgets;  /// 端口索引 -> 显示控件
};

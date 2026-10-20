#include "UpstreamWalk.h"

#include "Connection.h"
#include "NodeBase.h"
#include "Port.h"

// 图上游闭包游走面（词条切片 B 图谱族首抽，从 MainWindow::getUpstreamNodes 抽出）：
// 现层 contains 去重（同层同一源挂一次）与递归上溯与原体同帧同序。

QList<NodeBase *> UpstreamWalk::upstreamOf(NodeBase *node)
{
    QList<NodeBase *> upstreamNodes;
    if (!node) {
        return upstreamNodes;
    }

    // 获取当前节点的所有输入端口
    QList<Port *> inputPorts = node->inputPorts();
    for (Port *port : inputPorts) {
        // 获取连接到该输入端口的所有连接
        QList<MyConnection *> connections = port->connections();
        for (MyConnection *conn : connections) {
            // 获取连接的源端口（输出端口）
            Port *sourcePort = conn->sourcePort();
            if (sourcePort) {
                // 获取源端口所属的节点
                NodeBase *sourceNode = sourcePort->node();
                if (sourceNode && !upstreamNodes.contains(sourceNode)) {
                    upstreamNodes.append(sourceNode);
                    // 递归获取更上游的节点
                    upstreamNodes.append(upstreamOf(sourceNode));
                }
            }
        }
    }

    return upstreamNodes;
}

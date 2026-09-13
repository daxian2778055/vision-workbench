#pragma once

#include <QObject>
#include <QComboBox>
#include <HalconCpp.h>

class FlowExecutor;
class FlowScene;
class NodeBase;
class MvsImageSourceNode;

/// 管理执行启动/停止，连接 FlowExecutor 信号
class NodeExecutionController : public QObject
{
    Q_OBJECT
public:
    explicit NodeExecutionController(FlowExecutor *executor, QComboBox *modeCombo,
                                      QObject *parent = nullptr);

    FlowExecutor *executor() const { return m_executor; }

    /// 设置当前流程场景
    void setCurrentScene(FlowScene *scene);

    /// 更新执行按钮状态
    void updateExecutionButtons();

    /// 刷新所有 MVS 图像源的像素格式控件
    void refreshAllMvsPixelFormats();

signals:
    void executionStarted();
    void executionStopped();
    void executionFinished();
    void executionError(const QString &error);
    void nodeExecuted(NodeBase *node, bool success);
    void imageReady(NodeBase *node, const HalconCpp::HImage &image);

public slots:
    void onStartExecution();
    void onStopExecution();

private slots:
    void onModeChanged(int index);

private:
    FlowExecutor *m_executor;
    QComboBox *m_modeCombo;
    FlowScene *m_currentScene;
};

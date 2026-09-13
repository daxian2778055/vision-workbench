#pragma once

#include <QObject>
#include <QList>
#include <QMap>
#include <QTabWidget>
#include "FlowExecutor.h"

class FlowScene;
class FlowExecutor;
class NodeBase;

/// 管理多流程标签页的增删、切换、模式存储
class FlowTabManager : public QObject
{
    Q_OBJECT
public:
    explicit FlowTabManager(QTabWidget *tabWidget, QObject *parent = nullptr);

    FlowScene *currentScene() const;
    QList<FlowScene *> scenes() const { return m_flowScenes; }
    int sceneCount() const { return m_flowScenes.size(); }

    FlowScene *createNewFlow();
    void closeFlowTab(int index);
    void clearAllFlows();

    FlowMode flowModeForScene(FlowScene *scene) const;
    void setFlowModeForScene(FlowScene *scene, FlowMode mode);

    QList<FlowScene *> allScenes() const { return m_flowScenes; }

signals:
    void currentSceneChanged(FlowScene *scene);
    void sceneAboutToClose(FlowScene *scene);
    void sceneCreated(FlowScene *scene);
    void flowModeChangedForScene(FlowScene *scene, FlowMode mode);

private slots:
    void onTabChanged(int index);

private:
    QTabWidget *m_tabWidget;
    QList<FlowScene *> m_flowScenes;
    QMap<FlowScene *, FlowMode> m_flowModes;
};

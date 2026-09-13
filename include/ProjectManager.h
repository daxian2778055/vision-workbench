#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QList>

class FlowScene;

class ProjectManager : public QObject
{
    Q_OBJECT

public:
    ProjectManager(QObject *parent = nullptr);
    ~ProjectManager();

    bool saveProject(const QString &filePath, const QList<FlowScene *> &scenes);
    bool loadProject(const QString &filePath, QList<FlowScene *> &scenes);

    /// 单场景序列化（供撤销/重做快照复用）
    QJsonObject sceneToJson(FlowScene *scene) const;
    /// 从 JSON 恢复单场景（清空并重建节点/连线）
    void sceneFromJson(const QJsonObject &json, FlowScene *scene);

    void setLastFilePath(const QString &path) { m_lastFilePath = path; }
    QString lastFilePath() const { return m_lastFilePath; }
    QString annotationDir() const;

private:
    QString m_lastFilePath;
};

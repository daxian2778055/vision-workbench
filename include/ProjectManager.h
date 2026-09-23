#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QList>

class FlowScene;
class QWidget;

class ProjectManager : public QObject
{
    Q_OBJECT

public:
    ProjectManager(QObject *parent = nullptr);
    ~ProjectManager();

    bool saveProject(const QString &filePath, const QList<FlowScene *> &scenes);
    bool loadProject(const QString &filePath, QList<FlowScene *> &scenes);

    /// 组装整个方案（场景 + 全局配置 + 运行界面布局）为 JSON 根对象。
    /// 手动保存与自动保存**必须共用它**：两套序列化会分叉，而分叉的表现是"崩溃恢复出来的
    /// 方案缺东西 / 与手动保存的内容不一致"——现场极难发现，故此处不留第二份实现。
    QJsonObject buildProjectJson(const QList<FlowScene *> &scenes) const;
    /// 把方案 JSON 应用到场景列表与各全局管理器（loadProject 与崩溃恢复共用）。
    /// 非 const：内部要调用非 const 的 sceneFromJson，且会改写全局管理器单例。
    bool applyProjectJson(const QJsonObject &root, QList<FlowScene *> &scenes);

    /// 交互式保存：弹文件对话框（.vfp 补全）+ 保存 + 成功/失败提示框。
    /// 返回是否保存成功；savedPath 在用户选定路径后即被赋值（取消则为空串）。
    bool saveProjectInteractive(QWidget *parent, const QList<FlowScene *> &scenes,
                                QString *savedPath = nullptr);
    /// 交互式选择要打开的方案文件（.vfp）；取消返回空串
    static QString askOpenProjectPath(QWidget *parent);

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

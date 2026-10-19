#pragma once

#include <QString>
#include <QList>
#include <functional>

class QAction;
class QDialog;
class QObject;
class FlowScene;

/// 菜单对话框路由面（词条切片 B 第二抽，从 MainWindow 通讯/系统菜单接线抽出）。
///
/// 职责：把「点某菜单动作 → 开某个辅助对话框」的接线语义收成一张路由工具面，四形：
///   · toDialog：固定键直路由——动作触发即走展示钩子（键与工厂由调用方给出）；
///   · toScenesDialog：多场景版——流程场景列表在触发当下现取，随工厂闭包带走；
///   · toFlowSceneDialog：单场景守卫版——场景在触发当下现取，缺失先报回调、不进展示；
///   · direct：非对话框动作（相机管理/口令直达一类）的原样转接（无键无工厂）。
///
/// 表现留 MainWindow：展示钩子即 MainWindow::showAuxDialog 的转发闭包（AuxPanelManager
/// 同键复用、关闭即销毁、非模态的既有语义不动）；对话框工厂在展示当下才被调用（懒构造：
/// 未展示不建窗，现有管线里 create 由 showDialog 同步调起）；路由面自己不持任何窗口指针。
/// 场景缺失的提示框留在调用方回调里（GUI 不进本面）。
class AuxDialogMenuRouter final {
public:
    using DialogFactory = std::function<QDialog *()>;
    using ShowDialog = std::function<void(const QString &key, const DialogFactory &factory)>;
    using ScenesProvider = std::function<QList<FlowScene *>()>;
    using SceneProvider = std::function<FlowScene *()>;
    using SceneMissing = std::function<void()>;
    using MultiSceneFactory = std::function<QDialog *(const QList<FlowScene *> &scenes)>;
    using SceneFactory = std::function<QDialog *(FlowScene *scene)>;

    AuxDialogMenuRouter() = default;
    explicit AuxDialogMenuRouter(const ShowDialog &show) : m_show(show) {}

    /// 装配/替换展示钩子（MainWindow 在界面就绪后转递 showAuxDialog）
    void setShow(const ShowDialog &show) { m_show = show; }

    /// 固定键直路由（连接生命周期随 context；动作触发即走钩子）
    void toDialog(QAction *action, QObject *context, const QString &key,
                  const DialogFactory &make);

    /// 多场景路由：列表在触发当下快照入工厂闭包（原体在工厂闭包内现取，同步展示链内两刻同瞬）
    void toScenesDialog(QAction *action, QObject *context, const QString &key,
                        const ScenesProvider &scenes, const MultiSceneFactory &make);

    /// 单场景守卫路由：场景缺失先报 onMissing 且**不进展示**（原体 flowVariable 缺场景
    /// 静默早退 → 调用方给空操作；带提示的入口在调用方回调里补）。
    void toFlowSceneDialog(QAction *action, QObject *context, const QString &key,
                           const SceneProvider &currentScene, const SceneMissing &onMissing,
                           const SceneFactory &make);

    /// 非对话框动作原样转接（handler 即原 lambda 体，本面不包不改）
    void direct(QAction *action, QObject *context, const std::function<void()> &handler);

private:
    ShowDialog m_show;
};

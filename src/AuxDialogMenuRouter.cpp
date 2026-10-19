#include "AuxDialogMenuRouter.h"

#include <QAction>
#include <QObject>

// 菜单对话框路由面（词条切片 B 第二抽）——四形连接语义：
//  · 所有连接的接收上下文用调用方传入的 context（MainWindow 侧一律传 this，与原
//    connect 形态一致，拆除序不变）；
//  · 展示钩子按值拷入连接体：连接不随本面对象的销毁而失效；
//  · 键只作透传，本面不拼不改（键的全部来源与唯一性钉在调用方的调用点上）；
//  · 场景与列表在触发当下取值后经工厂闭包传递，工厂在展示当下才被调用（懒构造）。

void AuxDialogMenuRouter::toDialog(QAction *action, QObject *context, const QString &key,
                                   const DialogFactory &make)
{
    const ShowDialog show = m_show;
    QObject::connect(action, &QAction::triggered, context, [show, key, make]() {
        show(key, make);
    });
}

void AuxDialogMenuRouter::toScenesDialog(QAction *action, QObject *context, const QString &key,
                                         const ScenesProvider &scenes,
                                         const MultiSceneFactory &make)
{
    const ShowDialog show = m_show;
    QObject::connect(action, &QAction::triggered, context, [show, key, scenes, make]() {
        const QList<FlowScene *> list = scenes();   // 触发当下取列表
        show(key, [list, make]() { return make(list); });
    });
}

void AuxDialogMenuRouter::toFlowSceneDialog(QAction *action, QObject *context, const QString &key,
                                            const SceneProvider &currentScene,
                                            const SceneMissing &onMissing,
                                            const SceneFactory &make)
{
    const ShowDialog show = m_show;
    QObject::connect(action, &QAction::triggered, context,
                     [show, key, currentScene, onMissing, make]() {
        FlowScene *scene = currentScene();          // 触发当下取场景
        if (!scene) {
            onMissing();                            // 缺失先报回调、不进展示（原体静默早退位）
            return;
        }
        show(key, [scene, make]() { return make(scene); });
    });
}

void AuxDialogMenuRouter::direct(QAction *action, QObject *context,
                                 const std::function<void()> &handler)
{
    QObject::connect(action, &QAction::triggered, context, handler);
}

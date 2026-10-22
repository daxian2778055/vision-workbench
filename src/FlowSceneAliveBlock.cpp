#include "FlowSceneAliveBlock.h"

FlowSceneAliveBlock::CaptureTicket::~CaptureTicket()
{
    if (m_mutex) {
        m_mutex->unlock();
    }
}

FlowSceneAliveBlock::CaptureTicket::CaptureTicket(FlowSceneAliveBlock *block)
{
    if (block) {
        block->m_mutex.lock();   // 块读锁：ticket 的作用域即「在飞捕获可安全进行」的窗
        m_mutex = &block->m_mutex;
        m_alive = (block->m_scene != nullptr && !block->m_gone);
        m_scene = m_alive ? block->m_scene : nullptr;   // 防呆：判走即置空影针
    }
}

FlowSceneAliveBlock::CaptureTicket::CaptureTicket(CaptureTicket &&o) noexcept
    : m_mutex(o.m_mutex)
    , m_scene(o.m_scene)
    , m_alive(o.m_alive)
{
    o.m_mutex = nullptr;   // 锁所有权随移走：源票析构不再放锁
    o.m_scene = nullptr;
    o.m_alive = false;
}

FlowSceneAliveBlock::CaptureTicket &FlowSceneAliveBlock::CaptureTicket::operator=(CaptureTicket &&o) noexcept
{
    if (this != &o) {
        if (m_mutex) {
            m_mutex->unlock();
        }
        m_mutex = o.m_mutex;
        m_scene = o.m_scene;
        m_alive = o.m_alive;
        o.m_mutex = nullptr;
        o.m_scene = nullptr;
        o.m_alive = false;
    }
    return *this;
}

void FlowSceneAliveBlock::bind(FlowScene *scene)
{
    QMutexLocker locker(&m_mutex);
    m_scene = scene;
    m_gone = false;   // 换绑即重置：新当值场景生死由它自己表态
    ++m_generation;
}

void FlowSceneAliveBlock::markGone(FlowScene *scene)
{
    if (scene == nullptr) {
        return;
    }
    // 写锁：等在飞捕获（块读锁）先完成——之后场景成员才开始拆除 ⇒ 析构不与取快照并发进行。
    // 身份认领：只标「当前绑定正是本场景」的块；旧场景亡不误伤新（别的场景的）块。
    QMutexLocker locker(&m_mutex);
    if (m_scene == scene) {
        m_gone = true;
    }
}

quint64 FlowSceneAliveBlock::generation() const
{
    QMutexLocker locker(&m_mutex);
    return m_generation;
}

FlowSceneAliveBlock::CaptureTicket FlowSceneAliveBlock::acquireForCapture()
{
    return CaptureTicket(this);
}

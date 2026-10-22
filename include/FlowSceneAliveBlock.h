#pragma once

#include <QMutex>

class FlowScene;

/// 段二存活控制块（切片 C 立项书 §13.1）：把「执行器 ↔ 场景」之间的场景指针一族收进共享控制块。
/// · 每场景自建一块（FlowScene 构造体）；执行器在 setFlowScene 时取**当值场景自代**的块（shared_ptr
///   拷贝）⇒ 场景先亡后块仍在执行器手里（墓碑位与运行态可读），场景亡不再产生悬垂解引用。
/// · 块持场景为**弱侧**：裸指针＋显式墓碑，不延长场景生命；不做 QPointer——跨线程查询与
///   析构竞争语义含糊，块锁＋墓碑把合同写全、每一步显式。
/// · 块锁是叶子锁：单向锁序＝（执行器互斥／图缓存锁）→ 块锁 → 场景图锁；从不逆向。
/// · 析构保据：~FlowScene 体首动作 markGone(this)（块写锁）；在飞捕获持块读锁取快照 ⇒
///   写锁被读锁挡住 → 析构等在飞快照完成才推进（取快照与销毁不并发，UAF 窗口关闭）。
/// · 运行态随块存活：墓碑位＋换绑计数（诊断可观测）。本块是零行为面：只决定
///   「可借／不再可借」，不改变任何取到快照之后的既有语义（墓碑机与持有期不动）。
class FlowSceneAliveBlock
{
public:
    /// 售票：票构造即判走（场景亡/未绑 ⇒ 不借出场景指针并置空影针，防御性上不可解引用）。
    /// 票持块**读锁**（构造取锁、析构放锁），取快照届期须在票的作用域内完成。
    class CaptureTicket
    {
    public:
        ~CaptureTicket();
        CaptureTicket(CaptureTicket &&o) noexcept;
        CaptureTicket &operator=(CaptureTicket &&o) noexcept;
        CaptureTicket(const CaptureTicket &) = delete;
        CaptureTicket &operator=(const CaptureTicket &) = delete;

        /// true＝票构造瞬间场景在世且已绑定（读侧可解引用 scene()）；false＝场景亡/未绑。
        bool alive() const { return m_alive; }
        /// 仅 alive()==true 时可解引用；否则恒空（防呆置空，误用面在编译后也不成立）。
        FlowScene *scene() const { return m_scene; }

    private:
        friend class FlowSceneAliveBlock;
        explicit CaptureTicket(FlowSceneAliveBlock *block);
        QMutex *m_mutex = nullptr;   /// 非拥有：借块锁（构造加、析构放）
        FlowScene *m_scene = nullptr;
        bool m_alive = false;
    };

    FlowSceneAliveBlock() = default;
    /// 拷贝禁止：块随最后一个 shared_ptr 真析构（增减不对称照 §13.2——增可在任何时机）。
    FlowSceneAliveBlock(const FlowSceneAliveBlock &) = delete;
    FlowSceneAliveBlock &operator=(const FlowSceneAliveBlock &) = delete;

    /// 执行器 setFlowScene 的同款绑定（含 nullptr 解绑）：写侧置当前指针、清墓碑、推进换绑计数。
    void bind(FlowScene *scene);
    /// 场景析构第一动作：**身份认领**——只有当前绑定指向同一场景才落墓碑（旧场景亡不误伤新绑定）。
    void markGone(FlowScene *scene);
    /// 换绑计数（诊断/观测用；随 bind 推进，从不回落）。
    quint64 generation() const;
    /// 售票（run 轮首与同步执行共用的借入口；未绑/亡 ⇒ alive()==false、scene()==nullptr）。
    CaptureTicket acquireForCapture();

private:
    mutable QMutex m_mutex;         /// 块锁（叶子）
    FlowScene *m_scene = nullptr;   /// 弱侧裸指针（弱不延长生命；只可判走、生死由场景自己）
    bool m_gone = false;
    quint64 m_generation = 0;       /// 运行态：换绑计数
};

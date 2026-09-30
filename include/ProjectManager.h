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
    bool loadProject(const QString &filePath, QList<FlowScene *> &scenes,
                     const QString &passphrase = QString());

    /// 导出加密 / 只读方案（G-P1-10）。
    /// passphrase 非空时加密（AES-256-CTR + PBKDF2-HMAC-SHA256）；readonly 置只读标志。
    /// 返回是否成功落盘；成功时同步记录 m_lastFilePath 与只读状态。
    bool exportEncryptedProject(const QString &filePath, const QList<FlowScene *> &scenes,
                                const QString &passphrase, bool readonly);

    /// 最近一次 loadProject 是否以只读形态加载（加密只读包 / 非加密只读包）。
    bool isReadonlyLoaded() const { return m_loadedReadonly; }
    void resetReadonlyFlag() { m_loadedReadonly = false; }

    /// 文件是否为需口令的加密信封（供 UI 在打开前决定是否弹入口令框）。
    static bool fileNeedsPassphrase(const QString &filePath);

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


signals:
    /// U-34（推进计划 §3.32 登记未修第 2 条那一格的可见面）：载入期每发生一条
    /// 「降级／被拒／按现状加载」留痕就发一句人话。产线侧唯一的接线在 ProjectLoadNotes.h，
    /// 由 MainWindow 把每一句写进气窗日志面板；本类同时保留 VFP_DEBUG 那一份（崩溃日志文件），
    /// 两条面共用同一份文案，不各写一遍。
    void loadNote(const QString &note);
private:
    /// 一句载入期留痕走两条面：崩溃日志文件（VFP_DEBUG，改前唯一那条）与可见面信号 loadNote。
    /// 文案在这里拼一遍，载入期八个站点都调它。
    void reportLoadNote(const QString &text);

    QString m_lastFilePath;
    bool m_loadedReadonly = false;
    /// U-36：本次载入有多少条「原始载荷没进内存表」——按两条通道数（标定整条被拒／夹具侧降级），
    /// 其中夹具那条通道装着两类不同后果：矩阵或位姿作废（键名保留、载荷写成空）与键名为空整条跳过
    /// （不进表、无键名可保留 ⇒ 保存后整条不再写回），合起来是三种落盘损失（U-37 复核意见 S-1）。
    /// 只有这三种的载荷会在下一次保存时从方案文件里消失或变空（其余留痕不改写落盘内容）。
    /// ↑这半句由 U-38 实测证否，原文保留不删：算子侧还有下面两个计数器覆盖的一族，它们同样改写落盘
    /// 内容（取证读数 build/u38_probe/u38_gate_before.raw.txt）。
    int m_loadRawLossCount = 0;
    /// U-38：算子侧的落盘损失另数，不与上面那个合并——上面那个管「标定／夹具的载荷没进表」，
    /// 这两个管「方案里的算子与连线在下一次保存时被改写或整条不再写回」。分两条通道各自成句，
    /// 现场才能看出损失出在哪一侧（合成一句就只能知道"共几项"而不知道丢了什么）。
    /// 类型号一类：文件里带的类型号被整键去掉（算子已下线 ⇒ 降级成通用算子，它不再带 vfpNodeTypeId），
    /// 或被换成当前实现的类型号（显示名被另一实现复用）——两种都让下一次保存写出的不再是文件里那一个。
    int m_loadNodeTypeLossCount = 0;
    /// 连线一类：文件里的一条连线没能接回（端口下标越界或端点算子缺失）⇒ 下一次保存整条不再写回。
    /// 降级算子的出端口清单被改写成通用算子的那一份是同一次降级的另一半后果，不单列成一项
    /// （那会把一次损失数两遍），但要在合计句里点名。
    int m_loadEdgeLossCount = 0;
};

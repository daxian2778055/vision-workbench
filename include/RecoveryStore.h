#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>

/// 方案「自动保存 + 崩溃恢复」的落盘逻辑（无 GUI 依赖，可在 CI 单元测试）。
///
/// 为什么单独成类：MainWindow 无法在 CI 中实例化（重量级窗口 + HALCON/相机/数据库初始化，
/// 见 PanelVisibilityStore.h 同类说明），把这段逻辑留在窗口里就毫无自动化兜底——而它出错的
/// 表现恰恰是最难接受的：用户以为有备份，真出事时才发现恢复文件是坏的 / 是空的 / 时间对不上。
/// 抽出后可用 QTEST 钉住：往返一致性、原子写、坏文件、内容基线（"是否有未保存改动"）、干净退出清理。
///
/// 存储位置：`<应用目录>/data/recovery/recovery.vfp`（与本仓 AppDatabase 的 `exe/data/visionflow.db`
/// 同一惯例——运行时数据随安装目录走，现场打包/迁移/排障都是整目录操作）；应用目录不可写时
/// 回落到用户数据目录（见 defaultDirPath 的可写探针）。
///
/// 三个关键设计取舍（都有代价，写在这里免得下一个人"顺手"改回去）：
///  1. **内容与元信息同文件**：recoveryMeta（原方案路径 / 自动保存时刻 / 版本）直接写在项目 JSON
///     内。若拆成 .meta 旁挂文件，两次写入之间崩溃就会留下"时间对不上内容"的恢复文件，且事后
///     无法判断谁对。同文件写入是原子的，两者不可能不一致。
///  2. **恢复文件就是合法方案文件**：只多一个 recoveryMeta 键（加载时被忽略），所以用户也可以
///     直接「文件 → 加载项目」打开它来恢复——不依赖本类的任何入口，也不怕本类将来被改坏。
///  3. **"是否有未保存改动"用内容基线判断，不引入脏标记**：本仓 FlowScene 是自研撤销栈、没有
///     clean 态（见 FlowScene.h），且全局配置（变量/相机/通讯/触发/标定）同样可被改动；与其到处
///     补置位（漏一处就静默失效），不如比较"当前序列化内容 vs 上次落盘内容"——序列化本身就是
///     唯一真相（同一状态两次序列化字节一致，用例已钉住这个前提）。
class RecoveryStore
{
public:
    /// 恢复文件的存在性与元信息（供启动时向用户展示）
    struct Info {
        bool exists = false;      ///< 文件存在
        bool readable = false;    ///< 且是合法 JSON 对象（false = 损坏，不可恢复）
        bool metaPresent = false; ///< 含 recoveryMeta（缺失时后两个字段为默认值）
        QString originalPath;     ///< 自动保存时对应的方案路径（从未保存过的新方案为空）
        QDateTime savedAt;        ///< 自动保存时刻（缺失/格式非法时为无效时间）
        qint64 size = 0;          ///< 字节数
    };

    /// 正式运行：落在 defaultDirPath()
    RecoveryStore();
    /// 测试用：注入存储目录（避免污染真实数据目录）
    explicit RecoveryStore(const QString &dirPath);

    QString dirPath() const { return m_dir; }
    /// 恢复文件名（固定名：同一时刻只有一个当前方案，只保留最新一份）
    static QString recoveryFileName();
    /// 供"加载方案"路径读取的临时导出文件名
    static QString restoreFileName();
    /// 元信息键名（recoveryMeta）
    static QString metaKey();
    /// 默认存储目录：优先 `<应用目录>/data/recovery`，不可写则回落用户数据目录
    static QString defaultDirPath();

    /// 写入恢复文件（QSaveFile 原子写）。originalPath 用于元信息，可为空（从未保存过的新方案）。
    bool saveRecovery(const QJsonObject &projectJson, const QString &originalPath,
                      const QDateTime &now = QDateTime::currentDateTime());
    Info info() const;
    /// 读出恢复内容（仅 Info::readable 为 true 时成功）
    bool loadRecovery(QJsonObject *out) const;
    /// 把恢复内容导出成可被「加载方案」直接打开的临时文件，成功时给出其路径
    bool exportForLoad(QString *outPath) const;
    /// 清除恢复现场（干净退出 / 用户选择丢弃）：恢复文件 + 临时文件。幂等。
    bool clearRecovery();

    /// 记录"当前内容已落盘"（保存/加载成功后调用）
    void markSaved(const QJsonObject &projectJson);
    bool hasBaseline() const { return !m_baseline.isEmpty(); }
    /// 相对上次落盘是否有改动（尚无基线时：非空方案即视为有改动）
    bool hasUnsavedChanges(const QJsonObject &projectJson) const;
    /// 该内容是否已写入恢复文件（进程内记忆；避免每分钟重复写同一份内容）
    bool recoveryUpToDate(const QJsonObject &projectJson) const;
    /// 空方案判定：没有任何流程，或所有流程都没有节点（用于"别对着空白界面弹提示"）
    static bool isEmptyProject(const QJsonObject &projectJson);
    /// 规范化序列化（紧凑 JSON，用于内容比较）
    static QByteArray compact(const QJsonObject &json);

private:
    QString m_dir;
    QByteArray m_baseline;      ///< 上次落盘内容（空 = 尚无基线）
    QByteArray m_lastRecovery;  ///< 最近一次写入恢复文件的内容
};

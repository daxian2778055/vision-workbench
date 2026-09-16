#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

/// 辅助面板显隐的持久化。
///
/// 单独成类的原因：MainWindow 无法在 CI 中实例化（重量级窗口 + HALCON/相机/数据库初始化），
/// 因此"记住并恢复面板显隐"这段逻辑此前没有任何自动化兜底。抽出来并允许注入 QSettings 后，
/// 就能对"跨重启往返、键一致、默认关闭"这些真正会出错的地方做单元测试。
class PanelVisibilityStore
{
public:
    /// 使用应用默认配置（正式运行）
    PanelVisibilityStore();
    /// 使用指定 ini 文件（测试用，避免污染注册表）
    explicit PanelVisibilityStore(const QString &iniPath);

    /// 该面板上次退出时是否处于打开状态（未来记录过则为 false）
    bool isVisible(const QString &panelKey) const;
    /// 记录面板显隐
    void setVisible(const QString &panelKey, bool visible);

    /// 全部受管面板的键（集中定义：避免各处写裸字符串，导致写入与恢复的键不一致而静默失效）
    static QStringList knownKeys();
    /// 面板键 -> 配置项名（panels/<key>）
    static QString settingsKey(const QString &panelKey);

private:
    QSettings m_settings;
};

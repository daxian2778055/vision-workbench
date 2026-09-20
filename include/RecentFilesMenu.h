#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QScopedPointer>
#include <functional>

class QMenu;
class QSettings;

/// 「最近打开」菜单：方案路径的记录、去重置顶、上限截断、菜单重建与清空。
///
/// 从 MainWindow 抽出（含 QSettings 键 "recentFiles" 的读写契约不变）。
/// 可选注入 ini 路径（测试用，避免污染注册表）；打开动作通过回调交给调用方
/// （MainWindow 注入 loadProjectFile），本类不反向依赖主窗口。
class RecentFilesMenu : public QObject
{
    Q_OBJECT
public:
    /// 记录上限（与原实现一致）
    static constexpr int MaxEntries = 8;

    /// menuParent：挂载点（一般是「文件」菜单）；openCallback：点击某条记录时打开该方案；
    /// settingsIniPath：空 = 应用默认配置（正式运行），非空 = 指定 ini（测试用）
    RecentFilesMenu(QMenu *menuParent, std::function<void(const QString &)> openCallback,
                    const QString &settingsIniPath = QString(), QObject *parent = nullptr);

    /// 记录一次打开/保存：去重 + 置顶 + 截断 + 落盘 + 重建菜单
    void add(const QString &filePath);
    /// 按配置重建菜单（空列表显示"（无最近记录）"占位）
    void rebuild();
    /// 清空记录
    void clear();
    /// 当前记录（最新在前；供诊断/测试）
    QStringList files() const;
    /// 菜单本体（已挂到 menuParent 下；供调用方定制样式或测试触发条目）
    QMenu *menu() const { return m_menu; }

private:
    QMenu *m_menu;
    std::function<void(const QString &)> m_openCallback;
    QScopedPointer<QSettings> m_settings;
};

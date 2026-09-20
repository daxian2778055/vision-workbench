#include "RecentFilesMenu.h"

#include <QMenu>
#include <QAction>
#include <QSettings>

namespace {
const QString kRecentFilesKey = QStringLiteral("recentFiles");
}

RecentFilesMenu::RecentFilesMenu(QMenu *menuParent, std::function<void(const QString &)> openCallback,
                                 const QString &settingsIniPath, QObject *parent)
    : QObject(parent)
    , m_menu(new QMenu(QStringLiteral("最近打开"), menuParent))
    , m_openCallback(std::move(openCallback))
{
    if (settingsIniPath.isEmpty())
        m_settings.reset(new QSettings());   // 应用默认配置（正式运行）
    else
        m_settings.reset(new QSettings(settingsIniPath, QSettings::IniFormat));
    if (menuParent)
        menuParent->addMenu(m_menu);
    rebuild();
}

QStringList RecentFilesMenu::files() const
{
    return m_settings->value(kRecentFilesKey).toStringList();
}

void RecentFilesMenu::add(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    QStringList list = files();
    list.removeAll(filePath);
    list.prepend(filePath);
    while (list.size() > MaxEntries) {
        list.removeLast();
    }
    m_settings->setValue(kRecentFilesKey, list);
    rebuild();
}

void RecentFilesMenu::clear()
{
    m_settings->remove(kRecentFilesKey);
    rebuild();
}

void RecentFilesMenu::rebuild()
{
    if (!m_menu)
        return;
    m_menu->clear();
    const QStringList list = files();
    if (list.isEmpty()) {
        QAction *empty = m_menu->addAction(QStringLiteral("（无最近记录）"));
        empty->setEnabled(false);
        return;
    }
    for (const QString &path : list) {
        QAction *act = m_menu->addAction(path);
        connect(act, &QAction::triggered, this, [this, path]() {
            if (m_openCallback)
                m_openCallback(path);
        });
    }
    m_menu->addSeparator();
    QAction *clearAct = m_menu->addAction(QStringLiteral("清空记录"));
    connect(clearAct, &QAction::triggered, this, [this]() { clear(); });
}

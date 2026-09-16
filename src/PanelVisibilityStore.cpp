#include "PanelVisibilityStore.h"

PanelVisibilityStore::PanelVisibilityStore()
    : m_settings()
{
}

PanelVisibilityStore::PanelVisibilityStore(const QString &iniPath)
    : m_settings(iniPath, QSettings::IniFormat)
{
}

QStringList PanelVisibilityStore::knownKeys()
{
    // 与 MainWindow::openAuxPanel() 支持的键保持一致
    return {QStringLiteral("resultTable"), QStringLiteral("variable"),
            QStringLiteral("performance"), QStringLiteral("outputData")};
}

QString PanelVisibilityStore::settingsKey(const QString &panelKey)
{
    return QStringLiteral("panels/%1").arg(panelKey);
}

bool PanelVisibilityStore::isVisible(const QString &panelKey) const
{
    if (panelKey.isEmpty())
        return false;
    return m_settings.value(settingsKey(panelKey), false).toBool();
}

void PanelVisibilityStore::setVisible(const QString &panelKey, bool visible)
{
    if (panelKey.isEmpty())
        return;   // 空键会写成 "panels/"，读回来永远匹配不上，直接忽略
    m_settings.setValue(settingsKey(panelKey), visible);
}

#include "UiLogBus.h"

UiLogBus *UiLogBus::instance()
{
    static UiLogBus *inst = new UiLogBus();
    return inst;
}

UiLogBus::UiLogBus(QObject *parent)
    : QObject(parent)
{
}

void UiLogBus::publish(const QString &message)
{
    if (message.isEmpty()) return;
    m_recent.append(message);
    while (m_recent.size() > kRecentLimit)
        m_recent.removeFirst();
    emit messageLogged(message);
}

QStringList UiLogBus::recent(int maxLines) const
{
    if (maxLines <= 0) return {};
    return m_recent.size() <= maxLines ? m_recent : m_recent.mid(m_recent.size() - maxLines);
}
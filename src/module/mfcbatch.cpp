#include "module/mfcbatch.h"

namespace MfcBatch
{

QString itemStateName(ItemState state)
{
    switch(state)
    {
    case ItemState::Pending:
        return QStringLiteral("pending");
    case ItemState::Running:
        return QStringLiteral("running");
    case ItemState::Verified:
        return QStringLiteral("verified");
    case ItemState::Failed:
        return QStringLiteral("failed");
    case ItemState::Skipped:
        return QStringLiteral("skipped");
    case ItemState::Candidate:
        return QStringLiteral("candidate");
    }
    return QStringLiteral("unknown");
}

bool retryable(ItemState state)
{
    return state == ItemState::Failed || state == ItemState::Candidate;
}

void Plan::build(const Mfc::Document& document, quint64 session)
{
    m_items.clear();
    for(int sector = 0; sector < document.sectorCount(); sector++)
    {
        for(char type : {'A', 'B'})
        {
            const Mfc::KeyEntry& entry = document.key(sector, type);
            if(entry.verifiedIn(session))
                continue;
            Item item;
            item.sector = sector;
            item.type = type;
            item.key = entry.value;
            if(entry.hasValue())
            {
                // A value that was never authenticated is only a candidate.
                item.state = ItemState::Pending;
            }
            m_items.append(item);
        }
    }
}

int Plan::next() const
{
    for(int i = 0; i < m_items.size(); i++)
        if(m_items[i].state == ItemState::Pending)
            return i;
    return -1;
}

bool Plan::setResult(int index, ItemState state, const QString& detail, const QString& key)
{
    if(index < 0 || index >= m_items.size())
        return false;
    if(state == ItemState::Running)
        m_items[index].attempts += 1;
    m_items[index].state = state;
    m_items[index].detail = detail;
    if(!key.isEmpty())
        m_items[index].key = key;
    return true;
}

int Plan::retryMissing()
{
    int count = 0;
    for(Item& item : m_items)
    {
        if(item.state != ItemState::Failed && item.state != ItemState::Candidate)
            continue;
        item.state = ItemState::Pending;
        item.detail.clear();
        count++;
    }
    return count;
}

void Plan::invalidateRunning(const QString& reason)
{
    for(Item& item : m_items)
        if(item.state == ItemState::Running)
        {
            item.state = ItemState::Pending;
            item.detail = reason;
        }
}

Progress Plan::progress() const
{
    Progress progress;
    progress.total = m_items.size();
    for(const Item& item : m_items)
    {
        switch(item.state)
        {
        case ItemState::Pending:
        case ItemState::Running:
            break;
        case ItemState::Verified:
            progress.processed++;
            progress.verified++;
            break;
        case ItemState::Failed:
            progress.processed++;
            progress.failed++;
            break;
        case ItemState::Candidate:
            progress.processed++;
            progress.missing++;
            break;
        case ItemState::Skipped:
            progress.processed++;
            progress.skipped++;
            break;
        }
    }
    return progress;
}

QString Plan::describe(const Item& item)
{
    return QStringLiteral("sector %1 Key %2").arg(item.sector).arg(QChar(item.type));
}

} // namespace MfcBatch

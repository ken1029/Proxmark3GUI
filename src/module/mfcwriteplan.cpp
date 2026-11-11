#include "mfcwriteplan.h"

namespace Mfc
{

QString writeOutcomeName(WriteOutcome outcome)
{
    switch(outcome)
    {
    case WriteOutcome::Pending:
        return QStringLiteral("pending");
    case WriteOutcome::Blocked:
        return QStringLiteral("blocked");
    case WriteOutcome::WriteFailed:
        return QStringLiteral("failed");
    case WriteOutcome::Verified:
        return QStringLiteral("verified");
    case WriteOutcome::WrittenUnverified:
        return QStringLiteral("written, unverified");
    case WriteOutcome::Mismatch:
        return QStringLiteral("mismatch");
    }
    return QStringLiteral("unknown");
}

bool retryable(WriteOutcome outcome)
{
    // Only a write that never reported success may be sent again.  A trailer
    // whose verification failed may already carry the new keys; rewriting it
    // would use an authentication the card no longer accepts.
    return outcome == WriteOutcome::WriteFailed;
}

QString previewStateName(PreviewState state)
{
    switch(state)
    {
    case PreviewState::NotPreviewed:
        return QStringLiteral("not previewed");
    case PreviewState::Same:
        return QStringLiteral("same");
    case PreviewState::Different:
        return QStringLiteral("different");
    case PreviewState::Unreadable:
        return QStringLiteral("unreadable");
    case PreviewState::PartiallyUnknown:
        return QStringLiteral("partially unknown");
    case PreviewState::NoKey:
        return QStringLiteral("no verified key");
    }
    return QStringLiteral("unknown");
}

void WritePlan::reset(const Document& source, quint64 session)
{
    m_source = source;
    m_session = session;
    m_rows.clear();
    for(int block = 0; block < m_source.blockCount(); block++)
    {
        if(!m_source.blockComplete(block))
            continue;
        WriteRow row;
        row.block = block;
        row.allowed = blockAllowed(block, &row.blockedReason);
        m_rows.append(row);
    }
}

bool WritePlan::matches(const Document& document, quint64 session) const
{
    return session != 0 && session == m_session
           && document.uid == m_source.uid
           && document.cardType() == m_source.cardType()
           && document.completeBlockCount() == m_source.completeBlockCount();
}

void WritePlan::setIncludeBlock0(bool on)
{
    m_includeBlock0 = on;
}

void WritePlan::setIncludeTrailers(bool on)
{
    m_includeTrailers = on;
}

int WritePlan::indexOfBlock(int block) const
{
    for(int i = 0; i < m_rows.size(); i++)
        if(m_rows.at(i).block == block)
            return i;
    return -1;
}

bool WritePlan::row(int block, WriteRow* out) const
{
    const int index = indexOfBlock(block);
    if(index < 0)
        return false;
    if(out != nullptr)
        *out = m_rows.at(index);
    return true;
}

bool WritePlan::trailerFieldsValid(int block, TrailerPlan* plan) const
{
    TrailerPlan p;
    p.targetHex = m_source.blockHex(block);
    if(p.targetHex.size() != 32)
    {
        p.error = QStringLiteral("the trailer is incomplete");
        if(plan != nullptr)
            *plan = p;
        return false;
    }
    p.acl = p.targetHex.mid(12, 8);
    p.keyA = p.targetHex.mid(0, 12);
    p.keyB = p.targetHex.mid(20, 12);
    if(p.keyA.contains(QLatin1Char('?')))
    {
        p.error = QStringLiteral("Key A is unknown; writing would destroy it");
        if(plan != nullptr)
            *plan = p;
        return false;
    }
    if(!TrailerAcl::valid(p.acl))
    {
        p.error = QStringLiteral("the access bits are not valid");
        if(plan != nullptr)
            *plan = p;
        return false;
    }
    if(p.keyB.contains(QLatin1Char('?')))
    {
        p.error = QStringLiteral("Key B is unknown; writing would destroy it");
        if(plan != nullptr)
            *plan = p;
        return false;
    }

    // Write permission is decided by the access conditions that are on the card
    // now, not by the ones the document wants to install.
    WriteRow current;
    if(!row(block, &current) || current.cardHex.size() != 32)
    {
        p.error = QStringLiteral("preview the card first: the current access bits decide this write");
        if(plan != nullptr)
            *plan = p;
        return false;
    }
    p.cardAcl = current.cardHex.mid(12, 8);
    if(!TrailerAcl::valid(p.cardAcl))
    {
        p.error = QStringLiteral("the card access bits could not be read");
        if(plan != nullptr)
            *plan = p;
        return false;
    }

    const int sector = m_source.spec().sectorOfBlock(block);
    if(TrailerAcl::keyAWriteAllowed(p.cardAcl, 'A') && m_source.keyVerifiedIn(sector, 'A', m_session))
        p.writeKeyType = 'A';
    else if(TrailerAcl::keyBWriteAllowed(p.cardAcl, 'B') && m_source.keyVerifiedIn(sector, 'B', m_session))
        p.writeKeyType = 'B';
    else
    {
        p.error = QStringLiteral("the card access bits do not allow writing this trailer with a verified key");
        if(plan != nullptr)
            *plan = p;
        return false;
    }

    // Verification uses the new keys, because the old authentication may stop
    // working as soon as the write lands.
    TrailerVerifyPlan verify;
    verify.valid = true;
    verify.authType = 'A';
    verify.keyAProven = true;
    verify.checkAcl = true;
    verify.checkKeyB = TrailerAcl::keyBReadable(p.acl);
    p.verify = verify;
    p.valid = true;
    if(plan != nullptr)
        *plan = p;
    return true;
}

TrailerPlan WritePlan::trailerPlan(int block) const
{
    TrailerPlan plan;
    if(!m_source.spec().isTrailerBlock(block))
    {
        plan.error = QStringLiteral("not a sector trailer");
        return plan;
    }
    trailerFieldsValid(block, &plan);
    return plan;
}

bool WritePlan::blockAllowed(int block, QString* reason) const
{
    auto fail = [reason](const QString& text) {
        if(reason != nullptr)
            *reason = text;
        return false;
    };
    if(indexOfBlock(block) < 0)
        return fail(QStringLiteral("the block was never read"));
    if(block == 0 && !m_includeBlock0)
        return fail(QStringLiteral("Block 0 is protected; include it explicitly"));
    if(m_source.spec().isTrailerBlock(block))
    {
        if(!m_includeTrailers)
            return fail(QStringLiteral("sector trailers are protected; include them explicitly"));
        TrailerPlan plan;
        if(!trailerFieldsValid(block, &plan))
            return fail(plan.error);
        return true;
    }
    const int sector = m_source.spec().sectorOfBlock(block);
    if(!m_source.keyVerifiedIn(sector, 'A', m_session) && !m_source.keyVerifiedIn(sector, 'B', m_session))
        return fail(QStringLiteral("no key verified for sector %1").arg(sector));
    return true;
}

bool WritePlan::select(int block, bool selected, QString* reason)
{
    const int index = indexOfBlock(block);
    if(index < 0)
    {
        if(reason != nullptr)
            *reason = QStringLiteral("the block was never read");
        return false;
    }
    WriteRow& target = m_rows[index];
    if(!selected)
    {
        target.selected = false;
        return true;
    }
    QString why;
    if(!blockAllowed(block, &why))
    {
        target.selected = false;
        target.allowed = false;
        target.blockedReason = why;
        if(reason != nullptr)
            *reason = why;
        return false;
    }
    target.selected = true;
    target.allowed = true;
    target.blockedReason.clear();
    return true;
}

bool WritePlan::isSelected(int block) const
{
    WriteRow current;
    return row(block, &current) && current.selected;
}

void WritePlan::setPreview(int block, const QString& cardHex, PreviewState state, const QString& note)
{
    const int index = indexOfBlock(block);
    if(index < 0)
        return;
    WriteRow& target = m_rows[index];
    target.cardHex = cardHex;
    target.preview = state;
    target.note = note;
    target.allowed = blockAllowed(block, &target.blockedReason);
    // Only a block that is allowed and known to differ is pre-selected.  An
    // unreadable block is never treated as safe to write.
    target.selected = target.allowed && state == PreviewState::Different;
    if(target.selected)
        target.blockedReason.clear();
}

void WritePlan::setReadBack(int block, const QString& hex)
{
    const int index = indexOfBlock(block);
    if(index >= 0)
        m_rows[index].readBack = hex;
}

void WritePlan::clearPreview()
{
    for(WriteRow& row : m_rows)
    {
        row.cardHex.clear();
        row.preview = PreviewState::NotPreviewed;
        row.note.clear();
        row.allowed = blockAllowed(row.block, &row.blockedReason);
        row.selected = false;
    }
}

void WritePlan::setOutcome(int block, WriteOutcome outcome, const QString& note)
{
    const int index = indexOfBlock(block);
    if(index < 0)
        return;
    m_rows[index].outcome = outcome;
    if(!note.isEmpty() || outcome != WriteOutcome::Pending)
        m_rows[index].note = note;
}

WriteOutcome WritePlan::outcome(int block) const
{
    WriteRow current;
    return row(block, &current) ? current.outcome : WriteOutcome::Blocked;
}

QList<int> WritePlan::selectedBlocks() const
{
    QList<int> blocks;
    for(const WriteRow& row : m_rows)
    {
        if(!row.selected)
            continue;
        // Re-validate: the include options may have changed since selection.
        if(!blockAllowed(row.block))
            continue;
        blocks.append(row.block);
    }
    return blocks;
}

QList<int> WritePlan::retryBlocks() const
{
    QList<int> blocks;
    for(const WriteRow& row : m_rows)
        if(retryable(row.outcome))
            blocks.append(row.block);
    return blocks;
}

bool WritePlan::anyWrittenUnverified() const
{
    for(const WriteRow& row : m_rows)
        if(row.outcome == WriteOutcome::WrittenUnverified)
            return true;
    return false;
}

} // namespace Mfc

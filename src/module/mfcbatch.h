#ifndef MFCBATCH_H
#define MFCBATCH_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mfcdocument.h"

/*
 * "Fill in the missing keys" batch plan.
 *
 * The plan is the queue the GUI walks: one item per (sector, Key A/B).  It is
 * independent of the device so the stop/continue/retry rules are testable:
 * a failed item never blocks the next one, a cancelled plan keeps everything
 * already verified, and "retry missing" only re-queues items that are still
 * unknown or failed.  A key is only marked verified when the card authenticated
 * it in the current RF session; a value that merely came out of a command is a
 * candidate.
 */
namespace MfcBatch
{

enum class ItemState
{
    Pending,   // not attempted yet
    Running,   // the command for this item is in flight
    Verified,  // the card authenticated the key in the current session
    Failed,    // the attempt failed; retrying is safe
    Skipped,   // no method can reach this key with what is known
    Candidate, // a value was recovered but could not be authenticated
};

QString itemStateName(ItemState state);
bool retryable(ItemState state);

struct Item
{
    int sector = 0;
    char type = 'A';
    QString key;      // candidate value, when known
    ItemState state = ItemState::Pending;
    QString detail;   // why it failed / what is missing
    int attempts = 0;
};

struct Progress
{
    int total = 0;
    int processed = 0;
    int verified = 0;
    int missing = 0;
    int failed = 0;
    int skipped = 0;
};

class Plan
{
public:
    Plan() = default;

    // Builds the queue for every key that is not verified in `session`.
    // Sectors whose both keys are already verified are not queued.
    void build(const Mfc::Document& document, quint64 session);
    bool isEmpty() const { return m_items.isEmpty(); }
    const QVector<Item>& items() const { return m_items; }
    int size() const { return m_items.size(); }

    // Index of the next item to run, or -1 when nothing is left.
    int next() const;
    // Marks the current item; the caller supplies the evidence it obtained.
    bool setResult(int index, ItemState state, const QString& detail = QString(),
                   const QString& key = QString());
    // Re-queues Failed / Candidate items after a card reconnect, but only when
    // the caller explicitly asks for it.
    int retryMissing();
    // Everything that was running when the card changed goes back to Pending.
    void invalidateRunning(const QString& reason);
    Progress progress() const;
    bool done() const { return next() < 0; }
    // Human readable "sector 3 Key A" for the task line.
    static QString describe(const Item& item);

private:
    QVector<Item> m_items;
};

} // namespace MfcBatch

#endif // MFCBATCH_H

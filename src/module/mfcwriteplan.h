#ifndef MFCWRITEPLAN_H
#define MFCWRITEPLAN_H

#include <QList>
#include <QString>
#include <QVector>

#include "mfcdocument.h"

/*
 * Controlled write plan.
 *
 * The plan is the single place that decides whether a block may be written:
 * the dialog defaults, the preview, a manual check in the table, "retry failed
 * only" and the loop that sends the commands all ask the same object.  A row
 * that is not allowed can never become selected, so a preview result or a
 * double click cannot smuggle Block 0 or a sector trailer into the command
 * sequence.
 *
 * The plan is also a snapshot: it copies the source document and the RF session
 * it was built for, so a card change or a reconnect invalidates it instead of
 * writing one card's data onto another.
 */
namespace Mfc
{

enum class WriteOutcome
{
    Pending,            // not attempted yet
    Blocked,            // refused by policy (protected, incomplete, no key, ACL)
    WriteFailed,        // the client did not report a successful write
    Verified,           // written, and every readable field matched
    WrittenUnverified,  // write reported success, read-back could not prove it
    Mismatch,           // written, but the read-back shows different data
};

QString writeOutcomeName(WriteOutcome outcome);
// True when the write is known not to have taken effect, so retrying is safe.
bool retryable(WriteOutcome outcome);

// What the preview learned about one block on the card.
enum class PreviewState
{
    NotPreviewed,
    Same,
    Different,
    Unreadable,
    PartiallyUnknown,
    NoKey,
};

QString previewStateName(PreviewState state);

// What can be proven about a trailer after writing it.
struct TrailerVerifyPlan
{
    bool valid = false;
    QString error;
    char authType = 0;      // key used for the verification read ('A' or 'B')
    bool checkAcl = false;  // the access bytes are readable with authType
    bool checkKeyB = false; // Key B is readable as data under the new ACL
    bool keyAProven = false; // a successful authentication proves Key A
};

struct TrailerPlan
{
    bool valid = false;
    QString error;
    QString acl;            // document bytes 6..9 (8 hex)
    QString keyA;           // document Key A (12 hex)
    QString keyB;           // document Key B (12 hex)
    QString targetHex;      // the 32 hex payload that would be sent
    QString cardAcl;        // access bytes read from the card during the preview
    char writeKeyType = 0;  // key that may write the trailer under cardAcl
    TrailerVerifyPlan verify;
};

struct WriteRow
{
    int block = 0;
    bool selected = false;
    bool allowed = false;
    QString blockedReason;
    WriteOutcome outcome = WriteOutcome::Pending;
    PreviewState preview = PreviewState::NotPreviewed;
    QString cardHex;   // preview read-back, may contain '?'
    QString readBack;  // post-write read-back, when one was possible
    QString note;      // human readable state detail
};

class WritePlan
{
public:
    WritePlan() = default;

    // Freeze the source and the RF session the plan belongs to.
    void reset(const Document& source, quint64 session);
    bool matches(const Document& document, quint64 session) const;
    bool isEmpty() const { return m_rows.isEmpty(); }

    void setIncludeBlock0(bool on);
    void setIncludeTrailers(bool on);
    bool includeBlock0() const { return m_includeBlock0; }
    bool includeTrailers() const { return m_includeTrailers; }
    quint64 session() const { return m_session; }
    QString sourceUid() const { return m_source.uid; }
    CardType cardType() const { return m_source.cardType(); }
    const Document& source() const { return m_source; }

    const QVector<WriteRow>& rows() const { return m_rows; }
    int indexOfBlock(int block) const;
    bool row(int block, WriteRow* out) const;

    // Policy.  Never writes; returns false with a reason when the block may not
    // be sent in the current state of the plan.
    bool blockAllowed(int block, QString* reason = nullptr) const;
    TrailerPlan trailerPlan(int block) const;

    // Selection.  Selecting a block that is not allowed fails and is reported;
    // deselecting always works.
    bool select(int block, bool selected, QString* reason = nullptr);
    bool isSelected(int block) const;

    // Preview results.  "different"/"unreadable" never imply permission.
    void setPreview(int block, const QString& cardHex, PreviewState state, const QString& note);
    void setReadBack(int block, const QString& hex);
    void clearPreview();

    void setOutcome(int block, WriteOutcome outcome, const QString& note = QString());
    WriteOutcome outcome(int block) const;

    // Re-validated selection, in block order.
    QList<int> selectedBlocks() const;
    // Only outcomes where the write is known not to have happened.
    QList<int> retryBlocks() const;
    bool anyWrittenUnverified() const;

private:
    bool trailerFieldsValid(int block, TrailerPlan* plan) const;

    Document m_source;
    quint64 m_session = 0;
    bool m_includeBlock0 = false;
    bool m_includeTrailers = false;
    QVector<WriteRow> m_rows;
};

} // namespace Mfc

#endif // MFCWRITEPLAN_H

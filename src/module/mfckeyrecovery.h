#ifndef MFCKEYRECOVERY_H
#define MFCKEYRECOVERY_H

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mfcdocument.h"

/*
 * MIFARE Classic key recovery: method advice, client output parsing and the
 * per-sector key state merge.  No device code lives here, so preconditions,
 * partial tables, duplicate rows and long-running stages are unit tested with
 * plain fixtures.
 */
namespace MfcRecovery
{

enum class Method
{
    KnownKeys,
    Dictionary,
    AutoPwn,
    Nested,
    StaticNested,
    Hardnested,
    Darkside,
    Sen,
    Isen,
};

enum class NonceType
{
    Unknown,
    Normal,
    Static,
    SuperStatic,
    StaticEncrypted,
};

enum class TaskState
{
    Idle,
    Running,
    Cancelling,
    Completed,
    Partial,
    Failed,
    Cancelled,
    Aborted,
};

QString methodName(Method method);
QString methodKey(Method method);
QString nonceTypeName(NonceType type);
QString taskStateName(TaskState state);

struct Advice
{
    Method method = Method::KnownKeys;
    bool applicable = false;
    bool preconditionUnverified = false;
    QString reason;
    QStringList requirements;
};

// Explain what is known, what is missing and which method is suggested.
QVector<Advice> advise(const Mfc::Document& document, NonceType nonce = NonceType::Unknown);

struct Candidate
{
    int sector = -1;
    char type = 'A';
    QString key;
    bool found = false;
    char marker = '\0';
    Mfc::KeySource source = Mfc::KeySource::Recovery;
};

struct ParseResult
{
    QVector<Candidate> keys;
    NonceType nonce = NonceType::Unknown;
    bool sawTable = false;
    bool sawCompletion = false;
    bool sawFailure = false;
    QStringList warnings;
};

// Parse one complete client output (ANSI is stripped here).  The sector number
// is taken from the table row, never from the row order, so a partial or
// shuffled table cannot shift a key into the wrong sector.
ParseResult parseOutput(const QString& cleanOutput, const Mfc::Spec& spec);

struct MergeResult
{
    int added = 0;
    int updated = 0;
    int conflicts = 0;
    QStringList notes;
};

// Merge only into the sectors named by the candidates.  A verified key is never
// overwritten by an empty or unverified value; conflicts are reported.
// session is the RF session that produced the table: a recovered key is only
// marked verified for that session.
MergeResult merge(const ParseResult& parsed, Mfc::Document* document,
                  bool markVerified, Mfc::KeySource fallbackSource, quint64 session = 0);

// Last recognisable phase in the accumulated output.
QString detectStage(const QString& cleanOutput);

QString cardArg(const Mfc::Spec& spec);
QString sectorArg(int sector);

// Command builders.  The profile supplies the command text; only the
// placeholders documented in config_rrgv4.23346.json are replaced.
struct Command
{
    QString text;
    QString successPattern;
    QString failurePattern;
    // The v4.23346 client calls kbd_enter_pressed() while this command runs
    // (hf mf chk, autopwn, nested, darkside, sen), so the command must run in
    // its own one-shot client process: a "rem" line queued behind it on the
    // pipe would be consumed as a key press and abort it.
    bool usesStdin = false;
    qint64 hardTimeoutMs = 30 * 60 * 1000;
};

Command buildCheck(const QVariantMap& profile, const Mfc::Spec& spec,
                   const QString& dictionary = QString(), bool includeDefaults = true);
Command buildAutoPwn(const QVariantMap& profile, const Mfc::Spec& spec,
                     const QString& dictionary = QString(), bool noSave = true);
Command buildNested(const QVariantMap& profile, const Mfc::Spec& spec, bool isStatic,
                    int knownSector, char knownType, const QString& knownKey);
Command buildHardnested(const QVariantMap& profile, const Mfc::Spec& spec,
                        int knownSector, char knownType, const QString& knownKey,
                        int targetSector, char targetType);
Command buildDarkside(const QVariantMap& profile);
Command buildSen(const QVariantMap& profile);
Command buildIsen(const QVariantMap& profile);

class Tracker
{
public:
    void start(Method method, int totalKeys);
    void setStage(const QString& stage);
    void setFound(int found);
    void finish(TaskState state, const QString& message = QString());
    void tick();

    TaskState state() const { return m_state; }
    Method method() const { return m_method; }
    QString stage() const { return m_stage; }
    QString message() const { return m_message; }
    qint64 elapsedMs() const;
    int found() const { return m_found; }
    int total() const { return m_total; }

private:
    Method m_method = Method::KnownKeys;
    TaskState m_state = TaskState::Idle;
    QString m_stage;
    QString m_message;
    QElapsedTimer m_timer;
    int m_found = 0;
    int m_total = 0;
};

} // namespace MfcRecovery

Q_DECLARE_METATYPE(MfcRecovery::Method)
Q_DECLARE_METATYPE(MfcRecovery::NonceType)
Q_DECLARE_METATYPE(MfcRecovery::TaskState)

#endif // MFCKEYRECOVERY_H

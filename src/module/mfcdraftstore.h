#ifndef MFCDRAFTSTORE_H
#define MFCDRAFTSTORE_H

#include <QDateTime>
#include <QString>
#include <QVector>

#include "mfcdocument.h"

/*
 * Draft store for documents that were never saved by the user.
 *
 * Drafts live apart from the backup library: a draft is the working state of a
 * tab (including the RF session that produced each block), while the library
 * holds explicit backups.  Both the document and the index are written through
 * QSaveFile, so a crash leaves either the old or the new state behind, never a
 * truncated file.  Restored drafts are marked as historical evidence by the
 * caller: the session ids in them are never valid for the card in the field.
 */
namespace Mfc
{

struct DraftInfo
{
    QString id;
    QString name;          // tab name at save time
    QString sourceKind;    // "card", "file", "scan" or "new"
    QDateTime savedAt;
    CardType cardType = CardType::M1K;
    QString uid;
    quint64 session = 0;   // RF session that produced the evidence
    int completeBlocks = 0;
    int blockCount = 0;
    int verifiedKeys = 0;
};

class DraftStore
{
public:
    explicit DraftStore(const QString& dir = QString());

    QString dir() const { return m_dir; }
    void setDir(const QString& dir) { m_dir = dir; }
    QString indexFile() const;
    bool isEnabled() const { return !m_dir.isEmpty(); }

    bool load(QString* error = nullptr);
    const QVector<DraftInfo>& drafts() const { return m_infos; }

    // Saves the document and its metadata.  A new id is generated when empty.
    bool save(const Document& doc, const QString& id, const QString& name,
              const QString& sourceKind, quint64 session, QString* savedId,
              QString* error = nullptr);
    bool load(const QString& id, Document* doc, DraftInfo* info, QString* error = nullptr) const;
    bool remove(const QString& id, QString* error = nullptr);
    // Drops index rows whose document file disappeared.
    void prune();

    static QString newId();

private:
    bool writeIndex(QString* error) const;

    QString m_dir;
    QVector<DraftInfo> m_infos;
};

} // namespace Mfc

#endif // MFCDRAFTSTORE_H

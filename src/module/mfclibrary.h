#ifndef MFCLIBRARY_H
#define MFCLIBRARY_H

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include "mfcdocument.h"

/*
 * Local MIFARE Classic backup library.
 *
 * The library is a directory with an index (library.json) and one file per
 * imported backup.  Entries keep the real file SHA-256, so two files with the
 * same content are reported as duplicates instead of being silently merged.
 * Only the GUI writes here; the model is independent of the device and of the
 * Qt widgets, so filtering, duplicate detection and batch export planning are
 * unit tested with temporary directories.
 */
namespace Mfc
{

struct LibraryEntry
{
    QString id;          // stable identifier of the entry (UUID)
    QString filePath;    // absolute path of the stored backup
    QString fileName;    // file name inside the library (display)
    QString alias;
    QString notes;
    QStringList tags;
    QString sha256;      // SHA-256 of the stored file
    CardType cardType = CardType::M1K;
    QString uid;
    QDateTime createdAt;  // card dump creation time when the file records one
    QDateTime importedAt;
    QString source;       // "file", "card", "scan" or "draft"
    QString format;       // "bin", "eml" or "json"
    int completeBlocks = 0;
    int blockCount = 0;
    int verifiedKeys = 0;
    int version = 1;      // backup version for this card, 1-based
    bool missing = false; // the file is gone; the entry is kept for reference
};

struct LibraryFilter
{
    QString text;              // alias, notes, tag, UID or file name
    bool hasType = false;
    CardType type = CardType::M1K;
    QString uid;               // exact UID, case-insensitive
    QDateTime from;            // importedAt >= from
    QDateTime to;              // importedAt <= to
    int minCompletePercent = -1;
    bool duplicatesOnly = false;
};

struct LibraryExportItem
{
    QString id;
    QString targetPath;
    bool exists = false;     // the target already exists (conflict)
    bool duplicateName = false; // another item in this batch wants the same path
};

class Library
{
public:
    explicit Library(const QString& rootDir = QString());

    QString rootDir() const { return m_root; }
    void setRootDir(const QString& rootDir) { m_root = rootDir; }
    QString indexFile() const;
    QString filesDir() const;

    // Reads library.json.  A missing index is an empty library, not an error;
    // a corrupt index is an error and leaves the previous entries untouched.
    bool load(QString* error = nullptr);
    // Atomic rewrite (QSaveFile + rename).
    bool save(QString* error = nullptr) const;
    bool isLoaded() const { return m_loaded; }

    const QVector<LibraryEntry>& entries() const { return m_entries; }
    QVector<LibraryEntry> filtered(const LibraryFilter& filter) const;
    bool entry(const QString& id, LibraryEntry* out) const;
    // Entries that share the same SHA-256 (order is stable).
    QVector<QPair<QString, QString>> duplicatePairs() const;
    bool isDuplicate(const QString& sha256, const QString& exceptId = QString()) const;

    // Validates the file as a Classic dump and copies it into the library.
    // duplicate is set when an entry with the same digest already exists; the
    // existing entry is returned and nothing is copied.
    bool importFile(const QString& filename, LibraryEntry* out,
                    QString* error = nullptr, bool* duplicate = nullptr);
    // Serialises the document (JSON by default) into the library.
    bool importDocument(const Document& doc, const QString& preferredName,
                        LibraryEntry* out, QString* error = nullptr, bool* duplicate = nullptr,
                        const QString& source = QStringLiteral("card"));

    bool updateEntry(const LibraryEntry& entry, QString* error = nullptr);
    bool remove(const QString& id, bool deleteFile, QString* error = nullptr);

    // Batch conversion/export planning.  Nothing is written here: the caller
    // shows the conflicts and only then converts.  format: "bin", "eml", "json".
    QVector<LibraryExportItem> planExport(const QVector<LibraryEntry>& entries,
                                          const QString& directory, const QString& format,
                                          QString* error = nullptr) const;
    static bool exportEntry(const LibraryEntry& entry, const QString& targetPath,
                            const QString& format, QString* error = nullptr);

    static QString normalizedFormat(const QString& format);
    static QString suggestedFileName(const LibraryEntry& entry, const QString& format);

private:
    LibraryEntry makeEntry(const Document& doc, const QString& path, const QString& format,
                           const QString& source) const;
    int nextVersion(const QString& uid, CardType type) const;
    bool storeBytes(const QByteArray& bytes, const QString& extension, QString* path,
                    QString* error) const;

    QString m_root;
    QVector<LibraryEntry> m_entries;
    bool m_loaded = false;
};

} // namespace Mfc

#endif // MFCLIBRARY_H

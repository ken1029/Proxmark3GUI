#include "module/mfclibrary.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace Mfc
{

namespace
{

QString digestOfFile(const QString& filename, QString* error = nullptr)
{
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot read %1: %2").arg(filename, file.errorString());
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot hash %1").arg(filename);
        return QString();
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString extensionForFormat(const QString& format)
{
    if(format == QStringLiteral("eml"))
        return QStringLiteral("eml");
    if(format == QStringLiteral("bin"))
        return QStringLiteral("bin");
    return QStringLiteral("json");
}

QString extensionOf(const QString& filename)
{
    const QString suffix = QFileInfo(filename).suffix().toLower();
    if(suffix == QStringLiteral("dump"))
        return QStringLiteral("bin");
    if(suffix == QStringLiteral("txt"))
        return QStringLiteral("eml");
    if(suffix == QStringLiteral("eml") || suffix == QStringLiteral("bin") || suffix == QStringLiteral("json"))
        return suffix;
    return QStringLiteral("bin");
}

QString safeName(const QString& text)
{
    QString result;
    for(const QChar& c : text)
    {
        if(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_'))
            result += c;
        else if(c.isSpace())
            result += QLatin1Char('_');
    }
    while(result.startsWith(QLatin1Char('_')))
        result.remove(0, 1);
    return result.isEmpty() ? QStringLiteral("backup") : result;
}

} // namespace

Library::Library(const QString& rootDir) : m_root(rootDir)
{
}

QString Library::indexFile() const
{
    return QDir(m_root).filePath(QStringLiteral("library.json"));
}

QString Library::filesDir() const
{
    return QDir(m_root).filePath(QStringLiteral("files"));
}

bool Library::load(QString* error)
{
    m_entries.clear();
    if(m_root.isEmpty())
    {
        m_loaded = true;
        return true;
    }
    QFile file(indexFile());
    if(!file.exists())
    {
        m_loaded = true;
        return true;
    }
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot open %1: %2").arg(indexFile(), file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        if(error != nullptr)
            *error = QStringLiteral("library index is corrupt at offset %1: %2")
                     .arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = json.object();
    if(root.value("format").toString() != QStringLiteral("mfc-library"))
    {
        if(error != nullptr)
            *error = QStringLiteral("not an MFC backup library index");
        return false;
    }
    for(const QJsonValue& value : root.value("entries").toArray())
    {
        const QJsonObject item = value.toObject();
        LibraryEntry entry;
        entry.id = item.value("id").toString();
        if(entry.id.isEmpty())
            continue;
        entry.filePath = item.value("path").toString();
        entry.fileName = item.value("name").toString();
        entry.alias = item.value("alias").toString();
        entry.notes = item.value("notes").toString();
        for(const QJsonValue& tag : item.value("tags").toArray())
            entry.tags << tag.toString();
        entry.sha256 = item.value("sha256").toString();
        CardType type = CardType::M1K;
        if(Spec::fromName(item.value("card").toString(), &type))
            entry.cardType = type;
        entry.uid = Document::normalizeHex(item.value("uid").toString());
        entry.createdAt = QDateTime::fromString(item.value("created").toString(), Qt::ISODate);
        entry.importedAt = QDateTime::fromString(item.value("imported").toString(), Qt::ISODate);
        entry.source = item.value("source").toString();
        entry.format = item.value("format").toString();
        entry.completeBlocks = item.value("complete").toInt();
        entry.blockCount = item.value("blocks").toInt();
        entry.verifiedKeys = item.value("keys").toInt();
        entry.version = qMax(1, item.value("version").toInt(1));
        entry.missing = entry.filePath.isEmpty() || !QFileInfo::exists(entry.filePath);
        m_entries.append(entry);
    }
    m_loaded = true;
    return true;
}

bool Library::save(QString* error) const
{
    if(m_root.isEmpty())
    {
        if(error != nullptr)
            *error = QStringLiteral("the backup library has no directory");
        return false;
    }
    QDir().mkpath(filesDir());
    QJsonArray array;
    for(const LibraryEntry& entry : m_entries)
    {
        QJsonObject item;
        item["id"] = entry.id;
        item["path"] = entry.filePath;
        item["name"] = entry.fileName;
        item["alias"] = entry.alias;
        item["notes"] = entry.notes;
        QJsonArray tags;
        for(const QString& tag : entry.tags)
            tags.append(tag);
        item["tags"] = tags;
        item["sha256"] = entry.sha256;
        item["card"] = Spec::get(entry.cardType).name();
        item["uid"] = entry.uid;
        item["created"] = entry.createdAt.toString(Qt::ISODate);
        item["imported"] = entry.importedAt.toString(Qt::ISODate);
        item["source"] = entry.source;
        item["format"] = entry.format;
        item["complete"] = entry.completeBlocks;
        item["blocks"] = entry.blockCount;
        item["keys"] = entry.verifiedKeys;
        item["version"] = entry.version;
        array.append(item);
    }
    QJsonObject root;
    root["format"] = "mfc-library";
    root["version"] = 1;
    root["entries"] = array;

    QSaveFile file(indexFile());
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot write %1: %2").arg(indexFile(), file.errorString());
        return false;
    }
    return true;
}

QVector<LibraryEntry> Library::filtered(const LibraryFilter& filter) const
{
    QVector<LibraryEntry> result;
    const QString text = filter.text.trimmed().toLower();
    QSet<QString> duplicates;
    if(filter.duplicatesOnly)
    {
        QHash<QString, int> counts;
        for(const LibraryEntry& entry : m_entries)
            if(!entry.sha256.isEmpty())
                counts[entry.sha256] += 1;
        for(auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            if(it.value() > 1)
                duplicates.insert(it.key());
    }
    for(const LibraryEntry& entry : m_entries)
    {
        if(filter.hasType && entry.cardType != filter.type)
            continue;
        if(!filter.uid.isEmpty() && entry.uid.compare(filter.uid, Qt::CaseInsensitive) != 0)
            continue;
        if(filter.from.isValid() && entry.importedAt.isValid() && entry.importedAt < filter.from)
            continue;
        if(filter.to.isValid() && entry.importedAt.isValid() && entry.importedAt > filter.to)
            continue;
        if(filter.minCompletePercent >= 0 && entry.blockCount > 0)
        {
            const int percent = entry.completeBlocks * 100 / entry.blockCount;
            if(percent < filter.minCompletePercent)
                continue;
        }
        if(filter.duplicatesOnly && !duplicates.contains(entry.sha256))
            continue;
        if(!text.isEmpty())
        {
            const QString haystack = (entry.alias + QLatin1Char(' ') + entry.notes + QLatin1Char(' ')
                                      + entry.tags.join(QLatin1Char(' ')) + QLatin1Char(' ')
                                      + entry.uid + QLatin1Char(' ') + entry.fileName).toLower();
            if(!haystack.contains(text))
                continue;
        }
        result.append(entry);
    }
    return result;
}

bool Library::entry(const QString& id, LibraryEntry* out) const
{
    for(const LibraryEntry& item : m_entries)
        if(item.id == id)
        {
            if(out != nullptr)
                *out = item;
            return true;
        }
    return false;
}

QVector<QPair<QString, QString>> Library::duplicatePairs() const
{
    QVector<QPair<QString, QString>> result;
    for(int i = 0; i < m_entries.size(); i++)
    {
        if(m_entries[i].sha256.isEmpty())
            continue;
        for(int j = i + 1; j < m_entries.size(); j++)
            if(!m_entries[j].sha256.isEmpty() && m_entries[i].sha256 == m_entries[j].sha256)
                result.append(qMakePair(m_entries[i].id, m_entries[j].id));
    }
    return result;
}

bool Library::isDuplicate(const QString& sha256, const QString& exceptId) const
{
    if(sha256.isEmpty())
        return false;
    for(const LibraryEntry& entry : m_entries)
        if(entry.id != exceptId && entry.sha256.compare(sha256, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

int Library::nextVersion(const QString& uid, CardType type) const
{
    int version = 1;
    for(const LibraryEntry& entry : m_entries)
        if(entry.cardType == type && !uid.isEmpty() && entry.uid.compare(uid, Qt::CaseInsensitive) == 0)
            version = qMax(version, entry.version + 1);
    return version;
}

LibraryEntry Library::makeEntry(const Document& doc, const QString& path, const QString& format,
                                const QString& source) const
{
    LibraryEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.filePath = path;
    entry.fileName = QFileInfo(path).fileName();
    entry.sha256 = digestOfFile(path);
    entry.cardType = doc.cardType();
    entry.uid = doc.uid;
    entry.createdAt = doc.createdAt;
    entry.importedAt = QDateTime::currentDateTime();
    entry.source = source;
    entry.format = format;
    entry.completeBlocks = doc.completeBlockCount();
    entry.blockCount = doc.blockCount();
    entry.verifiedKeys = doc.verifiedKeyCount();
    entry.version = nextVersion(doc.uid, doc.cardType());
    return entry;
}

bool Library::storeBytes(const QByteArray& bytes, const QString& extension, QString* path,
                         QString* error) const
{
    if(m_root.isEmpty())
    {
        if(error != nullptr)
            *error = QStringLiteral("the backup library has no directory");
        return false;
    }
    if(!QDir().mkpath(filesDir()))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot create %1").arg(filesDir());
        return false;
    }
    const QString digest = QString::fromLatin1(
                               QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    const QString name = QStringLiteral("%1.%2").arg(digest.left(16), extension);
    const QString target = QDir(filesDir()).filePath(name);
    if(QFileInfo::exists(target) && QFileInfo(target).size() == bytes.size())
    {
        if(path != nullptr)
            *path = target;
        return true;
    }
    QSaveFile file(target);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot write %1: %2").arg(target, file.errorString());
        return false;
    }
    if(path != nullptr)
        *path = target;
    return true;
}

bool Library::importFile(const QString& filename, LibraryEntry* out, QString* error, bool* duplicate)
{
    if(duplicate != nullptr)
        *duplicate = false;
    Document doc;
    const IoResult loaded = Document::loadDataFile(filename, QStringLiteral("auto"), &doc);
    if(!loaded.ok)
    {
        if(error != nullptr)
            *error = loaded.error;
        return false;
    }
    const QString sha256 = digestOfFile(filename, error);
    if(sha256.isEmpty())
        return false;
    for(const LibraryEntry& entry : m_entries)
    {
        if(entry.sha256.compare(sha256, Qt::CaseInsensitive) == 0 && !entry.missing)
        {
            if(duplicate != nullptr)
                *duplicate = true;
            if(out != nullptr)
                *out = entry;
            return true;
        }
    }

    QFile source(filename);
    if(!source.open(QIODevice::ReadOnly))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot read %1: %2").arg(filename, source.errorString());
        return false;
    }
    const QString format = normalizedFormat(extensionOf(filename));
    QString storedPath;
    if(!storeBytes(source.readAll(), extensionForFormat(format), &storedPath, error))
        return false;
    LibraryEntry entry = makeEntry(doc, storedPath, format, QStringLiteral("file"));
    m_entries.append(entry);
    if(!save(error))
    {
        m_entries.removeLast();
        return false;
    }
    if(out != nullptr)
        *out = entry;
    return true;
}

bool Library::importDocument(const Document& doc, const QString& preferredName, LibraryEntry* out,
                             QString* error, bool* duplicate, const QString& source)
{
    if(duplicate != nullptr)
        *duplicate = false;
    IoResult result;
    const QByteArray bytes = doc.dataFileBytes(QStringLiteral("json"), false, &result);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    const QString sha256 = QString::fromLatin1(
                               QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    for(const LibraryEntry& entry : m_entries)
    {
        if(entry.sha256.compare(sha256, Qt::CaseInsensitive) == 0 && !entry.missing)
        {
            if(duplicate != nullptr)
                *duplicate = true;
            if(out != nullptr)
                *out = entry;
            return true;
        }
    }
    QString storedPath;
    if(!storeBytes(bytes, QStringLiteral("json"), &storedPath, error))
        return false;
    LibraryEntry entry = makeEntry(doc, storedPath, QStringLiteral("json"), source);
    entry.fileName = safeName(preferredName.isEmpty() ? doc.uid : preferredName)
                     + QStringLiteral(".json");
    m_entries.append(entry);
    if(!save(error))
    {
        m_entries.removeLast();
        return false;
    }
    if(out != nullptr)
        *out = entry;
    return true;
}

bool Library::updateEntry(const LibraryEntry& entry, QString* error)
{
    for(int i = 0; i < m_entries.size(); i++)
    {
        if(m_entries[i].id != entry.id)
            continue;
        m_entries[i] = entry;
        return save(error);
    }
    if(error != nullptr)
        *error = QStringLiteral("no library entry %1").arg(entry.id);
    return false;
}

bool Library::remove(const QString& id, bool deleteFile, QString* error)
{
    for(int i = 0; i < m_entries.size(); i++)
    {
        if(m_entries[i].id != id)
            continue;
        const QString path = m_entries[i].filePath;
        m_entries.removeAt(i);
        if(deleteFile && !path.isEmpty() && QFileInfo::exists(path))
        {
            // Only files that live inside the library directory are removed.
            const QString filesRoot = QDir(filesDir()).absolutePath() + QLatin1Char('/');
            if(QDir::cleanPath(path).startsWith(filesRoot))
                QFile::remove(path);
        }
        return save(error);
    }
    if(error != nullptr)
        *error = QStringLiteral("no library entry %1").arg(id);
    return false;
}

QString Library::normalizedFormat(const QString& format)
{
    const QString f = format.trimmed().toLower();
    if(f == QStringLiteral("bin") || f == QStringLiteral("dump"))
        return QStringLiteral("bin");
    if(f == QStringLiteral("eml") || f == QStringLiteral("txt") || f == QStringLiteral("text"))
        return QStringLiteral("eml");
    return QStringLiteral("json");
}

QString Library::suggestedFileName(const LibraryEntry& entry, const QString& format)
{
    QString base = entry.alias.trimmed();
    if(base.isEmpty())
        base = entry.uid.trimmed();
    if(base.isEmpty())
        base = QFileInfo(entry.fileName).completeBaseName();
    base = safeName(base);
    return base + QLatin1Char('_') + QString::number(entry.version) + QLatin1Char('.')
           + extensionForFormat(normalizedFormat(format));
}

QVector<LibraryExportItem> Library::planExport(const QVector<LibraryEntry>& entries,
                                               const QString& directory, const QString& format,
                                               QString* error) const
{
    QVector<LibraryExportItem> items;
    if(directory.isEmpty())
    {
        if(error != nullptr)
            *error = QStringLiteral("no export directory");
        return items;
    }
    QSet<QString> used;
    for(const LibraryEntry& entry : entries)
    {
        LibraryExportItem item;
        item.id = entry.id;
        item.targetPath = QDir(directory).filePath(suggestedFileName(entry, format));
        item.exists = QFileInfo::exists(item.targetPath);
        const QString key = QDir::cleanPath(item.targetPath).toLower();
        item.duplicateName = used.contains(key);
        used.insert(key);
        items.append(item);
    }
    return items;
}

bool Library::exportEntry(const LibraryEntry& entry, const QString& targetPath,
                          const QString& format, QString* error)
{
    Document doc;
    const IoResult loaded = Document::loadDataFile(entry.filePath, QStringLiteral("auto"), &doc);
    if(!loaded.ok)
    {
        if(error != nullptr)
            *error = loaded.error;
        return false;
    }
    const QString directory = QFileInfo(targetPath).absolutePath();
    if(!QDir().mkpath(directory))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot create %1").arg(directory);
        return false;
    }
    const IoResult saved = doc.saveDataFile(targetPath, normalizedFormat(format), false);
    if(!saved.ok)
    {
        if(error != nullptr)
            *error = saved.error;
        return false;
    }
    return true;
}

} // namespace Mfc

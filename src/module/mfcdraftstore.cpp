#include "module/mfcdraftstore.h"

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

QString documentPath(const QString& dir, const QString& id)
{
    return QDir(dir).filePath(id + QStringLiteral(".json"));
}

} // namespace

DraftStore::DraftStore(const QString& dir) : m_dir(dir)
{
}

QString DraftStore::indexFile() const
{
    return QDir(m_dir).filePath(QStringLiteral("drafts.json"));
}

QString DraftStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool DraftStore::load(QString* error)
{
    m_infos.clear();
    if(m_dir.isEmpty())
        return true;
    QFile file(indexFile());
    if(!file.exists())
        return true;
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
            *error = QStringLiteral("draft index is corrupt at offset %1: %2")
                     .arg(parseError.offset).arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = json.object();
    for(const QJsonValue& value : root.value("drafts").toArray())
    {
        const QJsonObject item = value.toObject();
        DraftInfo info;
        info.id = item.value("id").toString();
        if(info.id.isEmpty())
            continue;
        info.name = item.value("name").toString();
        info.sourceKind = item.value("source").toString();
        info.savedAt = QDateTime::fromString(item.value("saved").toString(), Qt::ISODate);
        CardType type = CardType::M1K;
        if(Spec::fromName(item.value("card").toString(), &type))
            info.cardType = type;
        info.uid = Document::normalizeHex(item.value("uid").toString());
        info.session = static_cast<quint64>(item.value("session").toDouble());
        info.completeBlocks = item.value("complete").toInt();
        info.blockCount = item.value("blocks").toInt();
        info.verifiedKeys = item.value("keys").toInt();
        if(QFileInfo::exists(documentPath(m_dir, info.id)))
            m_infos.append(info);
    }
    return true;
}

bool DraftStore::writeIndex(QString* error) const
{
    QDir().mkpath(m_dir);
    QJsonArray array;
    for(const DraftInfo& info : m_infos)
    {
        QJsonObject item;
        item["id"] = info.id;
        item["name"] = info.name;
        item["source"] = info.sourceKind;
        item["saved"] = info.savedAt.toString(Qt::ISODate);
        item["card"] = Spec::get(info.cardType).name();
        item["uid"] = info.uid;
        item["session"] = static_cast<double>(info.session);
        item["complete"] = info.completeBlocks;
        item["blocks"] = info.blockCount;
        item["keys"] = info.verifiedKeys;
        array.append(item);
    }
    QJsonObject root;
    root["format"] = "mfc-drafts";
    root["version"] = 1;
    root["drafts"] = array;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    QSaveFile file(indexFile());
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot write %1: %2").arg(indexFile(), file.errorString());
        return false;
    }
    return true;
}

bool DraftStore::save(const Document& doc, const QString& id, const QString& name,
                      const QString& sourceKind, quint64 session, QString* savedId,
                      QString* error)
{
    if(m_dir.isEmpty())
    {
        if(error != nullptr)
            *error = QStringLiteral("drafts are disabled");
        return false;
    }
    if(!QDir().mkpath(m_dir))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot create %1").arg(m_dir);
        return false;
    }
    const QString draftId = id.isEmpty() ? newId() : id;
    const QByteArray bytes = doc.toJson();
    QSaveFile file(documentPath(m_dir, draftId));
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot write draft %1: %2").arg(draftId, file.errorString());
        return false;
    }

    DraftInfo info;
    info.id = draftId;
    info.name = name;
    info.sourceKind = sourceKind;
    info.savedAt = QDateTime::currentDateTime();
    info.cardType = doc.cardType();
    info.uid = doc.uid;
    info.session = session;
    info.completeBlocks = doc.completeBlockCount();
    info.blockCount = doc.blockCount();
    info.verifiedKeys = doc.verifiedKeyCount();
    bool replaced = false;
    for(int i = 0; i < m_infos.size(); i++)
    {
        if(m_infos[i].id != draftId)
            continue;
        m_infos[i] = info;
        replaced = true;
        break;
    }
    if(!replaced)
        m_infos.append(info);
    if(!writeIndex(error))
    {
        if(!replaced)
            m_infos.removeLast();
        return false;
    }
    if(savedId != nullptr)
        *savedId = draftId;
    return true;
}

bool DraftStore::load(const QString& id, Document* doc, DraftInfo* info, QString* error) const
{
    const QString path = documentPath(m_dir, id);
    const IoResult result = Document::loadDataFile(path, QStringLiteral("json"), doc);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    if(info != nullptr)
    {
        *info = DraftInfo();
        for(const DraftInfo& item : m_infos)
            if(item.id == id)
            {
                *info = item;
                break;
            }
        info->id = id;
    }
    return true;
}

bool DraftStore::remove(const QString& id, QString* error)
{
    for(int i = 0; i < m_infos.size(); i++)
    {
        if(m_infos[i].id != id)
            continue;
        m_infos.removeAt(i);
        QFile::remove(documentPath(m_dir, id));
        return writeIndex(error);
    }
    return true;
}

void DraftStore::prune()
{
    for(int i = m_infos.size() - 1; i >= 0; i--)
        if(!QFileInfo::exists(documentPath(m_dir, m_infos[i].id)))
            m_infos.removeAt(i);
}

} // namespace Mfc

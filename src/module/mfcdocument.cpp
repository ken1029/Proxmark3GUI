#include "mfcdocument.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

namespace Mfc
{

static const char* BLOCK_STATES[] = {"unknown", "read", "modified", "verified", "failed"};
static const char* KEY_STATES[] = {"unknown", "pending", "verified", "failed"};
static const char* KEY_SOURCES[] = {"none", "manual", "imported", "dictionary", "recovery", "card"};

static int stateIndex(BlockState state)
{
    return qBound(0, int(state), int(sizeof(BLOCK_STATES) / sizeof(BLOCK_STATES[0])) - 1);
}

static int stateIndex(KeyState state)
{
    return qBound(0, int(state), int(sizeof(KEY_STATES) / sizeof(KEY_STATES[0])) - 1);
}

static int stateIndex(KeySource source)
{
    return qBound(0, int(source), int(sizeof(KEY_SOURCES) / sizeof(KEY_SOURCES[0])) - 1);
}

QString blockStateName(BlockState state)
{
    return QString::fromLatin1(BLOCK_STATES[stateIndex(state)]);
}

QString keyStateName(KeyState state)
{
    return QString::fromLatin1(KEY_STATES[stateIndex(state)]);
}

QString keySourceName(KeySource source)
{
    return QString::fromLatin1(KEY_SOURCES[stateIndex(source)]);
}

QString diffStateName(DiffState state)
{
    switch(state)
    {
    case DiffState::Same:
        return QStringLiteral("same");
    case DiffState::Different:
        return QStringLiteral("different");
    case DiffState::Unknown:
        return QStringLiteral("unknown");
    case DiffState::Incomparable:
        return QStringLiteral("incomparable");
    }
    return QStringLiteral("unknown");
}

// ---------------------------------------------------------------- Spec

int Spec::blocksInSector(int sector) const
{
    if(sector < 0 || sector >= sectorCount)
        return 0;
    // 4K: sectors 0..31 hold 4 blocks, 32..39 hold 16.
    return sector >= 32 ? 16 : 4;
}

int Spec::firstBlockOfSector(int sector) const
{
    if(sector < 0 || sector >= sectorCount)
        return -1;
    if(sector < 32)
        return sector * 4;
    return 128 + (sector - 32) * 16;
}

int Spec::trailerBlockOfSector(int sector) const
{
    const int first = firstBlockOfSector(sector);
    if(first < 0)
        return -1;
    return first + blocksInSector(sector) - 1;
}

int Spec::sectorOfBlock(int block) const
{
    if(block < 0 || block >= blockCount)
        return -1;
    if(block < 128)
        return block / 4;
    return 32 + (block - 128) / 16;
}

bool Spec::isTrailerBlock(int block) const
{
    const int sector = sectorOfBlock(block);
    return sector >= 0 && trailerBlockOfSector(sector) == block;
}

QString Spec::name() const
{
    switch(type)
    {
    case CardType::Mini:
        return QStringLiteral("mini");
    case CardType::M1K:
        return QStringLiteral("1k");
    case CardType::M2K:
        return QStringLiteral("2k");
    case CardType::M4K:
        return QStringLiteral("4k");
    }
    return QStringLiteral("1k");
}

QString Spec::displayName() const
{
    switch(type)
    {
    case CardType::Mini:
        return QStringLiteral("MIFARE Classic Mini");
    case CardType::M1K:
        return QStringLiteral("MIFARE Classic 1K");
    case CardType::M2K:
        return QStringLiteral("MIFARE Classic 2K");
    case CardType::M4K:
        return QStringLiteral("MIFARE Classic 4K");
    }
    return QStringLiteral("MIFARE Classic 1K");
}

Spec Spec::get(CardType type)
{
    Spec spec;
    spec.type = type;
    switch(type)
    {
    case CardType::Mini:
        spec.sectorCount = 5;
        spec.blockCount = 20;
        break;
    case CardType::M1K:
        spec.sectorCount = 16;
        spec.blockCount = 64;
        break;
    case CardType::M2K:
        spec.sectorCount = 32;
        spec.blockCount = 128;
        break;
    case CardType::M4K:
        spec.sectorCount = 40;
        spec.blockCount = 256;
        break;
    }
    return spec;
}

bool Spec::fromName(const QString& name, CardType* type)
{
    const QString n = name.trimmed().toLower();
    if(n == QStringLiteral("mini") || n == QStringLiteral("min") || n == QStringLiteral("s20"))
        *type = CardType::Mini;
    else if(n == QStringLiteral("1k") || n == QStringLiteral("s50") || n == QStringLiteral("m1k"))
        *type = CardType::M1K;
    else if(n == QStringLiteral("2k") || n == QStringLiteral("m2k"))
        *type = CardType::M2K;
    else if(n == QStringLiteral("4k") || n == QStringLiteral("s70") || n == QStringLiteral("m4k"))
        *type = CardType::M4K;
    else
        return false;
    return true;
}

bool Spec::fromBlockCount(int count, CardType* type)
{
    switch(count)
    {
    case 20:
        *type = CardType::Mini;
        return true;
    case 64:
        *type = CardType::M1K;
        return true;
    case 128:
        *type = CardType::M2K;
        return true;
    case 256:
        *type = CardType::M4K;
        return true;
    default:
        return false;
    }
}

bool Spec::fromByteSize(qint64 size, CardType* type)
{
    return fromBlockCount(int(size / 16), type) && (size % 16 == 0);
}

// ---------------------------------------------------------------- Block

bool Block::complete() const
{
    return hex.size() == 32 && !hex.contains(QLatin1Char('?'));
}

QByteArray Block::bytes() const
{
    if(!complete())
        return QByteArray();
    return QByteArray::fromHex(hex.toLatin1());
}

void Block::clear()
{
    hex.clear();
    state = BlockState::Unknown;
}

// ---------------------------------------------------------------- KeyEntry

void KeyEntry::clear()
{
    value.clear();
    state = KeyState::Unknown;
    source = KeySource::None;
    verifiedAt = QDateTime();
}

// ---------------------------------------------------------------- Document

Document::Document(CardType type)
    : m_spec(Spec::get(type))
{
    m_blocks.resize(m_spec.blockCount);
    m_keyA.resize(m_spec.sectorCount);
    m_keyB.resize(m_spec.sectorCount);
    createdAt = QDateTime::currentDateTimeUtc();
}

void Document::setCardType(CardType type)
{
    if(type == m_spec.type)
        return;
    m_spec = Spec::get(type);
    m_blocks.clear();
    m_blocks.resize(m_spec.blockCount);
    m_keyA.clear();
    m_keyB.clear();
    m_keyA.resize(m_spec.sectorCount);
    m_keyB.resize(m_spec.sectorCount);
}

void Document::clear()
{
    for(Block& b : m_blocks)
        b.clear();
    for(KeyEntry& k : m_keyA)
        k.clear();
    for(KeyEntry& k : m_keyB)
        k.clear();
    uid.clear();
    sourceFile.clear();
}

const Block& Document::block(int index) const
{
    static const Block empty;
    if(index < 0 || index >= m_blocks.size())
        return empty;
    return m_blocks.at(index);
}

bool Document::setBlock(int index, const QString& hex, BlockState state)
{
    if(index < 0 || index >= m_blocks.size())
        return false;
    const QString normalized = normalizeHex(hex);
    if(normalized.isEmpty())
    {
        m_blocks[index].clear();
        return true;
    }
    if(!isBlockText(normalized))
        return false;
    m_blocks[index].hex = normalized;
    m_blocks[index].state = (normalized.isEmpty() || normalized.count(QLatin1Char('?')) == 32)
                            ? BlockState::Unknown : state;
    return true;
}

bool Document::setBlockBytes(int index, const QByteArray& data, BlockState state)
{
    if(data.size() != 16)
        return false;
    return setBlock(index, QString::fromLatin1(data.toHex().toUpper()), state);
}

bool Document::setBlockState(int index, BlockState state)
{
    if(index < 0 || index >= m_blocks.size())
        return false;
    m_blocks[index].state = state;
    return true;
}

QString Document::blockHex(int index) const
{
    return block(index).hex;
}

BlockState Document::blockState(int index) const
{
    return block(index).state;
}

bool Document::blockComplete(int index) const
{
    return block(index).complete();
}

int Document::completeBlockCount() const
{
    int count = 0;
    for(const Block& b : m_blocks)
        if(b.complete())
            count++;
    return count;
}

int Document::unknownBlockCount() const
{
    return m_spec.blockCount - completeBlockCount();
}

int Document::modifiedBlockCount() const
{
    int count = 0;
    for(const Block& b : m_blocks)
        if(b.state == BlockState::Modified)
            count++;
    return count;
}

const KeyEntry& Document::key(int sector, char type) const
{
    static const KeyEntry empty;
    if(sector < 0 || sector >= m_spec.sectorCount)
        return empty;
    return (type == 'B' || type == 'b') ? m_keyB.at(sector) : m_keyA.at(sector);
}

bool Document::setKey(int sector, char type, const QString& value, KeyState state, KeySource source)
{
    if(sector < 0 || sector >= m_spec.sectorCount)
        return false;
    const QString normalized = normalizeKey(value);
    if(!normalized.isEmpty() && !isKeyText(normalized))
        return false;
    KeyEntry& entry = (type == 'B' || type == 'b') ? m_keyB[sector] : m_keyA[sector];
    entry.value = normalized;
    entry.state = normalized.isEmpty() ? KeyState::Unknown : state;
    entry.source = normalized.isEmpty() ? KeySource::None : source;
    entry.verifiedAt = entry.state == KeyState::Verified ? QDateTime::currentDateTimeUtc() : QDateTime();
    return true;
}

bool Document::setKeyVerified(int sector, char type, const QString& value, KeySource source)
{
    return setKey(sector, type, value, KeyState::Verified, source);
}

bool Document::setKeyEntry(int sector, char type, const KeyEntry& entry)
{
    if(sector < 0 || sector >= m_spec.sectorCount)
        return false;
    if(entry.hasValue() && !isKeyText(entry.value))
        return false;
    KeyEntry& target = (type == 'B' || type == 'b') ? m_keyB[sector] : m_keyA[sector];
    target = entry;
    if(!target.hasValue())
        target.clear();
    return true;
}

int Document::verifiedKeyCount() const
{
    int count = 0;
    for(const KeyEntry& k : m_keyA)
        if(k.verified())
            count++;
    for(const KeyEntry& k : m_keyB)
        if(k.verified())
            count++;
    return count;
}

int Document::pendingKeyCount() const
{
    int count = 0;
    for(const KeyEntry& k : m_keyA)
        if(k.state == KeyState::Pending)
            count++;
    for(const KeyEntry& k : m_keyB)
        if(k.state == KeyState::Pending)
            count++;
    return count;
}

int Document::unknownKeyCount() const
{
    return keyCount() - verifiedKeyCount() - pendingKeyCount();
}

bool Document::isKeyText(const QString& key)
{
    if(key.size() != 12)
        return false;
    for(const QChar c : key)
    {
        const ushort u = c.unicode();
        if(!((u >= '0' && u <= '9') || (u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f')))
            return false;
    }
    return true;
}

bool Document::isBlockText(const QString& hex, bool allowUnknown)
{
    if(hex.size() != 32)
        return false;
    for(const QChar c : hex)
    {
        const ushort u = c.unicode();
        const bool isHex = (u >= '0' && u <= '9') || (u >= 'A' && u <= 'F') || (u >= 'a' && u <= 'f');
        if(!isHex && !(allowUnknown && u == '?'))
            return false;
    }
    return true;
}

QString Document::normalizeHex(const QString& text)
{
    QString result = text;
    result.remove(QRegularExpression(QStringLiteral("[\\s:]")));
    return result.toUpper();
}

QString Document::normalizeKey(const QString& key)
{
    return normalizeHex(key);
}

QString Document::maskedKey()
{
    return QStringLiteral("????????????");
}

// ---------------------------------------------------------------- JSON

QByteArray Document::toJson() const
{
    QJsonObject root;
    root["format"] = "mfc-document";
    root["version"] = 1;
    root["card"] = m_spec.name();
    root["uid"] = uid;
    root["created"] = createdAt.toString(Qt::ISODate);

    QJsonArray blocks;
    for(int i = 0; i < m_blocks.size(); i++)
    {
        QJsonObject item;
        item["i"] = i;
        item["state"] = blockStateName(m_blocks[i].state);
        item["data"] = m_blocks[i].hex;
        blocks.append(item);
    }
    root["blocks"] = blocks;

    auto keyArray = [this](bool keyA) {
        QJsonArray array;
        const QVector<KeyEntry>& list = keyA ? m_keyA : m_keyB;
        for(int i = 0; i < list.size(); i++)
        {
            QJsonObject item;
            item["i"] = i;
            item["value"] = list[i].value;
            item["state"] = keyStateName(list[i].state);
            item["source"] = keySourceName(list[i].source);
            if(list[i].verifiedAt.isValid())
                item["verified"] = list[i].verifiedAt.toString(Qt::ISODate);
            array.append(item);
        }
        return array;
    };
    QJsonObject keys;
    keys["A"] = keyArray(true);
    keys["B"] = keyArray(false);
    root["keys"] = keys;

    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

static bool parseKeyArray(const QJsonArray& array, int sectorCount, QVector<KeyEntry>* target,
                          QString* error)
{
    target->clear();
    target->resize(sectorCount);
    QSet<int> seen;
    for(const QJsonValue& value : array)
    {
        if(!value.isObject())
        {
            *error = QStringLiteral("key entry is not an object");
            return false;
        }
        const QJsonObject item = value.toObject();
        const int i = item.value("i").toInt(-1);
        if(i < 0 || i >= sectorCount)
        {
            *error = QStringLiteral("key sector %1 out of range").arg(i);
            return false;
        }
        if(seen.contains(i))
        {
            *error = QStringLiteral("duplicate key sector %1").arg(i);
            return false;
        }
        seen.insert(i);
        KeyEntry entry;
        entry.value = Document::normalizeKey(item.value("value").toString());
        if(!entry.value.isEmpty() && !Document::isKeyText(entry.value))
        {
            *error = QStringLiteral("key sector %1 is not 12 hex symbols").arg(i);
            return false;
        }
        const QString state = item.value("state").toString();
        entry.state = KeyState::Unknown;
        for(int s = 0; s <= int(KeyState::Failed); s++)
            if(state == QString::fromLatin1(KEY_STATES[s]))
                entry.state = KeyState(s);
        const QString source = item.value("source").toString();
        entry.source = KeySource::None;
        for(int s = 0; s <= int(KeySource::Card); s++)
            if(source == QString::fromLatin1(KEY_SOURCES[s]))
                entry.source = KeySource(s);
        entry.verifiedAt = QDateTime::fromString(item.value("verified").toString(), Qt::ISODate);
        if(entry.value.isEmpty())
        {
            entry.state = KeyState::Unknown;
            entry.source = KeySource::None;
        }
        (*target)[i] = entry;
    }
    return true;
}

static IoResult officialMfcFromJson(const QJsonObject& root, Document* out)
{
    IoResult result;
    QString fileType = root.value("FileType").toString().trimmed();
    if(!fileType.startsWith("mfc", Qt::CaseInsensitive))
    {
        result.error = QStringLiteral("not an official MIFARE Classic dump (FileType=%1)").arg(fileType);
        return result;
    }

    const QJsonObject blocks = root.value("blocks").toObject();
    int maxBlock = -1;
    for(auto it = blocks.constBegin(); it != blocks.constEnd(); ++it)
    {
        bool ok = false;
        const int index = it.key().toInt(&ok);
        if(ok)
            maxBlock = qMax(maxBlock, index);
    }
    if(maxBlock < 0)
    {
        result.error = QStringLiteral("official dump has no blocks");
        return result;
    }

    CardType type = CardType::M1K;
    if(maxBlock >= 128)
        type = CardType::M4K;
    else if(maxBlock >= 64)
        type = CardType::M2K;
    else if(maxBlock >= 20)
        type = CardType::M1K;
    else
        type = CardType::Mini;
    Document doc(type);

    const QJsonObject card = root.value("Card").toObject();
    doc.uid = Document::normalizeHex(card.value("UID").toString());
    for(int i = 0; i <= maxBlock && i < doc.blockCount(); i++)
    {
        const QString hex = Document::normalizeHex(blocks.value(QString::number(i)).toString());
        if(hex.isEmpty())
            continue;
        if(!Document::isBlockText(hex))
        {
            result.error = QStringLiteral("official dump block %1 is not 32 hex symbols").arg(i);
            return result;
        }
        doc.setBlock(i, hex, BlockState::Read);
    }

    const QJsonObject sectorKeys = root.value("SectorKeys").toObject();
    for(auto it = sectorKeys.constBegin(); it != sectorKeys.constEnd(); ++it)
    {
        bool ok = false;
        const int sector = it.key().toInt(&ok);
        if(!ok || sector < 0 || sector >= doc.sectorCount())
            continue;
        const QJsonObject keys = it.value().toObject();
        const QString keyA = Document::normalizeKey(keys.value("KeyA").toString());
        const QString keyB = Document::normalizeKey(keys.value("KeyB").toString());
        if(Document::isKeyText(keyA))
            doc.setKey(sector, 'A', keyA, KeyState::Pending, KeySource::Imported);
        if(Document::isKeyText(keyB))
            doc.setKey(sector, 'B', keyB, KeyState::Pending, KeySource::Imported);
    }

    if(doc.uid.isEmpty() && doc.blockComplete(0))
        doc.uid = doc.blockHex(0).left(8);
    *out = doc;
    result.ok = true;
    result.detected = type;
    return result;
}

static IoResult documentFromJson(const QByteArray& data, Document* out)
{
    IoResult result;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        result.error = QStringLiteral("JSON parse error at offset %1: %2")
                       .arg(parseError.offset).arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = json.object();
    if(root.contains("FileType") && root.value("FileType").toString().startsWith("mfc", Qt::CaseInsensitive))
        return officialMfcFromJson(root, out);
    const QString format = root.value("format").toString();
    if(format != QStringLiteral("mfc-document") && format != QStringLiteral("mfc-keys"))
    {
        result.error = QStringLiteral("not an mfc document (format=%1)").arg(format);
        return result;
    }
    if(root.value("version").toInt(0) != 1)
    {
        result.error = QStringLiteral("unsupported document version");
        return result;
    }
    CardType type;
    if(!Spec::fromName(root.value("card").toString(), &type))
    {
        result.error = QStringLiteral("unknown card type '%1'").arg(root.value("card").toString());
        return result;
    }

    Document doc(type);
    doc.uid = Document::normalizeHex(root.value("uid").toString());
    doc.createdAt = QDateTime::fromString(root.value("created").toString(), Qt::ISODate);
    if(!doc.createdAt.isValid())
        doc.createdAt = QDateTime::currentDateTimeUtc();

    if(root.contains("blocks"))
    {
        if(!root.value("blocks").isArray())
        {
            result.error = QStringLiteral("blocks is not an array");
            return result;
        }
        QSet<int> seen;
        for(const QJsonValue& value : root.value("blocks").toArray())
        {
            if(!value.isObject())
            {
                result.error = QStringLiteral("block entry is not an object");
                return result;
            }
            const QJsonObject item = value.toObject();
            const int i = item.value("i").toInt(-1);
            if(i < 0 || i >= doc.blockCount())
            {
                result.error = QStringLiteral("block %1 out of range").arg(i);
                return result;
            }
            if(seen.contains(i))
            {
                result.error = QStringLiteral("duplicate block %1").arg(i);
                return result;
            }
            seen.insert(i);
            QString hex = Document::normalizeHex(item.value("data").toString());
            if(hex.isEmpty())
                hex = QString(32, QLatin1Char('?'));
            if(!Document::isBlockText(hex))
            {
                result.error = QStringLiteral("block %1 is not 32 hex symbols").arg(i);
                return result;
            }
            BlockState state = BlockState::Unknown;
            const QString stateName = item.value("state").toString();
            for(int s = 0; s <= int(BlockState::Failed); s++)
                if(stateName == QString::fromLatin1(BLOCK_STATES[s]))
                    state = BlockState(s);
            if(hex.count(QLatin1Char('?')) == 32)
                state = BlockState::Unknown;
            doc.setBlock(i, hex, state);
        }
    }

    if(root.contains("keys"))
    {
        const QJsonObject keys = root.value("keys").toObject();
        for(char type : {'A', 'B'})
        {
            const QString name = QString(QChar(type));
            if(!keys.contains(name))
                continue;
            QVector<KeyEntry> entries;
            if(!parseKeyArray(keys.value(name).toArray(), doc.sectorCount(), &entries, &result.error))
                return result;
            for(int i = 0; i < entries.size(); i++)
            {
                const KeyEntry& entry = entries.at(i);
                doc.setKeyEntry(i, type, entry);
            }
        }
    }

    *out = doc;
    result.ok = true;
    result.detected = type;
    return result;
}

// ---------------------------------------------------------------- data I/O

IoResult Document::loadDataFile(const QString& filename, const QString& formatHint, Document* out)
{
    IoResult result;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("cannot open %1: %2").arg(filename, file.errorString());
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    QString format = formatHint.trimmed().toLower();
    if(format.isEmpty() || format == QStringLiteral("auto"))
    {
        const QByteArray trimmed = data.trimmed();
        if(trimmed.startsWith('{'))
            format = QStringLiteral("json");
        else
        {
            CardType type;
            if(Spec::fromByteSize(data.size(), &type) && data.size() >= 320)
                format = QStringLiteral("bin");
            else
                format = QStringLiteral("eml");
        }
    }

    Document doc;
    if(format == QStringLiteral("json"))
    {
        result = documentFromJson(data, &doc);
        if(!result.ok)
            return result;
    }
    else if(format == QStringLiteral("bin"))
    {
        CardType type;
        if(!Spec::fromByteSize(data.size(), &type))
        {
            result.error = QStringLiteral("binary size %1 is not a MIFARE Classic dump "
                                          "(expected 320, 1024, 2048 or 4096 bytes)").arg(data.size());
            return result;
        }
        doc = Document(type);
        for(int i = 0; i < doc.blockCount(); i++)
            doc.setBlockBytes(i, data.mid(i * 16, 16), BlockState::Read);
    }
    else if(format == QStringLiteral("eml") || format == QStringLiteral("txt"))
    {
        QString text = QString::fromUtf8(data);
        text.remove(QLatin1Char('\r'));
        QStringList lines = text.split(QLatin1Char('\n'));
        while(!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        CardType type;
        if(!Spec::fromBlockCount(lines.size(), &type))
        {
            result.error = QStringLiteral("text dump has %1 lines; expected 20, 64, 128 or 256")
                           .arg(lines.size());
            return result;
        }
        doc = Document(type);
        for(int i = 0; i < lines.size(); i++)
        {
            const QString hex = normalizeHex(lines[i]);
            if(!isBlockText(hex))
            {
                result.error = QStringLiteral("line %1 is not 32 hex symbols").arg(i + 1);
                return result;
            }
            doc.m_blocks[i].hex = hex;
            doc.m_blocks[i].state = hex.count(QLatin1Char('?')) == 32
                                    ? BlockState::Unknown : BlockState::Read;
        }
    }
    else
    {
        result.error = QStringLiteral("unsupported data format '%1'").arg(format);
        return result;
    }

    if(doc.uid.isEmpty() && doc.blockComplete(0))
        doc.uid = doc.blockHex(0).left(8);
    doc.sourceFile = QFileInfo(filename).absoluteFilePath();
    *out = doc;
    result.ok = true;
    result.detected = doc.cardType();
    return result;
}

QByteArray Document::dataFileBytes(const QString& format, bool allowUnknownBin, IoResult* result) const
{
    IoResult local;
    const QString f = format.trimmed().toLower();
    QByteArray output;

    if(f == QStringLiteral("bin"))
    {
        if(!allowUnknownBin && unknownBlockCount() > 0)
        {
            local.error = QStringLiteral("%1 block(s) were never read; a raw binary dump would "
                                         "invent data. Use EML/JSON or confirm zeros are acceptable.")
                          .arg(unknownBlockCount());
        }
        else
        {
            for(const Block& b : m_blocks)
            {
                if(b.complete())
                    output += b.bytes();
                else
                {
                    output += QByteArray(16, '\0');
                    local.warning = QStringLiteral("incomplete blocks were written as 00");
                }
            }
            local.ok = true;
        }
    }
    else if(f == QStringLiteral("eml") || f == QStringLiteral("txt"))
    {
        for(const Block& b : m_blocks)
        {
            QString hex = b.hex;
            if(hex.isEmpty())
                hex = QString(32, QLatin1Char('?'));
            output += hex.toLatin1();
            output += '\n';
        }
        local.ok = true;
    }
    else if(f == QStringLiteral("json"))
    {
        output = toJson();
        local.ok = true;
    }
    else
    {
        local.error = QStringLiteral("unsupported data format '%1'").arg(format);
    }

    local.detected = cardType();
    if(result != nullptr)
        *result = local;
    return local.ok ? output : QByteArray();
}

IoResult Document::saveDataFile(const QString& filename, const QString& format, bool allowUnknownBin) const
{
    IoResult result;
    const QByteArray data = dataFileBytes(format, allowUnknownBin, &result);
    if(!result.ok)
        return result;

    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly))
    {
        result.ok = false;
        result.error = QStringLiteral("cannot write %1: %2").arg(filename, file.errorString());
        return result;
    }
    if(file.write(data) != data.size() || !file.commit())
    {
        result.ok = false;
        result.error = QStringLiteral("cannot write %1: %2").arg(filename, file.errorString());
        return result;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------- key I/O

QByteArray Document::keyFileBytes(const QString& layout, bool fillUnknownWithFF, IoResult* result) const
{
    IoResult local;
    const QString l = layout.trimmed().toLower();
    QByteArray output;

    auto keyBytes = [&](const KeyEntry& entry, bool* unknown) {
        QByteArray bytes;
        if(entry.hasValue())
        {
            bytes = QByteArray::fromHex(entry.value.toLatin1());
        }
        else
        {
            *unknown = true;
            bytes = QByteArray(6, char(0xFF));
        }
        return bytes;
    };

    if(l == QStringLiteral("rrg") || l == QStringLiteral("legacy"))
    {
        bool unknown = false;
        QByteArray keyA;
        QByteArray keyB;
        for(int s = 0; s < sectorCount(); s++)
        {
            bool uA = false, uB = false;
            keyA += keyBytes(m_keyA.at(s), &uA);
            keyB += keyBytes(m_keyB.at(s), &uB);
            unknown = unknown || uA || uB;
        }
        if(unknown && !fillUnknownWithFF)
        {
            local.error = QStringLiteral("unknown key(s) cannot be represented without a placeholder");
        }
        else
        {
            if(l == QStringLiteral("rrg"))
            {
                // RRG createMfcKeyDump(): all key A first, then all key B.
                output = keyA + keyB;
            }
            else
            {
                // Legacy Proxmark3GUI layout: A0 B0 A1 B1 ...
                for(int s = 0; s < sectorCount(); s++)
                {
                    output += keyA.mid(s * 6, 6);
                    output += keyB.mid(s * 6, 6);
                }
            }
            if(unknown)
                local.warning = QStringLiteral("unknown key(s) were written as FFFFFFFFFFFF");
            local.ok = true;
        }
    }
    else if(l == QStringLiteral("text"))
    {
        for(int s = 0; s < sectorCount(); s++)
        {
            for(char type : {'A', 'B'})
            {
                const KeyEntry& entry = key(s, type);
                if(entry.hasValue())
                    output += QStringLiteral("%1:%2:%3\n").arg(s).arg(type).arg(entry.value).toLatin1();
                else if(fillUnknownWithFF)
                    output += QStringLiteral("%1:%2:FFFFFFFFFFFF\n").arg(s).arg(type).toLatin1();
            }
        }
        local.warning = fillUnknownWithFF ? QStringLiteral("unknown key(s) were written as FFFFFFFFFFFF")
                                          : QString();
        local.ok = true;
    }
    else if(l == QStringLiteral("json"))
    {
        output = toJson();
        local.ok = true;
    }
    else
    {
        local.error = QStringLiteral("unsupported key layout '%1'").arg(layout);
    }

    local.detected = cardType();
    if(result != nullptr)
        *result = local;
    return local.ok ? output : QByteArray();
}

bool Document::parseKeyText(const QString& text, Document* doc, QString* error, QString* warning)
{
    QSet<QString> seen;
    int nextSector = 0;
    int nextType = 0; // 0 -> A, 1 -> B
    int lineNo = 0;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")));
    QRegularExpression explicitRe(QStringLiteral("^\\s*(\\d{1,2})\\s*[:|,]\\s*([ABab])\\s*[:|,]\\s*([0-9A-Fa-f?]{12})\\s*$"));
    QRegularExpression typedRe(QStringLiteral("^\\s*([ABab])\\s*[:= ]\\s*([0-9A-Fa-f?]{12})\\s*$"));
    QRegularExpression pairRe(QStringLiteral("^\\s*([0-9A-Fa-f?]{12})\\s+([0-9A-Fa-f?]{12})\\s*$"));
    QRegularExpression singleRe(QStringLiteral("^\\s*([0-9A-Fa-f?]{12})\\s*$"));

    for(const QString& raw : lines)
    {
        lineNo++;
        const QString line = raw.trimmed();
        if(line.isEmpty() || line.startsWith('#') || line.startsWith("//"))
            continue;

        int sector = -1;
        char type = 'A';
        QString value;
        QRegularExpressionMatch match = explicitRe.match(line);
        if(match.hasMatch())
        {
            sector = match.captured(1).toInt();
            type = match.captured(2).toUpper().at(0).toLatin1();
            value = match.captured(3).toUpper();
        }
        else if((match = typedRe.match(line)).hasMatch())
        {
            type = match.captured(1).toUpper().at(0).toLatin1();
            value = match.captured(2).toUpper();
            sector = nextSector;
            if(type == 'B')
                nextSector++;
        }
        else if((match = pairRe.match(line)).hasMatch())
        {
            sector = nextSector;
            if(!doc->setKey(sector, 'A', match.captured(1), KeyState::Pending, KeySource::Imported)
               || !doc->setKey(sector, 'B', match.captured(2), KeyState::Pending, KeySource::Imported))
            {
                *error = QStringLiteral("line %1: sector %2 out of range").arg(lineNo).arg(sector);
                return false;
            }
            seen.insert(QStringLiteral("%1A").arg(sector));
            seen.insert(QStringLiteral("%1B").arg(sector));
            nextSector = sector + 1;
            nextType = 0;
            continue;
        }
        else if((match = singleRe.match(line)).hasMatch())
        {
            sector = nextSector;
            type = nextType == 0 ? 'A' : 'B';
            value = match.captured(1).toUpper();
            if(type == 'B')
                nextSector++;
            nextType = (nextType + 1) % 2;
        }
        else
        {
            *error = QStringLiteral("line %1: expected 12 hex symbols or sector:type:key")
                     .arg(lineNo);
            return false;
        }

        if(sector < 0 || sector >= doc->sectorCount())
        {
            *error = QStringLiteral("line %1: sector %2 out of range").arg(lineNo).arg(sector);
            return false;
        }
        const QString id = QStringLiteral("%1%2").arg(sector).arg(type);
        if(seen.contains(id))
            *warning = QStringLiteral("line %1: duplicate sector %2 key %3 ignored")
                       .arg(lineNo).arg(sector).arg(type);
        seen.insert(id);
        if(value == QStringLiteral("????????????"))
            doc->setKey(sector, type, QString(), KeyState::Unknown, KeySource::None);
        else
            doc->setKey(sector, type, value, KeyState::Pending, KeySource::Imported);
    }
    return true;
}

static bool looksLikeKeyText(const QByteArray& data)
{
    if(data.isEmpty())
        return false;
    bool hasNewline = false;
    for(const char raw : data)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        if(c == '\n' || c == '\r')
        {
            hasNewline = true;
            continue;
        }
        if(c == ' ' || c == '\t' || c == ':' || c == '#' || c == '/'
           || c == '?' || c == '|' || c == ','
           || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
            continue;
        return false;
    }
    return hasNewline;
}

IoResult Document::loadKeyFile(const QString& filename, const QString& layoutHint, Document* out)
{
    IoResult result;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("cannot open %1: %2").arg(filename, file.errorString());
        return result;
    }
    const QByteArray data = file.readAll();
    file.close();

    const QByteArray trimmed = data.trimmed();
    QString layout = layoutHint.trimmed().toLower();
    if(layout.isEmpty() || layout == QStringLiteral("auto"))
    {
        if(trimmed.startsWith('{'))
            layout = QStringLiteral("json");
        else if(looksLikeKeyText(data))
            layout = QStringLiteral("text");
        else if(data.size() == 5 * 12 || data.size() == 16 * 12
                || data.size() == 32 * 12 || data.size() == 40 * 12)
        {
            result.ambiguousLayout = true;
            result.error = QStringLiteral("binary key file layout is ambiguous: RRG key.bin stores "
                                          "all key A first, the legacy GUI stores A0 B0 A1 B1. "
                                          "Choose the layout explicitly.");
            return result;
        }
        else
            layout = QStringLiteral("text");
    }

    Document doc;
    if(layout == QStringLiteral("json"))
    {
        result = documentFromJson(data, &doc);
        if(!result.ok)
            return result;
    }
    else if(layout == QStringLiteral("rrg") || layout == QStringLiteral("legacy"))
    {
        CardType type;
        switch(data.size())
        {
        case 5 * 12:
            type = CardType::Mini;
            break;
        case 16 * 12:
            type = CardType::M1K;
            break;
        case 32 * 12:
            type = CardType::M2K;
            break;
        case 40 * 12:
            type = CardType::M4K;
            break;
        default:
            result.error = QStringLiteral("key file size %1 is not 20/64/128/256 * 12 bytes")
                           .arg(data.size());
            return result;
        }
        doc = Document(type);
        const int sectors = doc.sectorCount();
        auto readKey = [&data](int index) {
            return QString::fromLatin1(data.mid(index * 6, 6).toHex().toUpper());
        };
        if(layout == QStringLiteral("rrg"))
        {
            for(int s = 0; s < sectors; s++)
            {
                doc.setKey(s, 'A', readKey(s), KeyState::Pending, KeySource::Imported);
                doc.setKey(s, 'B', readKey(sectors + s), KeyState::Pending, KeySource::Imported);
            }
        }
        else
        {
            for(int s = 0; s < sectors; s++)
            {
                doc.setKey(s, 'A', readKey(s * 2), KeyState::Pending, KeySource::Imported);
                doc.setKey(s, 'B', readKey(s * 2 + 1), KeyState::Pending, KeySource::Imported);
            }
        }
        result.warning = QStringLiteral("imported keys are unverified candidates; "
                                        "FFFFFFFFFFFF may be a placeholder, not a default key");
    }
    else if(layout == QStringLiteral("text"))
    {
        // Parse into the largest card, then shrink to the smallest card that
        // holds every referenced sector.  A text key file carries no capacity,
        // so inventing one would be worse than reporting the inferred type.
        Document parsed(CardType::M4K);
        QString error, warning;
        if(!parseKeyText(QString::fromUtf8(data), &parsed, &error, &warning))
        {
            result.error = error;
            return result;
        }
        int maxSector = -1;
        for(int s = 0; s < parsed.sectorCount(); s++)
            if(parsed.key(s, 'A').hasValue() || parsed.key(s, 'B').hasValue())
                maxSector = s;
        // Text carries no capacity.  Default to 1K (the common card) instead
        // of shrinking a normal key list to a rare Mini.
        CardType type = CardType::M1K;
        if(maxSector >= 32)
            type = CardType::M4K;
        else if(maxSector >= 16)
            type = CardType::M2K;
        doc = Document(type);
        for(int s = 0; s <= maxSector && s < doc.sectorCount(); s++)
        {
            for(char keyType : {'A', 'B'})
            {
                const KeyEntry& entry = parsed.key(s, keyType);
                doc.setKey(s, keyType, entry.value, entry.state, entry.source);
            }
        }
        result.warning = warning;
        if(!warning.isEmpty())
            result.warning += QStringLiteral("; ");
        result.warning += QStringLiteral("imported keys are unverified candidates");
    }
    else
    {
        result.error = QStringLiteral("unsupported key layout '%1'").arg(layout);
        return result;
    }

    *out = doc;
    result.ok = true;
    result.detected = doc.cardType();
    return result;
}

IoResult Document::saveKeyFile(const QString& filename, const QString& layout, bool fillUnknownWithFF) const
{
    IoResult result;
    const QByteArray data = keyFileBytes(layout, fillUnknownWithFF, &result);
    if(!result.ok)
        return result;
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly))
    {
        result.ok = false;
        result.error = QStringLiteral("cannot write %1: %2").arg(filename, file.errorString());
        return result;
    }
    if(file.write(data) != data.size() || !file.commit())
    {
        result.ok = false;
        result.error = QStringLiteral("cannot write %1: %2").arg(filename, file.errorString());
        return result;
    }
    return result;
}

// ---------------------------------------------------------------- compare

CompareResult Document::compare(const Document& other) const
{
    CompareResult result;
    const int maxBlocks = qMax(blockCount(), other.blockCount());
    const int commonBlocks = qMin(blockCount(), other.blockCount());
    result.capacityMismatch = cardType() != other.cardType();
    if(result.capacityMismatch)
        result.notes << QStringLiteral("capacity differs: %1 vs %2")
                     .arg(cardDisplayName(), other.cardDisplayName());
    if(!uid.isEmpty() && !other.uid.isEmpty() && uid != other.uid)
    {
        result.uidMismatch = true;
        result.notes << QStringLiteral("UID differs: %1 vs %2").arg(uid, other.uid);
    }

    for(int i = 0; i < maxBlocks; i++)
    {
        Diff diff;
        diff.block = i;
        if(i >= commonBlocks)
        {
            diff.state = DiffState::Incomparable;
            diff.a = i < blockCount() ? blockHex(i) : QString();
            diff.b = i < other.blockCount() ? other.blockHex(i) : QString();
            diff.note = QStringLiteral("block does not exist on the smaller card");
            result.incomparable++;
            result.blocks.append(diff);
            continue;
        }
        diff.a = blockHex(i);
        diff.b = other.blockHex(i);
        if(!blockComplete(i) || !other.blockComplete(i))
        {
            diff.state = DiffState::Unknown;
            diff.note = QStringLiteral("at least one side was not read");
            result.unknown++;
        }
        else if(diff.a == diff.b)
        {
            diff.state = DiffState::Same;
            result.same++;
        }
        else
        {
            diff.state = DiffState::Different;
            if(spec().isTrailerBlock(i))
                diff.note = QStringLiteral("trailer: keys or access bits differ");
            result.different++;
        }
        result.blocks.append(diff);
    }

    const int maxSectors = qMax(sectorCount(), other.sectorCount());
    const int commonSectors = qMin(sectorCount(), other.sectorCount());
    for(int s = 0; s < maxSectors; s++)
    {
        for(char type : {'A', 'B'})
        {
            KeyDiff diff;
            diff.sector = s;
            diff.type = type;
            if(s >= commonSectors)
            {
                diff.state = DiffState::Incomparable;
                result.keys.append(diff);
                continue;
            }
            const KeyEntry& a = key(s, type);
            const KeyEntry& b = other.key(s, type);
            diff.a = a.value;
            diff.b = b.value;
            if(!a.hasValue() || !b.hasValue())
                diff.state = DiffState::Unknown;
            else if(a.value == b.value)
                diff.state = DiffState::Same;
            else
                diff.state = DiffState::Different;
            result.keys.append(diff);
        }
    }
    return result;
}

QByteArray Document::canonicalBytes() const
{
    QByteArray out;
    out += "MFC1\n";
    out += "card=" + cardName().toLatin1() + "\n";
    out += "uid=" + uid.toLatin1() + "\n";
    for(int i = 0; i < blockCount(); i++)
    {
        const Block& b = m_blocks.at(i);
        out += QStringLiteral("B%1,%2,%3\n").arg(i)
               .arg(blockStateName(b.state))
               .arg(b.hex.isEmpty() ? QString(32, QLatin1Char('?')) : b.hex).toLatin1();
    }
    for(int s = 0; s < sectorCount(); s++)
    {
        for(char type : {'A', 'B'})
        {
            const KeyEntry& k = key(s, type);
            out += QStringLiteral("K%1%2,%3,%4,%5\n").arg(s).arg(type)
                   .arg(keyStateName(k.state))
                   .arg(keySourceName(k.source))
                   .arg(k.value).toLatin1();
        }
    }
    return out;
}

QString Document::sha256() const
{
    return QString::fromLatin1(QCryptographicHash::hash(canonicalBytes(), QCryptographicHash::Sha256).toHex());
}

} // namespace Mfc

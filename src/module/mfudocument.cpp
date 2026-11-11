#include "module/mfudocument.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextCodec>

namespace Mfu
{

namespace
{

const char* const PAGE_STATES[] = {"unknown", "read", "modified", "failed"};

QString cleanText(const QString& text)
{
    QString clean = text;
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;]*[A-Za-z]"));
    clean.remove(ansi);
    clean.remove(QLatin1Char('\r'));
    return clean;
}

QString hexBytes(const QByteArray& bytes)
{
    QString result;
    for(int i = 0; i < bytes.size(); i++)
    {
        if(i != 0)
            result += QLatin1Char(' ');
        result += QStringLiteral("%1").arg(static_cast<quint8>(bytes.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return result;
}

QByteArray bytesFromHex(const QString& text)
{
    QString hex = text;
    hex.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    QByteArray result;
    for(int i = 0; i + 1 < hex.size(); i += 2)
    {
        bool ok = false;
        const int value = hex.mid(i, 2).toInt(&ok, 16);
        if(!ok)
            return QByteArray();
        result.append(static_cast<char>(value));
    }
    return result;
}

// Static lock bits use the NXP mapping the client prints in `hf mfu dump`:
// bit_stat[j] is bit (7 - j % 8) of lock byte j / 8.
bool staticLockBit(const QByteArray& lock, int page)
{
    static const int map[13] = {4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8};
    if(page < 3 || page > 15)
        return false;
    const int index = map[page - 3];
    if(index / 8 >= lock.size())
        return false;
    return (static_cast<quint8>(lock.at(index / 8)) & (1 << (7 - index % 8))) != 0;
}

bool dynamicLockBit(const QByteArray& lock, int page)
{
    int bit = -1;
    if(page >= 16 && page <= 19)
        bit = 6;
    else if(page >= 20 && page <= 23)
        bit = 5;
    else if(page >= 24 && page <= 27)
        bit = 4;
    else if(page >= 28 && page <= 31)
        bit = 2;
    else if(page >= 32 && page <= 35)
        bit = 1;
    else if(page >= 36 && page <= 39)
        bit = 0;
    else if(page == 40)
        bit = 12;
    else if(page == 41)
        bit = 11;
    else if(page == 42)
        bit = 10;
    else if(page == 43)
        bit = 9;
    if(bit < 0)
        return false;
    const int byteIndex = bit / 8;
    if(byteIndex >= lock.size())
        return false;
    return (static_cast<quint8>(lock.at(byteIndex)) & (1 << (7 - bit % 8))) != 0;
}

const char* const URI_PREFIXES[] =
{
    "", "http://www.", "https://www.", "http://", "https://", "tel:", "mailto:",
    "ftp://anonymous:anonymous@", "ftp://ftp.", "ftps://", "sftp://", "smb://",
    "nfs://", "ftp://", "dav://", "news:", "telnet://", "imap:", "rtsp://",
    "urn:", "pop:", "sip:", "sips:", "tftp:", "btspp://", "btl2cap://",
    "btgoep://", "tcpobex://", "irdaobex://", "file://", "urn:epc:id:",
    "urn:epc:tag:", "urn:epc:pat:", "urn:epc:raw:", "urn:epc:", "urn:nfc:",
};

QString decodeNdefRecord(const QByteArray& type, const QByteArray& payload, quint8 tnf)
{
    if(tnf == 0x01 && type == QByteArray("U") && !payload.isEmpty())
    {
        const quint8 prefix = static_cast<quint8>(payload.at(0));
        const QString rest = QString::fromUtf8(payload.mid(1));
        if(prefix < sizeof(URI_PREFIXES) / sizeof(URI_PREFIXES[0]))
            return QString::fromLatin1(URI_PREFIXES[prefix]) + rest;
        return rest;
    }
    if(tnf == 0x01 && type == QByteArray("T") && payload.size() >= 1)
    {
        const quint8 status = static_cast<quint8>(payload.at(0));
        const int languageLength = status & 0x3f;
        if(payload.size() >= 1 + languageLength)
        {
            const QByteArray text = payload.mid(1 + languageLength);
            return (status & 0x80) ? QString::fromUtf16(reinterpret_cast<const ushort*>(text.constData()),
                                                        text.size() / 2)
                                   : QString::fromUtf8(text);
        }
    }
    return QString();
}

QVector<NdefRecord> parseNdefMessage(const QByteArray& message, bool* complete)
{
    QVector<NdefRecord> records;
    if(complete != nullptr)
        *complete = true;
    int offset = 0;
    while(offset < message.size())
    {
        const quint8 flags = static_cast<quint8>(message.at(offset));
        if(offset + 2 > message.size())
        {
            if(complete != nullptr)
                *complete = false;
            break;
        }
        const int typeLength = static_cast<quint8>(message.at(offset + 1));
        int headerLength = 2;
        int payloadLength = 0;
        const bool shortRecord = (flags & 0x10) != 0;
        if(shortRecord)
        {
            if(offset + 3 > message.size())
            {
                if(complete != nullptr)
                    *complete = false;
                break;
            }
            payloadLength = static_cast<quint8>(message.at(offset + 2));
            headerLength = 3;
        }
        else
        {
            if(offset + 6 > message.size())
            {
                if(complete != nullptr)
                    *complete = false;
                break;
            }
            payloadLength = (static_cast<quint8>(message.at(offset + 2)) << 24)
                            | (static_cast<quint8>(message.at(offset + 3)) << 16)
                            | (static_cast<quint8>(message.at(offset + 4)) << 8)
                            | static_cast<quint8>(message.at(offset + 5));
            headerLength = 6;
        }
        if(flags & 0x08) // ID length present
            headerLength += 1;
        const int typeOffset = offset + headerLength;
        const int payloadOffset = typeOffset + typeLength;
        if(payloadOffset + payloadLength > message.size())
        {
            if(complete != nullptr)
                *complete = false;
            break;
        }
        NdefRecord record;
        record.type = message.mid(typeOffset, typeLength);
        record.payload = message.mid(payloadOffset, payloadLength);
        record.typeName = QString::fromLatin1(record.type);
        const quint8 tnf = flags & 0x07;
        record.text = decodeNdefRecord(record.type, record.payload, tnf);
        record.decoded = !record.text.isNull();
        records.append(record);
        offset = payloadOffset + payloadLength;
        if(flags & 0x40) // message end
            break;
    }
    return records;
}

} // namespace

QString pageStateName(PageState state)
{
    const int index = static_cast<int>(state);
    if(index >= 0 && index < 4)
        return QString::fromLatin1(PAGE_STATES[index]);
    return QStringLiteral("unknown");
}

QString typeName(Type type)
{
    return Spec::get(type).name;
}

Spec Spec::get(Type type)
{
    Spec spec;
    spec.type = type;
    switch(type)
    {
    case Type::UL:
        spec.pages = 16;
        spec.userEnd = 15;
        spec.name = QStringLiteral("UL");
        spec.displayName = QStringLiteral("MIFARE Ultralight (MF0ICU1)");
        break;
    case Type::ULC:
        spec.pages = 48;
        spec.userEnd = 39;
        spec.name = QStringLiteral("UL-C");
        spec.displayName = QStringLiteral("MIFARE Ultralight C (MF0ULC)");
        break;
    case Type::ULEV1_48:
        spec.pages = 20;
        spec.userEnd = 15;
        spec.name = QStringLiteral("UL EV1 48");
        spec.displayName = QStringLiteral("MIFARE Ultralight EV1 48bytes (MF0UL1101)");
        break;
    case Type::ULEV1_128:
        spec.pages = 41;
        spec.userEnd = 35;
        spec.name = QStringLiteral("UL EV1 128");
        spec.displayName = QStringLiteral("MIFARE Ultralight EV1 128bytes (MF0UL2101)");
        break;
    case Type::ULEV1:
        spec.pages = 41;
        spec.userEnd = 35;
        spec.name = QStringLiteral("UL EV1");
        spec.displayName = QStringLiteral("MIFARE Ultralight EV1 (unknown size)");
        break;
    case Type::ULAES:
        spec.pages = 48;
        spec.userEnd = 39;
        spec.name = QStringLiteral("UL AES");
        spec.displayName = QStringLiteral("MIFARE Ultralight AES");
        break;
    case Type::NTAG203:
        spec.pages = 42;
        spec.userEnd = 39;
        spec.name = QStringLiteral("NTAG203");
        spec.displayName = QStringLiteral("NTAG 203 144bytes (NT2H0301F0DT)");
        break;
    case Type::NTAG210:
        spec.pages = 20;
        spec.userEnd = 15;
        spec.name = QStringLiteral("NTAG210");
        spec.displayName = QStringLiteral("NTAG 210 48bytes (NT2L1011G0DU)");
        break;
    case Type::NTAG212:
        spec.pages = 41;
        spec.userEnd = 35;
        spec.name = QStringLiteral("NTAG212");
        spec.displayName = QStringLiteral("NTAG 212 128bytes (NT2L1211G0DU)");
        break;
    case Type::NTAG213:
        spec.pages = 45;
        spec.userEnd = 39;
        spec.name = QStringLiteral("NTAG213");
        spec.displayName = QStringLiteral("NTAG 213 144bytes (NT2H1311G0DU)");
        break;
    case Type::NTAG215:
        spec.pages = 135;
        spec.userEnd = 129;
        spec.name = QStringLiteral("NTAG215");
        spec.displayName = QStringLiteral("NTAG 215 504bytes (NT2H1511G0DU)");
        break;
    case Type::NTAG216:
        spec.pages = 231;
        spec.userEnd = 225;
        spec.name = QStringLiteral("NTAG216");
        spec.displayName = QStringLiteral("NTAG 216 888bytes (NT2H1611G0DU)");
        break;
    case Type::MyD:
        spec.pages = 0;
        spec.name = QStringLiteral("My-D");
        spec.displayName = QStringLiteral("Infineon My-D");
        break;
    case Type::Other:
        spec.name = QStringLiteral("other");
        spec.displayName = QStringLiteral("Ultralight/NTAG compatible tag");
        break;
    case Type::Unknown:
        break;
    }
    return spec;
}

Type Spec::fromTypeString(const QString& text)
{
    const QString value = text.toUpper();
    if(value.contains(QStringLiteral("ULTRALIGHT C")))
        return Type::ULC;
    if(value.contains(QStringLiteral("ULTRALIGHT AES")))
        return Type::ULAES;
    if(value.contains(QStringLiteral("ULTRALIGHT EV1 48")))
        return Type::ULEV1_48;
    if(value.contains(QStringLiteral("ULTRALIGHT EV1 128")))
        return Type::ULEV1_128;
    if(value.contains(QStringLiteral("ULTRALIGHT EV1")))
        return Type::ULEV1;
    if(value.contains(QStringLiteral("ULTRALIGHT")))
        return Type::UL;
    if(value.contains(QStringLiteral("NTAG 203")))
        return Type::NTAG203;
    if(value.contains(QStringLiteral("NTAG 210")))
        return Type::NTAG210;
    if(value.contains(QStringLiteral("NTAG 212")))
        return Type::NTAG212;
    if(value.contains(QStringLiteral("NTAG 213")))
        return Type::NTAG213;
    if(value.contains(QStringLiteral("NTAG 215")))
        return Type::NTAG215;
    if(value.contains(QStringLiteral("NTAG 216")))
        return Type::NTAG216;
    if(value.contains(QStringLiteral("NTAG")))
        return Type::Other;
    if(value.contains(QStringLiteral("MY-D")) || value.contains(QStringLiteral("MY_D")))
        return Type::MyD;
    if(value.contains(QStringLiteral("UL")))
        return Type::UL;
    return Type::Unknown;
}

bool Spec::fromName(const QString& name, Type* type)
{
    const QString value = name.trimmed().toUpper();
    for(int i = int(Type::Unknown); i <= int(Type::Other); i++)
    {
        const Spec spec = get(Type(i));
        if(spec.name.toUpper() == value || spec.displayName.toUpper() == value)
        {
            if(type != nullptr)
                *type = spec.type;
            return true;
        }
    }
    return false;
}

QByteArray Page::bytes() const
{
    if(hex.size() != 8)
        return QByteArray();
    return bytesFromHex(hex);
}

Document::Document(Type type)
{
    setType(type);
}

void Document::setType(Type type)
{
    m_spec = Spec::get(type);
    resizePages();
}

void Document::resizePages()
{
    const int count = m_spec.pages;
    if(count <= 0)
        return;
    const int old = m_pages.size();
    m_pages.resize(count);
    for(int i = old; i < count; i++)
    {
        m_pages[i].state = PageState::Unknown;
        m_pages[i].otp = (i == 3);
        m_pages[i].config = (i > m_spec.userEnd);
    }
}

const Page& Document::page(int index) const
{
    static const Page empty;
    if(index < 0 || index >= m_pages.size())
        return empty;
    return m_pages.at(index);
}

bool Document::setPage(int index, const QString& hex, PageState state)
{
    if(index < 0 || index >= m_pages.size())
        return false;
    const QString normalized = Document::normalizedHex(hex);
    if(normalized.size() != 8)
        return false;
    m_pages[index].hex = normalized;
    m_pages[index].state = state;
    return true;
}

bool Document::setPageBytes(int index, const QByteArray& data, PageState state)
{
    if(data.size() != 4)
        return false;
    return setPage(index, hexBytes(data).remove(QLatin1Char(' ')), state);
}

QString Document::pageHex(int index) const
{
    return page(index).hex;
}

PageState Document::pageState(int index) const
{
    return page(index).state;
}

bool Document::pageComplete(int index) const
{
    return page(index).complete();
}

int Document::readPages() const
{
    int count = 0;
    for(const Page& item : m_pages)
        if(item.complete())
            count++;
    return count;
}

int Document::unknownPages() const
{
    return m_pages.size() - readPages();
}

int Document::lockedPages() const
{
    int count = 0;
    for(int i = 0; i < m_pages.size(); i++)
        if(locked(i))
            count++;
    return count;
}

bool Document::locked(int index) const
{
    if(index < 0 || index >= m_pages.size())
        return false;
    if(index <= 2)
        return true; // UID pages are always read-only
    if(index <= 15)
        return staticLockBit(staticLock, index);
    return dynamicLockBit(dynamicLock, index);
}

void Document::applyLockBits()
{
    for(int i = 0; i < m_pages.size(); i++)
        m_pages[i].locked = locked(i);
}

QByteArray Document::ndefBytes() const
{
    return parseNdef().raw;
}

Ndef Document::parseNdef() const
{
    Ndef ndef;
    if(!pageComplete(3))
    {
        ndef.error = QStringLiteral("the capability container (page 3) was not read");
        return ndef;
    }
    const QByteArray cc = page(3).bytes();
    if(cc.isEmpty() || (static_cast<quint8>(cc.at(0)) != 0xE1 && static_cast<quint8>(cc.at(0)) != 0xF1))
    {
        ndef.error = QStringLiteral("no NDEF capability container (magic %1)")
                     .arg(cc.isEmpty() ? QStringLiteral("??") : hexBytes(cc.left(1)));
        return ndef;
    }
    ndef.present = true;
    const int declared = static_cast<quint8>(cc.at(2)) * 8;

    QByteArray data;
    for(int i = m_spec.userStart; i < m_pages.size(); i++)
    {
        if(!m_pages[i].complete())
            break;
        data += m_pages[i].bytes();
        if(declared > 0 && data.size() >= declared)
            break;
    }
    if(declared > 0 && data.size() < declared)
    {
        ndef.complete = false;
        ndef.error = QStringLiteral("read %1 of %2 NDEF bytes").arg(data.size()).arg(declared);
    }
    else
        ndef.complete = true;
    if(declared > 0)
        data.truncate(qMin(declared, data.size()));

    int offset = 0;
    while(offset < data.size())
    {
        const quint8 type = static_cast<quint8>(data.at(offset));
        if(type == 0x00)
        {
            offset++;
            continue;
        }
        if(type == 0xFE)
            break;
        if(offset + 1 >= data.size())
        {
            ndef.complete = false;
            break;
        }
        int length = static_cast<quint8>(data.at(offset + 1));
        int header = 2;
        if(type == 0x03 && length == 0xFF)
        {
            if(offset + 3 >= data.size())
            {
                ndef.complete = false;
                break;
            }
            length = (static_cast<quint8>(data.at(offset + 2)) << 8)
                     | static_cast<quint8>(data.at(offset + 3));
            header = 4;
        }
        else if(type != 0x01 && type != 0x02 && type != 0x03)
        {
            ndef.complete = false;
            ndef.error = QStringLiteral("unknown TLV 0x%1").arg(type, 2, 16, QLatin1Char('0'));
            break;
        }
        if(offset + header + length > data.size())
        {
            ndef.complete = false;
            break;
        }
        if(type == 0x03)
        {
            ndef.raw = data.mid(offset + header, length);
            bool messageComplete = true;
            ndef.records = parseNdefMessage(ndef.raw, &messageComplete);
            if(!messageComplete)
                ndef.complete = false;
            break;
        }
        offset += header + length;
    }
    if(ndef.raw.isEmpty() && ndef.present)
        ndef.error = QStringLiteral("no NDEF message in the data area");
    return ndef;
}

IoResult Document::parseInfo(const QString& text, Document* out)
{
    IoResult result;
    const QString clean = cleanText(text);
    if(clean.trimmed().isEmpty())
    {
        result.error = QStringLiteral("empty client output");
        return result;
    }
    Document doc(out != nullptr ? out->type() : Type::Unknown);
    bool typeSeen = false;
    const QRegularExpression uidRe(QStringLiteral("UID\\s*:\\s*((?:[0-9A-Fa-f]{2}\\s*)+)"),
                                   QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch uidMatch = uidRe.match(clean);
    if(uidMatch.hasMatch())
        doc.uid = Document::normalizedHex(uidMatch.captured(1));

    const QRegularExpression typeRe(QStringLiteral("TYPE\\s*:\\s*([^\\r\\n]+)"),
                                    QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch typeMatch = typeRe.match(clean);
    if(typeMatch.hasMatch())
    {
        doc.setType(Spec::fromTypeString(typeMatch.captured(1)));
        typeSeen = true;
    }
    const QRegularExpression productRe(QStringLiteral("Product type\\s*:\\s*([^\\r\\n]+)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch productMatch = productRe.match(clean);
    if(productMatch.hasMatch())
        doc.productType = productMatch.captured(1).trimmed();

    const QRegularExpression versionRe(QStringLiteral("Raw bytes\\s*:\\s*((?:[0-9A-Fa-f]{2}\\s*){8,10})"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch versionMatch = versionRe.match(clean);
    if(versionMatch.hasMatch())
        doc.versionHex = Document::normalizedHex(versionMatch.captured(1));

    const QRegularExpression lockRe(QStringLiteral("^\\s*Lock\\s*:\\s*((?:[0-9A-Fa-f]{2}\\s*){2})"),
                                    QRegularExpression::MultilineOption);
    const QRegularExpressionMatch lockMatch = lockRe.match(clean);
    if(lockMatch.hasMatch())
        doc.staticLock = bytesFromHex(lockMatch.captured(1));

    const QRegularExpression dynamicRe(QStringLiteral("Dynamic lock[^\\r\\n]*?((?:[0-9A-Fa-f]{2}\\s*){3})"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch dynamicMatch = dynamicRe.match(clean);
    if(dynamicMatch.hasMatch())
        doc.dynamicLock = bytesFromHex(dynamicMatch.captured(1));

    const QRegularExpression signatureRe(QStringLiteral("Signature\\s*\\.*\\s*:?\\s*((?:[0-9A-Fa-f]{2}\\s*){16,48})"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch signatureMatch = signatureRe.match(clean);
    if(signatureMatch.hasMatch())
        doc.signature = Document::normalizedHex(signatureMatch.captured(1));

    doc.rawInfo = clean;
    if(doc.uid.isEmpty())
    {
        result.error = QStringLiteral("no UID in the client output (is a card present?)");
        return result;
    }
    if(!typeSeen)
        result.warning = QStringLiteral("the tag type was not recognised; pages are unknown");
    doc.applyLockBits();
    if(out != nullptr)
        *out = doc;
    result.ok = true;
    return result;
}

IoResult Document::parseDump(const QString& text, Document* out, int startPage)
{
    IoResult result;
    const QString clean = cleanText(text);
    const QStringList lines = clean.split(QLatin1Char('\n'));
    const QRegularExpression rowRe(QStringLiteral(
                                       "^\\s*(\\d+)/0x([0-9A-Fa-f]{2})\\s*\\|\\s*"
                                       "([0-9A-Fa-f]{2}(?:\\s+[0-9A-Fa-f]{2}){3})\\s*\\|"
                                       "\\s*([01?])?\\s*\\|"));

    QMap<int, QByteArray> pages;
    int maxPage = -1;
    for(const QString& line : lines)
    {
        const QRegularExpressionMatch match = rowRe.match(line);
        if(!match.hasMatch())
            continue;
        bool ok = false;
        const int page = match.captured(1).toInt(&ok);
        if(!ok)
            continue;
        const QByteArray bytes = bytesFromHex(match.captured(3));
        if(bytes.size() != 4)
            continue;
        pages.insert(page, bytes);
        maxPage = qMax(maxPage, page);
    }
    if(pages.isEmpty())
    {
        result.error = QStringLiteral("no page rows in the dump output");
        return result;
    }

    Type type = out != nullptr ? out->type() : Type::Unknown;
    if(type == Type::Unknown)
    {
        // Guess from the last page in the table.  The caller can correct an
        // unusual tag; a known type from `hf mfu info` is always preferred.
        const int count = maxPage + 1;
        if(count <= 16)
            type = Type::UL;
        else if(count <= 20)
            type = Type::ULEV1_48;
        else if(count <= 41)
            type = Type::NTAG212;
        else if(count <= 42)
            type = Type::NTAG203;
        else if(count <= 45)
            type = Type::NTAG213;
        else if(count <= 48)
            type = Type::ULC;
        else if(count <= 135)
            type = Type::NTAG215;
        else
            type = Type::NTAG216;
    }
    Document doc(type);
    if(out != nullptr)
    {
        doc.uid = out->uid;
        doc.rawInfo = out->rawInfo;
        doc.versionHex = out->versionHex;
        doc.signature = out->signature;
    }
    for(auto it = pages.constBegin(); it != pages.constEnd(); ++it)
        doc.setPageBytes(it.key() - (startPage > 0 ? startPage : 0), it.value(), PageState::Read);
    doc.rawDump = clean;
    if(doc.pageComplete(2) && doc.pageComplete(3))
    {
        const QByteArray low = doc.page(2).bytes();
        const QByteArray high = doc.page(3).bytes();
        doc.staticLock = QByteArray() + low.at(2) + low.at(3);
        if(doc.spec().pages >= 45 && doc.pageComplete(40))
        {
            const QByteArray config = doc.page(40).bytes();
            doc.dynamicLock = config.left(3);
        }
    }
    doc.applyLockBits();
    if(out != nullptr)
        *out = doc;
    result.ok = true;
    return result;
}

IoResult Document::loadDumpFile(const QString& filename, Document* out)
{
    IoResult result;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("cannot read %1: %2").arg(filename, file.errorString());
        return result;
    }
    const QByteArray data = file.readAll();
    const bool looksText = data.contains("/0x") || data.contains("block#");
    if(!looksText)
    {
        if(data.size() % 4 != 0 || data.isEmpty())
        {
            result.error = QStringLiteral("%1 is not a 4-byte page dump").arg(filename);
            return result;
        }
        Document doc(out != nullptr ? out->type() : Type::Unknown);
        if(!doc.spec().known())
        {
            const int count = data.size() / 4;
            if(count <= 16)
                doc.setType(Type::UL);
            else if(count <= 20)
                doc.setType(Type::ULEV1_48);
            else if(count <= 45)
                doc.setType(Type::NTAG213);
            else if(count <= 135)
                doc.setType(Type::NTAG215);
            else
                doc.setType(Type::NTAG216);
        }
        for(int i = 0; i < data.size() / 4; i++)
            doc.setPageBytes(i, data.mid(i * 4, 4), PageState::Read);
        doc.sourceFile = filename;
        doc.applyLockBits();
        if(out != nullptr)
            *out = doc;
        result.ok = true;
        return result;
    }
    result = parseDump(QString::fromUtf8(data), out);
    if(result.ok && out != nullptr)
        out->sourceFile = filename;
    return result;
}

IoResult Document::loadInfoFile(const QString& filename, Document* out)
{
    IoResult result;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("cannot read %1: %2").arg(filename, file.errorString());
        return result;
    }
    result = parseInfo(QString::fromUtf8(file.readAll()), out);
    if(result.ok && out != nullptr)
        out->sourceFile = filename;
    return result;
}

QByteArray Document::toJson() const
{
    QJsonObject root;
    root["format"] = "mfu-document";
    root["version"] = 1;
    root["type"] = m_spec.name;
    root["uid"] = uid;
    root["product"] = productType;
    root["versionBytes"] = versionHex;
    root["signature"] = signature;
    root["staticLock"] = hexBytes(staticLock);
    root["dynamicLock"] = hexBytes(dynamicLock);
    root["source"] = sourceFile;
    QJsonArray pages;
    for(int i = 0; i < m_pages.size(); i++)
    {
        QJsonObject item;
        item["i"] = i;
        item["data"] = m_pages[i].hex;
        item["state"] = pageStateName(m_pages[i].state);
        item["locked"] = m_pages[i].locked;
        pages.append(item);
    }
    root["pages"] = pages;
    if(!rawInfo.isEmpty())
        root["info"] = rawInfo;
    if(!rawDump.isEmpty())
        root["dump"] = rawDump;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

IoResult Document::fromJson(const QByteArray& data, Document* out)
{
    IoResult result;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(data, &parseError);
    if(parseError.error != QJsonParseError::NoError || !json.isObject())
    {
        result.error = QStringLiteral("MFU JSON parse error at %1: %2")
                       .arg(parseError.offset).arg(parseError.errorString());
        return result;
    }
    const QJsonObject root = json.object();
    if(root.value("format").toString() != QStringLiteral("mfu-document"))
    {
        result.error = QStringLiteral("not an mfu-document");
        return result;
    }
    Type type = Type::Unknown;
    Spec::fromName(root.value("type").toString(), &type);
    Document doc(type);
    doc.uid = Document::normalizedHex(root.value("uid").toString());
    doc.productType = root.value("product").toString();
    doc.versionHex = Document::normalizedHex(root.value("versionBytes").toString());
    doc.signature = Document::normalizedHex(root.value("signature").toString());
    doc.staticLock = bytesFromHex(root.value("staticLock").toString());
    doc.dynamicLock = bytesFromHex(root.value("dynamicLock").toString());
    doc.sourceFile = root.value("source").toString();
    doc.rawInfo = root.value("info").toString();
    doc.rawDump = root.value("dump").toString();
    for(const QJsonValue& value : root.value("pages").toArray())
    {
        const QJsonObject item = value.toObject();
        const int index = item.value("i").toInt(-1);
        const QString hex = Document::normalizedHex(item.value("data").toString());
        PageState state = PageState::Unknown;
        for(int i = 0; i <= int(PageState::Failed); i++)
            if(pageStateName(PageState(i)) == item.value("state").toString())
                state = PageState(i);
        if(index >= 0 && hex.size() == 8)
            doc.setPage(index, hex, state);
    }
    doc.applyLockBits();
    if(out != nullptr)
        *out = doc;
    result.ok = true;
    return result;
}

bool Document::saveJson(const QString& filename, QString* error) const
{
    const QByteArray bytes = toJson();
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot write %1: %2").arg(filename, file.errorString());
        return false;
    }
    return true;
}

IoResult Document::loadJson(const QString& filename, Document* out)
{
    IoResult result;
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        result.error = QStringLiteral("cannot read %1: %2").arg(filename, file.errorString());
        return result;
    }
    result = fromJson(file.readAll(), out);
    return result;
}

QString Document::normalizedHex(const QString& text)
{
    QString hex = text;
    hex.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
    return hex.toUpper();
}

} // namespace Mfu

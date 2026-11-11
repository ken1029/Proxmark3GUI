#include "module/tracemodel.h"

#include <QRegularExpression>
#include <QStringList>

namespace Trace
{

namespace
{

QString cleanLine(const QString& text)
{
    QString clean = text;
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;]*[A-Za-z]"));
    clean.remove(ansi);
    clean.remove(QLatin1Char('\r'));
    return clean;
}

QByteArray parseData(const QString& text, bool* parityError, bool* shortBytes)
{
    QByteArray data;
    *parityError = false;
    *shortBytes = false;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    for(QString token : tokens)
    {
        if(token.endsWith(QLatin1Char('!')))
        {
            *parityError = true;
            token.chop(1);
        }
        if(token.endsWith(QLatin1Char('\'')))
        {
            *shortBytes = true;
            token.chop(1);
        }
        token.remove(QLatin1Char('['));
        token.remove(QLatin1Char(']'));
        if(token.size() != 2)
            continue;
        bool ok = false;
        const int value = token.toInt(&ok, 16);
        if(!ok)
            continue;
        data.append(static_cast<char>(value));
    }
    return data;
}

Direction parseDirection(const QString& text)
{
    const QString value = text.trimmed().toLower();
    if(value == QStringLiteral("rdr") || value == QStringLiteral("reader"))
        return Direction::Reader;
    if(value == QStringLiteral("tag") || value == QStringLiteral("card"))
        return Direction::Tag;
    return Direction::Unknown;
}

Crc parseCrc(const QString& text)
{
    const QString value = text.trimmed().toLower();
    if(value.isEmpty())
        return Crc::None;
    if(value == QStringLiteral("ok"))
        return Crc::Ok;
    if(value == QStringLiteral("!!") || value == QStringLiteral("fail") || value == QStringLiteral("bad"))
        return Crc::Fail;
    if(value == QStringLiteral("a ok"))
        return Crc::AOk;
    if(value == QStringLiteral("b ok"))
        return Crc::BOk;
    return Crc::Unknown;
}

QString hexSpaced(const QByteArray& data)
{
    QString result;
    for(int i = 0; i < data.size(); i++)
    {
        if(i != 0)
            result += QLatin1Char(' ');
        result += QStringLiteral("%1").arg(static_cast<quint8>(data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
    }
    return result;
}

} // namespace

QString directionName(Direction direction)
{
    switch(direction)
    {
    case Direction::Reader:
        return QStringLiteral("Rdr");
    case Direction::Tag:
        return QStringLiteral("Tag");
    case Direction::Unknown:
        break;
    }
    return QStringLiteral("?");
}

QString crcName(Crc crc)
{
    switch(crc)
    {
    case Crc::Ok:
        return QStringLiteral("ok");
    case Crc::Fail:
        return QStringLiteral("!!");
    case Crc::AOk:
        return QStringLiteral("A ok");
    case Crc::BOk:
        return QStringLiteral("B ok");
    case Crc::None:
        return QString();
    case Crc::Unknown:
        break;
    }
    return QStringLiteral("?");
}

ParseResult parse(const QString& text, bool relativeUs)
{
    ParseResult result;
    result.raw = text;
    const QStringList lines = text.split(QLatin1Char('\n'));
    bool sawHeader = false;
    Packet current;
    bool haveCurrent = false;

    auto finish = [&]() {
        if(!haveCurrent)
            return;
        if(current.data.isEmpty() && current.annotation.isEmpty())
        {
            haveCurrent = false;
            return;
        }
        current.index = result.packets.size();
        result.packets.append(current);
        haveCurrent = false;
    };

    for(int lineNumber = 0; lineNumber < lines.size(); lineNumber++)
    {
        const QString line = cleanLine(lines.at(lineNumber));
        if(line.trimmed().isEmpty())
            continue;
        if(line.contains(QStringLiteral("Start")) && line.contains(QStringLiteral("Src"))
           && line.contains(QStringLiteral("Data")))
        {
            sawHeader = true;
            continue;
        }
        if(line.contains(QStringLiteral("Frame Delay Time")))
        {
            // The delay belongs between the frames; keep it as a note on the
            // next packet instead of inventing a packet for it.
            if(haveCurrent)
                current.annotation += (current.annotation.isEmpty() ? QString() : QStringLiteral("; ")) + line.trimmed();
            continue;
        }
        if(!line.contains(QLatin1Char('|')))
            continue;
        const QStringList columns = line.split(QLatin1Char('|'));
        if(columns.size() < 5)
            continue;
        const QString startText = columns.at(0).trimmed();
        const QString endText = columns.at(1).trimmed();
        const QString srcText = columns.at(2).trimmed();
        const QString dataText = columns.at(3).trimmed();
        const QString crcText = columns.at(4).trimmed();
        const QString annotation = columns.size() > 5 ? columns.mid(5).join(QLatin1Char('|')).trimmed() : QString();

        if(startText.isEmpty() && srcText.isEmpty())
        {
            // Continuation row of the previous frame.
            if(!haveCurrent)
                continue;
            bool parity = false;
            bool shortByte = false;
            current.data += parseData(dataText, &parity, &shortByte);
            current.parityError = current.parityError || parity;
            current.shortBytes = current.shortBytes || shortByte;
            if(!crcText.isEmpty())
                current.crc = parseCrc(crcText);
            if(!annotation.isEmpty())
                current.annotation += (current.annotation.isEmpty() ? QString() : QStringLiteral(" ")) + annotation;
            continue;
        }

        finish();
        current = Packet();
        current.line = lineNumber + 1;
        bool ok = false;
        current.start = startText.toDouble(&ok);
        if(!ok)
            continue;
        current.end = endText.toDouble(&ok) ? endText.toDouble() : current.start;
        current.direction = parseDirection(srcText);
        bool parity = false;
        bool shortByte = false;
        current.data = parseData(dataText, &parity, &shortByte);
        current.parityError = parity;
        current.shortBytes = shortByte;
        current.crc = parseCrc(crcText);
        current.annotation = annotation;
        haveCurrent = true;
    }
    finish();

    if(!sawHeader && result.packets.isEmpty())
    {
        result.error = QStringLiteral("no trace table in the input");
        return result;
    }
    if(result.packets.isEmpty())
    {
        result.error = QStringLiteral("the trace table has no packets");
        return result;
    }
    result.ok = true;
    if(!relativeUs)
        result.warnings << QStringLiteral("Start/End are 13.56 MHz ticks; divide by 13.56 for microseconds");
    for(const Packet& packet : result.packets)
        if(packet.direction == Direction::Unknown)
            result.warnings << QStringLiteral("packet %1 has an unknown direction").arg(packet.index);
    return result;
}

QVector<Packet> filter(const QVector<Packet>& packets, const Filter& filter)
{
    QVector<Packet> result;
    const QString text = filter.text.trimmed().toLower();
    for(const Packet& packet : packets)
    {
        if(filter.hasDirection && packet.direction != filter.direction)
            continue;
        if(filter.hasCrc && packet.crc != filter.crc)
            continue;
        if(filter.hasTime)
        {
            const double from = qMin(filter.from, filter.to);
            const double to = qMax(filter.from, filter.to);
            if(packet.end < from || packet.start > to)
                continue;
        }
        if(!text.isEmpty())
        {
            const QString haystack = (packet.annotation + QLatin1Char(' ') + hexSpaced(packet.data)).toLower();
            if(!haystack.contains(text))
                continue;
        }
        result.append(packet);
    }
    return result;
}

QByteArray toCsv(const QVector<Packet>& packets)
{
    QByteArray output;
    output += "index,start,end,direction,data,crc,parity_error,short_bytes,annotation\n";
    for(const Packet& packet : packets)
    {
        QString annotation = packet.annotation;
        annotation.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        output += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,\"%9\"\n")
                  .arg(packet.index)
                  .arg(QString::number(packet.start, 'f', 2))
                  .arg(QString::number(packet.end, 'f', 2))
                  .arg(directionName(packet.direction))
                  .arg(hexSpaced(packet.data))
                  .arg(crcName(packet.crc))
                  .arg(packet.parityError ? 1 : 0)
                  .arg(packet.shortBytes ? 1 : 0)
                  .arg(annotation)
                  .toUtf8();
    }
    return output;
}

QByteArray toHexText(const QVector<Packet>& packets)
{
    QByteArray output;
    for(const Packet& packet : packets)
    {
        const double seconds = packet.start / 13.56e6;
        output += QStringLiteral("0.%1\n").arg(static_cast<quint64>(seconds * 1e9), 9, 10, QLatin1Char('0')).toUtf8();
        const QByteArray length = QByteArray::number(packet.data.size(), 16).rightJustified(4, '0');
        output += "000000 00 " + QByteArray(packet.direction == Direction::Tag ? "ff" : "fe") + " "
                  + length.left(2) + " " + length.mid(2, 2) + " ";
        for(int i = 0; i < packet.data.size(); i++)
            output += QByteArray::number(static_cast<quint8>(packet.data.at(i)), 16).rightJustified(2, '0')
                      + QByteArray(" ");
        output += "\n";
    }
    return output;
}

QByteArray toText(const QVector<Packet>& packets, bool relativeUs)
{
    QByteArray output;
    output += "      Start |        End | Src | Data                                                                     | CRC | Annotation\n";
    output += "------------+------------+-----+--------------------------------------------------------------------------+-----+--------------------\n";
    for(const Packet& packet : packets)
    {
        const QString data = hexSpaced(packet.data);
        output += (relativeUs
                   ? QStringLiteral(" %1 | %2 | %3 |%4 | %5| %6\n")
                   : QStringLiteral(" %1 | %2 | %3 |%4 | %5| %6\n"))
                  .arg(packet.start, 10, 'f', relativeUs ? 1 : 0)
                  .arg(packet.end, 10, 'f', relativeUs ? 1 : 0)
                  .arg(directionName(packet.direction))
                  .arg(data.leftJustified(72, QLatin1Char(' ')))
                  .arg(crcName(packet.crc).leftJustified(4, QLatin1Char(' ')))
                  .arg(packet.annotation)
                  .toUtf8();
    }
    return output;
}

} // namespace Trace

#ifndef TRACEMODEL_H
#define TRACEMODEL_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/*
 * Parsed `trace list` output.
 *
 * The client prints a fixed-width table (Start | End | Src | Data | CRC |
 * Annotation) and a frame may span several rows.  Only what the client actually
 * printed is stored: a CRC the client did not classify stays Crc::None, and a
 * missing column stays unknown instead of being guessed.
 */
namespace Trace
{

enum class Direction
{
    Unknown,
    Reader,
    Tag,
};

enum class Crc
{
    Unknown, // the column was not present
    None,    // the client printed a blank CRC column
    Ok,
    Fail,
    AOk,
    BOk,
};

QString directionName(Direction direction);
QString crcName(Crc crc);

struct Packet
{
    int index = 0;
    double start = 0.0;
    double end = 0.0;
    Direction direction = Direction::Unknown;
    QByteArray data;
    Crc crc = Crc::Unknown;
    QString annotation;
    bool parityError = false;
    bool shortBytes = false;
    int line = 0; // first source line (1-based)
};

struct Filter
{
    bool hasDirection = false;
    Direction direction = Direction::Unknown;
    bool hasCrc = false;
    Crc crc = Crc::Unknown;
    bool hasTime = false;
    double from = 0.0;
    double to = 0.0;
    QString text; // annotation or hex substring
};

struct ParseResult
{
    bool ok = false;
    QString error;
    QStringList warnings;
    QVector<Packet> packets;
    QString raw;
};

// relativeUs is true when the table was produced with `--us` (fractional
// microseconds); otherwise the Start/End columns are 13.56 MHz ticks.
ParseResult parse(const QString& text, bool relativeUs = false);
QVector<Packet> filter(const QVector<Packet>& packets, const Filter& filter);

// Exports.  CSV is one row per packet; hex is the text2pcap input format.
QByteArray toCsv(const QVector<Packet>& packets);
QByteArray toHexText(const QVector<Packet>& packets);
// A compact human readable list (the same columns as the client).
QByteArray toText(const QVector<Packet>& packets, bool relativeUs = false);

} // namespace Trace

#endif // TRACEMODEL_H

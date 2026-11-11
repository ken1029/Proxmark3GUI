#include "module/demomode.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>

namespace Demo
{

const char* const Provider::kDemoUid = "A1B2C3D4";

namespace
{

QString blockPattern(int block)
{
    QString hex;
    for(int i = 0; i < 16; i++)
        hex += QStringLiteral("%1").arg((block * 16 + i * 7) & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
    return hex;
}

QString spaced(const QString& hex)
{
    QString result;
    for(int i = 0; i + 1 < hex.size(); i += 2)
    {
        if(!result.isEmpty())
            result += QLatin1Char(' ');
        result += hex.mid(i, 2);
    }
    return result;
}

QString keyForSector(int sector)
{
    if(sector <= 8)
        return QStringLiteral("FFFFFFFFFFFF");
    return QStringLiteral("A0A1A2A3A4%1").arg(sector, 2, 16, QLatin1Char('0')).toUpper();
}

} // namespace

Provider& Provider::instance()
{
    static Provider provider;
    return provider;
}

Provider::Provider()
{
    buildCard();
}

void Provider::setActive(bool active)
{
    m_active = active;
    if(active)
        reset();
}

void Provider::reset()
{
    m_written.clear();
    buildCard();
}

void Provider::buildCard()
{
    m_card = Mfc::Document(Mfc::CardType::M1K);
    m_card.uid = QString::fromLatin1(kDemoUid);
    m_card.sourceFile = QStringLiteral("demo");
    m_card.createdAt = QDateTime::currentDateTime();
    for(int sector = 0; sector < m_card.sectorCount(); sector++)
    {
        const QString keyA = keyForSector(sector);
        const QString keyB = keyForSector(sector);
        const bool known = sector <= 8;
        if(known)
        {
            m_card.setKeyVerified(sector, 'A', keyA, Mfc::KeySource::Dictionary, kDemoSession);
            m_card.setKeyVerified(sector, 'B', keyB, Mfc::KeySource::Dictionary, kDemoSession);
        }
        for(int block = m_card.spec().firstBlockOfSector(sector);
                block <= m_card.spec().trailerBlockOfSector(sector); block++)
        {
            if(!known)
                continue;
            QString hex;
            if(block == 0)
                hex = QStringLiteral("A1B2C3D4D40800046263646566676869");
            else if(m_card.spec().isTrailerBlock(block))
                hex = keyA + QStringLiteral("FF078069") + keyB;
            else
                hex = blockPattern(block);
            m_card.setBlock(block, hex, Mfc::BlockState::Read, kDemoSession);
        }
    }

    m_dumpA = m_card;
    m_dumpA.sourceFile = QStringLiteral("demo-dump-A");
    m_dumpB = m_card;
    m_dumpB.sourceFile = QStringLiteral("demo-dump-B");
    // A visible difference in a readable sector and an unread block, so the
    // comparison page shows different and unknown side by side.
    m_dumpB.setBlock(5, QStringLiteral("DEADBEEFCAFEBABE0011223344556677"),
                     Mfc::BlockState::Modified);
    m_dumpB.setBlock(6, QString(), Mfc::BlockState::Unknown);
}

QString Provider::blockLine(int block) const
{
    QString hex = m_written.value(block, m_card.blockHex(block));
    if(hex.isEmpty())
        return QString();
    return QStringLiteral("[+] %1/0x%2 | %3")
           .arg(block, 2, 10, QLatin1Char('0'))
           .arg(QStringLiteral("%1").arg(block, 2, 16, QLatin1Char('0')).toUpper())
           .arg(spaced(hex));
}

QString Provider::rdblResponse(int block, const QString& key, char type) const
{
    Q_UNUSED(type);
    const int sector = m_card.spec().sectorOfBlock(block);
    if(sector < 0 || sector >= m_card.sectorCount())
        return QStringLiteral("[-] block %1 is not part of a MIFARE Classic 1K\n").arg(block);
    if(key.compare(keyForSector(sector), Qt::CaseInsensitive) != 0)
        return QStringLiteral("[-] Can't authenticate block %1 with key %2\n").arg(block).arg(key);
    return blockLine(block) + QStringLiteral("\n");
}

QString Provider::wrblResponse(int block, const QString& key, char type, const QString& data)
{
    Q_UNUSED(type);
    const int sector = m_card.spec().sectorOfBlock(block);
    if(sector < 0 || sector >= m_card.sectorCount())
        return QStringLiteral("[-] block %1 is not part of a MIFARE Classic 1K\n").arg(block);
    if(key.compare(keyForSector(sector), Qt::CaseInsensitive) != 0)
        return QStringLiteral("[-] Can't authenticate block %1 with key %2\n").arg(block).arg(key);
    if(m_card.spec().isTrailerBlock(block))
        return QStringLiteral("[-] demo mode does not write sector trailers\n");
    m_written.insert(block, data.toUpper());
    return QStringLiteral("[+] Write ( ok )\n") + blockLine(block) + QStringLiteral("\n");
}

QString Provider::keyTable() const
{
    QString text;
    text += QStringLiteral("[=] Running dictionary attack against MIFARE Classic 1k\n");
    for(int sector = 0; sector < m_card.sectorCount(); sector++)
    {
        const QString key = keyForSector(sector);
        text += QStringLiteral("[+] %1 | %2 | %3 | 1 | %3 | 1\n")
                .arg(sector, 3, 10, QLatin1Char(' '))
                .arg(m_card.spec().trailerBlockOfSector(sector), 3, 10, QLatin1Char(' '))
                .arg(key);
    }
    text += QStringLiteral("[=] Done!\n");
    return text;
}

bool Provider::handles(const QString& command) const
{
    const QString cmd = command.trimmed().toLower();
    return cmd.startsWith(QStringLiteral("hf 14a info"))
           || cmd.startsWith(QStringLiteral("hf mf info"))
           || cmd.startsWith(QStringLiteral("hf mf rdbl"))
           || cmd.startsWith(QStringLiteral("hf mf wrbl"))
           || cmd.startsWith(QStringLiteral("hf mf chk"))
           || cmd.startsWith(QStringLiteral("hf mfu info"))
           || cmd.startsWith(QStringLiteral("hf mfu dump"))
           || cmd.startsWith(QStringLiteral("hf mfu ndefread"))
           || cmd.startsWith(QStringLiteral("trace list"));
}

QString Provider::respond(const QString& command)
{
    const QString cmd = command.trimmed();
    const QString lower = cmd.toLower();
    if(lower.startsWith(QStringLiteral("hf 14a info")))
        return QStringLiteral(
                   "[=] --- Tag Information --------------------------\n"
                   "[=]       TYPE: MIFARE Classic 1K\n"
                   "[+]       UID: A1 B2 C3 D4\n"
                   "[=]      ATQA: 00 04\n"
                   "[=]       SAK: 08\n"
                   "[=]     ATS: 10 78 80 02 00 90 00\n");
    if(lower.startsWith(QStringLiteral("hf mf info")))
        return QStringLiteral(
                   "[=] --- Tag Information --------------------------\n"
                   "[=]       TYPE: MIFARE Classic 1K\n"
                   "[+]       UID: A1 B2 C3 D4\n"
                   "[=] Prng........ weak\n"
                   "[=] Static nonce....... no\n");
    if(lower.startsWith(QStringLiteral("hf mf rdbl")))
    {
        const QRegularExpression blockRe(QStringLiteral("--blk\\s+(\\d+)"));
        const QRegularExpression keyRe(QStringLiteral("-k\\s+([0-9A-Fa-f]{12})"));
        const QRegularExpression typeRe(QStringLiteral("\\s-([abAB])\\s"));
        const QRegularExpressionMatch blockMatch = blockRe.match(cmd);
        if(!blockMatch.hasMatch())
            return QStringLiteral("[-] no block given\n");
        const QRegularExpressionMatch keyMatch = keyRe.match(cmd);
        const QRegularExpressionMatch typeMatch = typeRe.match(cmd);
        return rdblResponse(blockMatch.captured(1).toInt(),
                            keyMatch.hasMatch() ? keyMatch.captured(1) : QString(),
                            typeMatch.hasMatch() ? typeMatch.captured(1).at(0).toUpper().toLatin1() : 'A');
    }
    if(lower.startsWith(QStringLiteral("hf mf wrbl")))
    {
        const QRegularExpression blockRe(QStringLiteral("--blk\\s+(\\d+)"));
        const QRegularExpression keyRe(QStringLiteral("-k\\s+([0-9A-Fa-f]{12})"));
        const QRegularExpression typeRe(QStringLiteral("\\s-([abAB])\\s"));
        const QRegularExpression dataRe(QStringLiteral("-d\\s+([0-9A-Fa-f]{32})"));
        const QRegularExpressionMatch blockMatch = blockRe.match(cmd);
        const QRegularExpressionMatch dataMatch = dataRe.match(cmd);
        if(!blockMatch.hasMatch() || !dataMatch.hasMatch())
            return QStringLiteral("[-] missing block or data\n");
        const QRegularExpressionMatch keyMatch = keyRe.match(cmd);
        const QRegularExpressionMatch typeMatch = typeRe.match(cmd);
        return wrblResponse(blockMatch.captured(1).toInt(),
                            keyMatch.hasMatch() ? keyMatch.captured(1) : QString(),
                            typeMatch.hasMatch() ? typeMatch.captured(1).at(0).toUpper().toLatin1() : 'A',
                            dataMatch.captured(1));
    }
    if(lower.startsWith(QStringLiteral("hf mf chk")))
        return keyTable();
    if(lower.startsWith(QStringLiteral("hf mfu info")))
        return mfuInfoText();
    if(lower.startsWith(QStringLiteral("hf mfu dump")))
        return mfuDumpText();
    if(lower.startsWith(QStringLiteral("hf mfu ndefread")))
        return mfuNdefText();
    if(lower.startsWith(QStringLiteral("trace list")))
        return traceText();
    return QString();
}

QString Provider::mfuInfoText() const
{
    return QStringLiteral(
               "[=] --- Tag Information --------------------------\n"
               "[=]       TYPE: NTAG 213 144bytes (NT2H1311G0DU)\n"
               "[+]       UID: 04 A2 B3 C4 D5 E6 F0\n"
               "[=]      BCC0: 88 ( ok )\n"
               "[=]      BCC1: F0 ( ok )\n"
               "[=]  Internal: 48 ( default )\n"
               "[=]      Lock: 00 00 - 0000000000000000\n"
               "[=]       OTP: 00 00 00 00 - 00000000000000000000000000000000\n"
               "[=] --- Tag Version\n"
               "[=]       Raw bytes: 00 04 04 02 01 00 0F 03\n"
               "[=]       Vendor ID: 04, NXP Semiconductors Germany\n"
               "[=]    Product type: NTAG\n"
               "[=]            Size: 144 bytes\n");
}

QString Provider::mfuDumpText() const
{
    static const char* const pages[45] =
    {
        "04 A2 B3 7C", "C4 D5 E6 F0 F0 48 00 00", "00 00 00 00", "E1 10 12 00",
        "03 0F D1 01", "0B 55 01 67", "6F 6F 67 6C", "65 2E 63 6F", "6D FE 00 00",
    };
    QString text;
    text += QStringLiteral("block#   | data        |lck| ascii\n");
    text += QStringLiteral("---------+-------------+---+------\n");
    for(int page = 0; page < 45; page++)
    {
        QString hex;
        if(page < 9)
        {
            const QString source = QString::fromLatin1(pages[page]);
            if(page == 0)
                hex = QStringLiteral("04 A2 B3 7C");
            else if(page == 1)
                hex = QStringLiteral("C4 D5 E6 F0");
            else
                hex = source;
        }
        else
            hex = QStringLiteral("00 00 00 00");
        if(page == 2)
            hex = QStringLiteral("F0 48 00 00");
        text += QStringLiteral("%1/0x%2 | %3 | 0 | ...\n")
                .arg(page, 3, 10, QLatin1Char(' '))
                .arg(QStringLiteral("%1").arg(page, 2, 16, QLatin1Char('0')).toUpper())
                .arg(hex);
    }
    return text;
}

QString Provider::mfuNdefText() const
{
    return QStringLiteral(
               "[=] --- NDEF Message\n"
               "[+]   03: NDEF Message TLV - length: 15\n"
               "[+]   D1 01 0B 55 01 67 6F 6F 67 6C 65 2E 63 6F 6D\n"
               "[+]   URI: http://www.google.com\n");
}

QString Provider::traceText() const
{
    return QStringLiteral(
               "      Start |        End | Src | Data                                                                     | CRC | Annotation\n"
               "------------+------------+-----+--------------------------------------------------------------------------+-----+--------------------\n"
               "          0 |         84 | Rdr |26                                                                       |     | \n"
               "        100 |        200 | Tag |04 00                                                                   | ok  | \n"
               "        300 |        400 | Rdr |50 00                                                                   | ok  | \n"
               "        500 |        600 | Tag |00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00                         | !!  |\n"
               "        700 |        800 | Rdr |93 20                                                                   | ok  | \n"
               "        900 |       1000 | Tag |88 04 A2 B3 7C                                                         | ok  | \n");
}

bool Provider::writeDumpFiles(const QString& dir, QString* pathA, QString* pathB, QString* error) const
{
    if(!QDir().mkpath(dir))
    {
        if(error != nullptr)
            *error = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    const QString fileA = QDir(dir).filePath(QStringLiteral("demo-dump-A.json"));
    const QString fileB = QDir(dir).filePath(QStringLiteral("demo-dump-B.json"));
    for(const QPair<QString, Mfc::Document>& item : QVector<QPair<QString, Mfc::Document>>{
            qMakePair(fileA, m_dumpA), qMakePair(fileB, m_dumpB)})
    {
        QSaveFile file(item.first);
        const QByteArray bytes = item.second.toJson();
        if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        {
            if(error != nullptr)
                *error = QStringLiteral("cannot write %1").arg(item.first);
            return false;
        }
    }
    if(pathA != nullptr)
        *pathA = fileA;
    if(pathB != nullptr)
        *pathB = fileB;
    return true;
}

} // namespace Demo

#ifndef MFCDOCUMENT_H
#define MFCDOCUMENT_H

#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVector>

/*
 * MIFARE Classic dump document.
 *
 * The document is deliberately independent of the GUI and of the Proxmark
 * client: the format, comparison and key-state rules are unit tested without a
 * device.  A block keeps 32 characters (two per byte) and may contain '?' for
 * bytes that were never read.  "Unknown" is therefore never confused with a
 * real 0x00, and a binary export can refuse to silently invent data.
 */
namespace Mfc
{

enum class CardType
{
    Mini = 0,
    M1K = 1,
    M2K = 2,
    M4K = 4,
};

enum class BlockState
{
    Unknown,   // never read / no payload
    Read,      // read from a card or a file
    Modified,  // edited in the GUI
    Verified,  // written and read back successfully
    Failed,    // the last read or write failed
};

enum class KeyState
{
    Unknown,   // no value
    Pending,   // a value exists but was not authenticated against this card
    Verified,  // authenticated successfully
    Failed,    // authentication failed
};

enum class KeySource
{
    None,
    Manual,
    Imported,
    Dictionary,
    Recovery,
    Card,
};

enum class DiffState
{
    Same,
    Different,
    Unknown,       // at least one side was not read
    Incomparable,  // the smaller card has no such block
};

QString blockStateName(BlockState state);
QString keyStateName(KeyState state);
QString keySourceName(KeySource source);
QString diffStateName(DiffState state);

struct Spec
{
    CardType type = CardType::M1K;
    int sectorCount = 16;
    int blockCount = 64;

    int blocksInSector(int sector) const;
    int firstBlockOfSector(int sector) const;
    int trailerBlockOfSector(int sector) const;
    int sectorOfBlock(int block) const;
    bool isTrailerBlock(int block) const;
    QString name() const;             // "mini", "1k", "2k", "4k"
    QString displayName() const;      // "MIFARE Classic 1K"
    int byteSize() const { return blockCount * 16; }

    static Spec get(CardType type);
    static bool fromName(const QString& name, CardType* type);
    static bool fromBlockCount(int count, CardType* type);
    static bool fromByteSize(qint64 size, CardType* type);
};

struct Block
{
    QString hex;  // 32 chars, uppercase hex and '?'
    BlockState state = BlockState::Unknown;

    bool complete() const;            // 16 real bytes
    bool known() const { return state != BlockState::Unknown && state != BlockState::Failed && !hex.isEmpty(); }
    QByteArray bytes() const;         // empty when incomplete
    void clear();
};

struct KeyEntry
{
    QString value;                    // 12 uppercase hex, empty when unknown
    KeyState state = KeyState::Unknown;
    KeySource source = KeySource::None;
    QDateTime verifiedAt;

    bool hasValue() const { return value.size() == 12; }
    bool verified() const { return state == KeyState::Verified; }
    void clear();
};

struct IoResult
{
    bool ok = false;
    bool ambiguousLayout = false;
    QString error;
    QString warning;
    CardType detected = CardType::M1K;
};

struct Diff
{
    int block = 0;
    DiffState state = DiffState::Unknown;
    QString a;
    QString b;
    QString note;
};

struct KeyDiff
{
    int sector = 0;
    char type = 'A';
    DiffState state = DiffState::Unknown;
    QString a;
    QString b;
};

struct CompareResult
{
    QVector<Diff> blocks;
    QVector<KeyDiff> keys;
    int same = 0;
    int different = 0;
    int unknown = 0;
    int incomparable = 0;
    bool capacityMismatch = false;
    bool uidMismatch = false;
    QStringList notes;
};

class Document
{
public:
    explicit Document(CardType type = CardType::M1K);

    void setCardType(CardType type);
    void clear();

    CardType cardType() const { return m_spec.type; }
    const Spec& spec() const { return m_spec; }
    QString cardName() const { return m_spec.name(); }
    QString cardDisplayName() const { return m_spec.displayName(); }

    QString uid;                      // optional, uppercase hex
    QString sourceFile;
    QDateTime createdAt;

    // --- blocks ---
    int blockCount() const { return m_spec.blockCount; }
    int sectorCount() const { return m_spec.sectorCount; }
    const Block& block(int index) const;
    bool setBlock(int index, const QString& hex, BlockState state);
    bool setBlockBytes(int index, const QByteArray& data, BlockState state);
    bool setBlockState(int index, BlockState state);
    QString blockHex(int index) const;
    BlockState blockState(int index) const;
    bool blockComplete(int index) const;
    int completeBlockCount() const;
    int unknownBlockCount() const;
    int modifiedBlockCount() const;

    // --- keys ---
    const KeyEntry& key(int sector, char type) const;
    bool setKey(int sector, char type, const QString& value, KeyState state, KeySource source);
    bool setKeyVerified(int sector, char type, const QString& value, KeySource source);
    bool setKeyEntry(int sector, char type, const KeyEntry& entry);
    int verifiedKeyCount() const;
    int pendingKeyCount() const;
    int unknownKeyCount() const;
    int keyCount() const { return m_spec.sectorCount * 2; }

    // --- validation helpers ---
    static bool isKeyText(const QString& key);
    static bool isBlockText(const QString& hex, bool allowUnknown = true);
    static QString normalizeHex(const QString& text);
    static QString normalizeKey(const QString& key);
    static QString maskedKey();       // "????????????"

    // --- file I/O -------------------------------------------------------
    // formatHint: "auto", "bin", "eml", "json".
    // The document is only modified when the whole file is valid.
    static IoResult loadDataFile(const QString& filename, const QString& formatHint, Document* out);
    // format: "bin", "eml", "json".  bin refuses unknown bytes unless
    // allowUnknownBin is true (then zeros are written and a warning returned).
    IoResult saveDataFile(const QString& filename, const QString& format, bool allowUnknownBin = false) const;
    QByteArray dataFileBytes(const QString& format, bool allowUnknownBin = false, IoResult* result = nullptr) const;

    // layoutHint: "auto", "rrg", "legacy", "text", "json".
    // "auto" accepts JSON/text, and reports ambiguousLayout for a binary file
    // because RRG key.bin (A0 A1 ... B0 B1 ...) and the legacy GUI layout
    // (A0 B0 A1 B1 ...) have the same size and cannot be told apart safely.
    static IoResult loadKeyFile(const QString& filename, const QString& layoutHint, Document* out);
    static bool parseKeyText(const QString& text, Document* doc, QString* error, QString* warning);
    IoResult saveKeyFile(const QString& filename, const QString& layout, bool fillUnknownWithFF = true) const;
    QByteArray keyFileBytes(const QString& layout, bool fillUnknownWithFF = true, IoResult* result = nullptr) const;

    // --- comparison / integrity ----------------------------------------
    CompareResult compare(const Document& other) const;
    QByteArray canonicalBytes() const;
    QString sha256() const;
    QByteArray toJson() const;

private:
    Spec m_spec;
    QVector<Block> m_blocks;
    QVector<KeyEntry> m_keyA;
    QVector<KeyEntry> m_keyB;
};

} // namespace Mfc

Q_DECLARE_METATYPE(Mfc::CardType)
Q_DECLARE_METATYPE(Mfc::BlockState)
Q_DECLARE_METATYPE(Mfc::KeyState)
Q_DECLARE_METATYPE(Mfc::DiffState)

#endif // MFCDOCUMENT_H

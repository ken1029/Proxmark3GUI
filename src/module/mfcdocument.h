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
    // RF session (connection + card presence) that produced this payload.
    // 0 means "not read from a card in a known session" (file, manual edit).
    quint64 readSession = 0;

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
    // RF session that authenticated this key.  A key verified in an earlier
    // session stays in the document as history but is not evidence for the
    // card that is in the field now.
    quint64 verifiedSession = 0;

    bool hasValue() const { return value.size() == 12; }
    bool verified() const { return state == KeyState::Verified; }
    bool verifiedIn(quint64 session) const { return verified() && session != 0 && verifiedSession == session; }
    void clear();
};

/*
 * MIFARE Classic sector-trailer access conditions.
 *
 * The bit extraction and the permission table match the emulator model in
 * armsrc/mifaresim.c (IsTrailerAccessAllowed), which is the same table the
 * NXP MF1S50 datasheet defines.  "acl" is the 8 hex characters at bytes 6..9
 * of a trailer block; only bytes 6..8 carry the conditions, byte 9 is GPB.
 */
struct TrailerAcl
{
    // True when bytes 6..8 form a valid complement pair (parity check).
    static bool valid(const QString& acl);
    // -1 when the three condition bytes are not a valid complement pair.
    static int condition(const QString& acl);
    static bool keyBReadable(const QString& acl);            // readable as data with Key A
    static bool keyAWriteAllowed(const QString& acl, char authType);
    static bool keyBWriteAllowed(const QString& acl, char authType);
    static bool aclWriteAllowed(const QString& acl, char authType);
    static bool aclReadable(const QString& acl, char authType);
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
    bool setBlock(int index, const QString& hex, BlockState state, quint64 session = 0);
    bool setBlockBytes(int index, const QByteArray& data, BlockState state, quint64 session = 0);
    bool setBlockState(int index, BlockState state);
    QString blockHex(int index) const;
    BlockState blockState(int index) const;
    bool blockComplete(int index) const;
    // True only when the payload was read from the card in that RF session.
    bool blockReadIn(int index, quint64 session) const;
    int completeBlockCount() const;
    int unknownBlockCount() const;
    int modifiedBlockCount() const;

    // --- keys ---
    const KeyEntry& key(int sector, char type) const;
    bool setKey(int sector, char type, const QString& value, KeyState state, KeySource source);
    bool setKeyVerified(int sector, char type, const QString& value, KeySource source, quint64 session = 0);
    bool setKeyEntry(int sector, char type, const KeyEntry& entry);
    bool keyVerifiedIn(int sector, char type, quint64 session) const;
    int verifiedKeyCount() const;
    int verifiedKeyCountIn(quint64 session) const;
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
    // Hash of canonicalBytes(): the internal document digest, not a file digest.
    QString sha256() const;
    // SHA-256 of the file on disk, for "<file>.sha256" sidecars.
    static QString sha256OfFile(const QString& filename, QString* error = nullptr);
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

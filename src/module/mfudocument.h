#ifndef MFUDOCUMENT_H
#define MFUDOCUMENT_H

#include <QByteArray>
#include <QMap>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

/*
 * MIFARE Ultralight / NTAG read-only document.
 *
 * The client prints a human readable page table (`hf mfu dump`) and the NDEF
 * data is a TLV structure inside the user pages.  Both are parsed here so the
 * page model, the lock bits and the NDEF records can be tested without a
 * device.  Unknown pages stay unknown: a page that was not read is never
 * presented as 00 00 00 00.
 */
namespace Mfu
{

enum class Type
{
    Unknown,
    UL,
    ULC,
    ULEV1_48,
    ULEV1_128,
    ULEV1,
    ULAES,
    NTAG203,
    NTAG210,
    NTAG212,
    NTAG213,
    NTAG215,
    NTAG216,
    MyD,
    Other,
};

enum class PageState
{
    Unknown,
    Read,
    Modified,
    Failed,
};

QString pageStateName(PageState state);
QString typeName(Type type);

struct Spec
{
    Type type = Type::Unknown;
    int pages = 0;       // total addressable pages, 0 when unknown
    int userStart = 4;   // first user page (after the capability container)
    int userEnd = 3;     // last user page, inclusive; < userStart when unknown
    QString name;        // "UL", "NTAG213", ...
    QString displayName; // "NTAG 213 144bytes (NT2H1311G0DU)"

    static Spec get(Type type);
    static Type fromTypeString(const QString& text);
    static bool fromName(const QString& name, Type* type);
    bool known() const { return type != Type::Unknown; }
    int userPages() const { return userEnd >= userStart ? userEnd - userStart + 1 : 0; }
};

struct Page
{
    QString hex;  // 8 hex characters, empty when unknown
    PageState state = PageState::Unknown;
    bool locked = false;
    bool otp = false;     // page 3
    bool config = false;  // dynamic lock / configuration pages

    bool complete() const { return hex.size() == 8; }
    QByteArray bytes() const;
};

struct NdefRecord
{
    QString typeName;   // "U", "T", "android.com:pkg", ...
    QByteArray type;
    QByteArray payload;
    QString text;       // decoded URI/text when the record is understood
    bool decoded = false;
};

struct Ndef
{
    bool present = false;    // a capability container was found
    bool complete = false;   // the whole message was inside the read pages
    QString error;
    QByteArray raw;
    QVector<NdefRecord> records;
};

struct IoResult
{
    bool ok = false;
    QString error;
    QString warning;
};

class Document
{
public:
    explicit Document(Type type = Type::Unknown);

    void setType(Type type);
    Type type() const { return m_spec.type; }
    const Spec& spec() const { return m_spec; }
    QString typeText() const { return m_spec.displayName; }

    QString uid;
    QString productType;  // "Ultralight" / "NTAG" as printed by the client
    QString versionHex;   // GET_VERSION bytes
    QString signature;     // originality signature when printed
    QByteArray staticLock;
    QByteArray dynamicLock;
    QString rawInfo;      // original `hf mfu info` output (ANSI stripped)
    QString rawDump;      // original `hf mfu dump` output (ANSI stripped)
    QString sourceFile;

    int pageCount() const { return m_pages.size(); }
    const Page& page(int index) const;
    bool setPage(int index, const QString& hex, PageState state);
    bool setPageBytes(int index, const QByteArray& data, PageState state);
    QString pageHex(int index) const;
    PageState pageState(int index) const;
    bool pageComplete(int index) const;
    int readPages() const;
    int unknownPages() const;
    int lockedPages() const;
    bool locked(int page) const;

    Ndef parseNdef() const;
    // Raw NDEF message bytes found in the TLV container (empty when absent).
    QByteArray ndefBytes() const;

    // Parsers.  Both accept the client's ANSI coloured output and return an
    // error instead of guessing when the text is not a page table.
    static IoResult parseInfo(const QString& text, Document* out);
    static IoResult parseDump(const QString& text, Document* out, int startPage = -1);
    static IoResult loadDumpFile(const QString& filename, Document* out);
    static IoResult loadInfoFile(const QString& filename, Document* out);

    QByteArray toJson() const;
    static IoResult fromJson(const QByteArray& data, Document* out);
    bool saveJson(const QString& filename, QString* error = nullptr) const;
    static IoResult loadJson(const QString& filename, Document* out);

    static QString normalizedHex(const QString& text);

private:
    void resizePages();
    void applyLockBits();

    Spec m_spec;
    QVector<Page> m_pages;
};

} // namespace Mfu

Q_DECLARE_METATYPE(Mfu::Type)
Q_DECLARE_METATYPE(Mfu::PageState)

#endif // MFUDOCUMENT_H

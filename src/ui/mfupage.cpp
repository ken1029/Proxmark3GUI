#include "ui/mfupage.h"

#include <QAbstractItemView>
#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{

// A card answers in milliseconds; the hard timeout only stops a client that
// never returns to its prompt.
const unsigned long kInfoTimeoutMs = 15000;
const unsigned long kDumpTimeoutMs = 60000;
const unsigned long kNdefTimeoutMs = 30000;

QStringList failurePatterns()
{
    return QStringList() << QStringLiteral("No tag found|Can't select card|"
                                           "command execution time out|not available in this mode");
}

QString spacedHex(const QByteArray& bytes)
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

QString spacedHexString(const QString& hex)
{
    QString result;
    for(int i = 0; i + 1 < hex.size(); i += 2)
    {
        if(i != 0)
            result += QLatin1Char(' ');
        result += hex.mid(i, 2);
    }
    return result;
}

QString asciiText(const QByteArray& bytes)
{
    QString result;
    for(int i = 0; i < bytes.size(); i++)
    {
        const int value = static_cast<quint8>(bytes.at(i));
        result += (value >= 0x20 && value < 0x7F) ? QLatin1Char(static_cast<char>(value))
                                                  : QLatin1Char('.');
    }
    return result;
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// NDEF type bytes are usually "U" or "T"; keep the text only when it is
// printable, otherwise show the bytes.
QString printableType(const Mfu::NdefRecord& record)
{
    if(record.typeName.isEmpty())
        return spacedHex(record.type);
    for(int i = 0; i < record.typeName.size(); i++)
    {
        const ushort value = record.typeName.at(i).unicode();
        if(value < 0x20 || value > 0x7E)
            return spacedHex(record.type);
    }
    return record.typeName;
}

} // namespace

MfuPage::MfuPage(Util* util, QWidget* parent)
    : QWidget(parent), util(util)
{
    setObjectName(QStringLiteral("mfuPage"));
    buildUi();
    refreshAll();
}

void MfuPage::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);

    QGroupBox* readBox = new QGroupBox(tr("Read the card"), this);
    QHBoxLayout* readLayout = new QHBoxLayout(readBox);
    readCardButton = new QPushButton(tr("Read card"), readBox);
    readCardButton->setObjectName(QStringLiteral("mfuReadCardButton"));
    readPagesButton = new QPushButton(tr("Read pages"), readBox);
    readPagesButton->setObjectName(QStringLiteral("mfuReadPagesButton"));
    readNdefButton = new QPushButton(tr("Read NDEF"), readBox);
    readNdefButton->setObjectName(QStringLiteral("mfuReadNdefButton"));
    saveButton = new QPushButton(tr("Save JSON..."), readBox);
    saveButton->setObjectName(QStringLiteral("mfuSaveJsonButton"));
    openButton = new QPushButton(tr("Open JSON..."), readBox);
    openButton->setObjectName(QStringLiteral("mfuOpenJsonButton"));
    readLayout->addWidget(readCardButton);
    readLayout->addWidget(readPagesButton);
    readLayout->addWidget(readNdefButton);
    readLayout->addSpacing(12);
    readLayout->addWidget(saveButton);
    readLayout->addWidget(openButton);
    readLayout->addStretch(1);
    root->addWidget(readBox);

    QGroupBox* summaryBox = new QGroupBox(tr("Card summary"), this);
    QGridLayout* summary = new QGridLayout(summaryBox);
    uidLabel = new QLabel(summaryBox);
    uidLabel->setObjectName(QStringLiteral("mfuUidLabel"));
    uidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    typeLabel = new QLabel(summaryBox);
    typeLabel->setObjectName(QStringLiteral("mfuTypeLabel"));
    typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    productLabel = new QLabel(summaryBox);
    productLabel->setObjectName(QStringLiteral("mfuProductLabel"));
    pagesLabel = new QLabel(summaryBox);
    pagesLabel->setObjectName(QStringLiteral("mfuPagesLabel"));
    lockedLabel = new QLabel(summaryBox);
    lockedLabel->setObjectName(QStringLiteral("mfuLockedLabel"));
    staticLockLabel = new QLabel(summaryBox);
    staticLockLabel->setObjectName(QStringLiteral("mfuStaticLockLabel"));
    staticLockLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dynamicLockLabel = new QLabel(summaryBox);
    dynamicLockLabel->setObjectName(QStringLiteral("mfuDynamicLockLabel"));
    dynamicLockLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ndefLabel = new QLabel(summaryBox);
    ndefLabel->setObjectName(QStringLiteral("mfuNdefLabel"));

    summary->addWidget(new QLabel(tr("UID:"), summaryBox), 0, 0);
    summary->addWidget(uidLabel, 0, 1);
    summary->addWidget(new QLabel(tr("Type:"), summaryBox), 0, 2);
    summary->addWidget(typeLabel, 0, 3);
    summary->addWidget(new QLabel(tr("Product type:"), summaryBox), 1, 0);
    summary->addWidget(productLabel, 1, 1);
    summary->addWidget(new QLabel(tr("Pages read:"), summaryBox), 1, 2);
    summary->addWidget(pagesLabel, 1, 3);
    summary->addWidget(new QLabel(tr("Locked pages:"), summaryBox), 2, 0);
    summary->addWidget(lockedLabel, 2, 1);
    summary->addWidget(new QLabel(tr("NDEF:"), summaryBox), 2, 2);
    summary->addWidget(ndefLabel, 2, 3);
    summary->addWidget(new QLabel(tr("Static lock:"), summaryBox), 3, 0);
    summary->addWidget(staticLockLabel, 3, 1);
    summary->addWidget(new QLabel(tr("Dynamic lock:"), summaryBox), 3, 2);
    summary->addWidget(dynamicLockLabel, 3, 3);
    summary->setColumnStretch(1, 1);
    summary->setColumnStretch(3, 1);
    root->addWidget(summaryBox);

    QGroupBox* pagesBox = new QGroupBox(tr("Pages"), this);
    QVBoxLayout* pagesLayout = new QVBoxLayout(pagesBox);
    pageTable = new QTableWidget(pagesBox);
    pageTable->setObjectName(QStringLiteral("mfuPageTable"));
    pageTable->setColumnCount(6);
    pageTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Page") << tr("Hex") << tr("ASCII")
                                         << tr("Locked") << tr("OTP / config") << tr("State"));
    pageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    pageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    pageTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    pageTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    pageTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    pageTable->verticalHeader()->setVisible(false);
    pageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    pageTable->setAlternatingRowColors(true);
    pageTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pagesLayout->addWidget(pageTable);
    root->addWidget(pagesBox, 3);

    QGroupBox* ndefBox = new QGroupBox(tr("NDEF"), this);
    QVBoxLayout* ndefLayout = new QVBoxLayout(ndefBox);
    ndefStatusLabel = new QLabel(ndefBox);
    ndefStatusLabel->setObjectName(QStringLiteral("mfuNdefStatusLabel"));
    ndefStatusLabel->setWordWrap(true);
    ndefHexEdit = new QLineEdit(ndefBox);
    ndefHexEdit->setObjectName(QStringLiteral("mfuNdefHexEdit"));
    ndefHexEdit->setReadOnly(true);
    ndefHexEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ndefHexEdit->setPlaceholderText(tr("NDEF TLV (hex)"));
    ndefTable = new QTableWidget(ndefBox);
    ndefTable->setObjectName(QStringLiteral("mfuNdefTable"));
    ndefTable->setColumnCount(3);
    ndefTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Type") << tr("Payload (hex)") << tr("Decoded text"));
    ndefTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ndefTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ndefTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    ndefTable->verticalHeader()->setVisible(false);
    ndefTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ndefTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ndefTable->setAlternatingRowColors(true);
    ndefTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ndefLayout->addWidget(ndefStatusLabel);
    ndefLayout->addWidget(ndefHexEdit);
    ndefLayout->addWidget(ndefTable);
    root->addWidget(ndefBox, 2);

    rawToggle = new QToolButton(this);
    rawToggle->setObjectName(QStringLiteral("mfuRawToggle"));
    rawToggle->setText(tr("Raw output"));
    rawToggle->setCheckable(true);
    rawToggle->setChecked(false);
    rawToggle->setArrowType(Qt::RightArrow);
    rawToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rawToggle->setAutoRaise(true);
    root->addWidget(rawToggle);

    rawEdit = new QPlainTextEdit(this);
    rawEdit->setObjectName(QStringLiteral("mfuRawOutput"));
    rawEdit->setReadOnly(true);
    rawEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    rawEdit->setVisible(false);
    root->addWidget(rawEdit, 2);

    statusLabel = new QLabel(tr("Ready"), this);
    statusLabel->setObjectName(QStringLiteral("mfuStatusLabel"));
    statusLabel->setWordWrap(true);
    root->addWidget(statusLabel);

    connect(readCardButton, &QPushButton::clicked, this, &MfuPage::readCard);
    connect(readPagesButton, &QPushButton::clicked, this, &MfuPage::readPages);
    connect(readNdefButton, &QPushButton::clicked, this, &MfuPage::readNdef);
    connect(saveButton, &QPushButton::clicked, this, &MfuPage::saveJson);
    connect(openButton, &QPushButton::clicked, this, &MfuPage::openJson);
    connect(rawToggle, &QToolButton::toggled, this, [this](bool expanded) {
        rawEdit->setVisible(expanded);
        rawToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
}

void MfuPage::setDeviceReady(bool ready)
{
    deviceReady = ready;
    updateButtons();
}

void MfuPage::setCapabilities(const Pm3Capabilities& value)
{
    capabilities = value;
    updateButtons();
}

void MfuPage::updateButtons()
{
    const bool iso14443a = capabilities.has(QStringLiteral("iso14443a"));
    const bool canRead = deviceReady && iso14443a && !busy;

    readCardButton->setEnabled(canRead);
    readPagesButton->setEnabled(canRead);
    readNdefButton->setEnabled(canRead);

    QString reason;
    if(!iso14443a)
        reason = capabilities.reason(QStringLiteral("ISO14443A"));
    else if(!deviceReady)
        reason = tr("Connect the client to read a card");

    readCardButton->setToolTip(reason.isEmpty() ? tr("Run hf mfu info") : reason);
    readPagesButton->setToolTip(reason.isEmpty() ? tr("Run hf mfu dump") : reason);
    readNdefButton->setToolTip(reason.isEmpty() ? tr("Run hf mfu ndefread") : reason);

    saveButton->setEnabled(!busy && hasDocument());
    openButton->setEnabled(!busy);
}

void MfuPage::setBusy(bool value)
{
    busy = value;
    updateButtons();
}

void MfuPage::setStatus(const QString& text)
{
    if(statusLabel != nullptr)
        statusLabel->setText(text);
    emit statusMessage(text);
}

bool MfuPage::hasDocument() const
{
    return !doc.uid.isEmpty() || doc.pageCount() > 0;
}

bool MfuPage::runCommand(const QString& command, unsigned long hardTimeoutMs, QString* output)
{
    if(output != nullptr)
        output->clear();
    if(busy)
        return false;
    if(util == nullptr)
    {
        setStatus(tr("No client connection."));
        return false;
    }

    setBusy(true);
    setStatus(tr("Running %1 ...").arg(command));

    Util::ReturnTrigger trigger(hardTimeoutMs);
    trigger.hardTimeout = hardTimeoutMs;
    // Pipe mode has no trailing prompt; the "rem <token>" reply is the real
    // completion boundary.  A read command never polls stdin, so the marker
    // cannot be consumed as a key press.
    trigger.requireCompletion = true;
    trigger.useCompletionMarker = true;
    trigger.failedOutputs = failurePatterns();

    const Util::ExecResult result = util->execCMDWithResult(command, trigger);
    const QString clean = Util::stripAnsi(result.output);
    if(rawEdit != nullptr)
        rawEdit->setPlainText(clean);

    setBusy(false);

    if(!result.isOk())
    {
        if(rawToggle != nullptr && !clean.isEmpty())
            rawToggle->setChecked(true);
        setStatus(describeResult(result, hardTimeoutMs));
        return false;
    }

    if(output != nullptr)
        *output = clean;
    return true;
}

QString MfuPage::describeResult(const Util::ExecResult& result, unsigned long hardTimeoutMs) const
{
    switch(result.status)
    {
    case Util::EXEC_OK:
        break;
    case Util::EXEC_TIMEOUT:
        return tr("No answer from the client within %1 s.").arg(hardTimeoutMs / 1000);
    case Util::EXEC_FAILED:
    {
        QString matched;
        if(Util::matchAnyPattern(Util::stripAnsi(result.output), failurePatterns(), &matched))
            return tr("The card did not answer: %1").arg(matched.trimmed());
        return tr("The command failed.");
    }
    case Util::EXEC_NOT_RUNNING:
        return tr("The client is not running.");
    case Util::EXEC_ABORTED:
        return tr("The client exited while the command was running.");
    case Util::EXEC_BUSY:
        return tr("The client is busy with another command.");
    case Util::EXEC_CANCELLED:
        return tr("The command was cancelled.");
    }
    return tr("The command failed.");
}

void MfuPage::readCard()
{
    if(busy)
        return;

    QString output;
    if(!runCommand(QStringLiteral("hf mfu info"), kInfoTimeoutMs, &output))
        return;

    Mfu::Document parsed;
    const Mfu::IoResult info = Mfu::Document::parseInfo(output, &parsed);
    if(!info.ok)
    {
        setStatus(tr("Could not read the card: %1").arg(info.error));
        return;
    }

    if(doc.type() == parsed.type() && !doc.uid.isEmpty() && doc.uid == parsed.uid
       && doc.pageCount() > 0)
    {
        // The same tag: keep the pages that were already read and only refresh
        // the identity and the lock bytes.
        doc.productType = parsed.productType.isEmpty() ? doc.productType : parsed.productType;
        if(!parsed.versionHex.isEmpty())
            doc.versionHex = parsed.versionHex;
        if(!parsed.signature.isEmpty())
            doc.signature = parsed.signature;
        if(!parsed.staticLock.isEmpty())
            doc.staticLock = parsed.staticLock;
        if(!parsed.dynamicLock.isEmpty())
            doc.dynamicLock = parsed.dynamicLock;
        doc.rawInfo = parsed.rawInfo;
    }
    else
    {
        doc = parsed;
    }

    refreshAll();

    if(doc.type() == Mfu::Type::Unknown)
        setStatus(tr("The card answered, but the tag type is not recognised; the page model is empty."));
    else
        setStatus(tr("Read %1, UID %2.").arg(doc.typeText(), spacedHexString(doc.uid)));
}

void MfuPage::readPages()
{
    if(busy)
        return;

    if(doc.type() == Mfu::Type::Unknown)
    {
        // The page count must come from the tag, never from a guess: identify
        // the tag first and keep the page model empty when it stays unknown.
        QString infoText;
        if(!runCommand(QStringLiteral("hf mfu info"), kInfoTimeoutMs, &infoText))
            return;

        Mfu::Document identified;
        const Mfu::IoResult info = Mfu::Document::parseInfo(infoText, &identified);
        if(!info.ok)
        {
            setStatus(tr("Could not identify the card: %1").arg(info.error));
            return;
        }
        doc = identified;
        refreshAll();
        if(doc.type() == Mfu::Type::Unknown)
        {
            setStatus(tr("The tag type is not recognised; the page model stays empty."));
            return;
        }
    }

    QString dumpText;
    if(!runCommand(QStringLiteral("hf mfu dump"), kDumpTimeoutMs, &dumpText))
        return;

    const QString productType = doc.productType;
    const QString sourceFile = doc.sourceFile;
    const QByteArray staticLock = doc.staticLock;
    const QByteArray dynamicLock = doc.dynamicLock;
    Mfu::Document parsed(doc.type());
    parsed.uid = doc.uid;
    parsed.rawInfo = doc.rawInfo;
    parsed.versionHex = doc.versionHex;
    parsed.signature = doc.signature;

    const Mfu::IoResult dump = Mfu::Document::parseDump(dumpText, &parsed);
    if(!dump.ok)
    {
        setStatus(tr("Could not read the pages: %1").arg(dump.error));
        return;
    }
    // parseDump keeps the identity it was given; the product type and the
    // source file are not part of the dump text, and a lock byte that was
    // known from `hf mfu info` stays known when the dump did not print it.
    parsed.productType = productType;
    parsed.sourceFile = sourceFile;
    if(parsed.staticLock.isEmpty())
        parsed.staticLock = staticLock;
    if(parsed.dynamicLock.isEmpty())
        parsed.dynamicLock = dynamicLock;
    doc = parsed;

    refreshAll();
    setStatus(tr("Read %1 of %2 pages, %3 locked.")
              .arg(doc.readPages()).arg(doc.pageCount()).arg(doc.lockedPages()));
}

void MfuPage::readNdef()
{
    if(busy)
        return;

    if(!runCommand(QStringLiteral("hf mfu ndefread"), kNdefTimeoutMs, nullptr))
        return;

    refreshNdef();

    if(doc.pageCount() == 0 || !doc.pageComplete(3))
    {
        setStatus(tr("NDEF read; read the pages to decode the message."));
        return;
    }

    const Mfu::Ndef ndef = doc.parseNdef();
    if(!ndef.present)
        setStatus(tr("No NDEF message: %1").arg(ndef.error.isEmpty() ? tr("unknown") : ndef.error));
    else
        setStatus(tr("NDEF read: %1 record(s).").arg(ndef.records.size()));
}

void MfuPage::saveJson()
{
    if(!hasDocument())
    {
        setStatus(tr("Nothing to save: read or open a document first."));
        return;
    }

    const QString name = doc.uid.isEmpty() ? QStringLiteral("mfu.json")
                                           : QStringLiteral("mfu-%1.json").arg(doc.uid);
    QString dir = QDir::homePath();
    if(!doc.sourceFile.isEmpty())
        dir = QFileInfo(doc.sourceFile).absolutePath();

    const QString filename = QFileDialog::getSaveFileName(this, tr("Save MFU document"),
                                                         QDir(dir).filePath(name),
                                                         tr("MFU document (*.json);;All files (*)"));
    if(filename.isEmpty())
        return;

    QString error;
    if(!doc.saveJson(filename, &error))
    {
        setStatus(tr("Could not save the document: %1").arg(error));
        return;
    }
    doc.sourceFile = filename;
    setStatus(tr("Saved %1.").arg(QDir::toNativeSeparators(filename)));
}

void MfuPage::openJson()
{
    const QString filename = QFileDialog::getOpenFileName(this, tr("Open MFU document"),
                                                         QDir::homePath(),
                                                         tr("MFU document (*.json);;All files (*)"));
    if(filename.isEmpty())
        return;

    Mfu::Document parsed;
    const Mfu::IoResult loaded = Mfu::Document::loadJson(filename, &parsed);
    if(!loaded.ok)
    {
        setStatus(tr("Could not open the document: %1").arg(loaded.error));
        return;
    }

    doc = parsed;
    doc.sourceFile = filename;
    const QString raw = doc.rawDump.isEmpty() ? doc.rawInfo : doc.rawDump;
    if(rawEdit != nullptr)
        rawEdit->setPlainText(raw);

    refreshAll();

    const QString type = doc.type() == Mfu::Type::Unknown ? tr("type not recognised") : doc.typeText();
    setStatus(tr("Opened %1: %2.").arg(QFileInfo(filename).fileName(), type));
}

void MfuPage::refreshAll()
{
    refreshSummary();
    refreshPages();
    refreshNdef();
    updateButtons();
}

void MfuPage::refreshSummary()
{
    uidLabel->setText(doc.uid.isEmpty() ? tr("unknown") : spacedHexString(doc.uid));

    if(doc.type() == Mfu::Type::Unknown)
        typeLabel->setText(doc.uid.isEmpty() ? tr("not read") : tr("type not recognised"));
    else
        typeLabel->setText(doc.typeText());

    productLabel->setText(doc.productType.isEmpty() ? tr("unknown") : doc.productType);

    if(doc.pageCount() == 0)
        pagesLabel->setText(doc.uid.isEmpty() ? tr("not read") : tr("type not recognised"));
    else
        pagesLabel->setText(tr("%1 / %2").arg(doc.readPages()).arg(doc.pageCount()));

    lockedLabel->setText(tr("%1").arg(doc.lockedPages()));
    staticLockLabel->setText(doc.staticLock.isEmpty() ? tr("unknown") : spacedHex(doc.staticLock));
    dynamicLockLabel->setText(doc.dynamicLock.isEmpty() ? tr("unknown") : spacedHex(doc.dynamicLock));

    QString ndefSummary;
    if(doc.pageCount() == 0)
    {
        ndefSummary = tr("not read");
    }
    else if(!doc.pageComplete(3))
    {
        ndefSummary = tr("unknown");
    }
    else
    {
        const Mfu::Ndef ndef = doc.parseNdef();
        if(!ndef.present)
            ndefSummary = tr("not present");
        else if(ndef.raw.isEmpty())
            ndefSummary = tr("no message");
        else if(!ndef.complete)
            ndefSummary = tr("incomplete");
        else
            ndefSummary = tr("present, complete");
        ndefLabel->setToolTip(ndef.error);
    }
    if(doc.pageCount() == 0 || !doc.pageComplete(3))
        ndefLabel->setToolTip(QString());
    ndefLabel->setText(ndefSummary);
}

void MfuPage::refreshPages()
{
    QSignalBlocker blocker(pageTable);
    pageTable->setRowCount(0);
    pageTable->setRowCount(doc.pageCount());

    for(int page = 0; page < doc.pageCount(); page++)
    {
        const Mfu::Page& data = doc.page(page);
        const bool complete = data.complete();

        QTableWidgetItem* indexItem = readOnlyItem(QString::number(page));
        indexItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QTableWidgetItem* hexItem = readOnlyItem(complete ? spacedHex(data.bytes())
                                                          : QStringLiteral("????????"));
        QTableWidgetItem* asciiItem = readOnlyItem(complete ? asciiText(data.bytes())
                                                            : QStringLiteral("????"));
        QTableWidgetItem* lockedItem = readOnlyItem(lockText(page));

        QString kind;
        if(data.otp)
            kind = tr("OTP");
        else if(data.config)
            kind = tr("config");
        QTableWidgetItem* kindItem = readOnlyItem(kind);

        QTableWidgetItem* stateItem = readOnlyItem(stateText(data.state));

        QColor color;
        bool colored = true;
        switch(data.state)
        {
        case Mfu::PageState::Unknown:
            color = QColor(120, 120, 120);
            break;
        case Mfu::PageState::Failed:
            color = QColor(190, 40, 40);
            break;
        case Mfu::PageState::Modified:
            color = QColor(190, 130, 0);
            break;
        case Mfu::PageState::Read:
            colored = false;
            break;
        }
        if(colored)
        {
            indexItem->setForeground(color);
            hexItem->setForeground(color);
            asciiItem->setForeground(color);
            lockedItem->setForeground(color);
            kindItem->setForeground(color);
            stateItem->setForeground(color);
        }

        pageTable->setItem(page, 0, indexItem);
        pageTable->setItem(page, 1, hexItem);
        pageTable->setItem(page, 2, asciiItem);
        pageTable->setItem(page, 3, lockedItem);
        pageTable->setItem(page, 4, kindItem);
        pageTable->setItem(page, 5, stateItem);
    }
}

void MfuPage::refreshNdef()
{
    QSignalBlocker blocker(ndefTable);
    ndefTable->setRowCount(0);
    ndefHexEdit->clear();

    if(doc.pageCount() == 0)
    {
        ndefStatusLabel->setText(tr("no page model"));
        return;
    }
    if(!doc.pageComplete(3))
    {
        ndefStatusLabel->setText(tr("unknown: the capability container (page 3) was not read"));
        return;
    }

    const Mfu::Ndef ndef = doc.parseNdef();
    if(!ndef.present)
    {
        ndefStatusLabel->setText(tr("not present: %1")
                                 .arg(ndef.error.isEmpty() ? tr("unknown") : ndef.error));
        return;
    }
    if(ndef.raw.isEmpty())
        ndefStatusLabel->setText(tr("capability container present, no NDEF message: %1")
                                 .arg(ndef.error.isEmpty() ? tr("unknown") : ndef.error));
    else if(!ndef.complete)
        ndefStatusLabel->setText(tr("incomplete: %1")
                                 .arg(ndef.error.isEmpty() ? tr("unknown") : ndef.error));
    else
        ndefStatusLabel->setText(tr("present, complete: %1 record(s)").arg(ndef.records.size()));

    ndefHexEdit->setText(spacedHex(ndef.raw));
    ndefHexEdit->setCursorPosition(0);

    ndefTable->setRowCount(ndef.records.size());
    for(int i = 0; i < ndef.records.size(); i++)
    {
        const Mfu::NdefRecord& record = ndef.records.at(i);
        const QString payload = record.payload.isEmpty() ? QString() : spacedHex(record.payload);
        const QString text = record.decoded ? record.text : tr("(binary)");
        ndefTable->setItem(i, 0, readOnlyItem(printableType(record)));
        ndefTable->setItem(i, 1, readOnlyItem(payload));
        ndefTable->setItem(i, 2, readOnlyItem(text));
    }
}

QString MfuPage::stateText(Mfu::PageState state) const
{
    switch(state)
    {
    case Mfu::PageState::Read:
        return tr("read");
    case Mfu::PageState::Modified:
        return tr("modified");
    case Mfu::PageState::Failed:
        return tr("failed");
    case Mfu::PageState::Unknown:
        break;
    }
    return tr("unknown");
}

QString MfuPage::lockText(int page) const
{
    if(page <= 2)
        return tr("Yes"); // the UID pages are always read-only
    if(page <= 15)
    {
        if(doc.staticLock.size() < 2)
            return QStringLiteral("?");
    }
    else if(page <= 43)
    {
        if(doc.dynamicLock.size() < 2)
            return QStringLiteral("?");
    }
    else
    {
        return tr("n/a");
    }
    return doc.locked(page) ? tr("Yes") : tr("No");
}

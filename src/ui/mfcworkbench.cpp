#include "ui/mfcworkbench.h"

#include <QAction>
#include <algorithm>
#include <functional>

#include <QApplication>
#include <QAbstractItemView>
#include <QClipboard>
#include <QFileInfo>
#include <QMenu>
#include <QColor>
#include <QHBoxLayout>
#include <QPair>
#include <QTextCursor>
#include <QCheckBox>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>

#include "common/pm3cmd.h"
#include "module/demomode.h"
#include "module/mfclibrary.h"
#include "module/mifare.h"

namespace
{

int typeToInt(Mfc::CardType type)
{
    switch(type)
    {
    case Mfc::CardType::Mini:
        return 0;
    case Mfc::CardType::M1K:
        return 1;
    case Mfc::CardType::M2K:
        return 2;
    case Mfc::CardType::M4K:
        return 4;
    }
    return 1;
}

Mfc::CardType intToType(int type)
{
    switch(type)
    {
    case 0:
        return Mfc::CardType::Mini;
    case 2:
        return Mfc::CardType::M2K;
    case 4:
        return Mfc::CardType::M4K;
    default:
        return Mfc::CardType::M1K;
    }
}

QString spacedHex(const QString& hex)
{
    if(hex.isEmpty())
        return QString();
    QString result;
    for(int i = 0; i < hex.size(); i += 2)
    {
        if(!result.isEmpty())
            result += QLatin1Char(' ');
        result += hex.mid(i, 2);
    }
    return result;
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QColor stateColor(Mfc::KeyState state)
{
    switch(state)
    {
    case Mfc::KeyState::Verified:
        return QColor(0, 150, 90);
    case Mfc::KeyState::Pending:
        return QColor(190, 130, 0);
    case Mfc::KeyState::Failed:
        return QColor(190, 40, 40);
    case Mfc::KeyState::Unknown:
        break;
    }
    return QColor(120, 120, 120);
}

QString asciiOf(const QString& hex)
{
    QString result;
    for(int i = 0; i + 1 < hex.size(); i += 2)
    {
        const QChar hi = hex.at(i);
        const QChar lo = hex.at(i + 1);
        if(hi == QLatin1Char('?') || lo == QLatin1Char('?'))
        {
            result += QLatin1Char('.');
            continue;
        }
        bool ok = false;
        const int value = hex.mid(i, 2).toInt(&ok, 16);
        result += (ok && value >= 0x20 && value < 0x7f) ? QChar(value) : QLatin1Char('.');
    }
    return result;
}

QColor diffColor(Mfc::DiffState state)
{
    switch(state)
    {
    case Mfc::DiffState::Same:
        return QColor(0, 150, 90);
    case Mfc::DiffState::Different:
        return QColor(190, 40, 40);
    case Mfc::DiffState::Unknown:
        return QColor(190, 130, 0);
    case Mfc::DiffState::Incomparable:
        return QColor(120, 120, 120);
    }
    return QColor(120, 120, 120);
}

} // namespace

MfcWorkbench::MfcWorkbench(Util* util, Mifare* mifare, QWidget* parent)
    : QWidget(parent), util(util), mifare(mifare)
{
    setObjectName("mfcWorkbench");
    buildUi();
    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(500);
    connect(refreshTimer, &QTimer::timeout, this, &MfcWorkbench::refresh);
    refreshTimer->start();

    connect(util, &Util::commandOutput, this, &MfcWorkbench::onCommandOutput);
    connect(util, &Util::commandFinished, this, &MfcWorkbench::onCommandFinished);
    connect(util, &Util::abortClientRequested, this, &MfcWorkbench::abortClientRequested);

    draftTimer = new QTimer(this);
    draftTimer->setInterval(15000);
    connect(draftTimer, &QTimer::timeout, this, &MfcWorkbench::flushDrafts);
    draftTimer->start();

    scanTimer = new QTimer(this);
    scanTimer->setInterval(1500);
    connect(scanTimer, &QTimer::timeout, this, &MfcWorkbench::scanTick);

    drafts.setDir(QDir::current().filePath(QStringLiteral("drafts")));
    drafts.load();
    importFromMifare();
    addDocument(doc, tr("Document 1"), QStringLiteral("new"));
    refreshDocumentTabs();
}

void MfcWorkbench::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);

    QHBoxLayout* documentBar = new QHBoxLayout();
    docTabBar = new QTabBar(this);
    docTabBar->setObjectName("mfcDocumentTabs");
    docTabBar->setExpanding(false);
    docTabBar->setTabsClosable(true);
    docTabBar->setMovable(false);
    docTabBar->setUsesScrollButtons(true);
    docTabBar->setElideMode(Qt::ElideRight);
    docMenuButton = new QToolButton(this);
    docMenuButton->setObjectName("mfcDocumentMenu");
    docMenuButton->setText(tr("Document"));
    docMenuButton->setPopupMode(QToolButton::InstantPopup);
    QMenu* documentMenu = new QMenu(docMenuButton);
    documentMenu->addAction(tr("New document"), this, &MfcWorkbench::newDocumentTab);
    documentMenu->addAction(tr("Open dump..."), this, &MfcWorkbench::openDocumentTab);
    documentMenu->addAction(tr("Save document..."), this, &MfcWorkbench::saveCurrentDocument);
    documentMenu->addSeparator();
    documentMenu->addAction(tr("Rename tab..."), this, &MfcWorkbench::renameCurrentDocument);
    documentMenu->addAction(tr("Close tab"), this, &MfcWorkbench::closeCurrentDocument);
    documentMenu->addSeparator();
    documentMenu->addAction(tr("Restore draft..."), this, &MfcWorkbench::restoreDraftDialog);
    documentMenu->addAction(tr("Save drafts now"), this, &MfcWorkbench::flushDrafts);
    docMenuButton->setMenu(documentMenu);
    documentBar->addWidget(docTabBar, 1);
    documentBar->addWidget(docMenuButton);
    root->addLayout(documentBar);

    QGroupBox* summaryBox = new QGroupBox(tr("Card / document"), this);
    QGridLayout* summary = new QGridLayout(summaryBox);
    cardLabel = new QLabel(tr("No document"), summaryBox);
    QFont bold = cardLabel->font();
    bold.setBold(true);
    cardLabel->setFont(bold);
    uidLabel = new QLabel(QString(), summaryBox);
    nonceLabel = new QLabel(QString(), summaryBox);
    cardTypeBox = new QComboBox(summaryBox);
    cardTypeBox->addItem(tr("Mini"), 0);
    cardTypeBox->addItem(tr("1K"), 1);
    cardTypeBox->addItem(tr("2K"), 2);
    cardTypeBox->addItem(tr("4K"), 4);
    QPushButton* infoButton = new QPushButton(tr("Identify card"), summaryBox);
    QPushButton* nonceButton = new QPushButton(tr("Nonce diagnostics"), summaryBox);
    QPushButton* importButton = new QPushButton(tr("Import from data page"), summaryBox);
    QPushButton* applyButton = new QPushButton(tr("Apply to data page"), summaryBox);

    summary->addWidget(cardLabel, 0, 0, 1, 3);
    summary->addWidget(new QLabel(tr("Card type:"), summaryBox), 0, 3);
    summary->addWidget(cardTypeBox, 0, 4);
    summary->addWidget(infoButton, 0, 5);
    summary->addWidget(nonceButton, 0, 6);
    summary->addWidget(new QLabel(tr("UID:"), summaryBox), 1, 0);
    summary->addWidget(uidLabel, 1, 1, 1, 2);
    summary->addWidget(new QLabel(tr("Nonce:"), summaryBox), 1, 3);
    summary->addWidget(nonceLabel, 1, 4);
    summary->addWidget(importButton, 1, 5);
    summary->addWidget(applyButton, 1, 6);

    keySummary = new QLabel(summaryBox);
    blockSummary = new QLabel(summaryBox);
    stageLabel = new QLabel(summaryBox);
    statusLabel = new QLabel(summaryBox);
    batchLabel = new QLabel(summaryBox);
    batchLabel->setObjectName("mfcBatchLabel");
    batchLabel->setWordWrap(true);
    summary->addWidget(batchLabel, 5, 0, 1, 7);
    progress = new QProgressBar(summaryBox);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(true);
    keySummary->setTextFormat(Qt::RichText);
    blockSummary->setTextFormat(Qt::RichText);
    keySummary->setTextInteractionFlags(Qt::TextBrowserInteraction);
    blockSummary->setTextInteractionFlags(Qt::TextBrowserInteraction);
    summary->addWidget(keySummary, 2, 0, 1, 4);
    summary->addWidget(blockSummary, 2, 4, 1, 3);
    summary->addWidget(stageLabel, 3, 0, 1, 4);
    summary->addWidget(statusLabel, 3, 4, 1, 3);
    summary->addWidget(progress, 4, 0, 1, 7);
    root->addWidget(summaryBox);

    pageTabs = new QTabWidget(this);

    // --- keys ---
    QWidget* keyPage = new QWidget(pageTabs);
    QVBoxLayout* keyLayout = new QVBoxLayout(keyPage);
    keyTable = new QTableWidget(keyPage);
    keyTable->setColumnCount(8);
    keyTable->setHorizontalHeaderLabels(QStringList()
                                        << tr("Sector") << tr("Key A") << tr("A state") << tr("A source")
                                        << tr("Key B") << tr("B state") << tr("B source") << tr("Note"));
    keyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    keyTable->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    keyTable->setAlternatingRowColors(true);
    keyLayout->addWidget(keyTable);
    QHBoxLayout* keyButtons = new QHBoxLayout();
    QPushButton* loadKeyButton = new QPushButton(tr("Load keys..."), keyPage);
    QPushButton* saveKeyButton = new QPushButton(tr("Save keys..."), keyPage);
    QPushButton* verifyButton = new QPushButton(tr("Verify known keys"), keyPage);
    batchButton = new QPushButton(tr("Fill missing keys..."), keyPage);
    batchButton->setObjectName("mfcBatchButton");
    keyButtons->addWidget(loadKeyButton);
    keyButtons->addWidget(saveKeyButton);
    keyButtons->addWidget(verifyButton);
    keyButtons->addWidget(batchButton);
    keyButtons->addStretch(1);
    keyLayout->addLayout(keyButtons);
    pageTabs->addTab(keyPage, tr("Keys && access"));

    // --- data ---
    QWidget* dataPage = new QWidget(pageTabs);
    QVBoxLayout* dataLayout = new QVBoxLayout(dataPage);
    QHBoxLayout* dataFilter = new QHBoxLayout();
    dataFilter->addWidget(new QLabel(tr("Show:"), dataPage));
    filterBox = new QComboBox(dataPage);
    filterBox->addItem(tr("All blocks"), "all");
    filterBox->addItem(tr("Unknown / failed"), "unknown");
    filterBox->addItem(tr("Modified"), "modified");
    filterBox->addItem(tr("Readable"), "read");
    dataFilter->addWidget(filterBox);
    dataFilter->addWidget(new QLabel(tr("Sector:"), dataPage));
    sectorFilter = new QComboBox(dataPage);
    sectorFilter->setObjectName("mfcSectorFilter");
    sectorFilter->addItem(tr("All"), -1);
    dataFilter->addWidget(sectorFilter);
    searchEdit = new QLineEdit(dataPage);
    searchEdit->setObjectName("mfcSearchEdit");
    searchEdit->setPlaceholderText(tr("Search hex or ASCII"));
    searchEdit->setClearButtonEnabled(true);
    dataFilter->addWidget(searchEdit, 1);
    QPushButton* readButton = new QPushButton(tr("Read missing blocks"), dataPage);
    QPushButton* readUnknownButton = new QPushButton(tr("Retry unknown blocks"), dataPage);
    QPushButton* backupButton = new QPushButton(tr("Save backup..."), dataPage);
    QPushButton* writeButton = new QPushButton(tr("Write plan..."), dataPage);
    dataFilter->addWidget(readButton);
    dataFilter->addWidget(readUnknownButton);
    dataFilter->addWidget(backupButton);
    dataFilter->addWidget(writeButton);
    dataLayout->addLayout(dataFilter);
    dataTable = new QTableWidget(dataPage);
    dataTable->setObjectName("mfcDataTable");
    dataTable->setColumnCount(6);
    dataTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Block") << tr("Sector") << tr("Type")
                                         << tr("Hex") << tr("ASCII") << tr("State"));
    dataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    dataTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    dataTable->setAlternatingRowColors(true);
    dataTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    dataTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dataTable->setContextMenuPolicy(Qt::CustomContextMenu);
    detailView = new QPlainTextEdit(dataPage);
    detailView->setObjectName("mfcBlockDetails");
    detailView->setReadOnly(true);
    detailView->setMinimumWidth(260);
    detailView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dataSplitter = new QSplitter(Qt::Horizontal, dataPage);
    dataSplitter->addWidget(dataTable);
    dataSplitter->addWidget(detailView);
    dataSplitter->setStretchFactor(0, 3);
    dataSplitter->setStretchFactor(1, 1);
    dataLayout->addWidget(dataSplitter, 1);
    pageTabs->addTab(dataPage, tr("Data / backup"));

    // --- compare ---
    QWidget* comparePage = new QWidget(pageTabs);
    QVBoxLayout* compareLayout = new QVBoxLayout(comparePage);
    QGridLayout* paths = new QGridLayout();
    compareAEdit = new QLineEdit(comparePage);
    compareBEdit = new QLineEdit(comparePage);
    QPushButton* loadAButton = new QPushButton(tr("Load A..."), comparePage);
    QPushButton* loadBButton = new QPushButton(tr("Load B..."), comparePage);
    QPushButton* documentAButton = new QPushButton(tr("Use document"), comparePage);
    QPushButton* documentBButton = new QPushButton(tr("Use document"), comparePage);
    QPushButton* snapshotAButton = new QPushButton(tr("Card snapshot"), comparePage);
    QPushButton* snapshotBButton = new QPushButton(tr("Card snapshot"), comparePage);
    QPushButton* libraryAButton = new QPushButton(tr("Library A..."), comparePage);
    QPushButton* libraryBButton = new QPushButton(tr("Library B..."), comparePage);
    documentAButton->setObjectName("mfcCompareDocumentA");
    documentBButton->setObjectName("mfcCompareDocumentB");
    snapshotAButton->setObjectName("mfcCompareSnapshotA");
    snapshotBButton->setObjectName("mfcCompareSnapshotB");
    libraryAButton->setObjectName("mfcCompareLibraryA");
    libraryBButton->setObjectName("mfcCompareLibraryB");
    QPushButton* compareButton = new QPushButton(tr("Compare A / B"), comparePage);
    QPushButton* exportButton = new QPushButton(tr("Export report..."), comparePage);
    showDiffOnlyBox = new QCheckBox(tr("Only show differences"), comparePage);
    paths->addWidget(new QLabel(tr("A:"), comparePage), 0, 0);
    paths->addWidget(compareAEdit, 0, 1);
    paths->addWidget(loadAButton, 0, 2);
    paths->addWidget(documentAButton, 0, 3);
    paths->addWidget(snapshotAButton, 0, 4);
    paths->addWidget(libraryAButton, 0, 5);
    paths->addWidget(new QLabel(tr("B:"), comparePage), 1, 0);
    paths->addWidget(compareBEdit, 1, 1);
    paths->addWidget(loadBButton, 1, 2);
    paths->addWidget(documentBButton, 1, 3);
    paths->addWidget(snapshotBButton, 1, 4);
    paths->addWidget(libraryBButton, 1, 5);
    paths->addWidget(showDiffOnlyBox, 2, 1);
    paths->addWidget(compareButton, 2, 2);
    paths->addWidget(exportButton, 2, 3);
    compareLayout->addLayout(paths);
    diffTable = new QTableWidget(comparePage);
    diffTable->setColumnCount(5);
    diffTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Block") << tr("State") << tr("A") << tr("B") << tr("Note"));
    diffTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    diffTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    diffTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    diffTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    diffTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    diffTable->setAlternatingRowColors(true);
    compareLayout->addWidget(diffTable);
    pageTabs->addTab(comparePage, tr("Compare"));

    // --- continuous scan ---
    QWidget* scanPage = new QWidget(pageTabs);
    QVBoxLayout* scanLayout = new QVBoxLayout(scanPage);
    QHBoxLayout* scanControls = new QHBoxLayout();
    scanButton = new QPushButton(tr("Start continuous scan"), scanPage);
    scanButton->setObjectName("mfcScanButton");
    scanButton->setCheckable(true);
    QCheckBox* autoReadBox = new QCheckBox(tr("Automatically read readable blocks"), scanPage);
    autoReadBox->setObjectName("mfcScanAutoRead");
    scanControls->addWidget(scanButton);
    scanControls->addWidget(autoReadBox);
    scanControls->addStretch(1);
    QPushButton* scanNameButton = new QPushButton(tr("Name..."), scanPage);
    QPushButton* scanCompareButton = new QPushButton(tr("Compare selected two"), scanPage);
    QPushButton* scanExportButton = new QPushButton(tr("Export selected..."), scanPage);
    QPushButton* scanRemoveButton = new QPushButton(tr("Remove result"), scanPage);
    scanControls->addWidget(scanNameButton);
    scanControls->addWidget(scanCompareButton);
    scanControls->addWidget(scanExportButton);
    scanControls->addWidget(scanRemoveButton);
    scanLayout->addLayout(scanControls);
    QLabel* scanHint = new QLabel(tr("Hold a card on the antenna to identify it. A card that stays in "
                                     "place stays in the same round; removing it and presenting it "
                                     "again starts a new round, even for the same UID."), scanPage);
    scanHint->setWordWrap(true);
    scanLayout->addWidget(scanHint);
    scanTable = new QTableWidget(scanPage);
    scanTable->setObjectName("mfcScanTable");
    scanTable->setColumnCount(6);
    scanTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Round") << tr("UID") << tr("Type")
                                         << tr("Blocks") << tr("Keys") << tr("Name"));
    scanTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    scanTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    scanTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    scanTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    scanTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    scanTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    scanTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    scanTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    scanLayout->addWidget(scanTable, 1);
    pageTabs->addTab(scanPage, tr("Continuous scan"));

    // --- methods ---
    QWidget* methodPage = new QWidget(pageTabs);
    QVBoxLayout* methodLayout = new QVBoxLayout(methodPage);
    adviceTable = new QTableWidget(methodPage);
    adviceTable->setColumnCount(4);
    adviceTable->setHorizontalHeaderLabels(QStringList()
                                           << tr("Method") << tr("Applicable") << tr("Why") << tr("Requires"));
    adviceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    adviceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    adviceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    adviceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    adviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    methodLayout->addWidget(adviceTable);

    QGridLayout* controls = new QGridLayout();
    methodBox = new QComboBox(methodPage);
    methodBox->setObjectName("mfcMethodBox");
    methodBox->addItem(tr("Known keys"), int(MfcRecovery::Method::KnownKeys));
    methodBox->addItem(tr("Dictionary (chk)"), int(MfcRecovery::Method::Dictionary));
    methodBox->addItem(tr("Automatic (autopwn)"), int(MfcRecovery::Method::AutoPwn));
    methodBox->addItem(tr("Nested"), int(MfcRecovery::Method::Nested));
    methodBox->addItem(tr("Static nested"), int(MfcRecovery::Method::StaticNested));
    methodBox->addItem(tr("Hardnested"), int(MfcRecovery::Method::Hardnested));
    methodBox->addItem(tr("Darkside"), int(MfcRecovery::Method::Darkside));
    methodBox->addItem(tr("SEN (static encrypted nonce)"), int(MfcRecovery::Method::Sen));
    methodBox->addItem(tr("ISEN (diagnostics)"), int(MfcRecovery::Method::Isen));
    dictionaryEdit = new QLineEdit(methodPage);
    dictionaryEdit->setObjectName("mfcDictionaryEdit");
    dictionaryEdit->setPlaceholderText(tr("Optional dictionary file (chk -f)"));
    includeDefaultsBox = new QCheckBox(tr("Include built-in default keys"), methodPage);
    includeDefaultsBox->setChecked(true);
    knownSectorSpin = new QSpinBox(methodPage);
    knownSectorSpin->setObjectName("mfcKnownSectorSpin");
    knownSectorSpin->setRange(0, doc.sectorCount() - 1);
    knownTypeBox = new QComboBox(methodPage);
    knownTypeBox->setObjectName("mfcKnownTypeBox");
    knownTypeBox->addItem("A", 'A');
    knownTypeBox->addItem("B", 'B');
    targetSectorSpin = new QSpinBox(methodPage);
    targetSectorSpin->setObjectName("mfcTargetSectorSpin");
    targetSectorSpin->setRange(0, doc.sectorCount() - 1);
    targetTypeBox = new QComboBox(methodPage);
    targetTypeBox->setObjectName("mfcTargetTypeBox");
    targetTypeBox->addItem("A", 'A');
    targetTypeBox->addItem("B", 'B');
    runButton = new QPushButton(tr("Run selected method"), methodPage);
    runButton->setObjectName("mfcRunButton");
    cancelButton = new QPushButton(tr("Cancel task"), methodPage);
    cancelButton->setObjectName("mfcCancelButton");
    cancelButton->setEnabled(false);

    controls->addWidget(new QLabel(tr("Method:"), methodPage), 0, 0);
    controls->addWidget(methodBox, 0, 1);
    controls->addWidget(new QLabel(tr("Dictionary:"), methodPage), 1, 0);
    controls->addWidget(dictionaryEdit, 1, 1);
    controls->addWidget(includeDefaultsBox, 1, 2);
    controls->addWidget(new QLabel(tr("Known sector:"), methodPage), 2, 0);
    controls->addWidget(knownSectorSpin, 2, 1);
    controls->addWidget(knownTypeBox, 2, 2);
    controls->addWidget(new QLabel(tr("Target sector:"), methodPage), 3, 0);
    controls->addWidget(targetSectorSpin, 3, 1);
    controls->addWidget(targetTypeBox, 3, 2);
    controls->addWidget(runButton, 4, 1);
    controls->addWidget(cancelButton, 4, 2);
    methodLayout->addLayout(controls);
    pageTabs->addTab(methodPage, tr("Key recovery / decryption"));
    pageTabs->setObjectName("mfcTabs");
    pageTabs->setCurrentIndex(1); // the daily flow starts at Data / backup
    // Switching to the recovery page is an implicit "what does this card
    // allow?" check.  Run the keyless nonce diagnostic so the applicability
    // table starts from evidence instead of "unknown (run diagnostics)".
    connect(pageTabs, &QTabWidget::currentChanged, this, [this, methodPage](int) {
        if(pageTabs->currentWidget() != methodPage)
            return;
        diagnoseNonceOnOpen();
    });

    root->addWidget(pageTabs, 1);

    logEdit = new QPlainTextEdit(this);
    logEdit->setReadOnly(true);
    logEdit->setMaximumHeight(150);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QGroupBox* logBox = new QGroupBox(tr("Task log"), this);
    QVBoxLayout* logLayout = new QVBoxLayout(logBox);
    logLayout->addWidget(logEdit);
    QPushButton* clearLogButton = new QPushButton(tr("Clear log"), logBox);
    logLayout->addWidget(clearLogButton, 0, Qt::AlignRight);
    root->addWidget(logBox);

    connect(cardTypeBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if(updatingTables)
            return;
        const Mfc::CardType type = intToType(cardTypeBox->currentData().toInt());
        if(type == doc.cardType())
            return;
        if(doc.completeBlockCount() > 0 || doc.verifiedKeyCount() > 0)
        {
            if(QMessageBox::question(this, tr("Change card type"),
                                     tr("Changing the card type clears the current document. Continue?"))
               != QMessageBox::Yes)
            {
                updatingTables = true;
                cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
                updatingTables = false;
                return;
            }
        }
        doc.setCardType(type);
        nonce = MfcRecovery::NonceType::Unknown;
        refreshKeyTable();
        refreshDataTable();
        refreshAdvice();
        refreshSummary();
        applyToMifare();
        emit documentTypeChanged(typeToInt(type));
    });
    connect(filterBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MfcWorkbench::onFilterChanged);
    connect(methodBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MfcWorkbench::onMethodChanged);
    connect(runButton, &QPushButton::clicked, this, &MfcWorkbench::runSelectedMethod);
    connect(cancelButton, &QPushButton::clicked, this, &MfcWorkbench::cancelTask);
    connect(readButton, &QPushButton::clicked, this, &MfcWorkbench::readBlocks);
    connect(readUnknownButton, &QPushButton::clicked, this, &MfcWorkbench::readUnknownBlocks);
    connect(backupButton, &QPushButton::clicked, this, &MfcWorkbench::saveBackup);
    connect(writeButton, &QPushButton::clicked, this, &MfcWorkbench::writePlan);
    connect(loadKeyButton, &QPushButton::clicked, this, &MfcWorkbench::loadKeyFile);
    connect(saveKeyButton, &QPushButton::clicked, this, &MfcWorkbench::saveKeyFile);
    connect(verifyButton, &QPushButton::clicked, this, &MfcWorkbench::verifyKeys);
    connect(infoButton, &QPushButton::clicked, this, &MfcWorkbench::readCardInfo);
    connect(nonceButton, &QPushButton::clicked, this, &MfcWorkbench::diagnoseNonce);
    connect(importButton, &QPushButton::clicked, this, &MfcWorkbench::importFromMifare);
    connect(applyButton, &QPushButton::clicked, this, &MfcWorkbench::applyToMifare);
    connect(loadAButton, &QPushButton::clicked, this, &MfcWorkbench::loadCompareA);
    connect(loadBButton, &QPushButton::clicked, this, &MfcWorkbench::loadCompareB);
    connect(documentAButton, &QPushButton::clicked, this, [this]() {
        compareA = doc;
        hasCompareA = true;
        compareAEdit->setText(tr("Current document (%1)").arg(tabs.value(activeIndex).name));
    });
    connect(documentBButton, &QPushButton::clicked, this, [this]() {
        compareB = doc;
        hasCompareB = true;
        compareBEdit->setText(tr("Current document (%1)").arg(tabs.value(activeIndex).name));
    });
    connect(snapshotAButton, &QPushButton::clicked, this, [this]() {
        compareA = snapshotDocument();
        hasCompareA = true;
        compareAEdit->setText(tr("Card %1 snapshot %2 (%3 block(s))")
                              .arg(compareA.uid, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
                              .arg(compareA.completeBlockCount()));
    });
    connect(snapshotBButton, &QPushButton::clicked, this, [this]() {
        compareB = snapshotDocument();
        hasCompareB = true;
        compareBEdit->setText(tr("Card %1 snapshot %2 (%3 block(s))")
                              .arg(compareB.uid, QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
                              .arg(compareB.completeBlockCount()));
    });
    connect(libraryAButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if(!loadCompareFromLibrary(true, &error))
            QMessageBox::information(this, tr("Compare"), error);
    });
    connect(libraryBButton, &QPushButton::clicked, this, [this]() {
        QString error;
        if(!loadCompareFromLibrary(false, &error))
            QMessageBox::information(this, tr("Compare"), error);
    });
    connect(compareButton, &QPushButton::clicked, this, &MfcWorkbench::compareDocuments);
    connect(exportButton, &QPushButton::clicked, this, &MfcWorkbench::exportCompareReport);
    connect(showDiffOnlyBox, &QCheckBox::stateChanged, this, &MfcWorkbench::refreshDiffTable);
    connect(clearLogButton, &QPushButton::clicked, logEdit, &QPlainTextEdit::clear);
    connect(keyTable, &QTableWidget::itemChanged, this, &MfcWorkbench::onKeyItemChanged);
    connect(dataTable, &QTableWidget::itemChanged, this, &MfcWorkbench::onDataItemChanged);
    connect(docTabBar, &QTabBar::currentChanged, this, &MfcWorkbench::documentTabChanged);
    connect(docTabBar, &QTabBar::tabCloseRequested, this, &MfcWorkbench::documentTabCloseRequested);
    connect(docTabBar, &QTabBar::customContextMenuRequested, this, &MfcWorkbench::documentTabContextMenu);
    connect(batchButton, &QPushButton::clicked, this, &MfcWorkbench::startBatchFromDialog);
    connect(scanButton, &QPushButton::toggled, this, &MfcWorkbench::scanToggled);
    connect(autoReadBox, &QCheckBox::toggled, this, [this](bool on) { scanAutoRead = on; });
    connect(scanTable, &QTableWidget::itemSelectionChanged, this, &MfcWorkbench::scanItemChanged);
    identifyAction = new QAction(tr("Identify card"), this);
    identifyAction->setObjectName("mfcIdentifyAction");
    connect(identifyAction, &QAction::triggered, this, &MfcWorkbench::readCardInfo);
    addAction(identifyAction);
    readAction = new QAction(tr("Read missing blocks"), this);
    readAction->setObjectName("mfcReadAction");
    connect(readAction, &QAction::triggered, this, &MfcWorkbench::readUnknownBlocks);
    addAction(readAction);
    verifyAction = new QAction(tr("Verify known keys"), this);
    verifyAction->setObjectName("mfcVerifyAction");
    connect(verifyAction, &QAction::triggered, this, &MfcWorkbench::verifyKeys);
    addAction(verifyAction);
    backupAction = new QAction(tr("Save backup..."), this);
    backupAction->setObjectName("mfcBackupAction");
    connect(backupAction, &QAction::triggered, this, &MfcWorkbench::saveBackup);
    addAction(backupAction);
    writeAction = new QAction(tr("Write plan..."), this);
    writeAction->setObjectName("mfcWriteAction");
    connect(writeAction, &QAction::triggered, this, &MfcWorkbench::writePlan);
    addAction(writeAction);
    batchAction = new QAction(tr("Fill missing keys..."), this);
    batchAction->setObjectName("mfcBatchAction");
    connect(batchAction, &QAction::triggered, this, &MfcWorkbench::startBatchFromDialog);
    addAction(batchAction);
    scanAction = new QAction(tr("Start continuous scan"), this);
    scanAction->setObjectName("mfcScanAction");
    connect(scanAction, &QAction::triggered, this, [this]() {
        scanButton->setChecked(!scanButton->isChecked());
    });
    addAction(scanAction);
    connect(scanNameButton, &QPushButton::clicked, this, [this]() {
        const QModelIndexList rows = scanTable->selectionModel() ? scanTable->selectionModel()->selectedRows() : QModelIndexList();
        if(rows.size() != 1)
        {
            setStatus(tr("Select exactly one scan result to name it"));
            return;
        }
        const int index = scanTable->item(rows.first().row(), 0)->data(Qt::UserRole).toInt();
        if(index < 0 || index >= tabs.size())
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Name scan result"), tr("Name:"),
                                                   QLineEdit::Normal, tabs[index].name, &ok);
        if(ok && !name.trimmed().isEmpty())
        {
            tabs[index].name = name.trimmed();
            refreshDocumentTabs();
            refreshScanList();
        }
    });
    connect(scanCompareButton, &QPushButton::clicked, this, [this]() {
        QList<int> selected;
        for(const QModelIndex& index : scanTable->selectionModel()->selectedRows())
            selected << scanTable->item(index.row(), 0)->data(Qt::UserRole).toInt();
        if(selected.size() != 2)
        {
            setStatus(tr("Select exactly two scan results to compare"));
            return;
        }
        compareA = tabs.at(selected.at(0)).document;
        compareB = tabs.at(selected.at(1)).document;
        hasCompareA = true;
        hasCompareB = true;
        compareAEdit->setText(tabs.at(selected.at(0)).name);
        compareBEdit->setText(tabs.at(selected.at(1)).name);
        comparison = compareA.compare(compareB);
        refreshDiffTable();
        pageTabs->setCurrentIndex(2);
    });
    connect(scanExportButton, &QPushButton::clicked, this, [this]() {
        QList<int> selected;
        for(const QModelIndex& index : scanTable->selectionModel()->selectedRows())
            selected << scanTable->item(index.row(), 0)->data(Qt::UserRole).toInt();
        if(selected.isEmpty())
        {
            setStatus(tr("Select at least one scan result"));
            return;
        }
        const QString directory = QFileDialog::getExistingDirectory(this, tr("Export scan results"));
        if(directory.isEmpty())
            return;
        int written = 0;
        for(int index : selected)
        {
            const QString name = QDir(directory).filePath(
                                     QStringLiteral("%1.eml").arg(tabs.at(index).name.isEmpty()
                                                                 ? QStringLiteral("scan") : tabs.at(index).name));
            const Mfc::IoResult result = tabs.at(index).document.saveDataFile(name, QStringLiteral("eml"));
            if(result.ok)
                written++;
            else
                logLine(tr("Export %1: %2").arg(name, result.error));
        }
        setStatus(tr("Exported %1 of %2 scan result(s)").arg(written).arg(selected.size()));
    });
    connect(scanRemoveButton, &QPushButton::clicked, this, [this]() {
        const QModelIndexList rows = scanTable->selectionModel() ? scanTable->selectionModel()->selectedRows() : QModelIndexList();
        if(rows.isEmpty())
            return;
        QList<int> indices;
        for(const QModelIndex& index : rows)
            indices << scanTable->item(index.row(), 0)->data(Qt::UserRole).toInt();
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        for(int index : indices)
            closeDocument(index);
        refreshScanList();
    });
    connect(keySummary, &QLabel::linkActivated, this, &MfcWorkbench::linkActivated);
    connect(blockSummary, &QLabel::linkActivated, this, &MfcWorkbench::linkActivated);
    connect(dataTable, &QTableWidget::itemSelectionChanged, this, &MfcWorkbench::refreshBlockDetails);
    connect(dataTable, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction(tr("Copy hex"), this, &MfcWorkbench::copyBlockHex);
        menu.addAction(tr("Paste rows"), this, &MfcWorkbench::pasteBlocks);
        menu.addAction(tr("Export selected blocks..."), this, &MfcWorkbench::exportSelectedBlocks);
        menu.addSeparator();
        menu.addAction(tr("Go to comparison"), this, [this]() {
            pageTabs->setCurrentIndex(2);
        });
        menu.addAction(tr("Show sector in key table"), this, [this]() {
            const int block = dataTable->currentItem() ? dataTable->currentItem()->data(Qt::UserRole).toInt() : -1;
            if(block < 0)
                return;
            const int sector = doc.spec().sectorOfBlock(block);
            keyTable->selectRow(sector);
            pageTabs->setCurrentIndex(0);
        });
        menu.exec(dataTable->viewport()->mapToGlobal(pos));
    });
    QAction* undoAction = new QAction(tr("Undo edit"), this);
    undoAction->setShortcut(QKeySequence::Undo);
    undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(undoAction, &QAction::triggered, this, &MfcWorkbench::undoDocument);
    addAction(undoAction);
    QAction* redoAction = new QAction(tr("Redo edit"), this);
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(redoAction, &QAction::triggered, this, &MfcWorkbench::redoDocument);
    addAction(redoAction);
    connect(searchEdit, &QLineEdit::textChanged, this, &MfcWorkbench::onFilterChanged);
    connect(sectorFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MfcWorkbench::onFilterChanged);
}

void MfcWorkbench::setConfigMap(const QVariantMap& config)
{
    configMap = config;
    refreshAdvice();
}

void MfcWorkbench::updateWindowTitle()
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
    {
        setWindowTitle(tr("MFC workbench"));
        return;
    }
    setWindowTitle(tr("%1%2 - MFC workbench")
                   .arg(tabs.at(activeIndex).dirty ? QStringLiteral("* ") : QString(),
                        tabs.at(activeIndex).name));
}

void MfcWorkbench::refreshDocumentTabs()
{
    if(docTabBar == nullptr)
        return;
    const QSignalBlocker blocker(docTabBar);
    while(docTabBar->count() > 0)
        docTabBar->removeTab(0);
    for(int i = 0; i < tabs.size(); i++)
    {
        QString name = tabs.at(i).name;
        if(name.isEmpty())
            name = tr("Document");
        if(tabs.at(i).dirty)
            name = QStringLiteral("* ") + name;
        const int index = docTabBar->addTab(name);
        docTabBar->setTabToolTip(index, tr("%1 - source: %2%3")
                                 .arg(tabs.at(i).name, tabs.at(i).sourceKind,
                                      tabs.at(i).sourcePath.isEmpty() ? QString()
                                      : QStringLiteral(" - ") + tabs.at(i).sourcePath));
    }
    if(activeIndex >= 0 && activeIndex < docTabBar->count())
        docTabBar->setCurrentIndex(activeIndex);
    if(docMenuButton != nullptr)
        docMenuButton->setText(tr("Document (%1)").arg(tabs.size()));
    updateWindowTitle();
}

void MfcWorkbench::saveActiveView()
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
        return;
    DocumentTab& tab = tabs[activeIndex];
    tab.document = doc;
    tab.scroll = dataTable != nullptr ? dataTable->verticalScrollBar()->value() : 0;
    QTableWidgetItem* item = dataTable != nullptr ? dataTable->currentItem() : nullptr;
    tab.selectedBlock = item != nullptr ? item->data(Qt::UserRole).toInt() : -1;
    tab.filterData = filterBox != nullptr ? filterBox->currentData().toString() : QStringLiteral("all");
    tab.search = searchEdit != nullptr ? searchEdit->text() : QString();
}

void MfcWorkbench::loadActiveDocument()
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
        return;
    const DocumentTab& tab = tabs.at(activeIndex);
    doc = tab.document;
    updatingTables = true;
    if(cardTypeBox != nullptr)
        cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
    if(filterBox != nullptr)
    {
        const int filterIndex = filterBox->findData(tab.filterData);
        filterBox->setCurrentIndex(filterIndex >= 0 ? filterIndex : 0);
    }
    if(searchEdit != nullptr)
        searchEdit->setText(tab.search);
    updatingTables = false;
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    if(dataTable != nullptr)
        dataTable->verticalScrollBar()->setValue(tab.scroll);
    if(tab.selectedBlock >= 0)
        locateBlock(tab.selectedBlock);
    updateWindowTitle();
}

int MfcWorkbench::addDocument(const Mfc::Document& document, const QString& name,
                              const QString& sourceKind, const QString& sourcePath)
{
    saveActiveView();
    DocumentTab tab;
    tab.document = document;
    tab.name = name;
    tab.sourceKind = sourceKind;
    tab.sourcePath = sourcePath;
    tab.filterData = QStringLiteral("all");
    tabs.append(tab);
    activeIndex = tabs.size() - 1;
    doc = document;
    updatingTables = true;
    if(cardTypeBox != nullptr)
        cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
    updatingTables = false;
    refreshDocumentTabs();
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    emit documentActivated(doc, name, sourceKind);
    return activeIndex;
}

void MfcWorkbench::documentTabChanged(int index)
{
    if(updatingTables || index < 0 || index >= tabs.size() || index == activeIndex)
        return;
    // A synchronous read/verify loop mutates `doc` between event loops, so a
    // tab switch would redirect its results.  An asynchronous task only writes
    // on completion; switching is safe there and the result is routed back to
    // the tab that started it.
    if(taskRunning && !util->isCommandActive())
    {
        const QSignalBlocker blocker(docTabBar);
        docTabBar->setCurrentIndex(activeIndex);
        setStatus(tr("Finish or stop the running read/verify loop before switching documents"));
        return;
    }
    saveActiveView();
    activeIndex = index;
    loadActiveDocument();
}

void MfcWorkbench::activateDocument(int index)
{
    documentTabChanged(index);
}

QString MfcWorkbench::documentTabName(int index) const
{
    return index >= 0 && index < tabs.size() ? tabs.at(index).name : QString();
}

QString MfcWorkbench::documentTabSource(int index) const
{
    return index >= 0 && index < tabs.size() ? tabs.at(index).sourceKind : QString();
}

bool MfcWorkbench::documentTabDirty(int index) const
{
    return index >= 0 && index < tabs.size() && tabs.at(index).dirty;
}

void MfcWorkbench::documentTabCloseRequested(int index)
{
    closeDocument(index);
}

void MfcWorkbench::documentTabContextMenu(const QPoint& pos)
{
    QMenu menu(this);
    menu.addAction(tr("New document"), this, &MfcWorkbench::newDocumentTab);
    menu.addAction(tr("Open dump..."), this, &MfcWorkbench::openDocumentTab);
    menu.addAction(tr("Save document..."), this, &MfcWorkbench::saveCurrentDocument);
    menu.addSeparator();
    const int index = docTabBar->tabAt(pos);
    if(index >= 0)
    {
        menu.addAction(tr("Rename \"%1\"...").arg(tabs.at(index).name), this, &MfcWorkbench::renameCurrentDocument);
        menu.addAction(tr("Close \"%1\"").arg(tabs.at(index).name), this, [this, index]() {
            closeDocument(index);
        });
    }
    menu.exec(docTabBar->mapToGlobal(pos));
}

void MfcWorkbench::newDocumentTab()
{
    Mfc::Document fresh(doc.cardType());
    addDocument(fresh, tr("Document %1").arg(tabs.size() + 1), QStringLiteral("new"));
    setStatus(tr("New document"));
}

void MfcWorkbench::openDocumentTab()
{
    const QString filename = QFileDialog::getOpenFileName(
                                 this, tr("Open MIFARE Classic dump"), QString(),
                                 tr("Dumps (*.bin *.dump *.eml *.txt *.json);;All files (*)"));
    if(filename.isEmpty())
        return;
    QString error;
    if(!openDocumentFile(filename, &error))
        QMessageBox::information(this, tr("Info"), error);
}

bool MfcWorkbench::openDocumentFile(const QString& filename, QString* error)
{
    const int existing = indexOfDocumentFile(filename);
    if(existing >= 0)
    {
        activateDocument(existing);
        return true;
    }
    Mfc::Document loaded;
    const Mfc::IoResult result = Mfc::Document::loadDataFile(filename, QStringLiteral("auto"), &loaded);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    addDocument(loaded, QFileInfo(filename).completeBaseName(), QStringLiteral("file"), filename);
    setStatus(tr("Opened %1").arg(filename));
    return true;
}

int MfcWorkbench::indexOfDocumentFile(const QString& filename) const
{
    const QString clean = QFileInfo(filename).absoluteFilePath();
    for(int i = 0; i < tabs.size(); i++)
        if(!tabs.at(i).sourcePath.isEmpty()
           && QFileInfo(tabs.at(i).sourcePath).absoluteFilePath() == clean)
            return i;
    return -1;
}

void MfcWorkbench::closeDocument(int index)
{
    if(index < 0 || index >= tabs.size())
        return;
    if(taskRunning || planBusy || batchRunning)
    {
        setStatus(tr("Stop the running task before closing a document"));
        return;
    }
    if(tabs.size() == 1)
    {
        // Never leave the workbench without a document: reset the only tab.
        doc = Mfc::Document(doc.cardType());
        nonce = MfcRecovery::NonceType::Unknown;
        tabs[0].document = doc;
        tabs[0].name = tr("Document 1");
        tabs[0].sourceKind = QStringLiteral("new");
        tabs[0].sourcePath.clear();
        tabs[0].dirty = false;
        tabs[0].undo.clear();
        tabs[0].redo.clear();
        activeIndex = 0;
        loadActiveDocument();
        refreshDocumentTabs();
        return;
    }
    if(tabs.at(index).dirty)
    {
        QMessageBox box(this);
        box.setWindowTitle(tr("Close document"));
        box.setText(tr("\"%1\" has unsaved changes.").arg(tabs.at(index).name));
        box.setInformativeText(tr("Save it before closing?"));
        QPushButton* save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
        QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if(box.clickedButton() == save)
        {
            activateDocument(index);
            saveCurrentDocument();
            if(tabs.at(index).dirty)
                return;
        }
        else if(box.clickedButton() != discard)
            return;
    }
    if(!tabs.at(index).draftId.isEmpty())
        drafts.remove(tabs.at(index).draftId);
    tabs.removeAt(index);
    if(activeIndex > index)
        activeIndex--;
    else if(activeIndex == index)
        activeIndex = qMin(index, tabs.size() - 1);
    loadActiveDocument();
    refreshDocumentTabs();
}

void MfcWorkbench::closeCurrentDocument()
{
    closeDocument(activeIndex);
}

void MfcWorkbench::renameCurrentDocument()
{
    if(activeIndex < 0)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename document"), tr("Name:"),
                                               QLineEdit::Normal, tabs.at(activeIndex).name, &ok);
    if(!ok || name.trimmed().isEmpty())
        return;
    tabs[activeIndex].name = name.trimmed();
    markDirty();
    refreshDocumentTabs();
}

void MfcWorkbench::saveCurrentDocument()
{
    if(activeIndex < 0)
        return;
    const QString defaultName = QStringLiteral("%1.json")
                                .arg(tabs.at(activeIndex).name.isEmpty()
                                     ? QStringLiteral("mfc") : tabs.at(activeIndex).name);
    const QString filename = QFileDialog::getSaveFileName(
                                 this, tr("Save MIFARE Classic document"), defaultName,
                                 tr("JSON document (*.json);;Text EML (*.eml);;Raw binary (*.bin)"));
    if(filename.isEmpty())
        return;
    QString format = QStringLiteral("json");
    if(filename.endsWith(QStringLiteral(".eml"), Qt::CaseInsensitive)
       || filename.endsWith(QStringLiteral(".txt"), Qt::CaseInsensitive))
        format = QStringLiteral("eml");
    else if(filename.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
            || filename.endsWith(QStringLiteral(".dump"), Qt::CaseInsensitive))
        format = QStringLiteral("bin");
    bool allowUnknown = false;
    if(format == QStringLiteral("bin") && doc.unknownBlockCount() > 0)
    {
        if(QMessageBox::warning(this, tr("Incomplete document"),
                                tr("%1 block(s) are unknown; a binary file uses 00 for them. Continue?")
                                .arg(doc.unknownBlockCount()),
                                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        allowUnknown = true;
    }
    const Mfc::IoResult saved = doc.saveDataFile(filename, format, allowUnknown);
    if(!saved.ok)
    {
        QMessageBox::information(this, tr("Info"), saved.error);
        return;
    }
    if(!saved.warning.isEmpty())
        logLine(saved.warning);
    tabs[activeIndex].sourcePath = filename;
    tabs[activeIndex].sourceKind = QStringLiteral("file");
    tabs[activeIndex].name = QFileInfo(filename).completeBaseName();
    tabs[activeIndex].dirty = false;
    if(!tabs.at(activeIndex).draftId.isEmpty())
    {
        drafts.remove(tabs.at(activeIndex).draftId);
        tabs[activeIndex].draftId.clear();
    }
    refreshDocumentTabs();
    setStatus(tr("Saved %1").arg(filename));
}

void MfcWorkbench::setDraftStoreDir(const QString& dir)
{
    drafts.setDir(dir);
    QString error;
    if(!drafts.load(&error) && !error.isEmpty())
        logLine(error);
    refreshDocumentTabs();
}

void MfcWorkbench::flushDrafts()
{
    if(!drafts.isEnabled())
        return;
    bool saved = false;
    for(int i = 0; i < tabs.size(); i++)
    {
        if(!tabs.at(i).dirty)
            continue;
        if(i == activeIndex)
            tabs[i].document = doc;
        QString id;
        QString error;
        if(drafts.save(tabs.at(i).document, tabs.at(i).draftId, tabs.at(i).name,
                       tabs.at(i).sourceKind, targetSession, &id, &error))
        {
            tabs[i].draftId = id;
            saved = true;
        }
        else if(!error.isEmpty())
            logLine(error);
    }
    if(saved)
        setStatus(tr("Draft saved"));
}

void MfcWorkbench::restoreDraftDialog()
{
    QString error;
    drafts.load(&error);
    if(drafts.drafts().isEmpty())
    {
        QMessageBox::information(this, tr("Restore draft"),
                                 error.isEmpty() ? tr("There is no saved draft.")
                                                 : error);
        return;
    }
    QStringList labels;
    for(const Mfc::DraftInfo& info : drafts.drafts())
        labels << tr("%1 - %2 - %3 - %4 block(s)")
                  .arg(info.name.isEmpty() ? tr("Draft") : info.name,
                       info.uid.isEmpty() ? tr("no UID") : info.uid,
                       info.savedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")),
                       QString::number(info.completeBlocks));
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Restore draft"), tr("Draft:"),
                                                   labels, 0, false, &ok);
    if(!ok || selected.isEmpty())
        return;
    const int index = labels.indexOf(selected);
    if(index < 0)
        return;
    if(!restoreDraft(drafts.drafts().at(index).id, &error))
        QMessageBox::information(this, tr("Restore draft"), error);
}

bool MfcWorkbench::restoreDraft(const QString& id, QString* error)
{
    Mfc::Document loaded;
    Mfc::DraftInfo info;
    if(!drafts.load(id, &loaded, &info, error))
        return false;
    addDocument(loaded, info.name.isEmpty() ? tr("Restored draft") : info.name,
                QStringLiteral("draft"));
    drafts.remove(id);
    // The session ids in a draft are historical: they were produced by an
    // earlier connection and never prove anything about the card in the field.
    targetPresent = deviceReady;
    targetUidValue.clear();
    targetSession = ++sessionSeq;
    refreshSummary();
    setStatus(tr("Draft restored; its RF evidence is historical. Identify the card before reading or writing."));
    return true;
}

void MfcWorkbench::pushUndo()
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
        return;
    tabs[activeIndex].document = doc;
    tabs[activeIndex].undo.append(doc);
    if(tabs[activeIndex].undo.size() > 50)
        tabs[activeIndex].undo.removeFirst();
    tabs[activeIndex].redo.clear();
}

void MfcWorkbench::markDirty()
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
        return;
    tabs[activeIndex].dirty = true;
    refreshDocumentTabs();
}

void MfcWorkbench::setTabDirty(bool dirty)
{
    if(activeIndex < 0 || activeIndex >= tabs.size())
        return;
    tabs[activeIndex].dirty = dirty;
    refreshDocumentTabs();
}

bool MfcWorkbench::undoDocument()
{
    if(activeIndex < 0 || tabs.at(activeIndex).undo.isEmpty())
        return false;
    tabs[activeIndex].redo.append(doc);
    doc = tabs[activeIndex].undo.takeLast();
    tabs[activeIndex].document = doc;
    updatingTables = true;
    cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
    updatingTables = false;
    markDirty();
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    applyToMifare();
    setStatus(tr("Undone"));
    return true;
}

bool MfcWorkbench::redoDocument()
{
    if(activeIndex < 0 || tabs.at(activeIndex).redo.isEmpty())
        return false;
    tabs[activeIndex].undo.append(doc);
    doc = tabs[activeIndex].redo.takeLast();
    tabs[activeIndex].document = doc;
    updatingTables = true;
    cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
    updatingTables = false;
    markDirty();
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    applyToMifare();
    setStatus(tr("Redone"));
    return true;
}

void MfcWorkbench::locateBlock(int block)
{
    if(block < 0 || dataTable == nullptr)
        return;
    for(int row = 0; row < dataTable->rowCount(); row++)
    {
        QTableWidgetItem* item = dataTable->item(row, 0);
        if(item != nullptr && item->data(Qt::UserRole).toInt() == block)
        {
            dataTable->selectRow(row);
            dataTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            refreshBlockDetails();
            return;
        }
    }
    updatingTables = true;
    filterBox->setCurrentIndex(0);
    if(sectorFilter != nullptr)
        sectorFilter->setCurrentIndex(0);
    if(searchEdit != nullptr)
        searchEdit->clear();
    updatingTables = false;
    refreshDataTable();
    for(int row = 0; row < dataTable->rowCount(); row++)
    {
        QTableWidgetItem* item = dataTable->item(row, 0);
        if(item != nullptr && item->data(Qt::UserRole).toInt() == block)
        {
            dataTable->selectRow(row);
            dataTable->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            refreshBlockDetails();
            return;
        }
    }
}

void MfcWorkbench::linkActivated(const QString& link)
{
    if(link == QStringLiteral("missing-keys"))
    {
        for(int sector = 0; sector < doc.sectorCount(); sector++)
        {
            if(doc.key(sector, 'A').verifiedIn(targetSession)
               && doc.key(sector, 'B').verifiedIn(targetSession))
                continue;
            keyTable->selectRow(sector);
            keyTable->scrollToItem(keyTable->item(sector, 0), QAbstractItemView::PositionAtCenter);
            pageTabs->setCurrentIndex(0);
            setStatus(tr("Sector %1 still misses a verified key").arg(sector));
            return;
        }
        setStatus(tr("Every sector has both keys verified in this session"));
        return;
    }
    if(link == QStringLiteral("missing-blocks"))
    {
        for(int block = 0; block < doc.blockCount(); block++)
        {
            if(doc.blockComplete(block))
                continue;
            locateBlock(block);
            setStatus(tr("Block %1 was not read").arg(block));
            return;
        }
        setStatus(tr("Every block is readable"));
        return;
    }
    if(link.startsWith(QStringLiteral("sector:")))
    {
        const int sector = link.mid(7).toInt();
        locateBlock(doc.spec().firstBlockOfSector(sector));
    }
}


void MfcWorkbench::suspendSessionForOneShot()
{
    // Called before the persistent client is stopped to free the port for the
    // one-shot recovery.  The stop is internal, so keep targetSession / UID /
    // present and let confirmOneShotSession() re-check the card afterwards.
    oneShotSessionPending = true;
}

void MfcWorkbench::setDeviceReady(bool ready)
{
    deviceReady = ready;
    if(ready && Util::isDemoMode())
    {
        targetSession = Demo::Provider::kDemoSession;
        sessionSeq = qMax(sessionSeq, targetSession);
        targetPresent = true;
        targetUidValue = QString::fromLatin1(Demo::Provider::kDemoUid);
        targetType = doc.cardType();
        refreshIdentity();
        refreshSummary();
        return;
    }
    if(ready)
    {
        if(oneShotSessionPending)
        {
            // The GUI stopped the client itself for a one-shot recovery.  Do
            // not start a new RF session; confirm the UID first and keep the
            // recovered keys valid only for the same card.
            targetPresent = true;
            targetType = doc.cardType();
            cardConfirmPending = true;
            setStatus(tr("Client reconnected; confirming the card..."));
            QTimer::singleShot(0, this, &MfcWorkbench::confirmOneShotSession);
        }
        else
        {
            // A new client connection is a new RF session: keys verified before it
            // stay in the document as history but are not evidence for this card.
            targetSession = ++sessionSeq;
            targetPresent = true;
            targetUidValue.clear();
            targetType = doc.cardType();
            setStatus(tr("Client connected; identify the card before reading or writing"));
        }
    }
    else
    {
        if(!oneShotSessionPending)
            invalidateTargetWhenDisconnected();
    }
    refreshIdentity();
    refreshSummary();
}

void MfcWorkbench::invalidateTargetWhenDisconnected()
{
    if(!targetPresent && targetSession != 0)
        return;
    targetPresent = false;
    targetSession = ++sessionSeq;
    targetUidValue.clear();
}

bool MfcWorkbench::documentMatchesTarget() const
{
    return targetPresent && !doc.uid.isEmpty() && !targetUidValue.isEmpty()
           && doc.uid.compare(targetUidValue, Qt::CaseInsensitive) == 0
           && doc.cardType() == targetType;
}

bool MfcWorkbench::ensureCardIdentified()
{
    if(documentMatchesTarget())
        return true;
    if(!deviceReady)
    {
        setStatus(tr("Connect the Proxmark3 first"));
        return false;
    }
    // readCardInfo() reports its own failure (no card, another card's document
    // kept, ...), so the caller only has to stop when the target is still open.
    // The flag blocks a second click while the identify dialog is open.
    cardConfirmPending = true;
    readCardInfo();
    cardConfirmPending = false;
    return documentMatchesTarget();
}

bool MfcWorkbench::applyCardIdentity(const QString& uid, Mfc::CardType type, QString* error)
{
    const QString normalized = uid.trimmed().toUpper();
    if(normalized.isEmpty())
    {
        if(error != nullptr)
            *error = tr("The card did not report a UID");
        return false;
    }
    targetUidValue = normalized;
    targetType = type;
    if(Util::isDemoMode() && doc.completeBlockCount() == 0 && doc.verifiedKeyCount() == 0)
    {
        // The demonstration card is a normal document: identify loads it, and
        // every later action goes through the normal workbench paths.
        doc = Demo::Provider::instance().card();
        doc.uid = normalized;
        doc.sourceFile = QStringLiteral("demo");
            updatingTables = true;
        cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
        updatingTables = false;
    }
    // Same card (or an empty document): adopt the identity.  A different card
    // keeps the old document intact and must be resolved by the user.
    if(doc.uid.isEmpty() || doc.uid.compare(normalized, Qt::CaseInsensitive) == 0)
    {
        if(doc.cardType() != type)
        {
            // Changing the size clears the document, so it is only automatic
            // when there is nothing to lose.
            if(doc.completeBlockCount() > 0 || doc.verifiedKeyCount() > 0
               || doc.pendingKeyCount() > 0)
            {
                if(error != nullptr)
                    *error = tr("The document is %1 but the card is %2; start a new document "
                                "for this card to change the size.")
                             .arg(doc.cardDisplayName(), Mfc::Spec::get(type).displayName());
                refreshIdentity();
                return false;
            }
            doc.setCardType(type);
            nonce = MfcRecovery::NonceType::Unknown;
            updatingTables = true;
            cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(type)));
            updatingTables = false;
            emit documentTypeChanged(typeToInt(type));
        }
        doc.uid = normalized;
        if(activeIndex >= 0 && activeIndex < tabs.size())
        {
            tabs[activeIndex].document = doc;
            tabs[activeIndex].dirty = true;
            refreshDocumentTabs();
        }
        refreshIdentity();
        return true;
    }
    if(error != nullptr)
        *error = tr("The document belongs to card %1 (%2); the card in the field is %3 (%4).")
                 .arg(doc.uid, doc.cardDisplayName(), normalized, Mfc::Spec::get(type).displayName());
    refreshIdentity();
    return false;
}

bool MfcWorkbench::startNewDocumentForTarget(QString* error)
{
    if(!targetPresent || targetUidValue.isEmpty())
    {
        if(error != nullptr)
            *error = tr("Identify the card first");
        return false;
    }
    if(Util::isDemoMode())
    {
        doc = Demo::Provider::instance().card();
        doc.uid = targetUidValue;
    }
    else
        doc = Mfc::Document(targetType);
    doc.uid = targetUidValue;
    if(activeIndex >= 0 && activeIndex < tabs.size())
    {
        tabs[activeIndex].document = doc;
        tabs[activeIndex].dirty = true;
    }
    nonce = MfcRecovery::NonceType::Unknown;
    updatingTables = true;
    cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(targetType)));
    updatingTables = false;
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    applyToMifare();
    emit documentTypeChanged(typeToInt(targetType));
    setStatus(tr("New document for card %1").arg(targetUidValue));
    return true;
}

void MfcWorkbench::refreshIdentity()
{
    if(!targetPresent)
        return;
    if(doc.uid.isEmpty())
        setStatus(tr("Card %1 in the field; identify it to bind the document")
                  .arg(targetUidValue.isEmpty() ? tr("(unknown UID)") : targetUidValue));
}

void MfcWorkbench::importFromMifare()
{
    if(mifare == nullptr)
        return;
    Mfc::Document loaded(intToType(mifare->cardType.type));
    loaded.uid = mifare->data_getUID();
    for(int i = 0; i < loaded.blockCount(); i++)
    {
        const QString hex = mifare->data_getData(i).trimmed().toUpper();
        if(!hex.isEmpty())
            loaded.setBlock(i, hex, Mfc::BlockState::Read);
    }
    for(int s = 0; s < loaded.sectorCount(); s++)
    {
        const QString a = mifare->data_getKey(s, Mifare::KEY_A);
        const QString b = mifare->data_getKey(s, Mifare::KEY_B);
        if(Mfc::Document::isKeyText(a))
            loaded.setKey(s, 'A', a, Mfc::KeyState::Pending, Mfc::KeySource::Manual);
        if(Mfc::Document::isKeyText(b))
            loaded.setKey(s, 'B', b, Mfc::KeyState::Pending, Mfc::KeySource::Manual);
    }
    doc = loaded;
    if(activeIndex >= 0 && activeIndex < tabs.size())
    {
        tabs[activeIndex].document = doc;
        tabs[activeIndex].dirty = true;
    }
    nonce = MfcRecovery::NonceType::Unknown;
    updatingTables = true;
    cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
    updatingTables = false;
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    setStatus(tr("Imported the data page into the workbench document"));
}

void MfcWorkbench::applyToMifare()
{
    if(mifare == nullptr)
        return;
    const int type = typeToInt(doc.cardType());
    const bool typeChanged = mifare->cardType.type != type;
    mifare->setCardType(type);
    for(int i = 0; i < doc.blockCount(); i++)
        mifare->data_setData(i, doc.blockHex(i).count(QLatin1Char('?')) == 32 ? QString() : doc.blockHex(i));
    for(int s = 0; s < doc.sectorCount(); s++)
    {
        mifare->data_setKey(s, Mifare::KEY_A, doc.key(s, 'A').value);
        mifare->data_setKey(s, Mifare::KEY_B, doc.key(s, 'B').value);
    }
    if(typeChanged)
        emit documentTypeChanged(type);
    mifare->data_syncWithDataWidget();
    mifare->data_syncWithKeyWidget();
}

void MfcWorkbench::refresh()
{
    if(taskRunning)
    {
        const QString stage = tracker.stage().isEmpty()
                              ? MfcRecovery::detectStage(taskOutput)
                              : tracker.stage();
        stageLabel->setText(tr("Stage: %1 | %2 s")
                            .arg(stage.isEmpty() ? tr("running") : stage)
                            .arg(tracker.elapsedMs() / 1000.0, 0, 'f', 1));
        progress->setRange(0, 0);
    }
    else
    {
        progress->setRange(0, 100);
        progress->setValue(0);
    }
    refreshSummary();
}

void MfcWorkbench::refreshSummary()
{
    QString identity = doc.cardDisplayName();
    if(!doc.uid.isEmpty())
        identity += QStringLiteral(" / ") + doc.uid;
    if(!documentMatchesTarget())
        identity += tr("   [not the card in the field]");
    cardLabel->setText(identity);
    uidLabel->setText(doc.uid.isEmpty() ? tr("(not read)") : doc.uid);
    nonceLabel->setText(nonceText());
    const int current = targetSession == 0 ? 0 : doc.verifiedKeyCountIn(targetSession);
    keySummary->setText(tr("Verified keys %1/%2 in this session | historical %3 | pending %4 | unknown %5"
                           "<br/><a href=\"missing-keys\">Locate the first missing key</a>")
                        .arg(current).arg(doc.keyCount())
                        .arg(doc.verifiedKeyCount() - current)
                        .arg(doc.pendingKeyCount()).arg(doc.unknownKeyCount()));
    blockSummary->setText(tr("Read blocks %1/%2 | unknown %3 | modified %4"
                             "<br/><a href=\"missing-blocks\">Locate the first unread block</a>")
                          .arg(doc.completeBlockCount()).arg(doc.blockCount())
                          .arg(doc.unknownBlockCount()).arg(doc.modifiedBlockCount()));
    if(batchRunning)
    {
        const MfcBatch::Progress progressValue = batchPlan.progress();
        batchLabel->setText(tr("Batch: %1 | processed %2/%3 | verified %4 | missing %5 | failed %6")
                            .arg(batchCurrent >= 0 && batchCurrent < batchPlan.size()
                                 ? MfcBatch::Plan::describe(batchPlan.items().at(batchCurrent))
                                 : tr("idle"))
                            .arg(progressValue.processed).arg(progressValue.total)
                            .arg(progressValue.verified).arg(progressValue.missing)
                            .arg(progressValue.failed));
    }
    else if(!batchLabel->text().isEmpty() && !taskRunning)
        batchLabel->setText(QString());
    if(!taskRunning)
        stageLabel->setText(tr("Idle"));
    runButton->setEnabled(deviceReady && !taskRunning && !planBusy);
    cancelButton->setEnabled(taskRunning);
    if(identifyAction != nullptr)
        identifyAction->setEnabled(deviceReady && !taskRunning && !planBusy && !cardConfirmPending);
    if(readAction != nullptr)
        readAction->setEnabled(deviceReady && !taskRunning && !planBusy);
    if(verifyAction != nullptr)
        verifyAction->setEnabled(deviceReady && !taskRunning && !planBusy);
    if(backupAction != nullptr)
        backupAction->setEnabled(!taskRunning);
    if(writeAction != nullptr)
        writeAction->setEnabled(deviceReady && !taskRunning && !planBusy && documentMatchesTarget());
    if(batchAction != nullptr)
        batchAction->setEnabled(deviceReady && !taskRunning && !planBusy);
    if(scanAction != nullptr)
        scanAction->setEnabled(deviceReady || Util::isDemoMode());
    emit identityChanged(doc.uid, doc.cardDisplayName(),
                         tabs.value(activeIndex).name);
}

QByteArray MfcWorkbench::viewState() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << dataTable->horizontalHeader()->saveState()
           << dataSplitter->saveState()
           << (pageTabs != nullptr ? pageTabs->currentIndex() : 0)
           << (logEdit != nullptr ? logEdit->height() : 0);
    return state;
}

void MfcWorkbench::setViewState(const QByteArray& state)
{
    if(state.isEmpty())
        return;
    QDataStream stream(state);
    QByteArray header;
    QByteArray splitter;
    int page = 0;
    int logHeight = 0;
    stream >> header >> splitter >> page >> logHeight;
    if(!header.isEmpty())
        dataTable->horizontalHeader()->restoreState(header);
    if(!splitter.isEmpty())
        dataSplitter->restoreState(splitter);
    if(pageTabs != nullptr && page >= 0 && page < pageTabs->count())
        pageTabs->setCurrentIndex(page);
    if(logHeight > 0 && logEdit != nullptr)
        logEdit->setMaximumHeight(logHeight);
}

void MfcWorkbench::setCompareDocuments(const Mfc::Document& a, const Mfc::Document& b,
                                       const QString& nameA, const QString& nameB)
{
    compareA = a;
    compareB = b;
    hasCompareA = true;
    hasCompareB = true;
    compareAEdit->setText(nameA.isEmpty() ? tr("document A") : nameA);
    compareBEdit->setText(nameB.isEmpty() ? tr("document B") : nameB);
    comparison = compareA.compare(compareB);
    refreshDiffTable();
}

QString MfcWorkbench::nonceText() const
{
    switch(nonce)
    {
    case MfcRecovery::NonceType::Normal:
        return tr("normal PRNG");
    case MfcRecovery::NonceType::Static:
        return tr("static nonce");
    case MfcRecovery::NonceType::SuperStatic:
        return tr("static nonce (even when nested)");
    case MfcRecovery::NonceType::StaticEncrypted:
        return tr("static encrypted nonce");
    case MfcRecovery::NonceType::Unknown:
        break;
    }
    return tr("unknown (run diagnostics)");
}

QString MfcWorkbench::keyStateText(Mfc::KeyState state) const
{
    switch(state)
    {
    case Mfc::KeyState::Verified:
        return tr("verified");
    case Mfc::KeyState::Pending:
        return tr("candidate");
    case Mfc::KeyState::Failed:
        return tr("failed");
    case Mfc::KeyState::Unknown:
        break;
    }
    return tr("unknown");
}

QString MfcWorkbench::blockStateText(Mfc::BlockState state) const
{
    switch(state)
    {
    case Mfc::BlockState::Read:
        return tr("read");
    case Mfc::BlockState::Modified:
        return tr("modified");
    case Mfc::BlockState::Verified:
        return tr("verified");
    case Mfc::BlockState::Failed:
        return tr("failed");
    case Mfc::BlockState::Unknown:
        break;
    }
    return tr("unknown");
}

void MfcWorkbench::refreshKeyTable()
{
    updatingTables = true;
    keyTable->setRowCount(doc.sectorCount());
    for(int s = 0; s < doc.sectorCount(); s++)
    {
        const Mfc::KeyEntry& a = doc.key(s, 'A');
        const Mfc::KeyEntry& b = doc.key(s, 'B');
        keyTable->setItem(s, 0, readOnlyItem(QString::number(s)));
        if(keyTable->item(s, 1) == nullptr)
            keyTable->setItem(s, 1, new QTableWidgetItem());
        keyTable->item(s, 1)->setText(a.value);
        keyTable->setItem(s, 2, readOnlyItem(keyStateText(a.state)));
        keyTable->setItem(s, 3, readOnlyItem(Mfc::keySourceName(a.source)));
        if(keyTable->item(s, 4) == nullptr)
            keyTable->setItem(s, 4, new QTableWidgetItem());
        keyTable->item(s, 4)->setText(b.value);
        keyTable->setItem(s, 5, readOnlyItem(keyStateText(b.state)));
        keyTable->setItem(s, 6, readOnlyItem(Mfc::keySourceName(b.source)));
        QString note;
        if(a.verified() || b.verified())
            note = tr("readable");
        else
            note = tr("needs recovery / verification");
        keyTable->setItem(s, 7, readOnlyItem(note));
        keyTable->item(s, 2)->setForeground(stateColor(a.state));
        keyTable->item(s, 5)->setForeground(stateColor(b.state));
    }
    updatingTables = false;
}

void MfcWorkbench::refreshDataTable()
{
    updatingTables = true;
    int selected = -1;
    if(dataTable->currentItem() != nullptr)
        selected = dataTable->currentItem()->data(Qt::UserRole).toInt();
    const int scroll = dataTable->verticalScrollBar()->value();
    const QString filter = filterBox->currentData().toString();
    const int sector = sectorFilter != nullptr ? sectorFilter->currentData().toInt() : -1;
    const QString search = searchEdit != nullptr ? searchEdit->text().trimmed().toUpper() : QString();

    QList<int> rows;
    for(int i = 0; i < doc.blockCount(); i++)
    {
        const Mfc::Block& block = doc.block(i);
        bool show = true;
        if(filter == QStringLiteral("unknown"))
            show = !block.complete() || block.state == Mfc::BlockState::Failed;
        else if(filter == QStringLiteral("modified"))
            show = block.state == Mfc::BlockState::Modified;
        else if(filter == QStringLiteral("read"))
            show = block.complete();
        if(show && sector >= 0 && doc.spec().sectorOfBlock(i) != sector)
            show = false;
        if(show && !search.isEmpty())
        {
            const QString hex = block.hex.isEmpty() ? QString(32, QLatin1Char('?')) : block.hex;
            show = hex.contains(search) || asciiOf(hex).toUpper().contains(search);
        }
        if(show)
            rows.append(i);
    }
    dataTable->setRowCount(rows.size());
    for(int row = 0; row < rows.size(); row++)
    {
        const int i = rows.at(row);
        const Mfc::Block& block = doc.block(i);
        QTableWidgetItem* blockItem = readOnlyItem(QString::number(i));
        blockItem->setData(Qt::UserRole, i);
        dataTable->setItem(row, 0, blockItem);
        dataTable->setItem(row, 1, readOnlyItem(QString::number(doc.spec().sectorOfBlock(i))));
        dataTable->setItem(row, 2, readOnlyItem(doc.spec().isTrailerBlock(i) ? tr("trailer") : tr("data")));
        const QString hex = block.hex.isEmpty() ? QString(32, QLatin1Char('?')) : block.hex;
        QTableWidgetItem* hexItem = new QTableWidgetItem(spacedHex(hex));
        hexItem->setData(Qt::UserRole, i);
        dataTable->setItem(row, 3, hexItem);
        dataTable->setItem(row, 4, readOnlyItem(asciiOf(hex)));
        QTableWidgetItem* stateItem = readOnlyItem(blockStateText(block.state));
        if(block.state == Mfc::BlockState::Failed)
            stateItem->setForeground(QColor(190, 40, 40));
        else if(block.state == Mfc::BlockState::Modified)
            stateItem->setForeground(QColor(190, 130, 0));
        else if(block.state == Mfc::BlockState::Verified)
            stateItem->setForeground(QColor(0, 150, 90));
        dataTable->setItem(row, 5, stateItem);
    }
    updatingTables = false;
    dataTable->verticalScrollBar()->setValue(scroll);
    if(selected >= 0)
        for(int row = 0; row < dataTable->rowCount(); row++)
            if(dataTable->item(row, 0)->data(Qt::UserRole).toInt() == selected)
            {
                dataTable->selectRow(row);
                break;
            }
    refreshBlockDetails();
}

void MfcWorkbench::refreshBlockDetails()
{
    if(detailView == nullptr)
        return;
    QTableWidgetItem* item = dataTable->currentItem();
    if(item == nullptr)
    {
        detailView->setPlainText(tr("Select a block to see its details."));
        return;
    }
    const int block = item->data(Qt::UserRole).toInt();
    if(block < 0 || block >= doc.blockCount())
    {
        detailView->clear();
        return;
    }
    const Mfc::Block& data = doc.block(block);
    const int sector = doc.spec().sectorOfBlock(block);
    const QString hex = data.hex.isEmpty() ? QString(32, QLatin1Char('?')) : data.hex;
    QStringList lines;
    lines << tr("Block %1 | sector %2 | %3")
             .arg(block).arg(sector)
             .arg(doc.spec().isTrailerBlock(block) ? tr("sector trailer") : tr("data"));
    lines << tr("State: %1").arg(blockStateText(data.state));
    if(data.readSession == 0)
        lines << tr("Source: file or manual edit (no RF session)");
    else
        lines << tr("Source: card, session %1%2")
                 .arg(data.readSession)
                 .arg(data.readSession == targetSession ? tr(" (current)") : tr(" (historical)"));
    const QString operation = tabs.value(activeIndex).lastOperation.value(block);
    lines << tr("Last operation: %1").arg(operation.isEmpty() ? tr("none") : operation);
    lines << QString();
    lines << tr("Hex:   %1").arg(spacedHex(hex));
    lines << tr("ASCII: %1").arg(asciiOf(hex));
    if(doc.spec().isTrailerBlock(block))
    {
        const Mfc::KeyEntry& keyA = doc.key(sector, 'A');
        const Mfc::KeyEntry& keyB = doc.key(sector, 'B');
        const QString acl = hex.mid(12, 8);
        lines << QString();
        lines << tr("Key A: %1 (%2, %3)")
                 .arg(keyA.hasValue() ? keyA.value : tr("unknown"),
                      keyStateText(keyA.state), Mfc::keySourceName(keyA.source));
        lines << tr("Key B: %1 (%2, %3)")
                 .arg(keyB.hasValue() ? keyB.value : tr("unknown"),
                      keyStateText(keyB.state), Mfc::keySourceName(keyB.source));
        lines << tr("Access bits: %1 (%2)")
                 .arg(acl, Mfc::TrailerAcl::valid(acl) ? tr("complement ok") : tr("complement invalid"));
        if(Mfc::TrailerAcl::valid(acl))
        {
            auto yesNo = [](bool value) { return value ? tr("yes") : tr("no"); };
            lines << tr("Key B readable with A: %1").arg(yesNo(Mfc::TrailerAcl::keyBReadable(acl)));
            lines << tr("ACL readable with A/B: %1 / %2")
                     .arg(yesNo(Mfc::TrailerAcl::aclReadable(acl, 'A')),
                          yesNo(Mfc::TrailerAcl::aclReadable(acl, 'B')));
            lines << tr("Key A writable with A/B: %1 / %2")
                     .arg(yesNo(Mfc::TrailerAcl::keyAWriteAllowed(acl, 'A')),
                          yesNo(Mfc::TrailerAcl::keyAWriteAllowed(acl, 'B')));
            lines << tr("Key B writable with A/B: %1 / %2")
                     .arg(yesNo(Mfc::TrailerAcl::keyBWriteAllowed(acl, 'A')),
                          yesNo(Mfc::TrailerAcl::keyBWriteAllowed(acl, 'B')));
        }
    }
    detailView->setPlainText(lines.join(QLatin1Char('\n')));
}

void MfcWorkbench::copyBlockHex()
{
    QStringList lines;
    for(const QModelIndex& index : dataTable->selectionModel()->selectedRows())
    {
        QTableWidgetItem* item = dataTable->item(index.row(), 3);
        if(item != nullptr)
            lines << item->text();
    }
    if(lines.isEmpty() && dataTable->currentItem() != nullptr)
        lines << dataTable->currentItem()->text();
    if(!lines.isEmpty())
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
    setStatus(tr("Copied %1 block(s) as hex").arg(lines.size()));
}

void MfcWorkbench::pasteBlocks()
{
    if(dataTable->currentItem() == nullptr)
        return;
    const QStringList lines = QApplication::clipboard()->text().split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                                     Qt::SkipEmptyParts);
    if(lines.isEmpty())
        return;
    const int startBlock = dataTable->currentItem()->data(Qt::UserRole).toInt();
    QVector<QPair<int, QString>> updates;
    for(int i = 0; i < lines.size(); i++)
    {
        const QString hex = Mfc::Document::normalizeHex(lines.at(i));
        if(hex.size() != 32 || !Mfc::Document::isBlockText(hex))
        {
            QMessageBox::information(this, tr("Paste blocks"),
                                     tr("Line %1 is not 32 hex symbols; nothing was changed.").arg(i + 1));
            return;
        }
        const int block = startBlock + i;
        if(block >= doc.blockCount())
        {
            QMessageBox::information(this, tr("Paste blocks"),
                                     tr("Line %1 is outside the %2 block card; nothing was changed.")
                                     .arg(i + 1).arg(doc.blockCount()));
            return;
        }
        updates.append(qMakePair(block, hex));
    }
    pushUndo();
    for(const QPair<int, QString>& update : updates)
    {
        const bool unknown = update.second.count(QLatin1Char('?')) == 32;
        doc.setBlock(update.first, update.second,
                     unknown ? Mfc::BlockState::Unknown : Mfc::BlockState::Modified);
        tabs[activeIndex].lastOperation[update.first] = tr("pasted from clipboard");
    }
    markDirty();
    refreshDataTable();
    refreshSummary();
    applyToMifare();
    setStatus(tr("Pasted %1 block(s)").arg(updates.size()));
}

void MfcWorkbench::exportSelectedBlocks()
{
    QList<int> blocks;
    for(const QModelIndex& index : dataTable->selectionModel()->selectedRows())
        blocks << dataTable->item(index.row(), 0)->data(Qt::UserRole).toInt();
    if(blocks.isEmpty() && dataTable->currentItem() != nullptr)
        blocks << dataTable->currentItem()->data(Qt::UserRole).toInt();
    if(blocks.isEmpty())
        return;
    const QString filename = QFileDialog::getSaveFileName(this, tr("Export selected blocks"),
                                                          QStringLiteral("blocks.eml"),
                                                          tr("EML (*.eml);;Text (*.txt)"));
    if(filename.isEmpty())
        return;
    QByteArray output;
    for(int block : blocks)
    {
        const QString hex = doc.blockHex(block);
        output += (hex.isEmpty() ? QString(32, QLatin1Char('?')) : hex).toUtf8();
        output += '\n';
    }
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit())
    {
        QMessageBox::information(this, tr("Info"), tr("Cannot write %1").arg(filename));
        return;
    }
    setStatus(tr("Exported %1 block(s)").arg(blocks.size()));
}

void MfcWorkbench::refreshAdvice()
{
    const QVector<MfcRecovery::Advice> advice = MfcRecovery::advise(doc, nonce);
    adviceTable->setRowCount(advice.size());
    for(int i = 0; i < advice.size(); i++)
    {
        const MfcRecovery::Advice& item = advice.at(i);
        adviceTable->setItem(i, 0, readOnlyItem(MfcRecovery::methodName(item.method)));
        adviceTable->setItem(i, 1, readOnlyItem(item.applicable
                                                ? (item.preconditionUnverified ? tr("conditional") : tr("yes"))
                                                : tr("no")));
        adviceTable->setItem(i, 2, readOnlyItem(item.reason));
        adviceTable->setItem(i, 3, readOnlyItem(item.requirements.join(QStringLiteral(", "))));
        adviceTable->item(i, 1)->setForeground(item.applicable
                                               ? (item.preconditionUnverified ? QColor(190, 130, 0) : QColor(0, 150, 90))
                                               : QColor(190, 40, 40));
    }
}

void MfcWorkbench::refreshDiffTable()
{
    diffTable->setRowCount(0);
    if(!hasCompareA || !hasCompareB)
        return;
    QList<Mfc::Diff> rows;
    for(const Mfc::Diff& diff : comparison.blocks)
    {
        if(showDiffOnlyBox->isChecked() && diff.state == Mfc::DiffState::Same)
            continue;
        rows.append(diff);
    }
    diffTable->setRowCount(rows.size());
    for(int i = 0; i < rows.size(); i++)
    {
        const Mfc::Diff& diff = rows.at(i);
        diffTable->setItem(i, 0, readOnlyItem(QString::number(diff.block)));
        diffTable->setItem(i, 1, readOnlyItem(Mfc::diffStateName(diff.state)));
        diffTable->setItem(i, 2, readOnlyItem(spacedHex(diff.a)));
        diffTable->setItem(i, 3, readOnlyItem(spacedHex(diff.b)));
        diffTable->setItem(i, 4, readOnlyItem(diff.note));
        diffTable->item(i, 1)->setForeground(diffColor(diff.state));
    }
    setStatus(tr("Compare: same %1 | different %2 | unknown %3 | incomparable %4")
              .arg(comparison.same).arg(comparison.different)
              .arg(comparison.unknown).arg(comparison.incomparable));
}

void MfcWorkbench::onFilterChanged()
{
    refreshDataTable();
}

void MfcWorkbench::onKeyItemChanged(QTableWidgetItem* item)
{
    if(updatingTables || item == nullptr)
        return;
    if(item->column() != 1 && item->column() != 4)
        return;
    const int sector = item->row();
    const char type = item->column() == 1 ? 'A' : 'B';
    const QString key = Mfc::Document::normalizeKey(item->text());
    pushUndo();
    if(key.isEmpty())
    {
        doc.setKey(sector, type, QString(), Mfc::KeyState::Unknown, Mfc::KeySource::None);
    }
    else if(Mfc::Document::isKeyText(key))
    {
        doc.setKey(sector, type, key, Mfc::KeyState::Pending, Mfc::KeySource::Manual);
    }
    else
    {
        setStatus(tr("A key must be 12 hex symbols"));
        refreshKeyTable();
        return;
    }
    tabs[activeIndex].lastOperation[sector] = tr("Key %1 edited manually").arg(QChar(type));
    markDirty();
    refreshKeyTable();
    refreshSummary();
    applyToMifare();
}

void MfcWorkbench::onDataItemChanged(QTableWidgetItem* item)
{
    if(updatingTables || item == nullptr || item->column() != 3)
        return;
    const int block = item->data(Qt::UserRole).toInt();
    const QString hex = Mfc::Document::normalizeHex(item->text());
    pushUndo();
    if(hex.isEmpty())
    {
        doc.setBlock(block, QString(), Mfc::BlockState::Unknown);
    }
    else if(Mfc::Document::isBlockText(hex))
    {
        doc.setBlock(block, hex, hex.count(QLatin1Char('?')) == 32
                     ? Mfc::BlockState::Unknown : Mfc::BlockState::Modified);
    }
    else
    {
        setStatus(tr("A block must be 32 hex symbols ('?' is allowed for unknown bytes)"));
        refreshDataTable();
        return;
    }
    tabs[activeIndex].lastOperation[block] = tr("edited manually");
    markDirty();
    refreshDataTable();
    refreshSummary();
    applyToMifare();
}

void MfcWorkbench::onMethodChanged()
{
    const MfcRecovery::Method method = MfcRecovery::Method(methodBox->currentData().toInt());
    const QVector<MfcRecovery::Advice> advice = MfcRecovery::advise(doc, nonce);
    for(const MfcRecovery::Advice& item : advice)
    {
        if(item.method == method)
        {
            setStatus(item.reason + (item.applicable ? QString() : tr(" (not applicable now)")));
            break;
        }
    }
}

void MfcWorkbench::runSelectedMethod()
{
    const MfcRecovery::Method method = MfcRecovery::Method(methodBox->currentData().toInt());
    if(method == MfcRecovery::Method::KnownKeys)
    {
        verifyKeys();
        return;
    }
    QString error;
    const MfcRecovery::Command command = commandForMethod(method, &error);
    if(!error.isEmpty())
    {
        QMessageBox::information(this, tr("Info"), error);
        return;
    }
    startMethod(method, command);
}

MfcRecovery::Command MfcWorkbench::commandForMethod(MfcRecovery::Method method, QString* error) const
{
    MfcRecovery::Command command;
    const Mfc::Spec& spec = doc.spec();
    const QString dictionary = dictionaryEdit->text().trimmed();

    auto verifiedKey = [this, &spec](int sector, char type, QString* key) {
        const Mfc::KeyEntry& entry = doc.key(sector, type);
        if(!entry.verified() || !entry.hasValue())
            return false;
        *key = entry.value;
        return true;
    };

    switch(method)
    {
    case MfcRecovery::Method::Dictionary:
        command = MfcRecovery::buildCheck(configMap, spec, dictionary, includeDefaultsBox->isChecked());
        break;
    case MfcRecovery::Method::AutoPwn:
        command = MfcRecovery::buildAutoPwn(configMap, spec, dictionary);
        break;
    case MfcRecovery::Method::Nested:
    case MfcRecovery::Method::StaticNested:
    {
        const int sector = knownSectorSpin->value();
        const char type = knownTypeBox->currentData().toChar().toLatin1();
        QString key;
        if(!verifiedKey(sector, type, &key))
        {
            *error = tr("Sector %1 key %2 is not verified; verify a known key first.")
                     .arg(sector).arg(type);
            break;
        }
        command = MfcRecovery::buildNested(configMap, spec,
                                           method == MfcRecovery::Method::StaticNested,
                                           sector, type, key);
        break;
    }
    case MfcRecovery::Method::Hardnested:
    {
        const int sector = knownSectorSpin->value();
        const char type = knownTypeBox->currentData().toChar().toLatin1();
        QString key;
        if(!verifiedKey(sector, type, &key))
        {
            *error = tr("Sector %1 key %2 is not verified; verify a known key first.")
                     .arg(sector).arg(type);
            break;
        }
        const int target = targetSectorSpin->value();
        const char targetType = targetTypeBox->currentData().toChar().toLatin1();
        if(doc.key(target, targetType).verified())
        {
            *error = tr("Sector %1 key %2 is already verified.").arg(target).arg(targetType);
            break;
        }
        command = MfcRecovery::buildHardnested(configMap, spec, sector, type, key, target, targetType);
        break;
    }
    case MfcRecovery::Method::Darkside:
        command = MfcRecovery::buildDarkside(configMap);
        break;
    case MfcRecovery::Method::Sen:
        command = MfcRecovery::buildSen(configMap);
        break;
    case MfcRecovery::Method::Isen:
        command = MfcRecovery::buildIsen(configMap);
        break;
    case MfcRecovery::Method::KnownKeys:
        break;
    }
    return command;
}

void MfcWorkbench::startMethod(MfcRecovery::Method method, const MfcRecovery::Command& command,
                               bool batchStep)
{
    if(!deviceReady || cardConfirmPending || (!batchStep && util->isChannelOwned()))
    {
        setStatus(cardConfirmPending ? tr("Confirming the card; retry in a moment")
                                     : util->isChannelOwned() ? tr("The client is busy; wait for it to finish")
                                                             : tr("Connect the Proxmark3 first"));
        return;
    }
    if(taskRunning && !batchStep)
    {
        setStatus(tr("Another device task is still running"));
        return;
    }
    if(command.text.trimmed().isEmpty())
    {
        setStatus(tr("The selected method has no command for this client"));
        return;
    }

    Util::AsyncCommand async;
    async.cmd = command.text;
    async.failurePatterns = QStringList({command.failurePattern});
    // Recovery commands poll stdin in v4.23346: they run in a one-shot client
    // process whose exit is the completion boundary, so no "rem" line is ever
    // queued behind a command that would read it as a key press.
    async.isolated = command.usesStdin;
    async.useCompletionMarker = !command.usesStdin;
    async.hardTimeoutMs = command.hardTimeoutMs;
    async.label = MfcRecovery::methodName(method);

    taskOutput.clear();
    currentMethod = method;
    currentTarget.sector = command.targetSector;
    currentTarget.type = command.targetType;
    taskDocumentIndex = activeIndex;
    cancelRequested = false;
    tracker.start(method, doc.keyCount());
    setTaskRunning(true);
    logLine(QStringLiteral("$ ") + command.text);
    if(!util->startCommand(async))
    {
        setTaskRunning(false);
        tracker.finish(MfcRecovery::TaskState::Failed, tr("The client is busy"));
        setStatus(tr("The client is busy with another command"));
        return;
    }
    taskSession = util->commandSession();
    setStatus(command.usesStdin
              ? tr("Running %1 in a one-shot client (the client reconnects when it finishes)...")
                .arg(MfcRecovery::methodName(method))
              : tr("Running %1...").arg(MfcRecovery::methodName(method)));
}

void MfcWorkbench::onCommandOutput(const QString& output)
{
    if(!taskRunning)
        return;
    // Late output of an earlier task may not be attributed to this one.
    const quint64 session = util->commandSession();
    if(taskSession != 0 && session != 0 && session != taskSession)
        return;
    const QString clean = Util::stripAnsi(output);
    taskOutput += clean;
    if(taskOutput.size() > 400000)
        taskOutput = taskOutput.right(200000);
    appendLog(clean);
    const QString stage = MfcRecovery::detectStage(taskOutput);
    if(!stage.isEmpty())
        tracker.setStage(stage);
}

void MfcWorkbench::onCommandFinished(const Util::ExecResult& result, const QString& output)
{
    if(!taskRunning && !batchRunning)
        return;
    // A task is bound to the document that started it.  If the user switched
    // tabs while it ran, bring that document back before touching it: late
    // output must never be applied to another card's document.
    if(taskDocumentIndex >= 0 && taskDocumentIndex < tabs.size() && taskDocumentIndex != activeIndex)
    {
        const int previous = activeIndex;
        saveActiveView();
        activeIndex = taskDocumentIndex;
        loadActiveDocument();
        setStatus(tr("Results applied to \"%1\" (the document that started the task)")
                  .arg(tabs.at(activeIndex).name));
        Q_UNUSED(previous);
    }
    taskOutput = Util::stripAnsi(output);
    taskSession = 0;
    const MfcRecovery::ParseResult parsed = MfcRecovery::parseOutput(taskOutput, doc.spec(), currentTarget);
    applyParsedResult(parsed, currentMethod, result.status);
    if(batchAwaiting)
    {
        batchAfterCommand(result.status);
        return;
    }

    MfcRecovery::TaskState state = MfcRecovery::TaskState::Failed;
    QString message;
    switch(result.status)
    {
    case Util::EXEC_OK:
        if(!parsed.keys.isEmpty())
        {
            state = doc.verifiedKeyCount() == doc.keyCount()
                    ? MfcRecovery::TaskState::Completed : MfcRecovery::TaskState::Partial;
            message = tr("Completed: %1 key(s) verified, %2 still unknown")
                      .arg(doc.verifiedKeyCount()).arg(doc.unknownKeyCount());
        }
        else
        {
            state = MfcRecovery::TaskState::Partial;
            message = tr("Method finished without a usable key");
        }
        break;
    case Util::EXEC_CANCELLED:
        state = MfcRecovery::TaskState::Cancelled;
        message = tr("Cancelled; completed work was kept");
        break;
    case Util::EXEC_ABORTED:
        state = MfcRecovery::TaskState::Aborted;
        message = tr("Client ended before the method completed");
        break;
    case Util::EXEC_TIMEOUT:
        state = MfcRecovery::TaskState::Failed;
        message = tr("Timed out; no result is claimed");
        break;
    case Util::EXEC_FAILED:
        state = MfcRecovery::TaskState::Failed;
        message = tr("The method reported a failure");
        break;
    case Util::EXEC_BUSY:
    case Util::EXEC_NOT_RUNNING:
        state = MfcRecovery::TaskState::Aborted;
        message = tr("The client is not available");
        break;
    }

    if(cancelRequested)
    {
        state = MfcRecovery::TaskState::Cancelled;
        message = tr("Cancelled; completed work was kept");
    }
    tracker.finish(state, message);
    setTaskRunning(false);
    markDirty();
    logLine(message);
    setStatus(message);
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    applyToMifare();
    emit statusMessage(message);
}

void MfcWorkbench::applyParsedResult(const MfcRecovery::ParseResult& parsed,
                                     MfcRecovery::Method method, Util::ExecStatus status)
{
    if(parsed.nonce != MfcRecovery::NonceType::Unknown)
        nonce = parsed.nonce;
    Mfc::KeySource source = Mfc::KeySource::Recovery;
    if(method == MfcRecovery::Method::Dictionary)
        source = Mfc::KeySource::Dictionary;
    MfcRecovery::MergeResult merged = MfcRecovery::merge(parsed, &doc, true, source, targetSession);
    for(const QString& note : parsed.warnings)
        logLine(tr("Parser: %1").arg(note));
    for(const QString& note : merged.notes)
        logLine(tr("Merge: %1").arg(note));
    tracker.setFound(doc.verifiedKeyCount());
    if(status == Util::EXEC_OK && parsed.sawFailure)
        logLine(tr("The client reported a failure in its output"));
}

void MfcWorkbench::cancelTask()
{
    if(!taskRunning)
        return;
    if(!util->isCommandActive())
    {
        // A synchronous read/verify loop is running.
        cancelRequested = true;
        setStatus(tr("Stopping after the current block..."));
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Cancel task"));
    box.setText(tr("How should the running task be stopped?"));
    box.setInformativeText(tr("Stop waiting keeps the client and the device state; "
                              "completed keys are kept. The client may still be executing the "
                              "command, so every page stays blocked until it reports a completion "
                              "boundary. Abort client stops the one-shot client (or reconnects a "
                              "persistent one)."));
    QPushButton* stopWait = box.addButton(tr("Stop waiting"), QMessageBox::AcceptRole);
    QPushButton* abortClient = box.addButton(tr("Abort client"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if(box.clickedButton() == stopWait)
    {
        cancelRequested = true;
        util->cancelCommand(false);
        // The client may still be executing the command.  Util keeps the
        // channel reserved for every page; this workbench additionally needs a
        // new identification before it reuses keys, because the card may have
        // been removed in the meantime.
        invalidateTargetWhenDisconnected();
        refreshSummary();
        setStatus(tr("Stopped waiting; the client may still be executing. "
                     "Reconnect and identify the card before the next task."));
    }
    else if(box.clickedButton() == abortClient)
    {
        cancelRequested = true;
        util->cancelCommand(true);
    }
}

void MfcWorkbench::verifyKeys()
{
    if(!deviceReady || taskRunning || cardConfirmPending || util->isChannelOwned())
    {
        if(cardConfirmPending)
            setStatus(tr("Confirming the card; retry in a moment"));
        else if(util->isChannelOwned())
            setStatus(tr("The client is busy; wait for it to finish"));
        return;
    }
    // Keys are verified against the card in the field.  Identify it first
    // instead of turning the button into a no-op until Identify was pressed.
    if(!documentMatchesTarget() && !ensureCardIdentified())
        return;
    QList<QPair<int, char>> pending;
    for(int s = 0; s < doc.sectorCount(); s++)
    {
        for(char type : {'A', 'B'})
        {
            const Mfc::KeyEntry& entry = doc.key(s, type);
            // A key verified in an earlier RF session (for example the recovery
            // run before the automatic reconnect) is history, not evidence for
            // the card in the field now.  Offer it for re-verification instead
            // of skipping it because its state is still "Verified".
            if(entry.hasValue() && !entry.verifiedIn(targetSession))
                pending.append(qMakePair(s, type));
        }
    }
    if(pending.isEmpty())
    {
        setStatus(tr("No candidate key to verify"));
        return;
    }

    taskRunning = true;
    taskDocumentIndex = activeIndex;
    cancelRequested = false;
    tracker.start(MfcRecovery::Method::KnownKeys, doc.keyCount());
    setTaskRunning(true);
    int done = 0;
    for(const auto& item : pending)
    {
        if(cancelRequested)
            break;
        const Mfc::KeyEntry entry = doc.key(item.first, item.second);
        logLine(tr("Verify sector %1 key %2: %3").arg(item.first).arg(item.second).arg(entry.value));
        verifyOneKey(item.first, item.second, entry.value);
        done++;
        if(done % 4 == 0)
        {
            refreshKeyTable();
            refreshSummary();
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }
    taskRunning = false;
    setTaskRunning(false);
    markDirty();
    refreshKeyTable();
    refreshSummary();
    applyToMifare();
    const QString message = cancelRequested
                            ? tr("Verification stopped; completed results were kept")
                            : tr("Verification finished: %1 verified").arg(doc.verifiedKeyCount());
    tracker.finish(cancelRequested ? MfcRecovery::TaskState::Cancelled
                                   : MfcRecovery::TaskState::Completed, message);
    setStatus(message);
}

bool MfcWorkbench::verifyOneKey(int sector, char type, const QString& key)
{
    QString hex;
    QString error;
    const int block = doc.spec().trailerBlockOfSector(sector);
    const bool ok = readBlockOnce(block, type, key, &hex, &error);
    if(ok)
    {
        doc.setKeyVerified(sector, type, key, Mfc::KeySource::Manual, targetSession);
        return true;
    }
    doc.setKey(sector, type, key, Mfc::KeyState::Failed, Mfc::KeySource::Manual);
    logLine(tr("  failed: %1").arg(error));
    return false;
}

bool MfcWorkbench::chooseVerifiedKey(int sector, char* type, QString* key) const
{
    // Only a key authenticated in the current RF session may be used on the
    // card that is in the field now.
    for(char candidate : {'A', 'B'})
    {
        const Mfc::KeyEntry& entry = doc.key(sector, candidate);
        if(entry.verifiedIn(targetSession) && entry.hasValue())
        {
            *type = candidate;
            *key = entry.value;
            return true;
        }
    }
    return false;
}

bool MfcWorkbench::readBlockOnce(int block, char keyType, const QString& key,
                                 QString* hex, QString* error)
{
    const QVariantMap config = configMap.value("normal read block").toMap();
    QString cmd = config.value("cmd").toString();
    if(cmd.isEmpty())
        cmd = QStringLiteral("hf mf rdbl --blk <block> -<key type> -k <key>");
    const QVariantMap types = config.value("key type").toMap();
    const QString typeName = QString(QChar(keyType));
    cmd.replace(QStringLiteral("<block>"), QString::number(block));
    cmd.replace(QStringLiteral("<key type>"), types.value(typeName).toString().isEmpty()
                ? typeName.toLower() : types.value(typeName).toString());
    cmd.replace(QStringLiteral("<key>"), key);

    const QString pattern = config.value("data pattern").toString().isEmpty()
                            ? QStringLiteral("([0-9a-fA-F]{2} ){15}[0-9a-fA-F]{2}")
                            : config.value("data pattern").toString();
    Util::ReturnTrigger trigger(2500, QStringList({pattern}));
    trigger.hardTimeout = 6000;
    trigger.failedOutputs = QStringList({"\\[-\\]", "Can't authenticate", "command execution time out",
                                         "not available in this mode"});
    Util::ExecResult result = util->execCMDWithResult(cmd, trigger, true);
    QRegularExpression re(pattern);
    const QRegularExpressionMatch match = re.match(result.output);
    if(!result.isOk() || !match.hasMatch())
    {
        if(error != nullptr)
            *error = result.status == Util::EXEC_NOT_RUNNING ? tr("client is not running")
                                                              : tr("read failed or timed out");
        return false;
    }
    *hex = match.captured().toUpper();
    hex->remove(QLatin1Char(' '));
    return true;
}

void MfcWorkbench::readBlocks()
{
    readUnknownBlocks();
}

void MfcWorkbench::readUnknownBlocks()
{
    if(!deviceReady || taskRunning || cardConfirmPending || util->isChannelOwned())
    {
        if(cardConfirmPending)
            setStatus(tr("Confirming the card; retry in a moment"));
        else if(util->isChannelOwned())
            setStatus(tr("The client is busy; wait for it to finish"));
        return;
    }
    if(!documentMatchesTarget())
    {
        setStatus(tr("Identify the card first: reads need keys verified against this card"));
        return;
    }
    taskRunning = true;
    taskDocumentIndex = activeIndex;
    cancelRequested = false;
    tracker.start(MfcRecovery::Method::KnownKeys, doc.blockCount());
    setTaskRunning(true);

    int read = 0;
    int failed = 0;
    for(int block = 0; block < doc.blockCount(); block++)
    {
        if(cancelRequested)
            break;
        if(doc.blockComplete(block))
            continue;
        const int sector = doc.spec().sectorOfBlock(block);
        char keyType = 'A';
        QString key;
        if(!chooseVerifiedKey(sector, &keyType, &key))
        {
            failed++;
            continue;
        }

        QString hex;
        QString error;
        bool ok = readBlockOnce(block, keyType, key, &hex, &error);
        if(!ok && keyType == 'A')
        {
            const Mfc::KeyEntry& b = doc.key(sector, 'B');
            if(b.verifiedIn(targetSession) && b.hasValue())
                ok = readBlockOnce(block, 'B', b.value, &hex, &error);
        }
        if(!ok)
        {
            failed++;
            if(!doc.blockComplete(block))
                doc.setBlockState(block, Mfc::BlockState::Failed);
            tabs[activeIndex].lastOperation[block] = tr("read failed: %1").arg(error);
            logLine(tr("Block %1: %2").arg(block).arg(error));
            continue;
        }

        if(doc.spec().isTrailerBlock(block))
        {
            doc.setBlock(block, trailerReadback(block, keyType, hex), Mfc::BlockState::Read, targetSession);
            if(!doc.blockComplete(block))
                logLine(tr("Block %1: trailer is masked by its access conditions; "
                           "verify both keys to complete it").arg(block));
        }
        else
        {
            doc.setBlock(block, hex, Mfc::BlockState::Read, targetSession);
        }
        tabs[activeIndex].lastOperation[block] = tr("read in session %1").arg(targetSession);
        read++;
        if(read % 8 == 0)
        {
            refreshDataTable();
            refreshSummary();
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }

    taskRunning = false;
    setTaskRunning(false);
    markDirty();
    refreshDataTable();
    refreshKeyTable();
    refreshSummary();
    applyToMifare();
    const QString message = tr("Read %1 block(s), %2 failed, %3 unknown remain")
                            .arg(read).arg(failed).arg(doc.unknownBlockCount());
    tracker.finish(cancelRequested ? MfcRecovery::TaskState::Cancelled
                                   : MfcRecovery::TaskState::Completed, message);
    setStatus(message);
}

QString MfcWorkbench::trailerReadback(int block, char authType, const QString& cardHex) const
{
    // A trailer read returns Key A masked, the access bytes, and Key B only
    // when the access conditions make it readable as data.  Values that were
    // not returned are left unknown; only a key authenticated in this session
    // may fill Key A (the read proves that key works on this sector).
    const QString read = Mfc::Document::normalizeHex(cardHex);
    const int sector = doc.spec().sectorOfBlock(block);
    const Mfc::KeyEntry& keyA = doc.key(sector, 'A');
    QString result = keyA.verifiedIn(targetSession) ? keyA.value : QString(12, QLatin1Char('?'));
    if(read.size() != 32 || !Mfc::Document::isBlockText(read))
        return result + QString(20, QLatin1Char('?'));
    const QString acl = read.mid(12, 8);
    if(!Mfc::TrailerAcl::valid(acl))
        return result + QString(20, QLatin1Char('?'));
    result += acl;
    const bool keyBReadable = Mfc::TrailerAcl::keyBReadable(acl);
    const Mfc::KeyEntry& keyB = doc.key(sector, 'B');
    if(keyBReadable && Mfc::TrailerAcl::aclReadable(acl, authType))
    {
        // The card returned Key B as data.
        result += read.mid(20, 12);
    }
    else if(keyB.verifiedIn(targetSession) && keyB.hasValue())
    {
        // The access conditions mask Key B (or this authentication key may not
        // read it).  The verified key store still holds the value; using it is
        // what makes a trailer complete instead of showing 12 unknown bytes.
        result += keyB.value;
    }
    else
    {
        result += QString(12, QLatin1Char('?'));
    }
    return result;
}

void MfcWorkbench::saveBackup()
{
    const QString defaultName = QStringLiteral("backup-%1-%2.json")
                                .arg(doc.uid.isEmpty() ? QStringLiteral("mfc") : doc.uid,
                                     QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
    const QString filename = QFileDialog::getSaveFileName(
                                 this, tr("Save MIFARE Classic backup"), defaultName,
                                 tr("JSON document (*.json);;Text EML (*.eml);;Raw binary (*.bin)"));
    if(filename.isEmpty())
        return;

    QString format = "json";
    if(filename.endsWith(".eml", Qt::CaseInsensitive) || filename.endsWith(".txt", Qt::CaseInsensitive))
        format = "eml";
    else if(filename.endsWith(".bin", Qt::CaseInsensitive) || filename.endsWith(".dump", Qt::CaseInsensitive))
        format = "bin";

    bool allowUnknown = false;
    if(format == "bin" && doc.unknownBlockCount() > 0)
    {
        if(QMessageBox::warning(this, tr("Incomplete backup"),
                                tr("%1 block(s) were never read. A raw binary file cannot "
                                   "represent unknown data and would contain zeros. Continue?")
                                .arg(doc.unknownBlockCount()),
                                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        allowUnknown = true;
    }

    QString error;
    if(!saveBackupTo(filename, &error, allowUnknown))
    {
        QMessageBox::information(this, tr("Info"), error);
        setStatus(error);
    }
}

bool MfcWorkbench::saveBackupTo(const QString& filename, QString* error, bool allowUnknownBin)
{
    QString format = "json";
    if(filename.endsWith(".eml", Qt::CaseInsensitive) || filename.endsWith(".txt", Qt::CaseInsensitive))
        format = "eml";
    else if(filename.endsWith(".bin", Qt::CaseInsensitive) || filename.endsWith(".dump", Qt::CaseInsensitive))
        format = "bin";

    const Mfc::IoResult result = doc.saveDataFile(filename, format, allowUnknownBin);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    if(!result.warning.isEmpty())
        logLine(result.warning);

    // The sidecar must describe the file that was written, not the internal
    // document digest: "sha256sum -c" and Get-FileHash have to agree with it.
    QString hashError;
    const QString hash = Mfc::Document::sha256OfFile(filename, &hashError);
    if(hash.isEmpty())
    {
        const QString message = tr("Backup saved, but %1").arg(hashError);
        logLine(message);
        if(error != nullptr)
            *error = message;
        setStatus(tr("Backup saved; the .sha256 sidecar could not be computed"));
        return false;
    }
    QFile sidecar(filename + ".sha256");
    if(!sidecar.open(QIODevice::WriteOnly | QIODevice::Text)
       || sidecar.write((hash + "  " + QFileInfo(filename).fileName() + "\n").toUtf8()) < 0
       || !sidecar.flush())
    {
        const QString message = tr("Backup saved, but the .sha256 sidecar could not be written");
        logLine(message);
        if(error != nullptr)
            *error = message;
        setStatus(tr("Backup saved; sidecar failed"));
        return false;
    }
    logLine(tr("Saved %1 (sha256 %2)").arg(filename, hash));
    setStatus(tr("Backup saved; sha256 %1").arg(hash.left(16)));
    if(activeIndex >= 0 && activeIndex < tabs.size())
    {
        tabs[activeIndex].document = doc;
        tabs[activeIndex].sourcePath = filename;
        tabs[activeIndex].sourceKind = QStringLiteral("file");
        tabs[activeIndex].dirty = false;
        if(!tabs.at(activeIndex).draftId.isEmpty())
        {
            drafts.remove(tabs.at(activeIndex).draftId);
            tabs[activeIndex].draftId.clear();
        }
        refreshDocumentTabs();
    }
    return true;
}

Mfc::WriteOutcome MfcWorkbench::writeBlockOnce(int block, const QString& hex,
                                              const Mfc::TrailerPlan* trailer, QString* detail,
                                              QString* readBackOut)
{
    auto say = [detail](const QString& text) {
        if(detail != nullptr)
            *detail = text;
    };
    const int sector = doc.spec().sectorOfBlock(block);
    const bool isTrailer = doc.spec().isTrailerBlock(block);

    char keyType = 0;
    QString key;
    if(isTrailer)
    {
        // The trailer may only be written with a key the card's *current*
        // access conditions accept; the plan already decided that.
        if(trailer == nullptr || !trailer->valid || trailer->writeKeyType == 0)
        {
            say(trailer != nullptr && !trailer->error.isEmpty()
                ? trailer->error : tr("no trailer write plan"));
            return Mfc::WriteOutcome::Blocked;
        }
        keyType = trailer->writeKeyType;
        key = doc.key(sector, keyType).value;
        if(!doc.key(sector, keyType).verifiedIn(targetSession) || !Mfc::Document::isKeyText(key))
        {
            say(tr("Key %1 of sector %2 is not verified in this session").arg(QChar(keyType)).arg(sector));
            return Mfc::WriteOutcome::Blocked;
        }
    }
    else if(!chooseVerifiedKey(sector, &keyType, &key))
    {
        say(tr("no verified key for sector %1").arg(sector));
        return Mfc::WriteOutcome::Blocked;
    }

    const QVariantMap config = configMap.value("normal write block").toMap();
    const QVariantMap types = config.value("key type").toMap();
    QString cmd = config.value("cmd").toString();
    if(cmd.isEmpty())
        cmd = QStringLiteral("hf mf wrbl --blk <block> -<key type> -k <key> -d <data> --force");
    const QString typeName = QString(QChar(keyType));
    cmd.replace(QStringLiteral("<block>"), QString::number(block));
    cmd.replace(QStringLiteral("<key type>"), types.value(typeName).toString().isEmpty()
                ? typeName.toLower() : types.value(typeName).toString());
    cmd.replace(QStringLiteral("<key>"), key);
    cmd.replace(QStringLiteral("<data>"), hex);
    const Util::ExecResult result = util->execCMDWithResult(cmd, Pm3Cmd::makeWriteTrigger(config, 3000));
    if(!result.isOk() || result.failurePatternMatched)
    {
        say(tr("write failed or timed out"));
        return Mfc::WriteOutcome::WriteFailed;
    }

    if(!isTrailer)
    {
        QString readBack;
        QString readError;
        if(!readBlockOnce(block, keyType, key, &readBack, &readError))
        {
            // The client reported a successful write: this is not a failure,
            // it is a write that could not be proven.
            say(tr("write reported ok, read-back failed: %1").arg(readError));
            return Mfc::WriteOutcome::WrittenUnverified;
        }
        if(readBackOut != nullptr)
            *readBackOut = readBack;
        if(readBack != hex)
        {
            say(tr("read-back differs: %1").arg(spacedHex(readBack)));
            return Mfc::WriteOutcome::Mismatch;
        }
        say(tr("read back ok"));
        return Mfc::WriteOutcome::Verified;
    }

    // Trailer: verify with the *new* keys.  Key A is never returned, so a
    // successful authentication is what proves it; the access bytes and Key B
    // are compared only when the new access conditions return them.
    const Mfc::TrailerVerifyPlan& verify = trailer->verify;
    const QString newAcl = hex.mid(12, 8);
    const QString newKeyB = hex.mid(20, 12);
    const char authType = verify.authType == 0 ? 'A' : verify.authType;
    const QString authKey = authType == 'A' ? hex.mid(0, 12) : newKeyB;
    QString readBack;
    QString readError;
    if(!readBlockOnce(block, authType, authKey, &readBack, &readError))
    {
        say(tr("write reported ok; the card did not answer the verification read (%1)").arg(readError));
        return Mfc::WriteOutcome::WrittenUnverified;
    }
    if(readBack.size() != 32)
    {
        say(tr("write reported ok; the verification read was incomplete"));
        return Mfc::WriteOutcome::WrittenUnverified;
    }
    if(readBackOut != nullptr)
        *readBackOut = readBack;
    QStringList differences;
    if(verify.checkAcl && readBack.mid(12, 8) != newAcl)
        differences << tr("access bits %1 (wanted %2)").arg(readBack.mid(12, 8), newAcl);
    const bool keyBComparable = verify.checkKeyB && Mfc::TrailerAcl::keyBReadable(newAcl);
    if(keyBComparable && readBack.mid(20, 12) != newKeyB)
        differences << tr("Key B %1 (wanted %2)").arg(readBack.mid(20, 12), newKeyB);
    if(!differences.isEmpty())
    {
        say(tr("read-back differs: %1").arg(differences.join(QStringLiteral(", "))));
        return Mfc::WriteOutcome::Mismatch;
    }
    if(keyBComparable)
        say(tr("verified: Key A authenticates, access bits and Key B match"));
    else
        say(tr("verified: Key A authenticates, access bits match (Key B is not readable)"));
    return Mfc::WriteOutcome::Verified;
}

void MfcWorkbench::writePlan()
{
    if(taskRunning || planBusy || cardConfirmPending)
    {
        setStatus(cardConfirmPending ? tr("Confirming the card; retry in a moment")
                                     : tr("Another device task is still running"));
        return;
    }
    if(!deviceReady || util->isChannelOwned())
    {
        setStatus(util->isChannelOwned() ? tr("The client is busy; wait for it to finish")
                                          : tr("Connect the Proxmark3 first"));
        return;
    }
    if(!documentMatchesTarget())
    {
        setStatus(tr("Identify the card first: the write plan is bound to the card in the field"));
        return;
    }

    // Freeze the source document and the RF session.  Every command below is
    // checked against this snapshot; a card change aborts the plan instead of
    // writing one card's data onto another.
    plan.reset(doc, targetSession);
    if(plan.isEmpty())
    {
        setStatus(tr("No complete block to write"));
        return;
    }

    QDialog dialog(this);
    dialog.setObjectName("mfcWritePlanDialog");
    dialog.setWindowTitle(tr("Write plan"));
    dialog.resize(900, 520);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* hint = new QLabel(tr("Only complete blocks are written. Block 0 and trailers are "
                                 "excluded unless you explicitly include them; a trailer also "
                                 "needs a preview so its current access conditions can be checked."),
                              &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    QLabel* target = new QLabel(tr("Source document: %1 | target card: %2 / %3 | session %4")
                                .arg(tabs.value(activeIndex).name,
                                     targetUidValue.isEmpty() ? tr("unknown UID") : targetUidValue,
                                     doc.cardDisplayName())
                                .arg(targetSession), &dialog);
    target->setObjectName("mfcWriteTarget");
    target->setWordWrap(true);
    layout->addWidget(target);

    QCheckBox* block0Box = new QCheckBox(tr("Include block 0 (UID/manufacturer)"), &dialog);
    QCheckBox* trailerBox = new QCheckBox(tr("Include sector trailers (keys / access bits)"), &dialog);
    block0Box->setObjectName("mfcWriteIncludeBlock0");
    trailerBox->setObjectName("mfcWriteIncludeTrailers");
    layout->addWidget(block0Box);
    layout->addWidget(trailerBox);

    QTableWidget* table = new QTableWidget(&dialog);
    table->setObjectName("mfcWriteTable");
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels(QStringList()
                                     << tr("Write") << tr("Block") << tr("Type")
                                     << tr("Document") << tr("Card before")
                                     << tr("Read-back after") << tr("State"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* previewButton = new QPushButton(tr("Read card && preview"), &dialog);
    QPushButton* writeButton = new QPushButton(tr("Write selected"), &dialog);
    QPushButton* retryButton = new QPushButton(tr("Retry failed only"), &dialog);
    QPushButton* stopButton = new QPushButton(tr("Stop after current block"), &dialog);
    QPushButton* closeButton = new QPushButton(tr("Close"), &dialog);
    previewButton->setObjectName("mfcWritePreview");
    writeButton->setObjectName("mfcWriteWrite");
    retryButton->setObjectName("mfcWriteRetry");
    stopButton->setObjectName("mfcWriteStop");
    closeButton->setObjectName("mfcWriteClose");
    buttons->addWidget(previewButton);
    buttons->addWidget(writeButton);
    buttons->addWidget(retryButton);
    buttons->addWidget(stopButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    const QVector<Mfc::WriteRow> rows = plan.rows();
    table->setRowCount(rows.size());
    for(int row = 0; row < rows.size(); row++)
    {
        const int block = rows.at(row).block;
        QTableWidgetItem* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Unchecked);
        table->setItem(row, 0, check);
        QTableWidgetItem* blockItem = readOnlyItem(QString::number(block));
        blockItem->setData(Qt::UserRole, block);
        table->setItem(row, 1, blockItem);
        table->setItem(row, 2, readOnlyItem(doc.spec().isTrailerBlock(block) ? tr("trailer") : tr("data")));
        table->setItem(row, 3, readOnlyItem(spacedHex(plan.source().blockHex(block))));
        table->setItem(row, 4, readOnlyItem(QString()));
        table->setItem(row, 5, readOnlyItem(QString()));
        table->setItem(row, 6, readOnlyItem(QString()));
    }

    auto syncRow = [&](int row) {
        const Mfc::WriteRow current = plan.rows().at(row);
        const QSignalBlocker blocker(table);
        table->item(row, 0)->setCheckState(current.selected ? Qt::Checked : Qt::Unchecked);
        table->item(row, 4)->setText(current.cardHex.isEmpty() ? QString() : spacedHex(current.cardHex));
        table->item(row, 5)->setText(current.readBack.isEmpty() ? QString() : spacedHex(current.readBack));
        QString state;
        if(current.outcome != Mfc::WriteOutcome::Pending)
            state = Mfc::writeOutcomeName(current.outcome);
        else if(current.preview != Mfc::PreviewState::NotPreviewed)
            state = Mfc::previewStateName(current.preview);
        if(!current.note.isEmpty())
            state = state.isEmpty() ? current.note : state + QStringLiteral(" - ") + current.note;
        if(!state.isEmpty() && !current.allowed && !current.blockedReason.isEmpty())
            state += QStringLiteral(" [") + current.blockedReason + QStringLiteral("]");
        else if(state.isEmpty() && !current.allowed)
            state = current.blockedReason;
        table->item(row, 6)->setText(state);
    };
    auto syncAll = [&]() {
        for(int row = 0; row < plan.rows().size(); row++)
            syncRow(row);
    };
    auto applyInclude = [&]() {
        plan.setIncludeBlock0(block0Box->isChecked());
        plan.setIncludeTrailers(trailerBox->isChecked());
    };
    auto setBusy = [&](bool busy) {
        planBusy = busy;
        previewButton->setEnabled(!busy);
        writeButton->setEnabled(!busy);
        retryButton->setEnabled(!busy);
        closeButton->setEnabled(!busy);
        stopButton->setEnabled(busy);
        table->setEnabled(!busy);
        block0Box->setEnabled(!busy);
        trailerBox->setEnabled(!busy);
        refreshSummary();
    };

    // Default: every ordinary data block that already has a verified key.
    for(int row = 0; row < plan.rows().size(); row++)
    {
        const int block = plan.rows().at(row).block;
        if(block == 0 || doc.spec().isTrailerBlock(block))
            continue;
        plan.select(block, true);
    }
    syncAll();

    connect(table, &QTableWidget::itemChanged, &dialog, [&](QTableWidgetItem* item) {
        if(planBusy || item == nullptr || item->column() != 0)
            return;
        const int block = table->item(item->row(), 1)->data(Qt::UserRole).toInt();
        const bool wanted = item->checkState() == Qt::Checked;
        QString reason;
        if(!plan.select(block, wanted, &reason))
            setStatus(reason);
        syncRow(item->row());
    });
    connect(block0Box, &QCheckBox::toggled, &dialog, [&](bool on) {
        if(planBusy)
            return;
        applyInclude();
        plan.select(0, on);
        syncAll();
    });
    connect(trailerBox, &QCheckBox::toggled, &dialog, [&](bool on) {
        if(planBusy)
            return;
        applyInclude();
        for(const Mfc::WriteRow& row : plan.rows())
            if(doc.spec().isTrailerBlock(row.block))
                plan.select(row.block, on);
        syncAll();
    });
    connect(retryButton, &QPushButton::clicked, &dialog, [&]() {
        if(planBusy)
            return;
        applyInclude();
        const QList<int> retry = plan.retryBlocks();
        for(const Mfc::WriteRow& row : plan.rows())
            plan.select(row.block, retry.contains(row.block));
        syncAll();
        setStatus(retry.isEmpty()
                  ? tr("Nothing to retry: only writes that never reported success are retried")
                  : tr("%1 block(s) selected for retry").arg(retry.size()));
    });
    connect(stopButton, &QPushButton::clicked, &dialog, [&]() {
        cancelRequested = true;
    });
    connect(previewButton, &QPushButton::clicked, &dialog, [&]() {
        if(planBusy)
            return;
        if(!plan.matches(doc, targetSession) || !documentMatchesTarget())
        {
            QMessageBox::information(&dialog, tr("Write plan"),
                                     tr("The card changed; identify it again before previewing."));
            return;
        }
        setBusy(true);
        cancelRequested = false;
        plan.clearPreview();
        syncAll();
        for(int row = 0; row < plan.rows().size(); row++)
        {
            if(cancelRequested)
                break;
            QApplication::processEvents();
            const int block = plan.rows().at(row).block;
            const int sector = doc.spec().sectorOfBlock(block);
            const bool isTrailer = doc.spec().isTrailerBlock(block);
            char keyType = 'A';
            QString key;
            if(!chooseVerifiedKey(sector, &keyType, &key))
            {
                plan.setPreview(block, QString(), Mfc::PreviewState::NoKey,
                                     tr("no key verified for sector %1").arg(sector));
                syncRow(row);
                continue;
            }
            QString cardHex;
            QString error;
            if(!readBlockOnce(block, keyType, key, &cardHex, &error))
            {
                plan.setPreview(block, QString(), Mfc::PreviewState::Unreadable, error);
                syncRow(row);
                continue;
            }
            const QString documentHex = plan.source().blockHex(block);
            if(!isTrailer)
            {
                const Mfc::PreviewState state = cardHex == documentHex
                                                ? Mfc::PreviewState::Same : Mfc::PreviewState::Different;
                plan.setPreview(block, cardHex, state,
                                     state == Mfc::PreviewState::Same ? tr("identical") : tr("differs"));
                syncRow(row);
                continue;
            }
            // Trailer: Key A is masked, so compare the fields a read returns.
            const QString cardAcl = cardHex.mid(12, 8);
            const QString docAcl = documentHex.mid(12, 8);
            QStringList differences;
            if(cardAcl != docAcl)
                differences << tr("access bits");
            const bool keyBReadable = Mfc::TrailerAcl::keyBReadable(cardAcl);
            if(keyBReadable && cardHex.mid(20, 12) != documentHex.mid(20, 12))
                differences << tr("Key B");
            const Mfc::PreviewState state = differences.isEmpty() ? Mfc::PreviewState::Same
                                                                  : Mfc::PreviewState::Different;
            QString note = differences.isEmpty()
                           ? tr("readable fields match")
                           : tr("differs: %1").arg(differences.join(QStringLiteral(", ")));
            if(!keyBReadable)
                note += tr("; Key B is not readable by the card");
            plan.setPreview(block, cardHex, state, note);
            syncRow(row);
        }
        setBusy(false);
        setStatus(tr("Preview finished; select the blocks to write"));
    });

    connect(writeButton, &QPushButton::clicked, &dialog, [&]() {
        if(planBusy)
            return;
        if(!plan.matches(doc, targetSession) || !documentMatchesTarget())
        {
            QMessageBox::information(&dialog, tr("Write plan"),
                                     tr("The card changed; identify it again before writing."));
            return;
        }
        applyInclude();
        const QList<int> selected = plan.selectedBlocks();
        if(selected.isEmpty())
        {
            QMessageBox::information(&dialog, tr("Write plan"), tr("No block is selected."));
            return;
        }
        // Re-validate the whole selection before the first command: the include
        // options, the keys and the card may all have changed since selection.
        for(int block : selected)
        {
            QString why;
            if(!plan.blockAllowed(block, &why))
            {
                QMessageBox::information(&dialog, tr("Write plan"),
                                         tr("Block %1 cannot be written: %2").arg(block).arg(why));
                syncAll();
                return;
            }
        }
        if(confirmWrites
           && QMessageBox::warning(&dialog, tr("Write plan"),
                                   tr("Write %1 block(s) to the card? This cannot be undone.")
                                   .arg(selected.size()),
                                   QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;

        setBusy(true);
        cancelRequested = false;
        taskRunning = true;
        tracker.start(MfcRecovery::Method::KnownKeys, selected.size());
        setTaskRunning(true);
        int verified = 0;
        int failed = 0;
        int unverified = 0;
        QStringList failures;
        for(int block : selected)
        {
            if(cancelRequested)
                break;
            if(!plan.matches(doc, targetSession) || !documentMatchesTarget())
            {
                logLine(tr("The card changed; the write plan was stopped"));
                break;
            }
            QString why;
            if(!plan.blockAllowed(block, &why))
            {
                plan.setOutcome(block, Mfc::WriteOutcome::Blocked, why);
                syncAll();
                continue;
            }
            const int row = plan.indexOfBlock(block);
            if(row >= 0)
                table->item(row, 6)->setText(tr("writing..."));
            QApplication::processEvents();
            const QString hex = doc.blockHex(block);
            Mfc::TrailerPlan trailerPlan;
            const Mfc::TrailerPlan* trailer = nullptr;
            if(doc.spec().isTrailerBlock(block))
            {
                trailerPlan = plan.trailerPlan(block);
                trailer = &trailerPlan;
            }
            QString detail;
            QString readBack;
            const Mfc::WriteOutcome outcome = writeBlockOnce(block, hex, trailer, &detail, &readBack);
            plan.setOutcome(block, outcome, detail);
            plan.setReadBack(block, readBack);
            if(outcome == Mfc::WriteOutcome::Verified)
            {
                doc.setBlock(block, hex, Mfc::BlockState::Verified, targetSession);
                verified++;
            }
            else if(outcome == Mfc::WriteOutcome::WrittenUnverified)
            {
                // Keep the payload as modified: the card may or may not carry
                // it, and it must not be retried automatically.
                doc.setBlock(block, hex, Mfc::BlockState::Modified, targetSession);
                unverified++;
                failures << tr("%1: %2").arg(block).arg(detail);
            }
            else
            {
                doc.setBlock(block, hex, Mfc::BlockState::Failed, targetSession);
                failed++;
                failures << tr("%1: %2").arg(block).arg(detail);
            }
            syncAll();
            refreshSummary();
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        taskRunning = false;
        setTaskRunning(false);
        setBusy(false);
        refreshDataTable();
        refreshKeyTable();
        refreshSummary();
        applyToMifare();
        QString message = tr("Write plan: %1 verified, %2 failed, %3 need re-checking")
                          .arg(verified).arg(failed).arg(unverified);
        if(cancelRequested)
            message += tr(" (stopped after the current block)");
        logLine(message);
        for(const QString& failure : failures)
            logLine(QStringLiteral("  ") + failure);
        setStatus(message);
        plan.clearPreview();
        syncAll();
        if(!failures.isEmpty())
            QMessageBox::information(&dialog, tr("Write plan"),
                                     message + QStringLiteral("\n\n") + failures.join(QStringLiteral("\n")));
    });
    connect(closeButton, &QPushButton::clicked, &dialog, [&]() {
        if(planBusy)
        {
            cancelRequested = true;
            return;
        }
        dialog.accept();
    });

    dialog.exec();
    planBusy = false;
    cancelRequested = false;
    refreshSummary();
}

void MfcWorkbench::saveKeyFile()
{
    const QString defaultName = QStringLiteral("key-%1.bin")
                                .arg(doc.uid.isEmpty() ? QStringLiteral("mfc") : doc.uid);
    QString selected;
    const QString filename = QFileDialog::getSaveFileName(
                                 this, tr("Save MIFARE Classic keys"), defaultName,
                                 tr("RRG client key.bin (*.bin);;Legacy GUI key.bin (*.bin);;"
                                    "Text key list (*.txt);;JSON (*.json)"), &selected);
    if(filename.isEmpty())
        return;

    QString layout = "rrg";
    if(selected.contains("Legacy"))
        layout = "legacy";
    else if(selected.contains("Text") || filename.endsWith(".txt", Qt::CaseInsensitive))
        layout = "text";
    else if(selected.contains("JSON") || filename.endsWith(".json", Qt::CaseInsensitive))
        layout = "json";

    const Mfc::IoResult result = doc.saveKeyFile(filename, layout, true);
    if(!result.ok)
    {
        QMessageBox::information(this, tr("Info"), result.error);
        return;
    }
    if(!result.warning.isEmpty())
        logLine(result.warning);
    logLine(tr("Saved keys to %1 (%2 layout)").arg(filename, layout));
    setStatus(tr("Keys saved"));
}

bool MfcWorkbench::importKeyFile(const QString& filename, const QString& layout, QString* error)
{
    Mfc::Document loaded;
    const Mfc::IoResult result = Mfc::Document::loadKeyFile(filename, layout, &loaded);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    if(loaded.cardType() == doc.cardType())
    {
        for(int s = 0; s < loaded.sectorCount(); s++)
            for(char type : {'A', 'B'})
                if(loaded.key(s, type).hasValue())
                    doc.setKeyEntry(s, type, loaded.key(s, type));
    }
    else
    {
        doc = loaded;
        nonce = MfcRecovery::NonceType::Unknown;
        updatingTables = true;
        cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(doc.cardType())));
        updatingTables = false;
        emit documentTypeChanged(typeToInt(doc.cardType()));
    }
    if(!result.warning.isEmpty())
        logLine(result.warning);
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    applyToMifare();
    return true;
}

void MfcWorkbench::loadKeyFile()
{
    const QString filename = QFileDialog::getOpenFileName(
                                 this, tr("Load MIFARE Classic keys"), QString(),
                                 tr("Key files (*.bin *.txt *.json);;All files (*)"));
    if(filename.isEmpty())
        return;
    QString error;
    if(importKeyFile(filename, "auto", &error))
    {
        setStatus(tr("Loaded keys from %1").arg(filename));
        return;
    }
    if(!mifare->lastIoAmbiguous() && !error.contains("ambiguous"))
    {
        QMessageBox::information(this, tr("Info"), error);
        return;
    }
    QMessageBox box(this);
    box.setWindowTitle(tr("Key file layout"));
    box.setText(tr("This binary key file has two possible layouts. Which one is it?"));
    box.setInformativeText(tr("RRG/official client: all key A first, then all key B.\n"
                              "Legacy GUI: A0 B0 A1 B1 ..."));
    QPushButton* rrg = box.addButton(tr("RRG key.bin"), QMessageBox::AcceptRole);
    QPushButton* legacy = box.addButton(tr("Legacy GUI"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if(box.clickedButton() == rrg)
        importKeyFile(filename, "rrg", &error);
    else if(box.clickedButton() == legacy)
        importKeyFile(filename, "legacy", &error);
    else
        return;
    if(!error.isEmpty())
        QMessageBox::information(this, tr("Info"), error);
    else
        setStatus(tr("Loaded keys from %1").arg(filename));
}

bool MfcWorkbench::queryCardIdentity(QString* uidOut, Mfc::CardType* typeOut, QString* error)
{
    if(!deviceReady)
    {
        if(error != nullptr)
            *error = tr("Connect the Proxmark3 first");
        return false;
    }
    const QVariantMap info = configMap.value("info").toMap();
    QString cmd = info.value("basic cmd").toString();
    if(cmd.isEmpty())
        cmd = info.value("cmd").toString();
    if(cmd.isEmpty())
        cmd = QStringLiteral("hf 14a info");
    Util::ReturnTrigger trigger(4000);
    trigger.hardTimeout = 8000;
    Util::ExecResult result = util->execCMDWithResult(cmd, trigger, true);
    appendLog(Util::stripAnsi(result.output));
    if(!result.isOk() && result.status != Util::EXEC_TIMEOUT)
    {
        if(error != nullptr)
            *error = tr("Card information failed: the client is not available");
        return false;
    }

    const QString clean = Util::stripAnsi(result.output);
    QRegularExpression uidRe(QStringLiteral("UID\\s*:?\\s*((?:[0-9A-Fa-f]{2}[ ]*){4,10})"));
    QRegularExpressionMatch uidMatch = uidRe.match(clean);
    QString uid;
    if(uidMatch.hasMatch())
    {
        uid = uidMatch.captured(1);
        uid.remove(QRegularExpression(QStringLiteral("\\s")));
        uid = uid.toUpper();
    }
    QRegularExpression typeRe(QStringLiteral("MIFARE\\s+Classic\\s+(Mini|1K|2K|4K)"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch typeMatch = typeRe.match(clean);
    if(!typeMatch.hasMatch())
        typeMatch = QRegularExpression(QStringLiteral("MIFARE\\s+(Mini)\\b"),
                                       QRegularExpression::CaseInsensitiveOption).match(clean);
    Mfc::CardType type = doc.cardType();
    if(typeMatch.hasMatch())
        Mfc::Spec::fromName(typeMatch.captured(1).toLower(), &type);

    if(uid.isEmpty())
    {
        if(error != nullptr)
            *error = tr("No card answered; the document was not changed");
        return false;
    }
    if(uidOut != nullptr)
        *uidOut = uid;
    if(typeOut != nullptr)
        *typeOut = type;
    return true;
}

void MfcWorkbench::confirmOneShotSession()
{
    if(!oneShotSessionPending)
        return;
    oneShotSessionPending = false;
    if(!deviceReady)
    {
        cardConfirmPending = false;
        invalidateTargetWhenDisconnected();
        refreshSummary();
        return;
    }

    QString uid;
    Mfc::CardType type = doc.cardType();
    QString error;
    if(!queryCardIdentity(&uid, &type, &error))
    {
        cardConfirmPending = false;
        invalidateTargetWhenDisconnected();
        setStatus(tr("Client reconnected; the card could not be confirmed. Identify it before reading."));
        refreshSummary();
        return;
    }

    const bool sameCard = !doc.uid.isEmpty()
                          && doc.uid.compare(uid, Qt::CaseInsensitive) == 0
                          && doc.cardType() == type;
    if(sameCard)
    {
        // Keep targetSession: the recovered keys were authenticated against
        // this card in the one-shot client and remain valid.
        targetPresent = true;
        targetUidValue = uid;
        targetType = type;
        setStatus(tr("Client reconnected; same card %1 confirmed. Recovered keys stay valid.").arg(uid));
    }
    else
    {
        // The card changed while the one-shot client ran: no old evidence may
        // be reused.  Keep the document, but require Identify / a new document.
        invalidateTargetWhenDisconnected();
        targetPresent = true;
        targetUidValue = uid;
        targetType = type;
        setStatus(tr("Client reconnected; card %1 is not the document card. Identify or start a new document.").arg(uid));
    }
    cardConfirmPending = false;
    refreshIdentity();
    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    emit statusMessage(statusLabel->text());
}

void MfcWorkbench::readCardInfo()
{
    if(util->isChannelOwned())
    {
        setStatus(tr("The client is busy; wait for it to finish"));
        return;
    }
    QString uid;
    Mfc::CardType type = doc.cardType();
    QString error;
    if(!queryCardIdentity(&uid, &type, &error))
    {
        setStatus(error);
        refreshSummary();
        return;
    }

    if(!applyCardIdentity(uid, type, &error))
    {
        // The document belongs to another card.  It is kept; the user decides
        // whether to start a new one (and is offered a backup first).
        QMessageBox box(this);
        box.setWindowTitle(tr("Card changed"));
        box.setText(error);
        box.setInformativeText(tr("Keep the current document as a file before starting a new one? "
                                  "Keys and blocks that were read from the other card are never "
                                  "reused for this card."));
        QPushButton* save = box.addButton(tr("Save document, then start new"), QMessageBox::AcceptRole);
        QPushButton* start = box.addButton(tr("Start new document"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if(box.clickedButton() == save)
            saveBackup();
        if(box.clickedButton() == save || box.clickedButton() == start)
        {
            startNewDocumentForTarget(&error);
            setStatus(tr("Card %1 identified; new document").arg(uid));
        }
        else
        {
            setStatus(tr("Card %1 identified; the document still belongs to another card").arg(uid));
        }
        refreshSummary();
        return;
    }

    refreshKeyTable();
    refreshDataTable();
    refreshAdvice();
    refreshSummary();
    setStatus(tr("Card identified: %1").arg(uid));
}

void MfcWorkbench::diagnoseNonce()
{
    if(!deviceReady)
    {
        setStatus(tr("Connect the Proxmark3 first"));
        return;
    }
    MfcRecovery::Command command;
    command.text = QStringLiteral("hf mf info");
    command.failurePattern = QStringLiteral("No tag found|Can't select card|command execution time out|not available in this mode");
    command.hardTimeoutMs = 120000;
    startMethod(MfcRecovery::Method::Isen, command);
}

void MfcWorkbench::diagnoseNonceOnOpen()
{
    if(!deviceReady || taskRunning || cardConfirmPending)
        return;
    // Only an unknown nonce needs a device round trip; switching tabs after a
    // diagnosis keeps the result instead of re-running the command every time.
    if(nonce != MfcRecovery::NonceType::Unknown)
        return;
    diagnoseNonce();
}

Mfc::Document MfcWorkbench::snapshotDocument() const
{
    // A snapshot is what the card itself returned in this RF session: unread
    // blocks stay unknown and only keys authenticated now are carried over.
    Mfc::Document snapshot(doc.cardType());
    snapshot.uid = doc.uid;
    snapshot.createdAt = QDateTime::currentDateTime();
    for(int i = 0; i < doc.blockCount(); i++)
        if(doc.blockReadIn(i, targetSession))
            snapshot.setBlock(i, doc.blockHex(i), Mfc::BlockState::Read, targetSession);
    for(int sector = 0; sector < doc.sectorCount(); sector++)
        for(char type : {'A', 'B'})
            if(doc.keyVerifiedIn(sector, type, targetSession))
                snapshot.setKeyVerified(sector, type, doc.key(sector, type).value,
                                        Mfc::KeySource::Card, targetSession);
    return snapshot;
}

bool MfcWorkbench::loadDocumentFile(const QString& filename, Mfc::Document* out, QString* error) const
{
    const Mfc::IoResult result = Mfc::Document::loadDataFile(filename, QStringLiteral("auto"), out);
    if(!result.ok && error != nullptr)
        *error = result.error;
    return result.ok;
}

bool MfcWorkbench::loadCompareFromLibrary(bool sideA, QString* error)
{
    Mfc::Library library(libraryRoot);
    if(!library.load(error))
        return false;
    if(library.entries().isEmpty())
    {
        if(error != nullptr)
            *error = tr("The backup library is empty");
        return false;
    }
    QStringList labels;
    for(const Mfc::LibraryEntry& entry : library.entries())
        labels << tr("%1 - %2 - %3")
                  .arg(entry.alias.isEmpty() ? entry.fileName : entry.alias,
                       entry.uid.isEmpty() ? tr("no UID") : entry.uid,
                       entry.importedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Compare with library"),
                                                   tr("Backup:"), labels, 0, false, &ok);
    if(!ok)
        return false;
    const int index = labels.indexOf(selected);
    if(index < 0)
        return false;
    Mfc::Document loaded;
    if(!loadDocumentFile(library.entries().at(index).filePath, &loaded, error))
        return false;
    if(sideA)
    {
        compareA = loaded;
        hasCompareA = true;
        compareAEdit->setText(tr("Library: %1").arg(selected));
    }
    else
    {
        compareB = loaded;
        hasCompareB = true;
        compareBEdit->setText(tr("Library: %1").arg(selected));
    }
    return true;
}

bool MfcWorkbench::loadCompareFile(bool sideA, const QString& filename, QString* error)
{
    Mfc::Document loaded;
    const Mfc::IoResult result = Mfc::Document::loadDataFile(filename, "auto", &loaded);
    if(!result.ok)
    {
        if(error != nullptr)
            *error = result.error;
        return false;
    }
    if(sideA)
    {
        compareA = loaded;
        hasCompareA = true;
        compareAEdit->setText(filename);
    }
    else
    {
        compareB = loaded;
        hasCompareB = true;
        compareBEdit->setText(filename);
    }
    return true;
}

void MfcWorkbench::loadCompareA()
{
    const QString filename = QFileDialog::getOpenFileName(this, tr("Load dump A"), QString(),
                                                          tr("Dumps (*.bin *.dump *.eml *.json *.txt);;All files (*)"));
    if(filename.isEmpty())
        return;
    QString error;
    if(!loadCompareFile(true, filename, &error))
        QMessageBox::information(this, tr("Info"), error);
}

void MfcWorkbench::loadCompareB()
{
    const QString filename = QFileDialog::getOpenFileName(this, tr("Load dump B"), QString(),
                                                          tr("Dumps (*.bin *.dump *.eml *.json *.txt);;All files (*)"));
    if(filename.isEmpty())
        return;
    QString error;
    if(!loadCompareFile(false, filename, &error))
        QMessageBox::information(this, tr("Info"), error);
}

void MfcWorkbench::compareDocuments()
{
    if(!hasCompareA || !hasCompareB)
    {
        setStatus(tr("Load both dump A and dump B first"));
        return;
    }
    comparison = compareA.compare(compareB);
    refreshDiffTable();
    for(const QString& note : comparison.notes)
        logLine(note);
}

void MfcWorkbench::exportCompareReport()
{
    if(!hasCompareA || !hasCompareB)
    {
        setStatus(tr("Load and compare two dumps first"));
        return;
    }
    const QString filename = QFileDialog::getSaveFileName(this, tr("Export comparison report"),
                                                          "mfc-compare.md",
                                                          tr("Markdown (*.md);;CSV (*.csv);;Text (*.txt)"));
    if(filename.isEmpty())
        return;

    QByteArray report;
    if(filename.endsWith(".csv", Qt::CaseInsensitive))
    {
        report += "block,state,a,b,note\n";
        for(const Mfc::Diff& diff : comparison.blocks)
            report += QStringLiteral("%1,%2,%3,%4,\"%5\"\n").arg(diff.block)
                      .arg(Mfc::diffStateName(diff.state), diff.a, diff.b, diff.note).toUtf8();
    }
    else
    {
        report += "# MIFARE Classic comparison\n\n";
        report += QStringLiteral("- A: %1 (%2)\n").arg(compareAEdit->text(), compareA.cardDisplayName()).toUtf8();
        report += QStringLiteral("- B: %1 (%2)\n").arg(compareBEdit->text(), compareB.cardDisplayName()).toUtf8();
        report += QStringLiteral("- Same: %1, different: %2, unknown: %3, incomparable: %4\n")
                  .arg(comparison.same).arg(comparison.different)
                  .arg(comparison.unknown).arg(comparison.incomparable).toUtf8();
        for(const QString& note : comparison.notes)
            report += QStringLiteral("- %1\n").arg(note).toUtf8();
        report += "\n| Block | State | A | B | Note |\n|---|---|---|---|---|\n";
        for(const Mfc::Diff& diff : comparison.blocks)
            report += QStringLiteral("| %1 | %2 | `%3` | `%4` | %5 |\n")
                      .arg(diff.block).arg(Mfc::diffStateName(diff.state))
                      .arg(diff.a, diff.b, diff.note).toUtf8();
    }

    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || file.write(report) != report.size() || !file.commit())
    {
        QMessageBox::information(this, tr("Info"), tr("Failed to write %1").arg(filename));
        return;
    }
    setStatus(tr("Report saved to %1").arg(filename));
}


void MfcWorkbench::startBatchFromDialog()
{
    if(!deviceReady || taskRunning || planBusy || batchRunning || cardConfirmPending
       || util->isChannelOwned())
    {
        setStatus(cardConfirmPending ? tr("Confirming the card; retry in a moment")
                                     : util->isChannelOwned() ? tr("The client is busy; wait for it to finish")
                                                             : tr("Another device task is still running"));
        return;
    }
    if(!documentMatchesTarget() && !ensureCardIdentified())
        return;

    QDialog dialog(this);
    dialog.setObjectName("mfcBatchDialog");
    dialog.setWindowTitle(tr("Fill in missing keys"));
    dialog.resize(900, 460);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* hint = new QLabel(tr("Queue one (sector, Key A/B) at a time. A key is marked verified only "
                                 "after the card authenticates it; a recovered value that could not be "
                                 "checked stays a candidate. Cancelling keeps everything already verified."),
                              &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QGridLayout* controls = new QGridLayout();
    QComboBox* methodBoxLocal = new QComboBox(&dialog);
    methodBoxLocal->setObjectName("mfcBatchMethod");
    methodBoxLocal->addItem(tr("Verify candidates"), int(MfcRecovery::Method::KnownKeys));
    methodBoxLocal->addItem(tr("Dictionary (chk)"), int(MfcRecovery::Method::Dictionary));
    methodBoxLocal->addItem(tr("Automatic (autopwn)"), int(MfcRecovery::Method::AutoPwn));
    methodBoxLocal->addItem(tr("Nested"), int(MfcRecovery::Method::Nested));
    methodBoxLocal->addItem(tr("Static nested"), int(MfcRecovery::Method::StaticNested));
    methodBoxLocal->addItem(tr("Hardnested"), int(MfcRecovery::Method::Hardnested));
    methodBoxLocal->addItem(tr("Darkside"), int(MfcRecovery::Method::Darkside));
    methodBoxLocal->addItem(tr("SEN"), int(MfcRecovery::Method::Sen));
    QSpinBox* knownSectorLocal = new QSpinBox(&dialog);
    knownSectorLocal->setRange(0, doc.sectorCount() - 1);
    knownSectorLocal->setValue(qBound(0, knownSectorSpin->value(), doc.sectorCount() - 1));
    QComboBox* knownTypeLocal = new QComboBox(&dialog);
    knownTypeLocal->addItem("A", 'A');
    knownTypeLocal->addItem("B", 'B');
    QLineEdit* dictionaryLocal = new QLineEdit(&dialog);
    dictionaryLocal->setPlaceholderText(tr("Optional dictionary file"));
    controls->addWidget(new QLabel(tr("Method:"), &dialog), 0, 0);
    controls->addWidget(methodBoxLocal, 0, 1);
    controls->addWidget(new QLabel(tr("Starting key:"), &dialog), 0, 2);
    controls->addWidget(knownSectorLocal, 0, 3);
    controls->addWidget(knownTypeLocal, 0, 4);
    controls->addWidget(dictionaryLocal, 1, 1, 1, 4);
    layout->addLayout(controls);

    QTableWidget* table = new QTableWidget(&dialog);
    table->setObjectName("mfcBatchTable");
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(QStringList()
                                     << tr("Sector") << tr("Key") << tr("Value")
                                     << tr("State") << tr("Detail"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    QLabel* progressLocal = new QLabel(&dialog);
    progressLocal->setObjectName("mfcBatchProgress");
    layout->addWidget(progressLocal);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* start = new QPushButton(tr("Start"), &dialog);
    QPushButton* stop = new QPushButton(tr("Stop"), &dialog);
    QPushButton* retry = new QPushButton(tr("Retry missing only"), &dialog);
    QPushButton* readRest = new QPushButton(tr("Read remaining blocks"), &dialog);
    QPushButton* close = new QPushButton(tr("Close"), &dialog);
    start->setObjectName("mfcBatchStart");
    stop->setObjectName("mfcBatchStop");
    retry->setObjectName("mfcBatchRetry");
    readRest->setObjectName("mfcBatchReadRest");
    buttons->addWidget(start);
    buttons->addWidget(stop);
    buttons->addWidget(retry);
    buttons->addWidget(readRest);
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    auto applyPlan = [&]() {
        batchPlan.build(doc, targetSession);
        table->setRowCount(batchPlan.size());
        for(int row = 0; row < batchPlan.size(); row++)
        {
            const MfcBatch::Item& item = batchPlan.items().at(row);
            table->setItem(row, 0, readOnlyItem(QString::number(item.sector)));
            table->setItem(row, 1, readOnlyItem(QString(QChar(item.type))));
            table->setItem(row, 2, readOnlyItem(item.key));
            table->setItem(row, 3, readOnlyItem(MfcBatch::itemStateName(item.state)));
            table->setItem(row, 4, readOnlyItem(item.detail));
        }
        const MfcBatch::Progress progressValue = batchPlan.progress();
        progressLocal->setText(tr("Total %1 | processed %2 | verified %3 | missing %4 | failed %5")
                               .arg(progressValue.total).arg(progressValue.processed)
                               .arg(progressValue.verified).arg(progressValue.missing)
                               .arg(progressValue.failed));
    };
    auto syncPlan = [&]() {
        if(table->rowCount() != batchPlan.size())
        {
            applyPlan();
            return;
        }
        for(int row = 0; row < batchPlan.size(); row++)
        {
            const MfcBatch::Item& item = batchPlan.items().at(row);
            table->item(row, 2)->setText(item.key);
            table->item(row, 3)->setText(MfcBatch::itemStateName(item.state));
            table->item(row, 4)->setText(item.detail);
            table->item(row, 3)->setForeground(item.state == MfcBatch::ItemState::Verified
                                               ? QColor(0, 150, 90)
                                               : item.state == MfcBatch::ItemState::Failed
                                               ? QColor(190, 40, 40) : QColor(120, 120, 120));
        }
        const MfcBatch::Progress progressValue = batchPlan.progress();
        progressLocal->setText(tr("Total %1 | processed %2 | verified %3 | missing %4 | failed %5")
                               .arg(progressValue.total).arg(progressValue.processed)
                               .arg(progressValue.verified).arg(progressValue.missing)
                               .arg(progressValue.failed));
    };
    applyPlan();

    QTimer updateTimer;
    updateTimer.setInterval(150);
    connect(&updateTimer, &QTimer::timeout, &dialog, syncPlan);
    updateTimer.start();

    auto setBusy = [&](bool busy) {
        start->setEnabled(!busy);
        stop->setEnabled(busy);
        retry->setEnabled(!busy);
        readRest->setEnabled(!busy);
        close->setEnabled(!busy);
        methodBoxLocal->setEnabled(!busy);
        knownSectorLocal->setEnabled(!busy);
        knownTypeLocal->setEnabled(!busy);
        dictionaryLocal->setEnabled(!busy);
    };
    connect(start, &QPushButton::clicked, &dialog, [&]() {
        const MfcRecovery::Method method = MfcRecovery::Method(methodBoxLocal->currentData().toInt());
        batchKnownSector = knownSectorLocal->value();
        batchKnownType = knownTypeLocal->currentData().toChar().toLatin1();
        batchDictionary = dictionaryLocal->text().trimmed();
        QString error;
        if(!startBatchRecovery(method, &error))
        {
            QMessageBox::information(&dialog, tr("Batch recovery"), error);
            return;
        }
        setBusy(true);
        syncPlan();
    });
    connect(stop, &QPushButton::clicked, &dialog, [&]() {
        cancelRequested = true;
        if(util->isCommandActive())
            util->cancelCommand(true);
        setStatus(tr("Stopping the batch after the current item..."));
    });
    connect(table, &QTableWidget::cellDoubleClicked, &dialog, [&, this](int row, int) {
        if(row < 0 || row >= batchPlan.size())
            return;
        const MfcBatch::Item item = batchPlan.items().at(row);
        locateBlock(doc.spec().firstBlockOfSector(item.sector));
    });
    connect(retry, &QPushButton::clicked, &dialog, [&]() {
        retryMissingKeys();
        if(batchRunning)
            setBusy(true);
        syncPlan();
    });
    connect(readRest, &QPushButton::clicked, &dialog, [&]() {
        readRemainingBlocks();
    });
    connect(close, &QPushButton::clicked, &dialog, [&]() {
        if(batchRunning)
        {
            cancelRequested = true;
            if(util->isCommandActive())
                util->cancelCommand(true);
            return;
        }
        dialog.accept();
    });
    connect(this, &MfcWorkbench::taskStateChanged, &dialog, [&](bool running) {
        if(!running && batchRunning)
            setBusy(true);
        else if(!running)
            setBusy(false);
    });
    dialog.exec();
    if(batchRunning)
    {
        cancelRequested = true;
        if(util->isCommandActive())
            util->cancelCommand(true);
    }
    refresh();
}

bool MfcWorkbench::startBatchRecovery(MfcRecovery::Method method, QString* error)
{
    if(!deviceReady || taskRunning || planBusy || batchRunning || cardConfirmPending
       || util->isChannelOwned())
    {
        if(error != nullptr)
            *error = util->isChannelOwned() ? tr("The client is busy; wait for it to finish")
                                             : tr("Another device task is still running");
        return false;
    }
    if(!documentMatchesTarget() && !ensureCardIdentified())
    {
        if(error != nullptr)
            *error = tr("Identify the card first");
        return false;
    }
    batchMethod = method;
    batchPlan.build(doc, targetSession);
    if(batchPlan.isEmpty())
    {
        if(error != nullptr)
            *error = tr("Every key is already verified in this session");
        return false;
    }
    batchRunning = true;
    batchAwaiting = false;
    batchTableDone = false;
    batchCurrent = -1;
    batchDocumentIndex = activeIndex;
    taskDocumentIndex = activeIndex;
    cancelRequested = false;
    tracker.start(method, batchPlan.size());
    setTaskRunning(true);
    setStatus(tr("Batch: %1 item(s) queued").arg(batchPlan.size()));
    batchNext();
    return true;
}

void MfcWorkbench::batchNext()
{
    if(!batchRunning)
        return;
    if(!cancelRequested && (!deviceReady || cardConfirmPending || util->isChannelOwned()))
    {
        // A one-shot recovery stopped the persistent client to free the port.
        // Wait for the reconnect and the UID confirmation instead of treating a
        // refused dispatch as a running command (which would wedge the queue).
        QTimer::singleShot(300, this, &MfcWorkbench::batchNext);
        return;
    }
    if(batchRunning && !documentMatchesTarget() && !Util::isDemoMode())
    {
        cancelRequested = true;
        setStatus(tr("The card changed; the batch stopped after the current item"));
    }
    if(cancelRequested)
    {
        batchPlan.invalidateRunning(tr("cancelled"));
        const MfcBatch::Progress progressValue = batchPlan.progress();
        batchRunning = false;
        batchAwaiting = false;
        batchCurrent = -1;
        tracker.finish(MfcRecovery::TaskState::Cancelled, tr("Batch cancelled"));
        setTaskRunning(false);
        markDirty();
        refreshKeyTable();
        refreshSummary();
        batchLabel->setText(tr("Batch cancelled: %1 verified, %2 missing")
                            .arg(progressValue.verified).arg(progressValue.total - progressValue.processed));
        setStatus(batchLabel->text());
        return;
    }

    // Commands that recover the whole key table in one run.
    if(batchMethod == MfcRecovery::Method::Dictionary || batchMethod == MfcRecovery::Method::AutoPwn
       || batchMethod == MfcRecovery::Method::Sen || batchMethod == MfcRecovery::Method::Isen)
    {
        if(batchTableDone)
        {
            const MfcBatch::Progress progressValue = batchPlan.progress();
            batchRunning = false;
            batchAwaiting = false;
            batchCurrent = -1;
            tracker.finish(progressValue.failed > 0 ? MfcRecovery::TaskState::Partial
                                                    : MfcRecovery::TaskState::Completed,
                           tr("Batch finished"));
            setTaskRunning(false);
            markDirty();
            refreshKeyTable();
            refreshSummary();
            batchLabel->setText(tr("Batch finished: %1 verified, %2 failed, %3 missing")
                                .arg(progressValue.verified).arg(progressValue.failed)
                                .arg(progressValue.total - progressValue.processed));
            setStatus(batchLabel->text());
            return;
        }
        batchTableDone = true;
        batchCurrent = -2;
        knownSectorSpin->setValue(batchKnownSector);
        knownTypeBox->setCurrentIndex(knownTypeBox->findData(batchKnownType));
        if(!batchDictionary.isEmpty())
            dictionaryEdit->setText(batchDictionary);
        QString error;
        const MfcRecovery::Command command = commandForMethod(batchMethod, &error);
        if(!error.isEmpty() || command.text.trimmed().isEmpty())
        {
            batchPlan.setResult(0, MfcBatch::ItemState::Skipped, error);
            QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
            return;
        }
        startMethod(batchMethod, command, true);
        batchAwaiting = true;
        return;
    }

    const int index = batchPlan.next();
    if(index < 0)
    {
        const MfcBatch::Progress progressValue = batchPlan.progress();
        batchRunning = false;
        batchAwaiting = false;
        batchCurrent = -1;
        tracker.finish(progressValue.failed > 0 ? MfcRecovery::TaskState::Partial
                                                : MfcRecovery::TaskState::Completed,
                       tr("Batch finished"));
        setTaskRunning(false);
        markDirty();
        refreshKeyTable();
        refreshSummary();
        batchLabel->setText(tr("Batch finished: %1 verified, %2 failed, %3 missing")
                            .arg(progressValue.verified).arg(progressValue.failed)
                            .arg(progressValue.total - progressValue.processed));
        setStatus(batchLabel->text());
        return;
    }
    batchCurrent = index;
    const MfcBatch::Item item = batchPlan.items().at(index);
    batchPlan.setResult(index, MfcBatch::ItemState::Running);
    refreshSummary();

    if(batchMethod == MfcRecovery::Method::KnownKeys)
    {
        if(item.key.isEmpty())
        {
            batchPlan.setResult(index, MfcBatch::ItemState::Skipped,
                                tr("no candidate key: run a recovery method"));
        }
        else
        {
            const bool ok = verifyOneKey(item.sector, item.type, item.key);
            batchPlan.setResult(index, ok ? MfcBatch::ItemState::Verified : MfcBatch::ItemState::Failed,
                                ok ? tr("authenticated against the card") : tr("authentication failed"));
            tabs[activeIndex].lastOperation[doc.spec().trailerBlockOfSector(item.sector)] =
                ok ? tr("key %1 verified").arg(QChar(item.type))
                   : tr("key %1 failed authentication").arg(QChar(item.type));
        }
        QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
        return;
    }

    // Per-target methods need a verified starting key.  Prefer the manually
    // selected one, otherwise any verified key from another sector.
    int knownSector = batchKnownSector;
    char knownType = batchKnownType;
    QString knownKey;
    if(knownSector >= 0 && knownSector < doc.sectorCount()
       && doc.key(knownSector, knownType).verifiedIn(targetSession)
       && doc.key(knownSector, knownType).hasValue())
        knownKey = doc.key(knownSector, knownType).value;
    if(knownKey.isEmpty())
    {
        for(int sector = 0; sector < doc.sectorCount() && knownKey.isEmpty(); sector++)
        {
            if(sector == item.sector)
                continue;
            for(char type : {'A', 'B'})
                if(doc.key(sector, type).verifiedIn(targetSession) && doc.key(sector, type).hasValue())
                {
                    knownSector = sector;
                    knownType = type;
                    knownKey = doc.key(sector, type).value;
                    break;
                }
        }
    }
    if(knownKey.isEmpty())
    {
        batchPlan.setResult(index, MfcBatch::ItemState::Skipped,
                            tr("no verified starting key for this method"));
        QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
        return;
    }

    MfcRecovery::Command command;
    if(batchMethod == MfcRecovery::Method::Nested || batchMethod == MfcRecovery::Method::StaticNested)
        command = MfcRecovery::buildNested(configMap, doc.spec(),
                                           batchMethod == MfcRecovery::Method::StaticNested,
                                           knownSector, knownType, knownKey);
    else if(batchMethod == MfcRecovery::Method::Hardnested)
        command = MfcRecovery::buildHardnested(configMap, doc.spec(), knownSector, knownType,
                                               knownKey, item.sector, item.type);
    else if(batchMethod == MfcRecovery::Method::Darkside)
    {
        if(item.sector != 0 || item.type != 'A')
        {
            batchPlan.setResult(index, MfcBatch::ItemState::Skipped,
                                tr("darkside only recovers sector 0 Key A"));
            QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
            return;
        }
        command = MfcRecovery::buildDarkside(configMap);
        command.targetSector = item.sector;
        command.targetType = item.type;
    }
    if(command.text.trimmed().isEmpty())
    {
        batchPlan.setResult(index, MfcBatch::ItemState::Skipped,
                            tr("the client profile has no command for this method"));
        QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
        return;
    }
    currentTarget.sector = item.sector;
    currentTarget.type = item.type;
    startMethod(batchMethod, command, true);
    batchAwaiting = true;
}

void MfcWorkbench::batchAfterCommand(Util::ExecStatus status)
{
    if(!batchRunning)
        return;
    batchAwaiting = false;
    if(batchCurrent >= 0)
    {
        const MfcBatch::Item item = batchPlan.items().at(batchCurrent);
        const Mfc::KeyEntry entry = doc.key(item.sector, item.type);
        if(status == Util::EXEC_OK && entry.hasValue())
        {
            // A one-shot recovery stopped the persistent client; the reconnect
            // is asynchronous, so the recovered value stays a candidate until
            // it can be authenticated.  A persistent command is verified now.
            const bool oneShot = (batchMethod == MfcRecovery::Method::Nested
                                  || batchMethod == MfcRecovery::Method::StaticNested
                                  || batchMethod == MfcRecovery::Method::Hardnested
                                  || batchMethod == MfcRecovery::Method::Darkside);
            if(oneShot)
                batchPlan.setResult(batchCurrent, MfcBatch::ItemState::Candidate,
                                    tr("recovered; verify after the reconnect"));
            else
            {
                const bool ok = verifyOneKey(item.sector, item.type, entry.value);
                batchPlan.setResult(batchCurrent,
                                    ok ? MfcBatch::ItemState::Verified : MfcBatch::ItemState::Candidate,
                                    ok ? tr("authenticated against the card")
                                       : tr("recovered value did not authenticate"));
            }
        }
        else
            batchPlan.setResult(batchCurrent, MfcBatch::ItemState::Failed, tr("no key recovered"));
    }
    else
    {
        // Whole-table method: take the evidence from the document.
        for(int i = 0; i < batchPlan.size(); i++)
        {
            const MfcBatch::Item item = batchPlan.items().at(i);
            const Mfc::KeyEntry entry = doc.key(item.sector, item.type);
            if(entry.verifiedIn(targetSession))
                batchPlan.setResult(i, MfcBatch::ItemState::Verified, tr("authenticated against the card"));
            else if(entry.hasValue())
                batchPlan.setResult(i, MfcBatch::ItemState::Candidate, tr("candidate from the command output"));
            else
                batchPlan.setResult(i, MfcBatch::ItemState::Failed, tr("not recovered"));
        }
    }
    batchCurrent = -1;
    QTimer::singleShot(0, this, &MfcWorkbench::batchNext);
}

void MfcWorkbench::retryMissingKeys()
{
    if(batchRunning)
        return;
    QString error;
    if(!startBatchRecovery(batchMethod, &error))
        setStatus(error);
}

void MfcWorkbench::readRemainingBlocks()
{
    readUnknownBlocks();
}

void MfcWorkbench::refreshScanList()
{
    if(scanTable == nullptr)
        return;
    QTableWidgetItem* current = scanTable->currentItem();
    const int selected = current != nullptr ? current->data(Qt::UserRole).toInt() : -1;
    scanTable->setRowCount(0);
    for(int index = 0; index < tabs.size(); index++)
    {
        if(tabs.at(index).sourceKind != QStringLiteral("scan"))
            continue;
        const int row = scanTable->rowCount();
        scanTable->insertRow(row);
        QTableWidgetItem* roundItem = readOnlyItem(QString::number(tabs.at(index).round));
        roundItem->setData(Qt::UserRole, index);
        scanTable->setItem(row, 0, roundItem);
        const Mfc::Document& document = tabs.at(index).document;
        scanTable->setItem(row, 1, readOnlyItem(document.uid));
        scanTable->setItem(row, 2, readOnlyItem(document.cardDisplayName()));
        scanTable->setItem(row, 3, readOnlyItem(QStringLiteral("%1/%2")
                                                .arg(document.completeBlockCount())
                                                .arg(document.blockCount())));
        scanTable->setItem(row, 4, readOnlyItem(QString::number(document.verifiedKeyCount())));
        scanTable->setItem(row, 5, readOnlyItem(tabs.at(index).name));
        if(index == selected)
            scanTable->selectRow(row);
    }
}

void MfcWorkbench::scanToggled(bool on)
{
    scanEnabled = on;
    scanButton->setText(on ? tr("Stop continuous scan") : tr("Start continuous scan"));
    if(on)
    {
        scanTimer->start();
        setStatus(tr("Continuous scan: present a card"));
        scanTick();
    }
    else
    {
        scanTimer->stop();
        scanCardPresent = false;
        scanUid.clear();
        setStatus(tr("Continuous scan stopped"));
    }
}

void MfcWorkbench::scanTick()
{
    if(!scanEnabled || !deviceReady || util->isChannelOwned() || taskRunning || planBusy
       || cardConfirmPending)
        return;
    QString uid;
    Mfc::CardType type = doc.cardType();
    QString error;
    if(!queryCardIdentity(&uid, &type, &error))
    {
        if(scanCardPresent)
        {
            scanCardPresent = false;
            scanUid.clear();
            setStatus(tr("Scan: card removed; present it again for a new round"));
        }
        return;
    }
    if(scanCardPresent && scanUid.compare(uid, Qt::CaseInsensitive) == 0)
    {
        // The card stayed in place: keep the same round and document.
        refreshScanList();
        return;
    }
    scanCardPresent = true;
    scanUid = uid;
    scanRound++;
    Mfc::Document scanned(type);
    scanned.uid = uid;
    targetPresent = true;
    targetUidValue = uid;
    targetType = type;
    const int index = addDocument(scanned, tr("Scan %1 #%2").arg(uid).arg(scanRound),
                                  QStringLiteral("scan"));
    tabs[index].round = scanRound;
    scanDocumentIndex = index;
    refreshScanList();
    setStatus(tr("Scan: %1 (%2)").arg(uid, Mfc::Spec::get(type).displayName()));
    if(scanAutoRead)
    {
        if(Util::isDemoMode())
        {
            // The demonstration card carries its keys: read through the normal
            // workbench path so the result is a normal document.
            doc = Demo::Provider::instance().card();
            doc.uid = uid;
            tabs[activeIndex].document = doc;
        }
        if(documentMatchesTarget())
            readUnknownBlocks();
    }
}

void MfcWorkbench::scanItemChanged()
{
    const QModelIndexList rows = scanTable->selectionModel() ? scanTable->selectionModel()->selectedRows() : QModelIndexList();
    if(rows.size() != 1)
        return;
    const int index = scanTable->item(rows.first().row(), 0)->data(Qt::UserRole).toInt();
    if(index >= 0 && index < tabs.size() && index != activeIndex)
    {
        saveActiveView();
        activeIndex = index;
        loadActiveDocument();
        refreshDocumentTabs();
    }
}

void MfcWorkbench::setTaskRunning(bool running)
{
    taskRunning = running;
    runButton->setEnabled(deviceReady && !running);
    cancelButton->setEnabled(running);
    progress->setRange(0, running ? 0 : 100);
    progress->setValue(0);
    if(!running)
        stageLabel->setText(tr("Idle"));
    emit taskStateChanged(running);
}

void MfcWorkbench::logLine(const QString& text)
{
    if(text.trimmed().isEmpty())
        return;
    logEdit->appendPlainText(text.trimmed());
    logEdit->verticalScrollBar()->setValue(logEdit->verticalScrollBar()->maximum());
}

void MfcWorkbench::appendLog(const QString& text)
{
    if(text.isEmpty())
        return;
    logEdit->moveCursor(QTextCursor::End);
    logEdit->insertPlainText(text);
    logEdit->moveCursor(QTextCursor::End);
}

void MfcWorkbench::setStatus(const QString& text)
{
    statusLabel->setText(text);
    emit statusMessage(text);
}

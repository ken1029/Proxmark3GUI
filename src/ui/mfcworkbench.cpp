#include "ui/mfcworkbench.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QColor>
#include <QHBoxLayout>
#include <QPair>
#include <QTextCursor>
#include <QCheckBox>
#include <QComboBox>
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
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "common/pm3cmd.h"
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

const char* METHOD_KEYS[] =
{
    "known", "check", "autopwn", "nested", "staticnested",
    "hardnested", "darkside", "sen", "isen"
};

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

    importFromMifare();
}

void MfcWorkbench::buildUi()
{
    QVBoxLayout* root = new QVBoxLayout(this);

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
    progress = new QProgressBar(summaryBox);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(true);
    summary->addWidget(keySummary, 2, 0, 1, 4);
    summary->addWidget(blockSummary, 2, 4, 1, 3);
    summary->addWidget(stageLabel, 3, 0, 1, 4);
    summary->addWidget(statusLabel, 3, 4, 1, 3);
    summary->addWidget(progress, 4, 0, 1, 7);
    root->addWidget(summaryBox);

    QTabWidget* tabs = new QTabWidget(this);

    // --- keys ---
    QWidget* keyPage = new QWidget(tabs);
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
    keyButtons->addWidget(loadKeyButton);
    keyButtons->addWidget(saveKeyButton);
    keyButtons->addWidget(verifyButton);
    keyButtons->addStretch(1);
    keyLayout->addLayout(keyButtons);
    tabs->addTab(keyPage, tr("Keys && access"));

    // --- data ---
    QWidget* dataPage = new QWidget(tabs);
    QVBoxLayout* dataLayout = new QVBoxLayout(dataPage);
    QHBoxLayout* dataFilter = new QHBoxLayout();
    dataFilter->addWidget(new QLabel(tr("Show:"), dataPage));
    filterBox = new QComboBox(dataPage);
    filterBox->addItem(tr("All blocks"), "all");
    filterBox->addItem(tr("Unknown / failed"), "unknown");
    filterBox->addItem(tr("Modified"), "modified");
    filterBox->addItem(tr("Readable"), "read");
    dataFilter->addWidget(filterBox);
    dataFilter->addStretch(1);
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
    dataTable->setColumnCount(5);
    dataTable->setHorizontalHeaderLabels(QStringList()
                                         << tr("Block") << tr("Sector") << tr("Type") << tr("Data (hex)") << tr("State"));
    dataTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    dataTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    dataTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    dataTable->setAlternatingRowColors(true);
    dataTable->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dataLayout->addWidget(dataTable);
    tabs->addTab(dataPage, tr("Data / backup"));

    // --- compare ---
    QWidget* comparePage = new QWidget(tabs);
    QVBoxLayout* compareLayout = new QVBoxLayout(comparePage);
    QGridLayout* paths = new QGridLayout();
    compareAEdit = new QLineEdit(comparePage);
    compareBEdit = new QLineEdit(comparePage);
    QPushButton* loadAButton = new QPushButton(tr("Load A..."), comparePage);
    QPushButton* loadBButton = new QPushButton(tr("Load B..."), comparePage);
    QPushButton* compareButton = new QPushButton(tr("Compare A / B"), comparePage);
    QPushButton* exportButton = new QPushButton(tr("Export report..."), comparePage);
    showDiffOnlyBox = new QCheckBox(tr("Only show differences"), comparePage);
    paths->addWidget(new QLabel(tr("A:"), comparePage), 0, 0);
    paths->addWidget(compareAEdit, 0, 1);
    paths->addWidget(loadAButton, 0, 2);
    paths->addWidget(new QLabel(tr("B:"), comparePage), 1, 0);
    paths->addWidget(compareBEdit, 1, 1);
    paths->addWidget(loadBButton, 1, 2);
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
    tabs->addTab(comparePage, tr("Compare"));

    // --- methods ---
    QWidget* methodPage = new QWidget(tabs);
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
    dictionaryEdit->setPlaceholderText(tr("Optional dictionary file (chk -f)"));
    includeDefaultsBox = new QCheckBox(tr("Include built-in default keys"), methodPage);
    includeDefaultsBox->setChecked(true);
    knownSectorSpin = new QSpinBox(methodPage);
    knownSectorSpin->setRange(0, doc.sectorCount() - 1);
    knownTypeBox = new QComboBox(methodPage);
    knownTypeBox->addItem("A", 'A');
    knownTypeBox->addItem("B", 'B');
    targetSectorSpin = new QSpinBox(methodPage);
    targetSectorSpin->setRange(0, doc.sectorCount() - 1);
    targetTypeBox = new QComboBox(methodPage);
    targetTypeBox->addItem("A", 'A');
    targetTypeBox->addItem("B", 'B');
    runButton = new QPushButton(tr("Run selected method"), methodPage);
    cancelButton = new QPushButton(tr("Cancel task"), methodPage);
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
    tabs->addTab(methodPage, tr("Key recovery / decryption"));

    root->addWidget(tabs, 1);

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
    connect(compareButton, &QPushButton::clicked, this, &MfcWorkbench::compareDocuments);
    connect(exportButton, &QPushButton::clicked, this, &MfcWorkbench::exportCompareReport);
    connect(showDiffOnlyBox, &QCheckBox::stateChanged, this, &MfcWorkbench::refreshDiffTable);
    connect(clearLogButton, &QPushButton::clicked, logEdit, &QPlainTextEdit::clear);
    connect(keyTable, &QTableWidget::itemChanged, this, &MfcWorkbench::onKeyItemChanged);
    connect(dataTable, &QTableWidget::itemChanged, this, &MfcWorkbench::onDataItemChanged);
}

void MfcWorkbench::setConfigMap(const QVariantMap& config)
{
    configMap = config;
    refreshAdvice();
}

void MfcWorkbench::setDeviceReady(bool ready)
{
    deviceReady = ready;
    refreshSummary();
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
    cardLabel->setText(doc.cardDisplayName() + (doc.uid.isEmpty() ? QString() : QStringLiteral(" / ") + doc.uid));
    uidLabel->setText(doc.uid.isEmpty() ? tr("(not read)") : doc.uid);
    nonceLabel->setText(nonceText());
    keySummary->setText(tr("Verified keys %1/%2 | pending %3 | unknown %4")
                        .arg(doc.verifiedKeyCount()).arg(doc.keyCount())
                        .arg(doc.pendingKeyCount()).arg(doc.unknownKeyCount()));
    blockSummary->setText(tr("Read blocks %1/%2 | unknown %3 | modified %4")
                          .arg(doc.completeBlockCount()).arg(doc.blockCount())
                          .arg(doc.unknownBlockCount()).arg(doc.modifiedBlockCount()));
    if(!taskRunning)
        stageLabel->setText(tr("Idle"));
    runButton->setEnabled(deviceReady && !taskRunning);
    cancelButton->setEnabled(taskRunning);
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
    const QString filter = filterBox->currentData().toString();
    QList<int> rows;
    for(int i = 0; i < doc.blockCount(); i++)
    {
        const Mfc::Block& block = doc.block(i);
        bool show = true;
        if(filter == "unknown")
            show = !block.complete() || block.state == Mfc::BlockState::Failed;
        else if(filter == "modified")
            show = block.state == Mfc::BlockState::Modified;
        else if(filter == "read")
            show = block.complete();
        if(show)
            rows.append(i);
    }
    dataTable->setRowCount(rows.size());
    for(int row = 0; row < rows.size(); row++)
    {
        const int i = rows.at(row);
        const Mfc::Block& block = doc.block(i);
        dataTable->setItem(row, 0, readOnlyItem(QString::number(i)));
        dataTable->setItem(row, 1, readOnlyItem(QString::number(doc.spec().sectorOfBlock(i))));
        dataTable->setItem(row, 2, readOnlyItem(doc.spec().isTrailerBlock(i) ? tr("trailer") : tr("data")));
        QTableWidgetItem* hexItem = new QTableWidgetItem(spacedHex(block.hex.isEmpty()
                                                                  ? QString(32, QLatin1Char('?'))
                                                                  : block.hex));
        hexItem->setData(Qt::UserRole, i);
        dataTable->setItem(row, 3, hexItem);
        dataTable->setItem(row, 4, readOnlyItem(blockStateText(block.state)));
    }
    updatingTables = false;
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
        adviceTable->setItem(i, 3, readOnlyItem(item.requires.join(QStringLiteral(", "))));
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

void MfcWorkbench::startMethod(MfcRecovery::Method method, const MfcRecovery::Command& command)
{
    if(!deviceReady)
    {
        setStatus(tr("Connect the Proxmark3 first"));
        return;
    }
    if(taskRunning)
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
    async.useCompletionMarker = true;
    async.hardTimeoutMs = command.hardTimeoutMs;
    async.label = MfcRecovery::methodName(method);

    taskOutput.clear();
    currentMethod = method;
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
    setStatus(tr("Running %1...").arg(MfcRecovery::methodName(method)));
}

void MfcWorkbench::onCommandOutput(const QString& output)
{
    if(!taskRunning)
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
    if(!taskRunning)
        return;
    taskOutput = Util::stripAnsi(output);
    const MfcRecovery::ParseResult parsed = MfcRecovery::parseOutput(taskOutput, doc.spec());
    applyParsedResult(parsed, currentMethod, result.status);

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
    MfcRecovery::MergeResult merged = MfcRecovery::merge(parsed, &doc, true, source);
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
                              "completed keys are kept. Abort client disconnects the client "
                              "and reconnects it, which is the only safe way to stop a long command."));
    QPushButton* stopWait = box.addButton(tr("Stop waiting"), QMessageBox::AcceptRole);
    QPushButton* abortClient = box.addButton(tr("Abort client"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if(box.clickedButton() == stopWait)
    {
        cancelRequested = true;
        util->cancelCommand(false);
        // The client may still be executing the command.  Do not let another
        // device task start until the user reconnects (Stop / Connect).
        deviceReady = false;
        refreshSummary();
        setStatus(tr("Stopped waiting; the client may still be executing. Reconnect before the next task."));
    }
    else if(box.clickedButton() == abortClient)
    {
        cancelRequested = true;
        util->cancelCommand(true);
    }
}

void MfcWorkbench::verifyKeys()
{
    if(!deviceReady || taskRunning)
        return;
    QList<QPair<int, char>> pending;
    for(int s = 0; s < doc.sectorCount(); s++)
    {
        for(char type : {'A', 'B'})
        {
            const Mfc::KeyEntry& entry = doc.key(s, type);
            if(entry.hasValue() && entry.state == Mfc::KeyState::Pending)
                pending.append(qMakePair(s, type));
        }
    }
    if(pending.isEmpty())
    {
        setStatus(tr("No candidate key to verify"));
        return;
    }

    taskRunning = true;
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
        doc.setKeyVerified(sector, type, key, Mfc::KeySource::Manual);
        return true;
    }
    doc.setKey(sector, type, key, Mfc::KeyState::Failed, Mfc::KeySource::Manual);
    logLine(tr("  failed: %1").arg(error));
    return false;
}

bool MfcWorkbench::chooseVerifiedKey(int sector, char* type, QString* key) const
{
    for(char candidate : {'A', 'B'})
    {
        const Mfc::KeyEntry& entry = doc.key(sector, candidate);
        if(entry.verified() && entry.hasValue())
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
    if(!deviceReady || taskRunning)
        return;
    taskRunning = true;
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
        if(!ok)
        {
            if(keyType == 'A')
            {
                const Mfc::KeyEntry& b = doc.key(sector, 'B');
                if(b.verified() && b.hasValue())
                    ok = readBlockOnce(block, 'B', b.value, &hex, &error);
            }
        }
        if(!ok)
        {
            failed++;
            if(!doc.blockComplete(block))
                doc.setBlockState(block, Mfc::BlockState::Failed);
            logLine(tr("Block %1: %2").arg(block).arg(error));
            continue;
        }

        if(doc.spec().isTrailerBlock(block))
        {
            // Trailer read with A: bytes 0..5 = key A, 6..9 = access bits,
            // 10..15 = key B (unreadable with A).  Keep the verified key and
            // never invent the other one.
            const QString a = doc.key(sector, 'A').value;
            const QString b = doc.key(sector, 'B').value;
            const QString acl = hex.mid(12, 8);
            const bool aclValid = Mifare::data_isACBitsValid(acl);
            QString merged = (Mfc::Document::isKeyText(a) ? a : QString(12, QLatin1Char('?')))
                             + (aclValid ? acl : QString(8, QLatin1Char('?')))
                             + (Mfc::Document::isKeyText(b) ? b : QString(12, QLatin1Char('?')));
            if(Mfc::Document::isBlockText(merged))
                doc.setBlock(block, merged, Mfc::BlockState::Read);
        }
        else
        {
            doc.setBlock(block, hex, Mfc::BlockState::Read);
        }
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

    const Mfc::IoResult result = doc.saveDataFile(filename, format, allowUnknown);
    if(!result.ok)
    {
        QMessageBox::information(this, tr("Info"), result.error);
        setStatus(result.error);
        return;
    }
    if(!result.warning.isEmpty())
        logLine(result.warning);
    const QString hash = doc.sha256();
    QFile sidecar(filename + ".sha256");
    if(sidecar.open(QIODevice::WriteOnly | QIODevice::Text))
        sidecar.write((hash + "  " + QFileInfo(filename).fileName() + "\n").toUtf8());
    logLine(tr("Saved %1 (sha256 %2)").arg(filename, hash));
    setStatus(tr("Backup saved; sha256 %1").arg(hash.left(16)));
}

bool MfcWorkbench::writeBlockOnce(int block, const QString& hex, QString* error)
{
    const int sector = doc.spec().sectorOfBlock(block);
    char keyType = 'A';
    QString key;
    if(!chooseVerifiedKey(sector, &keyType, &key))
    {
        if(error != nullptr)
            *error = tr("no verified key for sector %1").arg(sector);
        return false;
    }

    const QVariantMap config = configMap.value("normal write block").toMap();
    QString cmd = config.value("cmd").toString();
    if(cmd.isEmpty())
        cmd = QStringLiteral("hf mf wrbl --blk <block> -<key type> -k <key> -d <data> --force");
    const QVariantMap types = config.value("key type").toMap();
    const QString typeName = QString(QChar(keyType));
    cmd.replace(QStringLiteral("<block>"), QString::number(block));
    cmd.replace(QStringLiteral("<key type>"), types.value(typeName).toString().isEmpty()
                ? typeName.toLower() : types.value(typeName).toString());
    cmd.replace(QStringLiteral("<key>"), key);
    cmd.replace(QStringLiteral("<data>"), hex);
    Util::ExecResult result = util->execCMDWithResult(cmd, Pm3Cmd::makeWriteTrigger(config, 3000));
    if(!result.isOk())
    {
        if(error != nullptr)
            *error = tr("write failed or timed out");
        return false;
    }

    // Read back.  A trailer may mask the other key with '?'; that is not a
    // verified mismatch, but a readable different value is.
    QString readBack;
    QString readError;
    if(!readBlockOnce(block, keyType, key, &readBack, &readError))
    {
        if(error != nullptr)
            *error = tr("write ok, read-back failed: %1").arg(readError);
        return false;
    }
    if(readBack != hex && !readBack.contains(QLatin1Char('?')))
    {
        if(error != nullptr)
            *error = tr("read-back mismatch");
        return false;
    }
    doc.setBlock(block, hex, Mfc::BlockState::Verified);
    return true;
}

void MfcWorkbench::writePlan()
{
    if(!deviceReady || taskRunning)
    {
        setStatus(tr("Connect the Proxmark3 first"));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Write plan"));
    dialog.resize(900, 520);
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLabel* hint = new QLabel(tr("Only complete blocks are written. Block 0 and trailers are "
                                 "excluded unless you explicitly include them."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    QCheckBox* block0Box = new QCheckBox(tr("Include block 0 (UID/manufacturer)"), &dialog);
    QCheckBox* trailerBox = new QCheckBox(tr("Include sector trailers (keys / access bits)"), &dialog);
    layout->addWidget(block0Box);
    layout->addWidget(trailerBox);

    QTableWidget* table = new QTableWidget(&dialog);
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels(QStringList()
                                     << tr("Write") << tr("Block") << tr("Type")
                                     << tr("Document") << tr("Card") << tr("State"));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);

    QHBoxLayout* buttons = new QHBoxLayout();
    QPushButton* previewButton = new QPushButton(tr("Read card && preview"), &dialog);
    QPushButton* writeButton = new QPushButton(tr("Write selected"), &dialog);
    QPushButton* stopButton = new QPushButton(tr("Stop after current block"), &dialog);
    QPushButton* closeButton = new QPushButton(tr("Close"), &dialog);
    buttons->addWidget(previewButton);
    buttons->addWidget(writeButton);
    buttons->addWidget(stopButton);
    buttons->addStretch(1);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    QList<int> rows;
    for(int block = 0; block < doc.blockCount(); block++)
        if(doc.blockComplete(block))
            rows.append(block);

    auto applyDefaults = [&]() {
        for(int row = 0; row < table->rowCount(); row++)
        {
            const int block = table->item(row, 1)->data(Qt::UserRole).toInt();
            const bool isBlock0 = block == 0;
            const bool isTrailer = doc.spec().isTrailerBlock(block);
            const bool check = !isBlock0 && !isTrailer;
            table->item(row, 0)->setCheckState(check ? Qt::Checked : Qt::Unchecked);
        }
    };

    table->setRowCount(rows.size());
    for(int row = 0; row < rows.size(); row++)
    {
        const int block = rows.at(row);
        QTableWidgetItem* check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(Qt::Checked);
        table->setItem(row, 0, check);
        QTableWidgetItem* blockItem = readOnlyItem(QString::number(block));
        blockItem->setData(Qt::UserRole, block);
        table->setItem(row, 1, blockItem);
        table->setItem(row, 2, readOnlyItem(doc.spec().isTrailerBlock(block) ? tr("trailer") : tr("data")));
        table->setItem(row, 3, readOnlyItem(spacedHex(doc.blockHex(block))));
        table->setItem(row, 4, readOnlyItem(QString()));
        table->setItem(row, 5, readOnlyItem(tr("not checked")));
    }
    applyDefaults();
    connect(block0Box, &QCheckBox::toggled, &dialog, [&](bool on) {
        for(int row = 0; row < table->rowCount(); row++)
            if(table->item(row, 1)->data(Qt::UserRole).toInt() == 0)
                table->item(row, 0)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    });
    connect(trailerBox, &QCheckBox::toggled, &dialog, [&](bool on) {
        for(int row = 0; row < table->rowCount(); row++)
        {
            const int block = table->item(row, 1)->data(Qt::UserRole).toInt();
            if(doc.spec().isTrailerBlock(block))
                table->item(row, 0)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        }
    });

    connect(stopButton, &QPushButton::clicked, &dialog, [&]() {
        cancelRequested = true;
    });
    connect(previewButton, &QPushButton::clicked, &dialog, [&]() {
        cancelRequested = false;
        for(int row = 0; row < table->rowCount(); row++)
        {
            if(cancelRequested)
                break;
            QApplication::processEvents();
            const int block = table->item(row, 1)->data(Qt::UserRole).toInt();
            const int sector = doc.spec().sectorOfBlock(block);
            char keyType = 'A';
            QString key;
            if(!chooseVerifiedKey(sector, &keyType, &key))
            {
                table->item(row, 5)->setText(tr("no verified key"));
                table->item(row, 0)->setCheckState(Qt::Unchecked);
                continue;
            }
            QString cardHex;
            QString error;
            if(!readBlockOnce(block, keyType, key, &cardHex, &error))
            {
                table->item(row, 5)->setText(tr("unreadable"));
                table->item(row, 4)->setText(error);
                table->item(row, 0)->setCheckState(Qt::Checked);
                continue;
            }
            table->item(row, 4)->setText(spacedHex(cardHex));
            if(cardHex == doc.blockHex(block))
            {
                table->item(row, 5)->setText(tr("same"));
                table->item(row, 0)->setCheckState(Qt::Unchecked);
            }
            else if(cardHex.contains(QLatin1Char('?')))
            {
                table->item(row, 5)->setText(tr("partially unknown"));
                table->item(row, 0)->setCheckState(Qt::Checked);
            }
            else
            {
                table->item(row, 5)->setText(tr("different"));
                table->item(row, 0)->setCheckState(Qt::Checked);
            }
        }
    });

    connect(writeButton, &QPushButton::clicked, &dialog, [&]() {
        QList<int> selected;
        for(int row = 0; row < table->rowCount(); row++)
            if(table->item(row, 0)->checkState() == Qt::Checked)
                selected.append(row);
        if(selected.isEmpty())
        {
            QMessageBox::information(&dialog, tr("Write plan"), tr("No block is selected."));
            return;
        }
        if(QMessageBox::warning(&dialog, tr("Write plan"),
                                tr("Write %1 block(s) to the card? This cannot be undone.")
                                .arg(selected.size()),
                                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;

        taskRunning = true;
        cancelRequested = false;
        tracker.start(MfcRecovery::Method::KnownKeys, selected.size());
        setTaskRunning(true);
        int written = 0;
        QStringList failures;
        for(int row : selected)
        {
            if(cancelRequested)
                break;
            const int block = table->item(row, 1)->data(Qt::UserRole).toInt();
            const QString hex = doc.blockHex(block);
            QString error;
            table->item(row, 5)->setText(tr("writing..."));
            QApplication::processEvents();
            if(writeBlockOnce(block, hex, &error))
            {
                table->item(row, 5)->setText(tr("verified"));
                written++;
            }
            else
            {
                table->item(row, 5)->setText(tr("failed"));
                failures << QStringLiteral("%1: %2").arg(block).arg(error);
                doc.setBlock(block, hex, Mfc::BlockState::Failed);
            }
            refreshSummary();
            QApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        taskRunning = false;
        setTaskRunning(false);
        refreshDataTable();
        refreshKeyTable();
        refreshSummary();
        applyToMifare();
        const QString message = tr("Write plan: %1 block(s) verified, %2 failed")
                                .arg(written).arg(failures.size());
        logLine(message);
        for(const QString& failure : failures)
            logLine(QStringLiteral("  ") + failure);
        setStatus(message);
        if(!failures.isEmpty())
            QMessageBox::information(&dialog, tr("Write plan"),
                                     message + QStringLiteral("\n\n") + failures.join(QStringLiteral("\n")));
    });
    connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::accept);

    dialog.exec();
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

void MfcWorkbench::readCardInfo()
{
    if(!deviceReady)
    {
        setStatus(tr("Connect the Proxmark3 first"));
        return;
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
        setStatus(tr("Card information failed: the client is not available"));
        return;
    }

    const QString clean = Util::stripAnsi(result.output);
    QRegularExpression uidRe(QStringLiteral("UID\\s*:?\\s*((?:[0-9A-Fa-f]{2}[ ]*){4,10})"));
    QRegularExpressionMatch uidMatch = uidRe.match(clean);
    if(uidMatch.hasMatch())
    {
        QString uid = uidMatch.captured(1);
        uid.remove(QRegularExpression(QStringLiteral("\\s")));
        doc.uid = uid.toUpper();
    }
    QRegularExpression typeRe(QStringLiteral("MIFARE\\s+Classic\\s+(Mini|1K|2K|4K)"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch typeMatch = typeRe.match(clean);
    if(!typeMatch.hasMatch())
        typeMatch = QRegularExpression(QStringLiteral("MIFARE\\s+(Mini)\\b"),
                                       QRegularExpression::CaseInsensitiveOption).match(clean);
    if(typeMatch.hasMatch())
    {
        Mfc::CardType type = Mfc::CardType::M1K;
        Mfc::Spec::fromName(typeMatch.captured(1).toLower(), &type);
        if(type != doc.cardType())
        {
            if(doc.completeBlockCount() > 0 || doc.verifiedKeyCount() > 0)
            {
                if(QMessageBox::question(this, tr("Card type"),
                                         tr("The card is %1 but the document is %2. Change it?")
                                         .arg(typeMatch.captured(1), doc.cardDisplayName()))
                   != QMessageBox::Yes)
                {
                    refreshSummary();
                    return;
                }
            }
            doc.setCardType(type);
            nonce = MfcRecovery::NonceType::Unknown;
            updatingTables = true;
            cardTypeBox->setCurrentIndex(cardTypeBox->findData(typeToInt(type)));
            updatingTables = false;
            emit documentTypeChanged(typeToInt(type));
            refreshKeyTable();
            refreshDataTable();
            refreshAdvice();
        }
    }
    refreshSummary();
    setStatus(tr("Card identified"));
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

#include "ui/diagnosticspage.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "common/util.h"

namespace
{

// Features written by scripts/gen-capabilities.py.  Pm3Capabilities only
// answers has()/reason() for a name, so the page lists the manifest's own
// feature set in the generator's order.  A feature that the manifest reports
// as removed but that is not listed here is appended by rebuildCapabilityTable().
QStringList featureKeys()
{
    return QStringList()
           << QStringLiteral("lf")
           << QStringLiteral("hitag")
           << QStringLiteral("em4x50")
           << QStringLiteral("em4x70")
           << QStringLiteral("zx8211")
           << QStringLiteral("hf")
           << QStringLiteral("iso15693")
           << QStringLiteral("legicrf")
           << QStringLiteral("iso14443b")
           << QStringLiteral("iso14443a")
           << QStringLiteral("desfire_sim")
           << QStringLiteral("iclass")
           << QStringLiteral("seos")
           << QStringLiteral("felica")
           << QStringLiteral("nfcbarcode")
           << QStringLiteral("hfsniff")
           << QStringLiteral("hfplot")
           << QStringLiteral("compression")
           << QStringLiteral("standalone");
}

const int kCommandHardTimeoutMs = 15000;
const int kCommandIdleTimeoutMs = 4000;

QString hexWord(quint16 value)
{
    return QStringLiteral("0x%1").arg(QString::number(value, 16).rightJustified(4, QLatin1Char('0')).toUpper());
}

QTableWidgetItem* readOnlyItem(const QString& text)
{
    QTableWidgetItem* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QStringList diagnosticFailurePatterns()
{
    return QStringList()
           << QStringLiteral("\\[!\\]")
           << QStringLiteral("\\[-\\]")
           << QStringLiteral("unknown command")
           << QStringLiteral("is not a valid command")
           << QStringLiteral("No Proxmark3 device")
           << QStringLiteral("device is not connected");
}

QString statusText(Util::ExecStatus status)
{
    switch(status)
    {
    case Util::EXEC_OK:
        return DiagnosticsPage::tr("completed");
    case Util::EXEC_TIMEOUT:
        return DiagnosticsPage::tr("timed out");
    case Util::EXEC_FAILED:
        return DiagnosticsPage::tr("failed");
    case Util::EXEC_NOT_RUNNING:
        return DiagnosticsPage::tr("client is not running");
    case Util::EXEC_ABORTED:
        return DiagnosticsPage::tr("client exited");
    case Util::EXEC_BUSY:
        return DiagnosticsPage::tr("client is busy");
    case Util::EXEC_CANCELLED:
        return DiagnosticsPage::tr("cancelled");
    }
    return DiagnosticsPage::tr("unknown");
}

} // namespace

DiagnosticsPage::DiagnosticsPage(Util* util, QWidget* parent)
    : QWidget(parent), util(util), connected(false), commandRunning(false),
      summaryTable(nullptr), capabilityTable(nullptr), capabilityHintLabel(nullptr),
      logEdit(nullptr), clientVersionButton(nullptr), antennaButton(nullptr),
      cardInfoButton(nullptr), nonceButton(nullptr), copyButton(nullptr), saveButton(nullptr)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QLabel* titleLabel = new QLabel(tr("Device diagnostics"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    QLabel* introLabel = new QLabel(
        tr("What the GUI knows about the connected Proxmark3, the firmware it runs "
           "and the commands that firmware was built with."), this);
    introLabel->setWordWrap(true);
    mainLayout->addWidget(introLabel);

    // --- Connection and versions -----------------------------------------
    QGroupBox* summaryBox = new QGroupBox(tr("Connection and versions"), this);
    QVBoxLayout* summaryLayout = new QVBoxLayout(summaryBox);

    summaryTable = new QTableWidget(RowCount, 2, summaryBox);
    summaryTable->setHorizontalHeaderLabels(QStringList() << tr("Item") << tr("Value"));
    summaryTable->verticalHeader()->setVisible(false);
    summaryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    summaryTable->setSelectionMode(QAbstractItemView::SingleSelection);
    summaryTable->setAlternatingRowColors(true);
    summaryTable->setWordWrap(false);
    summaryTable->setTextElideMode(Qt::ElideMiddle);
    summaryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    summaryTable->horizontalHeader()->setStretchLastSection(true);

    const QStringList summaryLabels = QStringList()
                                      << tr("Connection")
                                      << tr("COM port")
                                      << tr("USB serial")
                                      << tr("USB VID / PID")
                                      << tr("USB location")
                                      << tr("Client version")
                                      << tr("Firmware version")
                                      << tr("Firmware commit")
                                      << tr("Platform")
                                      << tr("Capability manifest")
                                      << tr("Capabilities")
                                      << tr("Removed features")
                                      << tr("Client channel")
                                      << tr("Demo mode")
                                      << tr("Handshake last error");
    Q_ASSERT(summaryLabels.size() == RowCount);
    for(int row = 0; row < summaryLabels.size(); ++row)
    {
        summaryTable->setItem(row, 0, readOnlyItem(summaryLabels.at(row)));
        summaryTable->setItem(row, 1, readOnlyItem(QString()));
    }
    summaryLayout->addWidget(summaryTable);
    mainLayout->addWidget(summaryBox, 1);

    // --- Capabilities ----------------------------------------------------
    QGroupBox* capabilityBox = new QGroupBox(tr("Firmware capabilities"), this);
    QVBoxLayout* capabilityLayout = new QVBoxLayout(capabilityBox);

    capabilityHintLabel = new QLabel(capabilityBox);
    capabilityHintLabel->setWordWrap(true);
    capabilityLayout->addWidget(capabilityHintLabel);

    capabilityTable = new QTableWidget(0, 3, capabilityBox);
    capabilityTable->setHorizontalHeaderLabels(QStringList() << tr("Feature")
                                            << tr("Status")
                                            << tr("Reason"));
    capabilityTable->verticalHeader()->setVisible(false);
    capabilityTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    capabilityTable->setSelectionMode(QAbstractItemView::SingleSelection);
    capabilityTable->setAlternatingRowColors(true);
    capabilityTable->setWordWrap(false);
    capabilityTable->setTextElideMode(Qt::ElideMiddle);
    capabilityTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    capabilityTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    capabilityTable->horizontalHeader()->setStretchLastSection(true);
    capabilityLayout->addWidget(capabilityTable);
    mainLayout->addWidget(capabilityBox, 1);

    // --- Diagnostic commands --------------------------------------------
    QGroupBox* commandBox = new QGroupBox(tr("Diagnostics"), this);
    QVBoxLayout* commandLayout = new QVBoxLayout(commandBox);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    clientVersionButton = new QPushButton(tr("Client version"), commandBox);
    clientVersionButton->setToolTip(tr("Runs \"hw version\" and shows the client and firmware versions."));
    antennaButton = new QPushButton(tr("Antenna measurement"), commandBox);
    antennaButton->setToolTip(tr("Runs \"hf tune\" and shows the measured antenna voltage."));
    cardInfoButton = new QPushButton(tr("Card info"), commandBox);
    cardInfoButton->setToolTip(tr("Runs \"hf 14a info\" and shows the card in the field."));
    nonceButton = new QPushButton(tr("Nonce diagnostic"), commandBox);
    nonceButton->setToolTip(tr("Runs \"hf mf info\" and shows the MIFARE nonce diagnostics."));
    copyButton = new QPushButton(tr("Copy summary"), commandBox);
    copyButton->setToolTip(tr("Puts the summary above on the clipboard."));
    saveButton = new QPushButton(tr("Save report..."), commandBox);
    saveButton->setToolTip(tr("Writes the summary and the command output to a text file."));

    buttonLayout->addWidget(clientVersionButton);
    buttonLayout->addWidget(antennaButton);
    buttonLayout->addWidget(cardInfoButton);
    buttonLayout->addWidget(nonceButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(copyButton);
    buttonLayout->addWidget(saveButton);
    commandLayout->addLayout(buttonLayout);

    logEdit = new QPlainTextEdit(commandBox);
    logEdit->setReadOnly(true);
    logEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    logEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logEdit->setPlaceholderText(tr("Diagnostic command output appears here."));
    logEdit->setMaximumBlockCount(20000);
    commandLayout->addWidget(logEdit);
    mainLayout->addWidget(commandBox, 2);

    connect(clientVersionButton, &QPushButton::clicked, this, &DiagnosticsPage::onClientVersion);
    connect(antennaButton, &QPushButton::clicked, this, &DiagnosticsPage::onAntennaMeasurement);
    connect(cardInfoButton, &QPushButton::clicked, this, &DiagnosticsPage::onCardInfo);
    connect(nonceButton, &QPushButton::clicked, this, &DiagnosticsPage::onNonceDiagnostic);
    connect(copyButton, &QPushButton::clicked, this, &DiagnosticsPage::copySummary);
    connect(saveButton, &QPushButton::clicked, this, &DiagnosticsPage::saveReport);

    refresh();
}

void DiagnosticsPage::setDeviceState(bool connected, const QString& portName,
                                     const QString& clientVersion, const QString& firmwareVersion,
                                     const QString& handshakeError)
{
    this->connected = connected;
    this->portName = portName;
    this->clientVersion = clientVersion;
    this->firmwareVersion = firmwareVersion;
    this->handshakeError = handshakeError;
    refresh();
}

void DiagnosticsPage::setCapabilities(const Pm3Capabilities& capabilities)
{
    this->capabilities = capabilities;
    refresh();
}

void DiagnosticsPage::setDeviceIdentity(const Device::Identity& identity)
{
    this->identity = identity;
    refresh();
}

void DiagnosticsPage::refresh()
{
    rebuildCapabilityTable();
    updateSummary();
    updateActions();
}

void DiagnosticsPage::setSummaryRow(int row, const QString& value)
{
    if(row < 0 || row >= summaryTable->rowCount())
        return;
    QTableWidgetItem* item = summaryTable->item(row, 1);
    if(item == nullptr)
    {
        item = readOnlyItem(value);
        summaryTable->setItem(row, 1, item);
        return;
    }
    item->setText(value);
}

void DiagnosticsPage::updateSummary()
{
    setSummaryRow(RowConnection, connected ? tr("Connected") : tr("Disconnected"));

    QString port = portName;
    if(port.isEmpty())
        port = identity.portName;
    setSummaryRow(RowPort, port.isEmpty() ? tr("unknown") : port);

    setSummaryRow(RowSerial, identity.serial.isEmpty() ? tr("unknown") : identity.serial);
    setSummaryRow(RowVidPid, identity.hasVidPid
                  ? QStringLiteral("%1 / %2").arg(hexWord(identity.vid), hexWord(identity.pid))
                  : tr("unknown"));
    setSummaryRow(RowLocation, identity.systemLocation.isEmpty() ? tr("unknown") : identity.systemLocation);

    setSummaryRow(RowClientVersion, clientVersion.isEmpty() ? tr("unknown") : clientVersion);

    QString firmware = firmwareVersion;
    if(firmware.isEmpty())
        firmware = capabilities.firmwareVersion();
    setSummaryRow(RowFirmwareVersion, firmware.isEmpty() ? tr("unknown") : firmware);

    setSummaryRow(RowFirmwareCommit, capabilities.firmwareCommit().isEmpty()
                  ? tr("unknown") : capabilities.firmwareCommit());

    QString platform = capabilities.platform();
    if(platform.isEmpty())
        platform = tr("unknown");
    else if(capabilities.platformSize() > 0)
        platform = tr("%1 (%2 KB)").arg(platform).arg(capabilities.platformSize());
    setSummaryRow(RowPlatform, platform);

    setSummaryRow(RowManifest, capabilities.manifestPath().isEmpty()
                  ? tr("not found") : capabilities.manifestPath());

    setSummaryRow(RowCapabilities, capabilitySummary);

    const QStringList removed = capabilities.removedFeatures();
    setSummaryRow(RowRemoved, removed.isEmpty() ? tr("none") : removed.join(QStringLiteral(", ")));

    QString channel = tr("unknown");
    if(util != nullptr)
    {
        if(util->isChannelOwned())
            channel = tr("owned (a command is running)");
        else
            channel = tr("free");
        if(util->isCommandActive())
            channel += tr(" - command active");
    }
    setSummaryRow(RowChannel, channel);

    setSummaryRow(RowDemo, Util::isDemoMode() ? tr("on") : tr("off"));

    setSummaryRow(RowHandshake, handshakeError.isEmpty() ? tr("none") : handshakeError);

    summaryTable->resizeRowsToContents();
}

void DiagnosticsPage::rebuildCapabilityTable()
{
    const bool loaded = capabilities.isLoaded();

    QStringList keys = featureKeys();
    const QStringList removed = capabilities.removedFeatures();
    for(const QString& name : removed)
    {
        const QString key = name.toLower();
        if(!keys.contains(key))
            keys << key;
    }

    capabilityTable->clearContents();
    capabilityTable->setRowCount(0);

    int supported = 0;
    for(const QString& key : keys)
    {
        const bool isSupported = capabilities.has(key);
        if(isSupported)
            ++supported;

        const int row = capabilityTable->rowCount();
        capabilityTable->insertRow(row);

        QTableWidgetItem* nameItem = readOnlyItem(key.toUpper());
        QTableWidgetItem* statusItem;
        QTableWidgetItem* reasonItem;
        QColor color;
        if(!loaded)
        {
            statusItem = readOnlyItem(tr("Unknown"));
            reasonItem = readOnlyItem(tr("No capability manifest found, feature support is unknown"));
            color = QColor(120, 120, 120);
        }
        else
        {
            statusItem = readOnlyItem(isSupported ? tr("Supported") : tr("Unsupported"));
            reasonItem = readOnlyItem(capabilities.reason(key));
            color = isSupported ? QColor(0, 150, 90) : QColor(190, 40, 40);
        }
        statusItem->setForeground(color);

        capabilityTable->setItem(row, 0, nameItem);
        capabilityTable->setItem(row, 1, statusItem);
        capabilityTable->setItem(row, 2, reasonItem);
    }

    if(loaded)
    {
        capabilitySummary = tr("%1 of %2 supported").arg(supported).arg(keys.size());
        capabilityHintLabel->setText(tr("Manifest: %1").arg(capabilities.manifestPath()));
    }
    else
    {
        capabilitySummary = tr("unknown (no manifest)");
        capabilityHintLabel->setText(
            tr("No capability manifest was found next to the client. Feature support is "
               "unknown, so no command is disabled; the client reports unsupported "
               "commands as failures."));
    }
    capabilityTable->resizeColumnsToContents();
    capabilityTable->horizontalHeader()->setStretchLastSection(true);
}

void DiagnosticsPage::updateActions()
{
    const bool idle = !commandRunning;
    clientVersionButton->setEnabled(connected && idle);
    antennaButton->setEnabled(connected && idle && capabilities.has(QStringLiteral("hf")));
    cardInfoButton->setEnabled(connected && idle && capabilities.has(QStringLiteral("iso14443a")));
    nonceButton->setEnabled(connected && idle && capabilities.has(QStringLiteral("iso14443a")));
    copyButton->setEnabled(true);
    saveButton->setEnabled(true);
}

void DiagnosticsPage::runCommand(const QString& title, const QString& command,
                                 const QStringList& successPatterns)
{
    if(util == nullptr)
        return;
    if(!connected)
    {
        appendLog(tr("[!] Connect the Proxmark3 before running \"%1\".").arg(command));
        emit statusMessage(tr("Connect the Proxmark3 first"));
        return;
    }
    if(commandRunning)
    {
        appendLog(tr("[!] A diagnostic command is already running."));
        return;
    }
    if(util->isChannelOwned() || util->isCommandActive())
    {
        appendLog(tr("[!] The client is busy; \"%1\" was not started.").arg(command));
        emit statusMessage(tr("The client is busy"));
        return;
    }

    commandRunning = true;
    updateActions();

    appendLog(QString());
    appendLog(QStringLiteral("$ %1").arg(command));
    appendLog(tr("--- %1, %2").arg(title,
                                   QDateTime::currentDateTime().toString(Qt::ISODate)));

    Util::ReturnTrigger trigger(kCommandIdleTimeoutMs);
    trigger.hardTimeout = kCommandHardTimeoutMs;
    trigger.expectedOutputs = successPatterns;
    trigger.failedOutputs = diagnosticFailurePatterns();
    // The marker line also gives "hf tune" the Enter it waits for; the client
    // prints the prompt after it, which releases the channel even when the
    // marker itself was consumed by the command.
    trigger.useCompletionMarker = true;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Util::ExecResult result = util->execCMDWithResult(command, trigger, true);
    QApplication::restoreOverrideCursor();

    commandRunning = false;

    const QString output = Util::stripAnsi(result.output).trimmed();
    appendLog(output.isEmpty() ? tr("(no output)") : output);

    QString state = statusText(result.status);
    if(result.status == Util::EXEC_TIMEOUT && result.successPatternMatched)
        state = tr("completed, no completion marker");
    appendLog(tr("--- %1: %2").arg(title, state));
    emit statusMessage(tr("%1: %2").arg(title, state));

    updateActions();
}

void DiagnosticsPage::onClientVersion()
{
    runCommand(tr("Client version"), QStringLiteral("hw version"),
               QStringList() << QStringLiteral("\\[ Client \\]")
               << QStringLiteral("Firmware"));
}

void DiagnosticsPage::onAntennaMeasurement()
{
    runCommand(tr("Antenna measurement"), QStringLiteral("hf tune"),
               QStringList() << QStringLiteral("Done!")
               << QStringLiteral("mV")
               << QStringLiteral("V /"));
}

void DiagnosticsPage::onCardInfo()
{
    runCommand(tr("Card info"), QStringLiteral("hf 14a info"),
               QStringList() << QStringLiteral("UID")
               << QStringLiteral("ATQA")
               << QStringLiteral("SAK"));
}

void DiagnosticsPage::onNonceDiagnostic()
{
    runCommand(tr("Nonce diagnostic"), QStringLiteral("hf mf info"),
               QStringList() << QStringLiteral("MIFARE")
               << QStringLiteral("Nonce")
               << QStringLiteral("PRNG"));
}

QString DiagnosticsPage::buildSummary() const
{
    QStringList lines;
    lines << tr("Proxmark3 device diagnostics");
    lines << tr("Generated: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    lines << QString();
    for(int row = 0; row < summaryTable->rowCount(); ++row)
    {
        const QTableWidgetItem* labelItem = summaryTable->item(row, 0);
        const QTableWidgetItem* valueItem = summaryTable->item(row, 1);
        lines << tr("%1: %2").arg(labelItem != nullptr ? labelItem->text() : QString(),
                                  valueItem != nullptr ? valueItem->text() : QString());
    }
    return lines.join(QStringLiteral("\n"));
}

void DiagnosticsPage::copySummary()
{
    QApplication::clipboard()->setText(buildSummary());
    emit statusMessage(tr("Diagnostics summary copied to the clipboard"));
}

void DiagnosticsPage::saveReport()
{
    const QString defaultName = QStringLiteral("proxmark3-diagnostics-%1.txt")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString fileName = QFileDialog::getSaveFileName(
                                 this, tr("Save diagnostics report"),
                                 QDir::home().filePath(defaultName),
                                 tr("Text files (*.txt);;All files (*)"));
    if(fileName.isEmpty())
        return;

    QString report = buildSummary();
    report += QStringLiteral("\n\n");
    report += tr("--- Command output ---");
    report += QStringLiteral("\n\n");
    const QString log = logEdit->toPlainText();
    report += log.isEmpty() ? tr("(no command output)") : log;
    report += QLatin1Char('\n');

    QSaveFile file(fileName);
    if(!file.open(QFile::WriteOnly | QFile::Text))
    {
        QMessageBox::warning(this, tr("Save report"),
                             tr("Cannot write \"%1\": %2").arg(fileName, file.errorString()));
        return;
    }
    file.write(report.toUtf8());
    if(!file.commit())
    {
        QMessageBox::warning(this, tr("Save report"),
                             tr("Cannot write \"%1\": %2").arg(fileName, file.errorString()));
        return;
    }
    emit statusMessage(tr("Diagnostics report saved to %1").arg(fileName));
}

void DiagnosticsPage::appendLog(const QString& text)
{
    logEdit->appendPlainText(text);
}

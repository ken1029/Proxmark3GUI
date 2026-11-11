#include "ui/tracepage.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "common/util.h"

namespace
{

enum TraceColumn
{
    ColIndex = 0,
    ColStart,
    ColEnd,
    ColDirection,
    ColCrc,
    ColParity,
    ColShort,
    ColLength,
    ColData,
    ColAnnotation,
    ColumnCount
};

QString crcDisplay(Trace::Crc crc)
{
    if(crc == Trace::Crc::None)
        return QString();
    return Trace::crcName(crc);
}

} // namespace

TracePage::TracePage(Util* util, QWidget* parent)
    : QWidget(parent)
    , util(util)
    , deviceReady(false)
    , running(false)
{
    setObjectName(QStringLiteral("TracePage"));
    buildUi();
    updateButtons();
    showStatus(tr("Load a text trace or capture one from the device."));
}

void TracePage::buildUi()
{
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* toolbar = new QHBoxLayout();
    captureDeviceButton = new QPushButton(tr("Capture device trace"), this);
    captureDeviceButton->setObjectName(QStringLiteral("captureDeviceButton"));
    captureBufferButton = new QPushButton(tr("Capture client buffer"), this);
    captureBufferButton->setObjectName(QStringLiteral("captureBufferButton"));
    loadButton = new QPushButton(tr("Load..."), this);
    loadButton->setObjectName(QStringLiteral("loadButton"));
    saveButton = new QPushButton(tr("Save packets..."), this);
    saveButton->setObjectName(QStringLiteral("saveButton"));
    clearButton = new QPushButton(tr("Clear"), this);
    clearButton->setObjectName(QStringLiteral("clearButton"));
    rawToggle = new QToolButton(this);
    rawToggle->setObjectName(QStringLiteral("rawToggle"));
    rawToggle->setCheckable(true);
    rawToggle->setText(tr("Show raw output"));
    rawToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);

    toolbar->addWidget(captureDeviceButton);
    toolbar->addWidget(captureBufferButton);
    toolbar->addWidget(loadButton);
    toolbar->addWidget(saveButton);
    toolbar->addWidget(clearButton);
    toolbar->addStretch();
    toolbar->addWidget(rawToggle);
    layout->addLayout(toolbar);

    QHBoxLayout* filters = new QHBoxLayout();
    filters->addWidget(new QLabel(tr("Direction:"), this));
    directionCombo = new QComboBox(this);
    directionCombo->setObjectName(QStringLiteral("directionCombo"));
    directionCombo->addItem(tr("Any"), -1);
    directionCombo->addItem(tr("Rdr"), static_cast<int>(Trace::Direction::Reader));
    directionCombo->addItem(tr("Tag"), static_cast<int>(Trace::Direction::Tag));
    directionCombo->addItem(tr("Unknown"), static_cast<int>(Trace::Direction::Unknown));
    filters->addWidget(directionCombo);

    filters->addWidget(new QLabel(tr("CRC:"), this));
    crcCombo = new QComboBox(this);
    crcCombo->setObjectName(QStringLiteral("crcCombo"));
    crcCombo->addItem(tr("Any"), -1);
    crcCombo->addItem(QStringLiteral("ok"), static_cast<int>(Trace::Crc::Ok));
    crcCombo->addItem(QStringLiteral("!!"), static_cast<int>(Trace::Crc::Fail));
    crcCombo->addItem(QStringLiteral("A ok"), static_cast<int>(Trace::Crc::AOk));
    crcCombo->addItem(QStringLiteral("B ok"), static_cast<int>(Trace::Crc::BOk));
    crcCombo->addItem(tr("blank"), static_cast<int>(Trace::Crc::None));
    filters->addWidget(crcCombo);

    timeCheck = new QCheckBox(tr("Time range:"), this);
    timeCheck->setObjectName(QStringLiteral("timeCheck"));
    filters->addWidget(timeCheck);
    timeFrom = new QDoubleSpinBox(this);
    timeFrom->setObjectName(QStringLiteral("timeFrom"));
    timeFrom->setRange(0.0, 1.0e12);
    timeFrom->setDecimals(2);
    timeFrom->setSingleStep(100.0);
    timeFrom->setValue(0.0);
    timeFrom->setEnabled(false);
    filters->addWidget(timeFrom);
    filters->addWidget(new QLabel(tr("to"), this));
    timeTo = new QDoubleSpinBox(this);
    timeTo->setObjectName(QStringLiteral("timeTo"));
    timeTo->setRange(0.0, 1.0e12);
    timeTo->setDecimals(2);
    timeTo->setSingleStep(100.0);
    timeTo->setValue(1.0e12);
    timeTo->setEnabled(false);
    filters->addWidget(timeTo);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("searchEdit"));
    searchEdit->setPlaceholderText(tr("hex or annotation"));
    searchEdit->setClearButtonEnabled(true);
    filters->addWidget(searchEdit, 1);
    layout->addLayout(filters);

    QSplitter* splitter = new QSplitter(Qt::Vertical, this);
    table = new QTableWidget(splitter);
    table->setObjectName(QStringLiteral("traceTable"));
    table->setColumnCount(ColumnCount);
    table->setHorizontalHeaderLabels(QStringList()
                                     << tr("#") << tr("Start") << tr("End") << tr("Direction")
                                     << tr("CRC") << tr("Parity") << tr("Short") << tr("Length")
                                     << tr("Data (hex)") << tr("Annotation"));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(ColData, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(ColAnnotation, QHeaderView::Stretch);
    splitter->addWidget(table);

    detailEdit = new QPlainTextEdit(splitter);
    detailEdit->setObjectName(QStringLiteral("detailEdit"));
    detailEdit->setReadOnly(true);
    detailEdit->setMaximumHeight(110);
    detailEdit->setPlaceholderText(tr("Select a packet to see its full data."));
    splitter->addWidget(detailEdit);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);

    rawEdit = new QPlainTextEdit(this);
    rawEdit->setObjectName(QStringLiteral("rawOutput"));
    rawEdit->setReadOnly(true);
    rawEdit->setMaximumHeight(160);
    rawEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    rawEdit->setVisible(false);
    layout->addWidget(rawEdit);

    QHBoxLayout* statusRow = new QHBoxLayout();
    countLabel = new QLabel(this);
    countLabel->setObjectName(QStringLiteral("countLabel"));
    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("statusLabel"));
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusRow->addWidget(countLabel);
    statusRow->addWidget(statusLabel, 1);
    layout->addLayout(statusRow);

    connect(captureDeviceButton, SIGNAL(clicked()), this, SLOT(captureDeviceTrace()));
    connect(captureBufferButton, SIGNAL(clicked()), this, SLOT(captureClientBuffer()));
    connect(loadButton, SIGNAL(clicked()), this, SLOT(loadTrace()));
    connect(saveButton, SIGNAL(clicked()), this, SLOT(savePackets()));
    connect(clearButton, SIGNAL(clicked()), this, SLOT(clearTrace()));
    connect(rawToggle, SIGNAL(toggled(bool)), rawEdit, SLOT(setVisible(bool)));
    connect(rawToggle, SIGNAL(toggled(bool)), this, SLOT(updateRawToggleText()));
    connect(directionCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(applyFilters()));
    connect(crcCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(applyFilters()));
    connect(timeCheck, SIGNAL(toggled(bool)), timeFrom, SLOT(setEnabled(bool)));
    connect(timeCheck, SIGNAL(toggled(bool)), timeTo, SLOT(setEnabled(bool)));
    connect(timeCheck, SIGNAL(toggled(bool)), this, SLOT(applyFilters()));
    connect(timeFrom, SIGNAL(valueChanged(double)), this, SLOT(applyFilters()));
    connect(timeTo, SIGNAL(valueChanged(double)), this, SLOT(applyFilters()));
    connect(searchEdit, SIGNAL(textChanged(QString)), this, SLOT(applyFilters()));
    connect(table, SIGNAL(itemSelectionChanged()), this, SLOT(updateDetail()));

    applyFilters();
}

void TracePage::updateRawToggleText()
{
    rawToggle->setText(rawEdit->isVisible() ? tr("Hide raw output") : tr("Show raw output"));
}

void TracePage::setDeviceReady(bool ready)
{
    deviceReady = ready;
    updateButtons();
}

void TracePage::setCapabilities(const Pm3Capabilities& capabilities)
{
    this->capabilities = capabilities;
    updateButtons();
}

void TracePage::updateButtons()
{
    const bool supported = capabilities.has(QStringLiteral("hfsniff"));
    const bool canCapture = deviceReady && supported && !running;

    QString tip;
    if(!deviceReady)
        tip = tr("Connect the Proxmark3 first");
    else if(!supported)
    {
        tip = capabilities.reason(QStringLiteral("HFSNIFF"));
        if(tip.isEmpty())
            tip = tr("This firmware reports no HF sniffing support");
    }
    else
        tip = tr("Read the packet trace from the device");

    captureDeviceButton->setEnabled(canCapture);
    captureBufferButton->setEnabled(canCapture);
    captureDeviceButton->setToolTip(tip);
    captureBufferButton->setToolTip(tip);

    loadButton->setEnabled(!running);
    saveButton->setEnabled(!running && !visible.isEmpty());
    clearButton->setEnabled(!running && (!packets.isEmpty() || !rawText.isEmpty()));
}

void TracePage::captureDeviceTrace()
{
    capture(QStringLiteral("trace list"), tr("device trace"));
}

void TracePage::captureClientBuffer()
{
    capture(QStringLiteral("trace list --buffer"), tr("client buffer"));
}

void TracePage::capture(const QString& command, const QString& source)
{
    if(running)
        return;
    if(util == nullptr)
    {
        showStatus(tr("No client connection is available."));
        return;
    }
    if(!deviceReady || !capabilities.has(QStringLiteral("hfsniff")))
    {
        showStatus(tr("Trace capture is not available: %1").arg(
                       capabilities.has(QStringLiteral("hfsniff"))
                       ? tr("connect the Proxmark3 first")
                       : capabilities.reason(QStringLiteral("HFSNIFF"))));
        return;
    }

    running = true;
    updateButtons();
    showStatus(tr("Capturing %1...").arg(source));

    Util::ReturnTrigger trigger(120000);
    trigger.hardTimeout = 120000;
    trigger.failedOutputs = QStringList({QStringLiteral("No trace"),
                                         QStringLiteral("not available in this mode"),
                                         QStringLiteral("command execution time out")});
    const Util::ExecResult result = util->execCMDWithResult(command, trigger, true);

    running = false;
    const QString output = Util::stripAnsi(result.output);
    rawEdit->setPlainText(output);
    if(!rawEdit->isVisible() && !output.isEmpty())
        rawToggle->setChecked(true);

    if(result.failurePatternMatched)
    {
        QString failure;
        const QStringList lines = output.split(QLatin1Char('\n'));
        for(const QString& line : lines)
        {
            if(Util::matchAnyPattern(line, trigger.failedOutputs))
            {
                failure = line.trimmed();
                break;
            }
        }
        if(failure.isEmpty())
            failure = tr("the client refused the trace command");
        showStatus(tr("Trace capture failed: %1").arg(failure));
        updateButtons();
        return;
    }

    switch(result.status)
    {
    case Util::EXEC_NOT_RUNNING:
        showStatus(tr("Trace capture failed: the client is not running."));
        updateButtons();
        return;
    case Util::EXEC_BUSY:
        showStatus(tr("Trace capture failed: the client is busy with another command."));
        updateButtons();
        return;
    case Util::EXEC_ABORTED:
        showStatus(tr("Trace capture failed: the client exited."));
        updateButtons();
        return;
    case Util::EXEC_CANCELLED:
        showStatus(tr("Trace capture was cancelled."));
        updateButtons();
        return;
    default:
        break;
    }

    if(output.trimmed().isEmpty())
    {
        showStatus(tr("Trace capture returned no output."));
        updateButtons();
        return;
    }

    parseTraceText(output, source);
    if(result.status == Util::EXEC_TIMEOUT)
        showStatus(statusLabel->text() + QLatin1Char(' ') + tr("(capture timed out; partial output)"));
    updateButtons();
}

void TracePage::loadTrace()
{
    const QString filename = QFileDialog::getOpenFileName(
                                 this, tr("Load trace"),
                                 QString(),
                                 tr("Trace files (*.txt *.trace *.log);;All files (*)"));
    if(filename.isEmpty())
        return;
    loadTraceFile(filename);
}

void TracePage::loadTraceFile(const QString& filename)
{
    if(filename.isEmpty() || running)
        return;

    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly))
    {
        showStatus(tr("Cannot read %1: %2").arg(QFileInfo(filename).fileName(), file.errorString()));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    const QFileInfo info(filename);
    const bool traceSuffix = info.suffix().compare(QStringLiteral("trace"), Qt::CaseInsensitive) == 0;
    if(bytes.contains('\0') || (traceSuffix && !bytes.contains("Start")))
    {
        showStatus(tr("%1 is a binary trace file. This page reads the client's text table: "
                      "export it as text and load that instead.").arg(info.fileName()));
        return;
    }

    const QString text = QString::fromUtf8(bytes);
    if(text.trimmed().isEmpty())
    {
        showStatus(tr("%1 is empty.").arg(info.fileName()));
        return;
    }

    parseTraceText(text, tr("Loaded %1").arg(info.fileName()));
    updateButtons();
}

void TracePage::savePackets()
{
    if(running)
        return;

    const QVector<Trace::Packet> selected = selectedPackets();
    if(selected.isEmpty())
    {
        showStatus(tr("There are no packets to save."));
        return;
    }

    QString selectedFilter;
    QString filename = QFileDialog::getSaveFileName(
                           this, tr("Save packets"), QStringLiteral("trace.csv"),
                           tr("CSV file (*.csv);;pcapng text (*.txt)"), &selectedFilter);
    if(filename.isEmpty())
        return;

    bool csv = true;
    const QString suffix = QFileInfo(filename).suffix().toLower();
    if(suffix == QStringLiteral("txt") || suffix == QStringLiteral("pcapng"))
        csv = false;
    else if(suffix.isEmpty())
    {
        csv = !selectedFilter.contains(QStringLiteral("pcapng"), Qt::CaseInsensitive);
        filename += csv ? QStringLiteral(".csv") : QStringLiteral(".txt");
    }

    writePackets(filename, csv, selected);
    updateButtons();
}

bool TracePage::writePackets(const QString& filename, bool csv, const QVector<Trace::Packet>& items)
{
    const QByteArray bytes = csv ? Trace::toCsv(items) : Trace::toHexText(items);
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly)
       || file.write(bytes) != bytes.size()
       || !file.commit())
    {
        showStatus(tr("Cannot write %1: %2").arg(QDir::toNativeSeparators(filename), file.errorString()));
        return false;
    }
    showStatus(tr("Wrote %1 packets to %2")
               .arg(items.size())
               .arg(QDir::toNativeSeparators(filename)));
    return true;
}

void TracePage::clearTrace()
{
    if(running)
        return;
    packets.clear();
    visible.clear();
    rawText.clear();
    rawEdit->clear();
    {
        const QSignalBlocker blocker(table);
        table->clearContents();
        table->setRowCount(0);
    }
    detailEdit->clear();
    applyFilters();
    showStatus(tr("Trace cleared."));
    updateButtons();
}

void TracePage::parseTraceText(const QString& text, const QString& source)
{
    rawText = text;
    rawEdit->setPlainText(text);

    const Trace::ParseResult result = Trace::parse(text, false);
    packets = result.packets;
    if(!result.ok)
        packets.clear();
    applyFilters();

    if(!result.ok)
    {
        showStatus(tr("%1: %2").arg(source, result.error));
        return;
    }

    QStringList warnings = result.warnings;
    if(warnings.size() > 4)
    {
        const int extra = warnings.size() - 4;
        warnings = warnings.mid(0, 4);
        warnings << tr("and %1 more").arg(extra);
    }
    QString message = tr("%1: %2 packets").arg(source).arg(packets.size());
    if(!warnings.isEmpty())
        message += QStringLiteral(" (") + warnings.join(QStringLiteral("; ")) + QLatin1Char(')');
    showStatus(message);
    updateButtons();
}

void TracePage::applyFilters()
{
    Trace::Filter filter;
    if(directionCombo->currentIndex() > 0)
    {
        filter.hasDirection = true;
        filter.direction = static_cast<Trace::Direction>(directionCombo->currentData().toInt());
    }
    if(crcCombo->currentIndex() > 0)
    {
        filter.hasCrc = true;
        filter.crc = static_cast<Trace::Crc>(crcCombo->currentData().toInt());
    }
    if(timeCheck->isChecked())
    {
        filter.hasTime = true;
        filter.from = timeFrom->value();
        filter.to = timeTo->value();
    }
    filter.text = searchEdit->text();

    visible = Trace::filter(packets, filter);
    rebuildTable();

    if(packets.isEmpty())
        countLabel->setText(tr("No packets"));
    else
        countLabel->setText(tr("%1 of %2 packets").arg(visible.size()).arg(packets.size()));
    updateButtons();
}

void TracePage::rebuildTable()
{
    const QSignalBlocker blocker(table);
    table->clearContents();
    table->setRowCount(visible.size());

    for(int row = 0; row < visible.size(); row++)
    {
        const Trace::Packet& packet = visible.at(row);

        QTableWidgetItem* indexItem = new QTableWidgetItem(QString::number(packet.index + 1));
        indexItem->setData(Qt::UserRole, row);
        table->setItem(row, ColIndex, indexItem);
        table->setItem(row, ColStart, new QTableWidgetItem(formatTime(packet.start)));
        table->setItem(row, ColEnd, new QTableWidgetItem(formatTime(packet.end)));
        table->setItem(row, ColDirection, new QTableWidgetItem(Trace::directionName(packet.direction)));
        table->setItem(row, ColCrc, new QTableWidgetItem(crcDisplay(packet.crc)));
        table->setItem(row, ColParity, new QTableWidgetItem(packet.parityError ? QStringLiteral("!!") : QString()));
        table->setItem(row, ColShort, new QTableWidgetItem(packet.shortBytes ? tr("short") : QString()));
        table->setItem(row, ColLength, new QTableWidgetItem(QString::number(packet.data.size())));
        table->setItem(row, ColData, new QTableWidgetItem(dataHex(packet.data)));
        table->setItem(row, ColAnnotation, new QTableWidgetItem(packet.annotation));
    }

    updateDetail();
}

void TracePage::updateDetail()
{
    if(table->selectionModel() == nullptr)
        return;

    const QModelIndexList rows = table->selectionModel()->selectedRows();
    if(rows.isEmpty())
    {
        detailEdit->setPlainText(tr("Select a packet to see its full data."));
        return;
    }

    const int row = rows.first().row();
    if(row < 0 || row >= visible.size())
    {
        detailEdit->clear();
        return;
    }

    const Trace::Packet& packet = visible.at(row);
    const QString crc = crcDisplay(packet.crc).isEmpty() ? tr("blank") : crcDisplay(packet.crc);
    QStringList lines;
    lines << tr("Packet #%1  %2  %3 - %4")
          .arg(packet.index + 1)
          .arg(Trace::directionName(packet.direction))
          .arg(formatTime(packet.start))
          .arg(formatTime(packet.end));
    lines << tr("CRC: %1    Parity: %2    Short: %3    Length: %4")
          .arg(crc)
          .arg(packet.parityError ? tr("error") : tr("none"))
          .arg(packet.shortBytes ? tr("yes") : tr("no"))
          .arg(packet.data.size());
    lines << tr("Data: %1").arg(dataHex(packet.data));
    lines << tr("Annotation: %1").arg(packet.annotation);
    detailEdit->setPlainText(lines.join(QLatin1Char('\n')));
}

QVector<Trace::Packet> TracePage::selectedPackets() const
{
    QVector<Trace::Packet> result;
    if(table->selectionModel() != nullptr)
    {
        QVector<int> rows;
        const QModelIndexList selected = table->selectionModel()->selectedRows();
        for(const QModelIndex& index : selected)
            rows.append(index.row());
        std::sort(rows.begin(), rows.end());
        for(int row : rows)
            if(row >= 0 && row < visible.size())
                result.append(visible.at(row));
    }
    if(result.isEmpty())
        result = visible;
    return result;
}

void TracePage::showStatus(const QString& text)
{
    statusLabel->setText(text);
    emit statusMessage(text);
}

QString TracePage::formatTime(double value)
{
    const bool integral = static_cast<qint64>(value) == value;
    return QString::number(value, 'f', integral ? 0 : 2);
}

QString TracePage::dataHex(const QByteArray& data)
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

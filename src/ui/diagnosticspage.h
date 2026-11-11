#ifndef DIAGNOSTICPAGE_H
#define DIAGNOSTICPAGE_H

#include <QWidget>

#include "common/capabilities.h"
#include "common/deviceidentity.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class Util;

/*
 * Device diagnostics: what the GUI believes about the connected Proxmark3.
 *
 * The page shows the connection state, the USB identity (a COM port alone is
 * not an identity, see Device::Identity), the client and firmware versions,
 * the capability manifest and the state of the client channel.  The manifest
 * is reported as "unknown" when it is missing, never as an empty list of
 * unsupported features.
 *
 * Every command the page runs goes through Util (never through a process of
 * its own) and is capped by a 15 s hard timeout, so a wedged client cannot
 * block the GUI forever.  The output is kept in a plain text log that can be
 * copied or saved next to the other support information.
 */
class DiagnosticsPage : public QWidget
{
    Q_OBJECT
public:
    explicit DiagnosticsPage(Util* util, QWidget* parent = nullptr);

    void setDeviceState(bool connected, const QString& portName, const QString& clientVersion,
                        const QString& firmwareVersion, const QString& handshakeError);
    void setCapabilities(const Pm3Capabilities& capabilities);
    void setDeviceIdentity(const Device::Identity& identity);
    void refresh();

signals:
    void statusMessage(const QString& text);

private slots:
    void onClientVersion();
    void onAntennaMeasurement();
    void onCardInfo();
    void onNonceDiagnostic();
    void copySummary();
    void saveReport();

private:
    enum SummaryRow
    {
        RowConnection = 0,
        RowPort,
        RowSerial,
        RowVidPid,
        RowLocation,
        RowClientVersion,
        RowFirmwareVersion,
        RowFirmwareCommit,
        RowPlatform,
        RowManifest,
        RowCapabilities,
        RowRemoved,
        RowChannel,
        RowDemo,
        RowHandshake,
        RowCount
    };

    void updateSummary();
    void rebuildCapabilityTable();
    void updateActions();
    void runCommand(const QString& title, const QString& command, const QStringList& successPatterns);
    void appendLog(const QString& text);
    QString buildSummary() const;
    void setSummaryRow(int row, const QString& value);

    Util* util;
    bool connected;
    QString portName;
    QString clientVersion;
    QString firmwareVersion;
    QString handshakeError;
    Pm3Capabilities capabilities;
    Device::Identity identity;
    bool commandRunning;
    // "n of m supported" or "unknown"; filled by rebuildCapabilityTable().
    QString capabilitySummary;

    QTableWidget* summaryTable;
    QTableWidget* capabilityTable;
    QLabel* capabilityHintLabel;
    QPlainTextEdit* logEdit;
    QPushButton* clientVersionButton;
    QPushButton* antennaButton;
    QPushButton* cardInfoButton;
    QPushButton* nonceButton;
    QPushButton* copyButton;
    QPushButton* saveButton;
};

#endif // DIAGNOSTICPAGE_H

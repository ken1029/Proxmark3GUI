#ifndef TRACEPAGE_H
#define TRACEPAGE_H

#include <QVector>
#include <QWidget>

#include "common/capabilities.h"
#include "module/tracemodel.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QToolButton;
class Util;

/*
 * Trace capture and viewer.
 *
 * `trace list` output is parsed by Trace::parse(); the page only shows what the
 * client printed.  A direction or CRC the client did not report stays unknown
 * ("?") and is never shown as a reader packet or as a CRC match.  Device
 * commands go through Util, so demo mode and channel ownership work unchanged.
 */
class TracePage : public QWidget
{
    Q_OBJECT
    friend class GuiTests;

public:
    explicit TracePage(Util* util, QWidget* parent = nullptr);

    void setDeviceReady(bool ready);
    void setCapabilities(const Pm3Capabilities& capabilities);
    void loadTraceFile(const QString& filename);

signals:
    void statusMessage(const QString& text);

private slots:
    void captureDeviceTrace();
    void captureClientBuffer();
    void loadTrace();
    void savePackets();
    void clearTrace();
    void applyFilters();
    void updateDetail();
    void updateRawToggleText();

private:
    void buildUi();
    void capture(const QString& command, const QString& source);
    void parseTraceText(const QString& text, const QString& source);
    void rebuildTable();
    void updateButtons();
    void showStatus(const QString& text);
    QVector<Trace::Packet> selectedPackets() const;
    bool writePackets(const QString& filename, bool csv, const QVector<Trace::Packet>& items);

    static QString formatTime(double value);
    static QString dataHex(const QByteArray& data);

    Util* util;
    Pm3Capabilities capabilities;
    bool deviceReady;
    bool running;

    QPushButton* captureDeviceButton;
    QPushButton* captureBufferButton;
    QPushButton* loadButton;
    QPushButton* saveButton;
    QPushButton* clearButton;
    QToolButton* rawToggle;
    QComboBox* directionCombo;
    QComboBox* crcCombo;
    QCheckBox* timeCheck;
    QDoubleSpinBox* timeFrom;
    QDoubleSpinBox* timeTo;
    QLineEdit* searchEdit;
    QLabel* countLabel;
    QLabel* statusLabel;
    QTableWidget* table;
    QPlainTextEdit* detailEdit;
    QPlainTextEdit* rawEdit;

    QVector<Trace::Packet> packets;
    QVector<Trace::Packet> visible;
    QString rawText;
};

#endif // TRACEPAGE_H

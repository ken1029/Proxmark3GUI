#ifndef MFUPAGE_H
#define MFUPAGE_H

#include <QWidget>

#include "common/capabilities.h"
#include "common/util.h"
#include "module/mfudocument.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QToolButton;

/*
 * Read-only MIFARE Ultralight / NTAG page.
 *
 * "Read card" runs `hf mfu info` and identifies the tag, "Read pages" runs
 * `hf mfu dump` and fills the page table, "Read NDEF" runs `hf mfu ndefread`.
 * The page model and the NDEF records are built from the client's own output,
 * so a page that was not read stays unknown instead of showing zeros.
 *
 * All device access goes through Util; the document model is Mfu::Document.
 */
class MfuPage : public QWidget
{
    Q_OBJECT
public:
    explicit MfuPage(Util* util, QWidget* parent = nullptr);

    void setDeviceReady(bool ready);
    void setCapabilities(const Pm3Capabilities& capabilities);

    const Mfu::Document& document() const { return doc; }
    bool isBusy() const { return busy; }

public slots:
    void readCard();
    void readPages();
    void readNdef();
    void saveJson();
    void openJson();

signals:
    void statusMessage(const QString& text);

private:
    void buildUi();
    void setBusy(bool busy);
    void updateButtons();
    void setStatus(const QString& text);
    void refreshAll();
    void refreshSummary();
    void refreshPages();
    void refreshNdef();
    bool hasDocument() const;
    // Runs one client command through Util and returns its ANSI stripped
    // output.  False when the command failed, timed out or the channel is
    // owned by another task.
    bool runCommand(const QString& command, unsigned long hardTimeoutMs, QString* output);
    QString describeResult(const Util::ExecResult& result, unsigned long hardTimeoutMs) const;
    QString stateText(Mfu::PageState state) const;
    QString lockText(int page) const;

    Util* util = nullptr;
    Pm3Capabilities capabilities;
    Mfu::Document doc;
    bool deviceReady = false;
    bool busy = false;

    QPushButton* readCardButton = nullptr;
    QPushButton* readPagesButton = nullptr;
    QPushButton* readNdefButton = nullptr;
    QPushButton* saveButton = nullptr;
    QPushButton* openButton = nullptr;
    QLabel* uidLabel = nullptr;
    QLabel* typeLabel = nullptr;
    QLabel* productLabel = nullptr;
    QLabel* pagesLabel = nullptr;
    QLabel* lockedLabel = nullptr;
    QLabel* staticLockLabel = nullptr;
    QLabel* dynamicLockLabel = nullptr;
    QLabel* ndefLabel = nullptr;
    QLabel* ndefStatusLabel = nullptr;
    QLineEdit* ndefHexEdit = nullptr;
    QTableWidget* pageTable = nullptr;
    QTableWidget* ndefTable = nullptr;
    QToolButton* rawToggle = nullptr;
    QPlainTextEdit* rawEdit = nullptr;
    QLabel* statusLabel = nullptr;
};

#endif // MFUPAGE_H

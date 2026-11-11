#ifndef MFCWORKBENCH_H
#define MFCWORKBENCH_H

#include <QWidget>

#include "common/util.h"
#include "module/mfcdocument.h"
#include "module/mfckeyrecovery.h"
#include "module/mfcwriteplan.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

class Mifare;

/*
 * MIFARE Classic workbench: one document, per-sector key states, recovery
 * method advice, long-task control, reading/backup and offline A/B comparison.
 * All device access goes through Util; the document model is Mfc::Document.
 */
class MfcWorkbench : public QWidget
{
    Q_OBJECT
    friend class GuiTests;
public:
    explicit MfcWorkbench(Util* util, Mifare* mifare, QWidget* parent = nullptr);

    void setConfigMap(const QVariantMap& configMap);
    void setDeviceReady(bool ready);
    bool isDeviceReady() const { return deviceReady; }
    bool isTaskActive() const { return taskRunning; }
    const Mfc::Document& document() const { return doc; }
    Mfc::Document& document() { return doc; }

    // --- RF session / card identity -----------------------------------
    // Every connection and every known card change starts a new session.  Keys
    // and blocks keep the session that produced them; only evidence from the
    // current session may be used to read or write the card in the field.
    quint64 activeSession() const { return targetSession; }
    QString targetUid() const { return targetUidValue; }
    bool hasTarget() const { return targetPresent; }
    // The document was read from (or identified for) the card that is present.
    bool documentMatchesTarget() const;
    // Identity reported by "Identify card".  Returns false when the document
    // belongs to another card; the caller then decides (new document / keep).
    bool applyCardIdentity(const QString& uid, Mfc::CardType type, QString* error = nullptr);
    // Start a fresh document for the card in the field (keeps the old one only
    // if the caller saved it first).
    bool startNewDocumentForTarget(QString* error = nullptr);
    // Write plan of the open dialog (test/automation hook).
    const Mfc::WritePlan& currentWritePlan() const { return plan; }

    // Test/automation hooks: no modal dialogs, explicit paths.
    bool importKeyFile(const QString& filename, const QString& layout, QString* error = nullptr);
    // Automation hook: skip the modal "write to the card?" confirmation.  The
    // write policy itself is never bypassed.
    void setConfirmWrites(bool on) { confirmWrites = on; }
    // Save the document and its "<file>.sha256" sidecar without a file dialog.
    bool saveBackupTo(const QString& filename, QString* error = nullptr, bool allowUnknownBin = false);
    bool loadCompareFile(bool sideA, const QString& filename, QString* error = nullptr);
    Mfc::CompareResult currentComparison() const { return comparison; }

public slots:
    void importFromMifare();
    void applyToMifare();
    void readCardInfo();
    void diagnoseNonce();

signals:
    void documentTypeChanged(int type);
    void statusMessage(const QString& message);
    void abortClientRequested();
    void taskStateChanged(bool running);

private slots:
    void refresh();
    void onKeyItemChanged(QTableWidgetItem* item);
    void onDataItemChanged(QTableWidgetItem* item);
    void onFilterChanged();
    void onMethodChanged();
    void runSelectedMethod();
    void cancelTask();
    void verifyKeys();
    void readBlocks();
    void readUnknownBlocks();
    void saveBackup();
    void writePlan();
    void saveKeyFile();
    void loadKeyFile();
    void loadCompareA();
    void loadCompareB();
    void compareDocuments();
    void exportCompareReport();
    void onCommandOutput(const QString& output);
    void onCommandFinished(const Util::ExecResult& result, const QString& output);

private:
    void buildUi();
    void refreshKeyTable();
    void refreshDataTable();
    void refreshAdvice();
    void refreshSummary();
    void refreshDiffTable();
    void setTaskRunning(bool running);
    void logLine(const QString& text);
    void setStatus(const QString& text);
    QString cardTypeName() const;
    QString keyStateText(Mfc::KeyState state) const;
    QString blockStateText(Mfc::BlockState state) const;
    QString nonceText() const;
    bool chooseVerifiedKey(int sector, char* type, QString* key) const;
    // Trailer payload from a raw read: Key A from this session's verified key,
    // access bytes, and Key B from the card when the access conditions return
    // it, otherwise from the verified key store (the card masks it).
    QString trailerReadback(int block, char authType, const QString& cardHex) const;
    bool readBlockOnce(int block, char keyType, const QString& key, QString* hex, QString* error);
    bool verifyOneKey(int sector, char type, const QString& key);
    // Returns the outcome and a human readable detail.  A data block is
    // compared byte for byte; a trailer is verified through the new keys and
    // its access conditions, never by comparing masked bytes.
    Mfc::WriteOutcome writeBlockOnce(int block, const QString& hex,
                                     const Mfc::TrailerPlan* trailer, QString* detail);
    void startMethod(MfcRecovery::Method method, const MfcRecovery::Command& command);
    MfcRecovery::Command commandForMethod(MfcRecovery::Method method, QString* error) const;
    void applyParsedResult(const MfcRecovery::ParseResult& parsed, MfcRecovery::Method method,
                           Util::ExecStatus status);
    void appendLog(const QString& text);
    void invalidateTargetWhenDisconnected();
    void refreshIdentity();

    Util* util = nullptr;
    Mifare* mifare = nullptr;
    QVariantMap configMap;
    Mfc::Document doc;
    MfcRecovery::NonceType nonce = MfcRecovery::NonceType::Unknown;
    MfcRecovery::Tracker tracker;
    MfcRecovery::Method currentMethod = MfcRecovery::Method::KnownKeys;
    // Sector/key type a single-key method (hardnested, darkside) is aiming at.
    MfcRecovery::Target currentTarget;
    QString taskOutput;
    bool taskRunning = false;
    bool deviceReady = false;
    bool updatingTables = false;
    bool cancelRequested = false;
    // RF session of the card in the field, and what identifies it.
    quint64 sessionSeq = 0;
    quint64 targetSession = 0;
    bool targetPresent = false;
    QString targetUidValue;
    Mfc::CardType targetType = Mfc::CardType::M1K;
    // Session of the running task, so late output cannot leak into the next.
    quint64 taskSession = 0;
    Mfc::WritePlan plan;
    // True while the write dialog owns the plan (preview/write/cancel).
    bool planBusy = false;
    bool confirmWrites = true;

    Mfc::Document compareA;
    Mfc::Document compareB;
    bool hasCompareA = false;
    bool hasCompareB = false;
    Mfc::CompareResult comparison;

    QLabel* cardLabel = nullptr;
    QLabel* uidLabel = nullptr;
    QLabel* nonceLabel = nullptr;
    QLabel* keySummary = nullptr;
    QLabel* blockSummary = nullptr;
    QLabel* stageLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QProgressBar* progress = nullptr;
    QComboBox* cardTypeBox = nullptr;
    QComboBox* filterBox = nullptr;
    QComboBox* methodBox = nullptr;
    QComboBox* knownTypeBox = nullptr;
    QComboBox* targetTypeBox = nullptr;
    QSpinBox* knownSectorSpin = nullptr;
    QSpinBox* targetSectorSpin = nullptr;
    QLineEdit* dictionaryEdit = nullptr;
    QLineEdit* compareAEdit = nullptr;
    QLineEdit* compareBEdit = nullptr;
    QCheckBox* includeDefaultsBox = nullptr;
    QCheckBox* showDiffOnlyBox = nullptr;
    QTableWidget* keyTable = nullptr;
    QTableWidget* dataTable = nullptr;
    QTableWidget* adviceTable = nullptr;
    QTableWidget* diffTable = nullptr;
    QPlainTextEdit* logEdit = nullptr;
    QPushButton* runButton = nullptr;
    QPushButton* cancelButton = nullptr;
    QTimer* refreshTimer = nullptr;
};

#endif // MFCWORKBENCH_H

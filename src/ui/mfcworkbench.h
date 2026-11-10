#ifndef MFCWORKBENCH_H
#define MFCWORKBENCH_H

#include <QWidget>

#include "common/util.h"
#include "module/mfcdocument.h"
#include "module/mfckeyrecovery.h"

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
public:
    explicit MfcWorkbench(Util* util, Mifare* mifare, QWidget* parent = nullptr);

    void setConfigMap(const QVariantMap& configMap);
    void setDeviceReady(bool ready);
    bool isDeviceReady() const { return deviceReady; }
    bool isTaskActive() const { return taskRunning; }
    const Mfc::Document& document() const { return doc; }
    Mfc::Document& document() { return doc; }

    // Test/automation hooks: no modal dialogs, explicit paths.
    bool importKeyFile(const QString& filename, const QString& layout, QString* error = nullptr);
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
    bool readBlockOnce(int block, char keyType, const QString& key, QString* hex, QString* error);
    bool verifyOneKey(int sector, char type, const QString& key);
    bool writeBlockOnce(int block, const QString& hex, QString* error);
    void startMethod(MfcRecovery::Method method, const MfcRecovery::Command& command);
    MfcRecovery::Command commandForMethod(MfcRecovery::Method method, QString* error) const;
    void applyParsedResult(const MfcRecovery::ParseResult& parsed, MfcRecovery::Method method,
                           Util::ExecStatus status);
    void appendLog(const QString& text);

    Util* util = nullptr;
    Mifare* mifare = nullptr;
    QVariantMap configMap;
    Mfc::Document doc;
    MfcRecovery::NonceType nonce = MfcRecovery::NonceType::Unknown;
    MfcRecovery::Tracker tracker;
    MfcRecovery::Method currentMethod = MfcRecovery::Method::KnownKeys;
    QString taskOutput;
    bool taskRunning = false;
    bool deviceReady = false;
    bool updatingTables = false;
    bool cancelRequested = false;

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

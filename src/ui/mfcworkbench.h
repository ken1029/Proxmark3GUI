#ifndef MFCWORKBENCH_H
#define MFCWORKBENCH_H

#include <QHash>
#include <QWidget>

#include "common/util.h"
#include "module/mfcbatch.h"
#include "module/mfcdocument.h"
#include "module/mfcdraftstore.h"
#include "module/mfckeyrecovery.h"
#include "module/mfcwriteplan.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabBar;
class QTabWidget;
class QTableWidget;
class QTimer;
class QToolButton;

class Mifare;

/*
 * MIFARE Classic workbench.
 *
 * One document per tab, each with its own source, unsaved state and view
 * position.  All device access goes through Util; the document model is
 * Mfc::Document.  A task is bound to the document that started it, so late
 * output can never be applied to another tab's card.
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
    quint64 activeSession() const { return targetSession; }
    QString targetUid() const { return targetUidValue; }
    bool hasTarget() const { return targetPresent; }
    bool documentMatchesTarget() const;
    bool applyCardIdentity(const QString& uid, Mfc::CardType type, QString* error = nullptr);
    bool startNewDocumentForTarget(QString* error = nullptr);
    void suspendSessionForOneShot();
    const Mfc::WritePlan& currentWritePlan() const { return plan; }

    // --- documents ------------------------------------------------------
    int documentCount() const { return tabs.size(); }
    int currentDocumentIndex() const { return activeIndex; }
    int addDocument(const Mfc::Document& document, const QString& name,
                    const QString& sourceKind, const QString& sourcePath = QString());
    void activateDocument(int index);
    QString documentTabName(int index) const;
    QString documentTabSource(int index) const;
    bool documentTabDirty(int index) const;
    void closeDocument(int index);
    // Drafts are stored apart from the backup library.  A restored draft is
    // marked as historical evidence: its session ids never apply to the card
    // in the field.
    void setDraftStoreDir(const QString& dir);
    void setLibraryRoot(const QString& dir) { libraryRoot = dir; }
    int draftCount() const { return drafts.drafts().size(); }
    QString draftIdAt(int index) const
    {
        return index >= 0 && index < drafts.drafts().size() ? drafts.drafts().at(index).id : QString();
    }
    bool restoreDraft(const QString& id, QString* error = nullptr);

    // Test/automation hooks: no modal dialogs, explicit paths.
    bool importKeyFile(const QString& filename, const QString& layout, QString* error = nullptr);
    void setConfirmWrites(bool on) { confirmWrites = on; }
    bool saveBackupTo(const QString& filename, QString* error = nullptr, bool allowUnknownBin = false);
    bool loadCompareFile(bool sideA, const QString& filename, QString* error = nullptr);
    Mfc::CompareResult currentComparison() const { return comparison; }
    void setCompareDocuments(const Mfc::Document& a, const Mfc::Document& b,
                             const QString& nameA = QString(), const QString& nameB = QString());
    QByteArray viewState() const;
    void setViewState(const QByteArray& state);
    void locateBlock(int block);
    void markDirty();
    bool undoDocument();
    bool redoDocument();
    // Automation hook: run the batch queue without a dialog.
    bool startBatchRecovery(MfcRecovery::Method method, QString* error = nullptr);

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
    // A document was opened for another page (library, scan, file).
    void documentActivated(const Mfc::Document& document, const QString& name,
                           const QString& sourceKind);
    void identityChanged(const QString& uid, const QString& cardName, const QString& documentName);

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

    // documents
    void newDocumentTab();
    void openDocumentTab();
    void closeCurrentDocument();
    void renameCurrentDocument();
    void restoreDraftDialog();
    void documentTabChanged(int index);
    void documentTabCloseRequested(int index);
    void documentTabContextMenu(const QPoint& pos);
    void saveCurrentDocument();
    void flushDrafts();

    // data table
    void refreshBlockDetails();
    void copyBlockHex();
    void pasteBlocks();
    void exportSelectedBlocks();
    void linkActivated(const QString& link);

    // batch recovery
    void startBatchFromDialog();
    void batchNext();
    void batchAfterCommand(Util::ExecStatus status);
    void retryMissingKeys();
    void readRemainingBlocks();

    // continuous scan
    void scanToggled(bool on);
    void scanTick();
    void scanItemChanged();

private:
    struct DocumentTab
    {
        Mfc::Document document;
        QString name;
        QString sourceKind; // "card", "file", "scan", "library", "draft", "new"
        QString sourcePath;
        bool dirty = false;
        int scroll = 0;
        int selectedBlock = -1;
        int round = 0;
        QString filterData = QStringLiteral("all");
        QString draftId;
        QString search;
        QHash<int, QString> lastOperation;
        QVector<Mfc::Document> undo;
        QVector<Mfc::Document> redo;
    };

    void buildUi();
    void refreshKeyTable();
    void refreshDataTable();
    void refreshAdvice();
    void refreshSummary();
    void refreshDiffTable();
    void refreshDocumentTabs();
    void refreshScanList();
    void setTaskRunning(bool running);
    void logLine(const QString& text);
    void setStatus(const QString& text);
    QString cardTypeName() const;
    QString keyStateText(Mfc::KeyState state) const;
    QString blockStateText(Mfc::BlockState state) const;
    QString nonceText() const;
    bool chooseVerifiedKey(int sector, char* type, QString* key) const;
    bool queryCardIdentity(QString* uid, Mfc::CardType* type, QString* error);
    void confirmOneShotSession();
    bool ensureCardIdentified();
    void diagnoseNonceOnOpen();
    QString trailerReadback(int block, char authType, const QString& cardHex) const;
    bool readBlockOnce(int block, char keyType, const QString& key, QString* hex, QString* error);
    bool loadDocumentFile(const QString& filename, Mfc::Document* out, QString* error) const;
    Mfc::Document snapshotDocument() const;
    bool loadCompareFromLibrary(bool sideA, QString* error = nullptr);
    bool verifyOneKey(int sector, char type, const QString& key);
    Mfc::WriteOutcome writeBlockOnce(int block, const QString& hex,
                                     const Mfc::TrailerPlan* trailer, QString* detail,
                                     QString* readBackOut = nullptr);
    void startMethod(MfcRecovery::Method method, const MfcRecovery::Command& command,
                     bool batchStep = false);
    MfcRecovery::Command commandForMethod(MfcRecovery::Method method, QString* error) const;
    void applyParsedResult(const MfcRecovery::ParseResult& parsed, MfcRecovery::Method method,
                           Util::ExecStatus status);
    void appendLog(const QString& text);
    void invalidateTargetWhenDisconnected();
    void refreshIdentity();

    // documents
    void saveActiveView();
    void loadActiveDocument();
    void pushUndo();
    void setTabDirty(bool dirty);
    void bindTaskToDocument();
    bool openDocumentFile(const QString& filename, QString* error = nullptr);
    void updateWindowTitle();
    int indexOfDocumentFile(const QString& filename) const;

    Util* util = nullptr;
    Mifare* mifare = nullptr;
    QVariantMap configMap;
    Mfc::Document doc;
    MfcRecovery::NonceType nonce = MfcRecovery::NonceType::Unknown;
    MfcRecovery::Tracker tracker;
    MfcRecovery::Method currentMethod = MfcRecovery::Method::KnownKeys;
    MfcRecovery::Target currentTarget;
    QString taskOutput;
    bool taskRunning = false;
    bool deviceReady = false;
    bool oneShotSessionPending = false;
    bool cardConfirmPending = false;
    bool updatingTables = false;
    bool cancelRequested = false;
    quint64 sessionSeq = 0;
    quint64 targetSession = 0;
    bool targetPresent = false;
    QString targetUidValue;
    Mfc::CardType targetType = Mfc::CardType::M1K;
    quint64 taskSession = 0;
    int taskDocumentIndex = -1;
    Mfc::WritePlan plan;
    bool planBusy = false;
    bool confirmWrites = true;

    QString libraryRoot;

    Mfc::Document compareA;
    Mfc::Document compareB;
    bool hasCompareA = false;
    bool hasCompareB = false;
    Mfc::CompareResult comparison;

    // documents / drafts
    QVector<DocumentTab> tabs;
    int activeIndex = -1;
    Mfc::DraftStore drafts;
    QTimer* draftTimer = nullptr;

    // batch recovery
    MfcBatch::Plan batchPlan;
    MfcRecovery::Method batchMethod = MfcRecovery::Method::KnownKeys;
    bool batchRunning = false;
    bool batchAwaiting = false;
    bool batchTableDone = false;
    int batchCurrent = -1;
    int batchDocumentIndex = -1;
    int batchKnownSector = 0;
    char batchKnownType = 'A';
    QString batchDictionary;

    // continuous scan
    QTimer* scanTimer = nullptr;
    bool scanEnabled = false;
    bool scanAutoRead = false;
    bool scanCardPresent = false;
    QString scanUid;
    int scanDocumentIndex = -1;
    int scanRound = 0;

    QLabel* cardLabel = nullptr;
    QLabel* uidLabel = nullptr;
    QLabel* nonceLabel = nullptr;
    QLabel* keySummary = nullptr;
    QLabel* blockSummary = nullptr;
    QLabel* stageLabel = nullptr;
    QLabel* batchLabel = nullptr;
    QLabel* statusLabel = nullptr;
    QProgressBar* progress = nullptr;
    QComboBox* cardTypeBox = nullptr;
    QComboBox* filterBox = nullptr;
    QComboBox* sectorFilter = nullptr;
    QLineEdit* searchEdit = nullptr;
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
    QTableWidget* scanTable = nullptr;
    QSplitter* dataSplitter = nullptr;
    QPlainTextEdit* logEdit = nullptr;
    QPlainTextEdit* detailView = nullptr;
    QPushButton* runButton = nullptr;
    QPushButton* cancelButton = nullptr;
    QPushButton* batchButton = nullptr;
    QPushButton* scanButton = nullptr;
    QTabWidget* pageTabs = nullptr;
    QTabBar* docTabBar = nullptr;
    QToolButton* docMenuButton = nullptr;
    QTimer* refreshTimer = nullptr;
};

#endif // MFCWORKBENCH_H

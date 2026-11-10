#ifndef UTIL_H
#define UTIL_H

#include <QObject>
#include <QString>
#include <QDebug>
#include <QThread>
#include <QApplication>
#include <QElapsedTimer>
#include <QTime>
#include <QTimer>
#include <QMetaType>
#include <QRegularExpression>
#include <QSettings>
#include <QMainWindow>
#include <QInputDialog>
#include <QFileDialog>
#include <QDockWidget>

#include "ui_mainwindow.h"

class Util : public QObject
{
    Q_OBJECT
public:
    enum ClientType
    {
        CLIENTTYPE_OFFICIAL,
        CLIENTTYPE_ICEMAN,
    };

    enum ExecStatus
    {
        EXEC_OK,          // completed, success marker (or prompt) seen
        EXEC_TIMEOUT,     // no completion before the timeouts expired
        EXEC_FAILED,      // a failure marker was printed, or the command completed without its success marker
        EXEC_NOT_RUNNING, // the client process is not running
        EXEC_ABORTED,     // the client exited while the command was running
        EXEC_BUSY,        // another (blocking or long) command owns the client
        EXEC_CANCELLED,   // the user stopped waiting for this command
    };

    struct ExecResult
    {
        QString output;
        ExecStatus status;
        bool successPatternMatched;
        bool failurePatternMatched;
        bool promptSeen;
        bool completionMarkerSeen;
        ExecResult()
        {
            status = EXEC_NOT_RUNNING;
            successPatternMatched = false;
            failurePatternMatched = false;
            promptSeen = false;
            completionMarkerSeen = false;
        }
        bool isOk() const
        {
            return status == EXEC_OK;
        }
    };

    struct ReturnTrigger
    {
        unsigned long waitTime;      // idle timeout: no new output for this long ends the wait
        unsigned long hardTimeout;   // absolute timeout, 0 = 4 * waitTime (minimum 5s)
        QStringList expectedOutputs; // success patterns (regex)
        QStringList failedOutputs;   // failure patterns (regex), end the wait immediately
        bool requireCompletion;      // wait for a real completion boundary before reporting a result
        bool useCompletionMarker;    // append "rem <token>" so pipe mode has a boundary

        ReturnTrigger(unsigned long time)
        {
            waitTime = time;
            hardTimeout = 0;
            expectedOutputs = QStringList();
            failedOutputs = QStringList();
            requireCompletion = false;
            useCompletionMarker = false;
        }
        ReturnTrigger(const QStringList& outputs)
        {
            waitTime = 10000;
            hardTimeout = 0;
            expectedOutputs = outputs;
            failedOutputs = QStringList();
            requireCompletion = false;
            useCompletionMarker = false;
        }
        ReturnTrigger(unsigned long time, const QStringList& outputs)
        {
            waitTime = time;
            hardTimeout = 0;
            expectedOutputs = outputs;
            failedOutputs = QStringList();
            requireCompletion = false;
            useCompletionMarker = false;
        }
    };

    // A long-running command (recovery, dump).  It has no idle timeout: a CPU
    // phase may print nothing for minutes, so only the client exiting, an
    // explicit failure marker, the completion marker or the hard timeout ends
    // it.  Commands that poll stdin must not set useCompletionMarker.
    struct AsyncCommand
    {
        QString cmd;
        QStringList successPatterns;
        QStringList failurePatterns;
        bool useCompletionMarker = false;
        qint64 hardTimeoutMs = 0; // 0 = no hard timeout
        QString label;
    };

    Q_ENUM(Util::ClientType)
    Q_ENUM(Util::ExecStatus)

    explicit Util(QObject *parent = nullptr);

    void execCMD(const QString& cmd);
    // Long command API.  The caller must keep the GUI responsive and poll
    // commandFinished(); the device stays owned until it arrives.
    bool startCommand(const AsyncCommand& command);
    void cancelCommand(bool abortClient);
    bool isCommandActive() const { return asyncActive; }
    ExecResult execCMDWithResult(const QString& cmd, const ReturnTrigger& trigger = ReturnTrigger(10000), bool rawOutput = false);
    QString execCMDWithOutput(const QString& cmd, ReturnTrigger trigger = 10000, bool rawOutput = false);
    void delay(unsigned int msec);
    static ClientType getClientType();
    static int rawTabIndex;
    static QDockWidget* rawDockPtr;
    static bool chooseLanguage(QSettings *guiSettings, QMainWindow* window = nullptr);
    static QString stripAnsi(const QString& text);
    static bool matchAnyPattern(const QString& text, const QStringList& patterns, QString* matched = nullptr);
    static bool isPromptAtEnd(const QString& text);
    // Unique per command/connection; safe to embed in a "rem <token>" line.
    static QString makeCompletionToken(const QString& prefix = QStringLiteral("PM3GUI"));
    // True only for the client's "remark: <token>" reply, never for the echo.
    static bool hasCompletionMarker(const QString& cleanOutput, const QString& token);
public slots:
    void processOutput(const QString& output);
    static void setClientType(Util::ClientType clientType);
    void setRunningState(bool st);
    static void gotoRawTab();
    static void setUI(Ui::MainWindow *ui);
    static void setRawTab(QDockWidget* dockPtr, int tabIndex);
private:
    bool isRequiringOutput;
    bool isRunning;
    QString* requiredOutput;
    QElapsedTimer timeStamp;
    QTimer* asyncTimer = nullptr;
    AsyncCommand asyncCommand;
    bool asyncActive = false;
    bool asyncCancelRequested = false;
    QString asyncToken;
    int asyncLastLen = 0;
    QElapsedTimer asyncHardTimer;
    void finishAsync(ExecStatus status, bool successPatternMatched = false,
                     bool failurePatternMatched = false, bool completionMarkerSeen = false);

    static ClientType clientType;
    static Ui::MainWindow *ui;
signals:
    void refreshOutput(const QString& output);
    void commandOutput(const QString& output);
    void commandFinished(const Util::ExecResult& result, const QString& output);
    // The GUI must disconnect/reconnect the client (existing Stop semantics).
    void abortClientRequested();
    void write(QString data); // connected to PM3Process::write(QString data);
};

Q_DECLARE_METATYPE(Util::ExecResult)

#endif // UTIL_H

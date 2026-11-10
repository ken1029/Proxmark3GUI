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
    };

    struct ExecResult
    {
        QString output;
        ExecStatus status;
        bool successPatternMatched;
        bool failurePatternMatched;
        bool promptSeen;
        ExecResult()
        {
            status = EXEC_NOT_RUNNING;
            successPatternMatched = false;
            failurePatternMatched = false;
            promptSeen = false;
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
        bool waitForPrompt;          // require the client prompt before reporting completion

        ReturnTrigger(unsigned long time)
        {
            waitTime = time;
            hardTimeout = 0;
            expectedOutputs = QStringList();
            failedOutputs = QStringList();
            waitForPrompt = false;
        }
        ReturnTrigger(const QStringList& outputs)
        {
            waitTime = 10000;
            hardTimeout = 0;
            expectedOutputs = outputs;
            failedOutputs = QStringList();
            waitForPrompt = false;
        }
        ReturnTrigger(unsigned long time, const QStringList& outputs)
        {
            waitTime = time;
            hardTimeout = 0;
            expectedOutputs = outputs;
            failedOutputs = QStringList();
            waitForPrompt = false;
        }
    };

    Q_ENUM(Util::ClientType)
    Q_ENUM(Util::ExecStatus)

    explicit Util(QObject *parent = nullptr);

    void execCMD(const QString& cmd);
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
    static ClientType clientType;
    static Ui::MainWindow *ui;
signals:
    void refreshOutput(const QString& output);
    void write(QString data); // connected to PM3Process::write(QString data);
};

#endif // UTIL_H

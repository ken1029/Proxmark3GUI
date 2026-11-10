#ifndef PM3PROCESS_H
#define PM3PROCESS_H

#include <QProcess>
#include <QThread>
#include <QString>
#include <QDebug>
#include <QTimer>
#include <QtSerialPort/QSerialPortInfo>
#include <QtSerialPort/QSerialPort>
#include <QProcessEnvironment>
#include <QDir>

#include <atomic>

#include "util.h"

class PM3Process : public QProcess
{
    Q_OBJECT
public:
    explicit PM3Process(QThread* thread, QObject* parent = nullptr);
    bool waitForReadyRead(int msecs = 2000);
    // Wall clock of the last connectPM3() run, for the status bar.
    qint64 lastConnectMs = 0;
    // Connect phases: process creation, first version line, completion boundary.
    qint64 lastStartMs = 0;
    qint64 lastVersionMs = 0;
    qint64 lastHandshakeMs = 0;
    // Set from the GUI thread on shutdown so the connect wait loops give up at once.
    // Also invalidates the current/pending attempt.
    void requestStop();
    std::atomic<bool> stopRequested{false};
    // Allocate the attempt id on the GUI thread before queueing connectPM3().
    quint64 beginConnect();
    quint64 currentAttempt() const { return connectAttempt.load(); }

    void testThread();

public slots:
    void connectPM3(quint64 attempt, const QString& path, const QStringList args);
    void setSerialListener(const QString& name, bool state);
    void setSerialListener(bool state);
    qint64 write(QString data);
    void reconnectPM3(quint64 attempt);
    void setProcEnv(const QStringList* env);
    void setWorkingDir(const QString& dir);
    void killPM3(quint64 attempt);
private slots:
    void onTimeout();
    void onReadyRead();
private:
    bool isRequiringOutput;
    QString* requiredOutput; // It only works in this class now
    void setRequiringOutput(bool st);// It only works in this class now
    void stopProcess();
    QTimer* serialListener;
    QSerialPortInfo* portInfo;
    QString currPath;
    QString currPort = "";
    QStringList currArgs;
    QString clientVersion;
    QString firmwareVersion;
    QString targetName;
    std::atomic<quint64> connectAttempt{0};

signals:
    void PM3StatedChanged(bool st, const QString& info, quint64 attempt);
    void newOutput(const QString& output);
    void changeClientType(Util::ClientType);
    void PM3VersionInfo(const QString& clientVersion, const QString& firmwareVersion, const QString& target, bool mismatch, bool clientQtPlot, quint64 attempt);
    void HWConnectFailed(const QString& reason, quint64 attempt);
};

#endif // PM3PROCESS_H

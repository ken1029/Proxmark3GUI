#include "pm3process.h"
#include <QElapsedTimer>
#include <functional>
#include <QRegularExpression>

PM3Process::PM3Process(QThread* thread, QObject* parent): QProcess(parent)
{
    moveToThread(thread);
    setProcessChannelMode(PM3Process::MergedChannels);
    isRequiringOutput = false;
    requiredOutput = new QString();
    serialListener = new QTimer(); // if using new QTimer(this), the debug output will show "Cannot create children for a parent that is in a different thread."
    serialListener->moveToThread(this->thread());// I've tried many ways to creat a QTimer instance, but all of the instances are in the main thread(UI thread), so I have to move it manually
    serialListener->setInterval(1000);
    serialListener->setTimerType(Qt::VeryCoarseTimer);
    connect(serialListener, &QTimer::timeout, this, &PM3Process::onTimeout);
    connect(this, &PM3Process::readyRead, this, &PM3Process::onReadyRead);
    portInfo = nullptr;

    qRegisterMetaType<QProcess::ProcessError>("QProcess::ProcessError");
}

static QString baseVersion(const QString& version)
{
    QString result = version.trimmed();
    int dash = result.indexOf('-');
    if(dash > 0)
        result = result.left(dash);
    return result;
}

void PM3Process::requestStop()
{
    // Invalidate the pending/running attempt before the worker can run it.
    stopRequested.store(true);
    connectAttempt.fetch_add(1);
}

quint64 PM3Process::beginConnect()
{
    stopRequested.store(false);
    return connectAttempt.fetch_add(1) + 1;
}

void PM3Process::connectPM3(quint64 attempt, const QString& path, const QStringList args)
{
    if(attempt != connectAttempt.load() || stopRequested.load())
        return; // cancelled before the worker entered this slot

    QString result;
    Util::ClientType clientType = Util::CLIENTTYPE_OFFICIAL;

    // stash for reconnect
    currPath = path;
    currArgs = args;

    clientVersion.clear();
    firmwareVersion.clear();
    targetName.clear();
    lastConnectMs = 0;
    lastStartMs = 0;
    lastVersionMs = 0;
    lastHandshakeMs = 0;

    // A second connect click must not leave the previous client running.
    if(state() != QProcess::NotRunning)
    {
        kill();
        waitForFinished(2000);
    }

    // using "-f" option to make the client output flushed after every print.
    // single '\r' might appear. Don't use QProcess::Text there or '\r' is ignored.
    setRequiringOutput(true);
    QElapsedTimer startTimer;
    startTimer.start();
    start(path, args, QProcess::Unbuffered | QProcess::ReadWrite);
    if(!waitForStarted(10000))
    {
        lastStartMs = startTimer.elapsed();
        setRequiringOutput(false);
        qDebug() << "connect: client did not start:" << errorString();
        emit PM3StatedChanged(false, QString(), attempt);
        emit HWConnectFailed(tr("The client process did not start: %1").arg(errorString()), attempt);
        return;
    }
    lastStartMs = startTimer.elapsed();

    // Pipe mode has no startup prompt and no empty prompt after a command.
    // Queue a version query and a unique "rem" marker immediately; the marker
    // reply is the only completion boundary the GUI trusts.
    const QString token = Util::makeCompletionToken("PM3GUI_CONNECT");
    requiredOutput->clear();
    QProcess::write(("hw version\nrem " + token + "\n").toLatin1());

    QElapsedTimer handshakeTimer;
    handshakeTimer.start();
    bool versionSeen = false;
    bool completed = false;
    bool aborted = false;
    QString failure;
    while(true)
    {
        if(attempt != connectAttempt.load() || stopRequested.load())
        {
            aborted = true;
            failure = tr("Connection cancelled");
            break;
        }

        const QString clean = Util::stripAnsi(*requiredOutput);
        if(!versionSeen && (clean.contains("[ Client ]") || clean.contains("Client....") || clean.contains("Iceman/")))
        {
            versionSeen = true;
            lastVersionMs = handshakeTimer.elapsed();
        }
        if(Util::hasCompletionMarker(clean, token))
        {
            completed = true;
            break;
        }
        if(state() == QProcess::NotRunning)
        {
            aborted = true;
            failure = tr("The client exited during the handshake");
            break;
        }
        if(handshakeTimer.elapsed() >= 10000)
        {
            failure = tr("Timed out waiting for the client version");
            break;
        }
        waitForReadyRead(50);
    }
    lastHandshakeMs = handshakeTimer.elapsed();

    if(!completed)
    {
        setRequiringOutput(false);
        qDebug() << "connect failed:" << failure << "after" << lastHandshakeMs << "ms";
        const bool userCancelled = aborted && (stopRequested.load() || attempt != connectAttempt.load());
        stopProcess();
        emit PM3StatedChanged(false, QString(), attempt);
        if(!userCancelled)
            emit HWConnectFailed(failure, attempt);
        return;
    }

    result = *requiredOutput;
    setRequiringOutput(false);
    lastConnectMs = lastStartMs + lastHandshakeMs;
    qDebug() << "connect: start=" << lastStartMs << "ms version=" << lastVersionMs
             << "ms boundary=" << lastHandshakeMs << "ms total=" << lastConnectMs << "ms";

    if(attempt != connectAttempt.load() || stopRequested.load())
        return;

    const QString clean = Util::stripAnsi(result);
    // Device present: "    Client.... v4.23346 ..."; offline: "Iceman/master/v4.23346-suspect ..."
    QRegularExpression clientRe("(?:Iceman/[^/\\s]+/)?(v?\\d+(?:\\.\\d+)+(?:[-+][^\\s]*)?)");
    QRegularExpression firmwareRe("OS\\.{2,}\\s*([^\\s]+)");
    QRegularExpression targetRe("Target\\.{2,}\\s*([^\\r\\n]+)");
    const int clientLabelPos = clean.indexOf("Client....");
    const QString clientSection = (clientLabelPos >= 0) ? clean.mid(clientLabelPos) : clean;
    QRegularExpressionMatch match = clientRe.match(clientSection);
    if(match.hasMatch())
        clientVersion = match.captured(1);
    match = firmwareRe.match(clean);
    if(match.hasMatch())
        firmwareVersion = match.captured(1);
    match = targetRe.match(clean);
    if(match.hasMatch())
        targetName = match.captured(1).trimmed();

    // RRG/Iceman clients report v4.x; the official repo is still v3.x.
    if(result.contains("[=]") || clientVersion.startsWith("v4.") || clientVersion.startsWith("4."))
        clientType = Util::CLIENTTYPE_ICEMAN;
    else
        clientType = Util::CLIENTTYPE_OFFICIAL;

    const bool mismatch = !clientVersion.isEmpty() && !firmwareVersion.isEmpty()
                          && baseVersion(clientVersion) != baseVersion(firmwareVersion);
    const bool clientQtPlot = clean.contains("QT GUI support............ present");

    emit changeClientType(clientType);
    emit PM3VersionInfo(clientVersion, firmwareVersion, targetName, mismatch, clientQtPlot, attempt);

    if(!firmwareVersion.isEmpty())
    {
        if(attempt == connectAttempt.load() && !stopRequested.load())
            emit PM3StatedChanged(true, firmwareVersion, attempt);
    }
    else
    {
        qDebug() << "unexpected output:" << (clean.isEmpty() ? "(empty)" : clean);
        stopProcess();
        emit PM3StatedChanged(false, QString(), attempt);
        if(attempt == connectAttempt.load() && !stopRequested.load())
            emit HWConnectFailed(tr("The client reported no firmware version (offline?)"), attempt);
    }
}

void PM3Process::reconnectPM3(quint64 attempt)
{
    connectPM3(attempt, currPath, currArgs);
}

void PM3Process::setRequiringOutput(bool st)
{
    isRequiringOutput = st;
    if(isRequiringOutput)
        requiredOutput->clear();
}

bool PM3Process::waitForReadyRead(int msecs)
{
    return QProcess::waitForReadyRead(msecs);
}

void PM3Process::setSerialListener(const QString& name, bool state)
{
    if(state)
    {
        currPort = name;
        portInfo = new QSerialPortInfo(name);
        serialListener->start();
        qDebug() << serialListener->thread();
    }
    else
    {
        serialListener->stop();
        if(portInfo != nullptr)
        {
            delete portInfo;
            portInfo = nullptr;
        }
    }
}

void PM3Process::setSerialListener(bool state)
{
    setSerialListener(currPort, state);
}

void PM3Process::onTimeout() //when the proxmark3 client is unexpectedly terminated or the PM3 hardware is removed, the isBusy() will return false(only tested on Windows);
{
//    isBusy() is a deprecated function because it will block the serial port when the port is not in use.
//    However, the PM3 client is supposed to use the target serial port exclusively, so it should be fine
//    isBusy() will always return false on Raspbian, in this case, check "Keep the client active" in the Settings panel.
//
//    qDebug()<<portInfo->isBusy();
    if(!portInfo->isBusy())
    {
        killPM3(connectAttempt.load());
    }
}

void PM3Process::testThread()
{
    qDebug() << "PM3:" << QThread::currentThread();
}

qint64 PM3Process::write(QString data)
{
    return QProcess::write(data.toLatin1());
}

void PM3Process::onReadyRead()
{
    QString out = readAll();
    if(isRequiringOutput)
        requiredOutput->append(out);
    if(out != "")
    {
//        qDebug() << "PM3Process::onReadyRead:" << out;
        emit newOutput(out);

    }
}

void PM3Process::setProcEnv(const QStringList* env)
{
//    qDebug() << "passed Env List" << *env;
    this->setEnvironment(*env);
    //    qDebug() << "final Env List" << processEnvironment().toStringList();
}

void PM3Process::setWorkingDir(const QString& dir)
{
    // the working directory cannot be the default, or the client will failed to load the dll
    this->setWorkingDirectory(dir);
}

void PM3Process::stopProcess()
{
    if(state() != QProcess::NotRunning)
    {
        kill();
        waitForFinished(2000);
    }
    setSerialListener(false);
}

void PM3Process::killPM3(quint64 attempt)
{
    stopProcess();
    emit PM3StatedChanged(false, QString(), attempt);
}

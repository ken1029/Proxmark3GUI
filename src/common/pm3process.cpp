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
    stopRequested.store(true);
}

void PM3Process::connectPM3(const QString& path, const QStringList args)
{
    stopRequested.store(false);
    QString result;
    Util::ClientType clientType = Util::CLIENTTYPE_OFFICIAL;

    // stash for reconnect
    currPath = path;
    currArgs = args;

    clientVersion.clear();
    firmwareVersion.clear();
    targetName.clear();

    // using "-f" option to make the client output flushed after every print.
    // single '\r' might appear. Don't use QProcess::Text there or '\r' is ignored.
    setRequiringOutput(true);
    start(path, args, QProcess::Unbuffered | QProcess::ReadWrite);
    if(!waitForStarted(10000))
    {
        setRequiringOutput(false);
        return;
    }

    // Wait for the client prompt (or the hardware version block) instead of
    // assuming the first read already contains everything.
    // 50 ms slices: a 200 ms slice per read is the granularity the user sees as
    // "the connect takes ages" once the client has already printed the prompt.
    QElapsedTimer startupTimer;
    startupTimer.start();
    auto waitForOutput = [this](const std::function<bool(const QString&)>& done, int timeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while(!done(*requiredOutput))
        {
            if(stopRequested.load() || state() == QProcess::NotRunning || timer.elapsed() >= timeoutMs)
                return false;
            waitForReadyRead(50);
        }
        return true;
    };

    waitForOutput([](const QString& out) { return out.contains("-->") && out.contains("\n"); }, 10000);
    result = *requiredOutput;

    // Ask for the version block. It works offline too (client version only).
    const QString startupOutput = result;
    requiredOutput->clear();
    write("hw version\n");
    waitForOutput([](const QString& out) { return Util::isPromptAtEnd(Util::stripAnsi(out)); }, 10000);
    result = *requiredOutput;
    setRequiringOutput(false);
    lastConnectMs = startupTimer.elapsed();
    qDebug() << "connect: client ready in" << lastConnectMs << "ms";
    result = startupOutput + result;

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
    emit PM3VersionInfo(clientVersion, firmwareVersion, targetName, mismatch, clientQtPlot);

    if(!firmwareVersion.isEmpty())
    {
        emit PM3StatedChanged(true, firmwareVersion);
    }
    else
    {
        qDebug() << "unexpected output:" << (clean.isEmpty() ? "(empty)" : clean);
        emit HWConnectFailed();
        kill();
    }

    setRequiringOutput(false);
}

void PM3Process::reconnectPM3()
{
    connectPM3(currPath, currArgs);
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
        killPM3();
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

void PM3Process::killPM3()
{
    kill();
    waitForFinished(2000);
    emit PM3StatedChanged(false);
    setSerialListener(false);
}

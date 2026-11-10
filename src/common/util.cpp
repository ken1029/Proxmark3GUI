#include "util.h"

#include <atomic>

Util::ClientType Util::clientType = CLIENTTYPE_OFFICIAL;

int Util::rawTabIndex = 0;
QDockWidget* Util::rawDockPtr = nullptr;
Ui::MainWindow* Util::ui = nullptr;


Util::Util(QObject *parent) : QObject(parent)
{
    isRequiringOutput = false;
    isRunning = false;
    requiredOutput = new QString();
    timeStamp.start();
    qRegisterMetaType<Util::ClientType>("Util::ClientType");
    qRegisterMetaType<Util::ExecStatus>("Util::ExecStatus");
    qRegisterMetaType<Util::ExecResult>("Util::ExecResult");

    asyncTimer = new QTimer(this);
    asyncTimer->setInterval(100);
    connect(asyncTimer, &QTimer::timeout, this, &Util::onAsyncTimer);
}

void Util::onAsyncTimer()
{
    const QString clean = stripAnsi(*requiredOutput);
    if(!asyncActive)
    {
        // The caller was told the result already; the channel stays reserved
        // until the client proves it finished the command (late marker or the
        // prompt printed after it) or the process is gone
        // (setRunningState(false)).
        if(!channelOwned)
            return;
        const bool newOutput = requiredOutput->size() > asyncDrainStartLen;
        const bool marker = !drainToken.isEmpty() && hasCompletionMarker(clean, drainToken);
        const bool prompt = newOutput && isPromptAtEnd(clean);
        if(marker || prompt)
        {
            qDebug() << "late completion boundary; releasing the client channel";
            releaseChannel();
        }
        return;
    }

    const bool failure = matchAnyPattern(clean, asyncCommand.failurePatterns);
    const bool success = matchAnyPattern(clean, asyncCommand.successPatterns);
    const bool marker = asyncCommand.useCompletionMarker && hasCompletionMarker(clean, asyncToken);

    if(failure)
    {
        finishAsync(EXEC_FAILED, success, true, marker);
        return;
    }
    if(!isRunning && !isolatedActive)
    {
        finishAsync(EXEC_ABORTED, success, false, marker);
        return;
    }
    if(marker)
    {
        // Empty success patterns mean "the command itself completed"; the
        // caller inspects the parsed table to decide partial/completed.
        finishAsync((success || asyncCommand.successPatterns.isEmpty()) ? EXEC_OK : EXEC_FAILED,
                    success, false, true);
        return;
    }
    if(!asyncCommand.useCompletionMarker && success)
    {
        finishAsync(EXEC_OK, true, false, false);
        return;
    }
    if(asyncCommand.hardTimeoutMs > 0 && asyncHardTimer.elapsed() >= asyncCommand.hardTimeoutMs)
    {
        finishAsync(EXEC_TIMEOUT, success, false, marker);
        // A one-shot client is killed by the GUI; a persistent one may still be
        // computing, so the channel stays owned until it returns or exits.
        if(isolatedActive)
            emit isolatedAbortRequested();
    }
}

void Util::releaseChannel()
{
    const bool wasOwned = channelOwned;
    channelOwned = false;
    drainToken.clear();
    isRequiringOutput = false;
    if(!asyncActive)
        asyncTimer->stop();
    if(wasOwned)
        emit channelStateChanged(false);
}

void Util::isolatedFinished(ExecStatus status)
{
    if(!isolatedActive)
        return;
    const bool cancelled = asyncCancelRequested;
    finishAsync(cancelled ? EXEC_CANCELLED : status, false, false, false);
    isolatedActive = false;
    // The one-shot process has exited: the port and the channel are free.
    releaseChannel();
}


void Util::processOutput(const QString& output)
{
//    qDebug() << "Util::processOutput:" << output;
    if(isRequiringOutput)
    {
        requiredOutput->append(output);
        timeStamp.restart();
    }
    if(asyncActive)
        emit commandOutput(output);
    emit refreshOutput(output);
}

void Util::execCMD(const QString& cmd)
{
    qDebug() << "executing: " << cmd;
    if(channelOwned || asyncActive || isRequiringOutput)
    {
        qWarning() << "ignoring command while the client channel is owned:" << cmd;
        return;
    }
    if(isRunning)
        emit write(cmd + "\n");
}

bool Util::startCommand(const AsyncCommand& command)
{
    if(asyncActive || channelOwned || isRequiringOutput)
    {
        qWarning() << "cannot start long command:" << command.cmd
                   << "busy=" << asyncActive << "owned=" << channelOwned;
        return false;
    }
    if(!command.isolated && !isRunning)
    {
        qWarning() << "cannot start long command, the client is not running:" << command.cmd;
        return false;
    }
    asyncCommand = command;
    asyncActive = true;
    asyncCancelRequested = false;
    isolatedActive = command.isolated;
    channelOwned = true;
    asyncSessionId = ++sessionCounter;
    asyncToken = command.useCompletionMarker ? makeCompletionToken("PM3GUI_TASK") : QString();
    drainToken.clear();
    asyncLastLen = 0;
    asyncHardTimer.start();

    isRequiringOutput = true;
    requiredOutput->clear();
    timeStamp.start();

    qDebug() << "long task:" << command.cmd << "marker=" << asyncToken
             << "isolated=" << command.isolated;
    if(command.isolated)
    {
        // No stdin is queued behind the command: the one-shot client process
        // exits when the command is done.
        emit channelStateChanged(true);
        emit isolatedRunRequested(command.cmd, command.label);
    }
    else
    {
        QString wire = command.cmd + "\n";
        // Only safe for commands that do not poll stdin.  The client executes
        // this line after the command returned to its command loop.
        if(!asyncToken.isEmpty())
            wire += "rem " + asyncToken + "\n";
        emit write(wire);
        emit channelStateChanged(true);
    }

    asyncTimer->start();
    return true;
}

void Util::cancelCommand(bool abortClient)
{
    if(!asyncActive)
        return;
    asyncCancelRequested = true;
    if(isolatedActive)
    {
        // Killing the one-shot process is the only way to stop it; the port is
        // released by the process exit (isolatedFinished()).
        emit isolatedAbortRequested();
        return;
    }
    if(abortClient)
        emit abortClientRequested();
    // The already received output belongs to the caller; preserve it.  The
    // channel stays owned: the client may still be executing the command, and
    // nothing may be queued behind it until the completion boundary shows up.
    drainToken = asyncToken;
    asyncDrainStartLen = requiredOutput->size();
    finishAsync(EXEC_CANCELLED, false, false, false);
}

void Util::finishAsync(ExecStatus status, bool successPatternMatched,
                       bool failurePatternMatched, bool completionMarkerSeen)
{
    if(!asyncActive)
        return;
    asyncActive = false;
    // Keep capturing output while the channel is owned: late output belongs to
    // this task and must not leak into the next one.
    if(!channelOwned)
        isRequiringOutput = false;
    if(!channelOwned)
        asyncTimer->stop();

    ExecResult result;
    result.output = *requiredOutput;
    result.status = status;
    result.successPatternMatched = successPatternMatched;
    result.failurePatternMatched = failurePatternMatched;
    result.completionMarkerSeen = completionMarkerSeen;
    result.promptSeen = false;
    qDebug() << "long task finished:" << status << "cancel=" << asyncCancelRequested
             << "bytes=" << result.output.size() << "owned=" << channelOwned;

    // A command that reached a proven boundary releases the channel.  A
    // cancelled or timed-out command does not: the client may still be
    // executing it, so the channel stays owned until the late marker, the
    // prompt or the process exit proves otherwise.
    if(!isolatedActive
       && (status == EXEC_OK || status == EXEC_ABORTED || status == EXEC_NOT_RUNNING))
        releaseChannel();

    emit commandFinished(result, result.output);
}

QString Util::stripAnsi(const QString& text)
{
    // CSI sequences (colors, cursor movement) and OSC sequences
    static const QRegularExpression csi("\\x1b\\[[0-9;?]*[ -/]*[@-~]");
    static const QRegularExpression osc("\\x1b\\][^\\x07]*(?:\\x07|\\x1b\\\\)");
    QString result = text;
    result.remove(osc);
    result.remove(csi);
    return result;
}

bool Util::matchAnyPattern(const QString& text, const QStringList& patterns, QString* matched)
{
    for(const QString& pattern : patterns)
    {
        if(pattern.isEmpty())
            continue;
        QRegularExpression re(pattern, QRegularExpression::DotMatchesEverythingOption);
        QRegularExpressionMatch match = re.match(text);
        if(match.hasMatch())
        {
            if(matched != nullptr)
                *matched = match.captured();
            return true;
        }
    }
    return false;
}

bool Util::isPromptAtEnd(const QString& text)
{
    // v4.23346 prompt: "[usb] pm3 --> ", "[offline] pm3 --> ", "[usb|script] pm3 --> "
    static const QRegularExpression prompt("(?:^|[\\r\\n])[ \\t]*(?:\\[[^\\]\\r\\n]*\\][ \\t]*)?pm[35][ \\t]*-->[ \\t]*$");
    return prompt.match(text).hasMatch();
}

QString Util::makeCompletionToken(const QString& prefix)
{
    static std::atomic<quint64> counter(0);
    const quint64 id = counter.fetch_add(1) + 1;
    return QString("%1_%2_%3").arg(prefix).arg(QCoreApplication::applicationPid()).arg(id);
}

bool Util::hasCompletionMarker(const QString& cleanOutput, const QString& token)
{
    if(token.isEmpty())
        return false;
    // The client echoes "rem <token>"; only "remark: <token>" proves execution.
    static const QString suffix = "(?![A-Za-z0-9_])";
    QRegularExpression marker("remark[ \\t]*:[ \\t]*" + QRegularExpression::escape(token) + suffix);
    return marker.match(cleanOutput).hasMatch();
}

Util::ExecResult Util::execCMDWithResult(const QString& cmd, const ReturnTrigger& trigger, bool rawOutput)
{
    Q_UNUSED(rawOutput)
    ExecResult res;
    if(asyncActive || channelOwned)
    {
        res.status = EXEC_BUSY;
        return res;
    }
    if(!isRunning)
    {
        res.status = EXEC_NOT_RUNNING;
        return res;
    }

    // The synchronous command owns the channel for its whole run: a nested
    // command from an event handler during processEvents() is refused instead
    // of interleaving with the one that is waiting for its output.
    channelOwned = true;
    emit channelStateChanged(true);
    isRequiringOutput = true;
    requiredOutput->clear();
    timeStamp.start();
    const QString token = trigger.useCompletionMarker ? makeCompletionToken() : QString();
    qDebug() << "executing: " << cmd;
    QString wire = cmd + "\n";
    if(!token.isEmpty())
        wire += "rem " + token + "\n";
    emit write(wire);

    const qint64 idleLimit = qMax<qint64>((qint64)trigger.waitTime, 10);
    const qint64 hardLimit = trigger.hardTimeout ? (qint64)trigger.hardTimeout : qMax<qint64>(idleLimit * 4, 5000);
    QElapsedTimer hardTimer;
    hardTimer.start();

    int lastLen = -1;
    bool successMatched = false;
    bool failureMatched = false;
    bool promptSeen = false;
    bool completionMarkerSeen = false;
    bool clientExited = false;

    while(true)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 50);
        if(requiredOutput->size() != lastLen)
        {
            lastLen = requiredOutput->size();
            timeStamp.restart();
        }

        const QString clean = stripAnsi(*requiredOutput);

        if(!failureMatched && matchAnyPattern(clean, trigger.failedOutputs))
            failureMatched = true;
        if(!successMatched && matchAnyPattern(clean, trigger.expectedOutputs))
            successMatched = true;
        if(!completionMarkerSeen)
            completionMarkerSeen = hasCompletionMarker(clean, token);
        // An empty prompt is only a boundary for the explicit interactive mode.
        // In marker mode the client may split the echo after "pm3 --> ", so a
        // prompt match there is not a completed command.
        if(trigger.requireCompletion && !trigger.useCompletionMarker && isPromptAtEnd(clean))
            promptSeen = true;

        if(failureMatched)
            break;
        if(trigger.useCompletionMarker)
        {
            if(completionMarkerSeen)
                break;
        }
        else if(trigger.requireCompletion)
        {
            if(promptSeen)
                break;
        }
        else if(successMatched)
        {
            delay(200);
            break;
        }
        if(!isRunning)
        {
            clientExited = true;
            break;
        }
        if(timeStamp.elapsed() >= idleLimit || hardTimer.elapsed() >= hardLimit)
            break;
    }

    res.output = *requiredOutput;
    res.successPatternMatched = successMatched;
    res.failurePatternMatched = failureMatched;
    res.promptSeen = promptSeen;
    res.completionMarkerSeen = completionMarkerSeen;

    if(failureMatched)
        res.status = EXEC_FAILED;
    else if(trigger.useCompletionMarker)
        res.status = completionMarkerSeen
                     ? ((successMatched || trigger.expectedOutputs.isEmpty()) ? EXEC_OK : EXEC_FAILED)
                     : (clientExited ? EXEC_ABORTED : EXEC_TIMEOUT);
    else if(trigger.requireCompletion)
        res.status = promptSeen
                     ? ((successMatched || trigger.expectedOutputs.isEmpty()) ? EXEC_OK : EXEC_FAILED)
                     : (clientExited ? EXEC_ABORTED : EXEC_TIMEOUT);
    else if(successMatched)
        res.status = EXEC_OK; // legacy short command: the success line is its own boundary
    else if(clientExited)
        res.status = EXEC_ABORTED;
    else
        res.status = EXEC_TIMEOUT;

    // A short command that timed out may still be running in the client.  If it
    // was given a completion protocol (a marker or the interactive prompt), the
    // boundary will arrive when the client returns to its command loop, so the
    // channel stays reserved until then.  A legacy idle-timeout read has no
    // boundary to wait for at all; releasing is the only honest option there.
    const bool boundaryPossible = trigger.useCompletionMarker || trigger.requireCompletion;
    const bool proven = res.status == EXEC_OK || res.status == EXEC_FAILED
                        || res.status == EXEC_ABORTED || res.status == EXEC_NOT_RUNNING;
    if(proven || !boundaryPossible)
        releaseChannel();
    else
    {
        drainToken = token;
        asyncDrainStartLen = requiredOutput->size();
        asyncTimer->start();
    }
    return res;
}

QString Util::execCMDWithOutput(const QString& cmd, ReturnTrigger trigger, bool rawOutput)
{
    // if the trigger is empty, this function will wait trigger.waitTime then return all outputs during the wait time.
    // otherwise, this function will return empty string if no trigger is detected, or return outputs if any trigger is detected.
    // the waitTime will be refreshed if the client have new outputs
    ExecResult result = execCMDWithResult(cmd, trigger, rawOutput);

    // For functions without expected outputs in the return trigger, the result is the raw output.
    // For functions with expected outputs in the return trigger,
    // if rawOutput=true, the result is the raw output,
    // otherwise, if the status is OK, the result is the raw output,
    // otherwise, the result is empty(as a failed flag).
    if(trigger.expectedOutputs.isEmpty() || result.isOk() || rawOutput)
        return result.output;
    return "";
}

void Util::delay(unsigned int msec)
{
    QElapsedTimer timer;
    timer.start();
    while(timer.elapsed() < (qint64)msec)
        QApplication::processEvents(QEventLoop::AllEvents, 100);
}

Util::ClientType Util::getClientType()
{
    return Util::clientType;
}

void Util::setClientType(Util::ClientType clientType)
{
    Util::clientType = clientType;
}

void Util::setRunningState(bool st)
{
    this->isRunning = st;
    if(!st)
    {
        // The client process is gone: the device and the channel are free, and
        // no command can still be executing.
        releaseChannel();
    }
}

bool Util::chooseLanguage(QSettings* guiSettings, QMainWindow* window)
{
    // make sure the GUISettings is not in any group
    QSettings* langSettings = new QSettings(":/i18n/languages.ini", QSettings::IniFormat);
    QMap<QString, QString> langMap;
    langSettings->setIniCodec("UTF-8");
    langSettings->beginGroup("Languages");
    QStringList langList = langSettings->allKeys();
    for(int i = 0; i < langList.size(); i++)
        langMap.insert(langSettings->value(langList[i]).toString(), langList[i]);
    langMap.insert(tr("Load from external file"), "(ext)");
    langSettings->endGroup();
    delete langSettings;
    bool isOk = false;
    QString selectedText = QInputDialog::getItem(window, "", tr("Choose a language:"), langMap.keys(), 0, false, &isOk);
    if(!isOk)
        return false;
    if(langMap[selectedText] == "(ext)")
    {
        QString extPath = QFileDialog::getOpenFileName(window, tr("Select the translation file:"));
        if(extPath.isEmpty())
            return false;

        guiSettings->beginGroup("language");
        guiSettings->setValue("extPath", extPath);
        guiSettings->endGroup();
    }

    guiSettings->beginGroup("language");
    guiSettings->setValue("name", langMap[selectedText]);
    guiSettings->endGroup();
    guiSettings->sync();

    return isOk;
}

void Util::gotoRawTab()
{
    Util::ui->funcTab->setCurrentIndex(Util::rawTabIndex);
    Util::rawDockPtr->setVisible(true);
    Util::rawDockPtr->raise();
}

void Util::setUI(Ui::MainWindow *ui)
{
    Util::ui = ui;
}


void Util::setRawTab(QDockWidget *dockPtr, int tabIndex)
{
    Util::rawDockPtr = dockPtr;
    Util::rawTabIndex = tabIndex;
}

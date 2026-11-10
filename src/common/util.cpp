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
}


void Util::processOutput(const QString& output)
{
//    qDebug() << "Util::processOutput:" << output;
    if(isRequiringOutput)
    {
        requiredOutput->append(output);
        timeStamp.restart();
    }
    emit refreshOutput(output);
}

void Util::execCMD(const QString& cmd)
{
    qDebug() << "executing: " << cmd;
    if(isRunning)
        emit write(cmd + "\n");
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
    if(!isRunning)
    {
        res.status = EXEC_NOT_RUNNING;
        return res;
    }

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

    isRequiringOutput = false;
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

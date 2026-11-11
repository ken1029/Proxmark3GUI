#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QJsonDocument>
#include <QDirIterator>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QSemaphore>
#include <QSharedPointer>

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    clientQtPlot = true;
    ui->setupUi(this);
    dockAllWindows = new QAction(tr("Dock all windows"), this);
    myInfo = new QAction("wh201906", this);
    currVersion = new QAction(tr("Ver: ") + QApplication::applicationVersion().section('.', 0, -2), this); // ignore the 4th version number
    checkUpdate = new QAction(tr("Check Update"), this);
    connect(dockAllWindows, &QAction::triggered, [ = ]()
    {
        for(int i = 0; i < dockList.size(); i++)
            dockList[i]->setFloating(false);
    });
    connect(myInfo, &QAction::triggered, [ = ]()
    {
        QDesktopServices::openUrl(QUrl("https://github.com/wh201906"));
    });
    connect(checkUpdate, &QAction::triggered, [ = ]()
    {
        QDesktopServices::openUrl(QUrl("https://github.com/wh201906/Proxmark3GUI/releases"));
    });

    settings = new QSettings("GUIsettings.ini", QSettings::IniFormat);
    settings->setIniCodec("UTF-8");

    pm3Thread = new QThread(this);
    pm3 = new PM3Process(pm3Thread);
    // Do not quit the thread here: the stack-allocated MainWindow is destroyed
    // after aboutToQuit, and its destructor must still be able to run killPM3
    // in the PM3 thread. Just break a wait that is in flight.
    connect(QApplication::instance(), &QApplication::aboutToQuit, this, [this]() {
        if(pm3 != nullptr)
            pm3->requestStop();
    });
    pm3Thread->start();
    pm3state = false;
    clientWorkingDir = new QDir;

    util = new Util(this);
    Util::setUI(ui);
    mifare = new Mifare(ui, util, this);
    lf = new LF(ui, util, this);
    t55xxTab = new T55xxTab(util);
    connect(lf, &LF::LFfreqConfChanged, this, &MainWindow::onLFfreqConfChanged);
    connect(t55xxTab, &T55xxTab::setParentGUIState, this, &MainWindow::setState);
    ui->funcTab->insertTab(2, t55xxTab, tr("T55xx"));

    keyEventFilter = new MyEventFilter(QEvent::KeyPress);
    resizeEventFilter = new MyEventFilter(QEvent::Resize);

    // hide unused tabs
//    ui->funcTab->removeTab(1);
    ui->funcTab->removeTab(3);
    workbench = new MfcWorkbench(util, mifare, this);
    ui->funcTab->insertTab(2, workbench, tr("MFC workbench"));
    connect(workbench, &MfcWorkbench::abortClientRequested, this, &MainWindow::on_stopButton_clicked);
    connect(workbench, &MfcWorkbench::documentTypeChanged, this, &MainWindow::onMfcDocumentTypeChanged);
    connect(workbench, &MfcWorkbench::statusMessage, this, [this](const QString& message) {
        statusBar()->showMessage(message, 10000);
    });
    connect(workbench, &MfcWorkbench::taskStateChanged, this, [this](bool running) {
        // A one-shot recovery task stops the persistent client and reconnects
        // it when the run ends, so the pages stay disabled while it runs.
        setState(!running && pm3state);
    });
    // One-shot client for commands that poll stdin, and the global channel
    // ownership that keeps every page (Raw, Classic, workbench) blocked while a
    // command may still be executing.
    connect(util, &Util::isolatedRunRequested, this, &MainWindow::runIsolatedCommand);
    connect(util, &Util::isolatedAbortRequested, this, &MainWindow::abortIsolatedCommand);
    connect(util, &Util::channelStateChanged, this, [this](bool owned) {
        if(owned)
            setState(false);
        else if(pm3state)
            setState(true);
    });

    portSearchTimer = new QTimer(this);
    portSearchTimer->setInterval(2000);
    connect(portSearchTimer, &QTimer::timeout, this, &MainWindow::on_portSearchTimer_timeout);
    portSearchTimer->start();

    contextMenu = new QMenu();
    contextMenu->addAction(dockAllWindows);
    contextMenu->addSeparator();
    contextMenu->addAction(myInfo);
    currVersion->setEnabled(false);
    contextMenu->addAction(currVersion);
    contextMenu->addAction(checkUpdate);

}

MainWindow::~MainWindow()
{
    if(isolatedProcess != nullptr)
    {
        isolatedProcess->disconnect(this);
        isolatedProcess->kill();
        isolatedProcess->waitForFinished(2000);
        delete isolatedProcess;
        isolatedProcess = nullptr;
    }
    // Stop the client before the widgets it reports to are destroyed.
    // The QProcess lives in pm3Thread, so killPM3() must run there. Use a
    // queued call with a bounded wait: a blocking invoke would hang forever if
    // the thread had already stopped processing events.
    if(pm3 != nullptr)
    {
        pm3->disconnect(); // no stale signal may reach the dying window
        pm3->requestStop(); // breaks a connect that is still waiting for the marker
        QSharedPointer<QSemaphore> killed(new QSemaphore);
        QMetaObject::invokeMethod(pm3, [this, killed]() {
            pm3->killPM3(pm3->currentAttempt());
            killed->release();
        }, Qt::QueuedConnection);
        if(!killed->tryAcquire(1, 2000))
            qWarning() << "pm3 thread did not process killPM3";
    }
    pm3Thread->quit();
    if(!pm3Thread->wait(3000))
        qWarning() << "pm3 thread did not stop";
    // Both objects are thread-affine and the application is going away; let the OS
    // reclaim them instead of destroying them from the wrong thread.
    pm3Thread->setParent(nullptr);
    delete ui;
}

void MainWindow::loadConfig()
{
    QString filename = ui->Set_Client_configFileBox->currentData().toString();
    if(filename == "(ext)")
        filename = ui->Set_Client_configPathEdit->text();
    qDebug() << "config file:" << filename;
    QFile configList(filename);
    if(!configList.open(QFile::ReadOnly | QFile::Text))
    {
        QMessageBox::information(this, tr("Info"), tr("Failed to load config file"));
        return;
    }

    QByteArray configData = configList.readAll();
    QJsonDocument configJson(QJsonDocument::fromJson(configData));
    mifare->setConfigMap(configJson.object()["mifare classic"].toObject().toVariantMap());
    workbench->setConfigMap(configJson.object()["mifare classic"].toObject().toVariantMap());
    lf->setConfigMap(configJson.object()["lf"].toObject().toVariantMap());
    t55xxTab->setConfigMap(configJson.object()["t55xx"].toObject().toVariantMap());
}

void MainWindow::initUI() // will be called by main.app
{
    ui->retranslateUi(this);
    uiInit();
    signalInit();
    setState(false);
    dockInit();
}

// ******************** basic functions ********************

void MainWindow::on_portSearchTimer_timeout()
{
    QStringList newPortList; // for actural port name
    QStringList newPortNameList; // for display name
    const QString hint = " *";

    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
//        qDebug() << info.isNull() << info.portName() << info.description() << info.serialNumber() << info.manufacturer();
        if(!info.isNull())
        {
            QString idString = (info.description() + info.serialNumber() + info.manufacturer()).toLower();
            QString portName = info.portName();

            newPortList << portName;
            if(info.hasVendorIdentifier() && info.hasProductIdentifier())
            {
                quint16 vid = info.vendorIdentifier();
                quint16 pid = info.productIdentifier();
                if(vid == 0x9AC4 && pid == 0x4B8F)
                    portName += hint;
                else if(vid == 0x2D2D && pid == 0x504D)
                    portName += hint;
            }
            else if(idString.contains("proxmark") || idString.contains("iceman"))
                portName += hint;
            newPortNameList << portName;
        }
    }
    if(newPortList != portList) // update PM3_portBox when available ports changed
    {
        portList = newPortList;
        ui->PM3_portBox->clear();
        int selectId = -1;
        for(int i = 0; i < portList.size(); i++)
        {
            ui->PM3_portBox->addItem(newPortNameList[i], newPortList[i]);
            if(selectId == -1 && newPortNameList[i].endsWith(hint))
                selectId = i;
        }
        if(selectId != -1)
            ui->PM3_portBox->setCurrentIndex(selectId);
    }
}

void MainWindow::on_PM3_connectButton_clicked()
{
    if(connectInFlight)
    {
        qDebug() << "connect already in progress";
        return;
    }
    if(util->isChannelOwned())
    {
        // A command still owns the client/device; a second client must not open
        // the same serial port.
        QMessageBox::information(this, tr("Info"),
                                 tr("A command still owns the client. Wait for it to finish or "
                                    "stop it before connecting again."), QMessageBox::Ok);
        return;
    }
    connectClickTimer.start();
    qDebug() << "Main:" << QThread::currentThread();

    const QComboBox* portBox = ui->PM3_portBox;
    QString port;
    if(portBox->currentText() == portBox->itemText(portBox->currentIndex()))
        // in the list
        port = portBox->currentData().toString();
    else
        // not in the list
        port = portBox->currentText();
    qDebug() << "port:" << port;
    QString startArgs = ui->Set_Client_startArgsEdit->text();
    QString clientPath = ui->PM3_pathBox->currentText();
    QFileInfo clientFile(clientPath);
    bool clientExist = false;

    QStringList extList = {""};
#ifdef Q_OS_WIN
    if(clientFile.suffix().isEmpty())
    {
        QString pathExt = QProcessEnvironment::systemEnvironment().value("pathext");
        extList += pathExt.split(";", Qt::SkipEmptyParts);
        if(extList.size() == 1)
            extList += ".exe";
    }
#endif
    for(const QString& ext : extList)
    {
        QFileInfo executable(clientFile.filePath() + ext);
        if(executable.isFile())
        {
            clientExist = true;
            break;
        }
    }

    if(!clientExist)
    {
        QMessageBox::information(this, tr("Info"), tr("The client path is invalid"), QMessageBox::Ok);
        return;
    }

    // on RRG repo, if no port is specified, the client will search the available port
    if(port == "" && startArgs.contains("<port>")) // has <port>, no port
    {
        QMessageBox::information(this, tr("Info"), tr("Plz choose a port first"), QMessageBox::Ok);
        return;
    }

    if(!startArgs.contains("<port>")) // no <port>
        port = ""; // a symbol

    QStringList args = startArgs.replace("<port>", port).split(' ');
    addClientPath(clientPath);

    QProcess envSetProcess;
    QElapsedTimer envScriptTimer;
    envScriptTimer.start();
    QString envScriptPath = ui->Set_Client_envScriptEdit->text();
    if(envScriptPath.contains("<client dir>"))
        envScriptPath.replace("<client dir>", clientFile.absoluteDir().absolutePath());

    QFileInfo envScript(envScriptPath);
    if(envScript.exists())
    {
        qDebug() << envScript.absoluteFilePath();
        // use the shell session to keep the environment then read it
#ifdef Q_OS_WIN
        // cmd /c "<path>">>nul && set
        envSetProcess.start("cmd", {}, QProcess::Unbuffered | QProcess::ReadWrite | QProcess::Text);
        envSetProcess.write(QString("\"" + envScript.absoluteFilePath() + "\">>nul\n").toLatin1());
        envSetProcess.waitForReadyRead(10000);
        envSetProcess.readAll();
        envSetProcess.write("set\n");
#else
        // need implementation(or test if space works)
        // sh -c '. "<path>">>/dev/null && env'
        envSetProcess.start("sh -c \' . \"" + envScript.absoluteFilePath() + "\">>/dev/null && env");
#endif
        envSetProcess.waitForReadyRead(10000);
        QString envSetResult = QString(envSetProcess.readAll());
#if (QT_VERSION <= QT_VERSION_CHECK(5,14,0))
        clientEnv = envSetResult.split("\n", QString::SkipEmptyParts);
#else
        clientEnv = envSetResult.split("\n", Qt::SkipEmptyParts);
#endif
        if(clientEnv.size() > 2) // the first element is "set" and the last element is the current path
        {
            clientEnv.removeFirst();
            clientEnv.removeLast();
            emit setProcEnv(&clientEnv);
        }
//      qDebug() << "Get Env List" << clientEnv;
    }
    else
        clientEnv.clear();

    clientWorkingDir->setPath(QApplication::applicationDirPath());
    qDebug() << clientWorkingDir->absolutePath();
    clientWorkingDir->mkpath(ui->Set_Client_workingDirEdit->text());
    qDebug() << clientWorkingDir->absolutePath();
    clientWorkingDir->cd(ui->Set_Client_workingDirEdit->text());
    qDebug() << clientWorkingDir->absolutePath();
    emit setWorkingDir(clientWorkingDir->absolutePath());

    capabilities = Pm3Capabilities::load(clientPath, clientWorkingDir->absolutePath());
    if(capabilities.isLoaded())
        qDebug() << "capability manifest:" << capabilities.manifestPath();
    else
        qDebug() << "capability manifest not found, all GUI features stay enabled";
    applyCapabilities();

    loadConfig();
    // Port enumeration (2 s timer) and the isBusy() watchdog run while the client is
    // still starting up; on Windows both block the GUI/PM3 thread and make the
    // connect look slow. Scan stops now, the watchdog starts once we are connected.
    pendingPort = port;
    lastClientPath = clientPath;
    lastClientArgs = args;
    portSearchTimer->stop();
    setStatusBar(connectStatusBar, tr("Connecting..."));
    connectInFlight = true;
    ui->PM3_connectButton->setEnabled(false);
    activeConnectAttempt = pm3->beginConnect();
    qDebug() << "connect: prep=" << connectClickTimer.elapsed() << "ms attempt=" << activeConnectAttempt;
    emit connectPM3(activeConnectAttempt, clientPath, args);

    envSetProcess.kill();
    qDebug() << "connect: env script=" << envScriptTimer.elapsed() << "ms";
}

void MainWindow::onPM3ErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "PM3 Error:" << error << pm3->errorString();
    // connectPM3() reports FailedToStart through HWConnectFailed. Do not reset
    // here on Crashed: the signal can arrive after a new attempt has started.
}

void MainWindow::onPM3HWConnectFailed(const QString& reason, quint64 attempt)
{
    if(attempt != activeConnectAttempt)
    {
        qDebug() << "ignoring stale connect failure" << attempt << activeConnectAttempt;
        return;
    }
    activeConnectAttempt = 0;
    connectInFlight = false;
    ui->PM3_connectButton->setEnabled(true);
    onPM3StateChanged(false, QString(), 0);
    QMessageBox::information(this, tr("Info"), tr("Failed to connect to the hardware") + "\n" + reason);
}

void MainWindow::onPM3StateChanged(bool st, const QString& info, quint64 attempt)
{
    if(attempt != activeConnectAttempt)
    {
        qDebug() << "ignoring stale state" << st << "attempt" << attempt << "active" << activeConnectAttempt;
        return;
    }
    if(st || !connectInFlight)
    {
        connectInFlight = false;
        ui->PM3_connectButton->setEnabled(true);
    }
    pm3state = st;
    if(workbench != nullptr)
        workbench->setDeviceReady(st);
    setState(st);
    if(st == true)
    {
        const qint64 total = connectClickTimer.isValid() ? connectClickTimer.elapsed() : pm3->lastConnectMs;
        const qint64 processMs = pm3->lastStartMs + pm3->lastHandshakeMs;
        const qint64 prepMs = qMax<qint64>(0, total - processMs);
        qDebug() << "connect: prep=" << prepMs << "ms start=" << pm3->lastStartMs
                 << "ms version=" << pm3->lastVersionMs << "ms boundary=" << pm3->lastHandshakeMs
                 << "ms total=" << total << "ms";
        portSearchTimer->stop();
        if(!keepClientActive)
            emit setSerialListener(pendingPort, !pendingPort.isEmpty());
        setStatusBar(PM3VersionBar, info);
        setStatusBar(connectStatusBar, tr("Connected"));
    }
    else
    {
        emit setSerialListener(false);
        portSearchTimer->start();
        setStatusBar(PM3VersionBar, "");
        setStatusBar(connectStatusBar, tr("Not Connected"));
    }
}

void MainWindow::runIsolatedCommand(const QString& cmd, const QString& label)
{
    if(isolatedProcess != nullptr)
    {
        util->isolatedFinished(Util::EXEC_BUSY);
        return;
    }
    if(lastClientPath.isEmpty())
    {
        util->isolatedFinished(Util::EXEC_NOT_RUNNING);
        return;
    }

    // The persistent client owns the serial port; stop it before the one-shot
    // client starts so two processes never open the same COM port.  Remember
    // whether it was connected so the GUI can restore that connection by itself
    // when the one-shot run ends.
    reconnectAfterIsolated = false;
    if(pm3state)
    {
        reconnectAfterIsolated = true;
        on_PM3_disconnectButton_clicked();
    }
    if(pm3state)
    {
        reconnectAfterIsolated = false;
        util->isolatedFinished(Util::EXEC_BUSY);
        return;
    }
    util->delay(300); // let the killed client release the port

    QProcess* process = new QProcess(this);
    isolatedProcess = process;
    const quint64 runId = ++isolatedRunId;
    process->setProcessChannelMode(QProcess::MergedChannels);
    if(clientWorkingDir != nullptr && !clientWorkingDir->absolutePath().isEmpty())
        process->setWorkingDirectory(clientWorkingDir->absolutePath());
    if(!clientEnv.isEmpty())
    {
        QProcessEnvironment env;
        for(const QString& entry : clientEnv)
        {
            const int eq = entry.indexOf(QLatin1Char('='));
            if(eq > 0)
                env.insert(entry.left(eq), entry.mid(eq + 1));
        }
        process->setProcessEnvironment(env);
    }

    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        util->processOutput(QString::fromLocal8Bit(process->readAllStandardOutput()));
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, runId](int exitCode, QProcess::ExitStatus status) {
        if(runId != isolatedRunId)
            return;
        finishIsolatedRun(exitCode, status);
    });
    connect(process, &QProcess::errorOccurred, this, [this, runId](QProcess::ProcessError error) {
        if(runId != isolatedRunId)
            return;
        qWarning() << "one-shot client error:" << error;
    });

    QStringList args = lastClientArgs;
    args << QStringLiteral("-c") << cmd;
    setStatusBar(programStatusBar, tr("Running %1 in a one-shot client").arg(label));
    qDebug() << "one-shot client:" << lastClientPath << args;
    process->start(lastClientPath, args);
}

void MainWindow::abortIsolatedCommand()
{
    if(isolatedProcess == nullptr)
    {
        util->isolatedFinished(Util::EXEC_CANCELLED);
        return;
    }
    isolatedProcess->kill();
}

void MainWindow::finishIsolatedRun(int exitCode, QProcess::ExitStatus status)
{
    QProcess* process = isolatedProcess;
    isolatedProcess = nullptr;
    if(process != nullptr)
    {
        util->processOutput(QString::fromLocal8Bit(process->readAllStandardOutput()));
        process->deleteLater();
    }
    const bool ok = status == QProcess::NormalExit && exitCode == 0;
    qDebug() << "one-shot client finished: exit=" << exitCode << "status=" << status << "ok=" << ok;
    util->isolatedFinished(ok ? Util::EXEC_OK : Util::EXEC_FAILED);

    // The workbench has consumed the one-shot result.  Restore the persistent
    // client that was stopped to free the port, so a recovery run does not end
    // with the GUI disconnected.  The short delay lets Windows release the COM
    // port after the one-shot process exited.
    const bool reconnect = reconnectAfterIsolated;
    reconnectAfterIsolated = false;
    if(reconnect && !QCoreApplication::closingDown())
        QTimer::singleShot(200, this, &MainWindow::reconnectClient);
}

void MainWindow::reconnectClient()
{
    if(pm3state || connectInFlight || lastClientPath.isEmpty()
       || QCoreApplication::closingDown())
        return; // already connected, connecting, or shutting down
    setStatusBar(connectStatusBar, tr("Connecting..."));
    connectInFlight = true;
    ui->PM3_connectButton->setEnabled(false);
    activeConnectAttempt = pm3->beginConnect();
    qDebug() << "reconnect after one-shot client: attempt=" << activeConnectAttempt;
    emit connectPM3(activeConnectAttempt, lastClientPath, lastClientArgs);
}

void MainWindow::on_PM3_disconnectButton_clicked()
{
    // Break an in-flight handshake before the queued killPM3() can run, and
    // invalidate the attempt so queued true/error signals are ignored.
    const quint64 attempt = activeConnectAttempt;
    activeConnectAttempt = 0;
    if(pm3 != nullptr)
        pm3->requestStop();
    connectInFlight = false;
    ui->PM3_connectButton->setEnabled(true);
    onPM3StateChanged(false, QString(), 0);
    emit killPM3(attempt);
    emit setSerialListener(false);
}

void MainWindow::refreshOutput(const QString& output)
{
//    qDebug() << "MainWindow::refresh:" << output;
    ui->Raw_outputEdit->moveCursor(QTextCursor::End);
    ui->Raw_outputEdit->insertPlainText(output);
    ui->Raw_outputEdit->moveCursor(QTextCursor::End);
}

void MainWindow::on_stopButton_clicked()
{
    if(!pm3state)
        on_PM3_disconnectButton_clicked();
    else
    {
        on_PM3_disconnectButton_clicked();
        for(int i = 0; i < 10; i++)
        {
            util->delay(200);
            if(!pm3state)
                break;
        }
        activeConnectAttempt = pm3->beginConnect();
        emit reconnectPM3(activeConnectAttempt);
        emit setSerialListener(!keepClientActive);
    }
}
// *********************************************************

// ******************** raw command ********************

void MainWindow::on_Raw_CMDEdit_textChanged(const QString &arg1)
{
    stashedCMDEditText = arg1;
}

void MainWindow::on_Raw_sendCMDButton_clicked()
{
    util->execCMD(ui->Raw_CMDEdit->text());
    refreshCMD(ui->Raw_CMDEdit->text());
}

void MainWindow::on_Raw_clearOutputButton_clicked()
{
    ui->Raw_outputEdit->clear();
}

void MainWindow::on_Raw_CMDHistoryBox_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked)
    {
        ui->Raw_CMDHistoryWidget->setVisible(true);
        ui->Raw_clearHistoryButton->setVisible(true);
        ui->Raw_CMDHistoryBox->setText(tr("History:"));
    }
    else
    {
        ui->Raw_CMDHistoryWidget->setVisible(false);
        ui->Raw_clearHistoryButton->setVisible(false);
        ui->Raw_CMDHistoryBox->setText("");
    }
}

void MainWindow::on_Raw_clearHistoryButton_clicked()
{
    ui->Raw_CMDHistoryWidget->clear();
}

void MainWindow::on_Raw_CMDHistoryWidget_itemDoubleClicked(QListWidgetItem *item)
{
    ui->Raw_CMDEdit->setText(item->text());
    ui->Raw_CMDEdit->setFocus();
}

void MainWindow::sendMSG() // send command when pressing Enter
{
    if(ui->Raw_CMDEdit->hasFocus())
        on_Raw_sendCMDButton_clicked();
}


void MainWindow::refreshCMD(const QString& cmd)
{
    ui->Raw_CMDEdit->blockSignals(true);
    ui->Raw_CMDEdit->setText(cmd);
    if(cmd != "" && (ui->Raw_CMDHistoryWidget->count() == 0 || ui->Raw_CMDHistoryWidget->item(ui->Raw_CMDHistoryWidget->count() - 1)->text() != cmd))
        ui->Raw_CMDHistoryWidget->addItem(cmd);
    stashedCMDEditText = cmd;
    stashedIndex = -1;
    ui->Raw_CMDEdit->blockSignals(false);
}

void MainWindow::on_Raw_keyPressed(QObject* obj_addr, QEvent& event)
{
    if(event.type() == QEvent::KeyPress)
    {
        QKeyEvent& keyEvent = static_cast<QKeyEvent&>(event);
        if(obj_addr == ui->Raw_CMDEdit)
        {
            if(keyEvent.key() == Qt::Key_Up)
            {
                if(stashedIndex > 0)
                    stashedIndex--;
                else if(stashedIndex == -1)
                    stashedIndex = ui->Raw_CMDHistoryWidget->count() - 1;
            }
            else if(keyEvent.key() == Qt::Key_Down)
            {
                if(stashedIndex < ui->Raw_CMDHistoryWidget->count() - 1 && stashedIndex != -1)
                    stashedIndex++;
                else if(stashedIndex == ui->Raw_CMDHistoryWidget->count() - 1)
                    stashedIndex = -1;
            }
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
            {
                ui->Raw_CMDEdit->blockSignals(true);
                if(stashedIndex == -1)
                    ui->Raw_CMDEdit->setText(stashedCMDEditText);
                else
                    ui->Raw_CMDEdit->setText(ui->Raw_CMDHistoryWidget->item(stashedIndex)->text());
                ui->Raw_CMDEdit->blockSignals(false);
            }
        }
        else if(obj_addr == ui->Raw_outputEdit)
        {
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
                ui->Raw_CMDEdit->setFocus();
        }
    }
}
// *****************************************************

// ******************** mifare ********************
void MainWindow::on_MF_keyWidget_resized(QObject* obj_addr, QEvent& event)
{
    if(obj_addr == ui->MF_keyWidget && event.type() == QEvent::Resize)
    {
        QTableWidget* widget = (QTableWidget*)obj_addr;
        int keyItemWidth = widget->width();
        keyItemWidth -= widget->verticalScrollBar()->width();
        keyItemWidth -= 2 * widget->frameWidth();
        keyItemWidth -= widget->horizontalHeader()->sectionSize(0);
        widget->horizontalHeader()->resizeSection(1, keyItemWidth / 2);
        widget->horizontalHeader()->resizeSection(2, keyItemWidth / 2);
    }
}

void MainWindow::MF_onMFCardTypeChanged(int id, bool st)
{
    MFCardTypeBtnGroup->blockSignals(true);
    qDebug() << id << MFCardTypeBtnGroup->checkedId();
    if(!st)
    {
        int result;
        if(id > MFCardTypeBtnGroup->checkedId()) // id is specified in uiInit() with a proper order, so I can compare the size by id.
        {
            result = QMessageBox::question(this, tr("Info"), tr("Some of the data and key will be cleared.") + "\n" + tr("Continue?"), QMessageBox::Yes | QMessageBox::No);
        }
        else
        {
            result = QMessageBox::Yes;
        }
        if(result == QMessageBox::Yes)
        {
            qDebug() << "Yes";
            mifare->setCardType(MFCardTypeBtnGroup->checkedId());
            MF_widgetReset();
            mifare->data_syncWithDataWidget();
            mifare->data_syncWithKeyWidget();
        }
        else
        {
            qDebug() << "No";
            MFCardTypeBtnGroup->button(id)->setChecked(true);
        }
    }
    MFCardTypeBtnGroup->blockSignals(false);
}

void MainWindow::on_MF_selectAllBox_stateChanged(int arg1)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectAllBox->setTristate(false);
        ui->MF_selectAllBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.block_size; i++)
    {
        ui->MF_dataWidget->item(i, 1)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    ui->MF_selectTrailerBox->setCheckState(ui->MF_selectAllBox->checkState());
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_selectTrailerBox_stateChanged(int arg1)
{
    int selectedSubBlocks = 0;

    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectTrailerBox->setTristate(false);
        ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 1)->setCheckState(ui->MF_selectTrailerBox->checkState());
        selectedSubBlocks = 0;
        for(int j = 0; j < mifare->cardType.blk[i]; j++)
        {
            if(ui->MF_dataWidget->item(j + mifare->cardType.blks[i], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[i])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::PartiallyChecked);
        }
    }

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_data2KeyButton_clicked()
{
    mifare->data_data2Key();
}

void MainWindow::on_MF_key2DataButton_clicked()
{
    mifare->data_key2Data();
}

void MainWindow::on_MF_fillKeysButton_clicked()
{
    mifare->data_fillKeys();
}

void MainWindow::on_MF_trailerDecoderButton_clicked()
{
    decDialog = new MF_trailerDecoderDialog(this);
    decDialog->show();
}

void MainWindow::on_MF_dataWidget_itemChanged(QTableWidgetItem *item)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(item->column() == 0)
    {
        int selectedSectors = 0;
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            ui->MF_dataWidget->item(i + item->row(), 1)->setCheckState(item->checkState());
            qDebug() << i << mifare->cardType.blk[item->row()] << i + item->row() << ui->MF_dataWidget->item(i + item->row(), 1)->text();
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            if(ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->checkState() == Qt::Checked)
            {
                selectedSectors++;
            }
        }
        if(selectedSectors == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedSectors == mifare->cardType.sector_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 1)
    {
        int selectedSubBlocks = 0;
        int selectedBlocks = 0;
        int selectedTrailers = 0;

        for(int i = 0; i < mifare->cardType.block_size; i++)
        {
            if(ui->MF_dataWidget->item(i, 1)->checkState() == Qt::Checked)
                selectedBlocks++;
        }
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            if(ui->MF_dataWidget->item(i + mifare->cardType.blks[Mifare::data_b2s(item->row())], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            int targetBlock = mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1;
            if(ui->MF_dataWidget->item(targetBlock, 1)->checkState() == Qt::Checked)
                selectedTrailers++;
        }
        if(selectedBlocks == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedBlocks == mifare->cardType.block_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[Mifare::data_b2s(item->row())])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedTrailers == 0)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedTrailers == mifare->cardType.sector_size)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 2)
    {
        QString data = item->text().remove(" ").toUpper();
        if(data == "" || mifare->data_isDataValid(data) == Mifare::DATA_NOSPACE)
        {
            mifare->data_setData(item->row(), data);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Data must consists of 32 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithDataWidget(false, item->row());
    }
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}

void MainWindow::on_MF_keyWidget_itemChanged(QTableWidgetItem *item)
{
    if(item->column() == 1)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_A, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_A);
    }
    else if(item->column() == 2)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_B, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_B);
    }
}

void MainWindow::on_MF_File_loadButton_clicked()
{
    QString title = "";
    QString filename = "";
    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the data file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_loadDataFile(filename))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename
                                         + "\n" + mifare->lastIoError());
            }
            else
            {
                MF_widgetReset();
                mifare->data_syncWithDataWidget();
                mifare->data_syncWithKeyWidget();
                if(!mifare->lastIoWarning().isEmpty())
                    QMessageBox::information(this, tr("Info"), mifare->lastIoWarning());
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the key file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Key Files(*.bin *.dump)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            bool loaded = mifare->data_loadKeyFile(filename);
            if(!loaded && mifare->lastIoAmbiguous())
            {
                QMessageBox box(this);
                box.setWindowTitle(tr("Key file layout"));
                box.setText(tr("This binary key file has two possible layouts. Which one is it?"));
                box.setInformativeText(tr("RRG/official client: all key A first, then all key B.\n"
                                          "Legacy GUI: A0 B0 A1 B1 ..."));
                QPushButton* rrg = box.addButton(tr("RRG key.bin"), QMessageBox::AcceptRole);
                QPushButton* legacy = box.addButton(tr("Legacy GUI"), QMessageBox::AcceptRole);
                box.addButton(QMessageBox::Cancel);
                box.exec();
                if(box.clickedButton() == rrg)
                    loaded = mifare->data_loadKeyFile(filename, Mifare::KEYFILE_RRG);
                else if(box.clickedButton() == legacy)
                    loaded = mifare->data_loadKeyFile(filename, Mifare::KEYFILE_LEGACY);
            }
            if(!loaded)
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename
                                         + "\n" + mifare->lastIoError());
            else
            {
                MF_widgetReset();
                mifare->data_syncWithDataWidget();
                mifare->data_syncWithKeyWidget();
                if(!mifare->lastIoWarning().isEmpty())
                    QMessageBox::information(this, tr("Info"), mifare->lastIoWarning());
            }
        }
    }

}

void MainWindow::on_MF_File_saveButton_clicked()
{

    QString title = "";
    QString filename = "";
    QString selectedType = "";
    QString defaultName = mifare->data_getUID();
    if(defaultName != "")
        defaultName += "_";
    defaultName += QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss");

    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the location to save data file:");
        filename = QFileDialog::getSaveFileName(this, title, "./data_" + defaultName, tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveDataFile(filename, selectedType == tr("Binary Data Files(*.bin *.dump)")))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename
                                         + "\n" + mifare->lastIoError());
            }
            else if(!mifare->lastIoWarning().isEmpty())
            {
                QMessageBox::information(this, tr("Info"), mifare->lastIoWarning());
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the location to save key file:");
        filename = QFileDialog::getSaveFileName(this, title, "./key_" + defaultName, tr("Binary Key Files(*.bin *.dump)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveKeyFile(filename, selectedType == tr("Binary Key Files(*.bin *.dump)")))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename
                                         + "\n" + mifare->lastIoError());
            }
            else if(!mifare->lastIoWarning().isEmpty())
            {
                QMessageBox::information(this, tr("Info"), mifare->lastIoWarning());
            }
        }
    }
    qDebug() << filename << selectedType;
}

void MainWindow::on_MF_File_clearButton_clicked()
{
    if(ui->MF_File_keyButton->isChecked())
    {
        mifare->data_clearKey();
        mifare->data_syncWithKeyWidget();
    }
    else if(ui->MF_File_dataButton->isChecked())
    {
        mifare->data_clearData();
        mifare->data_syncWithDataWidget();
    }
}

void MainWindow::on_MF_Attack_infoButton_clicked()
{
    mifare->info();
}

void MainWindow::on_MF_Attack_chkButton_clicked()
{
    setState(false);
    mifare->chk();
    setState(true);
}

void MainWindow::on_MF_Attack_nestedButton_clicked()
{
    setState(false);
    mifare->nested();
    setState(true);
}

void MainWindow::on_MF_Attack_hardnestedButton_clicked()
{
    mifare->hardnested();
}

void MainWindow::on_MF_RW_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne();
    setState(true);
}

void MainWindow::on_MF_RW_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_dumpButton_clicked()
{
    mifare->dump();
}

void MainWindow::on_MF_RW_restoreButton_clicked()
{
    mifare->restore();
}

void MainWindow::on_MF_UID_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_wipeButton_clicked()
{
    mifare->wipeC();
}

void MainWindow::on_MF_UID_aboutUIDButton_clicked()
{
    QString msg;
    msg += tr("    Normally, the Block 0 of a typical Mifare card, which contains the UID, is locked during the manufacture. Users cannot write anything to Block 0 or set a new UID to a normal Mifare card.") + "\n";
    msg += tr("    Chinese Magic Cards(aka UID Cards) are some special cards whose Block 0 are writeable. And you can change UID by writing to it.") + "\n";
    msg += "\n";
    msg += tr("There are two versions of Chinese Magic Cards, the Gen1 and the Gen2.") + "\n";
    msg += tr("    Gen1:") + "\n" + tr("    also called UID card in China. It responses to some backdoor commands so you can access any blocks without password. The Proxmark3 has a bunch of related commands(csetblk, cgetblk, ...) to deal with this type of card, and my GUI also support these commands.") + "\n";
    msg += tr("    Gen2:") + "\n" + tr("    doesn't response to the backdoor commands, which means that a reader cannot detect whether it is a Chinese Magic Card or not by sending backdoor commands.") + "\n";
    msg += "\n";
    msg += tr("There are some types of Chinese Magic Card Gen2.") + "\n";
    msg += tr("    CUID Card:") + "\n" + tr("    the Block 0 is writeable, you can write to this block repeatedly by normal wrbl command.") + "\n";
    msg += tr("    (hf mf wrbl 0 A FFFFFFFFFFFF <the data you want to write>)") + "\n";
    msg += tr("    FUID Card:") + "\n" + tr("    you can only write to Block 0 once. After that, it seems like a typical Mifare card(Block 0 cannot be written to).") + "\n";
    msg += tr("    (some readers might try changing the Block 0, which could detect the CUID Card. In that case, you should use FUID card.)") + "\n";
    msg += tr("    UFUID Card:") + "\n" + tr("    It behaves like a CUID card(or UID card? I'm not sure) before you send some special command to lock it. Once it is locked, you cannot change its Block 0(just like a typical Mifare card).") + "\n";
    msg += "\n";
    msg += tr("    Seemingly, these Chinese Magic Cards are more easily to be compromised by Nested Attack(it takes little time to get an unknown key).") + "\n";
    QMessageBox::information(this, tr("About UID Card"), msg);
}

void MainWindow::on_MF_UID_setParaButton_clicked()
{
    setState(false);
    mifare->setParameterC();
    setState(true);
}

void MainWindow::on_MF_UID_lockButton_clicked()
{
    mifare->lockC();
}

void MainWindow::on_MF_Sim_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_clearButton_clicked()
{
    mifare->wipeE();
}

void MainWindow::on_MF_Sim_simButton_clicked()
{
    mifare->simulate();
}

void MainWindow::on_MF_Sniff_loadButton_clicked() // use a tmp file to support complicated path
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later
    clientTracePath.mkpath(".");

    title = tr("Plz select the trace file:");
    filename = QFileDialog::getOpenFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")" + ";;" + tr("All Files(*.*)"));
    qDebug() << filename;
    if(filename != "")
    {
        QString tmpFile = QString("tmp-gui-%1-%2%3").arg(QCoreApplication::applicationPid())
                              .arg(QDateTime::currentMSecsSinceEpoch()).arg(defaultExtension);
        QString tmpPath = clientTracePath.absoluteFilePath(tmpFile);
        QFile::remove(tmpPath);
        if(QFile::copy(filename, tmpPath))
        {
            mifare->loadSniff(tmpFile);
            QFile::remove(tmpPath);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
        }
    }
}

void MainWindow::on_MF_Sniff_saveButton_clicked()
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later
    clientTracePath.mkpath(".");

    // v4.23346 `trace save` downloads the device trace by default; `--buffer`
    // saves the client side buffer instead. They are different contents, so ask.
    Mifare::TraceSource source = mifare->getTraceSource();
    QMessageBox sourceBox(this);
    sourceBox.setWindowTitle(tr("Save trace"));
    sourceBox.setText(tr("Where should the trace be saved from?"));
    sourceBox.setInformativeText(tr("Device trace: downloads the trace currently stored in the Proxmark (trace save -f).\n"
                                    "Client buffer: saves the trace loaded or listed in the client (trace save --buffer -f)."));
    QPushButton* deviceButton = sourceBox.addButton(tr("Device trace"), QMessageBox::AcceptRole);
    QPushButton* bufferButton = sourceBox.addButton(tr("Client buffer"), QMessageBox::AcceptRole);
    sourceBox.addButton(QMessageBox::Cancel);
    sourceBox.setDefaultButton(source == Mifare::TRACE_SOURCE_DEVICE ? deviceButton : bufferButton);
    sourceBox.exec();
    if(sourceBox.clickedButton() == deviceButton)
        source = Mifare::TRACE_SOURCE_DEVICE;
    else if(sourceBox.clickedButton() == bufferButton)
        source = Mifare::TRACE_SOURCE_BUFFER;
    else
        return;

    title = tr("Plz select the location to save trace file:");
    filename = QFileDialog::getSaveFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")");
    qDebug() << filename;
    if(filename.isEmpty())
        return;

    QString tmpFile = QString("tmp-gui-%1-%2%3").arg(QCoreApplication::applicationPid())
                          .arg(QDateTime::currentMSecsSinceEpoch()).arg(defaultExtension);
    QString tmpPath = clientTracePath.absoluteFilePath(tmpFile);
    QFile::remove(tmpPath);

    bool saved = mifare->saveSniff(tmpFile, source);

    // wait until the client has finished writing (size stable, non-empty)
    QFileInfo tmpInfo(tmpPath);
    qint64 lastSize = -1;
    QElapsedTimer waitTimer;
    waitTimer.start();
    while(saved && waitTimer.elapsed() < 5000)
    {
        tmpInfo.refresh();
        if(tmpInfo.exists() && tmpInfo.size() > 0 && tmpInfo.size() == lastSize)
            break;
        lastSize = tmpInfo.size();
        util->delay(100);
    }
    tmpInfo.refresh();
    saved = saved && tmpInfo.exists() && tmpInfo.size() > 0;

    if(saved)
    {
        QFile srcFile(tmpPath);
        if(srcFile.open(QFile::ReadOnly))
        {
            // QSaveFile commits atomically, the previous file survives a failure
            QSaveFile dstFile(filename);
            if(!dstFile.open(QFile::WriteOnly) || dstFile.write(srcFile.readAll()) < 0 || !dstFile.commit())
                saved = false;
        }
        else
        {
            saved = false;
        }
    }

    QFile::remove(tmpPath);
    if(!saved)
        QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
}

void MainWindow::on_MF_Sniff_sniffButton_clicked()
{
    setState(false);
    mifare->sniff();
    setState(true);
}

void MainWindow::on_MF_14aSniff_snoopButton_clicked()
{
    setState(false);
    mifare->sniff14a();
    setState(true);
}

void MainWindow::on_MF_Sniff_listButton_clicked()
{
    mifare->list();
}

void MainWindow::MF_widgetReset()
{
    int secs = mifare->cardType.sector_size;
    int blks = mifare->cardType.block_size;
    QBrush trailerItemForeColor = QBrush(QColor(0, 160, 255));
    ui->MF_RW_blockBox->clear();
    ui->MF_keyWidget->setRowCount(secs);
    ui->MF_dataWidget->setRowCount(blks);

    ui->MF_dataWidget->blockSignals(true);
    ui->MF_keyWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);

    for(int i = 0; i < blks; i++)
    {
        setTableItem(ui->MF_dataWidget, i, 0, "");
        setTableItem(ui->MF_dataWidget, i, 1, QString::number(i));
        ui->MF_dataWidget->item(i, 1)->setCheckState(Qt::Checked);
        setTableItem(ui->MF_dataWidget, i, 2, "");
        ui->MF_RW_blockBox->addItem(QString::number(i));
    }

    for(int i = 0; i < secs; i++)
    {
        setTableItem(ui->MF_keyWidget, i, 0, QString::number(i));
        setTableItem(ui->MF_keyWidget, i, 1, "");
        setTableItem(ui->MF_keyWidget, i, 2, "");
        setTableItem(ui->MF_dataWidget, mifare->cardType.blks[i], 0, QString::number(i));
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 2)->setForeground(trailerItemForeColor);
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
    }
    ui->MF_dataWidget->item(0, 2)->setForeground(QBrush(QColor(255, 160, 0)));
    ui->MF_selectAllBox->setCheckState(Qt::Checked);
    ui->MF_selectTrailerBox->setCheckState(Qt::Checked);

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_keyWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}
// ************************************************


// ******************** other ********************

void MainWindow::uiInit()
{
    connect(ui->Raw_CMDEdit, &QLineEdit::returnPressed, this, &MainWindow::sendMSG);
    ui->Raw_CMDEdit->installEventFilter(keyEventFilter);
    connect(keyEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_Raw_keyPressed);
    ui->MF_keyWidget->installEventFilter(resizeEventFilter);
    connect(resizeEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_MF_keyWidget_resized);
    ui->Raw_outputEdit->installEventFilter(keyEventFilter);

    connectStatusBar = new QLabel(this);
    programStatusBar = new QLabel(this);
    PM3VersionBar = new QLabel(this);
    stopButton = new QPushButton(this);
    connectStatusBar->setObjectName("connectStatusBar");
    programStatusBar->setObjectName("programStatusBar");
    PM3VersionBar->setObjectName("PM3VersionBar");
    setStatusBar(connectStatusBar, tr("Not Connected"));
    setStatusBar(programStatusBar, tr("Idle"));
    setStatusBar(PM3VersionBar, "");
    stopButton->setText(tr("Stop"));
    ui->statusbar->addPermanentWidget(PM3VersionBar, 1);
    ui->statusbar->addPermanentWidget(connectStatusBar, 1);
    ui->statusbar->addPermanentWidget(programStatusBar, 1);
    ui->statusbar->addPermanentWidget(stopButton);

    ui->MF_dataWidget->setColumnWidth(0, 55);
    ui->MF_dataWidget->setColumnWidth(1, 55);

    ui->MF_keyWidget->setColumnWidth(0, 45);

    MF_widgetReset();
    MFCardTypeBtnGroup = new QButtonGroup(this);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_miniButton, 0);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_1kButton, 1);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_2kButton, 2);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_4kButton, 4);
    connect(MFCardTypeBtnGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &MainWindow::MF_onMFCardTypeChanged);

    ui->MF_keyWidget->installEventFilter(this);
    ui->MF_dataWidget->installEventFilter(this);

    ui->Set_UI_Theme_nameBox->addItem(tr("(None)"), "(none)");
    ui->Set_UI_Theme_nameBox->addItem(tr("Dark"), "qdss_dark");
    ui->Set_UI_Theme_nameBox->addItem(tr("Light"), "qdss_light");

    settings->beginGroup("UI_grpbox_preference");

    QStringList boxNames = settings->allKeys();
    QGroupBox * boxptr;
    foreach(QString name, boxNames)
    {
        boxptr = this->findChild<QGroupBox*>(name);
        if(boxptr == nullptr)
            continue;
        if(settings->value(name, true).toBool())
        {
            boxptr->setMaximumHeight(16777215);
            boxptr->setChecked(true);
        }
        else
        {
            boxptr->setMaximumHeight(20);
            boxptr->setChecked(false);
        }
    }
    settings->endGroup();

    loadClientPathList();

    ui->Set_Client_GUIWorkingDirLabel->setText(QDir::currentPath());

    settings->beginGroup("Client_Args");
    ui->Set_Client_startArgsEdit->setText(settings->value("args", "<port> -f").toString());
    settings->endGroup();

    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = settings->value("state", false).toBool();
    settings->endGroup();
    ui->Set_Client_forceEnabledBox->setChecked(keepButtonsEnabled);

    // the disconnect detection doesn't work well on Linux/macOS
    // So it should be disabled on these platforms
    // https://github.com/wh201906/Proxmark3GUI/issues/22
    // #22, #26, #40, #41
    settings->beginGroup("Client_keepClientActive");
#ifdef Q_OS_WIN
    keepClientActive = settings->value("state", false).toBool();
#else
    keepClientActive = settings->value("state", true).toBool();
#endif
    settings->endGroup();
    ui->Set_Client_keepClientActiveBox->setChecked(keepClientActive);

    QDir configFiles(":/config/");
    configFiles.setSorting(QDir::Name);
    const QFileInfoList configFileList = configFiles.entryInfoList();
    ui->Set_Client_configFileBox->blockSignals(true);
    for(const auto& file : configFileList)
    {
        ui->Set_Client_configFileBox->addItem(file.fileName(), file.filePath());
    }

    // Use the last one as the default one
    ui->Set_Client_configFileBox->setCurrentIndex(ui->Set_Client_configFileBox->count() - 1);
    ui->Set_Client_configFileBox->addItem(tr("External file"), "(ext)");

    int configId = -1;
    settings->beginGroup("Client_Env");
    ui->Set_Client_envScriptEdit->setText(settings->value("scriptPath").toString());
    ui->Set_Client_workingDirEdit->setText(settings->value("workingDir", "../data").toString());
    configId = ui->Set_Client_configFileBox->findData(settings->value("configFile"));
    ui->Set_Client_configPathEdit->setText(settings->value("extConfigFilePath", "config.json").toString());
    settings->endGroup();
    if(configId != -1)
        ui->Set_Client_configFileBox->setCurrentIndex(configId);
    ui->Set_Client_configFileBox->blockSignals(false);
    on_Set_Client_configFileBox_currentIndexChanged(ui->Set_Client_configFileBox->currentIndex());

    // setValue() will trigger valueChanged()
    // setValue(settings->value()) will create a nested group
    // call endGroup() before apply the value
    settings->beginGroup("UI");
    int opacity = settings->value("Opacity", 100).toInt();
    int themeId = ui->Set_UI_Theme_nameBox->findData(settings->value("Theme_Name", "(none)").toString());
    settings->endGroup();
    ui->Set_UI_Opacity_Box->setValue(opacity);
    ui->Set_UI_Theme_nameBox->setCurrentIndex((themeId == -1) ? 0 : themeId);

    settings->beginGroup("UI");
    // QApplication::font() might return wrong result
    // If fonts are not specified in config file, don't touch them.
    QString tmpFontName;
    int tmpFontSize;
    bool fontValid = false, dataFontValid = false, CMDFontValid = false;
    tmpFontName = settings->value("Font_Name", "").toString();
    tmpFontSize = settings->value("Font_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_Font_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_Font_sizeBox->setValue(tmpFontSize);
        fontValid = true;
    }
    // The default font should be the same as MF_dataWidget's and MF_keyWidget's.
    tmpFontName = settings->value("DataFont_Name", "Consolas").toString();
    tmpFontSize = settings->value("DataFont_Size", 12).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_DataFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_DataFont_sizeBox->setValue(tmpFontSize);
        dataFontValid = true;
    }
    tmpFontName = settings->value("CMDFont_Name", "").toString();
    tmpFontSize = settings->value("CMDFont_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_CMDFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_CMDFont_sizeBox->setValue(tmpFontSize);
        CMDFontValid = true;
    }
    settings->endGroup();

    if(fontValid)
        on_Set_UI_Font_setButton_clicked();
    if(dataFontValid)
        on_Set_UI_DataFont_setButton_clicked();
    if(CMDFontValid)
        on_Set_UI_CMDFont_setButton_clicked();

    ui->MF_RW_keyTypeBox->addItem("A", Mifare::KEY_A);
    ui->MF_RW_keyTypeBox->addItem("B", Mifare::KEY_B);

    on_Raw_CMDHistoryBox_stateChanged(Qt::Unchecked);

}

void MainWindow::signalInit()
{
    connect(pm3, &PM3Process::newOutput, util, &Util::processOutput);
    connect(pm3, &PM3Process::changeClientType, util, &Util::setClientType);
    connect(util, &Util::refreshOutput, this, &MainWindow::refreshOutput);

    connect(this, &MainWindow::connectPM3, pm3, &PM3Process::connectPM3);
    connect(this, &MainWindow::reconnectPM3, pm3, &PM3Process::reconnectPM3);
    connect(pm3, &PM3Process::PM3StatedChanged, this, &MainWindow::onPM3StateChanged);
    connect(pm3, &PM3Process::PM3StatedChanged, util, &Util::setRunningState);
    connect(pm3, &PM3Process::errorOccurred, this, &MainWindow::onPM3ErrorOccurred);
    connect(pm3, &PM3Process::HWConnectFailed, this, &MainWindow::onPM3HWConnectFailed);
    connect(pm3, &PM3Process::PM3VersionInfo, this, &MainWindow::onPM3VersionInfo);
    connect(this, &MainWindow::killPM3, pm3, &PM3Process::killPM3);
    connect(this, &MainWindow::setProcEnv, pm3, &PM3Process::setProcEnv);
    connect(this, &MainWindow::setWorkingDir, pm3, &PM3Process::setWorkingDir);
    connect(this, QOverload<bool>::of(&MainWindow::setSerialListener), pm3, QOverload<bool>::of(&PM3Process::setSerialListener));
    connect(this, QOverload<const QString&, bool>::of(&MainWindow::setSerialListener), pm3, QOverload<const QString&, bool>::of(&PM3Process::setSerialListener));

    connect(util, &Util::write, pm3, &PM3Process::write);
    connect(mifare, &Mifare::cardTypeChanged, this, &MainWindow::onMfcDocumentTypeChanged);

    connect(ui->MF_typeGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_fileGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_RWGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_normalGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_UIDGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_simGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_sniffGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);

    connect(stopButton, &QPushButton::clicked, this, &MainWindow::on_stopButton_clicked);

    connect(ui->Set_UI_Opacity_slider, &QSlider::valueChanged, ui->Set_UI_Opacity_Box, &QSpinBox::setValue);
}

void MainWindow::setStatusBar(QLabel * target, const QString& text)
{
    if(target == PM3VersionBar)
        target->setText(tr("HW Version:") + text);
    else if(target == connectStatusBar)
        target->setText(tr("PM3:") + text);
    else if(target == programStatusBar)
        target->setText(tr("State:") + text);
}

void MainWindow::setTableItem(QTableWidget * widget, int row, int column, const QString& text)
{
    if(widget->item(row, column) == nullptr)
        widget->setItem(row, column, new QTableWidgetItem());
    widget->item(row, column)->setText(text);
}

bool MainWindow::eventFilter(QObject * watched, QEvent * event) // drag support
{
    if(event->type() == QEvent::DragEnter)
    {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
        dragEvent->acceptProposedAction();
        return true;
    }
    else if(event->type() == QEvent::Drop)
    {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
        if(watched == ui->MF_keyWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    if(mifare->data_loadKeyFile(urls[0].toLocalFile()))
                    {
                        MF_widgetReset();
                        mifare->data_syncWithDataWidget();
                        mifare->data_syncWithKeyWidget();
                    }
                    return true;
                }
            }
        }
        else if(watched == ui->MF_dataWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    if(mifare->data_loadDataFile(urls[0].toLocalFile()))
                    {
                        MF_widgetReset();
                        mifare->data_syncWithDataWidget();
                        mifare->data_syncWithKeyWidget();
                    }
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setState(bool st)
{
    if(!st && pm3state)
    {
        setStatusBar(programStatusBar, tr("Running"));
    }
    else
    {
        setStatusBar(programStatusBar, tr("Idle"));
    }
    setButtonsEnabled(st || keepButtonsEnabled);
}

void MainWindow::setButtonsEnabled(bool st)
{
    const bool mf = capabilities.has("iso14443a");
    const bool lf = capabilities.has("lf");
    ui->MF_attackGroupBox->setEnabled(st && mf);
    ui->MF_normalGroupBox->setEnabled(st && mf);
    ui->MF_UIDGroupBox->setEnabled(st && mf);
    ui->MF_simGroupBox->setEnabled(st && mf);
    ui->MF_sniffGroupBox->setEnabled(st && mf);
    ui->Raw_CMDEdit->setEnabled(st);
    ui->Raw_sendCMDButton->setEnabled(st);
    ui->LF_LFconfigGroupBox->setEnabled(st && lf);
    ui->LF_operationGroupBox->setEnabled(st && lf);
}

void MainWindow::applyCapabilities()
{
    const bool mf = capabilities.has("iso14443a");
    const bool lf = capabilities.has("lf");
    const bool hfSniff = capabilities.has("hfsniff") && mf;
    const bool plot = clientQtPlot && lf;

    // dockInit() moves every page out of funcTab into a QDockWidget, so the
    // visible tab is the dock, not the (now empty) QTabWidget. ui->t55xxTab is
    // the removed UI page; the live page is the member pointer.
    auto applyPage = [this](QWidget* page, bool enabled) {
        page->setEnabled(enabled);
        for(QDockWidget* dock : dockList)
            if(dock->widget() == page)
                dock->setEnabled(enabled);
    };
    applyPage(ui->mifareTab, mf);
    applyPage(workbench, mf);
    applyPage(ui->lfTab, lf);
    applyPage(this->t55xxTab, lf);

    const QString mfReason = capabilities.reason("ISO14443A");
    const QString lfReason = capabilities.reason("LF");
    const QString sniffReason = capabilities.has("hfsniff") ? mfReason : capabilities.reason("HFSNIFF");

    QList<QWidget*> mfWidgets = {ui->MF_attackGroupBox, ui->MF_normalGroupBox, ui->MF_UIDGroupBox,
                                 ui->MF_simGroupBox, ui->MF_sniffGroupBox, ui->mifareTab, workbench
                                };
    for(QWidget* widget : mfWidgets)
        widget->setToolTip(mf ? QString() : mfReason);

    QList<QWidget*> lfWidgets = {ui->LF_LFconfigGroupBox, ui->LF_operationGroupBox, ui->lfTab, this->t55xxTab};
    for(QWidget* widget : lfWidgets)
        widget->setToolTip(lf ? QString() : lfReason);

    ui->MF_Sniff_sniffButton->setEnabled(hfSniff);
    ui->MF_14aSniff_snoopButton->setEnabled(hfSniff);
    ui->MF_Sniff_sniffButton->setToolTip(hfSniff ? QString() : sniffReason);
    ui->MF_14aSniff_snoopButton->setToolTip(hfSniff ? QString() : sniffReason);

    ui->LF_Op_readButton->setEnabled(plot);
    ui->LF_Op_sniffButton->setEnabled(plot);
    if(!plot)
    {
        const QString reason = lf ? tr("This client was built without QT plot support") : lfReason;
        ui->LF_Op_readButton->setToolTip(reason);
        ui->LF_Op_sniffButton->setToolTip(reason);
    }

    setButtonsEnabled(pm3state || keepButtonsEnabled);

    if(capabilities.isLoaded() && !capabilities.removedFeatures().isEmpty())
    {
        statusBar()->showMessage(tr("Firmware capabilities: removed %1")
                                 .arg(capabilities.removedFeatures().join(", ")), 15000);
    }
}

void MainWindow::onPM3VersionInfo(const QString& clientVersion, const QString& firmwareVersion,
                                  const QString& target, bool mismatch, bool clientQtPlot, quint64 attempt)
{
    if(attempt != activeConnectAttempt)
    {
        qDebug() << "ignoring stale version info" << attempt << activeConnectAttempt;
        return;
    }
    this->clientQtPlot = clientQtPlot;
    applyCapabilities();

    QStringList info;
    if(!clientVersion.isEmpty())
        info << tr("client %1").arg(clientVersion);
    if(!firmwareVersion.isEmpty())
        info << tr("firmware %1").arg(firmwareVersion);
    if(!target.isEmpty())
        info << tr("target %1").arg(target);
    if(pm3 != nullptr && (connectClickTimer.isValid() || pm3->lastConnectMs > 0))
    {
        const qint64 totalMs = connectClickTimer.isValid() ? connectClickTimer.elapsed() : pm3->lastConnectMs;
        info << tr("ready in %1 s").arg(totalMs / 1000.0, 0, 'f', 2);
    }
    if(!info.isEmpty())
        statusBar()->showMessage(info.join(" | "), 15000);

    if(mismatch)
    {
        statusBar()->showMessage(tr("Client and firmware versions differ: %1 / %2")
                                 .arg(clientVersion, firmwareVersion), 30000);
        qDebug() << "client/firmware version mismatch" << clientVersion << firmwareVersion;
    }

    if(capabilities.isLoaded() && !firmwareVersion.isEmpty())
    {
        QString manifestVersion = capabilities.firmwareVersion();
        int dash = manifestVersion.indexOf('-');
        if(dash > 0)
            manifestVersion = manifestVersion.left(dash);
        QString reportedVersion = firmwareVersion;
        dash = reportedVersion.indexOf('-');
        if(dash > 0)
            reportedVersion = reportedVersion.left(dash);
        if(!manifestVersion.isEmpty() && manifestVersion != reportedVersion)
        {
            statusBar()->showMessage(tr("Capability manifest %1 does not match firmware %2")
                                     .arg(capabilities.firmwareVersion(), firmwareVersion), 30000);
            qDebug() << "capability manifest/firmware mismatch" << capabilities.firmwareVersion() << firmwareVersion;
        }
    }
}

void MainWindow::onMfcDocumentTypeChanged(int type)
{
    if(MFCardTypeBtnGroup != nullptr)
    {
        MFCardTypeBtnGroup->blockSignals(true);
        if(QAbstractButton* button = MFCardTypeBtnGroup->button(type))
            button->setChecked(true);
        MFCardTypeBtnGroup->blockSignals(false);
    }
    mifare->setCardType(type);
    MF_widgetReset();
    mifare->data_syncWithDataWidget();
    mifare->data_syncWithKeyWidget();
}

void MainWindow::on_GroupBox_clicked(bool checked)
{
    QGroupBox* box = dynamic_cast<QGroupBox*>(sender());

    settings->beginGroup("UI_grpbox_preference");
    if(checked)
    {
        box->setMaximumHeight(16777215);
        settings->setValue(box->objectName(), true);
    }
    else
    {
        box->setMaximumHeight(20);
        settings->setValue(box->objectName(), false);
    }
    settings->endGroup();
}

void MainWindow::addClientPath(const QString& path)
{
    m_clientPathList.removeAll(path);
    m_clientPathList.prepend(path);
    while(m_clientPathList.size() > 32) // the maximum count of path items
        m_clientPathList.removeLast();
    // sync to the storage
    saveClientPathList();
    // sync to the UI
    loadClientPathList();
}

void MainWindow::loadClientPathList()
{
    m_clientPathList.clear();
    settings->beginGroup("Client_Path");
    int len = settings->beginReadArray("pathList");
    settings->endArray();
    if(settings->contains("path") && len == 0)
    {
        qDebug() << "Using old client path storage";
        m_clientPathList += settings->value("path", "proxmark3").toString();
    }
    else
    {
        int arrayLen = settings->beginReadArray("pathList");
        for(int i = 0; i < arrayLen; i++)
        {
            settings->setArrayIndex(i);
            QString path = settings->value("path").toString();
            if(!path.isEmpty())
                m_clientPathList += path;
        }
        settings->endArray();
    }
    settings->endGroup();

    ui->PM3_pathBox->clear();
    for(const QString& clientPath : qAsConst(m_clientPathList))
        ui->PM3_pathBox->addItem(clientPath);
}

void MainWindow::saveClientPathList()
{
    settings->beginGroup("Client_Path");
    if(settings->contains("path"))
    {
        qDebug() << "Upgrading client path storage";
        QString oldPath = settings->value("path").toString();
        if(!oldPath.isEmpty() && !m_clientPathList.contains(oldPath))
            m_clientPathList.append(oldPath);
        settings->remove("path");
    }

    settings->beginWriteArray("pathList");
    for(int i = 0; i < m_clientPathList.size(); i++)
    {
        settings->setArrayIndex(i);
        settings->setValue("path", m_clientPathList[i]);
    }
    settings->endArray();
    settings->endGroup();
}
// ***********************************************



void MainWindow::on_MF_Attack_darksideButton_clicked()
{
    setState(false);
    mifare->darkside();
    setState(true);
}

void MainWindow::on_Set_Client_startArgsEdit_editingFinished()
{
    settings->beginGroup("Client_Args");
    settings->setValue("args", ui->Set_Client_startArgsEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_forceEnabledBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = (arg1 == Qt::Checked);
    settings->setValue("state", keepButtonsEnabled);
    settings->endGroup();
    if(keepButtonsEnabled)
        setButtonsEnabled(true);
}

void MainWindow::on_Set_UI_setLanguageButton_clicked()
{
    Util::chooseLanguage(settings, this);
}

void MainWindow::on_PM3_refreshPortButton_clicked()
{
    on_portSearchTimer_timeout();
}

void MainWindow::on_Set_Client_envScriptEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("scriptPath", ui->Set_Client_envScriptEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_workingDirEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("workingDir", ui->Set_Client_workingDirEdit->text());
    settings->endGroup();
}


void MainWindow::on_Set_Client_configPathEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("extConfigFilePath", ui->Set_Client_configPathEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_keepClientActiveBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_keepClientActive");
    keepClientActive = (arg1 == Qt::Checked);
    settings->setValue("state", keepClientActive);
    settings->endGroup();
    emit setSerialListener(!keepClientActive);
}

void MainWindow::on_LF_LFConf_freqSlider_valueChanged(int value)
{
    onLFfreqConfChanged(value, true);
}

void MainWindow::onLFfreqConfChanged(int divisor, bool isCustomized)
{
    ui->LF_LFConf_freqDivisorBox->blockSignals(true);
    ui->LF_LFConf_freqSlider->blockSignals(true);

    if(isCustomized)
        ui->LF_LFConf_freqOtherButton->setChecked(true);
    else if(divisor == 95)
        ui->LF_LFConf_freq125kButton->setChecked(true);
    else if(divisor == 88)
        ui->LF_LFConf_freq134kButton->setChecked(true);
    ui->LF_LFConf_freqLabel->setText(tr("Actural Freq: ") + QString("%1kHz").arg(LF::divisor2Freq(divisor), 0, 'f', 3));
    ui->LF_LFConf_freqDivisorBox->setValue(divisor);
    ui->LF_LFConf_freqSlider->setValue(divisor);

    ui->LF_LFConf_freqDivisorBox->blockSignals(false);
    ui->LF_LFConf_freqSlider->blockSignals(false);
}

void MainWindow::on_LF_LFConf_freqDivisorBox_valueChanged(int arg1)
{
    onLFfreqConfChanged(arg1, true);
}

void MainWindow::on_LF_LFConf_freq125kButton_clicked()
{
    onLFfreqConfChanged(95, false);
}

void MainWindow::on_LF_LFConf_freq134kButton_clicked()
{
    onLFfreqConfChanged(88, false);
}

void MainWindow::on_LF_Op_searchButton_clicked()
{
    setState(false);
    lf->search();
    setState(true);
}

void MainWindow::on_LF_Op_readButton_clicked()
{
    setState(false);
    lf->read();
    setState(true);
}

void MainWindow::on_LF_Op_tuneButton_clicked()
{
    setState(false);
    lf->tune();
    setState(true);
}

void MainWindow::on_LF_Op_sniffButton_clicked()
{
    setState(false);
    lf->sniff();
    setState(true);
}

void MainWindow::dockInit()
{
    setDockNestingEnabled(true);
    QDockWidget* dock;
    QWidget* widget;
    int count = ui->funcTab->count();
    qDebug() << "dock count" << count;
    for(int i = 0; i < count; i++)
    {
        dock = new QDockWidget(ui->funcTab->tabText(0), this);
        qDebug() << "dock name" << ui->funcTab->tabText(0);
        dock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);// movable is necessary, otherwise the dock cannot be dragged
        dock->setAllowedAreas(Qt::BottomDockWidgetArea);
        dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget = ui->funcTab->widget(0);
        dock->setWidget(widget);
        if(widget->objectName() == "rawTab")
            Util::setRawTab(dock, i);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if(!dockList.isEmpty())
            tabifyDockWidget(dockList[0], dock);
        dockList.append(dock);
    }
    ui->funcTab->setVisible(false);
    dockList[0]->setVisible(true);
    dockList[0]->raise();
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    contextMenu->exec(event->globalPos());
}

void MainWindow::on_LF_LFConf_getButton_clicked()
{
    setState(false);
    lf->getLFConfig();
    setState(true);
}

void MainWindow::on_LF_LFConf_setButton_clicked()
{
    LF::LFConfig config;
    setState(false);
    config.divisor = ui->LF_LFConf_freqDivisorBox->value();
    config.bitsPerSample = ui->LF_LFConf_bitsPerSampleBox->value();
    config.decimation = ui->LF_LFConf_decimationBox->value();
    config.averaging = ui->LF_LFConf_averagingBox->isChecked();
    config.triggerThreshold = ui->LF_LFConf_thresholdBox->value();
    config.samplesToSkip = ui->LF_LFConf_skipsBox->value();
    lf->setLFConfig(config);
    Util::gotoRawTab();
    setState(true);
}

void MainWindow::on_LF_LFConf_resetButton_clicked()
{
    setState(false);
    lf->resetLFConfig();
    setState(true);
}

void MainWindow::on_Set_Client_configFileBox_currentIndexChanged(int index)
{
    ui->Set_Client_configPathEdit->setVisible(ui->Set_Client_configFileBox->itemData(index).toString() == "(ext)");
    settings->beginGroup("Client_Env");
    settings->setValue("configFile", ui->Set_Client_configFileBox->currentData());
    settings->endGroup();
}


void MainWindow::on_Set_UI_Opacity_Box_valueChanged(int arg1)
{
    ui->Set_UI_Opacity_slider->blockSignals(true);
    ui->Set_UI_Opacity_slider->setValue(arg1);
    setWindowOpacity(arg1 / 100.0);
    settings->beginGroup("UI");
    settings->setValue("Opacity", ui->Set_UI_Opacity_Box->value());
    settings->endGroup();
    ui->Set_UI_Opacity_slider->blockSignals(false);
}


void MainWindow::on_Set_UI_Theme_setButton_clicked()
{
    settings->beginGroup("UI");
    settings->setValue("Theme_Name", ui->Set_UI_Theme_nameBox->currentData().toString());
    settings->endGroup();
}


void MainWindow::on_Set_UI_Font_setButton_clicked()
{
    QFont font = ui->Set_UI_Font_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_Font_sizeBox->value());
    QApplication::setFont(font, "QWidget");

    settings->beginGroup("UI");
    settings->setValue("Font_Name", ui->Set_UI_Font_nameBox->currentFont().family());
    settings->setValue("Font_Size", ui->Set_UI_Font_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_DataFont_setButton_clicked()
{
    QFont font = ui->Set_UI_DataFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_DataFont_sizeBox->value());
    ui->MF_dataWidget->setFont(font);
    ui->MF_keyWidget->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("DataFont_Name", ui->Set_UI_DataFont_nameBox->currentFont().family());
    settings->setValue("DataFont_Size", ui->Set_UI_DataFont_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_CMDFont_setButton_clicked()
{
    QFont font = ui->Set_UI_CMDFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_CMDFont_sizeBox->value());
    ui->Raw_outputEdit->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("CMDFont_Name", ui->Set_UI_CMDFont_nameBox->currentFont().family());
    settings->setValue("CMDFont_Size", ui->Set_UI_CMDFont_sizeBox->value());
    settings->endGroup();
}


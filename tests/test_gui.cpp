#include <QtTest>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "common/util.h"
#include "common/pm3cmd.h"
#include "common/capabilities.h"
#include "common/pm3process.h"
#include "ui/mf_sim_simdialog.h"
#include "ui/mainwindow.h"
#include <QComboBox>
#include <QMessageBox>
#include <QSettings>
#include <QTabWidget>

#ifndef SRCDIR
#define SRCDIR "."
#endif

class GuiTests : public QObject
{
    Q_OBJECT

private:
    QVariantMap loadProfile(const QString& name)
    {
        QFile file(QFileInfo(QString(SRCDIR) + "/config/" + name).absoluteFilePath());
        if(!file.open(QFile::ReadOnly | QFile::Text))
            return QVariantMap();
        return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    }

    // Feed a fixture to Util when it writes a command.
    void armFixture(Util& util, const QByteArray& fixture, int delayMs = 0)
    {
        QObject::connect(&util, &Util::write, &util, [&util, fixture, delayMs](QString) {
            QTimer::singleShot(delayMs, &util, [&util, fixture]() {
                util.processOutput(QString::fromUtf8(fixture));
            });
        });
    }

    // A modal QMessageBox would block the test run instead of failing it.
    struct DialogGuard
    {
        QStringList texts;
        QTimer timer;
        DialogGuard()
        {
            QObject::connect(&timer, &QTimer::timeout, [this]() {
                QWidget* modal = QApplication::activeModalWidget();
                QMessageBox* box = qobject_cast<QMessageBox*>(modal);
                if(box == nullptr)
                    return;
                texts << box->text();
                box->reject();
            });
            timer.start(100);
        }
        QString joined() const
        {
            return texts.join(" | ");
        }
    };

    // QProcess cannot exec a #! script on Windows, so the process tests use the
    // compiled mock client there. osVersion is empty for an offline client.
    QString makeMockClient(const QString& dirPath, const QByteArray& osVersion)
    {
        QDir().mkpath(dirPath);
#ifdef Q_OS_WIN
        const QString target = QDir(dirPath).filePath("mock-proxmark3.exe");
        QFile::remove(target);
        if(!QFile::copy(QCoreApplication::applicationDirPath() + "/mock-proxmark3.exe", target))
            return QString();
        // Windows drops an environment variable set to an empty value, so use a sentinel.
        qputenv("MOCK_OS_VERSION", osVersion.isEmpty() ? QByteArrayLiteral("offline") : osVersion);
        return target;
#else
        QByteArray body;
        body += "#!/bin/sh\n";
        body += "printf '[=] Session log /tmp/mock.log\\n[=] OFFLINE mode.\\n[offline|script] pm3 --> '\n";
        body += "while IFS= read -r line; do\n";
        body += "  case \"$line\" in\n";
        body += "    \"hw version\")\n";
        body += "      printf '\\n [ Client ]\\n  Iceman/master/v4.23346-suspect 2026-09-18\\n  QT GUI support............ present\\n'\n";
        if(!osVersion.isEmpty())
        {
            body += "      printf '\\n [ Model ]\\n    Target.... PM3 GENERIC\\n    Client.... v4.23346-suspect\\n";
            body += "    OS........ " + osVersion + "\\n'\n";
        }
        body += "      printf '\\n[offline|script] pm3 --> '\n;;\n";
        body += "  esac\n";
        body += "done\n";

        const QString target = QDir(dirPath).filePath("mock-proxmark3");
        QFile script(target);
        if(!script.open(QFile::WriteOnly | QFile::Text))
            return QString();
        script.write(body);
        script.close();
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return target;
#endif
    }

private slots:
    void init()
    {
        Util::setClientType(Util::CLIENTTYPE_ICEMAN);
    }

    void promptDetection()
    {
        QVERIFY(Util::isPromptAtEnd("[usb] pm3 --> "));
        QVERIFY(Util::isPromptAtEnd("\r[offline|script] pm3 --> "));
        QVERIFY(Util::isPromptAtEnd("text\n[usb|script] pm5 -->"));
        QVERIFY(!Util::isPromptAtEnd("pm3 --> something"));
    }

    void ansiStripping()
    {
        QCOMPARE(Util::stripAnsi("\x1b[32mWrite ( ok )\x1b[0m"), QString("Write ( ok )"));
    }

    void writeOk()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf wrbl --blk 1\n"
                         "Writing block no 1, key type:A - FFFFFFFFFFFF\n"
                         "[+] Write ( ok )\n"
                         "[usb] pm3 --> ");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.successPatternMatched);
        QVERIFY(result.promptSeen);
    }

    void writeTimeoutIsFailure()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf wrbl --blk 1\n[-] command execution time out\n[usb] pm3 --> ");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_FAILED);
        QVERIFY(result.failurePatternMatched);
    }

    void writeUnavailableIsFailure()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf wrbl --blk 1\n[!] This command is not available in this mode\n[usb] pm3 --> ");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_FAILED);
    }

    void echoOnlyIsFailure()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf wrbl --blk 1 -a -k FFFFFFFFFFFF -d 00000000000000000000000000000000 --force\n"
                         "Writing block no 1, key type:A - FFFFFFFFFFFF\n"
                         "[usb] pm3 --> ");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_FAILED);
    }

    void splitOutputStillCompletes()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        int stage = 0;
        QObject::connect(&util, &Util::write, &util, [&util, &stage](QString) {
            QTimer::singleShot(0, &util, [&util, &stage]() {
                util.processOutput("[+] Write ( ok )\n");
                QTimer::singleShot(50, &util, [&util, &stage]() {
                    util.processOutput("[usb] pm3 --> ");
                    stage++;
                });
            });
        });

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QCOMPARE(stage, 1);
    }

    void emulatorWriteCompletesOnPrompt()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["emulator write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf esetblk --blk 1\n[usb] pm3 --> ");

        Util::ExecResult result = util.execCMDWithResult("hf mf esetblk --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(!result.successPatternMatched);
    }

    void clientExitIsAborted()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        QObject::connect(&util, &Util::write, &util, [&util](QString) {
            QTimer::singleShot(50, &util, [&util]() { util.setRunningState(false); });
        });

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_ABORTED);
    }

    void legacyReadStillReturnsRawOutput()
    {
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf rdbl --blk 1\n"
                         " 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f\n"
                         "[usb] pm3 --> ");
        QString output = util.execCMDWithOutput("hf mf rdbl --blk 1", Util::ReturnTrigger(300, {"([0-9a-fA-F]{2} ){15}[0-9a-fA-F]{2}"}));
        QVERIFY(output.contains("0e 0f"));
    }

    void versionRegex()
    {
        QRegularExpression re("(?:Iceman/[^/\\s]+/)?(v?\\d+(?:\\.\\d+)+(?:[-+][^\\s]*)?)");
        QRegularExpressionMatch match = re.match("    Client.... v4.23346-suspect\n    OS........ v4.23346-suspect");
        QVERIFY(match.hasMatch());
        QCOMPARE(match.captured(1), QString("v4.23346-suspect"));
    }

    void traceSaveSourceSelection()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["save sniff"].toMap();
        QCOMPARE(Pm3Cmd::traceSaveCommand(config, false, "x.trace"), QString("trace save -f x.trace"));
        QCOMPARE(Pm3Cmd::traceSaveCommand(config, true, "x.trace"), QString("trace save --buffer -f x.trace"));

        QVariantMap oldProfile = loadProfile("config_rrgv4.16717.json");
        QVariantMap oldConfig = oldProfile["mifare classic"].toMap()["save sniff"].toMap();
        QCOMPARE(Pm3Cmd::traceSaveCommand(oldConfig, true, "x.trace"), oldConfig["cmd"].toString().replace("<filename>", "x.trace"));
    }

    void profilePatternsMatchRealClientOutput()
    {
        QVERIFY(QFile(":/config/config_rrgv4.23346.json").exists());
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap normal = profile["mifare classic"].toMap()["normal write block"].toMap();
        QVariantMap magic = profile["mifare classic"].toMap()["Magic Card write block"].toMap();
        QVariantMap emu = profile["mifare classic"].toMap()["emulator write block"].toMap();

        QVERIFY(Util::matchAnyPattern("Write ( ok )", Pm3Cmd::jsonStringList(normal["success pattern"])));
        QVERIFY(!Util::matchAnyPattern("Write ( ok )", Pm3Cmd::jsonStringList(normal["failed flag"])));
        QVERIFY(Util::matchAnyPattern("[-] command execution time out", Pm3Cmd::jsonStringList(normal["failed flag"])));
        QVERIFY(Util::matchAnyPattern("Writing block number: 1", Pm3Cmd::jsonStringList(magic["success pattern"])));
        QVERIFY(magic["verify read back"].toBool());
        QVERIFY(emu["verify read back"].toBool());
    }

    void capabilitiesManifest()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("pm3_capabilities.json");
        QFile file(path);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write("{\"manifest_version\":1,\"firmware\":{\"version\":\"v4.23346\",\"platform\":\"PM3GENERIC\",\"platform_size\":256},"
                   "\"features\":{\"lf\":false,\"iso14443a\":true,\"hfsniff\":false},\"removed\":[\"LF\"]}");
        file.close();

        Pm3Capabilities caps = Pm3Capabilities::load(path, dir.path());
        QVERIFY(caps.isLoaded());
        QVERIFY(!caps.has("lf"));
        QVERIFY(caps.has("iso14443a"));
        QVERIFY(!caps.has("hfsniff"));
        QVERIFY(caps.has("unknown_feature"));
        QCOMPARE(caps.firmwareVersion(), QString("v4.23346"));
        QCOMPARE(caps.removedFeatures(), QStringList() << "LF");

        QTemporaryDir emptyDir;
        QVERIFY(emptyDir.isValid());
        Pm3Capabilities missing = Pm3Capabilities::load(emptyDir.filePath("does-not-exist.json"), emptyDir.path());
        QVERIFY(!missing.isLoaded());
        QVERIFY(missing.has("lf"));
    }

    void simDialogUsesShortCrackOption()
    {
        Util::setClientType(Util::CLIENTTYPE_ICEMAN);
        MF_Sim_simDialog dialog(1, "1k");
        QCheckBox* xBox = dialog.findChild<QCheckBox*>("xBox");
        QCheckBox* eBox = dialog.findChild<QCheckBox*>("eBox");
        QVERIFY(xBox != nullptr);
        QVERIFY(eBox != nullptr);
        QCOMPARE(xBox->text(), QString("-x"));

        QSignalSpy spy(&dialog, SIGNAL(sendCMD(QString)));
        xBox->setChecked(true);
        QVERIFY(QMetaObject::invokeMethod(&dialog, "on_buttonBox_accepted"));
        QCOMPARE(spy.count(), 1);
        const QString cmd = spy.takeFirst().at(0).toString();
        QVERIFY2(cmd.contains("-x"), qPrintable(cmd));
        QVERIFY2(!cmd.contains("--crack"), qPrintable(cmd));

        // -e implies -x in the dialog
        xBox->setChecked(false);
        eBox->click();
        QVERIFY(xBox->isChecked());
        QVERIFY(QMetaObject::invokeMethod(&dialog, "on_buttonBox_accepted"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.takeFirst().at(0).toString().contains("-x"));
    }

    void connectAppliesCapabilityManifest()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString extConfig = QFileInfo(QString(SRCDIR) + "/config/config_rrgv4.23346.json").absoluteFilePath();
        const QString oldCwd = QDir::currentPath();
        // Same layout as the shipped package: gui/ holds the settings, the client
        // is addressed relatively as ../client/<exe>, data/ is the work dir.
        QVERIFY(QDir(dir.path()).mkpath("app"));
        QVERIFY(QDir(dir.path()).mkpath("client"));
        QVERIFY(QDir(dir.path()).mkpath("data"));
        QVERIFY(QDir(dir.path()).mkpath("config"));
        QDir::setCurrent(dir.filePath("app"));

        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        // Absolute work dir: QProcess resolves a relative program against the child
        // working directory on Unix but against the parent CWD on Windows, and both
        // must land on <temp>/client/<mock> here.
        settings.setValue("Client_Env/workingDir", dir.filePath("data"));
        settings.setValue("Client_Env/configFile", "(ext)");
        settings.setValue("Client_Env/extConfigFilePath", extConfig);
        settings.setValue("Client_Args/args", "-f");
        settings.sync();

        const QString scriptPath = makeMockClient(dir.filePath("client"), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QFile manifest(dir.filePath("config/pm3_capabilities.json"));
        QVERIFY(manifest.open(QFile::WriteOnly));
        manifest.write("{\"manifest_version\":1,\"firmware\":{\"version\":\"v4.23346\"},"
                       "\"features\":{\"lf\":false,\"iso14443a\":false,\"hfsniff\":false},\"removed\":[\"LF\",\"ISO14443A\"]}");
        manifest.close();

        DialogGuard guard;
        MainWindow window;
        window.initUI();
        QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
        QTabWidget* tabs = window.findChild<QTabWidget*>("funcTab");
        QWidget* mifareTab = window.findChild<QWidget*>("mifareTab");
        QWidget* lfTab = window.findChild<QWidget*>("lfTab");
        QVERIFY(pathBox != nullptr);
        QVERIFY(tabs != nullptr);
        QVERIFY(mifareTab != nullptr);
        QVERIFY(lfTab != nullptr);

        pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
        QTest::qWait(1500);

        // Collect the observations first: an early return would leave the client
        // process and its thread running and hang the test run.
        const bool mifareDisabled = !tabs->isTabEnabled(tabs->indexOf(mifareTab));
        const bool lfDisabled = !tabs->isTabEnabled(tabs->indexOf(lfTab));
        const bool mifareTip = !mifareTab->toolTip().isEmpty();
        const bool lfTip = !lfTab->toolTip().isEmpty();

        QMetaObject::invokeMethod(&window, "on_PM3_disconnectButton_clicked");
        QTest::qWait(1000);
        QDir::setCurrent(oldCwd);
        QVERIFY2(guard.texts.isEmpty(), qPrintable(guard.joined()));
        QVERIFY(mifareDisabled);
        QVERIFY(lfDisabled);
        QVERIFY(mifareTip);
        QVERIFY(lfTip);
    }

    void pm3ProcessParsesVersionBlock_data()
    {
        QTest::addColumn<QString>("osVersion");
        QTest::addColumn<bool>("expectState");
        QTest::addColumn<bool>("expectMismatch");
        QTest::newRow("matching") << "v4.23346-suspect" << true << false;
        QTest::newRow("mismatch") << "v4.16717" << true << true;
        QTest::newRow("offline") << "" << false << false;
    }

    void pm3ProcessParsesVersionBlock()
    {
        QFETCH(QString, osVersion);
        QFETCH(bool, expectState);
        QFETCH(bool, expectMismatch);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scriptPath = makeMockClient(dir.path(), osVersion.toUtf8());
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString)));
        QSignalSpy versionSpy(process, SIGNAL(PM3VersionInfo(QString, QString, QString, bool, bool)));
        QSignalSpy failedSpy(process, SIGNAL(HWConnectFailed()));
        thread.start();
        QElapsedTimer connectTimer;
        connectTimer.start();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));

        // wait for both signals without returning early (the thread must be stopped before asserts)
        QElapsedTimer waitTimer;
        waitTimer.start();
        while(waitTimer.elapsed() < 15000)
        {
            QCoreApplication::processEvents();
            if(versionSpy.count() > 0 && (expectState ? stateSpy.count() > 0 : failedSpy.count() > 0))
                break;
            QTest::qWait(50);
        }
        QCoreApplication::processEvents();
        const qint64 connectElapsed = connectTimer.elapsed();

        const int versionCount = versionSpy.count();
        const QList<QVariant> version = versionCount ? versionSpy.first() : QList<QVariant>();
        const int stateCount = stateSpy.count();
        const bool stateValue = stateCount ? stateSpy.first().at(0).toBool() : false;
        const int failedCount = failedSpy.count();

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection);
        process->waitForFinished(2000);
        thread.quit();
        thread.wait(5000);
        delete process;

        // The offline client must not fall through to the 10 s device wait; the bound
        // is loose because Wine takes seconds to spawn the mock process.
        QVERIFY2(connectElapsed < 9000, "startup should not wait for a device-only line");
        QCOMPARE(versionCount, 1);
        QCOMPARE(version.at(0).toString(), QString("v4.23346-suspect"));
        QCOMPARE(version.at(3).toBool(), expectMismatch);
        QVERIFY(version.at(4).toBool());
        if(expectState)
        {
            QCOMPARE(stateCount, 1);
            QVERIFY(stateValue);
            QCOMPARE(version.at(1).toString(), osVersion);
            QVERIFY(version.at(2).toString().contains("PM3 GENERIC"));
        }
        else
        {
            QCOMPARE(failedCount, 1);
        }
    }
    // End to end QProcess test: a real client process is started, never answers
    // the write command, the wait times out and the process is closed again.
    void mockProcessCommandTimeout()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        Util util;
        QObject::connect(process, &PM3Process::newOutput, &util, &Util::processOutput);
        QObject::connect(&util, &Util::write, process, &PM3Process::write);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString)));
        thread.start();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));

        QElapsedTimer timer;
        timer.start();
        while(timer.elapsed() < 10000 && stateSpy.count() == 0)
        {
            QCoreApplication::processEvents();
            QTest::qWait(20);
        }
        QCOMPARE(stateSpy.count(), 1);
        QVERIFY(process->state() == QProcess::Running);

        util.setRunningState(true);
        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                         Util::ReturnTrigger(300, {"Write \\( ok \\)"}));
        QCOMPARE(result.status, Util::EXEC_TIMEOUT);
        QVERIFY(process->state() == QProcess::Running);

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection);
        // waitForFinished() may only be called from the thread owning the process.
        bool stopped = false;
        timer.restart();
        while(timer.elapsed() < 5000)
        {
            QCoreApplication::processEvents();
            if(process->state() == QProcess::NotRunning)
            {
                stopped = true;
                break;
            }
            QTest::qWait(20);
        }
        QVERIFY(stopped);
        thread.quit();
        QVERIFY(thread.wait(5000));
        delete process;
    }

    // Closing the window while the client is still starting must not leave the
    // client (or the GUI process) behind: that locked the install directory.
    void closeWhileConnectingStopsClient()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString extConfig = QFileInfo(QString(SRCDIR) + "/config/config_rrgv4.23346.json").absoluteFilePath();
        QVERIFY(QDir(dir.path()).mkpath("app"));
        QVERIFY(QDir(dir.path()).mkpath("client"));
        QVERIFY(QDir(dir.path()).mkpath("data"));
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.filePath("app"));

        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.setValue("Client_Env/workingDir", dir.filePath("data"));
        settings.setValue("Client_Env/configFile", "(ext)");
        settings.setValue("Client_Env/extConfigFilePath", extConfig);
        settings.setValue("Client_Args/args", "-f");
        settings.sync();

        const QString scriptPath = makeMockClient(dir.filePath("client"), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        DialogGuard guard;
        QElapsedTimer timer;
        {
            MainWindow window;
            window.initUI();
            QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
            QVERIFY(pathBox != nullptr);
            pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());
            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QTest::qWait(100); // connect is in flight
            timer.start();
        }
        QDir::setCurrent(oldCwd);
        QVERIFY2(guard.texts.isEmpty(), qPrintable(guard.joined()));
        QVERIFY2(timer.elapsed() < 3000, "window destruction blocked on the client");
        QTest::qWait(200);
        QVERIFY2(QFile::remove(scriptPath), "mock client is still running (file locked)");
    }
};

QTEST_MAIN(GuiTests)
#include "test_gui.moc"

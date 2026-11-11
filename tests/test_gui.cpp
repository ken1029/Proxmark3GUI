#include <QtTest>
#include <cstdio>
#include <QAbstractButton>
#include <QCheckBox>
#include <QCryptographicHash>
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
#include "module/mfcdocument.h"
#include "module/mfckeyrecovery.h"
#include "common/pm3process.h"
#include "ui/mf_sim_simdialog.h"
#include "ui/mainwindow.h"
#include "ui/mfcworkbench.h"
#include <QComboBox>
#include <QMessageBox>
#include <QSettings>
#include <QTranslator>
#include <QDialog>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>

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

    // Feed a fixture to Util when it writes a command. %TOKEN% is replaced with
    // the "rem" token of that write, so fixtures can answer with the real marker.
    void armFixture(Util& util, const QByteArray& fixture, int delayMs = 0)
    {
        QObject::connect(&util, &Util::write, &util, [&util, fixture, delayMs](QString wire) {
            QByteArray body = fixture;
            const int remPos = wire.indexOf("rem ");
            if(remPos >= 0)
            {
                const QString token = wire.mid(remPos + 4).trimmed();
                body.replace("%TOKEN%", token.toUtf8());
            }
            QTimer::singleShot(delayMs, &util, [&util, body]() {
                util.processOutput(QString::fromUtf8(body));
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

    // The mock is a real executable on every platform: a shell script cannot
    // reproduce chunked pipe writes, and Windows QProcess cannot run #! scripts.
    // osVersion is empty for an offline client.
    QString makeMockClient(const QString& dirPath, const QByteArray& osVersion, const QByteArray& mode = QByteArray())
    {
        QDir().mkpath(dirPath);
#ifdef Q_OS_WIN
        const QString mockName = "mock-proxmark3.exe";
#else
        const QString mockName = "mock-proxmark3";
#endif
        const QString target = QDir(dirPath).filePath(mockName);
        QFile::remove(target);
        if(!QFile::copy(QCoreApplication::applicationDirPath() + "/" + mockName, target))
            return QString();
        // Windows drops an environment variable set to an empty value, so use a sentinel.
        qputenv("MOCK_OS_VERSION", osVersion.isEmpty() ? QByteArrayLiteral("offline") : osVersion);
        qputenv("MOCK_MODE", mode.isEmpty() ? QByteArrayLiteral("pipe") : mode);
        return target;
    }

    struct ConnectObservation
    {
        int stateCount = 0;
        int versionCount = 0;
        int failedCount = 0;
        bool stateValue = false;
        QList<QVariant> version;
        QStringList failedReasons;
        qint64 elapsedMs = 0;
        qint64 startMs = 0;
        qint64 versionMs = 0;
        qint64 handshakeMs = 0;
        QString output;
    };

    ConnectObservation runConnect(const QString& path, int waitMs = 15000)
    {
        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        QSignalSpy versionSpy(process, SIGNAL(PM3VersionInfo(QString, QString, QString, bool, bool, quint64)));
        QSignalSpy failedSpy(process, SIGNAL(HWConnectFailed(QString, quint64)));
        ConnectObservation observation;
        QObject::connect(process, &PM3Process::newOutput, process, [&observation](const QString& out) {
            observation.output += out;
        }, Qt::DirectConnection);

        thread.start();
        QElapsedTimer connectTimer;
        connectTimer.start();
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, path), Q_ARG(QStringList, QStringList()));

        QElapsedTimer waitTimer;
        waitTimer.start();
        while(waitTimer.elapsed() < waitMs)
        {
            QCoreApplication::processEvents();
            if(versionSpy.count() > 0 && (stateSpy.count() > 0 || failedSpy.count() > 0))
                break;
            QTest::qWait(10);
        }
        QCoreApplication::processEvents();
        QTest::qWait(50); // let the queued Not-Connected signal arrive after a failure
        QCoreApplication::processEvents();

        observation.elapsedMs = connectTimer.elapsed();
        observation.stateCount = stateSpy.count();
        observation.versionCount = versionSpy.count();
        observation.failedCount = failedSpy.count();
        observation.version = observation.versionCount ? versionSpy.first() : QList<QVariant>();
        observation.stateValue = observation.stateCount ? stateSpy.first().at(0).toBool() : false;
        for(const QList<QVariant>& args : failedSpy)
            observation.failedReasons << args.at(0).toString();
        observation.startMs = process->lastStartMs;
        observation.versionMs = process->lastVersionMs;
        observation.handshakeMs = process->lastHandshakeMs;

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection, Q_ARG(quint64, attempt));
        process->waitForFinished(2000);
        thread.quit();
        thread.wait(5000);
        delete process;
        return observation;
    }

    QStringList mockPids(const QString& path)
    {
        QFile file(path);
        if(!file.open(QFile::ReadOnly | QFile::Text))
            return QStringList();
        return QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
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

    void completionMarkerIgnoresEcho()
    {
        const QString token = "PM3GUI_TEST_123";
        QVERIFY(!Util::hasCompletionMarker("[offline|script] pm3 --> rem PM3GUI_TEST_123\n", token));
        QVERIFY(Util::hasCompletionMarker("[+] 2026-11-10T11:53:37Z remark: PM3GUI_TEST_123\n", token));
        QVERIFY(!Util::hasCompletionMarker("remark: PM3GUI_TEST_1234", token));
    }

    // Pipe mode: command echo, success line, "remark: <token>", no trailing prompt.
    void writeOkPipeMode()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\n"
                         "Writing block no 1, key type:A - FFFFFFFFFFFF\n"
                         "[+] Write ( ok )\n"
                         "[+] 2026-11-10T11:53:37Z remark: %TOKEN%\n");

        QElapsedTimer timer;
        timer.start();
        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.successPatternMatched);
        QVERIFY(result.completionMarkerSeen);
        QVERIFY(!result.promptSeen);
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QString("write waited %1 ms after the success line")
                                                    .arg(timer.elapsed())));
    }

    // The pipe client may split "pm3 --> " from the command text. In marker
    // mode that first chunk is not a boundary; only the marker reply is.
    void promptSplitDoesNotCompleteWrite()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        int stage = 0;
        QObject::connect(&util, &Util::write, &util, [&util, &stage](QString wire) {
            const QString token = wire.mid(wire.indexOf("rem ") + 4).trimmed();
            QTimer::singleShot(0, &util, [&util]() {
                util.processOutput("[usb|script] pm3 --> ");
            });
            QTimer::singleShot(100, &util, [&util]() {
                util.processOutput(" hf mf wrbl --blk 1\n");
            });
            QTimer::singleShot(200, &util, [&util]() {
                util.processOutput("[+] Write ( ok )\n");
            });
            QTimer::singleShot(300, &util, [&util, &stage, token]() {
                util.processOutput("[+] 2026-11-10T11:53:37Z remark: " + token + "\n");
                stage = 3;
            });
        });

        QElapsedTimer timer;
        timer.start();
        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 2000));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.successPatternMatched);
        QVERIFY(result.completionMarkerSeen);
        QVERIFY(!result.promptSeen);
        QCOMPARE(stage, 3);
        QVERIFY2(timer.elapsed() >= 280,
                 qPrintable(QString("completed before the marker: %1 ms").arg(timer.elapsed())));
    }

    void promptSplitDoesNotCompleteEmulatorWrite()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["emulator write block"].toMap();
        Util util;
        util.setRunningState(true);
        int stage = 0;
        QObject::connect(&util, &Util::write, &util, [&util, &stage](QString wire) {
            const QString token = wire.mid(wire.indexOf("rem ") + 4).trimmed();
            QTimer::singleShot(0, &util, [&util]() {
                util.processOutput("[usb|script] pm3 --> ");
            });
            QTimer::singleShot(100, &util, [&util]() {
                util.processOutput(" hf mf esetblk --blk 1\n");
            });
            QTimer::singleShot(300, &util, [&util, &stage, token]() {
                util.processOutput("[+] 2026-11-10T11:53:37Z remark: " + token + "\n");
                stage = 2;
            });
        });

        QElapsedTimer timer;
        timer.start();
        Util::ExecResult result = util.execCMDWithResult("hf mf esetblk --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 2000));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.completionMarkerSeen);
        QVERIFY(!result.successPatternMatched);
        QCOMPARE(stage, 2);
        QVERIFY2(timer.elapsed() >= 280,
                 qPrintable(QString("emulator completed on the prompt: %1 ms").arg(timer.elapsed())));
    }

    void staleTokenDoesNotComplete()
    {
        QVERIFY(!Util::hasCompletionMarker("remark: PM3GUI_OLD_TOKEN", "PM3GUI_NEW_TOKEN"));
        QVERIFY(Util::hasCompletionMarker("remark: PM3GUI_NEW_TOKEN", "PM3GUI_NEW_TOKEN"));
    }

    // Interactive clients are still supported: a trailing prompt is a boundary.
    void writeOkInteractivePrompt()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "hf mf wrbl --blk 1\n"
                         "Writing block no 1, key type:A - FFFFFFFFFFFF\n"
                         "[+] Write ( ok )\n"
                         "[usb] pm3 --> ");

        Util::ReturnTrigger trigger = Pm3Cmd::makeWriteTrigger(config, 300);
        trigger.useCompletionMarker = false;
        trigger.requireCompletion = true; // explicit interactive-prompt mode
        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1", trigger);
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.successPatternMatched);
        QVERIFY(result.promptSeen);
        QVERIFY(!result.completionMarkerSeen);
    }

    // A success line without any completion boundary is not a completed command.
    void writeSuccessWithoutBoundaryIsTimeout()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\n"
                         "[+] Write ( ok )\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_TIMEOUT);
        QVERIFY(result.successPatternMatched);
        QVERIFY(!result.isOk());
    }

    void writeTimeoutIsFailure()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\n[-] command execution time out\n");

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
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\n[!] This command is not available in this mode\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_FAILED);
    }

    // The command echo contains the token; only "remark: <token>" is success.
    void echoOnlyIsFailure()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1 -a -k FFFFFFFFFFFF -d 00 --force\n"
                         "Writing block no 1, key type:A - FFFFFFFFFFFF\n"
                         "[+] 2026-11-10T11:53:37Z remark: %TOKEN%\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_FAILED);
        QVERIFY(result.completionMarkerSeen);
    }

    void splitOutputStillCompletes()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["normal write block"].toMap();
        Util util;
        util.setRunningState(true);
        int stage = 0;
        QObject::connect(&util, &Util::write, &util, [&util, &stage](QString wire) {
            const QString token = wire.mid(wire.indexOf("rem ") + 4).trimmed();
            QTimer::singleShot(0, &util, [&util]() {
                util.processOutput("[+] Writ");
            });
            QTimer::singleShot(20, &util, [&util]() {
                util.processOutput("e ( ok )\n[+] remark: ");
            });
            QTimer::singleShot(50, &util, [&util, &stage, token]() {
                util.processOutput(token + "\n");
                stage++;
            });
        });

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QCOMPARE(stage, 1);
        QVERIFY(result.completionMarkerSeen);
    }

    void emulatorWriteCompletesOnMarker()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["emulator write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf esetblk --blk 1\n"
                         "[+] 2026-11-10T11:53:37Z remark: %TOKEN%\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf esetblk --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(!result.successPatternMatched);
        QVERIFY(result.completionMarkerSeen);
    }

    void emulatorWriteWithoutBoundaryTimesOut()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["emulator write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf esetblk --blk 1\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf esetblk --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_TIMEOUT);
    }

    // "Writing block number" is printed before the write; it is not completion.
    void magicWritePartialIsTimeout()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["Magic Card write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\nWriting block number: 1\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_TIMEOUT);
        QVERIFY(result.successPatternMatched);
    }

    void magicWriteCompletesOnMarker()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json");
        QVariantMap config = profile["mifare classic"].toMap()["Magic Card write block"].toMap();
        Util util;
        util.setRunningState(true);
        armFixture(util, "[offline|script] pm3 --> hf mf wrbl --blk 1\n"
                         "Writing block number: 1\n"
                         "[+] 2026-11-10T11:53:37Z remark: %TOKEN%\n");

        Util::ExecResult result = util.execCMDWithResult("hf mf wrbl --blk 1",
                                                          Pm3Cmd::makeWriteTrigger(config, 300));
        QCOMPARE(result.status, Util::EXEC_OK);
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

    void connectAppliesCapabilityManifest_data()
    {
        QTest::addColumn<bool>("lfEnabled");
        QTest::addColumn<bool>("isoEnabled");
        QTest::newRow("lf-removed") << false << false;
        QTest::newRow("lf-kept") << true << true;
    }

    void connectAppliesCapabilityManifest()
    {
        QFETCH(bool, lfEnabled);
        QFETCH(bool, isoEnabled);

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
        manifest.write(QString("{\"manifest_version\":1,\"firmware\":{\"version\":\"v4.23346\"},"
                               "\"features\":{\"lf\":%1,\"iso14443a\":%2,\"hfsniff\":false},"
                               "\"removed\":[]}")
                       .arg(lfEnabled ? "true" : "false")
                       .arg(isoEnabled ? "true" : "false")
                       .toUtf8());
        manifest.close();

        DialogGuard guard;
        MainWindow window;
        window.initUI();
        QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
        QTabWidget* tabs = window.findChild<QTabWidget*>("funcTab");
        QWidget* mifareTab = window.findChild<QWidget*>("mifareTab");
        QWidget* lfTab = window.findChild<QWidget*>("lfTab");
        QLabel* status = window.findChild<QLabel*>("connectStatusBar");
        QVERIFY(pathBox != nullptr);
        QVERIFY(tabs != nullptr);
        QVERIFY(mifareTab != nullptr);
        QVERIFY(lfTab != nullptr);
        QVERIFY(status != nullptr);

        QWidget* t55xxWidget = nullptr;
        for(QDockWidget* dock : window.findChildren<QDockWidget*>())
        {
            if(dock->windowTitle().contains("T55", Qt::CaseInsensitive))
            {
                t55xxWidget = dock->widget();
                break;
            }
        }
        QVERIFY2(t55xxWidget != nullptr, "the dynamic T55xx page must be in a dock");

        pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());
        QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
        QTest::qWait(1500);

        // Collect the observations first: an early return would leave the client
        // process and its thread running and hang the test run.
        const bool mifareDisabled = !mifareTab->isEnabled();
        const bool lfDisabled = !lfTab->isEnabled();
        const bool t55xxWidgetDisabled = !t55xxWidget->isEnabled();
        const bool mifareTip = !mifareTab->toolTip().isEmpty();
        const bool lfTip = !lfTab->toolTip().isEmpty();
        const bool t55xxTip = !t55xxWidget->toolTip().isEmpty();
        const bool connected = status->text().contains("Connected");

        QMetaObject::invokeMethod(&window, "on_PM3_disconnectButton_clicked");
        QTest::qWait(1000);
        QDir::setCurrent(oldCwd);
        QVERIFY2(guard.texts.isEmpty(), qPrintable(guard.joined()));
        QCOMPARE(mifareDisabled, !isoEnabled);
        QCOMPARE(lfDisabled, !lfEnabled);
        QCOMPARE(t55xxWidgetDisabled, !lfEnabled);
        QCOMPARE(mifareTip, !isoEnabled);
        QCOMPARE(lfTip, !lfEnabled);
        QCOMPARE(t55xxTip, !lfEnabled);
        QVERIFY2(connected, qPrintable(status->text()));
    }

    // A real v4.23346 client on a pipe prints no startup prompt and no empty
    // prompt after a command: the handshake must not wait for either.
    void realPipeHandshakeCompletes()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_STARTUP_DELAY_MS", "100");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_STARTUP_DELAY_MS");

        QVERIFY2(observation.stateCount == 1,
                 qPrintable(QString("connected signals: %1, failed: %2, elapsed: %3 ms")
                            .arg(observation.stateCount).arg(observation.failedCount).arg(observation.elapsedMs)));
        QCOMPARE(observation.failedCount, 0);
        QCOMPARE(observation.versionCount, 1);
        QVERIFY(observation.stateValue);
        QVERIFY2(observation.startMs >= 0, qPrintable(QString("start phase %1 ms").arg(observation.startMs)));
        QVERIFY2(observation.versionMs >= 90,
                 qPrintable(QString("startup delay was not measured separately: version %1 ms").arg(observation.versionMs)));
        QVERIFY2(observation.versionMs <= observation.handshakeMs + 5,
                 qPrintable(QString("version %1 ms, boundary %2 ms").arg(observation.versionMs).arg(observation.handshakeMs)));
        QVERIFY2(observation.handshakeMs < 2000,
                 qPrintable(QString("handshake took %1 ms").arg(observation.handshakeMs)));
        QVERIFY2(observation.elapsedMs < 3000,
                 qPrintable(QString("connect took %1 ms").arg(observation.elapsedMs)));
        QVERIFY2(observation.output.contains("remark: PM3GUI_CONNECT_"),
                 qPrintable(observation.output));
    }

    void splitAnsiCrlfHandshake()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_SPLIT", "1");
        qputenv("MOCK_ANSI", "1");
        qputenv("MOCK_CRLF", "1");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_SPLIT");
        qunsetenv("MOCK_ANSI");
        qunsetenv("MOCK_CRLF");

        QCOMPARE(observation.stateCount, 1);
        QCOMPARE(observation.failedCount, 0);
        QCOMPARE(observation.versionCount, 1);
        QCOMPARE(observation.version.at(1).toString(), QString("v4.23346-suspect"));
        QVERIFY(observation.handshakeMs < 3000);
    }

    // The echoed "rem <token>" must not complete the handshake; wait for the reply.
    void handshakeWaitsForMarkerNotEcho()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_REM_DELAY_MS", "300");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_REM_DELAY_MS");

        QCOMPARE(observation.stateCount, 1);
        QCOMPARE(observation.failedCount, 0);
        QVERIFY2(observation.handshakeMs >= 250,
                 qPrintable(QString("handshake returned after %1 ms, before the remark line")
                            .arg(observation.handshakeMs)));
    }

    // Version block is already visible; the next rem echo is split after its
    // prompt. Connected must wait for the remark line.
    void handshakeWaitsForMarkerAfterSplitPrompt()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_ECHO_SPLIT_MS", "100");
        qputenv("MOCK_REM_DELAY_MS", "100");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_ECHO_SPLIT_MS");
        qunsetenv("MOCK_REM_DELAY_MS");

        QCOMPARE(observation.stateCount, 1);
        QCOMPARE(observation.failedCount, 0);
        QVERIFY(observation.stateValue);
        QVERIFY2(observation.handshakeMs >= 180,
                 qPrintable(QString("connected before the remark: %1 ms").arg(observation.handshakeMs)));
    }

    void interactivePromptStillConnects()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect", "interactive");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        QCOMPARE(observation.stateCount, 1);
        QCOMPARE(observation.failedCount, 0);
        QCOMPARE(observation.versionCount, 1);
        QVERIFY(observation.output.contains("[offline|script] pm3 --> "));
    }

    void partialVersionFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_PARTIAL_VERSION", "1");
        qputenv("MOCK_EXIT_AFTER_VERSION", "1");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_PARTIAL_VERSION");
        qunsetenv("MOCK_EXIT_AFTER_VERSION");

        QCOMPARE(observation.stateCount, 1);
        QVERIFY(!observation.stateValue);
        QCOMPARE(observation.failedCount, 1);
    }

    void clientExitsDuringHandshakeFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_EXIT_AFTER_START", "1");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        const ConnectObservation observation = runConnect(scriptPath);
        qunsetenv("MOCK_EXIT_AFTER_START");

        QCOMPARE(observation.stateCount, 1);
        QVERIFY(!observation.stateValue);
        QCOMPARE(observation.failedCount, 1);
    }

    void clientStartFailureFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString missing = dir.filePath("does-not-exist");
        const ConnectObservation observation = runConnect(missing, 5000);
        QCOMPARE(observation.stateCount, 1);
        QVERIFY(!observation.stateValue);
        QCOMPARE(observation.failedCount, 1);
    }

    void cancelDuringHandshakeIsImmediate()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_HANDSHAKE_HANG", "1");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        QSignalSpy failedSpy(process, SIGNAL(HWConnectFailed(QString, quint64)));
        thread.start();
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));
        QTest::qWait(300);
        process->requestStop();
        QElapsedTimer timer;
        timer.start();
        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection, Q_ARG(quint64, attempt));
        process->waitForFinished(2000);
        const qint64 elapsed = timer.elapsed();
        thread.quit();
        QVERIFY(thread.wait(5000));
        delete process;
        qunsetenv("MOCK_HANDSHAKE_HANG");

        QCOMPARE(failedSpy.count(), 0); // user cancel is not an error dialog
        QVERIFY(stateSpy.count() >= 1);
        for(const QList<QVariant>& args : stateSpy)
            QCOMPARE(args.at(0).toBool(), false);
        QVERIFY2(elapsed < 3000, qPrintable(QString("cancel took %1 ms").arg(elapsed)));
    }

    // Deterministic: the connect slot is queued but the worker has not started.
    void cancelBeforeWorkerStarts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pidFile = dir.filePath("mock.pid");
        qputenv("MOCK_PID_FILE", pidFile.toUtf8());
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        QSignalSpy failedSpy(process, SIGNAL(HWConnectFailed(QString, quint64)));
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, scriptPath),
                                  Q_ARG(QStringList, QStringList()));
        process->requestStop(); // before the worker enters the slot
        thread.start();
        QTest::qWait(300);
        const int stateCount = stateSpy.count();
        const int failedCount = failedSpy.count();
        thread.quit();
        QVERIFY(thread.wait(5000));
        delete process;
        qunsetenv("MOCK_PID_FILE");

        QCOMPARE(stateCount, 0);
        QCOMPARE(failedCount, 0);
        QVERIFY2(!QFile::exists(pidFile), "a cancelled queued connect must not start the client");
    }

    // Queued true/error from an old attempt must not change the new connection.
    void staleSignalsIgnoredAfterCancel()
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
        {
            MainWindow window;
            window.initUI();
            QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
            QLabel* status = window.findChild<QLabel*>("connectStatusBar");
            QPushButton* connectButton = window.findChild<QPushButton*>("PM3_connectButton");
            QVERIFY(pathBox != nullptr);
            QVERIFY(status != nullptr);
            QVERIFY(connectButton != nullptr);
            pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());

            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QTest::qWait(1200);
            QVERIFY2(status->text().contains("Connected"), qPrintable(status->text()));
            const quint64 oldAttempt = window.activeConnectAttempt;
            QVERIFY(oldAttempt != 0);

            // Cancel, then deliver a success that was already in the GUI queue.
            QMetaObject::invokeMethod(&window, "on_PM3_disconnectButton_clicked");
            window.onPM3StateChanged(true, QString(), oldAttempt);
            QVERIFY2(status->text().contains("Not Connected"), qPrintable(status->text()));

            // New connection; stale signals from the old attempt must be ignored.
            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QTest::qWait(1200);
            QVERIFY2(status->text().contains("Connected"), qPrintable(status->text()));
            const quint64 newAttempt = window.activeConnectAttempt;
            QVERIFY(newAttempt != 0);
            QVERIFY(newAttempt != oldAttempt);
            window.onPM3StateChanged(true, QString(), oldAttempt);
            window.onPM3HWConnectFailed(QStringLiteral("stale"), oldAttempt);
            QVERIFY2(status->text().contains("Connected"), qPrintable(status->text()));

            QMetaObject::invokeMethod(&window, "on_PM3_disconnectButton_clicked");
            QTest::qWait(800);
        }
        QDir::setCurrent(oldCwd);
        QVERIFY2(guard.texts.isEmpty(), qPrintable(guard.joined()));
    }

    // A second click while connecting must not start another client process.
    void doubleClickStartsOneClient()
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

        qputenv("MOCK_STARTUP_DELAY_MS", "300");
        const QString pidFile = dir.filePath("mock.pid");
        qputenv("MOCK_PID_FILE", pidFile.toUtf8());
        const QString scriptPath = makeMockClient(dir.filePath("client"), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        DialogGuard guard;
        {
            MainWindow window;
            window.initUI();
            QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
            QVERIFY(pathBox != nullptr);
            pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());
            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QTest::qWait(1500);
        }
        QDir::setCurrent(oldCwd);
        qunsetenv("MOCK_STARTUP_DELAY_MS");
        qunsetenv("MOCK_PID_FILE");
        QVERIFY2(guard.texts.isEmpty(), qPrintable(guard.joined()));
        QCOMPARE(mockPids(pidFile).size(), 1);
    }

    // A failed handshake must not leave the button disabled or the status stuck
    // on "Connecting...".
    void connectFailureRestoresUi()
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

        qputenv("MOCK_EXIT_AFTER_START", "1");
        const QString scriptPath = makeMockClient(dir.filePath("client"), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        DialogGuard guard;
        {
            MainWindow window;
            window.initUI();
            QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
            QPushButton* connectButton = window.findChild<QPushButton*>("PM3_connectButton");
            QLabel* status = window.findChild<QLabel*>("connectStatusBar");
            QVERIFY(pathBox != nullptr);
            QVERIFY(connectButton != nullptr);
            QVERIFY(status != nullptr);
            pathBox->setCurrentText("../client/" + QFileInfo(scriptPath).fileName());
            QVERIFY(QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"));
            QTest::qWait(1200);
            QVERIFY(connectButton->isEnabled());
            QVERIFY2(status->text().contains("Not Connected"), qPrintable(status->text()));
        }
        QDir::setCurrent(oldCwd);
        qunsetenv("MOCK_EXIT_AFTER_START");
        QVERIFY2(!guard.texts.isEmpty(), "the failure must be reported to the user");
    }

#ifndef Q_OS_WIN
    // Real Linux v4.23346 client through QProcess pipes, no device attached.
    void realClientPipeHandshake()
    {
        QString client = qEnvironmentVariable("REAL_PM3_CLIENT");
        if(client.isEmpty())
            client = QStringLiteral("/work/src/proxmark3-4.23346/client/proxmark3");
        const QFileInfo info(client);
        if(!info.isFile() || !info.isExecutable())
            QSKIP("real Linux client not available");

        const ConnectObservation observation = runConnect(client, 15000);
        QCOMPARE(observation.stateCount, 1); // state update to Not Connected
        QVERIFY(!observation.stateValue); // never Connected without a device
        QCOMPARE(observation.failedCount, 1);
        QVERIFY(observation.versionCount == 1);
        QCOMPARE(observation.version.at(0).toString(), QString("v4.23346-suspect"));
        QVERIFY(observation.version.at(1).toString().isEmpty());
        QVERIFY2(observation.output.contains("remark: PM3GUI_CONNECT_"), qPrintable(observation.output));
        QVERIFY2(observation.handshakeMs < 5000,
                 qPrintable(QString("real client handshake took %1 ms").arg(observation.handshakeMs)));
    }
#endif

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

        const ConnectObservation observation = runConnect(scriptPath);

        QCOMPARE(observation.versionCount, 1);
        QCOMPARE(observation.version.at(0).toString(), QString("v4.23346-suspect"));
        QCOMPARE(observation.version.at(3).toBool(), expectMismatch);
        QVERIFY(observation.version.at(4).toBool());
        if(expectState)
        {
            QCOMPARE(observation.stateCount, 1);
            QVERIFY(observation.stateValue);
            QCOMPARE(observation.version.at(1).toString(), osVersion);
            QVERIFY(observation.version.at(2).toString().contains("PM3 GENERIC"));
            QCOMPARE(observation.failedCount, 0);
        }
        else
        {
            QCOMPARE(observation.stateCount, 1);
            QVERIFY(!observation.stateValue);
            QCOMPARE(observation.failedCount, 1);
            QVERIFY2(observation.failedReasons.first().contains("firmware"),
                     qPrintable(observation.failedReasons.join(" | ")));
        }
    }

    // End to end QProcess test: a real client process is started, never answers
    // the write command, the wait times out and the process is closed again.
    void mockProcessCommandTimeout()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_WRITE_MODE", "silent");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        Util util;
        QObject::connect(process, &PM3Process::newOutput, &util, &Util::processOutput);
        QObject::connect(&util, &Util::write, process, &PM3Process::write);
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        thread.start();
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));

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

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection, Q_ARG(quint64, attempt));
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
        qunsetenv("MOCK_WRITE_MODE");
    }

    // Read-back verification through a real QProcess pipe: normal success,
    // mismatch (3 attempts) and emulator success. No trailing prompt exists.
    void writeBlockVerifiesReadBack()
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
        QObject::connect(process, &PM3Process::PM3StatedChanged, &util,
                         [&util](bool st, const QString&, quint64) { util.setRunningState(st); });
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        thread.start();
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));
        QElapsedTimer timer;
        timer.start();
        while(timer.elapsed() < 10000 && stateSpy.count() == 0)
        {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
        QCOMPARE(stateSpy.count(), 1);
        util.setRunningState(true); // the queued state slot may not have run yet

        Ui::MainWindow ui;
        QMainWindow host;
        ui.setupUi(&host);
        Mifare mifare(&ui, &util);
        mifare.setConfigMap(loadProfile("config_rrgv4.23346.json")["mifare classic"].toMap());

        const QString data = "000102030405060708090A0B0C0D0E0F";
        QElapsedTimer writeTimer;
        writeTimer.start();
        QVERIFY2(mifare._writeblk(1, Mifare::KEY_A, "FFFFFFFFFFFF", data, Mifare::TARGET_MIFARE, 300),
                 "normal write/read-back must succeed");
        QVERIFY2(writeTimer.elapsed() < 3000,
                 qPrintable(QString("normal write took %1 ms").arg(writeTimer.elapsed())));
        QVERIFY2(!mifare._writeblk(1, Mifare::KEY_A, "FFFFFFFFFFFF",
                                   "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", Mifare::TARGET_UID, 300),
                 "a read-back mismatch must fail");
        QVERIFY2(mifare._writeblk(1, Mifare::KEY_A, "FFFFFFFFFFFF", data, Mifare::TARGET_EMULATOR, 300),
                 "emulator write/read-back must succeed");

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection, Q_ARG(quint64, attempt));
        timer.restart();
        while(timer.elapsed() < 5000 && process->state() != QProcess::NotRunning)
        {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
        thread.quit();
        QVERIFY(thread.wait(5000));
        delete process;
    }

    void writeBlockReadBackFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("MOCK_READ_MODE", "silent");
        const QString scriptPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!scriptPath.isEmpty());

        QThread thread;
        PM3Process* process = new PM3Process(&thread);
        Util util;
        QObject::connect(process, &PM3Process::newOutput, &util, &Util::processOutput);
        QObject::connect(&util, &Util::write, process, &PM3Process::write);
        QObject::connect(process, &PM3Process::PM3StatedChanged, &util,
                         [&util](bool st, const QString&, quint64) { util.setRunningState(st); });
        QSignalSpy stateSpy(process, SIGNAL(PM3StatedChanged(bool, QString, quint64)));
        thread.start();
        const quint64 attempt = process->beginConnect();
        QMetaObject::invokeMethod(process, "connectPM3", Qt::QueuedConnection,
                                  Q_ARG(quint64, attempt), Q_ARG(QString, scriptPath), Q_ARG(QStringList, QStringList()));
        QElapsedTimer timer;
        timer.start();
        while(timer.elapsed() < 10000 && stateSpy.count() == 0)
        {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
        QCOMPARE(stateSpy.count(), 1);
        util.setRunningState(true); // the queued state slot may not have run yet

        Ui::MainWindow ui;
        QMainWindow host;
        ui.setupUi(&host);
        Mifare mifare(&ui, &util);
        mifare.setConfigMap(loadProfile("config_rrgv4.23346.json")["mifare classic"].toMap());
        QVERIFY2(!mifare._writeblk(1, Mifare::KEY_A, "FFFFFFFFFFFF",
                                   "000102030405060708090A0B0C0D0E0F", Mifare::TARGET_UID, 300),
                 "an unreadable block must fail the write");

        QMetaObject::invokeMethod(process, "killPM3", Qt::BlockingQueuedConnection, Q_ARG(quint64, attempt));
        timer.restart();
        while(timer.elapsed() < 5000 && process->state() != QProcess::NotRunning)
        {
            QCoreApplication::processEvents();
            QTest::qWait(10);
        }
        thread.quit();
        QVERIFY(thread.wait(5000));
        delete process;
        qunsetenv("MOCK_READ_MODE");
    }

    // The Traditional Chinese translation must be built in and registered.
    void traditionalChineseTranslation()
    {
        QTranslator tw;
        QVERIFY(tw.load(QStringLiteral(":/i18n/zh_TW.qm")));
        QCOMPARE(tw.translate("Util", "Choose a language:"), QString::fromUtf8("選擇語言："));
        QCOMPARE(tw.translate("MainWindow", "Client Path:"), QString::fromUtf8("客戶端路徑："));

        QSettings languages(QStringLiteral(":/i18n/languages.ini"), QSettings::IniFormat);
        languages.setIniCodec("UTF-8");
        languages.beginGroup("Languages");
        QVERIFY(languages.allKeys().contains(QStringLiteral("zh_TW")));
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

    // ******************** MFC document / recovery ********************

    void mfcSpecMapping()
    {
        const Mfc::Spec mini = Mfc::Spec::get(Mfc::CardType::Mini);
        QCOMPARE(mini.sectorCount, 5);
        QCOMPARE(mini.blockCount, 20);
        QCOMPARE(mini.trailerBlockOfSector(4), 19);

        const Mfc::Spec m1k = Mfc::Spec::get(Mfc::CardType::M1K);
        QCOMPARE(m1k.trailerBlockOfSector(15), 63);
        QCOMPARE(m1k.sectorOfBlock(63), 15);
        QVERIFY(m1k.isTrailerBlock(63));
        QVERIFY(!m1k.isTrailerBlock(62));

        const Mfc::Spec m2k = Mfc::Spec::get(Mfc::CardType::M2K);
        QCOMPARE(m2k.trailerBlockOfSector(31), 127);

        const Mfc::Spec m4k = Mfc::Spec::get(Mfc::CardType::M4K);
        QCOMPARE(m4k.blocksInSector(31), 4);
        QCOMPARE(m4k.blocksInSector(32), 16);
        QCOMPARE(m4k.firstBlockOfSector(32), 128);
        QCOMPARE(m4k.trailerBlockOfSector(32), 143);
        QCOMPARE(m4k.sectorOfBlock(143), 32);
        QCOMPARE(m4k.trailerBlockOfSector(39), 255);
        QCOMPARE(m4k.sectorOfBlock(255), 39);
        QCOMPARE(m4k.sectorOfBlock(256), -1);
    }

    void mfcKeyLayoutRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Mfc::Document doc(Mfc::CardType::M1K);
        for(int s = 0; s < doc.sectorCount(); s++)
        {
            const QString a = QStringLiteral("%1").arg(s + 1, 12, 16, QLatin1Char('0')).toUpper();
            const QString b = QStringLiteral("%1").arg(0x100 + s, 12, 16, QLatin1Char('0')).toUpper();
            doc.setKey(s, 'A', a, Mfc::KeyState::Verified, Mfc::KeySource::Manual);
            doc.setKey(s, 'B', b, Mfc::KeyState::Pending, Mfc::KeySource::Imported);
        }

        const QString rrgPath = dir.filePath("key-rrg.bin");
        Mfc::IoResult saved = doc.saveKeyFile(rrgPath, "rrg");
        QVERIFY2(saved.ok, qPrintable(saved.error));
        QFile rrg(rrgPath);
        QVERIFY(rrg.open(QFile::ReadOnly));
        const QByteArray rrgBytes = rrg.readAll();
        QCOMPARE(rrgBytes.size(), 16 * 12);
        QCOMPARE(rrgBytes.mid(0, 6).toHex().toUpper(), QByteArray("000000000001"));
        QCOMPARE(rrgBytes.mid(6, 6).toHex().toUpper(), QByteArray("000000000002"));
        // RRG layout: all key A first, key B starts at sector count * 6.
        QCOMPARE(rrgBytes.mid(16 * 6, 6).toHex().toUpper(), QByteArray("000000000100"));

        const QString legacyPath = dir.filePath("key-legacy.bin");
        QVERIFY(doc.saveKeyFile(legacyPath, "legacy").ok);
        QFile legacy(legacyPath);
        QVERIFY(legacy.open(QFile::ReadOnly));
        const QByteArray legacyBytes = legacy.readAll();
        QCOMPARE(legacyBytes.mid(0, 6).toHex().toUpper(), QByteArray("000000000001"));
        QCOMPARE(legacyBytes.mid(6, 6).toHex().toUpper(), QByteArray("000000000100"));

        Mfc::Document loaded;
        Mfc::IoResult result = Mfc::Document::loadKeyFile(rrgPath, "rrg", &loaded);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(loaded.cardType(), Mfc::CardType::M1K);
        QCOMPARE(loaded.key(1, 'A').value, QString("000000000002"));
        QCOMPARE(loaded.key(0, 'B').value, QString("000000000100"));
        QCOMPARE(loaded.key(0, 'A').state, Mfc::KeyState::Pending);

        Mfc::Document legacyLoaded;
        QVERIFY(Mfc::Document::loadKeyFile(legacyPath, "legacy", &legacyLoaded).ok);
        QCOMPARE(legacyLoaded.key(1, 'A').value, QString("000000000002"));
        QCOMPARE(legacyLoaded.key(0, 'B').value, QString("000000000100"));

        Mfc::Document ambiguous;
        Mfc::IoResult autoResult = Mfc::Document::loadKeyFile(rrgPath, "auto", &ambiguous);
        QVERIFY(!autoResult.ok);
        QVERIFY(autoResult.ambiguousLayout);
    }

    void mfcDataRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Mfc::Document doc(Mfc::CardType::M1K);
        doc.uid = "11223344";
        QVERIFY(doc.setBlock(0, "11223344556677889900AABBCCDDEEFF", Mfc::BlockState::Read));
        QVERIFY(doc.setBlock(1, "000102030405060708090A0B0C0D0E0F", Mfc::BlockState::Modified));
        QVERIFY(doc.setBlock(2, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", Mfc::BlockState::Read));
        QCOMPARE(doc.completeBlockCount(), 3);
        QCOMPARE(doc.unknownBlockCount(), 61);
        QCOMPARE(doc.modifiedBlockCount(), 1);

        const QString binPath = dir.filePath("dump.bin");
        Mfc::IoResult binResult = doc.saveDataFile(binPath, "bin");
        QVERIFY(!binResult.ok); // 61 unknown blocks must not silently become 00

        Mfc::IoResult binAllowed = doc.saveDataFile(binPath, "bin", true);
        QVERIFY2(binAllowed.ok, qPrintable(binAllowed.error));
        QVERIFY(!binAllowed.warning.isEmpty());
        QFile bin(binPath);
        QVERIFY(bin.open(QFile::ReadOnly));
        QCOMPARE(bin.size(), 1024);

        Mfc::Document fromBin;
        Mfc::IoResult loadBin = Mfc::Document::loadDataFile(binPath, "bin", &fromBin);
        QVERIFY2(loadBin.ok, qPrintable(loadBin.error));
        QCOMPARE(fromBin.cardType(), Mfc::CardType::M1K);
        QCOMPARE(fromBin.blockHex(0), QString("11223344556677889900AABBCCDDEEFF"));
        QCOMPARE(fromBin.blockState(3), Mfc::BlockState::Read);
        QCOMPARE(fromBin.uid, QString("11223344"));

        const QString emlPath = dir.filePath("dump.eml");
        QVERIFY(doc.saveDataFile(emlPath, "eml").ok);
        QFile eml(emlPath);
        QVERIFY(eml.open(QFile::ReadOnly));
        const QList<QByteArray> lines = eml.readAll().split('\n');
        QCOMPARE(lines.size(), 65); // 64 lines + trailing newline
        QCOMPARE(lines.at(0), QByteArray("11223344556677889900AABBCCDDEEFF"));
        QCOMPARE(lines.at(5), QByteArray("????????????????????????????????"));

        Mfc::Document fromEml;
        Mfc::IoResult loadEml = Mfc::Document::loadDataFile(emlPath, "eml", &fromEml);
        QVERIFY2(loadEml.ok, qPrintable(loadEml.error));
        QCOMPARE(fromEml.blockState(0), Mfc::BlockState::Read);
        QCOMPARE(fromEml.blockState(5), Mfc::BlockState::Unknown);
        QCOMPARE(fromEml.unknownBlockCount(), 61);

        const QString jsonPath = dir.filePath("dump.json");
        QVERIFY(doc.saveDataFile(jsonPath, "json").ok);
        Mfc::Document fromJson;
        QVERIFY(Mfc::Document::loadDataFile(jsonPath, "auto", &fromJson).ok);
        QCOMPARE(fromJson.cardType(), Mfc::CardType::M1K);
        QCOMPARE(fromJson.uid, doc.uid);
        QCOMPARE(fromJson.blockState(1), Mfc::BlockState::Modified);
        QCOMPARE(fromJson.blockState(5), Mfc::BlockState::Unknown);
        QCOMPARE(fromJson.sha256(), doc.sha256());
    }

    void mfcOfficialJsonImport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QJsonObject root;
        root["FileType"] = "mfc v2";
        QJsonObject card;
        card["UID"] = "11223344";
        root["Card"] = card;
        QJsonObject blocks;
        for(int i = 0; i < 64; i++)
            blocks[QString::number(i)] = QString("%1").arg(i, 32, 16, QLatin1Char('0')).toUpper();
        root["blocks"] = blocks;
        QJsonObject sectors;
        QJsonObject sector0;
        sector0["KeyA"] = "FFFFFFFFFFFF";
        sector0["KeyB"] = "1234567890AB";
        sectors["0"] = sector0;
        root["SectorKeys"] = sectors;

        const QString path = dir.filePath("official.json");
        QFile file(path);
        QVERIFY(file.open(QFile::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();

        Mfc::Document doc;
        const Mfc::IoResult result = Mfc::Document::loadDataFile(path, "auto", &doc);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(doc.cardType(), Mfc::CardType::M1K);
        QCOMPARE(doc.uid, QString("11223344"));
        QCOMPARE(doc.blockHex(1), QString("00000000000000000000000000000001"));
        QCOMPARE(doc.blockState(1), Mfc::BlockState::Read);
        QCOMPARE(doc.key(0, 'A').value, QString("FFFFFFFFFFFF"));
        QCOMPARE(doc.key(0, 'B').value, QString("1234567890AB"));
        QCOMPARE(doc.key(0, 'A').state, Mfc::KeyState::Pending);
    }

    void mfcImportValidation()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Mfc::Document doc(Mfc::CardType::M1K);
        QVERIFY(doc.setBlock(0, "AABBCCDDEEFF00112233445566778899", Mfc::BlockState::Read));

        const QString shortBin = dir.filePath("short.bin");
        QFile shortFile(shortBin);
        QVERIFY(shortFile.open(QFile::WriteOnly));
        shortFile.write(QByteArray(100, '\0'));
        shortFile.close();
        Mfc::IoResult result = Mfc::Document::loadDataFile(shortBin, "bin", &doc);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("size"));
        QCOMPARE(doc.blockHex(0), QString("AABBCCDDEEFF00112233445566778899"));

        const QString badEml = dir.filePath("bad.eml");
        QFile bad(badEml);
        QVERIFY(bad.open(QFile::WriteOnly));
        QByteArray eml;
        for(int i = 0; i < 64; i++)
            eml += (i == 3) ? "ZZ\n" : "00000000000000000000000000000000\r\n";
        bad.write(eml);
        bad.close();
        result = Mfc::Document::loadDataFile(badEml, "eml", &doc);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("line 4"));
        QCOMPARE(doc.blockHex(0), QString("AABBCCDDEEFF00112233445566778899"));

        const QString truncated = dir.filePath("trunc.eml");
        QFile truncFile(truncated);
        QVERIFY(truncFile.open(QFile::WriteOnly));
        truncFile.write("00000000000000000000000000000000\n");
        truncFile.close();
        result = Mfc::Document::loadDataFile(truncated, "eml", &doc);
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("1 lines"));

        const QString keyText = dir.filePath("keys.txt");
        QFile keyFile(keyText);
        QVERIFY(keyFile.open(QFile::WriteOnly));
        keyFile.write("0:A:FFFFFFFFFFFF\n0:B:1234567890AB\n1:A:AAAABBBBCCCC\n");
        keyFile.close();
        Mfc::Document textDoc;
        result = Mfc::Document::loadKeyFile(keyText, "auto", &textDoc);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(textDoc.cardType(), Mfc::CardType::M1K);
        QCOMPARE(textDoc.key(0, 'A').value, QString("FFFFFFFFFFFF"));
        QCOMPARE(textDoc.key(0, 'B').value, QString("1234567890AB"));
        QCOMPARE(textDoc.key(1, 'A').value, QString("AAAABBBBCCCC"));
        QCOMPARE(textDoc.key(0, 'A').state, Mfc::KeyState::Pending);
    }

    void mfcCompare()
    {
        Mfc::Document a(Mfc::CardType::M1K);
        Mfc::Document b(Mfc::CardType::M1K);
        a.uid = "11223344";
        b.uid = "11223344";
        for(int i = 0; i < a.blockCount(); i++)
        {
            const QString data = QString("%1").arg(i, 32, 16, QLatin1Char('0')).toUpper();
            a.setBlock(i, data, Mfc::BlockState::Read);
            b.setBlock(i, data, Mfc::BlockState::Read);
        }
        Mfc::CompareResult same = a.compare(b);
        QCOMPARE(same.same, 64);
        QCOMPARE(same.different, 0);
        QCOMPARE(same.unknown, 0);
        QCOMPARE(same.incomparable, 0);

        b.setBlock(1, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", Mfc::BlockState::Read);
        b.setBlock(2, QString(), Mfc::BlockState::Unknown);
        Mfc::CompareResult diff = a.compare(b);
        QCOMPARE(diff.different, 1);
        QCOMPARE(diff.unknown, 1);
        QCOMPARE(diff.same, 62);

        Mfc::Document c(Mfc::CardType::M2K);
        c.uid = "55667788";
        for(int i = 0; i < c.blockCount(); i++)
            c.setBlock(i, QString("%1").arg(i, 32, 16, QLatin1Char('0')).toUpper(), Mfc::BlockState::Read);
        Mfc::CompareResult capacity = a.compare(c);
        QVERIFY(capacity.capacityMismatch);
        QVERIFY(capacity.uidMismatch);
        QCOMPARE(capacity.incomparable, 64);
        QCOMPARE(capacity.same, 64);
    }

    void mfcRecoveryParser()
    {
        const Mfc::Spec spec = Mfc::Spec::get(Mfc::CardType::M1K);
        const QString output =
            "-----+-----+--------------+---+--------------+----\n"
            " Sec | Blk | key A        |res| key B        |res\n"
            "-----+-----+--------------+---+--------------+----\n"
            " 015 | 063 | FFFFFFFFFFFF | D | 1234567890AB | 1 \n"
            " 000 | 003 | A0A1A2A3A4A5 | H | ------------ | 0 \n"
            " 002 | 011 | 111111111111 | 0 | 222222222222 | 0 \n"
            " 002 | 011 | 333333333333 | D | 444444444444 | D \n"
            " 007 | 099 | 555555555555 | D | 666666666666 | D \n"
            " 999 | 003 | 777777777777 | D | 888888888888 | D \n"
            "-----+-----+--------------+---+--------------+----\n"
            "Static enc nonce... yes\n"
            "time in checkkeys 1 seconds\n";
        MfcRecovery::ParseResult parsed = MfcRecovery::parseOutput(output, spec);
        QVERIFY(parsed.sawTable);
        QVERIFY(parsed.sawCompletion);
        QCOMPARE(parsed.nonce, MfcRecovery::NonceType::StaticEncrypted);
        QCOMPARE(parsed.keys.size(), 8); // 4 valid rows x 2 keys; 2 rows were rejected

        auto find = [&parsed](int sector, char type) {
            for(const MfcRecovery::Candidate& c : parsed.keys)
                if(c.sector == sector && c.type == type)
                    return c;
            return MfcRecovery::Candidate();
        };
        QVERIFY(find(15, 'A').found);
        QCOMPARE(find(15, 'A').key, QString("FFFFFFFFFFFF"));
        QVERIFY(find(15, 'B').found);
        QCOMPARE(find(0, 'A').source, Mfc::KeySource::Recovery);
        QVERIFY(!find(0, 'B').found);
        QVERIFY(!find(2, 'A').found); // result 0 is not a hit
        QCOMPARE(find(2, 'A').key, QString("111111111111"));
        QVERIFY(parsed.warnings.size() >= 1); // mismatched block 099 and sector 999

        MfcRecovery::ParseResult staticNonce = MfcRecovery::parseOutput(
            "Static nonce... yes, even when nested\n", spec);
        QCOMPARE(staticNonce.nonce, MfcRecovery::NonceType::SuperStatic);
        MfcRecovery::ParseResult normal = MfcRecovery::parseOutput("Prng....... hard\n", spec);
        QCOMPARE(normal.nonce, MfcRecovery::NonceType::Normal);

        QCOMPARE(MfcRecovery::detectStage("... Hardnested ...\nCalculating..."), QString("compute"));
        QCOMPARE(MfcRecovery::detectStage("Found valid key [ FFFFFFFFFFFF ]"), QString("done"));
    }

    // Real v4.23346 output: the client prefixes every table row with "[+] ",
    // which the row pattern has to tolerate.
    void mfcRecoveryStaticNestedTable()
    {
        const Mfc::Spec spec = Mfc::Spec::get(Mfc::CardType::M1K);
        const QString output =
            "[usb|script] pm3 --> hf mf staticnested --1k --blk 27 -a -k 9BA0707FD128\n"
            "[#] Static nonce....... 009080a2\n"
            "[+] Testing known keys. Sector count 16\n"
            "[+] Time in check keys 1 seconds\n"
            "[+] target block    0 key type A -- found valid key [ 3E632E271345 ]\n"
            "[+] Target block    0 key type B\n"
            "[!] No key found, next try...\n"
            "[+] target block    0 key type B -- found valid key [ 40EF1BA62B93 ]\n"
            "[+] time in static nested 20 seconds\n"
            "[=] Trying to read key B...\n"
            "[+] -----+-----+--------------+---+--------------+----\n"
            "[+]  Sec | Blk | key A        |res| key B        |res\n"
            "[+] -----+-----+--------------+---+--------------+----\n"
            "[+]  000 | 003 | 3E632E271345 | 1 | 40EF1BA62B93 | 1 \n"
            "[+]  001 | 007 | A747DD266F44 | 1 | 2A920A261244 | 1 \n"
            "[+]  002 | 011 | 9DCE30B48D61 | 1 | 28EC08EA7B6C | 1 \n"
            "[+]  003 | 015 | 2D7A10908F45 | 1 | 30AC08E92D24 | 1 \n"
            "[+]  004 | 019 | 2E98643ACD57 | 1 | AFA5576F57D8 | 1 \n"
            "[+]  005 | 023 | B1B17CC93BC4 | 1 | A7AC3DC8A218 | 1 \n"
            "[+]  006 | 027 | 9BA0707FD128 | 1 | 64C847347877 | 1 \n"
            "[+]  007 | 031 | BD396C5F146D | 1 | CFD4270E2C6E | 1 \n"
            "[+]  008 | 035 | 030203EBD753 | 1 | 0D5A31E29487 | 1 \n"
            "[+]  009 | 039 | 08306A62EB5A | 1 | 2297089323BF | 1 \n"
            "[+]  010 | 043 | F67000B1A95A | 1 | 96525AA47438 | 1 \n"
            "[+]  011 | 047 | A9B8AAA4E598 | 1 | 4AE5A8839DC7 | 1 \n"
            "[+]  012 | 051 | E3E254925EAA | 1 | 4CE3B74D9093 | 1 \n"
            "[+]  013 | 055 | 3E2726DD4BCE | 1 | FFCE0756664B | 1 \n"
            "[+]  014 | 059 | 250B159BC7A5 | 1 | F84874D19782 | 1 \n"
            "[+]  015 | 063 | 64F9FD4F4E93 | 1 | 64DFF3ECB19C | 1 \n"
            "[+] -----+-----+--------------+---+--------------+----\n"
            "[+] ( 0:Failed / 1:Success )\n";

        const MfcRecovery::ParseResult parsed = MfcRecovery::parseOutput(output, spec);
        QVERIFY(parsed.sawTable);
        QVERIFY(parsed.sawCompletion);
        QCOMPARE(parsed.nonce, MfcRecovery::NonceType::Static);
        QCOMPARE(parsed.keys.size(), 32);
        for(const MfcRecovery::Candidate& candidate : parsed.keys)
            QVERIFY2(candidate.found,
                     qPrintable(QString("sector %1 key %2 was not marked found")
                                .arg(candidate.sector).arg(candidate.type)));
    }

    // A one-shot recovery run frees the port by stopping the persistent client.
    // That stop reaches Util as setRunningState(false) -> releaseChannel() while
    // the task is already active; the task must keep capturing its output, or
    // commandFinished() carries an empty string and every recovered key is
    // reported as "Method finished without a usable key".
    void isolatedTaskSurvivesClientStop()
    {
        Util util;
        util.setRunningState(true);
        QSignalSpy finished(&util, &Util::commandFinished);
        QObject::connect(&util, &Util::isolatedRunRequested, &util,
                         [&util](const QString&, const QString&) {
            QTimer::singleShot(0, &util, [&util]() {
                // The GUI disconnects the persistent client before the one-shot
                // client starts: the port is released mid-task.
                util.setRunningState(false);
                QVERIFY(!util.isChannelOwned());
                util.processOutput("[+]  Sec | Blk | key A        |res| key B        |res\n");
                util.processOutput("[+]  000 | 003 | 3E632E271345 | 1 | 40EF1BA62B93 | 1 \n");
                util.processOutput("[+]  001 | 007 | A747DD266F44 | 1 | 2A920A261244 | 1 \n");
                util.isolatedFinished(Util::EXEC_OK);
            });
        });

        Util::AsyncCommand command;
        command.cmd = "hf mf staticnested --1k --blk 27 -a -k 9BA0707FD128";
        command.isolated = true;
        command.hardTimeoutMs = 5000;
        QVERIFY(util.startCommand(command));
        QTRY_COMPARE(finished.count(), 1);

        const Util::ExecResult result = finished.first().at(0).value<Util::ExecResult>();
        QCOMPARE(result.status, Util::EXEC_OK);
        QVERIFY(result.output.contains("3E632E271345"));
        const MfcRecovery::ParseResult parsed =
            MfcRecovery::parseOutput(result.output, Mfc::Spec::get(Mfc::CardType::M1K));
        QCOMPARE(parsed.keys.size(), 4);
        QVERIFY(!util.isChannelOwned());
        QVERIFY(!util.isCapturingOutput());
    }

    // hardnested/darkside recover one key and print it without a table; sen
    // prints its keys line by line.  None of them may end up as "no usable key".
    void mfcRecoverySingleKeyOutput()
    {
        const Mfc::Spec spec = Mfc::Spec::get(Mfc::CardType::M1K);

        // Real v4.23346 hardnested output: target block 23 = sector 5 key A.
        const QString hardnested =
            "[=] Target block no  23 target key type: A known target key: 000000000000 (not set)\n"
            "[=]  Time    | #nonces | Activity                                                | #states                   | Estimated\n"
            "[=]       29 |    2657 | Brute force completed. Key found:          B1B17CC93BC4 |                         0 |            0s\n"
            "[=] ---------+---------+---------------------------------------------------------+---------------------------+---------------\n";
        MfcRecovery::Target target;
        target.sector = 5;
        target.type = 'A';
        MfcRecovery::ParseResult parsed = MfcRecovery::parseOutput(hardnested, spec, target);
        QCOMPARE(parsed.keys.size(), 1);
        QCOMPARE(parsed.keys.first().sector, 5);
        QCOMPARE(parsed.keys.first().type, 'A');
        QCOMPARE(parsed.keys.first().key, QString("B1B17CC93BC4"));
        QVERIFY(parsed.keys.first().found);

        // No target, no attribution: warn instead of guessing a sector.
        const MfcRecovery::ParseResult orphan = MfcRecovery::parseOutput(hardnested, spec);
        QVERIFY(orphan.keys.isEmpty());
        QCOMPARE(orphan.warnings.size(), 1);

        // darkside prints the same "Found valid key" line for block 0 key A.
        MfcRecovery::Target block0;
        block0.sector = 0;
        block0.type = 'A';
        const MfcRecovery::ParseResult darkside = MfcRecovery::parseOutput(
            "[+] Found valid key [ 3E632E271345 ]\n[+] Time in darkside 12 seconds\n", spec, block0);
        QCOMPARE(darkside.keys.size(), 1);
        QCOMPARE(darkside.keys.first().sector, 0);
        QCOMPARE(darkside.keys.first().key, QString("3E632E271345"));

        // nested/staticnested also name the block and key type per line.
        const MfcRecovery::ParseResult blocks = MfcRecovery::parseOutput(
            "[+] target block    0 key type A -- found valid key [ 3E632E271345 ]\n"
            "[+] target block   24 key type B -- found valid key [ 64C847347877 ]\n", spec);
        QCOMPARE(blocks.keys.size(), 2);
        QCOMPARE(blocks.keys.at(0).sector, 0);
        QCOMPARE(blocks.keys.at(1).sector, 6);
        QCOMPARE(blocks.keys.at(1).type, 'B');

        // A block that starts no sector of this card cannot name a key.
        const MfcRecovery::ParseResult notFirst = MfcRecovery::parseOutput(
            "[+] target block   25 key type A -- found valid key [ 3E632E271345 ]\n", spec);
        QVERIFY(notFirst.keys.isEmpty());
        QCOMPARE(notFirst.warnings.size(), 1);

        // sen reports sector and key type in both of its result lines.
        const MfcRecovery::ParseResult sen = MfcRecovery::parseOutput(
            "[+] Reader material recovered sec 006 key A = 9BA0707FD128\n"
            "[+] Found initial key: sector 10 block 40 key B = 96525AA47438\n"
            "[=]  0.3 |      12 | Key sec 013 key B = FFCE0756664B |  20/32 keys | 0s \n", spec);
        QCOMPARE(sen.keys.size(), 3);
        QCOMPARE(sen.keys.at(0).sector, 6);
        QCOMPARE(sen.keys.at(0).type, 'A');
        QCOMPARE(sen.keys.at(1).sector, 10);
        QCOMPARE(sen.keys.at(1).type, 'B');
        QCOMPARE(sen.keys.at(2).sector, 13);
        QCOMPARE(sen.keys.at(2).key, QString("FFCE0756664B"));
    }

    void mfcRecoveryMerge()
    {
        Mfc::Document doc(Mfc::CardType::M1K);
        doc.setKeyVerified(0, 'A', "AAAAAAAAAAAA", Mfc::KeySource::Manual);
        doc.setKey(0, 'B', "FFFFFFFFFFFF", Mfc::KeyState::Pending, Mfc::KeySource::Imported);

        MfcRecovery::ParseResult parsed;
        MfcRecovery::Candidate same;
        same.sector = 0;
        same.type = 'A';
        same.key = "AAAAAAAAAAAA";
        same.found = true;
        parsed.keys.append(same);
        MfcRecovery::Candidate conflict;
        conflict.sector = 0;
        conflict.type = 'A';
        conflict.key = "BBBBBBBBBBBB";
        conflict.found = true;
        parsed.keys.append(conflict);
        MfcRecovery::Candidate ff;
        ff.sector = 0;
        ff.type = 'B';
        ff.key = "FFFFFFFFFFFF";
        ff.found = true;
        ff.marker = 'D';
        ff.source = Mfc::KeySource::Dictionary;
        parsed.keys.append(ff);
        MfcRecovery::Candidate added;
        added.sector = 1;
        added.type = 'A';
        added.key = "CCCCCCCCCCCC";
        added.found = true;
        parsed.keys.append(added);

        MfcRecovery::MergeResult merged = MfcRecovery::merge(parsed, &doc, true, Mfc::KeySource::Recovery);
        QCOMPARE(merged.conflicts, 1);
        QCOMPARE(doc.key(0, 'A').value, QString("AAAAAAAAAAAA"));
        QCOMPARE(doc.key(0, 'B').state, Mfc::KeyState::Verified);
        QCOMPARE(doc.key(1, 'A').value, QString("CCCCCCCCCCCC"));
        QCOMPARE(doc.key(1, 'A').state, Mfc::KeyState::Verified);
        QCOMPARE(doc.verifiedKeyCount(), 3);

        Mfc::Document pending(Mfc::CardType::M1K);
        pending.setKey(2, 'A', "DDDDDDDDDDDD", Mfc::KeyState::Pending, Mfc::KeySource::Imported);
        MfcRecovery::ParseResult failed;
        MfcRecovery::Candidate miss;
        miss.sector = 2;
        miss.type = 'A';
        miss.key = "DDDDDDDDDDDD";
        miss.found = false;
        failed.keys.append(miss);
        MfcRecovery::merge(failed, &pending, true, Mfc::KeySource::Recovery);
        QCOMPARE(pending.key(2, 'A').state, Mfc::KeyState::Failed);
    }

    void mfcRecoveryAdvice()
    {
        Mfc::Document empty(Mfc::CardType::M1K);
        auto adviceFor = [](const QVector<MfcRecovery::Advice>& list, MfcRecovery::Method method) {
            for(const MfcRecovery::Advice& advice : list)
                if(advice.method == method)
                    return advice;
            return MfcRecovery::Advice();
        };
        QVector<MfcRecovery::Advice> advice = MfcRecovery::advise(empty, MfcRecovery::NonceType::Unknown);
        QCOMPARE(advice.size(), 9);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Nested).applicable);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::Darkside).applicable);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::Darkside).preconditionUnverified);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Sen).applicable);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::Isen).applicable);

        advice = MfcRecovery::advise(empty, MfcRecovery::NonceType::Static);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::StaticNested).applicable);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Darkside).applicable);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Hardnested).applicable);

        advice = MfcRecovery::advise(empty, MfcRecovery::NonceType::StaticEncrypted);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::Sen).applicable);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Darkside).applicable);

        Mfc::Document complete(Mfc::CardType::M1K);
        for(int s = 0; s < complete.sectorCount(); s++)
        {
            complete.setKeyVerified(s, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual);
            complete.setKeyVerified(s, 'B', "FFFFFFFFFFFF", Mfc::KeySource::Manual);
        }
        advice = MfcRecovery::advise(complete, MfcRecovery::NonceType::Normal);
        QVERIFY(adviceFor(advice, MfcRecovery::Method::KnownKeys).applicable);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::Dictionary).applicable);
        QVERIFY(!adviceFor(advice, MfcRecovery::Method::AutoPwn).applicable);
    }

    void mfcCommandBuilders()
    {
        QVariantMap profile = loadProfile("config_rrgv4.23346.json")["mifare classic"].toMap();
        const Mfc::Spec spec = Mfc::Spec::get(Mfc::CardType::M1K);

        MfcRecovery::Command check = MfcRecovery::buildCheck(profile, spec, QString(), true);
        QCOMPARE(check.text, QString("hf mf chk --1k"));
        // The v4.23346 client polls stdin inside these commands, so they must
        // run in a one-shot process instead of queueing a marker behind them.
        QVERIFY(check.usesStdin);
        QCOMPARE(check.successPattern, QString());
        check = MfcRecovery::buildCheck(profile, spec, "dict.dic", false);
        QCOMPARE(check.text, QString("hf mf chk --1k -f \"dict.dic\" --no-default"));

        MfcRecovery::Command autopwn = MfcRecovery::buildAutoPwn(profile, spec);
        QCOMPARE(autopwn.text, QString("hf mf autopwn --1k --ns"));
        QVERIFY(autopwn.hardTimeoutMs >= 30 * 60 * 1000);
        QVERIFY(autopwn.usesStdin);

        MfcRecovery::Command nested = MfcRecovery::buildNested(profile, spec, false, 1, 'A', "FFFFFFFFFFFF");
        QCOMPARE(nested.text, QString("hf mf nested --1k --blk 7 -a -k FFFFFFFFFFFF"));
        QVERIFY(nested.usesStdin);

        MfcRecovery::Command hard = MfcRecovery::buildHardnested(profile, spec, 0, 'A', "AAAAAAAAAAAA", 2, 'B');
        QCOMPARE(hard.text, QString("hf mf hardnested --blk 3 -a -k AAAAAAAAAAAA --tblk 11 --tb"));
        QVERIFY(hard.usesStdin);

        QCOMPARE(MfcRecovery::buildDarkside(profile).text, QString("hf mf darkside"));
        QCOMPARE(MfcRecovery::buildSen(profile).text, QString("hf mf sen"));
        QCOMPARE(MfcRecovery::buildIsen(profile).text, QString("hf mf isen"));
        QVERIFY(MfcRecovery::buildDarkside(profile).usesStdin);
        QVERIFY(MfcRecovery::buildSen(profile).usesStdin);
        QVERIFY(MfcRecovery::buildIsen(profile).usesStdin);
    }


    void asyncCommandHasNoIdleTimeout()
    {
        Util util;
        util.setRunningState(true);
        QSignalSpy finished(&util, &Util::commandFinished);
        QObject::connect(&util, &Util::write, &util, [&util](QString wire) {
            const QString token = wire.mid(wire.indexOf("rem ") + 4).trimmed();
            QTimer::singleShot(0, &util, [&util]() {
                util.processOutput("[+] collecting nonces...\n");
            });
            // Long CPU phase: no output for well over any short-command idle
            // timeout, then the completion marker.
            QTimer::singleShot(450, &util, [&util, token]() {
                util.processOutput("[+] remark: " + token + "\n");
            });
        });

        Util::AsyncCommand command;
        command.cmd = "hf mf autopwn --1k --ns";
        command.useCompletionMarker = true;
        command.hardTimeoutMs = 5000;
        QVERIFY(util.startCommand(command));
        QCOMPARE(util.execCMDWithResult("hf mf rdbl --blk 1", Util::ReturnTrigger(100)).status,
                 Util::EXEC_BUSY);

        QElapsedTimer timer;
        timer.start();
        while(finished.isEmpty() && timer.elapsed() < 4000)
            QTest::qWait(20);
        QCOMPARE(finished.count(), 1);
        const QList<QVariant> args = finished.takeFirst();
        QCOMPARE(args.at(0).value<Util::ExecResult>().status, Util::EXEC_OK);
        QVERIFY(timer.elapsed() >= 400);
        QVERIFY(!util.isCommandActive());
    }

    // Cancel reports the result to the caller but keeps the client channel
    // reserved: the client may still be executing the command, so no page may
    // send another one (the old test expected the opposite and was wrong).
    void cancelKeepsChannelOwned()
    {
        Util util;
        util.setRunningState(true);
        QSignalSpy finished(&util, &Util::commandFinished);
        QSignalSpy writeSpy(&util, &Util::write);
        QSignalSpy ownedSpy(&util, &Util::channelStateChanged);
        QString token;
        QObject::connect(&util, &Util::write, &util, [&util, &token](QString wire) {
            const int remPos = wire.indexOf("rem ");
            if(remPos >= 0)
                token = wire.mid(remPos + 4).trimmed();
            QTimer::singleShot(20, &util, [&util]() {
                util.processOutput("[+] table row 000 | 003 | FFFFFFFFFFFF | 1\n");
            });
        });
        Util::AsyncCommand command;
        command.cmd = "hf mf chk --1k";
        command.useCompletionMarker = true;
        command.hardTimeoutMs = 5000;
        QVERIFY(util.startCommand(command));
        QVERIFY(!token.isEmpty());
        QTest::qWait(100);
        util.cancelCommand(false);
        QCOMPARE(finished.count(), 1);
        const Util::ExecResult result = finished.first().at(0).value<Util::ExecResult>();
        QCOMPARE(result.status, Util::EXEC_CANCELLED);
        QVERIFY(result.output.contains("table row"));
        QVERIFY(ownedSpy.count() >= 1);

        // Still reserved: neither the workbench nor Raw may send anything.
        QVERIFY(util.isChannelOwned());
        QVERIFY(!util.startCommand(command));
        QCOMPARE(util.execCMDWithResult("hf mf rdbl --blk 1", Util::ReturnTrigger(100)).status,
                 Util::EXEC_BUSY);
        util.execCMD("hf mf rdbl --blk 1");
        QCOMPARE(writeSpy.count(), 1); // only the cancelled command reached the client

        // The late completion boundary releases the channel.
        util.processOutput("[+] 2026-11-10T11:53:37Z remark: " + token + "\n");
        QTRY_VERIFY(!util.isChannelOwned());
        QCOMPARE(writeSpy.count(), 1);
        QVERIFY(util.startCommand(command));
        util.cancelCommand(true);
    }

    // Commands that poll stdin run in their own one-shot client: nothing may be
    // queued behind them, and the process exit is the completion boundary.
    void isolatedCommandUsesOneShotClient()
    {
        Util util;
        util.setRunningState(true);
        QSignalSpy finished(&util, &Util::commandFinished);
        QSignalSpy writeSpy(&util, &Util::write);
        QSignalSpy isolatedSpy(&util, &Util::isolatedRunRequested);
        QSignalSpy abortSpy(&util, &Util::isolatedAbortRequested);
        Util::AsyncCommand command;
        command.cmd = "hf mf chk --1k";
        command.isolated = true;
        command.hardTimeoutMs = 5000;
        QVERIFY(util.startCommand(command));
        QCOMPARE(isolatedSpy.count(), 1);
        QCOMPARE(isolatedSpy.first().at(0).toString(), QString("hf mf chk --1k"));
        QCOMPARE(writeSpy.count(), 0); // no "rem" line is ever queued
        QVERIFY(util.isChannelOwned());
        util.processOutput("[+] table row 000 | 003 | FFFFFFFFFFFF | 1\n");
        util.isolatedFinished(Util::EXEC_OK);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).value<Util::ExecResult>().status, Util::EXEC_OK);
        QVERIFY(finished.first().at(0).value<Util::ExecResult>().output.contains("table row"));
        QVERIFY(!util.isChannelOwned());

        // Stopping a one-shot task kills the process; its exit releases the
        // channel and the caller sees EXEC_CANCELLED.
        QVERIFY(util.startCommand(command));
        util.cancelCommand(false);
        QCOMPARE(abortSpy.count(), 1);
        QVERIFY(util.isChannelOwned());
        util.isolatedFinished(Util::EXEC_OK);
        QCOMPARE(finished.last().at(0).value<Util::ExecResult>().status, Util::EXEC_CANCELLED);
        QVERIFY(!util.isChannelOwned());
        QCOMPARE(writeSpy.count(), 0);
    }

    // Trailer access conditions come from the same table the firmware emulator
    // uses; a masked or unreadable field is never treated as data.
    void mfcTrailerAclRules()
    {
        // Transport configuration.  The firmware emulator (IsTrailerAccessAllowed)
        // and the client dump code both treat condition 0..2 as "Key B is
        // readable as data with Key A".
        QVERIFY(Mfc::TrailerAcl::valid("FF078069"));
        QCOMPARE(Mfc::TrailerAcl::condition("FF078069"), 1);
        QVERIFY(Mfc::TrailerAcl::keyBReadable("FF078069"));
        QVERIFY(Mfc::TrailerAcl::keyAWriteAllowed("FF078069", 'A'));
        QVERIFY(!Mfc::TrailerAcl::keyAWriteAllowed("FF078069", 'B'));
        QVERIFY(Mfc::TrailerAcl::aclWriteAllowed("FF078069", 'A'));
        QVERIFY(Mfc::TrailerAcl::aclReadable("FF078069", 'A'));

        QVERIFY(Mfc::TrailerAcl::valid("FF0F0000"));
        QCOMPARE(Mfc::TrailerAcl::condition("FF0F0000"), 0);
        QVERIFY(Mfc::TrailerAcl::keyBReadable("FF0F0000"));

        QVERIFY(Mfc::TrailerAcl::valid("7F0F0800"));
        QCOMPARE(Mfc::TrailerAcl::condition("7F0F0800"), 2);
        QVERIFY(Mfc::TrailerAcl::keyBReadable("7F0F0800"));

        // Condition 3: Key A is writable with Key B only, Key B is not readable.
        QVERIFY(Mfc::TrailerAcl::valid("7F078800"));
        QCOMPARE(Mfc::TrailerAcl::condition("7F078800"), 3);
        QVERIFY(!Mfc::TrailerAcl::keyBReadable("7F078800"));
        QVERIFY(Mfc::TrailerAcl::keyAWriteAllowed("7F078800", 'B'));
        QVERIFY(!Mfc::TrailerAcl::keyAWriteAllowed("7F078800", 'A'));

        QVERIFY(!Mfc::TrailerAcl::valid("FF078169")); // broken complement
        QCOMPARE(Mfc::TrailerAcl::condition("FF078169"), -1);
        QVERIFY(!Mfc::TrailerAcl::keyBReadable("????????????????"));
        QVERIFY(!Mfc::TrailerAcl::keyAWriteAllowed("????????????????", 'A'));
    }

    // A trailer read masks Key A and, for access conditions 3..5, Key B.  The
    // user already has the keys, so a masked trailer must be completed from the
    // verified key store instead of leaving 12 '?' in every sector.
    void trailerReadbackUsesVerifiedKeyB()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        QVERIFY(workbench != nullptr);
        workbench->setDeviceReady(true);
        const quint64 session = workbench->activeSession();
        Mfc::Document& doc = workbench->document();
        QVERIFY(doc.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, session));
        QVERIFY(doc.setKeyVerified(0, 'B', "1234567890AB", Mfc::KeySource::Manual, session));

        // Condition 3 (7F078800): Key B is not readable as data, so the card
        // returns a masked value for the Key A and Key B fields.
        const QString card = QStringLiteral("000000000000") + "7F078800" + QStringLiteral("000000000000");
        QCOMPARE(workbench->trailerReadback(3, 'A', card),
                 QStringLiteral("FFFFFFFFFFFF") + "7F078800" + QStringLiteral("1234567890AB"));
        QVERIFY(doc.setBlock(3, workbench->trailerReadback(3, 'A', card),
                             Mfc::BlockState::Read, session));
        QVERIFY(doc.blockComplete(3));

        // Without a verified Key B the masked bytes stay unknown (a real gap).
        QVERIFY(doc.setKey(0, 'B', "1234567890AB", Mfc::KeyState::Pending, Mfc::KeySource::Manual));
        QCOMPARE(workbench->trailerReadback(3, 'A', card),
                 QStringLiteral("FFFFFFFFFFFF") + "7F078800" + QStringLiteral("????????????"));

        QDir::setCurrent(oldCwd);
    }

    // After a reconnect every recovered key is historical.  "Verify keys" must
    // re-authenticate those values, otherwise the data page has no key it may
    // use and the user is stuck with "No candidate key to verify".
    void verifyKeysRevalidatesHistoricalKeys()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        workbench->setDeviceReady(true);
        const quint64 oldSession = workbench->activeSession();
        Mfc::Document& doc = workbench->document();
        QVERIFY(doc.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, oldSession));
        QVERIFY(doc.setKeyVerified(0, 'B', "1234567890AB", Mfc::KeySource::Manual, oldSession));

        // A reconnect starts a new RF session; the old verification is history.
        workbench->setDeviceReady(true);
        const quint64 newSession = workbench->activeSession();
        QVERIFY(newSession != oldSession);
        QVERIFY(!doc.keyVerifiedIn(0, 'A', newSession));
        QVERIFY(doc.setKeyVerified(9, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, newSession));
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));

        util->setRunningState(true);
        QSignalSpy writeSpy(util, &Util::write);
        armFixture(*util, QByteArray("FF FF FF FF FF FF 7F 07 88 00 12 34 56 78 90 AB\n"));
        workbench->verifyKeys();

        // Only the two historical keys are re-authenticated; the key already
        // verified in this session is not sent again.
        QCOMPARE(writeSpy.count(), 2);
        QVERIFY(doc.keyVerifiedIn(0, 'A', newSession));
        QVERIFY(doc.keyVerifiedIn(0, 'B', newSession));

        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // "Verify known keys" must not require a separate Identify first: the card
    // in the field is identified, then its keys are authenticated.
    void verifyKeysIdentifiesCardFirst()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        workbench->setDeviceReady(true);
        Mfc::Document& doc = workbench->document();
        QVERIFY(doc.setKey(0, 'A', "FFFFFFFFFFFF", Mfc::KeyState::Pending, Mfc::KeySource::Manual));
        QVERIFY(!workbench->documentMatchesTarget());

        util->setRunningState(true);
        QSignalSpy writeSpy(util, &Util::write);
        // One fixture answers both the identify and the trailer read.
        armFixture(*util, QByteArray("UID: A1 A2 A3 A4\nMIFARE Classic 1K\n"
                                     "FF FF FF FF FF FF 7F 07 88 00 12 34 56 78 90 AB\n"));
        workbench->verifyKeys();

        QVERIFY(workbench->documentMatchesTarget());
        QCOMPARE(doc.uid, QString("A1A2A3A4"));
        QVERIFY(doc.keyVerifiedIn(0, 'A', workbench->activeSession()));
        QVERIFY(writeSpy.count() >= 2);

        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // Switching to the recovery page runs the keyless nonce diagnostic once, so
    // the applicability table starts from evidence instead of "unknown".
    void recoveryTabDiagnosesNonce()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        workbench->setDeviceReady(true);
        QTabWidget* tabs = workbench->findChild<QTabWidget*>("mfcTabs");
        QVERIFY(tabs != nullptr);
        QCOMPARE(workbench->nonce, MfcRecovery::NonceType::Unknown);

        util->setRunningState(true);
        armFixture(*util, QByteArray("Prng........ weak\n"
                                     "[+] 2026-11-10T11:53:37Z remark: %TOKEN%\n"));
        tabs->setCurrentIndex(tabs->count() - 1); // Key recovery / decryption
        QElapsedTimer timer;
        timer.start();
        while(workbench->nonce == MfcRecovery::NonceType::Unknown && timer.elapsed() < 5000)
            QTest::qWait(20);
        QCOMPARE(workbench->nonce, MfcRecovery::NonceType::Normal);

        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // Verification evidence belongs to the RF session that produced it.
    void mfcSessionScopedEvidence()
    {
        Mfc::Document doc(Mfc::CardType::M1K);
        QVERIFY(doc.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, 7));
        QVERIFY(doc.keyVerifiedIn(0, 'A', 7));
        QVERIFY(!doc.keyVerifiedIn(0, 'A', 8));
        QCOMPARE(doc.verifiedKeyCountIn(7), 1);
        QCOMPARE(doc.verifiedKeyCountIn(8), 0);
        QCOMPARE(doc.verifiedKeyCount(), 1); // history is kept

        QVERIFY(doc.setBlock(1, "000102030405060708090A0B0C0D0E0F", Mfc::BlockState::Read, 7));
        QVERIFY(doc.blockReadIn(1, 7));
        QVERIFY(!doc.blockReadIn(1, 8));

        // Editing a key drops its session evidence but keeps the value.
        QVERIFY(doc.setKey(0, 'A', "FFFFFFFFFFFF", Mfc::KeyState::Pending, Mfc::KeySource::Manual));
        QCOMPARE(doc.key(0, 'A').verifiedSession, quint64(0));
        QCOMPARE(doc.verifiedKeyCountIn(7), 0);
    }

    // The write plan is the single gate for defaults, preview, manual ticks,
    // retry and the commands that are actually sent.
    void mfcWritePlanProtection()
    {
        Mfc::Document doc(Mfc::CardType::M1K);
        const QString data = "000102030405060708090A0B0C0D0E0F";
        const QString trailer = QStringLiteral("FFFFFFFFFFFF") + "FF078069" + QStringLiteral("FFFFFFFFFFFF");
        QVERIFY(doc.setBlock(0, "A1A2A3A4B5B6B7B8B9BABBBCBDBEBF00", Mfc::BlockState::Read, 5));
        QVERIFY(doc.setBlock(1, data, Mfc::BlockState::Read, 5));
        QVERIFY(doc.setBlock(2, data, Mfc::BlockState::Read, 5));
        QVERIFY(doc.setBlock(3, trailer, Mfc::BlockState::Read, 5));
        QVERIFY(doc.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, 5));

        Mfc::WritePlan plan;
        plan.reset(doc, 5);
        QCOMPARE(plan.rows().size(), 4);
        QVERIFY(plan.matches(doc, 5));
        QVERIFY(!plan.matches(doc, 6));

        QString reason;
        QVERIFY(!plan.select(0, true, &reason));
        QVERIFY(reason.contains("Block 0"));
        QVERIFY(!plan.select(3, true, &reason));
        QVERIFY(reason.contains("trailer", Qt::CaseInsensitive));
        QVERIFY(plan.select(1, true, &reason));
        QCOMPARE(plan.selectedBlocks(), QList<int>({1}));

        // Allowing block 0 is not the same as selecting it; the row still has
        // to pass the policy check when it is ticked.
        plan.setIncludeBlock0(true);
        QVERIFY(plan.blockAllowed(0));
        QCOMPARE(plan.selectedBlocks(), QList<int>({1}));

        // A trailer needs a preview: the access conditions on the card decide
        // whether the write is allowed at all.
        plan.setIncludeTrailers(true);
        QVERIFY(!plan.blockAllowed(3, &reason));
        QVERIFY(reason.contains("preview", Qt::CaseInsensitive));
        const QString cardTrailer = QStringLiteral("000000000000") + "FF078069" + QStringLiteral("000000000000");
        plan.setPreview(3, cardTrailer, Mfc::PreviewState::Different, "differs");
        QVERIFY(plan.blockAllowed(3, &reason));
        QVERIFY(plan.isSelected(3));
        const Mfc::TrailerPlan trailerPlan = plan.trailerPlan(3);
        QVERIFY(trailerPlan.valid);
        QCOMPARE(trailerPlan.writeKeyType, 'A');
        QVERIFY(trailerPlan.verify.checkAcl);
        QVERIFY(trailerPlan.verify.checkKeyB); // Key B is readable under FF078069

        // An unreadable preview is never "safe to write".
        plan.setPreview(3, QString(), Mfc::PreviewState::Unreadable, "timeout");
        QVERIFY(!plan.isSelected(3));
        QVERIFY(!plan.blockAllowed(3));

        // Retry only re-selects writes that never reported success.
        plan.setOutcome(1, Mfc::WriteOutcome::WriteFailed, "x");
        plan.setOutcome(2, Mfc::WriteOutcome::WrittenUnverified, "x");
        QCOMPARE(plan.retryBlocks(), QList<int>({1}));
        QVERIFY(plan.anyWrittenUnverified());
        QVERIFY(Mfc::retryable(Mfc::WriteOutcome::WriteFailed));
        QVERIFY(!Mfc::retryable(Mfc::WriteOutcome::WrittenUnverified));
        QVERIFY(!Mfc::retryable(Mfc::WriteOutcome::Mismatch));

        // No key verified in this session means no write at all.
        Mfc::Document historical = doc;
        QVERIFY(historical.setKey(0, 'A', "FFFFFFFFFFFF", Mfc::KeyState::Pending, Mfc::KeySource::Manual));
        Mfc::WritePlan second;
        second.reset(historical, 5);
        QVERIFY(!second.blockAllowed(1, &reason));
        QVERIFY(reason.contains("no key verified"));
        QVERIFY(!second.select(1, true, &reason));
        QVERIFY(second.selectedBlocks().isEmpty());
    }

    void workbenchRunsDictionaryTask()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        // The one-shot runner is a real process: point it at the mock client.
        window.lastClientPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!window.lastClientPath.isEmpty());
        window.clientWorkingDir->setPath(dir.path());

        util->setRunningState(true);
        QSignalSpy writeSpy(util, &Util::write);
        QSignalSpy isolatedSpy(util, &Util::isolatedRunRequested);
        workbench->setDeviceReady(true);
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        const quint64 session = workbench->activeSession();

        QComboBox* methodBox = workbench->findChild<QComboBox*>("mfcMethodBox");
        QVERIFY(methodBox != nullptr);
        methodBox->setCurrentIndex(methodBox->findText("Dictionary (chk)"));
        QVERIFY(QMetaObject::invokeMethod(workbench, "runSelectedMethod"));

        // chk polls stdin in the client: it runs in a one-shot process and no
        // "rem" line is queued behind it.
        QCOMPARE(isolatedSpy.count(), 1);
        QVERIFY(writeSpy.isEmpty());
        QVERIFY(workbench->isTaskActive());
        // The GUI stops the persistent client to free the port; that stop reaches
        // Util while the one-shot client is already running.
        util->setRunningState(false);

        QElapsedTimer timer;
        timer.start();
        while(workbench->isTaskActive() && timer.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(!workbench->isTaskActive());
        QCOMPARE(workbench->document().key(0, 'A').state, Mfc::KeyState::Verified);
        QCOMPARE(workbench->document().key(0, 'B').state, Mfc::KeyState::Verified);
        QCOMPARE(workbench->document().key(1, 'B').state, Mfc::KeyState::Unknown);
        QCOMPARE(workbench->document().key(0, 'A').verifiedSession, session);
        QVERIFY(!util->isChannelOwned());
        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // A recovery method that polls stdin runs in a one-shot client, which needs
    // the serial port: the GUI stops the persistent client first.  It must
    // restore that connection when the one-shot run ends, otherwise every
    // recovery leaves the user on "Not Connected" and asking for Connect again.
    void oneShotRunReconnectsClient()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        Util* util = window.findChild<Util*>();
        QVERIFY(util != nullptr);
        window.lastClientPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!window.lastClientPath.isEmpty());
        window.lastClientArgs = QStringList();
        window.clientWorkingDir->setPath(dir.path());

        // The GUI is connected: the one-shot run has to stop that client.
        util->setRunningState(true);
        window.pm3state = true;
        QSignalSpy stateSpy(window.pm3, SIGNAL(PM3StatedChanged(bool,QString,quint64)));

        window.runIsolatedCommand(QStringLiteral("hf mf chk --1k"), QStringLiteral("Dictionary"));
        QVERIFY(!window.pm3state);

        QElapsedTimer timer;
        timer.start();
        while(!window.pm3state && timer.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY2(window.pm3state, "the persistent client was not reconnected");
        QVERIFY(!stateSpy.isEmpty());
        QCOMPARE(stateSpy.last().at(0).toBool(), true);

        window.on_PM3_disconnectButton_clicked();
        QDir::setCurrent(oldCwd);
    }

    // The internal stop for a one-shot recovery must not invalidate the RF
    // session: after the reconnect the same UID keeps the recovered keys valid.
    void oneShotSessionSurvivesReconnect()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        util->setRunningState(true);
        workbench->setDeviceReady(true);
        const quint64 session = workbench->activeSession();
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        QVERIFY(workbench->document().setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, session));

        workbench->suspendSessionForOneShot();
        workbench->setDeviceReady(false); // internal stop for the one-shot client
        QCOMPARE(workbench->activeSession(), session);
        QVERIFY(workbench->documentMatchesTarget());

        armFixture(*util, QByteArray("UID: A1 A2 A3 A4\nMIFARE Classic 1K\n"));
        workbench->setDeviceReady(true); // reconnect
        QElapsedTimer timer;
        timer.start();
        while(workbench->cardConfirmPending && timer.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(!workbench->cardConfirmPending);
        QCOMPARE(workbench->activeSession(), session);
        QVERIFY(workbench->documentMatchesTarget());
        QVERIFY(workbench->document().key(0, 'A').verifiedIn(session));

        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // A different UID after the reconnect must drop the old evidence instead of
    // reading the new card with keys from the previous one.
    void oneShotSessionDropsChangedCard()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);

        util->setRunningState(true);
        workbench->setDeviceReady(true);
        const quint64 session = workbench->activeSession();
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        QVERIFY(workbench->document().setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, session));

        workbench->suspendSessionForOneShot();
        workbench->setDeviceReady(false);
        armFixture(*util, QByteArray("UID: B1 B2 B3 B4\nMIFARE Classic 1K\n"));
        workbench->setDeviceReady(true);
        QElapsedTimer timer;
        timer.start();
        while(workbench->cardConfirmPending && timer.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(!workbench->cardConfirmPending);
        QVERIFY(workbench->activeSession() != session);
        QVERIFY(!workbench->documentMatchesTarget());
        QCOMPARE(workbench->targetUid(), QString("B1B2B3B4"));

        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // hardnested prints one recovered key for --tblk/--ta; the workbench has to
    // attribute it to the target sector instead of reporting "no usable key".
    void workbenchRunsHardnestedTask()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);
        window.lastClientPath = makeMockClient(dir.path(), "v4.23346-suspect");
        QVERIFY(!window.lastClientPath.isEmpty());
        window.clientWorkingDir->setPath(dir.path());

        util->setRunningState(true);
        workbench->setDeviceReady(true);
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        const quint64 session = workbench->activeSession();
        workbench->document().setKeyVerified(10, 'A', "F67000B1A95A", Mfc::KeySource::Manual);

        auto selectType = [](QComboBox* box, char type) {
            for(int i = 0; i < box->count(); ++i)
                if(box->itemData(i).toChar().toLatin1() == type)
                {
                    box->setCurrentIndex(i);
                    return;
                }
        };

        QComboBox* methodBox = workbench->findChild<QComboBox*>("mfcMethodBox");
        QSpinBox* knownSector = workbench->findChild<QSpinBox*>("mfcKnownSectorSpin");
        QComboBox* knownType = workbench->findChild<QComboBox*>("mfcKnownTypeBox");
        QSpinBox* targetSector = workbench->findChild<QSpinBox*>("mfcTargetSectorSpin");
        QComboBox* targetType = workbench->findChild<QComboBox*>("mfcTargetTypeBox");
        QVERIFY(methodBox && knownSector && knownType && targetSector && targetType);
        methodBox->setCurrentIndex(methodBox->findText("Hardnested"));
        knownSector->setValue(10);
        selectType(knownType, 'A');
        targetSector->setValue(5); // block 23
        selectType(targetType, 'A');
        QVERIFY(QMetaObject::invokeMethod(workbench, "runSelectedMethod"));
        QVERIFY(workbench->isTaskActive());

        QElapsedTimer timer;
        timer.start();
        while(workbench->isTaskActive() && timer.elapsed() < 10000)
            QTest::qWait(20);
        QVERIFY(!workbench->isTaskActive());
        QCOMPARE(workbench->document().key(5, 'A').value, QString("B1B17CC93BC4"));
        QCOMPARE(workbench->document().key(5, 'A').state, Mfc::KeyState::Verified);
        QCOMPARE(workbench->document().key(5, 'A').verifiedSession, session);
        QCOMPARE(workbench->document().key(5, 'B').state, Mfc::KeyState::Unknown);
        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    // A card change keeps the old document but never lets its blocks or keys
    // pass as evidence for the card in the field.
    void workbenchRequiresCardIdentity()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        QVERIFY(workbench != nullptr);
        workbench->setDeviceReady(true);
        QVERIFY(workbench->activeSession() != 0);
        QVERIFY(!workbench->documentMatchesTarget());

        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        QCOMPARE(workbench->document().uid, QString("A1A2A3A4"));
        QVERIFY(workbench->documentMatchesTarget());

        Mfc::Document& document = workbench->document();
        document.setBlock(1, "000102030405060708090A0B0C0D0E0F", Mfc::BlockState::Read,
                          workbench->activeSession());
        document.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual,
                                workbench->activeSession());

        // Same capacity, different UID: the document is not adopted.
        QVERIFY(!workbench->applyCardIdentity("B1B2B3B4", Mfc::CardType::M1K, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(workbench->document().uid, QString("A1A2A3A4"));
        QVERIFY(!workbench->documentMatchesTarget());
        QCOMPARE(workbench->document().completeBlockCount(), 1);

        // A new document binds to the card in the field and starts empty.
        QVERIFY2(workbench->startNewDocumentForTarget(&error), qPrintable(error));
        QCOMPARE(workbench->document().uid, QString("B1B2B3B4"));
        QCOMPARE(workbench->document().completeBlockCount(), 0);
        QCOMPARE(workbench->document().verifiedKeyCount(), 0);
        QVERIFY(workbench->documentMatchesTarget());

        // Reconnect: the same card is identified again, but the verification
        // from the previous session is history, not evidence.
        workbench->document().setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual,
                                             workbench->activeSession());
        const quint64 firstSession = workbench->activeSession();
        workbench->setDeviceReady(false);
        QVERIFY(!workbench->hasTarget());
        workbench->setDeviceReady(true);
        QVERIFY(workbench->activeSession() != firstSession);
        QVERIFY(!workbench->documentMatchesTarget());
        QVERIFY2(workbench->applyCardIdentity("B1B2B3B4", Mfc::CardType::M1K, &error), qPrintable(error));
        QVERIFY(workbench->documentMatchesTarget());
        QCOMPARE(workbench->document().verifiedKeyCount(), 1);
        QCOMPARE(workbench->document().verifiedKeyCountIn(workbench->activeSession()), 0);
        QDir::setCurrent(oldCwd);
    }

    // The backup sidecar has to describe the bytes on disk, not the internal
    // document digest.
    void workbenchBackupSidecarHashesFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        QVERIFY(workbench != nullptr);
        workbench->document().setBlock(1, "000102030405060708090A0B0C0D0E0F",
                                       Mfc::BlockState::Read, 0);

        for(const QString& name : {QStringLiteral("backup.json"), QStringLiteral("backup.eml"),
                                   QStringLiteral("backup.bin")})
        {
            const QString path = dir.filePath(name);
            QString error;
            QVERIFY2(workbench->saveBackupTo(path, &error, true), qPrintable(error));

            QFile data(path);
            QVERIFY(data.open(QFile::ReadOnly));
            const QByteArray bytes = data.readAll();
            data.close();
            QVERIFY(!bytes.isEmpty());

            QFile sidecar(path + ".sha256");
            QVERIFY(sidecar.open(QFile::ReadOnly | QFile::Text));
            const QString text = QString::fromUtf8(sidecar.readAll()).trimmed();
            sidecar.close();
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            QCOMPARE(parts.size(), 2);
            QCOMPARE(parts.at(0),
                     QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()));
            QCOMPARE(parts.at(1), QFileInfo(path).fileName());
        }
        // The internal digest still exists, but it is a different value.
        QCOMPARE(workbench->document().sha256().size(), 64);
        QDir::setCurrent(oldCwd);
    }

    // Unchecking the include options is not a hint: the preview, a manual tick
    // and the write loop all go through the write plan, so no protected block
    // ever reaches the client.
    void workbenchWritePlanProtectsBlocks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        Util* util = window.findChild<Util*>();
        QVERIFY(workbench != nullptr);
        QVERIFY(util != nullptr);
        workbench->setConfirmWrites(false);

        util->setRunningState(true);
        workbench->setDeviceReady(true);
        QString error;
        QVERIFY2(workbench->applyCardIdentity("A1A2A3A4", Mfc::CardType::M1K, &error), qPrintable(error));
        Mfc::Document& doc = workbench->document();
        const quint64 session = workbench->activeSession();
        const QString data = "000102030405060708090A0B0C0D0E0F";
        doc.setBlock(0, "A1A2A3A4B5B6B7B8B9BABBBCBDBEBF00", Mfc::BlockState::Read, session);
        doc.setBlock(1, data, Mfc::BlockState::Read, session);
        doc.setBlock(2, data, Mfc::BlockState::Read, session);
        doc.setBlock(3, QStringLiteral("FFFFFFFFFFFF") + "FF078069" + QStringLiteral("FFFFFFFFFFFF"),
                     Mfc::BlockState::Read, session);
        doc.setKeyVerified(0, 'A', "FFFFFFFFFFFF", Mfc::KeySource::Manual, session);

        QStringList commands;
        QString cardReadHex = "aabbccddeeff00112233445566778899";
        QObject::connect(util, &Util::write, util, [util, &commands, &cardReadHex](QString wire) {
            commands << wire;
            const int remPos = wire.indexOf("rem ");
            const QString token = remPos >= 0 ? wire.mid(remPos + 4).trimmed() : QString();
            QTimer::singleShot(5, util, [util, token, wire, cardReadHex]() {
                if(wire.contains("rdbl"))
                {
                    QString spaced;
                    for(int i = 0; i < cardReadHex.size(); i += 2)
                        spaced += (spaced.isEmpty() ? QString() : QStringLiteral(" "))
                                  + cardReadHex.mid(i, 2);
                    util->processOutput(spaced + "\n");
                }
                else if(wire.contains("wrbl"))
                    util->processOutput("[+] Write ( ok )\n");
                if(!token.isEmpty())
                    util->processOutput("[+] remark: " + token + "\n");
            });
        });

        bool manualTickStuck = false;
        bool trailerTickStuck = false;
        bool timedOut = false;

        // Drive the modal dialog: preview (optional), manual ticks, write
        // (optional), close.  No message box may appear: the write confirmation
        // is disabled and every fixture answer keeps the outcome verified.
        auto runPlanDialog = [&](bool withPreview, bool withWrite) -> bool {
            QTableWidget* table = nullptr;
            QPushButton* preview = nullptr;
            QPushButton* write = nullptr;
            QPushButton* close = nullptr;
            int step = 0;
            int ticks = 0;
            QTimer driver;
            driver.setInterval(20);
            QObject::connect(&driver, &QTimer::timeout, [&]() {
                if(++ticks > 1500)
                {
                    timedOut = true;
                    driver.stop();
                    if(QDialog* dlg = window.findChild<QDialog*>("mfcWritePlanDialog"))
                        dlg->reject();
                    return;
                }
                if(table == nullptr)
                {
                    table = window.findChild<QTableWidget*>("mfcWriteTable");
                    preview = window.findChild<QPushButton*>("mfcWritePreview");
                    write = window.findChild<QPushButton*>("mfcWriteWrite");
                    close = window.findChild<QPushButton*>("mfcWriteClose");
                    if(table == nullptr || preview == nullptr || write == nullptr || close == nullptr)
                        return;
                }
                if(!preview->isEnabled())
                    return; // a task is running
                switch(step)
                {
                case 0:
                    if(withPreview)
                    {
                        preview->click();
                        step = 1;
                        return;
                    }
                    step = 2;
                    return;
                case 1:
                    // Both include options are off: a manual tick may not stick.
                    table->item(0, 0)->setCheckState(Qt::Checked);
                    manualTickStuck = manualTickStuck || table->item(0, 0)->checkState() == Qt::Checked;
                    table->item(3, 0)->setCheckState(Qt::Checked);
                    trailerTickStuck = trailerTickStuck || table->item(3, 0)->checkState() == Qt::Checked;
                    step = 2;
                    return;
                case 2:
                    if(withWrite)
                    {
                        if(!write->isEnabled())
                            return;
                        write->click();
                        step = 3;
                        return;
                    }
                    step = 3;
                    return;
                case 3:
                    close->click();
                    driver.stop();
                    return;
                default:
                    return;
                }
            });
            driver.start();
            const bool invoked = QMetaObject::invokeMethod(workbench, "writePlan");
            driver.stop();
            return invoked && !timedOut;
        };

        // 1) Preview with a card that differs: only ordinary blocks may be
        //    selected, and a manual tick on Block 0 / the trailer is refused.
        commands.clear();
        QVERIFY(runPlanDialog(true, false));
        QVERIFY2(!manualTickStuck, "a manual tick must not select protected Block 0");
        QVERIFY2(!trailerTickStuck, "a manual tick must not select a protected trailer");
        QVERIFY(!workbench->currentWritePlan().isSelected(0));
        QVERIFY(!workbench->currentWritePlan().isSelected(3));
        for(const QString& wire : commands)
            QVERIFY2(!wire.contains("wrbl"), qPrintable(wire)); // preview only

        // 2) Write: the read-back matches, so blocks 1 and 2 are the only
        //    commands the client sees.
        cardReadHex = data;
        commands.clear();
        QVERIFY(runPlanDialog(false, true));
        QVERIFY(!commands.isEmpty());
        int writes = 0;
        for(const QString& wire : commands)
        {
            if(!wire.contains("wrbl"))
                continue;
            writes++;
            QVERIFY2(!wire.contains("--blk 0 "), qPrintable(wire));
            QVERIFY2(!wire.contains("--blk 3 "), qPrintable(wire));
        }
        QCOMPARE(writes, 2);
        QCOMPARE(doc.blockState(1), Mfc::BlockState::Verified);
        QCOMPARE(doc.blockState(2), Mfc::BlockState::Verified);
        QVERIFY(!util->isChannelOwned());
        util->setRunningState(false);
        QDir::setCurrent(oldCwd);
    }

    void workbenchPageAndCompare()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(dir.path());
        QSettings settings("GUIsettings.ini", QSettings::IniFormat);
        settings.setValue("language/name", "en_US");
        settings.sync();

        MainWindow window;
        window.initUI();
        MfcWorkbench* workbench = window.findChild<MfcWorkbench*>("mfcWorkbench");
        QVERIFY2(workbench != nullptr, "workbench page must be part of the main window");
        bool docked = false;
        for(QDockWidget* dock : window.findChildren<QDockWidget*>())
            if(dock->widget() == workbench)
                docked = true;
        QVERIFY2(docked, "workbench must be moved into a dock like the other pages");
        QCOMPARE(workbench->document().cardType(), Mfc::CardType::M1K);
        QVERIFY(!workbench->isTaskActive());

        auto writeEml = [&dir](const QString& name, int differentBlock) {
            const QString path = dir.filePath(name);
            QFile file(path);
            if(!file.open(QFile::WriteOnly | QFile::Text))
                return QString();
            for(int i = 0; i < 64; i++)
            {
                const QString line = (i == differentBlock)
                                     ? QStringLiteral("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF")
                                     : QStringLiteral("%1").arg(i, 32, 16, QLatin1Char('0')).toUpper();
                file.write(line.toLatin1());
                file.write("\n");
            }
            file.close();
            return path;
        };
        const QString a = writeEml("a.eml", -1);
        const QString b = writeEml("b.eml", 1);
        QVERIFY(!a.isEmpty());
        QVERIFY(!b.isEmpty());
        QString error;
        QVERIFY2(workbench->loadCompareFile(true, a, &error), qPrintable(error));
        QVERIFY2(workbench->loadCompareFile(false, b, &error), qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(workbench, "compareDocuments"));
        const Mfc::CompareResult comparison = workbench->currentComparison();
        QCOMPARE(comparison.same, 63);
        QCOMPARE(comparison.different, 1);
        QCOMPARE(comparison.unknown, 0);

        // Ambiguous binary key layout must be reported, not guessed.
        const QString keyPath = dir.filePath("key.bin");
        QFile keyFile(keyPath);
        QVERIFY(keyFile.open(QFile::WriteOnly));
        QByteArray keys;
        for(int i = 0; i < 16 * 2; i++)
            keys += QByteArray(6, char(i + 1));
        keyFile.write(keys);
        keyFile.close();
        QVERIFY(!workbench->importKeyFile(keyPath, "auto", &error));
        QVERIFY(error.contains("ambiguous"));
        QVERIFY2(workbench->importKeyFile(keyPath, "rrg", &error), qPrintable(error));
        QCOMPARE(workbench->document().key(0, 'A').value, QString("010101010101"));
        QCOMPARE(workbench->document().key(0, 'B').value, QString("111111111111"));

        QDir::setCurrent(oldCwd);
    }
};

// Full QApplication shutdown path: aboutToQuit fires while the window is still
// alive, then exec() returns and the stack MainWindow destructor runs. Run this
// under an external timeout; a hang means the thread/process cleanup is wrong.
static int runQuitSmoke(int argc, char** argv)
{
    setvbuf(stderr, nullptr, _IONBF, 0);
    fprintf(stderr, "quit-smoke: start\n");
    if(qEnvironmentVariableIsEmpty("MOCK_OS_VERSION"))
        qputenv("MOCK_OS_VERSION", "v4.23346-suspect");
    QApplication app(argc, argv);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        fprintf(stderr, "quit-smoke: aboutToQuit\n");
    });
    QTemporaryDir dir;
    if(!dir.isValid())
        return 2;
    QDir root(dir.path());
    if(!root.mkpath("app") || !root.mkpath("data"))
        return 2;
    QDir::setCurrent(root.filePath("app"));

    const QByteArray client = QFile::encodeName(QString::fromLocal8Bit(argv[2]));
    fprintf(stderr, "quit-smoke: client=%s exists=%d\n", client.constData(), QFileInfo(QString::fromLocal8Bit(argv[2])).isFile() ? 1 : 0);
    QSettings settings("GUIsettings.ini", QSettings::IniFormat);
    settings.setValue("language/name", "en_US");
    settings.setValue("Client_Env/workingDir", root.filePath("data"));
    settings.setValue("Client_Env/configFile", "(ext)");
    QString configPath = QCoreApplication::applicationDirPath() + "/config/config_rrgv4.23346.json";
    if(!QFile::exists(configPath))
        configPath = QFileInfo(QString(SRCDIR) + "/config/config_rrgv4.23346.json").absoluteFilePath();
    settings.setValue("Client_Env/extConfigFilePath", configPath);
    settings.setValue("Client_Args/args", "-f");
    settings.sync();

    fprintf(stderr, "quit-smoke: creating window\n");
    MainWindow window;
    window.initUI();
    fprintf(stderr, "quit-smoke: window ready\n");
    QComboBox* pathBox = window.findChild<QComboBox*>("PM3_pathBox");
    if(pathBox == nullptr)
        return 3;
    pathBox->setCurrentText(QString::fromLocal8Bit(client));
    int modalCount = 0;
    QTimer modalWatch;
    QObject::connect(&modalWatch, &QTimer::timeout, [&modalCount]() {
        if(QWidget* modal = QApplication::activeModalWidget())
        {
            modalCount++;
            QMessageBox* box = qobject_cast<QMessageBox*>(modal);
            fprintf(stderr, "quit-smoke: unexpected modal: %s\n",
                    box ? box->text().toLocal8Bit().constData() : "");
            modal->close();
        }
    });
    modalWatch.start(50);
    fprintf(stderr, "quit-smoke: connecting\n");
    if(!QMetaObject::invokeMethod(&window, "on_PM3_connectButton_clicked"))
        return 3;
    fprintf(stderr, "quit-smoke: connected call returned\n");
    QTimer::singleShot(250, &app, &QCoreApplication::quit);
    const int rc = app.exec();
    fprintf(stderr, "quit-smoke: exec returned %d, modals=%d\n", rc, modalCount);
    return modalCount == 0 ? rc : 4;
}

int main(int argc, char** argv)
{
    if(argc >= 3 && QString::fromLocal8Bit(argv[1]) == "--quit-smoke")
        return runQuitSmoke(argc, argv);

    QApplication app(argc, argv);
    GuiTests tests;
    return QTest::qExec(&tests, argc, argv);
}


#include "test_gui.moc"

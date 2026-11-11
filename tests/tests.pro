QT += core gui widgets serialport testlib
CONFIG += c++11 console testcase
CONFIG -= app_bundle
TEMPLATE = app
TARGET = gui-tests

INCLUDEPATH += $$PWD/../src
# Override on the qmake command line to point the fixtures at a relocated copy.
isEmpty(SRCDIR_DIR): SRCDIR_DIR = $$PWD/..
DEFINES += SRCDIR=\\\"$$SRCDIR_DIR\\\"

# Mock client used by the process tests. On Windows QProcess cannot run a
# #! shell script, so the tests need a real executable there.
# Keep the test binary and the mock client in the build directory on every platform.
DESTDIR = $$OUT_PWD

win32: MOCK_TARGET = mock-proxmark3.exe
else: MOCK_TARGET = mock-proxmark3
mockclient.target = $$OUT_PWD/$$MOCK_TARGET
mockclient.commands = $$QMAKE_CC $$QMAKE_CFLAGS -o $$OUT_PWD/$$MOCK_TARGET $$PWD/mockclient.c
mockclient.depends = $$PWD/mockclient.c
QMAKE_EXTRA_TARGETS += mockclient
PRE_TARGETDEPS += $$OUT_PWD/$$MOCK_TARGET

SOURCES += \
    test_gui.cpp \
    ../src/common/capabilities.cpp \
    ../src/common/deviceidentity.cpp \
    ../src/common/myeventfilter.cpp \
    ../src/common/pm3cmd.cpp \
    ../src/common/pm3process.cpp \
    ../src/common/util.cpp \
    ../src/ui/mf_sim_simdialog.cpp \
    ../src/module/lf.cpp \
    ../src/module/mifare.cpp \
    ../src/module/demomode.cpp \
    ../src/module/mfcbatch.cpp \
    ../src/module/mfcdocument.cpp \
    ../src/module/mfcdraftstore.cpp \
    ../src/module/mfclibrary.cpp \
    ../src/module/mfudocument.cpp \
    ../src/module/tracemodel.cpp \
    ../src/module/mfckeyrecovery.cpp \
    ../src/module/mfcwriteplan.cpp \
    ../src/module/t55xxtab.cpp \
    ../src/ui/mainwindow.cpp \
    ../src/ui/mfcworkbench.cpp \
    ../src/ui/mfclibrarypage.cpp \
    ../src/ui/mfupage.cpp \
    ../src/ui/tracepage.cpp \
    ../src/ui/diagnosticspage.cpp \
    ../src/ui/commandpalette.cpp \
    ../src/ui/mf_attack_hardnesteddialog.cpp \
    ../src/ui/mf_trailerdecoderdialog.cpp \
    ../src/ui/mf_uid_parameterdialog.cpp

HEADERS += \
    ../src/common/capabilities.h \
    ../src/common/deviceidentity.h \
    ../src/common/myeventfilter.h \
    ../src/common/pm3cmd.h \
    ../src/common/pm3process.h \
    ../src/common/util.h \
    ../src/ui/mf_sim_simdialog.h \
    ../src/module/lf.h \
    ../src/module/mifare.h \
    ../src/module/demomode.h \
    ../src/module/mfcbatch.h \
    ../src/module/mfcdocument.h \
    ../src/module/mfcdraftstore.h \
    ../src/module/mfclibrary.h \
    ../src/module/mfudocument.h \
    ../src/module/tracemodel.h \
    ../src/module/mfckeyrecovery.h \
    ../src/module/mfcwriteplan.h \
    ../src/module/t55xxtab.h \
    ../src/ui/mainwindow.h \
    ../src/ui/mfcworkbench.h \
    ../src/ui/mfclibrarypage.h \
    ../src/ui/mfupage.h \
    ../src/ui/tracepage.h \
    ../src/ui/diagnosticspage.h \
    ../src/ui/commandpalette.h \
    ../src/ui/mf_attack_hardnesteddialog.h \
    ../src/ui/mf_trailerdecoderdialog.h \
    ../src/ui/mf_uid_parameterdialog.h

FORMS += \
    ../src/ui/mainwindow.ui \
    ../src/ui/mf_sim_simdialog.ui \
    ../src/ui/mf_attack_hardnesteddialog.ui \
    ../src/ui/mf_trailerdecoderdialog.ui \
    ../src/ui/mf_uid_parameterdialog.ui \
    ../src/ui/t55xxtab.ui

RESOURCES += \
    ../config/config.qrc \
    ../i18n/language.qrc

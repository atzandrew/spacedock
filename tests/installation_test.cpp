#include "loginsettings.h"
#include "singleinstance.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

class InstallationTest : public QObject {
    Q_OBJECT
private slots:
    void loginStartup() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
        qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
        const auto source = root.path() + "/data/applications/spacedock.desktop";
        const auto startup = root.path() + "/config/autostart/spacedock.desktop";
        QVERIFY(QDir().mkpath(QFileInfo(source).absolutePath()));
        auto write = [](const QString &path, const QByteArray &text) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        const QByteArray entry = "[Desktop Entry]\nType=Application\nName=SpaceDock\nExec=\""
            + QCoreApplication::applicationFilePath().toUtf8() + "\"\n";
        QVERIFY(write(source, entry));
        LoginSettings development(false);
        QVERIFY(!development.available());
        development.setEnabled(true);
        QVERIFY(!QFile::exists(startup));
        LoginSettings installed(true);
        QVERIFY(installed.available());
        QVERIFY(!installed.enabled());
        installed.setEnabled(true);
        QVERIFY2(installed.error().isEmpty(), qPrintable(installed.error()));
        QVERIFY(installed.enabled());
        LoginSettings restarted(true);
        QVERIFY(restarted.enabled());
        QVERIFY(write(startup, entry + "Hidden=true\n"));
        QVERIFY(!restarted.enabled());
        restarted.setEnabled(true);
        QVERIFY(restarted.enabled());
        restarted.setEnabled(false);
        QVERIFY(!QFile::exists(startup));
        QVERIFY(write(startup, "[Desktop Entry]\nExec=/another/spacedock\n"));
        restarted.setEnabled(false);
        QVERIFY(!restarted.error().isEmpty());
        QVERIFY(QFile::exists(startup));
        restarted.setEnabled(true);
        QVERIFY(!restarted.error().isEmpty());
        // A build cannot manage the installed app's startup entry.
        QVERIFY(write(source, "[Desktop Entry]\nExec=/another/spacedock\n"));
        LoginSettings build(true);
        QVERIFY(!build.available());
    }
    void instanceLock() {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--hold-instance"});
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForReadyRead());
        QCOMPARE(child.readAllStandardOutput().trimmed(), QByteArray("ready"));
        {
            SingleInstance duplicate;
            QVERIFY(!duplicate.acquire());
            QVERIFY(!duplicate.failed());
        }
        child.kill();
        QVERIFY(child.waitForFinished());
        // An abnormal termination must not leave a permanent lock behind.
        SingleInstance recovered;
        QVERIFY(recovered.acquire());
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--hold-instance")) {
        SingleInstance instance;
        if (!instance.acquire()) return 1;
        puts("ready"); fflush(stdout);
        return app.exec();
    }
    InstallationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "installation_test.moc"

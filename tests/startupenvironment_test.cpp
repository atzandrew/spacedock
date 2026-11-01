#include "startupenvironment.h"
#include <QProcess>
#include <QTest>

class StartupEnvironmentTest : public QObject {
    Q_OBJECT
private:
    QByteArray childShell() {
        QProcess child;
        child.start("/usr/bin/printenv", {"QT_WAYLAND_SHELL_INTEGRATION"});
        if (!child.waitForFinished()) return "child failed";
        return child.readAllStandardOutput().trimmed();
    }
private slots:
    void launchJobsDoNotInheritDockShell() {
        const auto original = qgetenv("QT_WAYLAND_SHELL_INTEGRATION");
        qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
        {
            StartupEnvironment environment(true);
            QCOMPARE(qgetenv("QT_WAYLAND_SHELL_INTEGRATION"), "xdg-shell");
            environment.restore();
            QVERIFY(!qEnvironmentVariableIsSet("QT_WAYLAND_SHELL_INTEGRATION"));
            QCOMPARE(childShell(), QByteArray());
        }
        if (original.isNull()) qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
        else qputenv("QT_WAYLAND_SHELL_INTEGRATION", original);
    }
    void preservesExplicitDesktopEnvironment() {
        const auto original = qgetenv("QT_WAYLAND_SHELL_INTEGRATION");
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell");
        {
            StartupEnvironment environment(true);
            environment.restore();
            QCOMPARE(childShell(), "xdg-shell");
        }
        if (original.isNull()) qunsetenv("QT_WAYLAND_SHELL_INTEGRATION");
        else qputenv("QT_WAYLAND_SHELL_INTEGRATION", original);
    }
};
QTEST_GUILESS_MAIN(StartupEnvironmentTest)
#include "startupenvironment_test.moc"

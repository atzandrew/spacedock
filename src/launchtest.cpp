#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
    bool smoke = false;
    for (int i = 1; i < argc; ++i) smoke |= QByteArray(argv[i]) == "--smoke-test";
    if (!smoke) {
        qputenv("QT_QPA_PLATFORM", "wayland");
        qputenv("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("spacedock-launch-test");
    app.setOrganizationName("spacedock");
    // Match the .desktop launcher, startup notification, and Wayland app_id.
    app.setDesktopFileName("spacedock-launch-test");
    app.setWindowIcon(QIcon::fromTheme("system-run"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"smoke-test", "Exit after the delayed window renders (for automated tests)."});
    parser.process(app);

    QQmlApplicationEngine engine;
    QElapsedTimer elapsed;
    elapsed.start();
    QTextStream(stdout) << "loading\n" << Qt::flush;
    QTimer delay;
    delay.setSingleShot(true);
    delay.setTimerType(Qt::PreciseTimer);
    delay.setInterval(3000);
    QObject::connect(&delay, &QTimer::timeout, &app, [&] {
        // No window or native surface exists until the simulated loading ends.
        // Qt's normal first-window startup/activation handling is preserved.
        engine.load(QUrl("qrc:/qml/LaunchTestWindow.qml"));
        if (engine.rootObjects().isEmpty()) { app.exit(1); return; }
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window) { app.exit(1); return; }
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
            QTextStream(stdout) << "ready " << elapsed.elapsed() << "\n" << Qt::flush;
            if (smoke) app.quit();
        }, Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
    });
    delay.start();
    if (smoke) QTimer::singleShot(8000, &app, [&app] { app.exit(2); });
    return app.exec();
}

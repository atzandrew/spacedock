#include "dockbackend.h"
#include "docklifecycle.h"
#include <QSessionManager>
#include "tooltipshadow.h"
#include "tuning.h"
#include "startupenvironment.h"
#include "submenuplacement.h"
#include <QQuickItem>
#include <QQuickWindow>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QScopeGuard>
#include <KConfigGroup>
#include <functional>
#include <memory>

// Diagnostics only: observe native delivery without accepting or changing it.
class DropTrace : public QObject {
public:
    using QObject::QObject;
    bool eventFilter(QObject *receiver, QEvent *event) override {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove
            || event->type() == QEvent::Drop) {
            // Enter/drop delimit delivery; omit per-motion logging to avoid
            // perturbing input timing while profiling.
            if (event->type() == QEvent::DragMove) return false;
            const auto *drop = static_cast<QDropEvent *>(event);
            qInfo() << "drop-trace native" << event->type() << receiver->metaObject()->className()
                    << "window" << (qobject_cast<QWindow *>(receiver) ? qobject_cast<QWindow *>(receiver)->title() : QString())
                    << "formats" << drop->mimeData()->formats()
                    << "urlCount" << drop->mimeData()->urls().size()
                    << "actions" << drop->possibleActions() << "proposed" << drop->proposedAction();
        } else if (event->type() == QEvent::DragLeave) {
            qInfo() << "drop-trace native leave" << receiver->metaObject()->className();
        }
        return false;
    }
};

int main(int argc, char **argv)
{
    // Select the shell integration before Qt connects to Wayland.
    bool preview = false;
    for (int i = 1; i < argc; ++i)
        preview |= QByteArray(argv[i]) == "--preview" || QByteArray(argv[i]) == "--smoke-test" || (QByteArray(argv[i]) == "--capture-preview" || QByteArray(argv[i]).startsWith("--capture-preview="));
    StartupEnvironment startupEnvironment(!preview);
    QApplication app(argc, argv);
    QElapsedTimer eventLoopClock;
    QTimer eventLoopProbe;
    if (qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_RESIZE")) {
        eventLoopClock.start();
        eventLoopProbe.setInterval(100);
        QObject::connect(&eventLoopProbe, &QTimer::timeout, &app, [&] {
            const auto elapsed = eventLoopClock.restart();
            if (elapsed > 200) qInfo() << "resize-profile event-loop gap ms" << elapsed;
        });
        eventLoopProbe.start();
    }
    app.setApplicationName("spacedock");
    app.setDesktopFileName("spacedock");
    app.setOrganizationName("spacedock");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("A classic-style standalone dock for Plasma Wayland.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"preview", "Use a regular development window instead of layer shell."});
    parser.addOption({"smoke-test", "Load the UI in preview mode and exit automatically."});
    parser.addOption({"tuning", "Path to hot-reloaded tuning.json.", "path"});
    parser.addOption({"capture-preview", "Save a development preview image and exit.", "path"});
    parser.addOption({"launch-test", "Add the real three-second startup test app to this dock session."});
    parser.addOption({"trace-tasks", "Print observed running/startup/active state transitions for diagnosis."});
    parser.addOption({"trace-drops", "Print native drag payload types and pin validation/results (no URL contents)."});
    parser.addOption({"check-reorder", "Check real task-model reorder permutations using disposable pins, without dock surfaces."});
    parser.addOption({"trace-drag", "Compare native pointer, mapped input and artwork grab positions during icon movement."});
    parser.addOption({"debug-open-menu", "Open the context menu for task <row> after startup and log its geometry.", "row"});
    parser.process(app);
    const bool checkReorder = parser.isSet("check-reorder");
    DropTrace dropTrace;
    if (parser.isSet("trace-drops")) app.installEventFilter(&dropTrace);
    SubmenuPlacement submenuPlacement;
    app.installEventFilter(&submenuPlacement);
    QString tuningPath = parser.value("tuning");
    if (tuningPath.isEmpty()) {
        tuningPath = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "spacedock/tuning.json");
        if (tuningPath.isEmpty()) tuningPath = ":/tuning.json";
    }
    if (!preview && QStandardPaths::locate(QStandardPaths::GenericDataLocation,
            "applications/spacedock.desktop").isEmpty()) {
        qCritical() << "Dock desktop entry is not registered. Run ./tools/run-dev.sh from the source directory"
                    << "to register the build before connecting to KWin.";
        return 1;
    }
    Tuning tuning(tuningPath, nullptr, !preview && !checkReorder);
    QQmlApplicationEngine engine;
    std::unique_ptr<QTemporaryDir> checkConfig;
    KSharedConfig::Ptr backendConfig;
    if (checkReorder) {
        // Keep KDE window-matching rules intact; isolate only our pin writes.
        checkConfig = std::make_unique<QTemporaryDir>();
        if (!checkConfig->isValid()) return 1;
        backendConfig = KSharedConfig::openConfig(checkConfig->filePath("dockrc"), KConfig::SimpleConfig);
        const KConfigGroup userPins(KSharedConfig::openConfig("spacedockrc"), "Launchers");
        if (userPins.hasKey("applications"))
            KConfigGroup(backendConfig, "Launchers").writeEntry("applications", userPins.readEntry("applications", QStringList()));
    }
    DockBackend backend(&engine, parser.isSet("smoke-test") || parser.isSet("capture-preview"), nullptr, backendConfig);
    // QML bindings and image providers use the backend. Destroy the windows
    // while it is still alive, including on early returns after loading QML.
    const auto destroyWindows = qScopeGuard([&engine] { qDeleteAll(engine.rootObjects()); });
    if (!backend.isReady()) {
        qCritical().noquote() << "KDE task manager module unavailable:" << backend.error();
        return 1;
    }
    if (parser.isSet("launch-test") && !backend.addLaunchTest()) {
        qCritical() << "Cannot add launch test: use ./tools/run-dev.sh --launch-test to register the real test app first.";
        return 1;
    }
    backend.setProperty("traceDrops", parser.isSet("trace-drops"));
    backend.setDragTracing(parser.isSet("trace-drag"));
    backend.setTaskTracing(parser.isSet("trace-tasks"));
    if (checkReorder) {
        auto wait = [](int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); };
        wait(1500); // Allow native task enumeration/launcher replacement to settle.
        auto keys = [&backend] {
            QStringList result;
            for (int i = 0; i < backend.rowCount(); ++i)
                result.append(backend.data(backend.index(i, 0), DockBackend::AppKey).toString());
            return result;
        };
        const auto original = keys();
        int pinned = 0;
        while (pinned < backend.rowCount() && backend.data(backend.index(pinned, 0), DockBackend::Pinned).toBool()) ++pinned;
        qInfo() << "reorder-check pinned" << pinned << "tasks" << original.size();
        if (pinned < 3) return 1;
        for (const int from : {0, pinned / 2, pinned - 1}) for (int to = 0; to < pinned; ++to) {
            if (from == to) continue;
            auto expected = keys();
            expected.move(from, to);
            const bool moved = backend.reorder(from, to);
            wait(100);
            if (!moved || keys() != expected) {
                qCritical() << "reorder-check FAIL" << from << to << "expected" << expected << "actual" << keys();
                return 1;
            }
            if (!backend.reorder(to, from)) return 1;
            wait(100);
            if (keys() != original) { qCritical() << "reorder-check restore FAIL"; return 1; }
        }
        qInfo() << "reorder-check PASS: complete visible permutations and inverse moves";
        return 0;
    }
    engine.addImageProvider("tasks", new TaskIcons(&backend));
    engine.addImageProvider("tooltipshadows", new ToolTipShadows);
    engine.rootContext()->setContextProperty("dockBackend", &backend);
    engine.rootContext()->setContextProperty("tuning", &tuning);
    engine.rootContext()->setContextProperty("previewMode", preview);
    engine.load(QUrl(preview ? "qrc:/qml/PreviewWindow.qml" : "qrc:/qml/DockWindow.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    DockLifecycle lifecycle(engine.rootObjects().first()->findChild<QObject *>("dockContent"));
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &lifecycle,
        [&lifecycle](QSessionManager &) { lifecycle.requestShutdown(); });
    // The visible window has its native shell now. Do not leak that selection
    // into applications started by libtaskmanager/KIO.
    startupEnvironment.restore();
    if (parser.isSet("capture-preview")) {
        const auto path = parser.value("capture-preview");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(tuning.autoHide().value("slideDurationMs").toInt() + 150, &app, [window, path] {
            QCoreApplication::exit(window && window->grabWindow().save(path) ? 0 : 1);
        });
    }
    if (parser.isSet("debug-open-menu")) {
        // Development aid: open a task menu without input injection so its
        // native placement can be screenshotted and measured.
        const int row = parser.value("debug-open-menu").toInt();
        QObject *root = engine.rootObjects().first();
        QTimer::singleShot(2500, &app, [root, row] {
            // Delegates have no QObject parent, so walk the visual item tree.
            QList<QObject *> menus;
            std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
                for (QObject *child : item->children())
                    if (child->objectName() == "taskMenu") menus.append(child);
                for (QQuickItem *child : item->childItems()) collect(child);
            };
            if (auto *window = qobject_cast<QQuickWindow *>(root)) collect(window->contentItem());
            for (QObject *menu : menus) {
                const auto task = qvariant_cast<QObject *>(menu->property("taskItem"));
                if (!task || task->property("index").toInt() != row) continue;
                // Hover the icon first, as a real right-click would, and let
                // magnification settle before opening.
                auto *dock = qvariant_cast<QQuickItem *>(menu->property("dock"));
                auto *target = qvariant_cast<QQuickItem *>(menu->property("anchor"));
                if (dock && target) {
                    dock->setProperty("pointerX", target->x() + target->width() / 2);
                    dock->setProperty("pointerInside", true);
                }
                QTimer::singleShot(1200, menu, [menu, target] {
                    const qreal before = target ? target->mapToScene(QPointF()).x() : 0;
                    QMetaObject::invokeMethod(menu, "openForIcon");
                    qInfo().noquote() << "debug-open-menu anchor at open" << before;
                });
                QTimer::singleShot(2000, menu, [menu] {
                    auto *anchor = qvariant_cast<QQuickItem *>(menu->property("anchor"));
                    auto *panel = qvariant_cast<QQuickItem *>(menu->property("background"));
                    QWindow *popup = panel ? panel->window() : nullptr;
                    qInfo().noquote() << "debug-open-menu"
                        << "menu.x" << menu->property("x").toDouble()
                        << "overlap" << menu->property("overlap").toDouble()
                        << "width" << menu->property("width").toDouble()
                        << "pointerX" << menu->property("pointerX").toDouble()
                        << "pointerTipX" << menu->property("pointerTipX").toDouble()
                        << "anchor" << (anchor ? anchor->mapRectToScene(QRectF(0, 0, anchor->width(), anchor->height())) : QRectF())
                        << "popup" << (popup ? popup->geometry() : QRect())
                        << "dockWindow" << (anchor && anchor->window() ? anchor->window()->geometry() : QRect())
                        << "dock" << (qvariant_cast<QQuickItem *>(menu->property("dock"))
                            ? qvariant_cast<QQuickItem *>(menu->property("dock"))->mapRectToScene(QRectF(QPointF(),
                                qvariant_cast<QQuickItem *>(menu->property("dock"))->size())) : QRectF())
                        << "anchorLocalX" << (anchor ? anchor->x() : 0)
                        << "placement" << (qvariant_cast<QQuickItem *>(menu->property("parent"))
                            ? qvariant_cast<QQuickItem *>(menu->property("parent"))->mapRectToScene(QRectF(QPointF(),
                                qvariant_cast<QQuickItem *>(menu->property("parent"))->size())) : QRectF());
                });
                return;
            }
            qWarning() << "debug-open-menu: no task row" << row;
        });
    }
    if (parser.isSet("smoke-test")) {
        // Also instantiate the production window to catch layer-shell QML errors.
        engine.load(QUrl("qrc:/qml/DockWindow.qml"));
        if (engine.rootObjects().size() != 2) return 1;
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    }
    return app.exec();
}

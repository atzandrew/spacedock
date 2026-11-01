#include "dockbackend.h"
#include "tooltipshadow.h"
#include "tuning.h"
#include "ui_test_support.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlComponent>
#include <QJSValue>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QDir>
#include <QFile>

class ArtworkUiTest : public QObject {
    Q_OBJECT
private slots:
    void customIconCanBeChangedAndReset() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        const auto choices = backend.iconChoices();
        QVERIFY(!choices.isEmpty());
        const auto key = choices.first().toMap().value("key").toString();
        QTemporaryDir dir;
        const auto path = dir.filePath("icon.png");
        QImage image(16, 16, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(path));
        QVERIFY(backend.setCustomIcon(key, QUrl::fromLocalFile(path)));
        TaskIcons provider(&backend);
        QSize reported;
        QCOMPARE(provider.requestPixmap("1/0", &reported, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::red));
        QCOMPARE(backend.icon(0, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::red));
        image.fill(Qt::blue);
        QVERIFY(image.save(path));
        QVERIFY(backend.setCustomIcon(key, QUrl::fromLocalFile(path)));
        QCOMPARE(backend.icon(0, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::blue));
        QCOMPARE(provider.requestPixmap("2/0", &reported, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::blue));
        DockBackend restarted(&engine, true);
        QCOMPARE(restarted.icon(0, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::blue));
        QVERIFY(!backend.setCustomIcon(key, QUrl::fromLocalFile(dir.filePath("missing"))));
        QVERIFY(!backend.saveApplicationEdit(key, " ", {}, true));
        QVERIFY(backend.saveApplicationEdit(key, "My Files", {}, true));
        QCOMPARE(backend.data(backend.index(0, 0), DockBackend::Name).toString(), QString("My Files"));
        QCOMPARE(restarted.data(restarted.index(0, 0), DockBackend::Name).toString(), QString("My Files"));
        QVERIFY(!backend.iconChoices().first().toMap().value("custom").toBool());
        const auto previousTheme = QIcon::themeName();
        const auto previousPaths = QIcon::themeSearchPaths();
        QVERIFY(QDir().mkpath(dir.filePath("testtheme/16x16/apps")));
        QFile index(dir.filePath("testtheme/index.theme"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("[Icon Theme]\nName=Test\nDirectories=16x16/apps\n\n[16x16/apps]\nSize=16\nType=Fixed\nContext=Applications\n");
        index.close();
        QVERIFY(image.save(dir.filePath("testtheme/16x16/apps/test-icon.png")));
        QIcon::setThemeSearchPaths({dir.path()});
        QIcon::setThemeName("testtheme");
        QVERIFY(backend.saveApplicationEdit(key, "My Files", {}, false, "test-icon"));
        QCOMPARE(backend.icon(0, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::blue));
        QCOMPARE(restarted.icon(0, QSize(16, 16)).toImage().pixelColor(8, 8), QColor(Qt::blue));
        QVERIFY(backend.saveApplicationEdit(key, "My Files", {}, true));
        QVERIFY(!backend.iconChoices().first().toMap().value("custom").toBool());
        QIcon::setThemeSearchPaths(previousPaths);
        QIcon::setThemeName(previousTheme);
        QVERIFY(backend.saveApplicationEdit(key, "Files", {}, false));
    }
    void appImageArtworkWaitsForReplacement() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        QQmlComponent component(&engine);
        component.setData(R"QML(
import QtQuick
Item {
    id: root
    width: 600; height: 224
    property int delegateRevision: 0
    property var geometry: ({})
    property var layout: ({reflectionOpacity: 0.2})
    property int partingDurationMs: 120
    property real iconBottom: 200
    Item { id: viewport }
    Item {
        id: original
        x: 10; y: 136; width: 64; height: 64
        property string appKey: "original"
        property bool needsAppImageSetup: true
        property real bounceOffset: 0
        property real appImageOpenProgress: 1
    }
    Item {
        id: replacement
        objectName: "replacementArtwork"
        x: 100; y: 136; width: 64; height: 64
        property string appKey: "replacement"
        property bool needsAppImageSetup: false
        property bool pinned: false
        property int index: 1
        property real bounceOffset: 0
        property url iconUrl: ""
    }
    QtObject {
        id: taskList
        property int count: 2
        function itemAt(index) { return index === 0 ? original : replacement }
    }
    AppImageArtworkTransition {
        objectName: "transitionUnderTest"
        anchors.fill: parent
        dock: root; tasks: taskList; viewport: viewport
    }
})QML", QUrl("qrc:/qml/AppImageTransitionHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QQuickItem> root(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY(root);
        QQuickWindow window;
        window.resize(600, 224);
        root->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *transition = root->findChild<QQuickItem *>("transitionUnderTest");
        auto *incoming = transition->findChild<QQuickItem *>("appImageIncomingArtwork");
        auto *box = transition->findChild<QQuickItem *>("appImageTransitionPlaceholder");
        QVERIFY(transition && incoming && box);
        QQmlExpression begin(engine.rootContext(), transition, "begin('original', 'replacement', 'stable', 0)");
        begin.evaluate();
        QVERIFY2(!begin.hasError(), qPrintable(begin.error().toString()));
        QTest::qWait(250);
        QVERIFY(transition->property("active").toBool());
        QVERIFY(!incoming->property("ready").toBool());
        QCOMPARE(box->opacity(), 1.0);
        auto *replacement = root->findChild<QQuickItem *>("replacementArtwork");
        QCOMPARE(box->x(), replacement->x());
        replacement->setX(160);
        QTRY_COMPARE(box->x(), replacement->x());
        QVERIFY(replacement->setProperty("iconUrl", QUrl("qrc:/assets/icons/appimage_open.png")));
        QTRY_VERIFY(incoming->property("ready").toBool());
        QTest::qWait(150);
        QVERIFY(transition->property("active").toBool());
        QCOMPARE(box->opacity(), 1.0); // Ready artwork alone is not a completed pin.
        replacement->setProperty("pinned", true);
        // The model may settle at a different index after merging tasks.
        QTRY_VERIFY(!transition->property("active").toBool());
        QCOMPARE(box->opacity(), 0.0);
        QCOMPARE(transition->property("progress").toDouble(), 1.0);
        root->setParentItem(nullptr);
    }
    void shadowRasterIsCachedAndStrengthensAlpha() {
        ToolTipShadows provider;
        QJsonObject spec {{"text", "Music"}, {"width", 120}, {"height", 72},
            {"dpr", 1}, {"family", "Inter"}, {"fontSize", 16}, {"weight", 600},
            {"baseline", 40}, {"offset", 3}, {"sourceScale", 1}, {"blur", 24}, {"coreBlur", 6}, {"strength", 6}};
        auto key = [&] { return QString::fromUtf8(QJsonDocument(spec).toJson(QJsonDocument::Compact).toPercentEncoding()); };
        QSize size;
        QElapsedTimer clock;
        clock.start();
        const auto image = provider.requestImage(key(), &size, {});
        const auto coldNs = clock.nsecsElapsed();
        QCOMPARE(size, QSize(120, 72));
        clock.restart();
        const auto cacheKey = key();
        for (int i = 0; i < 1000; ++i)
            QCOMPARE(provider.requestImage(cacheKey, &size, {}).cacheKey(), image.cacheKey());
        qInfo() << "Shadow cold render (us):" << coldNs / 1000.0
                << "cached request average (us):" << clock.nsecsElapsed() / 1000000.0;
        spec["sourceScale"] = 0.8;
        const auto smallerSource = provider.requestImage(key(), &size, {});
        qint64 smallerAlpha = 0, originalAlpha = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                smallerAlpha += qAlpha(smallerSource.pixel(x, y));
                originalAlpha += qAlpha(image.pixel(x, y));
            }
        QVERIFY(smallerAlpha > 0);
        QVERIFY(smallerAlpha < originalAlpha);
        spec["sourceScale"] = 1;
        spec["strength"] = 1.5;
        const auto lighter = provider.requestImage(key(), &size, {});
        qint64 darkAlpha = 0, lightAlpha = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                darkAlpha += qAlpha(image.pixel(x, y));
                lightAlpha += qAlpha(lighter.pixel(x, y));
            }
        QVERIFY(darkAlpha > lightAlpha);
        QVERIFY(lightAlpha > 0);
        spec["dpr"] = 2;
        const auto retina = provider.requestImage(key(), &size, {});
        QCOMPARE(retina.size(), QSize(240, 144));
    }
    void tooltipsAndFeedbackTrackTaskState() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        QVERIFY2(backend.isReady(), qPrintable(backend.error()));
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(content);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        QTRY_COMPARE(taskItems(content).size(), 13);
        const auto icons = taskItems(content);
        auto *tooltip = content->findChild<QQuickItem *>("dockToolTip");
        QVERIFY(tooltip);
        const QPoint center = icons[6]->mapToScene(QPointF(icons[6]->width() / 2, icons[6]->height() / 2)).toPoint();
        QTest::mouseMove(window, center);
        QTRY_VERIFY(tooltip->isVisible());
        QCOMPARE(tooltip->property("label").toString(), QString("Music"));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(!tooltip->isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_VERIFY(tooltip->isVisible());
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, center);
        QTRY_VERIFY(content->property("openMenus").toInt() > 0);
        QVERIFY(!tooltip->isVisible());
        auto *menu = icons[6]->findChild<QObject *>("taskMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(content->property("openMenus").toInt(), 0);
        QTest::mouseMove(window, QPoint(5, 5));
        QTest::mouseMove(window, center);
        QTRY_VERIFY(tooltip->isVisible());
        const QString longName = "<b>" + QString(180, 'W') + "</b>";
        QVERIFY(backend.sourceModel()->setData(backend.sourceModel()->index(6, 0), longName, Qt::UserRole + 1));
        QTRY_COMPARE(tooltip->property("label").toString(), longName);
        QVERIFY(tooltip->x() >= 0);
        QVERIFY(tooltip->x() + tooltip->width() <= window->width());
        const auto leftIcon = icons[0]->mapToScene(QPointF(icons[0]->width() / 2, icons[0]->height() / 2)).toPoint();
        QTest::mouseMove(window, leftIcon);
        QTRY_COMPARE(tooltip->property("label").toString(), QString("Files"));
        QVERIFY(tooltip->x() >= 0);
        QVERIFY(tooltip->x() + tooltip->width() <= window->width());
        QTest::mouseMove(window, QPoint(5, 5));
        QTRY_VERIFY(content->property("amount").toDouble() < 0.01);

        auto *bounce = qobject_cast<QQuickItem *>(icons[1]->property("bounce").value<QObject *>());
        QVERIFY(bounce);
        const auto task = backend.sourceModel()->index(1, 0);
        const int duration = tuning.feedback().value("bounceDurationMs").toInt();
        // A quick launch without startup notifications still completes exactly
        // one bounce, even if the window appears before that bounce reaches its peak.
        const auto launchPoint = icons[1]->mapToScene(QPointF(icons[1]->width() / 2, icons[1]->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, launchPoint);
        QTRY_VERIFY(bounce->property("playing").toBool());
        QVERIFY(!icons[1]->property("launching").toBool());
        QTest::qWait(duration / 4);
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 2));
        QTRY_VERIFY(icons[1]->property("running").toBool());
        QTest::qWait(duration / 4);
        QVERIFY(bounce->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!bounce->property("playing").toBool(), 1000);
        QTest::qWait(duration + 100);
        QVERIFY(!bounce->property("playing").toBool());
        QCOMPARE(icons[1]->property("bounceOffset").toDouble(), 0);
        QVERIFY(backend.sourceModel()->setData(task, false, Qt::UserRole + 2));
        QTest::mouseMove(window, QPoint(5, 5));
        QTRY_VERIFY(content->property("amount").toDouble() < 0.01);

        // A genuinely slow startup repeats, but a running window overrides
        // stale startup metadata instead of waiting for a fallback timer.
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 4));
        QTRY_VERIFY(icons[1]->property("launching").toBool());
        QTRY_VERIFY(icons[1]->property("bounceOffset").toDouble() > 4);
        const auto top = icons[1]->mapToScene(QPointF(icons[1]->width() / 2,
            -icons[1]->property("bounceOffset").toDouble() + 2)).toPoint();
        QTRY_VERIFY(window->mask().contains(top));
        QTest::qWait(tuning.feedback().value("bounceDurationMs").toInt() + 100);
        QVERIFY(bounce->property("playing").toBool());
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 2));
        QTRY_VERIFY_WITH_TIMEOUT(!bounce->property("playing").toBool(), 1000);
        QVERIFY(icons[1]->property("launching").toBool());
        QVERIFY(!icons[1]->property("loading").toBool());
        QCOMPARE(icons[1]->property("bounceOffset").toDouble(), 0);
        QTest::qWait(duration + 100);
        QVERIFY(!bounce->property("playing").toBool());
        QVERIFY(backend.sourceModel()->setData(task, false, Qt::UserRole + 4));
        QVERIFY(backend.sourceModel()->setData(task, false, Qt::UserRole + 2));

        // Startup completion during the first cycle lets that cycle land.
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 4));
        QTRY_VERIFY(bounce->property("playing").toBool());
        QTest::qWait(duration / 4);
        QVERIFY(backend.sourceModel()->setData(task, false, Qt::UserRole + 4));
        QTest::qWait(duration / 4);
        QVERIFY(bounce->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!bounce->property("playing").toBool(), 1000);

        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 5));
        QTRY_VERIFY(icons[1]->property("attention").toBool());
        QTRY_VERIFY(icons[1]->property("bounceOffset").toDouble() > 4);
        QTRY_VERIFY_WITH_TIMEOUT(!bounce->property("playing").toBool(), 2000);
        QVERIFY(icons[1]->property("attention").toBool());
        QCOMPARE(icons[1]->property("bounceOffset").toDouble(), 0);
        QVERIFY(backend.sourceModel()->setData(task, false, Qt::UserRole + 5));
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 5));
        QTRY_VERIFY(bounce->property("playing").toBool());
        QVERIFY(backend.sourceModel()->setData(task, true, Qt::UserRole + 6));
        QTRY_VERIFY(!bounce->property("playing").toBool());
        auto *editor = content->findChild<QQuickWindow *>("applicationSettings");
        QVERIFY(editor);
        QVERIFY(QMetaObject::invokeMethod(editor, "edit", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(editor->isVisible());
        auto *nameField = editor->findChild<QObject *>("applicationNameField");
        QVERIFY(nameField);
        QCOMPARE(nameField->property("text").toString(), QString("Files"));
        auto *iconPicker = editor->findChild<QObject *>("systemIconPicker");
        QVERIFY(iconPicker);
        QVERIFY(QMetaObject::invokeMethod(iconPicker, "open"));
        QTRY_VERIFY(iconPicker->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(iconPicker, "close"));
        editor->close();
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

    void taskReplacementPreservesBounce_data() {
        QTest::addColumn<double>("completionPhase");
        QTest::newRow("ready while rising") << 0.25;
        QTest::newRow("ready while falling") << 0.75;
    }
    void taskReplacementPreservesBounce() {
        QFETCH(double, completionPhase);
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        QVERIFY(backend.isReady());
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(content);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(5, 5));
        QTRY_COMPARE(taskItems(content).size(), 13);
        auto *model = qobject_cast<QStandardItemModel *>(backend.sourceModel());
        QVERIFY(model);
        auto findTerminal = [&]() -> QQuickItem * {
            for (auto *item : taskItems(content))
                if (item->property("appKey").toString() == "Terminal") return item;
            return nullptr;
        };
        QVERIFY(findTerminal());
        auto *bounce = findTerminal()->property("bounce").value<QObject *>();
        QVERIFY(bounce);
        QVERIFY(model->setData(model->index(1, 0), true, Qt::UserRole + 4));
        QTRY_VERIFY(bounce->property("playing").toBool());
        const int duration = tuning.feedback().value("bounceDurationMs").toInt();
        QTest::qWait(qRound(duration * completionPhase));
        const auto heightBefore = bounce->property("offset").toDouble();
        QVERIFY(heightBefore > 1);
        auto *readyWindow = model->item(1)->clone();
        readyWindow->setData(true, Qt::UserRole + 2);
        readyWindow->setData(false, Qt::UserRole + 4);
        readyWindow->setData(true, Qt::UserRole + 6);
        // Startup IDs and window IDs can differ only by the desktop suffix.
        readyWindow->setData("Terminal.desktop", Qt::UserRole + 7);
        QVERIFY(model->removeRow(1));
        model->insertRow(1, readyWindow);
        QVERIFY(findTerminal());
        QCOMPARE(findTerminal()->property("bounce").value<QObject *>(), bounce);
        QCOMPARE(findTerminal()->property("bounceOffset").toDouble(), heightBefore);
        QTRY_VERIFY(!bounce->property("launchActive").toBool());
        QVERIFY(bounce->property("playing").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!bounce->property("playing").toBool(), 1500);
        QCOMPARE(findTerminal()->property("bounceOffset").toDouble(), 0);
        QTest::qWait(duration + 50);
        QVERIFY(!bounce->property("playing").toBool());
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
};
int main(int argc, char **argv) {
    return runDockUiTest<ArtworkUiTest>(argc, argv);
}
#include "artwork_ui_test.moc"

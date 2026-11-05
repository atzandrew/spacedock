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
#include "launcher_model.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcess>
#include <QProcessEnvironment>
#include <KConfigGroup>
#include <KShell>
#include <KSycoca>
#include <KService>
#include <functional>

class AppImageSetupModel : public PinDropModel {
    Q_OBJECT
public:
    int launches = 0;
    int pinRequests = 0;
    int moveRequests = 0;
    bool allowAddition = true;
    bool allowRemoval = true;
    std::function<void()> onAdd;
    Q_INVOKABLE void requestActivate(const QModelIndex &) { ++launches; }
    Q_INVOKABLE bool requestAddLauncher(const QUrl &url) {
        ++pinRequests;
        if (!allowAddition) return false;
        const bool added = !launchers.contains(url.toString());
        if (added) launchers.append(url.toString());
        for (int row = 0; row < rowCount(); ++row)
            if (item(row)->data(Qt::UserRole + 1).toUrl() == url) item(row)->setData(true, Qt::UserRole + 4);
        if (onAdd) onAdd();
        return added;
    }
    Q_INVOKABLE virtual bool move(int, int, const QModelIndex &) { ++moveRequests; return false; }
    Q_INVOKABLE bool requestRemoveLauncher(const QUrl &url) {
        if (!allowRemoval || !launchers.removeAll(url.toString())) return false;
        for (int row = 0; row < rowCount(); ++row)
            if (item(row)->data(Qt::UserRole + 1).toUrl() == url) item(row)->setData(false, Qt::UserRole + 4);
        return true;
    }
};

class MovingAppImageSetupModel : public AppImageSetupModel {
public:
    bool move(int from, int to, const QModelIndex &) override {
        ++moveRequests;
        if (from < 0 || to < 0 || from >= rowCount() || to >= rowCount()) return false;
        insertRow(to, takeRow(from));
        return true;
    }
};

class AppImageIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void appImageDropsCreateLauncherReferences() {
        QTemporaryDir directory;
        const auto path = directory.filePath("Photo craft $`%\\\".AppImage");
        QFile image(path);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("AppImage test fixture (never executed)");
        image.close();
        const auto url = QUrl::fromLocalFile(path);
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        PinDropModel model;
        model.setProperty("launcherList", QStringList{});
        backend.setSourceModel(&model);
        QVERIFY(!backend.canPinUrls({url}));
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const auto launcherDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/launchers";
        const auto beforeValidation = QDir(launcherDirectory).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        QVERIFY(backend.canPinUrls({url}));
        QCOMPARE(QDir(launcherDirectory).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot), beforeValidation);
        QVERIFY(backend.pinUrls({url, url}, -1));
        const auto pins = model.property("launcherList").toStringList();
        QCOMPARE(pins.size(), 1);
        const auto launcher = QUrl(pins.first());
        QCOMPARE(DockBackend::applicationLauncher(launcher), launcher);
        KConfig desktop(launcher.toLocalFile(), KConfig::SimpleConfig);
        KConfigGroup entry(&desktop, "Desktop Entry");
        QCOMPARE(entry.readEntry("Name", QString()), QFileInfo(path).completeBaseName());
        auto command = entry.readEntry("Exec", QString());
        command.replace("%%", "%");
        KShell::Errors error;
        const auto arguments = KShell::splitArgs(command, KShell::AbortOnMeta, &error);
        QCOMPARE(error, KShell::NoError);
        QCOMPARE(arguments, (QStringList{"env", "BAMF_DESKTOP_FILE_HINT=" + launcher.toLocalFile(), path}));
        QCOMPARE(entry.readEntry("X-Spacedock-AppImage", QString()), path);
        QVERIFY(entry.readEntry("Icon", QString()).isEmpty());
        QVERIFY(backend.pinUrls({url}, -1));
        QCOMPARE(model.property("launcherList").toStringList(), pins);
        QVERIFY(QFile::exists(path));
        QVERIFY(!backend.canPinUrls({QUrl("https://example.com/test.AppImage")}));

        // Learn the running application's artwork, then retain it for its pin.
        model.setItemRoleNames({{Qt::UserRole + 1, "LauncherUrlWithoutIcon"}, {Qt::UserRole + 2, "IsWindow"}});
        QPixmap artwork(64, 64);
        artwork.fill(QColor("#48a070"));
        auto *windowTask = new QStandardItem(QIcon(artwork), "Photocraft");
        windowTask->setData(launcher, Qt::UserRole + 1);
        windowTask->setData(true, Qt::UserRole + 2);
        model.appendRow(windowTask);
        backend.cacheAppImageIcons();
        desktop.reparseConfiguration();
        const auto cachedIcon = entry.readEntry("Icon", QString());
        QVERIFY(QFile::exists(cachedIcon));
        QCOMPARE(QImage(cachedIcon).pixelColor(10, 10), QColor("#48a070"));
        windowTask->setData(false, Qt::UserRole + 2);
        windowTask->setIcon(QIcon::fromTheme("application-x-executable"));
        backend.cacheAppImageIcons();
        QCOMPARE(QImage(cachedIcon).pixelColor(10, 10), QColor("#48a070"));
        // Re-pinning refreshes the launch command without losing learned artwork.
        QVERIFY(backend.pinUrls({url}, -1));
        desktop.reparseConfiguration();
        QCOMPARE(entry.readEntry("Icon", QString()), cachedIcon);
        // Existing pins from the first AppImage implementation upgrade in place.
        entry.writeEntry("Exec", KShell::quoteArg(path).replace("%", "%%"));
        entry.deleteEntry("X-Spacedock-AppImage");
        entry.writeEntry("Icon", "application-x-executable");
        QVERIFY(entry.sync());
        DockBackend restarted(&engine);
        desktop.reparseConfiguration();
        QCOMPARE(entry.readEntry("X-Spacedock-AppImage", QString()), path);
        QVERIFY(entry.readEntry("Exec", QString()).contains("BAMF_DESKTOP_FILE_HINT="));
        QCOMPARE(entry.readEntry("Icon", QString()), cachedIcon);
    }
    void appImageLearnsRunningLauncherIdentity() {
        QTemporaryDir directory;
        const auto imagePath = directory.filePath("Editor.AppImage");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("test fixture, never executed");
        image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        PinDropModel model;
        model.setItemRoleNames({{Qt::UserRole + 1, "LauncherUrlWithoutIcon"}, {Qt::UserRole + 2, "IsWindow"},
            {Qt::UserRole + 3, "AppId"}, {Qt::UserRole + 4, "AppPid"}});
        backend.setSourceModel(&model);
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        const auto original = model.launchers.first();
        const auto mountedDesktop = directory.filePath("runtime-editor.desktop");
        QFile desktop(mountedDesktop);
        QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write("[Desktop Entry]\nType=Application\nName=Editor\nExec=/bin/true\n");
        desktop.close();
        const auto mountedUrl = QUrl::fromLocalFile(mountedDesktop);
        model.launchers = {"left", original, "right", mountedUrl.toString()};
        QPixmap artwork(32, 32);
        artwork.fill(QColor("#589ad0"));
        auto *window = new QStandardItem(QIcon(artwork), "Editor");
        window->setData(mountedUrl, Qt::UserRole + 1);
        window->setData(true, Qt::UserRole + 2);
        window->setData("runtime-editor", Qt::UserRole + 3);
        model.appendRow(window);
        backend.reconcileAppImageLaunchers();
        QCOMPARE(model.launchers[1], original); // Names alone never establish a match.
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("APPIMAGE", imagePath);
        process.setProcessEnvironment(environment);
        process.start("/bin/sleep", {"30"});
        QVERIFY(process.waitForStarted());
        window->setData(uint(process.processId()), Qt::UserRole + 4);
        // Exercise the replacement source notification path, including queued reconciliation.
        QTRY_COMPARE(model.launchers.size(), 3);
        QCOMPARE(model.launchers.first(), "left");
        QCOMPARE(model.launchers.last(), "right");
        const QUrl learned(model.launchers[1]);
        QVERIFY(learned.toString() != original);
        QCOMPARE(learned.fileName(), "runtime-editor.desktop");
        QVERIFY(QFile::exists(learned.toLocalFile() + ".png"));
        KConfig retained(learned.toLocalFile(), KConfig::SimpleConfig);
        QCOMPARE(KConfigGroup(&retained, "Desktop Entry").readEntry("X-Spacedock-AppImage", QString()), imagePath);
        process.terminate();
        QVERIFY(process.waitForFinished());
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        QCOMPARE(model.launchers, (QStringList{"left", learned.toString(), "right"}));
    }
    void appImageReusesRegisteredLauncher() {
        QTemporaryDir directory;
        const auto imagePath = directory.filePath("Registered.AppImage");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("test fixture, never executed");
        image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        PinDropModel model;
        backend.setSourceModel(&model);
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        const auto generated = model.launchers.first();
        const auto applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
        QVERIFY(QDir().mkpath(applications));
        KConfig desktop(applications + "/test.registered-appimage.desktop", KConfig::SimpleConfig);
        KConfigGroup entry(&desktop, "Desktop Entry");
        entry.writeEntry("Type", "Application");
        entry.writeEntry("Name", "Registered AppImage");
        entry.writeEntry("Exec", '"' + imagePath + "\" %F");
        entry.writeEntry("Icon", "applications-graphics");
        QVERIFY(entry.sync());
        const auto menus = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/menus";
        QVERIFY(QDir().mkpath(menus));
        QFile menu(menus + '/' + qEnvironmentVariable("XDG_MENU_PREFIX") + "applications.menu");
        QVERIFY(menu.open(QIODevice::WriteOnly));
        menu.write("<!DOCTYPE Menu PUBLIC '-//freedesktop//DTD Menu 1.0//EN' 'http://www.freedesktop.org/standards/menu-spec/1.0/menu.dtd'>"
            "<Menu><Name>Applications</Name><DefaultAppDirs/><Include><All/></Include></Menu>");
        menu.close();
        QProcess rebuild;
        rebuild.start("kbuildsycoca6", {"--noincremental"});
        QVERIFY(rebuild.waitForFinished(10000));
        QCOMPARE(rebuild.exitCode(), 0);
        KSycoca::self()->ensureCacheValid();
        QTRY_VERIFY_WITH_TIMEOUT(KService::serviceByMenuId("test.registered-appimage.desktop"), 3000);
        const QString registered = "applications:test.registered-appimage.desktop";
        model.launchers = {"left", generated, "right", registered};
        backend.reconcileAppImageLaunchers();
        QCOMPARE(model.launchers, (QStringList{"left", registered, "right"}));
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        QCOMPARE(model.launchers, (QStringList{"left", registered, "right"}));
    }
    void appImageSetupOffersManualReplacement() {
        QTemporaryDir directory;
        const auto imagePath = directory.filePath("Setup Editor.AppImage");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("fixture, never executed");
        image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, false, nullptr,
            KSharedConfig::openConfig(directory.filePath("dockrc"), KConfig::SimpleConfig));
        MovingAppImageSetupModel model;
        model.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "LauncherUrlWithoutIcon"},
            {Qt::UserRole + 2, "IsWindow"}, {Qt::UserRole + 3, "AppId"}, {Qt::UserRole + 4, "HasLauncher"}});
        backend.setSourceModel(&model);
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        const auto placeholder = model.launchers.first();
        auto addTask = [&](const QString &name, const QString &key, const QUrl &url, bool running) {
            auto *item = new QStandardItem(name);
            item->setData(url, Qt::UserRole + 1);
            item->setData(running, Qt::UserRole + 2);
            item->setData(key, Qt::UserRole + 3);
            item->setData(true, Qt::UserRole + 4);
            model.appendRow(item);
            return item;
        };
        addTask("Left", "left", QUrl("applications:left.desktop"), false);
        addTask("Placeholder", "setup-placeholder", QUrl(placeholder), false);
        addTask("Right", "right", QUrl("applications:right.desktop"), false);
        model.launchers = {"applications:left.desktop", placeholder, "applications:right.desktop"};
        QCOMPARE(backend.data(backend.index(1, 0), DockBackend::NeedsAppImageSetup).toBool(), true);
        QCOMPARE(backend.data(backend.index(1, 0), DockBackend::Name).toString(), "Add \"Setup Editor\" AppImage to Dock");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto *placeholderTask = taskItems(content)[1];
        auto *artworkItem = placeholderTask->findChild<QQuickItem *>("taskArtwork");
        QVERIFY(artworkItem);
        QCOMPARE(artworkItem->property("openProgress").toDouble(), 0.0);
        backend.activate(1);
        QCOMPARE(model.launches, 1);
        QVERIFY(backend.appImageSetupPending());
        QVERIFY(!backend.appImageSetupVisible());
        QVERIFY(content->property("holdingOpen").toBool());
        QTRY_COMPARE(artworkItem->property("openProgress").toDouble(), 1.0);
        const auto candidateDesktop = directory.filePath("running-editor.desktop");
        QFile desktop(candidateDesktop);
        QVERIFY(desktop.open(QIODevice::WriteOnly));
        desktop.write("[Desktop Entry]\nType=Application\nName=Chosen Editor\nExec=/bin/true\n");
        desktop.close();
        auto *candidate = addTask("Chosen Editor", "chosen-editor", QUrl::fromLocalFile(candidateDesktop), true);
        QPixmap artwork(32, 32);
        artwork.fill(QColor("#8030b0"));
        candidate->setIcon(QIcon(artwork));
        QTRY_VERIFY_WITH_TIMEOUT(backend.appImageSetupVisible(), 3000);
        QCOMPARE(backend.appImageSetupCandidates().size(), 1);
        QVERIFY(!backend.appImageSetupCandidates().first().toMap()["wasRunning"].toBool());
        QTRY_COMPARE(content->property("openMenus").toInt(), 1);
        backend.activate(1);
        QCOMPARE(model.launches, 1); // Repeated clicks open setup, not another instance.
        const auto originalPins = model.launchers;
        QObject *setupMenu = nullptr;
        for (auto *task : taskItems(content)) {
            auto *menu = task->findChild<QObject *>("appImageSetupMenu");
            if (menu && menu->property("visible").toBool()) setupMenu = menu;
        }
        QVERIFY(setupMenu);
        QCOMPARE(setupMenu->findChild<QObject *>("appImageSetupHeading")->property("text").toString(), "Confirm the AppImage");
        auto *cancel = setupMenu->findChild<QObject *>("appImageSetupCancel");
        QVERIFY(cancel);
        QVERIFY(QMetaObject::invokeMethod(cancel, "triggered"));
        QTRY_COMPARE(content->property("openMenus").toInt(), 0);
        QCOMPARE(model.launchers, originalPins);
        backend.showAppImageSetup(1);
        QTRY_COMPARE(content->property("openMenus").toInt(), 1);
        QVERIFY(!backend.confirmAppImageSetup("already-closed"));
        QVERIFY(!backend.appImageSetupError().isEmpty());
        QCOMPARE(model.launchers, originalPins);
        // Revalidate the stable key at commit; an ambiguous replacement must
        // not pin whichever row happens to appear first.
        model.appendRow(candidate->clone());
        QVERIFY(!backend.confirmAppImageSetup("chosen-editor"));
        QCOMPARE(model.pinRequests, 0);
        QVERIFY(backend.appImageSetupPending());
        model.removeRow(model.rowCount() - 1);
        model.allowAddition = false;
        QVERIFY(!backend.confirmAppImageSetup("chosen-editor"));
        QCOMPARE(model.launchers, originalPins);
        QVERIFY(backend.appImageSetupPending());
        model.allowAddition = true;
        model.onAdd = [&] {
            const auto requests = model.pinRequests;
            QVERIFY(!backend.confirmAppImageSetup("chosen-editor"));
            QCOMPARE(model.pinRequests, requests); // Re-entry cannot commit setup twice.
            backend.reconcileAppImageLaunchers();
            QVERIFY(model.launchers.contains(placeholder)); // Nested cache updates cannot replace it prematurely.
        };
        // The list can move while the chooser is open. Confirmation uses identity.
        model.insertRow(0, model.takeRow(3));
        model.launchers.append(QUrl::fromLocalFile(candidateDesktop).toString()); // A duplicate manual pin.
        QObject *choose = nullptr;
        for (auto *task : taskItems(content)) {
            auto *menu = task->findChild<QObject *>("appImageSetupMenu");
            if (!menu || !menu->property("visible").toBool()) continue;
            for (auto *item : menu->findChildren<QObject *>("appImageSetupCandidate"))
                if (item->property("candidateKey").toString() == "chosen-editor") choose = item;
        }
        QVERIFY(choose);
        QVERIFY(QMetaObject::invokeMethod(choose, "triggered"));
        QCOMPARE(model.pinRequests, 2);
        QVERIFY(model.moveRequests > 0);
        QVERIFY(!backend.appImageSetupPending());
        QTRY_COMPARE(content->property("openMenus").toInt(), 0);
        auto *artworkTransition = content->findChild<QQuickItem *>("appImageArtworkTransition");
        QVERIFY(artworkTransition);
        QVERIFY(artworkTransition->property("active").toBool());
        QTRY_VERIFY(!artworkTransition->property("active").toBool());
        QCOMPARE(model.launchers.size(), 3);
        QCOMPARE(model.launchers.first(), "applications:left.desktop");
        QCOMPARE(model.launchers.last(), "applications:right.desktop");
        QCOMPARE(model.launchers[1], QUrl::fromLocalFile(candidateDesktop).toString());
        const auto savedPins = KConfigGroup(KSharedConfig::openConfig(directory.filePath("dockrc"), KConfig::SimpleConfig), "Launchers")
            .readEntry("applications", QStringList());
        QCOMPARE(savedPins.size(), 3);
        const QUrl replacement(savedPins[1]);
        QCOMPARE(replacement.scheme(), "applications");
        const auto installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + replacement.path());
        QVERIFY(!installed.isEmpty());
        KConfig saved(installed, KConfig::SimpleConfig);
        const KConfigGroup entry(&saved, "Desktop Entry");
        QCOMPARE(entry.readEntry("StartupWMClass", QString()), "chosen-editor");
        QVERIFY(entry.readEntry("NoDisplay", false));
        QCOMPARE(QImage(entry.readEntry("Icon", QString())).pixelColor(4, 4), QColor("#8030b0"));
        int chosenRow = -1;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.item(row)->data(Qt::UserRole + 3).toString() == "chosen-editor") chosenRow = row;
        QVERIFY(chosenRow >= 0);
        model.item(chosenRow)->setData(QUrl::fromLocalFile(directory.filePath("updated-window-launcher.desktop")), Qt::UserRole + 1);
        backend.reconcileAppImageLaunchers();
        QCOMPARE(model.launchers[1], QUrl::fromLocalFile(candidateDesktop).toString()); // URL changes do not mean the app closed.
        QVERIFY(backend.pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        QCOMPARE(model.launchers.size(), 3);
        QCOMPARE(model.launchers[1], QUrl::fromLocalFile(candidateDesktop).toString());
        chosenRow = -1;
        for (int row = 0; row < model.rowCount(); ++row)
            if (model.item(row)->data(Qt::UserRole + 3).toString() == "chosen-editor") chosenRow = row;
        QVERIFY(chosenRow >= 0);
        QVERIFY(model.removeRow(chosenRow)); // The chosen running app closes.
        backend.reconcileAppImageLaunchers();
        QCOMPARE(model.launchers, savedPins); // Future launches use the persistent copy.
        QCOMPARE(model.launches, 1); // Choosing/cancelling never relaunches or closes the application.
        delete window;
    }
    void appImageRemovalForgetsSelection_data() {
        QTest::addColumn<bool>("runningDuringRemoval");
        QTest::addColumn<bool>("registeredChoice");
        QTest::newRow("running-app") << true << false;
        QTest::newRow("closed-app") << false << false;
        QTest::newRow("registered-app") << true << true;
    }
    void appImageRemovalForgetsSelection() {
        QFETCH(bool, runningDuringRemoval);
        QFETCH(bool, registeredChoice);
        QTemporaryDir directory;
        const auto imagePath = directory.filePath("Reset.AppImage");
        QFile image(imagePath);
        QVERIFY(image.open(QIODevice::WriteOnly));
        image.write("fixture, never executed");
        image.close();
        QVERIFY(image.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QQmlApplicationEngine engine;
        AppImageSetupModel model;
        model.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "LauncherUrlWithoutIcon"},
            {Qt::UserRole + 2, "IsWindow"}, {Qt::UserRole + 3, "AppId"}, {Qt::UserRole + 4, "HasLauncher"}});
        const auto config = KSharedConfig::openConfig(directory.filePath("dockrc"), KConfig::SimpleConfig);
        auto backend = std::make_unique<DockBackend>(&engine, false, nullptr, config);
        backend->setSourceModel(&model);
        QVERIFY(backend->pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        const auto placeholder = model.launchers.first();
        auto addTask = [&](const QString &key, const QUrl &url, bool running) {
            auto *item = new QStandardItem(key);
            item->setData(url, Qt::UserRole + 1);
            item->setData(running, Qt::UserRole + 2);
            item->setData(key, Qt::UserRole + 3);
            item->setData(true, Qt::UserRole + 4);
            model.appendRow(item);
            return item;
        };
        auto *source = addTask("placeholder", QUrl(placeholder), false);
        auto candidatePath = directory.filePath("wrong.desktop");
        if (registeredChoice) {
            const auto applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
            QVERIFY(QDir().mkpath(applications));
            candidatePath = applications + "/test-reset-" + QFileInfo(directory.path()).fileName() + ".desktop";
        }
        const QByteArray candidateContents("[Desktop Entry]\nType=Application\nName=Wrong choice\nExec=/bin/true\n");
        QFile candidateFile(candidatePath);
        QVERIFY(candidateFile.open(QIODevice::WriteOnly));
        QCOMPARE(candidateFile.write(candidateContents), candidateContents.size());
        candidateFile.close();
        const auto runningUrl = registeredChoice ? DockBackend::applicationLauncher(QUrl::fromLocalFile(candidatePath)) : QUrl::fromLocalFile(candidatePath);
        auto *candidate = addTask("wrong-choice", runningUrl, true);
        QPixmap wrongIcon(32, 32);
        wrongIcon.fill(Qt::red);
        candidate->setIcon(QIcon(wrongIcon));
        backend->showAppImageSetup(0);
        QVERIFY(backend->confirmAppImageSetup("wrong-choice"));
        const auto stablePins = KConfigGroup(config, "Launchers").readEntry("applications", QStringList());
        QCOMPARE(stablePins.size(), 1);
        if (!runningDuringRemoval) {
            candidate->setData(false, Qt::UserRole + 2);
            backend->reconcileAppImageLaunchers();
            candidate->setData(QUrl(stablePins.first()), Qt::UserRole + 1);
        }
        const auto beforeRemoval = model.launchers;
        model.allowRemoval = false;
        backend->togglePin(1);
        QCOMPARE(model.launchers, beforeRemoval);
        KConfig original(QUrl(placeholder).toLocalFile(), KConfig::SimpleConfig);
        KConfigGroup originalEntry(&original, "Desktop Entry");
        QVERIFY(!originalEntry.readEntry("X-Spacedock-ResolvedLauncher", QString()).isEmpty());
        model.allowRemoval = true;
        backend->togglePin(1);
        QVERIFY(model.launchers.isEmpty());
        QVERIFY(KConfigGroup(config, "Launchers").readEntry("applications", QStringList()).isEmpty());
        original.reparseConfiguration();
        QVERIFY(originalEntry.readEntry("X-Spacedock-ResolvedLauncher", QString()).isEmpty());
        QVERIFY(originalEntry.readEntry("X-Spacedock-RequireSelection", false));
        QVERIFY(!QFile::exists(QUrl(placeholder).toLocalFile() + ".png"));
        QVERIFY(QFile::exists(imagePath));
        if (registeredChoice) {
            QVERIFY(candidateFile.open(QIODevice::ReadOnly));
            QCOMPARE(candidateFile.readAll(), candidateContents); // User-installed launcher is untouched.
            candidateFile.close();
        } else {
            const auto installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + QUrl(stablePins.first()).path());
            KConfig owned(installed, KConfig::SimpleConfig);
            QVERIFY(KConfigGroup(&owned, "Desktop Entry").readEntry("Hidden", false));
        }
        backend->reconcileAppImageLaunchers();
        QVERIFY(model.launchers.isEmpty()); // Running old choice cannot resurrect the pin.
        QVERIFY(backend->pinUrls({QUrl::fromLocalFile(imagePath)}, -1));
        QCOMPARE(model.launchers, QStringList{placeholder});
        source->setData(true, Qt::UserRole + 4);
        QVERIFY(backend->data(backend->index(0, 0), DockBackend::NeedsAppImageSetup).toBool());
        backend->reconcileAppImageLaunchers();
        QCOMPARE(model.launchers, QStringList{placeholder});
        backend.reset();
        backend = std::make_unique<DockBackend>(&engine, false, nullptr, config);
        backend->setSourceModel(&model);
        backend->reconcileAppImageLaunchers();
        QCOMPARE(model.launchers, QStringList{placeholder}); // Requirement survives dock restart.
        const auto rightPath = directory.filePath("right.desktop");
        QFile rightFile(rightPath);
        QVERIFY(rightFile.open(QIODevice::WriteOnly));
        rightFile.write("[Desktop Entry]\nType=Application\nName=Right choice\nExec=/bin/true\n");
        rightFile.close();
        auto *right = addTask("right-choice", QUrl::fromLocalFile(rightPath), true);
        QPixmap rightIcon(32, 32);
        rightIcon.fill(Qt::green);
        right->setIcon(QIcon(rightIcon));
        backend->showAppImageSetup(0);
        QVERIFY(backend->confirmAppImageSetup("right-choice"));
        original.reparseConfiguration();
        QVERIFY(!originalEntry.readEntry("X-Spacedock-RequireSelection", false));
        const auto newPins = KConfigGroup(config, "Launchers").readEntry("applications", QStringList());
        const auto installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + QUrl(newPins.first()).path());
        KConfig corrected(installed, KConfig::SimpleConfig);
        const KConfigGroup correctedEntry(&corrected, "Desktop Entry");
        QVERIFY(!correctedEntry.readEntry("Hidden", false));
        QCOMPARE(correctedEntry.readEntry("StartupWMClass", QString()), "right-choice");
        QCOMPARE(QImage(correctedEntry.readEntry("Icon", QString())).pixelColor(3, 3), QColor(Qt::green));
    }

};
int main(int argc, char **argv) {
    return runDockUiTest<AppImageIntegrationTest>(argc, argv);
}
#include "appimage_integration_test.moc"

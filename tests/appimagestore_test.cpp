#include "appimagestore.h"
#include <KConfig>
#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

class AppImageStoreTest : public QObject {
    Q_OBJECT
    static QByteArray contents(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    static bool write(const QString &path, const QByteArray &bytes, bool executable = false) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
        file.close();
        return !executable || file.setPermissions(file.permissions() | QFile::ExeOwner);
    }
private slots:
    void canonicalLauncherAndRefresh() {
        QTemporaryDir directory;
        const auto image = directory.filePath("Editor 100%.AppImage");
        const auto link = directory.filePath("Link.AppImage");
        QVERIFY(write(image, "inert fixture; never executed", true));
        QVERIFY(QFile::link(image, link));
        const auto url = QUrl::fromLocalFile(image);
        // Validation must not create a launcher, including hover validation.
        const auto desktop = AppImageStore::appImageDesktopPath(image);
        QVERIFY(AppImageStore::isAppImage(url));
        QVERIFY(!QFileInfo::exists(desktop));
        const auto launcher = AppImageStore::appImageLauncher(QUrl::fromLocalFile(link));
        QVERIFY(!launcher.isEmpty());
        QCOMPARE(launcher, AppImageStore::appImageLauncher(url));
        QCOMPARE(AppImageStore::generatedAppImagePath(launcher), image);
        QCOMPARE(AppImageStore::launcherExecutable(launcher), image);
        const auto before = contents(launcher.toLocalFile());
        AppImageStore::refreshAppImageLauncher(launcher);
        QCOMPARE(contents(launcher.toLocalFile()), before);
        QCOMPARE(contents(image), QByteArray("inert fixture; never executed"));
    }

    void forgettingPersistsSelectionAndInvalidatesArtwork() {
        QTemporaryDir directory;
        const auto image = directory.filePath("Artwork.AppImage");
        QVERIFY(write(image, "inert fixture", true));
        const auto original = AppImageStore::appImageLauncher(QUrl::fromLocalFile(image));
        const auto learned = AppImageStore::appImageLauncher(QUrl::fromLocalFile(image), "test.artwork");
        AppImageStore store;
        QPixmap pixels(32, 32);
        pixels.fill(Qt::red);
        const QIcon icon(pixels);
        QVERIFY(store.cacheIcon(original, icon));
        QVERIFY(!store.cacheIcon(original, icon));
        QVERIFY(!store.cachedIcon(original, QSize(32, 32)).isNull());
        store.saveAssociation(original, learned, true);
        QVERIFY(!store.needsSetup(original));
        QVERIFY(store.forgetAssociation(learned, learned));
        QVERIFY(store.needsSetup(original));
        QVERIFY(store.requiresSelection(image));
        QVERIFY(store.cachedIcon(original, QSize(32, 32)).isNull());
        QVERIFY(!store.cacheIcon(original, icon));
        AppImageStore restarted;
        QVERIFY(restarted.requiresSelection(image));
        QVERIFY(restarted.knownAppImageLauncher(image).isEmpty());
        restarted.saveAssociation(original, learned, true);
        QVERIFY(!restarted.requiresSelection(image));
        // The same icon must be saved again after forgetting clears its cache key.
        store.saveAssociation(original, learned, true);
        QVERIFY(store.cacheIcon(original, icon));
        QCOMPARE(contents(image), QByteArray("inert fixture"));
    }

    void forgettingPreservesForeignLaunchers() {
        QTemporaryDir directory;
        const auto image = directory.filePath("Foreign.AppImage");
        QVERIFY(write(image, "inert fixture", true));
        const auto original = AppImageStore::appImageLauncher(QUrl::fromLocalFile(image));
        const auto applications = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
        QVERIFY(QDir().mkpath(applications));
        const auto installed = applications + "/user-editor.desktop";
        const QByteArray foreign("[Desktop Entry]\nType=Application\nName=User Editor\nExec=/bin/true\n");
        QVERIFY(write(installed, foreign));
        const QUrl selected("applications:user-editor.desktop");
        AppImageStore store;
        store.saveAssociation(original, selected, true);
        // Even a file occupying our generated name belongs to the user if its
        // ownership metadata points elsewhere.
        const auto ownedId = QFileInfo(original.toLocalFile()).fileName().replace("appimage-", "spacedock-appimage-");
        const auto collision = applications + '/' + ownedId;
        QVERIFY(write(collision, foreign));
        QVERIFY(store.confirmedAppImageLauncher(image, "test.foreign", "Foreign", {}).isEmpty());
        QVERIFY(store.forgetAssociation(selected, selected));
        QCOMPARE(contents(installed), foreign);
        QCOMPARE(contents(collision), foreign);
        QCOMPARE(contents(image), QByteArray("inert fixture"));
        QVERIFY(store.requiresSelection(image));
        // Store write APIs also reject a user-installed desktop file.
        store.saveAssociation(QUrl::fromLocalFile(installed), original, true);
        store.updatePresentation(QUrl::fromLocalFile(installed), "Changed", {});
        QCOMPARE(contents(installed), foreign);
    }
};

int main(int argc, char **argv) {
    QTemporaryDir xdg;
    qputenv("XDG_CONFIG_HOME", (xdg.path() + "/config").toUtf8());
    qputenv("XDG_CACHE_HOME", (xdg.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (xdg.path() + "/data").toUtf8());
    QGuiApplication app(argc, argv);
    app.setApplicationName("spacedock");
    app.setOrganizationName("spacedock");
    AppImageStoreTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "appimagestore_test.moc"

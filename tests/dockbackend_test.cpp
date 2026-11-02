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
#include <QFile>
#include <QImageWriter>
#include <QBuffer>
#include <QDataStream>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <KConfigGroup>

// Simulate the observed Plasma sync side effect, including hidden launchers.
class LauncherOrderModel : public PinDropModel {
    Q_OBJECT
public:
    int moveCalls = 0;
    int syncCalls = 0;
    Q_INVOKABLE bool move(int from, int to, const QModelIndex &) {
        ++moveCalls;
        launchers.move(from, to);
        return true;
    }
    Q_INVOKABLE void syncLaunchers() {
        ++syncCalls;
        launchers.swapItemsAt(2, 3);
    }
};

class ActivationModel : public QStandardItemModel {
    Q_OBJECT
public:
    QList<QPersistentModelIndex> activated;
    QModelIndex geometryIndex;
    QRect geometry;
    QObject *geometryItem = nullptr;
    Q_INVOKABLE void requestActivate(const QModelIndex &index) { activated.append(index); }
    Q_INVOKABLE void requestPublishDelegateGeometry(const QModelIndex &index, const QRect &rect, QObject *item) {
        geometryIndex = index; geometry = rect; geometryItem = item;
    }
};

class DockBackendTest : public QObject {
    Q_OBJECT
private slots:
    void folderPreferencesPersist() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString configPath = directory.filePath("dockrc");
        QQmlEngine engine;
        {
            DockBackend backend(&engine, true, nullptr, KSharedConfig::openConfig(configPath));
            QCOMPARE(backend.folderPreference("/tmp/one", "view"), QString("grid"));
            backend.setFolderPreference("/tmp/one", "view", "fan");
            backend.setFolderPreference("/tmp/one", "sort", "type");
        }
        KConfig saved(configPath, KConfig::SimpleConfig);
        const auto key = QString::fromLatin1(QCryptographicHash::hash(QByteArray("/tmp/one"), QCryptographicHash::Sha256).toHex());
        QCOMPARE(KConfigGroup(&saved, "FolderPreferences").readEntry(key + "_view", QString()), QString("fan"));
        DockBackend restored(&engine, true, nullptr, KSharedConfig::openConfig(configPath));
        QCOMPARE(restored.folderPreference("/tmp/one", "view"), QString("fan"));
        QCOMPARE(restored.folderPreference("/tmp/one", "sort"), QString("type"));
        QCOMPARE(restored.folderPreference("/tmp/two", "view"), QString("grid"));
    }
    void folderDropsAndContents() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto oldDataHome = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", directory.path().toUtf8());
        const QString folder = directory.filePath("My folder");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(QDir().mkpath(folder + "/Subfolder"));
        QFile file(folder + "/a # file.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QQmlEngine engine;
        DockBackend backend(&engine, false, nullptr, KSharedConfig::openConfig(directory.filePath("dockrc")));
        PinDropModel model;
        backend.setSourceModel(&model);
        const auto url = QUrl::fromLocalFile(folder);
        QVERIFY(backend.canPinUrls({url}));
        QVERIFY(!backend.canPinUrls({QUrl::fromLocalFile(file.fileName())}));
        QVERIFY(backend.pinUrls({url}, -1));
        QCOMPARE(model.launchers.size(), 1);
        QVERIFY(backend.pinUrls({url}, -1));
        QCOMPARE(model.launchers.size(), 1);
        constexpr int launcherRole = Qt::UserRole + 1;
        model.setItemRoleNames({{launcherRole, "LauncherUrlWithoutIcon"}});
        auto *item = new QStandardItem;
        item->setData(QUrl(model.launchers.first()), launcherRole);
        model.appendRow(item);
        QCOMPARE(backend.folderPath(0), folder);
        const auto contents = backend.folderContents(folder);
        QCOMPARE(contents.size(), 2);
        QCOMPARE(contents.at(0).toMap().value("name").toString(), QString("Subfolder"));
        QCOMPARE(contents.at(1).toMap().value("url").toUrl(), QUrl::fromLocalFile(file.fileName()));
        if (oldDataHome.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", oldDataHome);
    }
    void folderThumbnailsUseValidDesktopCache() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = qgetenv("XDG_CACHE_HOME");
        qputenv("XDG_CACHE_HOME", directory.path().toUtf8());
        const QString path = directory.filePath("photo #1.jpg");
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("original");
        original.close();
        const QString uri = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
        const auto hash = QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex();
        QVERIFY(QDir().mkpath(directory.filePath("thumbnails/normal")));
        const auto thumbnail = directory.filePath("thumbnails/normal/" + QString::fromLatin1(hash) + ".png");
        QImage image(32, 32, QImage::Format_ARGB32);
        image.fill(Qt::red);
        // Insert literal PNG text keys: Qt's PNG writer folds keys at colons.
        auto writeThumbnail = [&](const QByteArray &mtime) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "PNG")) return false;
            buffer.close();
            auto chunk = [](const QByteArray &key, const QByteArray &value) {
                const QByteArray data = QByteArray("tEXt") + key + QByteArray(1, '\0') + value;
                quint32 crc = 0xffffffff;
                for (const auto byte : data) {
                    crc ^= quint8(byte);
                    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1u));
                }
                QByteArray encoded;
                QDataStream stream(&encoded, QIODevice::WriteOnly);
                stream << quint32(data.size() - 4);
                stream.writeRawData(data.constData(), data.size());
                stream << (crc ^ 0xffffffffu);
                return encoded;
            };
            png.insert(33, chunk("Thumb::MTime", mtime));
            png.insert(33, chunk("Thumb::Size", QByteArray::number(QFileInfo(path).size())));
            QFile out(thumbnail);
            return out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(png) == png.size();
        };
        QVERIFY(writeThumbnail(QByteArray::number(QFileInfo(path).lastModified().toSecsSinceEpoch())));
        QQmlEngine engine;
        DockBackend backend(&engine, true);
        TaskIcons provider(&backend);
        const auto id = "file/" + QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        const auto valid = provider.requestPixmap(id, nullptr, QSize(64, 64)).toImage();
        QVERIFY(!valid.isNull());
        QCOMPARE(valid.pixelColor(16, 16), QColor(Qt::red));
        FolderThumbnails thumbnails;
        const auto encoded = id.mid(5);
        QCOMPARE(thumbnails.requestImage(encoded, nullptr, QSize(64, 64)).pixelColor(16, 16), QColor(Qt::red));
        // An old timestamp must invalidate the cached thumbnail.
        QVERIFY(writeThumbnail("1"));
        const auto fallback = provider.requestPixmap(id, nullptr, QSize(64, 64)).toImage();
        QVERIFY(fallback.isNull() || fallback.pixelColor(16, 16) != QColor(Qt::red));
        const auto missing = thumbnails.requestImage(encoded, nullptr, QSize(64, 64));
        QCOMPARE(missing.size(), QSize(1, 1));
        QCOMPARE(missing.pixelColor(0, 0).alpha(), 0);
        if (previous.isNull()) qunsetenv("XDG_CACHE_HOME"); else qputenv("XDG_CACHE_HOME", previous);
    }
    void publishesIconGeometry() {
        QTemporaryDir directory;
        QQmlEngine engine;
        DockBackend backend(&engine, false, nullptr, KSharedConfig::openConfig(directory.filePath("dockrc")));
        ActivationModel model;
        model.appendRow(new QStandardItem("app"));
        backend.setSourceModel(&model);
        QQuickWindow window;
        QQuickItem item(window.contentItem());
        item.setPosition(QPointF(20, 30));
        item.setSize(QSizeF(48, 48));
        backend.publishIconGeometry(0, &item);
        QCOMPARE(model.geometryIndex, model.index(0, 0));
        QCOMPARE(model.geometryItem, &item);
        QCOMPARE(model.geometry, QRect(item.mapToGlobal(QPointF()).toPoint(), QSize(48, 48)));
        backend.publishIconGeometry(-1, &item);
        QCOMPARE(model.geometryIndex, model.index(0, 0));
    }
    void groupedWindowActivation() {
        QTemporaryDir directory;
        QQmlEngine engine;
        DockBackend backend(&engine, false, nullptr, KSharedConfig::openConfig(directory.filePath("dockrc")));
        ActivationModel model;
        constexpr int windowRole = Qt::UserRole + 1, activeRole = Qt::UserRole + 2, stackingRole = Qt::UserRole + 3;
        model.setItemRoleNames({{windowRole, "IsWindow"}, {activeRole, "IsActive"}, {stackingRole, "StackingOrder"}});
        auto *group = new QStandardItem("group");
        model.appendRow(group);
        for (int i = 0; i < 3; ++i) {
            auto *child = new QStandardItem(QString::number(i));
            child->setData(true, windowRole);
            child->setData(i == 1, activeRole);
            child->setData(2 - i, stackingRole);
            group->appendRow(child);
        }
        model.appendRow(new QStandardItem("closed"));
        backend.setSourceModel(&model);
        backend.activate(0, false);
        QCOMPARE(model.activated.size(), 1);
        QCOMPARE(model.activated.takeFirst(), QPersistentModelIndex(group->child(2)->index()));
        group->child(1)->setData(false, activeRole);
        group->child(2)->setData(true, activeRole);
        backend.activate(0, false);
        QCOMPARE(model.activated.takeFirst(), QPersistentModelIndex(group->child(0)->index()));
        group->child(2)->setData(false, activeRole);
        group->child(1)->setData(true, activeRole);
        backend.activate(0, true);
        QCOMPARE(model.activated.size(), 3);
        QCOMPARE(model.activated.at(0), QPersistentModelIndex(group->child(2)->index()));
        QCOMPARE(model.activated.at(1), QPersistentModelIndex(group->child(0)->index()));
        QCOMPARE(model.activated.at(2), QPersistentModelIndex(group->child(1)->index()));
        model.activated.clear();
        backend.activate(1, false);
        QCOMPARE(model.activated.size(), 1);
        QCOMPARE(model.activated.first(), QPersistentModelIndex(model.index(1, 0)));
        model.activated.clear();
        model.item(1)->setData(true, windowRole);
        backend.activate(1, true);
        QCOMPARE(model.activated.first(), QPersistentModelIndex(model.index(1, 0)));
    }
    void iconProviderReturnsRequestedPixelsAndCaches() {
        QQmlEngine engine;
        DockBackend backend(&engine, true);
        QStandardItemModel model;
        auto *item = new QStandardItem("Example");
        item->setData(QIcon(":/assets/icons/appimage_closed.png"), Qt::DecorationRole);
        model.appendRow(item);
        backend.setSourceModel(&model);
        TaskIcons provider(&backend);
        QSize reported;
        const auto first = provider.requestPixmap("1/0", &reported, QSize(200, 200));
        QVERIFY(!first.isNull());
        QCOMPARE(first.size(), QSize(200, 200));
        QCOMPARE(first.devicePixelRatio(), 1.0);
        QCOMPARE(reported, first.size());
        const auto gray = provider.requestPixmap("gray/1/0", &reported, QSize(200, 200)).toImage();
        const auto original = first.toImage();
        QVERIFY(!gray.isNull());
        for (int y = 0; y < gray.height(); y += 7) {
            for (int x = 0; x < gray.width(); x += 7) {
                const auto pixel = gray.pixelColor(x, y);
                QCOMPARE(pixel.red(), pixel.green());
                QCOMPARE(pixel.green(), pixel.blue());
                QCOMPARE(pixel.alpha(), original.pixelColor(x, y).alpha());
            }
        }
        const auto second = provider.requestPixmap("1/0", &reported, QSize(200, 200));
        QCOMPARE(second.cacheKey(), first.cacheKey());
        const auto changed = provider.requestPixmap("2/0", &reported, QSize(200, 200));
        QCOMPARE(changed.cacheKey(), first.cacheKey());
        item->setData(QIcon(":/assets/icons/appimage_open.png"), Qt::DecorationRole);
        const auto updated = provider.requestPixmap("3/0", &reported, QSize(200, 200));
        QVERIFY(!updated.isNull());
        QVERIFY(updated.cacheKey() != first.cacheKey());
    }
    void sourceReplacementOwnsConnections() {
        QQmlEngine engine;
        QStandardItemModel oldSource, newSource;
        DockBackend backend(&engine, true);
        backend.setSourceModel(&oldSource);
        QSignalSpy revision(&backend, &DockBackend::revisionChanged);
        QSignalSpy structure(&backend, &DockBackend::taskStructureChanged);
        QSignalSpy about(&backend, &DockBackend::taskStructureAboutToChange);
        backend.setSourceModel(&newSource);
        QCOMPARE(revision.count(), 1);
        QCOMPARE(structure.count(), 1);
        QCOMPARE(about.count(), 1);
        revision.clear(); structure.clear(); about.clear();
        backend.setSourceModel(&newSource);
        QCOMPARE(revision.count(), 0);
        oldSource.appendRow(new QStandardItem("old"));
        oldSource.item(0)->setText("ignored");
        QCOMPARE(revision.count(), 0);
        QCOMPARE(structure.count(), 0);
        QCOMPARE(about.count(), 0);
        newSource.appendRow(new QStandardItem("new"));
        QCOMPARE(revision.count(), 1);
        QCOMPARE(structure.count(), 1);
        QCOMPARE(about.count(), 1);
        revision.clear();
        QSignalSpy data(&backend, &QAbstractItemModel::dataChanged);
        newSource.item(0)->setText("updated");
        QCOMPARE(revision.count(), 1);
        bool frontendRoles = false;
        for (const auto &event : data)
            frontendRoles |= event.at(2).value<QList<int>>().contains(DockBackend::AppKey);
        QVERIFY(frontendRoles);
        newSource.removeRow(0);
        QCOMPARE(structure.count(), 2);
        newSource.clear();
        QCOMPARE(structure.count(), 3);
        backend.setSourceModel(nullptr);
        revision.clear();
        newSource.appendRow(new QStandardItem("detached"));
        QCOMPARE(revision.count(), 0);
    }
    void reorderPreservesEveryOtherTask() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        auto keys = [&] {
            QStringList result;
            for (int row = 0; row < backend.rowCount(); ++row)
                result.append(backend.data(backend.index(row, 0), DockBackend::AppKey).toString());
            return result;
        };
        // Verify the complete permutation, not only the dragged destination.
        // Plasma's live move/sync behavior still needs trace-tasks validation.
        for (const int from : {0, 6, 12}) for (int to = 0; to < 13; ++to) {
            auto expected = keys();
            expected.move(from, to);
            QVERIFY(backend.reorder(from, to));
            QCOMPARE(keys(), expected);
        }
    }
    void pinnedReorderDoesNotResynchronizeUnrelatedLaunchers() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QStringList pins;
        for (int i = 0; i < 6; ++i) {
            const auto path = directory.filePath(QString("pin-%1.desktop").arg(i));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[Desktop Entry]\nType=Application\nName=Pin\nExec=/bin/true\n");
            file.close();
            pins.append(QUrl::fromLocalFile(path).toString());
        }
        LauncherOrderModel model;
        model.setItemRoleNames({{Qt::UserRole + 1, "HasLauncher"},
            {Qt::UserRole + 2, "LauncherUrlWithoutIcon"}});
        const QList<int> visiblePins{0, 2, 3, 4}; // two hidden launcher entries
        for (const int pin : visiblePins) {
            auto *item = new QStandardItem;
            item->setData(true, Qt::UserRole + 1);
            item->setData(QUrl(pins[pin]), Qt::UserRole + 2);
            model.appendRow(item);
        }
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        backend.setSourceModel(&model);
        for (int from = 0; from < 4; ++from) for (int to = 0; to < 4; ++to) {
            model.launchers = pins;
            auto expected = pins;
            expected.move(visiblePins[from], visiblePins[to]);
            QVERIFY(backend.reorder(from, to));
            QCOMPARE(model.launchers, expected);
            QCOMPARE(model.moveCalls, 0);
            QCOMPARE(model.syncCalls, 0);
            if (from != to)
                QCOMPARE(KConfigGroup(KSharedConfig::openConfig("spacedockrc"), "Launchers")
                    .readEntry("applications", QStringList()), expected);
        }
    }
    void plasmaManualOrderApi() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        QAbstractItemModel *plasma = nullptr;
        for (auto *model : backend.findChildren<QAbstractItemModel *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (model != backend.sourceModel() && model->property("launcherList").isValid()) plasma = model;
        }
        QVERIFY(plasma);
        // Verify the installed private API without requiring a writable service
        // cache or launching applications in the offscreen test process.
        QVERIFY(plasma->metaObject()->indexOfMethod("move(int,int,QModelIndex)") >= 0);
        QVERIFY(plasma->metaObject()->indexOfMethod("syncLaunchers()") >= 0);
        bool moved = true;
        QVERIFY(QMetaObject::invokeMethod(plasma, "move", Q_RETURN_ARG(bool, moved),
            Q_ARG(int, -1), Q_ARG(int, 0), Q_ARG(QModelIndex, QModelIndex())));
        QVERIFY(!moved);
        QVERIFY(QMetaObject::invokeMethod(plasma, "syncLaunchers"));
    }
    void desktopActionsUseInstalledKdeAdapter() {
        QTemporaryDir directory;
        const auto path = directory.filePath("context-test.desktop");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Context Test\nExec=/bin/true\nIcon=system-run\nActions=New;Private;\n"
            "[Desktop Action New]\nName=New Window\nExec=/bin/true --new\n"
            "[Desktop Action Private]\nName=New Private Window\nExec=/bin/true --private\n");
        file.close();
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        QStandardItemModel source;
        source.setItemRoleNames({{Qt::UserRole + 1, "LauncherUrlWithoutIcon"}});
        auto *item = new QStandardItem;
        item->setData(QUrl::fromLocalFile(path), Qt::UserRole + 1);
        source.appendRow(item);
        backend.setSourceModel(&source);
        const auto entries = backend.contextEntries(0);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].toMap().value("text").toString(), QString("New Window"));
        QCOMPARE(entries[1].toMap().value("text").toString(), QString("New Private Window"));
        QVERIFY(!backend.triggerContextAction(-1));
        const int token = entries.first().toMap().value("token").toInt();
        backend.contextEntries(0);
        QVERIFY(!backend.triggerContextAction(token));
        const auto refreshed = backend.contextEntries(0);
        source.removeRow(0);
        QVERIFY(!backend.triggerContextAction(refreshed.first().toMap().value("token").toInt()));
    }
    void desktopActionsForInstalledLauncher() {
        const auto dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
        QVERIFY(QDir().mkpath(dir));
        QFile file(dir + "/spacedock-context-test.desktop");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Context Test\nExec=/bin/true\nActions=Private;\n"
                   "[Desktop Action Private]\nName=New Private Window\nExec=/bin/true --private\n");
        file.close();
        QQmlApplicationEngine engine;
        DockBackend backend(&engine);
        QStandardItemModel source;
        source.setItemRoleNames({{Qt::UserRole + 1, "LauncherUrlWithoutIcon"}});
        auto *item = new QStandardItem;
        item->setData(QUrl("applications:spacedock-context-test.desktop"), Qt::UserRole + 1);
        source.appendRow(item);
        backend.setSourceModel(&source);
        const auto entries = backend.contextEntries(0);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().toMap().value("text").toString(), QString("New Private Window"));
    }

};
int main(int argc, char **argv) {
    return runDockUiTest<DockBackendTest>(argc, argv);
}
#include "dockbackend_test.moc"

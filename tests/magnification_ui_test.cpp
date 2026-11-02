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
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFont>
#include <QFontMetricsF>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QFile>
#include <KConfigGroup>

class FolderThumbnailFixture : public QQuickImageProvider {
public:
    FolderThumbnailFixture() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        const auto path = QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding);
        const bool hasPreview = path.endsWith("0.txt");
        QImage image(hasPreview ? QSize(16, 16) : QSize(1, 1), QImage::Format_ARGB32);
        image.fill(hasPreview ? Qt::red : Qt::transparent);
        if (size) *size = image.size();
        return image;
    }
};

// Model the launcher's synchronous row insertion without the KDE service cache.
class InsertingPinDropModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(QStringList launcherList READ launchers WRITE setLaunchers)
public:
    QStringList launchers() const { return m_launchers; }
    void setLaunchers(const QStringList &urls) {
        setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "LauncherUrlWithoutIcon"},
            {Qt::UserRole + 2, "HasLauncher"}});
        for (int i = 0; i < urls.size(); ++i) {
            if (m_launchers.contains(urls[i])) continue;
            auto *item = new QStandardItem(QUrl(urls[i]).fileName());
            item->setData(QUrl(urls[i]), Qt::UserRole + 1);
            item->setData(true, Qt::UserRole + 2);
            m_launchers.insert(i, urls[i]);
            appendRow(item);
        }
        // Plasma may append first and sort in a later event-loop turn.
        QTimer::singleShot(0, this, [this, urls] {
            // A model rebuild can temporarily remove an existing delegate.
            // Its original drop position must survive subsequent insertions.
            if (rowCount() > 1) insertRow(0, takeRow(0));
            for (int target = 0; target < urls.size(); ++target) {
                for (int row = target; row < rowCount(); ++row) {
                    if (item(row)->data(Qt::UserRole + 1).toUrl() != QUrl(urls[target])) continue;
                    if (row != target) insertRow(target, takeRow(row));
                    break;
                }
            }
        });
    }
private:
    QStringList m_launchers;
};

class RemovablePinModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(QStringList launcherList MEMBER launchers)
public:
    QStringList launchers;
    Q_INVOKABLE bool requestRemoveLauncher(const QUrl &url) {
        launchers.removeAll(url.toString());
        for (int row = 0; row < rowCount(); ++row) {
            if (item(row)->data(Qt::UserRole + 1).toUrl() != url) continue;
            if (item(row)->data(Qt::UserRole + 3).toBool())
                item(row)->setData(false, Qt::UserRole + 2);
            else removeRow(row);
            return true;
        }
        return false;
    }
};

class RunningPinModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(QStringList launcherList READ launcherList WRITE setLauncherList)
public:
    QStringList pins;
    QStringList launcherList() const { return pins; }
    void setLauncherList(const QStringList &value) {
        pins = value;
        for (int row = 0; row < rowCount(); ++row)
            item(row)->setData(pins.contains(item(row)->data(Qt::UserRole + 1).toUrl().toString()), Qt::UserRole + 2);
        for (int target = 0; target < pins.size(); ++target)
            for (int row = target; row < rowCount(); ++row)
                if (item(row)->data(Qt::UserRole + 1).toUrl().toString() == pins[target]) {
                    if (row != target) insertRow(target, takeRow(row));
                    break;
                }
    }
};

class MovableTaskModel : public QAbstractListModel {
public:
    QStringList names{"pin0", "pin1", "running0", "running1", "running2"};
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : names.size(); }
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "HasLauncher"}, {Qt::UserRole + 2, "IsWindow"}};
    }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        if (role == Qt::DisplayRole) return names[index.row()];
        if (role == Qt::UserRole + 1) return names[index.row()].startsWith("pin");
        if (role == Qt::UserRole + 2) return true;
        return {};
    }
    void moveTask(int from, int to) {
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        names.move(from, to);
        endMoveRows();
    }
};

class MagnificationUiTest : public QObject {
    Q_OBJECT
private slots:
    void closingUnpinnedTaskFadesAndResizes() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, false);
        QStandardItemModel model;
        model.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "HasLauncher"},
            {Qt::UserRole + 2, "IsWindow"}, {Qt::UserRole + 3, "AppId"}});
        for (const auto &name : {QString("pinned"), QString("running")}) {
            auto *item = new QStandardItem(name);
            item->setData(name == "pinned", Qt::UserRole + 1);
            item->setData(name == "running", Qt::UserRole + 2);
            item->setData(name, Qt::UserRole + 3);
            model.appendRow(item);
        }
        backend.setSourceModel(&model);
        Tuning tuning(":/tuning.json");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *dock = window->findChild<QObject *>("dockContent");
        auto *animation = window->findChild<QObject *>("closeAnimation");
        QVERIFY(dock && animation);
        QTest::qWait(500);
        const auto before = dock->property("shelfWidth").toDouble();
        QVERIFY(model.removeRow(1));
        QVERIFY(animation->property("active").toBool());
        QCOMPARE(animation->property("ghosts").value<QJSValue>().property("length").toInt(), 1);
        QCOMPARE(animation->property("progress").toDouble(), 0.0);
        QVERIFY(qAbs(dock->property("shelfWidth").toDouble() - before) < 0.1);
        QTRY_VERIFY(!animation->property("active").toBool());
        QVERIFY(dock->property("shelfWidth").toDouble() < before);
        delete window;
    }
    void dragRunningAppIntoPinnedGroup() {
        QTemporaryDir directory;
        QQmlApplicationEngine engine;
        auto config = KSharedConfig::openConfig(directory.filePath("dockrc"));
        DockBackend backend(&engine, false, nullptr, config);
        RunningPinModel model;
        model.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "LauncherUrlWithoutIcon"},
            {Qt::UserRole + 2, "HasLauncher"}, {Qt::UserRole + 3, "IsWindow"}, {Qt::UserRole + 4, "AppId"}});
        for (const auto &name : {QString("pinned"), QString("running")}) {
            QFile desktop(directory.filePath(name + ".desktop"));
            QVERIFY(desktop.open(QIODevice::WriteOnly));
            desktop.write("[Desktop Entry]\nType=Application\nName=Example\nExec=true\n");
            desktop.close();
            auto *item = new QStandardItem(name);
            item->setData(QUrl::fromLocalFile(desktop.fileName()), Qt::UserRole + 1);
            item->setData(name == "pinned", Qt::UserRole + 2);
            item->setData(name == "running", Qt::UserRole + 3);
            item->setData(name, Qt::UserRole + 4);
            model.appendRow(item);
        }
        const QString pinned = QUrl::fromLocalFile(directory.filePath("pinned.desktop")).toString();
        const QString running = QUrl::fromLocalFile(directory.filePath("running.desktop")).toString();
        model.pins = {pinned};
        backend.setSourceModel(&model);
        Tuning tuning(":/tuning.json");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto items = taskItems(content);
        auto start = items.at(1)->mapToScene(QPointF(items.at(1)->width()/2, items.at(1)->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, start - QPoint(20, 0), 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        items = taskItems(content);
        // Target the leading insertion boundary, not the icon center (which
        // can be closer to the boundary after it, especially with Trash).
        const auto end = items.first()->mapToScene(QPointF(0, items.first()->height()/2)).toPoint();
        QTest::mouseMove(window, end, 20);
        QTRY_COMPARE(content->property("placementCandidate").toInt(), 0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QTRY_VERIFY(!content->property("dragging").toBool());
        QCOMPARE(model.pins, QStringList({running, pinned}));
        QCOMPARE(KConfigGroup(config, "Launchers").readEntry("applications", QStringList()), model.pins);
        QVERIFY(backend.data(backend.index(0, 0), DockBackend::Running).toBool());
        QVERIFY(backend.data(backend.index(0, 0), DockBackend::Pinned).toBool());
        delete window;
    }
    void dragOffRemovesPinAndKeepsRunningApp() {
        QTemporaryDir directory;
        QQmlApplicationEngine engine;
        auto config = KSharedConfig::openConfig(directory.filePath("dockrc"));
        DockBackend backend(&engine, false, nullptr, config);
        RemovablePinModel model;
        model.setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "LauncherUrlWithoutIcon"},
            {Qt::UserRole + 2, "HasLauncher"}, {Qt::UserRole + 3, "IsWindow"}, {Qt::UserRole + 4, "AppId"}});
        const QString url = "applications:example.desktop";
        model.launchers = {url};
        auto *item = new QStandardItem("Example");
        item->setData(QUrl(url), Qt::UserRole + 1);
        item->setData(true, Qt::UserRole + 2);
        item->setData(true, Qt::UserRole + 3);
        item->setData("example", Qt::UserRole + 4);
        model.appendRow(item);
        backend.setSourceModel(&model);
        Tuning tuning(":/tuning.json");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setHeight(800);
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto *task = taskItems(content).first();
        const QPoint start = task->mapToScene(QPointF(task->width()/2, task->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, start - QPoint(0, 20), 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        QTest::mouseMove(window, start - QPoint(0, 300), 20);
        auto *cue = task->findChild<QQuickItem *>("removalCue");
        QVERIFY(cue);
        QTRY_VERIFY(cue->isVisible());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!content->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start - QPoint(0, 300));
        QCOMPARE(model.launchers.size(), 1);
        task = taskItems(content).first();
        const QPoint restart = task->mapToScene(QPointF(task->width()/2, task->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, restart);
        QTest::mouseMove(window, restart - QPoint(0, 20), 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        QTest::mouseMove(window, restart - QPoint(0, 300), 20);
        QTRY_VERIFY(cue->isVisible());
        QTRY_COMPARE(content->property("gapAmount").toDouble(), 0.0);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, restart - QPoint(0, 300));
        QVERIFY(content->property("settling").toBool());
        QCOMPARE(model.launchers.size(), 1);
        QTest::qWait(120);
        QCOMPARE(content->property("gapAmount").toDouble(), 0.0);
        QTRY_VERIFY(!content->property("dragging").toBool());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(!backend.data(backend.index(0, 0), DockBackend::Pinned).toBool());
        QVERIFY(backend.data(backend.index(0, 0), DockBackend::Running).toBool());
        QCOMPARE(KConfigGroup(config, "Launchers").readEntry("applications", QStringList()), QStringList());
        delete window;
    }
    void dockFitsAndRestoresPreferredSize() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        Tuning tuning(":/tuning.json");
        tuning.setSize(128);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *dock = window->findChild<QObject *>("dockContent");
        QVERIFY(dock);
        auto actualSize = [&] { return dock->property("layout").toMap().value("iconSize").toDouble(); };
        QTRY_VERIFY(actualSize() < 128);
        QVERIFY(dock->property("shelfWidth").toDouble() <= window->width());
        QVERIFY(QMetaObject::invokeMethod(dock, "openSettings"));
        auto *settings = dock->findChild<QQuickWindow *>("dockSettings");
        QVERIFY(settings);
        auto *slider = settings->findChild<QObject *>("dockSizeSlider");
        QVERIFY(slider);
        auto *notice = settings->findChild<QObject *>("magnificationUnavailable");
        QVERIFY(notice);
        QTRY_VERIFY(!dock->property("magnificationFits").toBool());
        QTRY_VERIFY(notice->property("visible").toBool());
        QVERIFY(tuning.magnification().value("enabled").toBool());
        QTRY_VERIFY(qAbs(slider->property("to").toDouble() - actualSize()) < 0.01);
        QTRY_VERIFY(qAbs(slider->property("value").toDouble() - actualSize()) < 0.01);
        const double crowded = actualSize();
        auto *model = qobject_cast<QStandardItemModel *>(backend.sourceModel());
        QVERIFY(model);
        QVERIFY(model->removeRow(model->rowCount() - 1));
        QTRY_VERIFY(actualSize() > crowded);
        QCOMPARE(tuning.size(), 128.0);
        window->setWidth(3000);
        QTRY_COMPARE(actualSize(), 128.0);
        QTRY_VERIFY(dock->property("magnificationFits").toBool());
        QTRY_VERIFY(!notice->property("visible").toBool());
        QTRY_COMPARE(slider->property("to").toDouble(), 128.0);
        QTRY_COMPARE(slider->property("value").toDouble(), 128.0);
        window->setWidth(5000);
        tuning.setMagnificationScale(4);
        window->setHeight(400);
        QTRY_VERIFY(!dock->property("magnificationFits").toBool());
        QTRY_VERIFY(notice->property("visible").toBool());
        window->setHeight(1000);
        QTRY_VERIFY(dock->property("magnificationFits").toBool());
        QTRY_VERIFY(!notice->property("visible").toBool());
        delete engine.rootObjects().first();
    }
    void sizeSliderResizesWhileHeld() {
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        Tuning tuning(":/tuning.json");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *dock = engine.rootObjects().first()->findChild<QObject *>("dockContent");
        QVERIFY(dock);
        qobject_cast<QQuickWindow *>(engine.rootObjects().first())->setWidth(3000);
        QVERIFY(QMetaObject::invokeMethod(dock, "openSettings"));
        auto *settings = dock->findChild<QQuickWindow *>("dockSettings");
        QVERIFY(settings);
        QVERIFY(QTest::qWaitForWindowExposed(settings));
        auto *slider = settings->findChild<QQuickItem *>("dockSizeSlider");
        QVERIFY(slider);
        auto *handle = qvariant_cast<QQuickItem *>(slider->property("handle"));
        QVERIFY(handle);
        auto pointFor = [&](double value) {
            const double x = slider->property("leftPadding").toDouble() + handle->width() / 2
                + (slider->property("availableWidth").toDouble() - handle->width()) * (value - 18) / 110;
            return slider->mapToScene(QPointF(x, slider->height() / 2)).toPoint();
        };
        const auto start = pointFor(48);
        const auto end = slider->mapToScene(QPointF(slider->width() - 8, slider->height() / 2)).toPoint();
        QTest::mousePress(settings, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(settings, pointFor(35), 20);
        QTest::mouseMove(settings, pointFor(49), 20);
        QTRY_COMPARE(tuning.size(), 48.0);
        QTest::mouseMove(settings, pointFor(60), 20);
        QTRY_VERIFY(tuning.size() > 53);
        QTest::mouseMove(settings, end, 20);
        QTRY_VERIFY_WITH_TIMEOUT(tuning.size() > 100, 1000);
        QTest::mouseRelease(settings, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(tuning.size() > 100);
        auto *scaleSlider = settings->findChild<QQuickItem *>("magnificationScaleSlider");
        QVERIFY(scaleSlider);
        const auto scaleEnd = scaleSlider->mapToScene(QPointF(scaleSlider->width() - 8, scaleSlider->height() / 2)).toPoint();
        QTest::mouseClick(settings, Qt::LeftButton, Qt::NoModifier, scaleEnd);
        QTRY_VERIFY(tuning.magnificationScale() > 3.8);
        QCOMPARE(tuning.magnification().value("scaleSamples").toList().first().toDouble(), tuning.magnificationScale());
        delete engine.rootObjects().first();
    }
    void externalApplicationDrops() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto desktop = directory.filePath("drop-test.desktop");
        QFile file(desktop);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Drop Test\nExec=/bin/true\nIcon=system-run\n");
        file.close();
        const auto url = QUrl::fromLocalFile(desktop);
        QCOMPARE(DockBackend::applicationLauncher(url), url);
        QVERIFY(DockBackend::applicationLauncher(QUrl("https://example.com/app.desktop")).isEmpty());
        QVERIFY(DockBackend::applicationLauncher(QUrl::fromLocalFile(directory.path())).isEmpty());
        const auto invalid = directory.filePath("document.desktop");
        QFile document(invalid);
        QVERIFY(document.open(QIODevice::WriteOnly));
        document.write("[Desktop Entry]\nType=Link\nName=Document\nURL=https://example.com\n");
        document.close();
        QQmlApplicationEngine engine;
        const auto config = KSharedConfig::openConfig(directory.filePath("dockrc"));
        KConfigGroup(config, "Launchers").writeEntry("applications", QStringList{});
        DockBackend backend(&engine, false, nullptr, config);
        QVERIFY(backend.isReady());
        QVERIFY(backend.canPinUrls({url}));
        QVERIFY(!backend.canPinUrls({url, QUrl::fromLocalFile(invalid)}));
        QVERIFY(!backend.pinUrls({url, QUrl::fromLocalFile(invalid)}, -1));
        QVERIFY(backend.pinUrls({url, url}, -1));
        QVERIFY(backend.pinUrls({url}, -1));
        const auto secondDesktop = directory.filePath("second.desktop");
        QVERIFY(QFile::copy(desktop, secondDesktop));
        const auto secondUrl = QUrl::fromLocalFile(secondDesktop);
        const auto pins = backend.sourceModel()->property("launcherList").toStringList();
        QCOMPARE(pins.count(url.toString()), 1);
        QCOMPARE(KConfigGroup(config, "Launchers")
            .readEntry("applications", QStringList()), pins);

        InsertingPinDropModel visiblePins;
        visiblePins.setLaunchers(pins);
        backend.setSourceModel(&visiblePins);

        Tuning tuning(":/tuning.json");
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        QMimeData mime;
        mime.setUrls({url});
        QTRY_VERIFY(!taskItems(content).isEmpty());
        auto taskPoint = [&] {
            auto *task = taskItems(content).first();
            return task->mapToScene(QPointF(task->width() / 2, task->height() / 2)).toPoint();
        };
        const QPoint point = taskPoint();
        QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QVERIFY(enter.isAccepted());
        QCOMPARE(enter.dropAction(), Qt::CopyAction);
        QVERIFY(content->property("holdingOpen").toBool());
        auto *dropArea = content->findChild<QQuickItem *>("externalDropController");
        QVERIFY(dropArea);
        QTRY_VERIFY(dropArea->property("slot").toInt() >= 0);
        QTRY_VERIFY(dropArea->property("gapProgress").toDouble() > 0);
        // Accepted native drops need the full surface even outside the shelf.
        QTRY_VERIFY(window->mask().contains(QPoint(1, 1)));
        // During widening, every delegate follows the shared geometry directly.
        // A second x animation would trail the recentering viewport and make
        // the rightmost icon move left before moving out to the right.
        for (auto *task : taskItems(content)) {
            const auto placement = task->property("placement").value<QJSValue>();
            QVERIFY(qAbs(task->x() - placement.property("x").toNumber()) < 0.1);
        }
        QTRY_COMPARE(dropArea->property("gapProgress").toDouble(), 1.0);
        // The window accepts drag tracking over its full surface, but a drop
        // above the row must cancel the addition rather than pin the launcher.
        QDragMoveEvent away(QPoint(1, 1), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &away);
        QTRY_COMPARE(dropArea->property("gapProgress").toDouble(), 0.0);
        QDropEvent cancelled(QPoint(1, 1), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &cancelled);
        QVERIFY(!cancelled.isAccepted());
        QCOMPARE(backend.sourceModel()->property("launcherList").toStringList(), pins);
        QDragEnterEvent afterCancel(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &afterCancel);
        QVERIFY(afterCancel.isAccepted());
        QTRY_COMPARE(dropArea->property("gapProgress").toDouble(), 1.0);
        // Leaving cancels only the preview; re-entry must accept a fresh offer.
        QDragLeaveEvent previewLeave;
        QCoreApplication::sendEvent(window, &previewLeave);
        QTRY_COMPARE(dropArea->property("slot").toInt(), -1);
        QVERIFY(!dropArea->property("acceptedDrag").toBool());
        QTRY_VERIFY(!window->mask().contains(QPoint(1, 1)));
        QCOMPARE(backend.sourceModel()->property("launcherList").toStringList(), pins);
        QDragEnterEvent reenter(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &reenter);
        QVERIFY(reenter.isAccepted());
        QCOMPARE(reenter.dropAction(), Qt::CopyAction);
        QTRY_VERIFY(window->mask().contains(QPoint(1, 1)));
        QTRY_COMPARE(dropArea->property("gapProgress").toDouble(), 1.0);
        QDragMoveEvent motion(point + QPoint(2, 0), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &motion);
        QVERIFY(motion.isAccepted());
        QCOMPARE(motion.dropAction(), Qt::CopyAction);
        QDropEvent drop(point, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), Qt::CopyAction);
        QCOMPARE(backend.sourceModel()->property("launcherList").toStringList(), pins);
        // Plasma's favorites launcher offers Move only. Plain file-manager
        // moves must remain rejected so a successful pin cannot delete a file.
        const QPoint fileMovePoint = taskPoint();
        auto *trash = content->findChild<QQuickItem *>("trashIcon");
        QVERIFY(trash);
        QVERIFY(!trash->contains(trash->mapFromScene(fileMovePoint)));
        QDragEnterEvent fileMove(fileMovePoint, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &fileMove);
        // The surface accepts file drags so they can reach Trash, but a move
        // dropped on the application row must neither pin nor delete the file.
        QVERIFY(fileMove.isAccepted());
        QDropEvent fileMoveDrop(fileMovePoint, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &fileMoveDrop);
        QVERIFY(!fileMoveDrop.isAccepted());
        QVERIFY(QFile::exists(desktop));
        QCOMPARE(backend.sourceModel()->property("launcherList").toStringList(), pins);
        QDragLeaveEvent fileLeave;
        QCoreApplication::sendEvent(window, &fileLeave);
        QMimeData favoriteMime;
        favoriteMime.setData("text/xx-kicker-kastatsfavorite-id", "drop-test.desktop");
        favoriteMime.setUrls({secondUrl});
        QDragEnterEvent launcherMove(point, Qt::MoveAction, &favoriteMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &launcherMove);
        QVERIFY(launcherMove.isAccepted());
        QCOMPARE(launcherMove.dropAction(), Qt::MoveAction);
        QMap<QString, qreal> beforeDrop;
        for (auto *task : taskItems(content))
            beforeDrop.insert(task->property("appKey").toString(), task->mapToScene(QPointF()).x());
        QDropEvent launcherDrop(point, Qt::MoveAction, &favoriteMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &launcherDrop);
        QVERIFY(launcherDrop.isAccepted());
        QCOMPARE(launcherDrop.dropAction(), Qt::MoveAction);
        QVERIFY(backend.sourceModel()->property("launcherList").toStringList().contains(secondUrl.toString()));
        QVERIFY(QFile::exists(secondDesktop));
        QQuickItem *inserted = nullptr;
        for (auto *task : taskItems(content))
            if (task->property("appKey").toString() == "second") inserted = task;
        QVERIFY(inserted);
        QVERIFY(inserted->opacity() < 1);
        QCoreApplication::processEvents(); // Apply the deferred append-then-sort.
        for (auto *task : taskItems(content)) {
            const auto key = task->property("appKey").toString();
            if (!beforeDrop.contains(key)) continue;
            QVERIFY(qAbs(task->mapToScene(QPointF()).x() - beforeDrop.value(key)) < 0.1);
            QCOMPARE(task->opacity(), 1.0);
        }
        QTRY_COMPARE(dropArea->property("slot").toInt(), -1);
        QVERIFY(!dropArea->property("committing").toBool());
        for (auto *task : taskItems(content)) QCOMPARE(task->opacity(), 1.0);
        // A deterministic task row verifies insertion independently of the
        // desktop service cache (which is unavailable in the sandbox).
        PinDropModel ordered;
        ordered.setItemRoleNames({{Qt::UserRole + 1, "LauncherUrlWithoutIcon"}});
        auto *first = new QStandardItem;
        first->setData(url, Qt::UserRole + 1);
        ordered.appendRow(first);
        ordered.setProperty("launcherList", QStringList{url.toString()});
        DockBackend insertion(&engine);
        insertion.setSourceModel(&ordered);
        QVERIFY(insertion.pinUrls({secondUrl}, 0));
        QCOMPARE(ordered.property("launcherList").toStringList(),
            (QStringList{secondUrl.toString(), url.toString()}));

    }
    void iconPickerDockClickRaisesChooser() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *content = window->findChild<QQuickItem *>("dockContent");
        auto *editor = window->findChild<QQuickWindow *>("applicationSettings");
        QVERIFY(content);
        QVERIFY(editor);
        editor->show();
        auto *picker = editor->findChild<QObject *>("systemIconPicker");
        QVERIFY(picker);
        QVERIFY(QMetaObject::invokeMethod(picker, "open"));
        QTRY_VERIFY(picker->property("visible").toBool());
        auto *shield = content->findChild<QQuickItem *>("iconPickerDockShield");
        auto *editorShield = editor->findChild<QQuickItem *>("iconPickerEditorShield");
        QVERIFY(shield && shield->isVisible());
        QVERIFY(editorShield && editorShield->isVisible());
        QVERIFY(backend.focusIconPicker(picker->property("title").toString()));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          QPoint(window->width() / 2, window->height() - 40));
        QVERIFY(picker->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(picker, "close"));
        QTRY_VERIFY(!picker->property("visible").toBool());
        QVERIFY(!shield->isVisible());
        QVERIFY(!editorShield->isVisible());
        editor->close();
    }
    void trashConfirmationIsRecreatedAfterRejection() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
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
        QVERIFY(content);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTemporaryDir fakeTools;
        QVERIFY(fakeTools.isValid());
        const QByteArray oldPath = qgetenv("PATH");
        qputenv("PATH", fakeTools.path().toUtf8());
        QFile fakeTrash(fakeTools.filePath("ktrash6"));
        QVERIFY(fakeTrash.open(QIODevice::WriteOnly));
        fakeTrash.write("#!/bin/sh\n: > ");
        fakeTrash.write(QFile::encodeName(fakeTools.filePath("emptied")));
        fakeTrash.write("\n");
        fakeTrash.close();
        QVERIFY(fakeTrash.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        for (int i = 0; i < 5; ++i) {
            content->setProperty("trashConfirmationPending", true);
            emit backend.trash()->confirmationReady(2, QStringLiteral("12 bytes"));
            QTRY_VERIFY(backend.trash()->confirmationVisible());
            QDialog *dialog = nullptr;
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == QStringLiteral("emptyTrashDialog")) dialog = qobject_cast<QDialog *>(widget);
            QVERIFY(dialog);
            QTRY_VERIFY(dialog->isVisible());
            auto *shield = content->findChild<QQuickItem *>("trashConfirmationShield");
            QVERIFY(shield && shield->isVisible());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              QPoint(window->width() / 2, window->height() - 40));
            QVERIFY(dialog->isVisible());
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            auto *noButton = buttons->button(QDialogButtonBox::No);
            QVERIFY(noButton && noButton->isDefault());
            if (i == 0) QTest::keyClick(dialog, Qt::Key_Return);
            else if (i == 1) dialog->close();
            else if (i == 2) QTest::keyClick(dialog, Qt::Key_Escape);
            else if (i == 3) QTest::mouseClick(noButton, Qt::LeftButton);
            else {
                QVERIFY(!QFile::exists(fakeTools.filePath("emptied")));
                QTest::mouseClick(buttons->button(QDialogButtonBox::Yes), Qt::LeftButton);
            }
            QTRY_VERIFY(!backend.trash()->confirmationVisible());
            QVERIFY(!content->property("trashConfirmationPending").toBool());
            QVERIFY(!shield->isVisible());
        }
        QTRY_VERIFY(QFile::exists(fakeTools.filePath("emptied")));
        qputenv("PATH", oldPath);
    }
    void outsideClickDismissesMenu() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
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
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        const QPoint center(window->width() / 2, window->height() - 40);
        const auto surfaceSize = window->size();
        window->setPosition(0, 400);
        for (const auto button : {Qt::LeftButton, Qt::RightButton}) {
            QTest::mouseMove(window, center);
            QTRY_VERIFY(content->property("amount").toDouble() > 0.99);
            QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, center);
            QTRY_COMPARE(content->property("openMenus").toInt(), 1);
            auto *menu = taskItems(content)[6]->findChild<QObject *>("taskMenu");
            QVERIFY(menu);
            QVERIFY(menu->property("showPointer").toBool());
            auto *anchor = qvariant_cast<QQuickItem *>(menu->property("anchor"));
            QVERIFY(anchor);
            QCOMPARE(anchor->objectName(), QString("dockMenuAnchor"));
            QCOMPARE(anchor->width(), taskItems(content)[6]->width());
            QCOMPARE(anchor->height(), taskItems(content)[6]->height());
            auto *popupContent = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
            QVERIFY(popupContent && popupContent->window());
            QQuickWindow *grabWindow = popupContent->window();
            if (button == Qt::LeftButton) {
                // Exercise app-specific rows without launching from sample mode.
                menu->setProperty("appEntries", QVariantList{
                    QVariantMap{{"text", "New Window"}, {"section", "actions"}, {"token", 1}},
                    QVariantMap{{"text", "New Private Window"}, {"section", "actions"}, {"token", 2}},
                    QVariantMap{{"text", "example.txt"}, {"section", "recent"}, {"token", 3}}});
                QCoreApplication::processEvents();
                QQmlExpression firstItem(engine.rootContext(), menu, "itemAt(0)");
                auto *item = qvariant_cast<QObject *>(firstItem.evaluate());
                QVERIFY2(!firstItem.hasError(), qPrintable(firstItem.error().toString()));
                QVERIFY(item);
                QCOMPARE(item->property("text").toString(), QString("New Window"));
                item->setProperty("highlighted", true);
                const auto capture = qEnvironmentVariable("SPACEDOCK_MENU_CAPTURE");
                if (!capture.isEmpty()) {
                    auto *menuContent = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
                    QVERIFY(menuContent && menuContent->window());
                    QTest::qWait(80);
                    QVERIFY(menuContent->window()->grabWindow().save(capture));
                }
                auto *recent = menu->findChild<QObject *>("dockRecentMenu");
                QVERIFY(recent);
                QVERIFY(QMetaObject::invokeMethod(recent, "open"));
                QTRY_VERIFY(recent->property("visible").toBool());
                QCOMPARE(content->property("openMenus").toInt(), 1);
                auto *recentContent = qvariant_cast<QQuickItem *>(recent->property("contentItem"));
                QVERIFY(recentContent && recentContent->window());
                grabWindow = recentContent->window();
            }

            if (button == Qt::RightButton) content->setProperty("autoHideEnabled", true);
            // A native popup grab receives desktop clicks in popup-local
            // coordinates, including positions outside its own rectangle.
            const auto outsideGlobal = window->mapToGlobal(QPoint(5, 5));
            QCursor::setPos(outsideGlobal);
            const auto outside = grabWindow->mapFromGlobal(outsideGlobal);
            QTest::mouseMove(grabWindow, outside);
            QTest::mouseClick(grabWindow, button, Qt::NoModifier, outside);
            QTRY_COMPARE_WITH_TIMEOUT(content->property("openMenus").toInt(), 0, 1000);
            QTRY_VERIFY2_WITH_TIMEOUT(content->property("amount").toDouble() < 0.01,
                qPrintable(QString("inside=%1 menus=%2 cursor=%3,%4").arg(content->property("pointerInside").toBool())
                    .arg(content->property("openMenus").toInt()).arg(backend.cursorPosition(window).x()).arg(backend.cursorPosition(window).y())), 1000);
            QVERIFY(!content->property("holdingOpen").toBool());
            QCOMPARE(window->size(), surfaceSize);
            if (button == Qt::RightButton) QTRY_VERIFY(content->property("hideRequested").toBool());
        }
    }
    void menuDismissalEasesPointerReconciliation() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        content->setProperty("autoHideEnabled", false);
        content->setProperty("hideRequested", false);
        const qreal target = content->width() / 2;
        QCursor::setPos(window->mapToGlobal(QPoint(qRound(target), qRound(content->height() - 5))));
        content->setProperty("pointerX", target - 50);
        content->setProperty("pointerInside", true);
        QVERIFY(QMetaObject::invokeMethod(content, "acquireMenuHold"));
        content->setProperty("amount", 1.0);
        content->setProperty("openMenus", 0);
        content->setProperty("menuPointerReconcilePending", true);
        const QVariant position = QPointF(target, content->height() - 5);
        QVERIFY(QMetaObject::invokeMethod(content, "observePointer", Q_ARG(QVariant, position), Q_ARG(QVariant, true)));
        QCOMPARE(content->property("pointerX").toDouble(), target - 50);
        content->setProperty("menuPointerReconcilePending", false);
        // A hover event delivered after the popup closes must not snap to the target.
        QVERIFY(QMetaObject::invokeMethod(content, "observePointer", Q_ARG(QVariant, position), Q_ARG(QVariant, false)));
        QCOMPARE(content->property("pointerX").toDouble(), target - 50);
        QTest::qWait(250);
        QVERIFY(qAbs(content->property("pointerX").toDouble() - target) < 0.1);
    }
    void folderDividerTracksScroll() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnailFixture);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setHeight(900);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto *task = taskItems(content)[0];
        auto *artwork = task->findChild<QQuickItem *>("taskArtwork");
        QVERIFY(artwork);
        auto *view = task->findChild<QObject *>("folderView");
        QVERIFY(view);
        QVERIFY(QMetaObject::invokeMethod(view, "openForIcon"));
        QVERIFY(view->property("openingProgress").toReal() < 1.0);
        QTRY_VERIFY(view->property("openingProgress").toReal() > 0.99);
        QTRY_VERIFY(qAbs(artwork->opacity() - 0.8) < 0.01);
        QTRY_VERIFY(artwork->property("grayscaleProgress").toReal() > 0.99);
        QTemporaryDir profileFolder;
        QVariantList entries;
        for (int i = 0; i < 80; ++i) {
            const QString path = profileFolder.filePath(QString::number(i) + ".txt");
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("folder profile");
            file.close();
            entries.append(QVariantMap{{"name", QString::number(i)}, {"icon", QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))}, {"url", QUrl::fromLocalFile(path)}});
        }
        QVERIFY(view->setProperty("entries", entries));
        auto *grid = view->findChild<QQuickItem *>("folderGrid");
        auto *divider = view->findChild<QQuickItem *>("folderScrollDivider");
        QVERIFY(grid);
        QVERIFY(divider);
        QQmlExpression firstFolderItem(engine.rootContext(), grid, "itemAtIndex(0)");
        QTRY_VERIFY(firstFolderItem.evaluate().value<QObject *>());
        auto *firstFolderEntry = firstFolderItem.evaluate().value<QObject *>();
        auto *thumbnail = firstFolderEntry->findChild<QQuickItem *>("folderThumbnail");
        auto *fileIcon = firstFolderEntry->findChild<QQuickItem *>("folderFileIcon");
        QVERIFY(thumbnail && fileIcon);
        QTRY_COMPARE(thumbnail->property("status").toInt(), 1); // Image.Ready
        QVERIFY(thumbnail->isVisible());
        QVERIFY(!fileIcon->isVisible());
        QQmlExpression secondFolderItem(engine.rootContext(), grid, "itemAtIndex(1)");
        QTRY_VERIFY(secondFolderItem.evaluate().value<QObject *>());
        auto *secondFolderEntry = secondFolderItem.evaluate().value<QObject *>();
        auto *fallbackThumbnail = secondFolderEntry->findChild<QQuickItem *>("folderThumbnail");
        auto *fallbackIcon = secondFolderEntry->findChild<QQuickItem *>("folderFileIcon");
        QVERIFY(fallbackThumbnail && fallbackIcon);
        QTRY_COMPARE(fallbackThumbnail->property("status").toInt(), 1);
        QVERIFY(!fallbackThumbnail->isVisible());
        QVERIFY(fallbackIcon->isVisible());
        QTRY_VERIFY(grid->property("contentHeight").toReal() > grid->height());
        QVERIFY(!divider->isVisible());
        auto *popup = grid->window();
        QVERIFY(popup);
        QCOMPARE(popup, window);
        QVERIFY(!view->property("nativeWindow").toBool());
        QTRY_VERIFY(window->mask().contains(QPoint(5, 5)));
        const QPointF position = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
        QWheelEvent wheel(position, popup->mapToGlobal(position.toPoint()), QPoint(), QPoint(0, -120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(popup, &wheel);
        QTRY_VERIFY(grid->property("contentY").toReal() > 1);
        QTRY_VERIFY(divider->isVisible());
        QTest::qWait(220);
        QWheelEvent reverse(position, popup->mapToGlobal(position.toPoint()), QPoint(), QPoint(0, 120),
            Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(popup, &reverse);
        QTRY_VERIFY(!divider->isVisible());
        QCOMPARE(grid->x(), qreal(25));
        QTest::qWait(550);
        QVERIFY(QMetaObject::invokeMethod(grid, "positionViewAtEnd"));
        QQmlExpression lastItem(engine.rootContext(), grid, "itemAtIndex(80)");
        QTRY_VERIFY(lastItem.evaluate().value<QObject *>());
        QCOMPARE(lastItem.evaluate().value<QObject *>()->objectName(), QString("folderOpenInFileManager"));
        auto *folderContent = view->findChild<QQuickItem *>("folderContent");
        QVERIFY(folderContent);
        const int closeDuration = view->property("closeDuration").toInt();
        QCOMPARE(view->property("openDuration").toInt(), qRound(closeDuration * 0.7));
        QSignalSpy closed(view, SIGNAL(closed()));
        auto *folderPanel = qvariant_cast<QObject *>(view->property("background"));
        QVERIFY(folderPanel);
        QSignalSpy panelPainted(folderPanel, SIGNAL(painted()));
        QVERIFY(panelPainted.isValid());
        QElapsedTimer closeFramesClock;
        QList<qint64> closeFrames;
        const bool traceFolderFrames = qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_FOLDER_FRAMES");
        auto frameConnection = connect(popup, &QQuickWindow::frameSwapped, view, [&] {
            if (traceFolderFrames && closeFramesClock.isValid()) closeFrames.append(closeFramesClock.elapsed());
        });
        auto *wheelAnimation = view->findChild<QObject *>("folderWheelAnimation");
        QVERIFY(wheelAnimation);
        QCoreApplication::sendEvent(popup, &reverse);
        QTRY_VERIFY(wheelAnimation->property("running").toBool());
        if (traceFolderFrames) closeFramesClock.start();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QVERIFY(!wheelAnimation->property("running").toBool());
        QTest::qWait(closeDuration / 3);
        QVERIFY(view->property("closingProgress").toReal() < 0.99);
        QVERIFY(view->property("closingProgress").toReal() > 0.08);
        auto *panelItem = qobject_cast<QQuickItem *>(folderPanel);
        QVERIFY(panelItem);
        const QImage closeFrame = popup->grabWindow();
        QVERIFY(!closeFrame.isNull());
        const QPointF panelPoint = panelItem->mapToScene(QPointF(20, 90));
        const QPoint pixel = (panelPoint * closeFrame.devicePixelRatio()).toPoint();
        QVERIFY(closeFrame.rect().contains(pixel));
        const QColor panelPixel = closeFrame.pixelColor(pixel);
        QVERIFY2(panelPixel.alpha() >= 150 && panelPixel.red() < 25,
            "Folder panel became transparent while closing");
        QCOMPARE(closed.count(), 0);
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(content->property("activeFolderView").isNull());
        QTRY_VERIFY(!window->mask().contains(QPoint(5, 5)));
        QCOMPARE(panelPainted.count(), 0);
        disconnect(frameConnection);
        if (traceFolderFrames) qInfo() << "folder-close frame times ms" << closeFrames << "panel paints" << panelPainted.count();
        QTRY_VERIFY(qAbs(artwork->opacity() - 1.0) < 0.01);
        QTRY_VERIFY(artwork->property("grayscaleProgress").toReal() < 0.01);
        QVERIFY(QMetaObject::invokeMethod(view, "openForIcon"));
        QVERIFY(view->property("openingProgress").toReal() < 1.0);
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_COMPARE(view->property("openingProgress").toReal(), qreal(1));
        QVERIFY(content->property("activeFolderView").isNull());
    }
    void openMenuFreezesIconLayout() {
        // Wayland places a menu once, when it opens. Icons moving afterwards
        // (here: a magnification animation still in flight) leave the menu
        // stranded while its arrow follows the icon.
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
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
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto *icon = taskItems(content)[4];
        const auto center = icon->mapToScene(QPointF(icon->width() / 2, icon->height() / 2)).toPoint();
        QCursor::setPos(window->mapToGlobal(center));
        QTest::mouseMove(window, center);
        QTRY_VERIFY(content->property("amount").toDouble() > 0.05);
        QVERIFY(content->property("amount").toDouble() < 0.9);
        auto *menu = icon->findChild<QObject *>("taskMenu");
        QVERIFY(menu);
        QVERIFY(QMetaObject::invokeMethod(menu, "openForIcon"));
        QTRY_COMPARE(content->property("openMenus").toInt(), 1);
        auto *anchor = qvariant_cast<QQuickItem *>(menu->property("anchor"));
        QVERIFY(anchor);
        const auto atOpen = anchor->mapRectToScene(QRectF(QPointF(), anchor->size()));
        QTest::qWait(400);
        QCOMPARE(anchor->mapRectToScene(QRectF(QPointF(), anchor->size())), atOpen);
        // Closing resumes the interrupted animation.
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(content->property("openMenus").toInt(), 0);
        QTRY_VERIFY(content->property("amount").toDouble() > 0.99);
    }
    void menusAnchorToLeftMiddleAndRightArtwork() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        QVERIFY(backend.sourceModel()->removeRows(9, 4));
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
        // Leave room for the complete magnification profile.
        window->resize(720, window->height());
        window->setPosition(100, 400);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        for (const int row : {0, 4, 8}) {
            auto *icon = taskItems(content)[row];
            const auto center = icon->mapToScene(QPointF(icon->width() / 2, icon->height() / 2)).toPoint();
            QCursor::setPos(window->mapToGlobal(center));
            QTest::mouseMove(window, center);
            QTRY_VERIFY(content->property("amount").toDouble() > 0.99);
            QVERIFY(icon->width() > 48);
            auto *menu = icon->findChild<QObject *>("taskMenu");
            QVERIFY(menu);
            QVERIFY(QMetaObject::invokeMethod(menu, "openForIcon"));
            QTRY_COMPARE(content->property("openMenus").toInt(), 1);
            auto *anchor = qvariant_cast<QQuickItem *>(menu->property("anchor"));
            QVERIFY(anchor);
            QCOMPARE(anchor->objectName(), QString("dockMenuAnchor"));
            const auto artwork = icon->mapToItem(content, QRectF(0, 0, icon->width(), icon->height()));
            QCOMPARE(anchor->position(), artwork.topLeft());
            QCOMPARE(anchor->size(), artwork.size());
            auto *menuContent = qvariant_cast<QQuickItem *>(menu->property("contentItem"));
            QVERIFY(menuContent && menuContent->window());
            auto *popup = menuContent->window();
            QVERIFY(popup != window);
            QTRY_VERIFY(popup->isVisible());
            // Check the actual popup window as well as QML-local coordinates.
            auto *panel = qvariant_cast<QQuickItem *>(menu->property("background"));
            QVERIFY(panel);
            // Custom shadows must fit inside the real popup surface. Wayland
            // ignores negative insets rather than expanding the native window.
            const auto panelBounds = panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height()));
            QVERIFY(QRectF(0, 0, popup->width(), popup->height()).contains(panelBounds));
            const auto tip = panel->mapToGlobal(QPointF(menu->property("pointerTipX").toDouble(),
                panel->height() - menu->property("shadowMargin").toDouble() - 1));
            const auto iconTop = icon->mapToGlobal(QPointF(icon->width() / 2, 0));
            QVERIFY2(qAbs(tip.x() - iconTop.x()) <= 1.5,
                qPrintable(QString("row=%1 tip=%2 icon=%3 popup=%4 menu=%5 pointer=%6 anchor=%7")
                    .arg(row).arg(tip.x()).arg(iconTop.x()).arg(popup->x()).arg(menu->property("x").toDouble())
                    .arg(menu->property("pointerX").toDouble()).arg(anchor->x())));
            QVERIFY(qAbs(tip.y() - iconTop.y() + 6) <= 1.5);
            // Wayland ignores Menu.x and anchors the native popup's left edge
            // to the menu's parent item, so that item must be the menu's
            // intended footprint: as wide as the menu, with the pointer tip
            // pointerTipInset from the left border (native left-side arrow).
            auto *placement = qvariant_cast<QQuickItem *>(menu->property("parent"));
            QVERIFY(placement && placement != anchor);
            QCOMPARE(placement->width(), menu->property("width").toDouble());
            if (row == 4)
                QVERIFY(qAbs(placement->mapToGlobal(QPointF(menu->property("shadowMargin").toDouble() + 1
                    + menu->property("pointerTipInset").toDouble(), 0)).x() - iconTop.x()) <= 1);
            if (row == 4) {
                // Simulate constrained popup placement putting the target near
                // the left corner. Check the painted tip, not just its input.
                // Just inside the rounded corner: border + triangle half-width + 4.
                const auto nearCornerX = menu->property("shadowMargin").toDouble() + 1
                    + menu->property("pointerHalfWidth").toDouble() + 4;
                const auto shift = menu->property("pointerX").toDouble() - nearCornerX;
                // Popup.x can read back stale while a window popup is open, so
                // derive the current offset from the real popup window.
                const auto currentX = popup->x() - placement->mapToGlobal(QPointF(0, 0)).x();
                menu->setProperty("x", currentX + shift);
                QTRY_VERIFY2(qAbs(panel->mapToGlobal(QPointF(
                    menu->property("pointerTipX").toDouble(), 0)).x() - iconTop.x()) <= 1.5,
                    qPrintable(QString("tip=%1 icon=%2 pointer=%3 target=%4 popup=%5 width=%6")
                        .arg(panel->mapToGlobal(QPointF(menu->property("pointerTipX").toDouble(), 0)).x())
                        .arg(iconTop.x()).arg(menu->property("pointerX").toDouble()).arg(nearCornerX)
                        .arg(popup->x()).arg(popup->width())));
            }
            QCursor::setPos(window->mapToGlobal(QPoint(5, 5)));
            QVERIFY(QMetaObject::invokeMethod(menu, "close"));
            QTest::mouseMove(window, QPoint(5, 5));
            QTRY_COMPARE(content->property("openMenus").toInt(), 0);
            QTRY_VERIFY(content->property("amount").toDouble() < 0.01);
        }
    }
    void removingTaskWithOpenMenuReleasesMagnification() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        const QPoint center(window->width() / 2, window->height() - 40);
        QTest::mouseMove(window, center);
        QTRY_VERIFY(content->property("amount").toDouble() > 0.99);
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, center);
        QTRY_COMPARE(content->property("openMenus").toInt(), 1);
        QTest::mouseMove(window, QPoint(5, 5));
        // Unpinning a stopped app removes the task while its menu is still
        // open. Reproduce that lifecycle without mutating the user's pins.
        QVERIFY(backend.sourceModel()->removeRow(6));
        QTRY_COMPARE(taskItems(content).size(), 12);
        QTRY_COMPARE_WITH_TIMEOUT(content->property("openMenus").toInt(), 0, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(content->property("amount").toDouble() < 0.01, 1000);
        QVERIFY(!content->property("holdingOpen").toBool());
        // Hover and departure must continue working after the removal.
        QTest::mouseMove(window, center);
        QTRY_VERIFY(content->property("amount").toDouble() > 0.99);
        QTest::mouseMove(window, QPoint(5, 5));
        QTRY_VERIFY(content->property("amount").toDouble() < 0.01);
    }
    void separatorAfterRunningRowsMove() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        MovableTaskModel model;
        DockBackend backend(&engine, true);
        backend.setSourceModel(&model);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        auto *presentation = window->findChild<QQuickItem *>("dockPresentation");
        auto *separator = window->findChild<QQuickItem *>("taskGroupSeparator");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        for (const auto &move : {std::pair{2, 4}, std::pair{4, 2}, std::pair{3, 2}}) {
            model.moveTask(move.first, move.second);
            QCoreApplication::processEvents();
            auto items = taskItems(content);
            std::sort(items.begin(), items.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->property("index").toInt() < b->property("index").toInt();
            });
            QCOMPARE(items.size(), 5);
            const auto left = items[1]->mapToItem(presentation, QPointF(items[1]->width(), 0)).x();
            const auto right = items[2]->mapToItem(presentation, QPointF()).x();
            QVERIFY2(right - left > 10, "The group gap must precede the first running task");
            QVERIFY(qAbs(separator->x() + separator->width() / 2 - (left + right) / 2) < 0.1);
        }
    }
    void reorderTargetsVisibleCompactBoundaries() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        auto *presentation = window->findChild<QQuickItem *>("dockPresentation");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto *separator = window->findChild<QQuickItem *>("taskGroupSeparator");
        QVERIFY(separator);
        QVERIFY(!separator->isVisible());
        auto *samples = qobject_cast<QStandardItemModel *>(backend.sourceModel());
        QVERIFY(samples);
        const auto roleNames = samples->roleNames();
        const int pinnedRole = roleNames.key("HasLauncher");
        samples->item(12)->setData(false, pinnedRole);
        QTRY_VERIFY(separator->isVisible());
        const auto row = taskItems(content);
        for (const double amount : {0.0, 0.5, 1.0}) {
            for (const double focus : {0.25, 0.5, 0.75}) {
                content->setProperty("pointerX", window->width() * focus);
                content->setProperty("amount", amount);
                QCoreApplication::processEvents();
                const auto a = row[11]->mapToItem(presentation, QPointF(row[11]->width(), 0)).x();
                const auto b = row[12]->mapToItem(presentation, QPointF()).x();
                QVERIFY(b - a > 10);
                QVERIFY(qAbs(separator->x() + separator->width() / 2 - (a + b) / 2) < 0.1);
            }
        }
        // Moving either boundary icon must not pull the fence along with the
        // floating delegate; it belongs to the group boundary in the row.
        for (const int from : {11, 12}) {
            content->setProperty("gapSlot", from);
            content->setProperty("dragSize", 48);
            content->setProperty("dragFrom", from);
            QCoreApplication::processEvents();
            const auto fenceX = separator->x();
            content->setProperty("dragCenter", 100.0);
            QCoreApplication::processEvents();
            QVERIFY(qAbs(separator->x() - fenceX) < 0.1);
            content->setProperty("dragCenter", 900.0);
            QCoreApplication::processEvents();
            QVERIFY(qAbs(separator->x() - fenceX) < 0.1);
            content->setProperty("dragFrom", -1);
            QCoreApplication::processEvents();
        }
        samples->item(12)->setData(true, pinnedRole);
        QTRY_VERIFY(!separator->isVisible());
        // Sorting emits a layout change and reuses delegates, as the live task
        // model can do when two running applications exchange positions.
        samples->item(11)->setData(false, pinnedRole);
        samples->item(12)->setData(false, pinnedRole);
        for (int i = 0; i < samples->rowCount(); ++i)
            samples->item(i)->setText(QString("%1").arg(i, 2, 10, QLatin1Char('0')));
        QTRY_VERIFY(separator->isVisible());
        samples->item(11)->setText("12");
        samples->item(12)->setText("11");
        samples->sort(0);
        QCoreApplication::processEvents();
        auto reordered = taskItems(content);
        std::sort(reordered.begin(), reordered.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->property("index").toInt() < b->property("index").toInt();
        });
        const auto leftEdge = reordered[10]->mapToItem(presentation, QPointF(reordered[10]->width(), 0)).x();
        const auto rightEdge = reordered[11]->mapToItem(presentation, QPointF()).x();
        QVERIFY(qAbs(separator->x() + separator->width() / 2 - (leftEdge + rightEdge) / 2) < 0.1);
        QCOMPARE(separator->opacity(), 0.8);
        auto *taskViewport = window->findChild<QQuickItem *>("taskViewport");
        QVERIFY(taskViewport);
        QVERIFY(separator->z() < taskViewport->z());
        samples->item(11)->setData(true, pinnedRole);
        samples->item(12)->setData(true, pinnedRole);
        QTRY_VERIFY(!separator->isVisible());
        for (const int from : {0, 6, 12}) for (const double amount : {0.0, 1.0}) {
            content->setProperty("dragFrom", from);
            content->setProperty("gapSlot", from);
            content->setProperty("dragSize", 48);
            content->setProperty("grabX", 24);
            content->setProperty("pickupProgress", 1);
            content->setProperty("pointerX", window->width() * 0.7);
            content->setProperty("amount", amount);
            QTest::qWait(260);
            auto items = taskItems(content);
            items.removeAt(from);
            for (int slot = 0; slot <= items.size(); ++slot) {
                const auto left = slot > 0 ? items[slot - 1]->mapToItem(presentation,
                    QPointF(items[slot - 1]->width(), 0)).x() : items.first()->mapToItem(presentation, QPointF()).x();
                const auto right = slot < items.size() ? items[slot]->mapToItem(presentation, QPointF()).x() : left;
                content->setProperty("placementCandidate", -1);
                content->setProperty("dragCenter", (left + right) / 2);
                content->setProperty("dragTop", content->property("iconBottom").toDouble() - 48);
                QVERIFY(QMetaObject::invokeMethod(content, "updateDragTarget"));
                QCOMPARE(content->property("placementCandidate").toInt(), slot);
            }
            QVERIFY(QMetaObject::invokeMethod(content, "cancelReorder"));
        }
    }
    void dragReordersWithoutLaunching() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        auto items = taskItems(content);
        QCOMPARE(items.size(), 13);
        auto *first = items.first();
        const auto original = backend.data(backend.index(0, 0), DockBackend::AppKey);
        const auto start = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        QTest::mouseMove(window, start);
        QTest::qWait(180);
        const auto press = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, press);
        QTest::mouseMove(window, press + QPoint(30, 0), 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        items = taskItems(content);
        auto *target = items.at(3);
        const auto end = target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint();
        QTest::mouseMove(window, end, 20);
        QTRY_VERIFY(content->property("placementCandidate").toInt() > 0);
        QTest::qWait(500);
        QCOMPARE(content->property("dragTo").toInt(), -1);
        QCOMPARE(content->property("gapAmount").toDouble(), 0.0);
        QTRY_VERIFY(content->property("dragTo").toInt() > 0);
        const auto destination = content->property("dragTo").toInt();
        // Holding directly over a target must not make the opened gap retarget,
        // close, or restart its dwell as the neighbors move out of the way.
        QTest::qWait(1500);
        QCOMPARE(content->property("dragTo").toInt(), destination);
        QCOMPARE(content->property("placementCandidate").toInt(), destination);
        QCOMPARE(content->property("gapAmount").toDouble(), 1.0);
        QVERIFY(!content->property("tooltipReady").toBool());
        QVERIFY(content->property("pointerX").toDouble() > press.x());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
        QVERIFY(content->property("settling").toBool());
        // A benign state revision during landing must not silently suppress
        // the eventual reorder commit and send the icon back to its old row.
        auto *liveState = qobject_cast<QStandardItemModel *>(backend.sourceModel());
        QVERIFY(liveState);
        QVERIFY(liveState->setData(liveState->index(5, 0), true,
            liveState->roleNames().key("IsActive")));
        QTest::qWait(40);
        QVERIFY(content->property("settling").toBool());
        QTRY_COMPARE(backend.data(backend.index(destination, 0), DockBackend::AppKey), original);
        QVERIFY(!content->property("dragging").toBool());
        for (auto *item : taskItems(content)) QVERIFY(!item->property("launchFeedback").toBool());
        QVERIFY(backend.reorder(destination, 0));
        QCOMPARE(backend.data(backend.index(0, 0), DockBackend::AppKey), original);
        // A vertical grab follows both axes, lets magnification leave normally,
        // and glides home when released away from the row.
        first = taskItems(content).first();
        QTest::mouseMove(window, QPoint(1, 1));
        QTest::qWait(180);
        auto home = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        QTest::mouseMove(window, home);
        QTest::qWait(180);
        home = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        const auto beforeY = first->mapToScene(QPointF()).y();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, home);
        const QPoint away(home.x() + 20, 10);
        QTest::mouseMove(window, away, 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        // Crossing the drag threshold transfers the currently drawn bounds;
        // it must not jerk the icon by the distance travelled since press.
        const auto pickupY = first->mapToScene(QPointF()).y();
        QVERIFY(pickupY <= beforeY + 2);
        QVERIFY(pickupY > beforeY + (away.y() - home.y()) + 5);
        QTRY_COMPARE(content->property("pickupProgress").toDouble(), 1.0);
        QVERIFY(qAbs(first->mapToScene(QPointF()).y() - beforeY - (away.y() - home.y())) < 2);
        const auto grabbedX = first->mapToScene(QPointF()).x();
        const auto grabbedY = first->mapToScene(QPointF()).y();
        const QPoint farther = away + QPoint(25, 5);
        QTest::mouseMove(window, farther, 20);
        QVERIFY(qAbs(first->mapToScene(QPointF()).x() - grabbedX - 25) < 2);
        QVERIFY(qAbs(first->mapToScene(QPointF()).y() - grabbedY - 5) < 2);
        QTRY_VERIFY(content->property("amount").toDouble() < 0.01);
        QCOMPARE(content->property("dragTo").toInt(), -1);
        QTRY_COMPARE(content->property("gapAmount").toDouble(), 0.0);
        const auto spec = tuning.layout();
        // Twelve remaining tasks plus Trash occupy the closed row.
        const double closedWidth = 13 * spec["iconSize"].toDouble() + 12 * spec["spacing"].toDouble()
            + 2 * spec["padding"].toDouble();
        QVERIFY(qAbs(content->property("shelfWidth").toDouble() - qMin(double(window->width()), closedWidth)) < 0.01);
        QCOMPARE(content->property("partingDurationMs").toInt(), 234);
        auto *neighbor = taskItems(content).at(2);
        const auto approach = neighbor->mapToScene(QPointF(neighbor->width()/2, neighbor->height()/2)).toPoint();
        QTest::mouseMove(window, approach, 20);
        QTRY_VERIFY(content->property("placementCandidate").toInt() >= 0);
        QTest::qWait(250);
        QCOMPARE(content->property("dragTo").toInt(), -1);
        QTest::mouseMove(window, farther, 20);
        QTRY_COMPARE(content->property("placementCandidate").toInt(), -1);
        QTest::qWait(1100);
        QCOMPARE(content->property("dragTo").toInt(), -1);
        QTest::mouseMove(window, approach, 20);
        QTest::qWait(500);
        QCOMPARE(content->property("dragTo").toInt(), -1);
        QTRY_VERIFY(content->property("dragTo").toInt() >= 0);
        QTRY_COMPARE(content->property("gapAmount").toDouble(), 1.0);
        QTRY_VERIFY(content->property("amount").toDouble() > 0.9);
        QTest::mouseMove(window, farther, 20);
        QTRY_COMPARE(content->property("dragTo").toInt(), -1);
        QTRY_COMPARE(content->property("gapAmount").toDouble(), 0.0);
        QTRY_VERIFY(content->property("amount").toDouble() < 0.01);
        const auto freeY = first->y();
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, farther);
        QVERIFY(content->property("settling").toBool());
        QCOMPARE(content->property("releaseBounds").toRectF().y(), freeY);
        QVERIFY(first->y() < content->property("iconBottom").toDouble() - first->height() - 10);
        QTest::qWait(100);
        QVERIFY(content->property("settling").toBool());
        QVERIFY(first->y() > freeY);
        QTRY_VERIFY(!content->property("dragging").toBool());
        QCOMPARE(content->property("gapAmount").toDouble(), 1.0);
        QCOMPARE(backend.data(backend.index(0, 0), DockBackend::AppKey), original);
        // Escape restores the original order and consumes the eventual release.
        first = taskItems(content).first();
        const auto cancelStart = first->mapToScene(QPointF(first->width()/2, first->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cancelStart);
        QTest::mouseMove(window, cancelStart + QPoint(80, 0), 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY(!content->property("dragging").toBool());
        QTest::mouseMove(window, cancelStart + QPoint(100, 5), 20);
        QVERIFY(!content->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cancelStart + QPoint(80, 0));
        QCOMPARE(backend.data(backend.index(0, 0), DockBackend::AppKey), original);
        QVERIFY(!backend.reorder(-1, 0));
        QVERIFY(!backend.reorder(0, backend.rowCount()));
    }
    void dragGrabStaysFixedDuringReveal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile defaults(":/tuning.json");
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        auto document = QJsonDocument::fromJson(defaults.readAll()).object();
        const auto tuningPath = directory.filePath("tuning.json");
        auto writeTuning = [&] {
            QFile file(tuningPath);
            if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(QJsonDocument(document).toJson()) > 0;
        };
        QVERIFY(writeTuning());
        Tuning tuning(tuningPath);
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        backend.setDragTracing(true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        content->setProperty("autoHideEnabled", true);
        auto *task = taskItems(content).at(2);
        auto cursor = task->mapToScene(QPointF(task->width() * 0.3, task->height() * 0.6)).toPoint();
        QTest::mouseMove(window, cursor);
        QTest::qWait(180);
        cursor = task->mapToScene(QPointF(task->width() * 0.3, task->height() * 0.6)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cursor);
        cursor += QPoint(30, 0);
        QTest::mouseMove(window, cursor, 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        QTRY_COMPARE(content->property("pickupProgress").toDouble(), 1.0);
        auto bounds = [&] { return content->property("floatingBounds").toRectF(); };
        // A stationary screen pointer must keep the same grabbed pixel while
        // reveal scale/translation changes under the independently drawn icon.
        // Native task-state updates must not cancel a held gesture. Before
        // this regression, the next move silently started a new pickup.
        auto *samples = qobject_cast<QStandardItemModel *>(backend.sourceModel());
        QVERIFY(samples);
        const int activeRole = samples->roleNames().key("IsActive");
        const auto heldBeforeUpdate = bounds();
        QVERIFY(samples->setData(samples->index(2, 0), true, activeRole));
        QVERIFY(content->property("dragging").toBool());
        QCOMPARE(content->property("pickupProgress").toDouble(), 1.0);
        QCOMPARE(bounds(), heldBeforeUpdate);
        // Track every delivered move, not just a stationary grab. The handler
        // belongs to the moving icon, so local-coordinate feedback must not
        // alter the pointer or the original grab point.
        for (const auto delta : {QPoint(-3, 0), QPoint(-7, 2), QPoint(-11, -4),
                QPoint(4, 0), QPoint(9, 3), QPoint(25, -2), QPoint(-30, 1)}) {
            cursor += delta;
            QTest::mouseMove(window, cursor, 20);
            const auto observed = content->property("dragPointer").toPointF();
            QVERIFY2(QLineF(observed, cursor).length() < 0.05,
                qPrintable(QString("move pointer expected %1,%2 observed %3,%4")
                    .arg(cursor.x()).arg(cursor.y()).arg(observed.x()).arg(observed.y())));
            const auto moved = bounds();
            QVERIFY(qAbs(moved.x() + content->property("grabX").toDouble() - cursor.x()) < 0.05);
            QVERIFY(qAbs(moved.y() + content->property("grabY").toDouble() - cursor.y()) < 0.05);
        }
        const auto heldAfterMoves = bounds();
        content->setProperty("hideProgress", 0.5);
        for (int i = 0; i < 5; ++i) {
            QTest::qWait(35);
            QVERIFY(qAbs(bounds().x() - heldAfterMoves.x()) < 0.05);
            QVERIFY(qAbs(bounds().y() - heldAfterMoves.y()) < 0.05);
            QVERIFY(qAbs(bounds().width() - heldAfterMoves.width()) < 0.05);
        }
        window->setWidth(window->width() + 120);
        auto resizedLayout = document["layout"].toObject();
        resizedLayout["iconSize"] = 44;
        document["layout"] = resizedLayout;
        QVERIFY(writeTuning());
        QVERIFY(tuning.reload());
        QTest::qWait(40);
        const auto mapped = task->mapRectToItem(content, QRectF(0, 0, task->width(), task->height()));
        QVERIFY(qAbs(mapped.x() - heldAfterMoves.x()) < 0.05);
        QVERIFY(qAbs(mapped.y() - heldAfterMoves.y()) < 0.05);
        QVERIFY(qAbs(mapped.width() - heldAfterMoves.width()) < 0.05);
        QVERIFY(qAbs(bounds().x() - heldAfterMoves.x()) < 0.05);
        content->setProperty("hideProgress", 0.0);
        QTest::qWait(35);
        const auto release = bounds();
        QVERIFY(QMetaObject::invokeMethod(content, "landReorder"));
        QVERIFY(qAbs(bounds().x() - release.x()) < 0.05);
        QVERIFY(qAbs(bounds().y() - release.y()) < 0.05);
        QVERIFY(qAbs(bounds().width() - release.width()) < 0.05);
        QTRY_VERIFY(!content->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cursor);
        // Actual identity replacement still cancels, and subsequent movement
        // during that same physical press must not start another pickup.
        content->setProperty("autoHideEnabled", false);
        QTRY_COMPARE(content->property("hideOffset").toDouble(), 0.0);
        task = taskItems(content).at(2);
        cursor = task->mapToScene(QPointF(task->width()/2, task->height()/2)).toPoint();
        QTest::mouseMove(window, cursor, 20);
        QTest::qWait(180);
        cursor = task->mapToScene(QPointF(task->width()/2, task->height()/2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cursor);
        cursor += QPoint(30, 0);
        QTest::mouseMove(window, cursor, 20);
        QTRY_VERIFY(content->property("dragging").toBool());
        const int from = content->property("dragFrom").toInt();
        QVERIFY(samples->setData(samples->index(from, 0), "replacement-app",
            samples->roleNames().key("AppId")));
        QVERIFY(!content->property("dragging").toBool());
        cursor += QPoint(15, 0);
        QTest::mouseMove(window, cursor, 20);
        QVERIFY(!content->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cursor);
    }
    void floatingIconHandsOffToNativeDrag() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        backend.setSimulatedTaskDrags(true);
        QSignalSpy started(&backend, &DockBackend::taskDragStarted);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(!content->property("startingUp").toBool());
        content->setProperty("nativeDragHandoff", true);
        const auto nativeSize = window->size();
        const auto original = backend.data(backend.index(0, 0), DockBackend::AppKey);
        auto artwork = [](QQuickItem *task) { return task->childItems().first(); };
        auto lift = [&](QQuickItem *task) -> QPoint {
            auto point = task->mapToScene(QPointF(task->width() / 2, task->height() / 2)).toPoint();
            QTest::mouseMove(window, point);
            QTest::qWait(180);
            point = task->mapToScene(QPointF(task->width() / 2, task->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(window, point + QPoint(20, 0), 20);
            // This helper returns a point, so it cannot QVERIFY; report instead.
            if (!QTest::qWaitFor([&] { return content->property("pickupProgress").toDouble() == 1.0
                    && content->property("dragging").toBool(); }))
                qWarning("pickup did not finish before the native-drag handoff check");
            return point + QPoint(20, 0);
        };
        auto *first = taskItems(content).first();
        auto cursor = lift(first);
        QVERIFY(content->property("dragging").toBool());
        // Inside the surface the dock draws the icon itself.
        QTest::mouseMove(window, cursor - QPoint(0, 30), 20);
        QCOMPARE(started.count(), 0);
        QVERIFY(artwork(first)->isVisible());
        // Reaching the top edge hands the icon to the compositor with the
        // current grab point and drawn size, so it does not jump.
        const QPoint top(cursor.x(), 4);
        QTest::mouseMove(window, top, 20);
        QCOMPARE(started.count(), 1);
        const auto bounds = content->property("floatingBounds").toRectF();
        const auto hotSpot = started.first().at(2).toPointF();
        QCOMPARE(started.first().at(0).toInt(), 0);
        QVERIFY(qAbs(started.first().at(1).toDouble() - bounds.width()) < 0.01);
        QVERIFY(QLineF(bounds.topLeft() + hotSpot, QPointF(top)).length() < 0.05);
        QVERIFY(!artwork(first)->isVisible());
        // Mouse events no longer drive the gesture; the grab release that
        // follows a native drag must not land it.
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, top);
        QVERIFY(content->property("dragging").toBool());
        QVERIFY(!content->property("settling").toBool());
        // Returning over the row targets a slot from drag events, then the
        // drop lands it from under the cursor.
        QMimeData mime;
        mime.setData(DockBackend::taskDragMimeType(), original.toString().toUtf8());
        auto *target = taskItems(content).at(3);
        const auto over = target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
        QDragEnterEvent enter(over, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        QVERIFY(enter.isAccepted());
        QDragMoveEvent moveEvent(over + QPoint(1, 0), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &moveEvent);
        // Wayland only delivers the drop if every motion is accepted.
        QVERIFY(moveEvent.isAccepted());
        QTRY_VERIFY(content->property("placementCandidate").toInt() > 0);
        QTRY_VERIFY(content->property("dragTo").toInt() > 0);
        const int destination = content->property("dragTo").toInt();
        QDropEvent drop(over + QPoint(1, 0), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &drop);
        QVERIFY(drop.isAccepted());
        QVERIFY(content->property("settling").toBool());
        QVERIFY(artwork(first)->isVisible());
        const auto landed = content->property("floatingBounds").toRectF();
        QVERIFY(QLineF(landed.topLeft() + hotSpot, QPointF(over + QPoint(1, 0))).length() < 1);
        emit backend.taskDragFinished(true);
        QTRY_VERIFY(!content->property("dragging").toBool());
        QCOMPARE(backend.data(backend.index(destination, 0), DockBackend::AppKey), original);
        QVERIFY(backend.reorder(destination, 0));
        // Dropped elsewhere (or cancelled): glide home, order unchanged.
        first = taskItems(content).first();
        cursor = lift(first);
        QVERIFY(content->property("dragging").toBool());
        QTest::mouseMove(window, QPoint(cursor.x(), 4), 20);
        QCOMPARE(started.count(), 2);
        emit backend.taskDragFinished(false);
        QVERIFY(content->property("settling").toBool());
        QVERIFY(artwork(first)->isVisible());
        auto *mouseArea = first->findChild<QObject *>("taskMouseArea");
        QVERIFY(mouseArea);
        QVERIFY(QMetaObject::invokeMethod(mouseArea, "canceled"));
        QVERIFY(content->property("settling").toBool());
        const auto returnStart = content->property("floatingBounds").toRectF();
        QVERIFY(returnStart.top() >= 0);
        QVERIFY(returnStart.left() >= 0);
        QVERIFY(returnStart.right() <= window->width());
        QTRY_VERIFY(content->property("floatingBounds").toRectF().top() > returnStart.top());
        QTest::qWait(100);
        QVERIFY(content->property("settling").toBool());
        QTRY_VERIFY(!content->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(cursor.x(), 4));
        QCOMPARE(backend.data(backend.index(0, 0), DockBackend::AppKey), original);
        QCOMPARE(window->size(), nativeSize);
    }
    void startupAndShutdownAnimateWithoutAutoHide() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(content);
        QVERIFY(!content->property("autoHideEnabled").toBool());
        QVERIFY(content->property("startingUp").toBool());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(content->property("hideOffset").toDouble() > 0
            && content->property("hideOffset").toDouble() < window->height());
        QTRY_VERIFY(!content->property("startingUp").toBool());
        QCOMPARE(content->property("hideOffset").toDouble(), 0.0);
        QSignalSpy finished(content, SIGNAL(shutdownFinished()));
        // Close is deferred with the native window still visible.
        window->close();
        QVERIFY(window->isVisible());
        QVERIFY(content->property("shuttingDown").toBool());
        content->setProperty("pointerInside", true);
        content->setProperty("autoHideEnabled", false);
        QVERIFY(QMetaObject::invokeMethod(content, "beginShutdown"));
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(content->property("hideOffset").toDouble(), double(window->height()));
    }
    void autoHideRevealsAtEdgeAndKeepsSurfaceFixed() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", false);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(content);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto size = window->size();
        QTest::mouseMove(window, QPoint(1, 1));
        content->setProperty("autoHideEnabled", true);
        QTRY_COMPARE(content->property("hideOffset").toDouble(), double(size.height()));
        QCOMPARE(content->property("revealScale").toDouble(), 0.8);
        auto *presentation = content->findChild<QQuickItem *>("dockPresentation");
        QVERIFY(presentation);
        QCOMPARE(presentation->transformOrigin(), QQuickItem::Bottom);
        QTRY_COMPARE(window->mask().intersected(QRect(QPoint(), size)).boundingRect().height(), 2);
        QVERIFY(!window->mask().contains(QPoint(size.width()/2, size.height()-30)));
        QTest::mouseMove(window, QPoint(size.width()/2, size.height()-1));
        QTRY_COMPARE(content->property("hideOffset").toDouble(), 0.0);
        QCOMPARE(presentation->scale(), 1.0);
        QCOMPARE(presentation->y(), 0.0);
        QTest::qWait(800);
        QVERIFY(!content->property("hideRequested").toBool());
        // A menu holds the dock open even after the pointer leaves it.
        content->setProperty("openMenus", 1);
        QTest::mouseMove(window, QPoint(1, 1));
        QTest::qWait(800);
        QVERIFY(!content->property("hideRequested").toBool());
        // A press takes over the hold when the last menu closes.
        content->setProperty("pressedTask", 0);
        content->setProperty("openMenus", 0);
        QTest::qWait(800);
        QVERIFY(content->property("holdingOpen").toBool());
        QVERIFY(!content->property("hideRequested").toBool());
        content->setProperty("pressedTask", -1);
        QTRY_VERIFY(content->property("hideRequested").toBool());
        // Disabling auto-hide reverses an in-flight slide.
        content->setProperty("autoHideEnabled", false);
        QTRY_COMPARE(content->property("hideOffset").toDouble(), 0.0);
        QCOMPARE(window->size(), size);
    }
    void hoverChangesContentAndInputWithoutResizingSurface() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, true);
        QVERIFY2(backend.isReady(), qPrintable(backend.error()));
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        // Use a regular offscreen window with production input-region updates.
        engine.rootContext()->setContextProperty("previewMode", false);
        QStringList warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *content = window->findChild<QQuickItem *>("dockContent");
        QVERIFY(content);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto surfaceSize = window->size();
        QTRY_VERIFY(!content->property("startingUp").toBool());
        QTRY_COMPARE(taskItems(content).size(), 13);
        const auto icons = taskItems(content);
        QCOMPARE(icons[6]->width(), 48);
        const auto musicCenter = icons[6]->mapToScene(QPointF(icons[6]->width() / 2, icons[6]->height() / 2)).toPoint();
        QTest::mouseMove(window, musicCenter);
        QTRY_VERIFY_WITH_TIMEOUT(content->property("amount").toDouble() > 0.99, 2000);
        QTRY_VERIFY(icons[6]->width() > 83);
        QCOMPARE(window->size(), surfaceSize);
        auto *tooltip = content->findChild<QQuickItem *>("dockToolTip");
        QVERIFY(tooltip);
        auto *caption = tooltip->findChild<QQuickItem *>("tooltipCaption");
        QVERIFY(caption);
        const auto labelFont = caption->property("font").value<QFont>();
        QCOMPARE(labelFont.family(), QString("Inter"));
        QCOMPARE(labelFont.pixelSize(), 16);
        QCOMPARE(labelFont.weight(), QFont::DemiBold);
        QTRY_VERIFY(tooltip->isVisible());
        QCOMPARE(tooltip->property("label").toString(), QString("Music"));
        const QFontMetricsF labelMetrics(labelFont);
        const double labelBottom = tooltip->y() + caption->property("baselineOffset").toDouble() + labelMetrics.descent();
        QVERIFY(qAbs(icons[6]->y() - labelBottom - 9) < 1);
        const auto labelCenter = tooltip->mapToScene(QPointF(tooltip->width() / 2, tooltip->height() / 2)).toPoint();
        QVERIFY(!window->mask().contains(labelCenter));
        const auto capture = qEnvironmentVariable("SPACEDOCK_TEST_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture));
        const auto iconTop = icons[6]->mapToScene(QPointF(icons[6]->width() / 2, 2)).toPoint();
        QTRY_VERIFY(window->mask().contains(iconTop));
        QVERIFY(!window->mask().contains(QPoint(5, 5)));
        // The pointer can traverse the newly enlarged area without collapsing it.
        QTest::mouseMove(window, iconTop);
        QTest::qWait(200);
        QVERIFY(content->property("amount").toDouble() > 0.99);
        // Cross every inter-icon gap above the shelf, where the old input
        // region had holes. Dwell long enough to expose an accidental exit.
        const int sweepY = window->height() - tuning.layout().value("iconBaselineInset").toInt() - 32;
        QVERIFY(sweepY < window->height() - tuning.layout().value("shelfDepth").toInt());
        for (int i = 0; i + 1 < icons.size(); ++i) {
            const auto right = icons[i]->mapToScene(QPointF(icons[i]->width(), 0)).x();
            const auto left = icons[i + 1]->mapToScene(QPointF(0, 0)).x();
            const QPoint gap(qRound((right + left) / 2), sweepY);
            QTRY_VERIFY(window->mask().contains(gap));
            QTest::mouseMove(window, gap);
            QTest::qWait(200);
            QVERIFY(content->property("magnifying").toBool());
            QVERIFY(content->property("amount").toDouble() > 0.99);
            QVERIFY(window->mask().contains(gap));
        }
        const auto focusBeforeExit = content->property("pointerX").toDouble();
        QTest::mouseMove(window, QPoint(5, 5));
        QCOMPARE(content->property("pointerX").toDouble(), focusBeforeExit);
        QTRY_VERIFY(!tooltip->isVisible());
        QTRY_VERIFY_WITH_TIMEOUT(content->property("amount").toDouble() < 0.01, 2000);
        QTRY_COMPARE(icons[6]->width(), 48);
        QVERIFY(!window->mask().contains(iconTop));
        QCOMPARE(window->size(), surfaceSize);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }

};
int main(int argc, char **argv) {
    return runDockUiTest<MagnificationUiTest>(argc, argv);
}
#include "magnification_ui_test.moc"

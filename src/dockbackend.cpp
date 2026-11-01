#include "dockbackend.h"
#include <KConfigGroup>
#include <KConfig>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QIcon>
#include <QProcess>
#include <QDesktopServices>
#include <QCursor>
#include <QRegion>
#include <QPolygon>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QPixmap>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QCryptographicHash>
#include <QImageReader>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QtMath>
#include <algorithm>


bool DockBackend::canTrashUrls(const QList<QUrl> &urls) const
{
    if (urls.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("kioclient")).isEmpty()) return false;
    for (const auto &url : urls) {
        if (!url.isLocalFile() || url.toLocalFile().isEmpty() || !QFileInfo::exists(url.toLocalFile())) return false;
    }
    return true;
}

bool DockBackend::trashUrls(const QList<QUrl> &urls)
{
    if (!canTrashUrls(urls)) return false;
    auto *process = new QProcess(this);
    QStringList arguments{QStringLiteral("move")};
    for (const auto &url : urls) arguments << url.toString(QUrl::FullyEncoded);
    arguments << QStringLiteral("trash:/");
    connect(process, &QProcess::finished, this, [process](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0)
            qWarning() << "Could not move items to trash:" << process->readAllStandardError();
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qWarning() << "Could not start trash operation:" << process->errorString();
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("kioclient"), arguments);
    return true;
}

void DockBackend::openTrash() { QDesktopServices::openUrl(QUrl(QStringLiteral("trash:/"))); }

DockBackend::DockBackend(QQmlEngine *engine, bool sampleTasks, QObject *parent, KSharedConfig::Ptr config)
    : QIdentityProxyModel(parent), m_config(config ? config : KSharedConfig::openConfig("spacedockrc")), m_sampleTasks(sampleTasks), m_engine(engine), m_appImages(*this, m_appImageStore, sampleTasks)
{
    connect(&m_appImages, &AppImageController::setupChanged, this, &DockBackend::appImageSetupChanged);
    connect(&m_appImages, &AppImageController::replacementPrepared, this, &DockBackend::appImageReplacementPrepared);
    connect(&m_appImages, &AppImageController::replacementAborted, this, &DockBackend::appImageReplacementAborted);
    QQmlComponent component(engine);
    component.setData(R"(
import org.kde.taskmanager 0.1 as TaskManager
TaskManager.TasksModel {
    groupMode: TaskManager.TasksModel.GroupApplications
    groupingWindowTasksThreshold: -1
    sortMode: TaskManager.TasksModel.SortManual
    separateLaunchers: true
    launchInPlace: true
    hideActivatedLaunchers: true
}
)", QUrl("qrc:/TaskAdapter.qml"));
    auto *tasks = qobject_cast<QAbstractItemModel *>(component.create());
    if (!tasks) {
        m_error = component.errorString();
        return;
    }
    tasks->setParent(this);
    // Filter at the task model so all row-based actions keep using its own
    // indices, including when SpaceDock itself has been pinned.
    auto *filteredTasks = qobject_cast<QSortFilterProxyModel *>(tasks);
    const int appIdRole = tasks->roleNames().key("AppId", -1);
    if (filteredTasks && appIdRole >= 0) {
        filteredTasks->setFilterRole(appIdRole);
        filteredTasks->setFilterRegularExpression(QRegularExpression(
            QStringLiteral("^(?!spacedock(?:\\.desktop)?$)"), QRegularExpression::CaseInsensitiveOption));
    }
    // Only seed installed launchers; saved user pins continue to take precedence.
    QStringList defaultPins;
    const QStringList desktopIds {
        "org.kde.dolphin.desktop", "org.kde.konsole.desktop",
        "org.mozilla.firefox.desktop", "org.kde.kate.desktop",
        "org.kde.gwenview.desktop", "org.kde.okular.desktop",
        "org.kde.elisa.desktop", "org.kde.kcalc.desktop",
        "org.kde.spectacle.desktop", "org.kde.ark.desktop",
        "org.kde.kolourpaint.desktop", "org.kde.discover.desktop",
        "systemsettings.desktop"
    };
    for (auto id : desktopIds) {
        auto installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + id);
        if (installed.isEmpty() && id == "org.mozilla.firefox.desktop") {
            id = "firefox.desktop";
            installed = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + id);
        }
        if (!installed.isEmpty()) defaultPins.append("applications:" + id);
    }
    const auto pins = KConfigGroup(m_config, "Launchers").readEntry("applications", defaultPins);
    if (!sampleTasks) {
        for (const auto &pin : pins) m_appImageStore.refreshAppImageLauncher(QUrl(pin));
        tasks->setProperty("launcherList", pins);
    }
    if (sampleTasks) {
        // Deterministic, inert delegates for smoke tests and preview captures.
        // The real Plasma module was instantiated above to validate availability.
        auto *samples = new QStandardItemModel(this);
        samples->setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::DecorationRole, "decoration"},
            {Qt::UserRole + 1, "AppName"}, {Qt::UserRole + 2, "IsWindow"},
            {Qt::UserRole + 3, "HasLauncher"}, {Qt::UserRole + 4, "IsStartup"},
            {Qt::UserRole + 5, "IsDemandingAttention"}, {Qt::UserRole + 6, "IsActive"},
            {Qt::UserRole + 7, "AppId"}});
        const QList<QPair<QString, QString>> apps {
            {"Files", "system-file-manager"}, {"Terminal", "utilities-terminal"}, {"Browser", "web-browser"},
            {"Editor", "kate"}, {"Photos", "gwenview"}, {"Documents", "okular"},
            {"Music", "elisa"}, {"Calculator", "accessories-calculator"},
            {"Screenshots", "spectacle"}, {"Archives", "ark"}, {"Paint", "kolourpaint"},
            {"Software", "plasmadiscover"}, {"Settings", "preferences-system"}
        };
        for (const auto &app : apps) {
            auto *item = new QStandardItem(QIcon::fromTheme(app.second), app.first);
            item->setData(app.first, Qt::UserRole + 1);
            item->setData(app.first, Qt::UserRole + 7);
            item->setData(samples->rowCount() % 3 == 0, Qt::UserRole + 2);
            item->setData(true, Qt::UserRole + 3);
            item->setData(false, Qt::UserRole + 4);
            item->setData(false, Qt::UserRole + 5);
            item->setData(false, Qt::UserRole + 6);
            samples->appendRow(item);
        }
        tasks = samples;
    }
    setSourceModel(tasks);
}

void DockBackend::setSourceModel(QAbstractItemModel *tasks)
{
    if (tasks == sourceModel()) return;
    for (const auto &connection : m_sourceConnections) disconnect(connection);
    m_sourceConnections.clear();
    emit taskStructureAboutToChange();
    QIdentityProxyModel::setSourceModel(tasks);
    if (tasks) {
        auto invalidate = [this] { invalidateSource(); };
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::dataChanged, this,
            [this, invalidate](const QModelIndex &first, const QModelIndex &last) {
                invalidate();
                // Upstream role numbers differ from the stable frontend roles.
                emit dataChanged(mapFromSource(first), mapFromSource(last), {Name, Running, Pinned, Launching, Attention, Active, AppKey, NeedsAppImageSetup});
            }));
        auto structureChanged = [this, invalidate] {
            invalidate();
            emit taskStructureChanged();
        };
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsInserted, this, structureChanged));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsRemoved, this, structureChanged));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsMoved, this, structureChanged));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::modelReset, this, structureChanged));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::layoutChanged, this, structureChanged));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsAboutToBeInserted, this, &DockBackend::taskStructureAboutToChange));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsAboutToBeRemoved, this, &DockBackend::taskStructureAboutToChange));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::rowsAboutToBeMoved, this, &DockBackend::taskStructureAboutToChange));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::modelAboutToBeReset, this, &DockBackend::taskStructureAboutToChange));
        m_sourceConnections.append(connect(tasks, &QAbstractItemModel::layoutAboutToBeChanged, this, &DockBackend::taskStructureAboutToChange));
    }
    invalidateSource();
    emit taskStructureChanged();
}

void DockBackend::invalidateSource()
{
    ++m_revision;
    emit revisionChanged();
    traceTasks();
    if (!m_sampleTasks && !m_iconCachePending) {
        m_iconCachePending = true;
        QTimer::singleShot(0, this, [this] {
            m_iconCachePending = false;
            reconcileAppImageLaunchers();
            cacheAppImageIcons();
            m_appImages.update();
        });
    }
}

QHash<int, QByteArray> DockBackend::roleNames() const
{
    return {{Name, "appName"}, {Running, "running"}, {Pinned, "pinned"},
        {Launching, "launching"}, {Attention, "attention"}, {Active, "active"}, {AppKey, "appKey"},
        {NeedsAppImageSetup, "needsAppImageSetup"}};
}

QVariant DockBackend::taskData(const QModelIndex &idx, const QByteArray &name) const
{
    const auto roles = sourceModel()->roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        if (it.value() == name) return sourceModel()->data(mapToSource(idx), it.key());
    return {};
}

QVariant DockBackend::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || !sourceModel()) return {};
    if (role == NeedsAppImageSetup) return needsAppImageSetup(idx);
    if (role == AppKey) {
        auto key = taskData(idx, "AppId").toString();
        if (key.isEmpty()) key = taskData(idx, "LauncherUrlWithoutIcon").toUrl().fileName();
        if (key.endsWith(".desktop")) key.chop(8);
        if (key.isEmpty()) key = data(idx, Name).toString();
        return key.isEmpty() ? QString("task-%1").arg(idx.row()) : key;
    }
    if (role == Name) {
        if (needsAppImageSetup(idx)) {
            const auto path = m_appImageStore.generatedAppImagePath(taskData(idx, "LauncherUrlWithoutIcon").toUrl());
            return tr("Add \"%1\" AppImage to Dock").arg(QFileInfo(path).completeBaseName());
        }
        const auto key = customIconKey(idx);
        const auto overrideName = KConfigGroup(m_config, "ApplicationNames").readEntry(
            QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()), QString());
        if (!overrideName.isEmpty()) return overrideName;
        const auto name = taskData(idx, "AppName").toString();
        return name.isEmpty() ? taskData(idx, "display") : name;
    }
    if (role == Running)
        return taskData(idx, "IsWindow").toBool() || taskData(idx, "IsGroupParent").toBool();
    if (role == Pinned) return taskData(idx, "HasLauncher");
    if (role == Launching) return taskData(idx, "IsStartup").toBool();
    if (role == Attention) return taskData(idx, "IsDemandingAttention").toBool();
    if (role == Active) return taskData(idx, "IsActive").toBool();
    return QIdentityProxyModel::data(idx, role);
}

bool DockBackend::addLaunchTest()
{
    if (m_sampleTasks || !sourceModel()) return false;
    const auto desktop = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
        "applications/spacedock-launch-test.desktop");
    if (desktop.isEmpty()) return false;
    auto pins = sourceModel()->property("launcherList").toStringList();
    const QString launcher = "applications:spacedock-launch-test.desktop";
    // Retire the old unregistered file URL if it was previously saved as a pin.
    pins.removeAll(QUrl::fromLocalFile(QDir(QCoreApplication::applicationDirPath())
        .filePath("spacedock-launch-test.desktop")).toString());
    if (!pins.contains(launcher)) pins.append(launcher);
    // Session-only pin; leave the user's stored launcher configuration alone.
    return sourceModel()->setProperty("launcherList", pins);
}

void DockBackend::trackDragPointer(QQuickWindow *window)
{
    if (!m_traceDrag || !window || m_dragTraceWindow == window) return;
    if (m_dragTraceWindow) m_dragTraceWindow->removeEventFilter(this);
    m_dragTraceWindow = window;
    window->installEventFilter(this);
    connect(window, &QObject::destroyed, this, [this, window] {
        if (m_dragTraceWindow == window) m_dragTraceWindow = nullptr;
    });
}

bool DockBackend::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dragTraceWindow && (event->type() == QEvent::MouseMove
            || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease))
        m_nativeDragPoint = static_cast<QMouseEvent *>(event)->position();
    return QIdentityProxyModel::eventFilter(watched, event);
}

void DockBackend::traceDrag(QQuickWindow *window, const QString &phase, const QPointF &mapped,
    const QRectF &artwork, const QPointF &grabOffset, qreal pickupProgress)
{
    if (!m_traceDrag || window != m_dragTraceWindow) return;
    qInfo() << "drag-trace" << m_dragTraceClock.elapsed() << phase
            << "native" << m_nativeDragPoint << "mapped" << mapped
            << "mapError" << mapped - m_nativeDragPoint
            << "grabError" << artwork.topLeft() + grabOffset - m_nativeDragPoint
            << "artwork" << artwork << "pickup" << pickupProgress;
}

void DockBackend::setTaskTracing(bool enabled)
{
    m_traceTasks = enabled;
    m_lastTaskSnapshot.clear();
    m_traceClock.start();
    traceTasks();
}

void DockBackend::traceTasks()
{
    if (!m_traceTasks || !sourceModel()) return;
    QStringList rows;
    for (int row = 0; row < rowCount(); ++row) {
        const auto task = index(row, 0);
        rows.append(QString("  %1: %2 running=%3 startup=%4 active=%5 launcher=%6 appId=%7 pid=%8 appImage=%9")
            .arg(row).arg(data(task, Name).toString().replace('\n', ' '))
            .arg(data(task, Running).toBool()).arg(data(task, Launching).toBool())
            .arg(data(task, Active).toBool()).arg(taskData(task, "LauncherUrlWithoutIcon").toUrl().toString())
            .arg(taskData(task, "AppId").toString()).arg(taskData(task, "AppPid").toUInt())
            .arg(data(task, Running).toBool() ? AppImageController::processAppImage(taskData(task, "AppPid").toUInt()) : QString()));
    }
    const auto snapshot = rows.join('\n');
    if (snapshot == m_lastTaskSnapshot) return;
    m_lastTaskSnapshot = snapshot;
    QTextStream(stderr) << "dock tasks +" << m_traceClock.elapsed() << "ms:\n" << snapshot << "\n" << Qt::flush;
}

bool DockBackend::needsAppImageSetup(const QModelIndex &task) const
{
    if (m_sampleTasks) return false;
    const auto launcher = taskData(task, "LauncherUrlWithoutIcon").toUrl();
    return m_appImageStore.needsSetup(launcher);
}

void DockBackend::activate(int row, bool bringAllWindows)
{
    if (m_sampleTasks || row < 0 || row >= rowCount()) return;
    if (!m_appImages.prepareActivation(row)) return;
    const auto parent = sourceModel()->index(row, 0);
    const int count = sourceModel()->rowCount(parent);
    if (!count) {
        QMetaObject::invokeMethod(sourceModel(), "requestActivate", Q_ARG(QModelIndex, parent));
        return;
    }
    QList<QPersistentModelIndex> windows;
    int active = -1;
    for (int i = 0; i < count; ++i) {
        const auto child = sourceModel()->index(i, 0, parent);
        if (!taskData(mapFromSource(child), "IsWindow").toBool()) continue;
        if (taskData(mapFromSource(child), "IsActive").toBool()) active = windows.size();
        windows.append(child);
    }
    if (windows.isEmpty()) return;
    auto activateWindow = [this](const QPersistentModelIndex &window) {
        if (window.isValid()) QMetaObject::invokeMethod(sourceModel(), "requestActivate", Q_ARG(QModelIndex, QModelIndex(window)));
    };
    if (!bringAllWindows) {
        activateWindow(windows.at(active < 0 ? 0 : (active + 1) % windows.size()));
        return;
    }
    // Raise back-to-front to retain the app's existing stacking order. Keep
    // its active window on top, if one was already focused.
    const QPersistentModelIndex activeWindow = active < 0 ? QPersistentModelIndex() : windows.at(active);
    std::stable_sort(windows.begin(), windows.end(), [this](const auto &a, const auto &b) {
        return taskData(mapFromSource(a), "StackingOrder").toLongLong()
            < taskData(mapFromSource(b), "StackingOrder").toLongLong();
    });
    for (const auto &window : windows) if (window != activeWindow) activateWindow(window);
    if (activeWindow.isValid()) activateWindow(activeWindow);
}

void DockBackend::publishIconGeometry(int row, QQuickItem *item)
{
    if (m_sampleTasks || !sourceModel() || !item || !item->window()
        || row < 0 || row >= rowCount() || item->width() <= 0 || item->height() <= 0) return;
    const auto topLeft = item->mapToGlobal(QPointF());
    const QRect bounds = QRectF(topLeft, QSizeF(item->width(), item->height())).toAlignedRect();
    QMetaObject::invokeMethod(sourceModel(), "requestPublishDelegateGeometry",
        Q_ARG(QModelIndex, sourceModel()->index(row, 0)), Q_ARG(QRect, bounds), Q_ARG(QObject *, item));
}

void DockBackend::startTaskDrag(QQuickWindow *window, int row, const QString &appKey,
    qreal size, const QPointF &hotSpot)
{
    emit taskDragStarted(row, size, hotSpot);
    if (m_simulateTaskDrags) return;
    if (!window || row < 0 || row >= rowCount() || size <= 0) {
        QMetaObject::invokeMethod(this, [this] { emit taskDragFinished(false); }, Qt::QueuedConnection);
        return;
    }
    auto icon = qvariant_cast<QIcon>(data(index(row, 0), Qt::DecorationRole));
    if (needsAppImageSetup(index(row, 0))) {
        const bool opened = appImageSetupPending() && data(index(row, 0), AppKey).toString() == appImageSetupKey();
        icon = QIcon(opened ? ":/assets/icons/appimage_open.png" : ":/assets/icons/appimage_closed.png");
    }
    if (icon.isNull()) icon = QIcon::fromTheme("application-x-executable");
    const int logical = qMax(1, qRound(size));
    const qreal ratio = window->devicePixelRatio();
    QPixmap pixmap = icon.pixmap(QSize(logical, logical), ratio);
    const QSize pixels(qCeil(logical * ratio), qCeil(logical * ratio));
    if (pixmap.size() != pixels)
        pixmap = pixmap.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pixmap.setDevicePixelRatio(ratio);
    auto *mime = new QMimeData;
    mime->setData(taskDragMimeType(), appKey.toUtf8());
    auto *drag = new QDrag(window);
    drag->setMimeData(mime);
    drag->setPixmap(pixmap);
    drag->setHotSpot(hotSpot.toPoint());
    // exec() runs a nested event loop. Start it after the QML handler returns.
    QTimer::singleShot(0, this, [this, drag] {
        const auto action = drag->exec(Qt::MoveAction);
        drag->deleteLater();
        emit taskDragFinished(action == Qt::MoveAction);
    });
}

QPointF DockBackend::cursorPosition(QQuickWindow *window) const
{
    return window ? window->mapFromGlobal(QCursor::pos()) : QPointF(-1, -1);
}

void DockBackend::taskAction(int row, const QString &action)
{
    if (m_sampleTasks || !sourceModel() || row < 0 || row >= rowCount()) return;
    const char *method = action == "new" ? "requestNewInstance"
        : action == "close" ? "requestClose" : nullptr;
    if (method) QMetaObject::invokeMethod(sourceModel(), method,
        Q_ARG(QModelIndex, sourceModel()->index(row, 0)));
}

QVariantList DockBackend::contextEntries(int row)
{
    m_contextActions.clear();
    m_contextTask = QModelIndex();
    if (m_sampleTasks || !sourceModel() || row < 0 || row >= rowCount()) return {};
    if (!m_appActions) {
        // Session-only model: never writes Plasma's favorites. Its action
        // metadata and dispatch retain KDE's desktop-action/KIO semantics.
        QQmlComponent component(m_engine);
        component.setData("import org.kde.plasma.private.kicker as Kicker\nKicker.SimpleFavoritesModel {}",
            QUrl("qrc:/AppActionsAdapter.qml"));
        m_appActions = qobject_cast<QAbstractItemModel *>(component.create());
        if (!m_appActions) { qWarning().noquote() << component.errorString(); return {}; }
        m_appActions->setParent(this);
    }
    const auto url = taskData(index(row, 0), "LauncherUrlWithoutIcon").toUrl();
    const auto launcher = applicationLauncher(url);
    if (launcher.isEmpty()) return {};
    // Kicker treats file: URLs as documents and does not resolve the
    // task model's applications: URLs. Use the actual desktop-file path.
    const auto favorite = launcher.isLocalFile() ? launcher.toLocalFile()
        : QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + launcher.path());
    if (favorite.isEmpty()) return {};
    m_appActions->setProperty("favorites", QStringList{favorite});
    if (!m_appActions->rowCount()) return {};
    m_contextTask = sourceModel()->index(row, 0);
    QVariantList entries;
    const auto roles = m_appActions->roleNames();
    int actionRole = -1;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (it.value() == "actionList") actionRole = it.key();
    if (actionRole < 0) return {};
    const auto actions = m_appActions->data(m_appActions->index(0, 0), actionRole).toList();
    for (const auto &value : actions) {
        const auto action = value.toMap();
        const auto id = action.value("actionId").toString();
        // Keep dock controls local. Avoid launcher-editor/widget actions that
        // require a Plasma applet or QApplication rather than QGuiApplication.
        if (id != "_kicker_jumpListAction" && id != "_kicker_recentDocument") continue;
        const int token = ++m_nextContextToken;
        m_contextActions.insert(token, action);
        auto text = action.value("text").toString();
        if (id == "_kicker_jumpListAction") text.replace("&&", "&");
        entries.append(QVariantMap{{"text", text}, {"token", token},
            {"section", id == "_kicker_recentDocument" ? "recent" : "actions"}});
    }
    return entries;
}

bool DockBackend::triggerContextAction(int token)
{
    if (m_sampleTasks || !m_appActions || !m_contextTask.isValid() || !m_contextActions.contains(token)) return false;
    const auto action = m_contextActions.value(token);
    // Clear before dispatch: launching may synchronously change task rows.
    m_contextActions.clear();
    bool result = false;
    return QMetaObject::invokeMethod(m_appActions, "trigger", Q_RETURN_ARG(bool, result),
        Q_ARG(int, 0), Q_ARG(QString, action.value("actionId").toString()),
        Q_ARG(QVariant, action.value("actionArgument"))) && result;
}

void DockBackend::togglePin(int row)
{
    if (m_sampleTasks || row < 0 || row >= rowCount()) return;
    const auto idx = index(row, 0);
    const auto url = taskData(idx, "LauncherUrlWithoutIcon").toUrl();
    if (!url.isValid() || url.isEmpty()) return;
    bool result = false;
    const bool removing = data(idx, Pinned).toBool();
    const auto method = removing ? "requestRemoveLauncher" : "requestAddLauncher";
    if (!QMetaObject::invokeMethod(sourceModel(), method, Q_RETURN_ARG(bool, result), Q_ARG(QUrl, url)) || !result) return;
    if (removing) m_appImages.forget(url);
    KConfigGroup group(m_config, "Launchers");
    group.writeEntry("applications", m_appImages.persistentLaunchers(sourceModel()->property("launcherList").toStringList()));
    group.sync();
    m_appImages.update();
}

bool DockBackend::reorder(int from, int to)
{
    if (!sourceModel() || from < 0 || to < 0 || from >= rowCount() || to >= rowCount()) return false;
    if (from == to) return true;
    if (data(index(from, 0), Pinned).toBool() != data(index(to, 0), Pinned).toBool()) return false;
    if (m_sampleTasks) {
        auto *samples = qobject_cast<QStandardItemModel *>(sourceModel());
        if (!samples) return false;
        samples->insertRow(to, samples->takeRow(from));
        return true;
    }
    if (m_traceTasks) {
        qInfo() << "reorder-trace request" << from << to;
        traceTasks();
    }
    auto pins = sourceModel()->property("launcherList").toStringList();
    if (data(index(from, 0), Pinned).toBool()) {
        const auto launcherAt = [this](int row) {
            return applicationLauncher(taskData(index(row, 0), "LauncherUrlWithoutIcon").toUrl());
        };
        const auto source = launcherAt(from);
        const auto target = launcherAt(to);
        if (source.isEmpty() || target.isEmpty()) return false;
        int sourcePin = -1;
        int targetPin = -1;
        for (int i = 0; i < pins.size(); ++i) {
            const auto launcher = applicationLauncher(QUrl(pins[i]));
            if (launcher == source) sourcePin = i;
            if (launcher == target) targetPin = i;
        }
        if (sourcePin < 0 || targetPin < 0) return false;
        pins.move(sourcePin, targetPin);
        if (!sourceModel()->setProperty("launcherList", pins)) return false;
        // Rebuild the manual map against the new launcher positions. A proxy
        // refresh alone leaves the hidden-launcher map in its previous order.
        if (sourceModel()->property("separateLaunchers").toBool()) {
            sourceModel()->setProperty("separateLaunchers", false);
            sourceModel()->setProperty("separateLaunchers", true);
        }
        if (m_traceTasks) { qInfo() << "reorder-trace after launcherList/resort"; traceTasks(); }
    } else {
        bool moved = false;
        if (!QMetaObject::invokeMethod(sourceModel(), "move", Q_RETURN_ARG(bool, moved),
                Q_ARG(int, from), Q_ARG(int, to), Q_ARG(QModelIndex, QModelIndex())) || !moved) return false;
        if (m_traceTasks) { qInfo() << "reorder-trace after task move"; traceTasks(); }
        return true;
    }
    KConfigGroup group(m_config, "Launchers");
    // The development startup helper remains session-only after reordering.
    pins.removeAll("applications:spacedock-launch-test.desktop");
    group.writeEntry("applications", m_appImages.persistentLaunchers(pins));
    return group.sync();
}

QUrl DockBackend::applicationLauncher(const QUrl &url)
{
    return AppImageStore::applicationLauncher(url);
}

bool DockBackend::canPinUrls(const QList<QUrl> &urls) const
{
    bool valid = !urls.isEmpty();
    for (const auto &url : urls) {
        const bool accepted = !applicationLauncher(url).isEmpty() || m_appImageStore.isAppImage(url);
        if (property("traceDrops").toBool())
            qInfo() << "drop-trace validate" << "scheme" << url.scheme() << "accepted" << accepted;
        valid = valid && accepted;
    }
    return valid;
}

bool DockBackend::pinTaskAt(int row, int beforeRow)
{
    if (!sourceModel() || row < 0 || row >= rowCount() || beforeRow < 0 || beforeRow >= rowCount()
        || data(index(row, 0), Pinned).toBool() || !data(index(beforeRow, 0), Pinned).toBool()) return false;
    const auto url = taskData(index(row, 0), "LauncherUrlWithoutIcon").toUrl();
    return pinUrls({url}, beforeRow);
}

bool DockBackend::pinUrls(const QList<QUrl> &urls, int beforeRow)
{
    if (property("traceDrops").toBool())
        qInfo() << "drop-trace pin request" << "count" << urls.size() << "beforeRow" << beforeRow;
    if (m_sampleTasks || !sourceModel() || !canPinUrls(urls)) {
        if (property("traceDrops").toBool()) qInfo() << "drop-trace pin rejected";
        return false;
    }
    auto pins = sourceModel()->property("launcherList").toStringList();
    int position = pins.size();
    if (beforeRow >= 0 && beforeRow < rowCount()) {
        const auto before = applicationLauncher(taskData(index(beforeRow, 0), "LauncherUrlWithoutIcon").toUrl());
        for (int i = 0; i < pins.size(); ++i)
            if (applicationLauncher(QUrl(pins[i])) == before) { position = i; break; }
    }
    for (const auto &url : urls) {
        const auto launcher = m_appImages.launcherForPin(url, pins);
        if (launcher.isEmpty()) return false;
        bool duplicate = false;
        for (const auto &pin : pins)
            if (QUrl(pin) == launcher || applicationLauncher(QUrl(pin)) == launcher) { duplicate = true; break; }
        if (!duplicate) pins.insert(position++, launcher.toString());
    }
    if (!sourceModel()->setProperty("launcherList", pins)) {
        if (property("traceDrops").toBool()) qInfo() << "drop-trace launcherList write failed";
        return false;
    }
    if (sourceModel()->property("separateLaunchers").toBool()) {
        sourceModel()->setProperty("separateLaunchers", false);
        sourceModel()->setProperty("separateLaunchers", true);
    }
    pins.removeAll("applications:spacedock-launch-test.desktop");
    KConfigGroup group(m_config, "Launchers");
    group.writeEntry("applications", m_appImages.persistentLaunchers(pins));
    const bool saved = group.sync();
    if (property("traceDrops").toBool())
        qInfo() << "drop-trace pin result" << "saved" << saved << "pinCount" << pins.size();
    return saved;
}

void DockBackend::setInputRegion(QQuickWindow *window, const QVariantList &rectangles, const QVariantList &shelfVertices)
{
    if (!window) return;
    const bool trace = qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_RESIZE");
    QElapsedTimer timer;
    if (trace) timer.start();
    QPolygon polygon;
    for (const auto &vertex : shelfVertices) polygon << vertex.toPointF().toPoint();
    QRegion region(polygon);
    for (const auto &rectangle : rectangles)
        region |= rectangle.toRectF().toAlignedRect();
    if (window->mask() != region) window->setMask(region);
    if (trace && timer.elapsed() >= 8)
        qInfo() << "resize-profile input-region ms" << timer.elapsed() << "rectangles" << region.rectCount();
}

void DockBackend::cacheAppImageIcons()
{
    if (m_sampleTasks || !sourceModel()) return;
    bool changed = false;
    for (int row = 0; row < rowCount(); ++row) {
        const auto task = index(row, 0);
        if (!data(task, Running).toBool()) continue;
        const auto launcher = taskData(task, "LauncherUrlWithoutIcon").toUrl();
        if (!m_appImageStore.cacheIcon(launcher, qvariant_cast<QIcon>(data(task, Qt::DecorationRole)))) continue;
        changed = true;
    }
    if (changed) {
        ++m_artworkRevision;
        ++m_revision;
        emit revisionChanged();
    }
}

QString DockBackend::customIconKey(const QModelIndex &task) const
{
    const auto launcher = taskData(task, "LauncherUrlWithoutIcon").toUrl().toString();
    if (!launcher.isEmpty()) {
        const auto stable = m_appImages.persistentLaunchers({launcher});
        if (!stable.isEmpty()) return stable.first();
    }
    auto key = taskData(task, "AppId").toString();
    if (key.endsWith(".desktop")) key.chop(8);
    return key.isEmpty() ? taskData(task, "AppName").toString() : key;
}

QString DockBackend::customIconPath(const QString &key)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/custom-icons/"
        + QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()) + ".png";
}

static QString iconConfigKey(const QString &key)
{
    return QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantList DockBackend::iconChoices() const
{
    QVariantList choices;
    for (int row = 0; row < rowCount(); ++row) {
        const auto task = index(row, 0);
        if (!data(task, Pinned).toBool() || needsAppImageSetup(task)) continue;
        const auto key = customIconKey(task);
        if (!key.isEmpty()) choices.append(QVariantMap{{"key", key}, {"name", data(task, Name)},
            {"custom", QFileInfo::exists(customIconPath(key)) || KConfigGroup(m_config, "ApplicationIcons").hasKey(iconConfigKey(key))}});
    }
    return choices;
}

bool DockBackend::setCustomIcon(const QString &key, const QUrl &url)
{
    bool valid = false;
    for (const auto &choice : iconChoices()) if (choice.toMap().value("key").toString() == key) valid = true;
    if (!valid || !url.isLocalFile() || !url.host().isEmpty()) return false;
    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    const auto image = reader.read();
    if (image.isNull()) return false;
    const auto path = customIconPath(key);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) return false;
    m_decodedCustomIcons.remove(path);
    KConfigGroup icons(m_config, "ApplicationIcons");
    icons.deleteEntry(iconConfigKey(key));
    if (!icons.sync()) return false;
    ++m_artworkRevision;
    ++m_revision;
    emit revisionChanged();
    return true;
}

bool DockBackend::resetCustomIcon(const QString &key)
{
    bool valid = false;
    for (const auto &choice : iconChoices()) if (choice.toMap().value("key").toString() == key) valid = true;
    if (!valid) return false;
    const auto path = customIconPath(key);
    if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
    m_decodedCustomIcons.remove(path);
    KConfigGroup icons(m_config, "ApplicationIcons");
    icons.deleteEntry(iconConfigKey(key));
    if (!icons.sync()) return false;
    ++m_artworkRevision;
    ++m_revision;
    emit revisionChanged();
    return true;
}

QVariantMap DockBackend::applicationEditorInfo(int row) const
{
    const auto task = index(row, 0);
    if (!task.isValid() || !data(task, Pinned).toBool() || needsAppImageSetup(task)) return {};
    const auto key = customIconKey(task);
    if (key.isEmpty()) return {};
    const QUrl launcher(key);
    QString desktopPath = launcher.isLocalFile() ? launcher.toLocalFile()
        : launcher.scheme() == "applications" ? QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + launcher.path()) : QString();
    QString originalPath;
    if (!desktopPath.isEmpty()) {
        KConfig desktop(desktopPath, KConfig::SimpleConfig);
        originalPath = KConfigGroup(&desktop, "Desktop Entry").readEntry("X-Spacedock-AppImage", QString());
        if (originalPath.isEmpty()) originalPath = desktopPath;
    }
    return {{"key", key}, {"name", data(task, Name)}, {"path", originalPath}, {"row", row},
        {"custom", QFileInfo::exists(customIconPath(key)) || KConfigGroup(m_config, "ApplicationIcons").hasKey(iconConfigKey(key))}};
}

bool DockBackend::saveApplicationEdit(const QString &key, const QString &name, const QUrl &image, bool restore, const QString &themeIcon)
{
    if (name.trimmed().isEmpty()) return false;
    int row = -1;
    for (int i = 0; i < rowCount(); ++i)
        if (applicationEditorInfo(i).value("key").toString() == key) { row = i; break; }
    if (row < 0) return false;
    if (!image.isEmpty() && !setCustomIcon(key, image)) return false;
    if (restore && !resetCustomIcon(key)) return false;
    if (!themeIcon.isEmpty()) {
        if (QIcon::fromTheme(themeIcon).isNull()) return false;
        const auto path = customIconPath(key);
        if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
        m_decodedCustomIcons.remove(path);
        KConfigGroup icons(m_config, "ApplicationIcons");
        icons.writeEntry(iconConfigKey(key), themeIcon);
        if (!icons.sync()) return false;
    }
    KConfigGroup names(m_config, "ApplicationNames");
    names.writeEntry(QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()), name.trimmed());
    if (!names.sync()) return false;
    ++m_artworkRevision;
    ++m_revision;
    emit dataChanged(index(row, 0), index(row, 0), {Name});
    emit revisionChanged();
    return true;
}

QPixmap DockBackend::icon(int row, QSize size) const
{
    if (needsAppImageSetup(index(row, 0)))
        return QIcon(":/assets/icons/appimage_closed.png").pixmap(size, 1.0);
    const auto key = customIconKey(index(row, 0));
    const auto themeIcon = KConfigGroup(m_config, "ApplicationIcons").readEntry(iconConfigKey(key), QString());
    if (!themeIcon.isEmpty()) {
        const auto themed = QIcon::fromTheme(themeIcon).pixmap(size, 1.0);
        if (!themed.isNull()) return themed;
    }
    if (!key.isEmpty() && QFileInfo::exists(customIconPath(key))) {
        const bool trace = qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_RESIZE");
        QElapsedTimer timer;
        if (trace) timer.start();
        const auto path = customIconPath(key);
        const auto cached = m_decodedCustomIcons.object(path);
        const QImage image = cached ? *cached : QImage(path);
        const auto decodeMs = trace ? timer.elapsed() : 0;
        if (!cached && !image.isNull()) {
            const auto kib = (image.sizeInBytes() + 1023) / 1024;
            if (kib <= m_decodedCustomIcons.maxCost())
                m_decodedCustomIcons.insert(path, new QImage(image), int(kib));
        }
        const auto custom = QPixmap::fromImage(image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (trace) qInfo() << "resize-profile custom-image decode ms" << decodeMs
                           << "scale-convert ms" << timer.elapsed() - decodeMs << "cache-hit" << bool(cached)
                           << "source" << image.size() << "requested" << size;
        if (!custom.isNull()) return custom;
    }
    return defaultIcon(row, size);
}

QPixmap DockBackend::defaultIcon(int row, QSize size) const
{
    if (needsAppImageSetup(index(row, 0))) return QIcon(":/assets/icons/appimage_closed.png").pixmap(size, 1.0);
    const auto launcher = taskData(index(row, 0), "LauncherUrlWithoutIcon").toUrl();
    const auto cached = m_appImageStore.cachedIcon(launcher, size);
    if (!cached.isNull()) return cached;
    auto icon = qvariant_cast<QIcon>(data(index(row, 0), Qt::DecorationRole));
    if (icon.isNull()) icon = QIcon::fromTheme("application-x-executable");
    return icon.pixmap(size, 1.0);
}

QString DockBackend::iconCacheIdentity(int row, bool defaultOnly) const
{
    const auto task = index(row, 0);
    if (!task.isValid()) return QStringLiteral("invalid/%1").arg(row);
    const auto icon = qvariant_cast<QIcon>(data(task, Qt::DecorationRole));
    const auto launcher = taskData(task, "LauncherUrlWithoutIcon").toUrl().toString();
    const auto customKey = customIconKey(task);
    // Model revisions cover many state changes; only artwork changes belong in
    // the raster cache key. Explicit edits and AppImage updates bump this epoch.
    return QString::fromLatin1(QCryptographicHash::hash(
        (QString::number(m_artworkRevision) + QChar(0) + QString::number(defaultOnly)
         + QChar(0) + QString::number(needsAppImageSetup(task)) + QChar(0) + launcher
         + QChar(0) + customKey + QChar(0) + QString::number(icon.cacheKey())
         + QChar(0) + QIcon::themeName()).toUtf8(), QCryptographicHash::Sha256).toHex());
}

QPixmap TaskIcons::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QSize pixels = requestedSize.isValid() ? requestedSize : QSize(64, 64);
    const bool theme = id.startsWith("theme/");
    const bool defaultOnly = id.startsWith("default/");
    const QString identity = theme ? id : m_backend->iconCacheIdentity(id.section('/', -1).toInt(), defaultOnly);
    const QString key = identity + QLatin1Char('/') + QString::number(pixels.width()) + QLatin1Char('x') + QString::number(pixels.height());
    if (const auto *cached = m_cache.object(key)) {
        if (size) *size = cached->size();
        return *cached;
    }
    const bool trace = qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_RESIZE");
    QElapsedTimer timer;
    if (trace) timer.start();
    const auto pixmap = id.startsWith("theme/") ? QIcon::fromTheme(QUrl::fromPercentEncoding(id.mid(6).toUtf8())).pixmap(pixels, 1.0)
        : id.startsWith("default/") ? m_backend->defaultIcon(id.section('/', -1).toInt(), pixels)
        : m_backend->icon(id.section('/', -1).toInt(), pixels);
    if (trace) qInfo() << "resize-profile icon ms" << timer.elapsed() << "requested" << requestedSize << "result" << pixmap.size();
    if (size) *size = pixmap.size();
    if (!pixmap.isNull()) m_cache.insert(key, new QPixmap(pixmap), qMax(1, int((qint64(pixmap.width()) * pixmap.height() * 4 + 1023) / 1024)));
    return pixmap;
}

QList<AppImageTask> DockBackend::appImageTasks() const
{
    QList<AppImageTask> tasks;
    tasks.reserve(rowCount());
    for (int row = 0; row < rowCount(); ++row)
        tasks.append(appImageTask(mapToSource(index(row, 0))));
    return tasks;
}

AppImageTask DockBackend::appImageTask(const QPersistentModelIndex &handle) const
{
    if (!sourceModel() || !handle.isValid() || handle.model() != sourceModel()) return {};
    const auto task = mapFromSource(handle);
    if (!task.isValid()) return {};
    return {handle, task.row(), data(task, AppKey).toString(), data(task, Name).toString(),
        taskData(task, "AppId").toString(), taskData(task, "LauncherUrlWithoutIcon").toUrl(),
        qvariant_cast<QIcon>(data(task, Qt::DecorationRole)), taskData(task, "AppPid").toUInt(),
        data(task, Running).toBool(), needsAppImageSetup(task)};
}

QStringList DockBackend::appImagePins() const
{
    return sourceModel() ? sourceModel()->property("launcherList").toStringList() : QStringList();
}

bool DockBackend::addAppImagePin(const QUrl &url, bool &added)
{
    return sourceModel() && QMetaObject::invokeMethod(sourceModel(), "requestAddLauncher",
        Q_RETURN_ARG(bool, added), Q_ARG(QUrl, url));
}

bool DockBackend::replaceAppImagePins(const QStringList &pins)
{
    return sourceModel() && sourceModel()->setProperty("launcherList", pins);
}

void DockBackend::refreshAppImagePins()
{
    if (sourceModel() && sourceModel()->property("separateLaunchers").toBool()) {
        sourceModel()->setProperty("separateLaunchers", false);
        sourceModel()->setProperty("separateLaunchers", true);
    }
}

bool DockBackend::moveAppImageTask(const QPersistentModelIndex &handle, int destination)
{
    if (!sourceModel() || !handle.isValid() || handle.model() != sourceModel()) return false;
    bool moved = false;
    QMetaObject::invokeMethod(sourceModel(), "move", Q_RETURN_ARG(bool, moved),
        Q_ARG(int, handle.row()), Q_ARG(int, destination), Q_ARG(QModelIndex, QModelIndex()));
    return moved;
}

bool DockBackend::saveAppImagePins(const QStringList &persistentPins)
{
    KConfigGroup group(m_config, "Launchers");
    group.writeEntry("applications", persistentPins);
    return group.sync();
}

void DockBackend::invalidateAppImageArtwork()
{
    ++m_artworkRevision;
    ++m_revision;
    emit revisionChanged();
}

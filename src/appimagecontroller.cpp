#include "appimagecontroller.h"
#include <QFile>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QDebug>

AppImageController::AppImageController(AppImageTaskAdapter &tasks, AppImageStore &store,
    bool sampleTasks, QObject *parent)
    : QObject(parent), m_tasks(tasks), m_store(store), m_sampleTasks(sampleTasks)
{
    m_setupTimer.setSingleShot(true);
    m_setupTimer.setInterval(2000);
    connect(&m_setupTimer, &QTimer::timeout, this, [this] {
        reconcile();
        update();
        if (!pending()) return;
        m_setupVisible = true;
        emit setupChanged();
    });
}

void AppImageController::begin(const AppImageTask &task)
{
    if (task.row < 0 || !task.needsSetup) return;
    m_setupTimer.stop();
    m_setupLauncher = task.launcher.toString();
    m_setupKey = task.key;
    m_setupName = QFileInfo(m_store.generatedAppImagePath(task.launcher)).completeBaseName();
    m_setupError.clear();
    m_setupVisible = false;
    m_setupExistingApps.clear();
    for (const auto &current : m_tasks.appImageTasks())
        if (current.running) m_setupExistingApps.insert(current.key);
    emit setupChanged();
}

void AppImageController::show(int row)
{
    if (m_sampleTasks) return;
    for (const auto &task : m_tasks.appImageTasks()) {
        if (task.row != row || !task.needsSetup) continue;
        if (m_setupLauncher != task.launcher.toString()) begin(task);
        m_setupTimer.stop();
        m_setupVisible = true;
        emit setupChanged();
        return;
    }
}

bool AppImageController::prepareActivation(int row)
{
    if (m_sampleTasks) return false;
    for (const auto &task : m_tasks.appImageTasks()) {
        if (task.row != row) continue;
        if (task.needsSetup) {
            if (m_setupLauncher == task.launcher.toString()) {
                m_setupTimer.stop();
                m_setupVisible = true;
                emit setupChanged();
                return false;
            }
            begin(task);
            m_setupTimer.start();
        }
        return true;
    }
    return false;
}

void AppImageController::cancel()
{
    if (!pending()) return;
    m_setupTimer.stop();
    m_setupLauncher.clear();
    m_setupKey.clear();
    m_setupName.clear();
    m_setupError.clear();
    m_setupVisible = false;
    m_setupExistingApps.clear();
    emit setupChanged();
}

void AppImageController::update()
{
    if (!pending() || !m_tasks.isReady()) return;
    if (!m_tasks.appImagePins().contains(m_setupLauncher)) {
        cancel();
        return;
    }
    emit setupChanged();
}

QVariantList AppImageController::candidates() const
{
    QVariantList recent, existing;
    if (!pending() || !m_tasks.isReady()) return recent;
    for (const auto &task : m_tasks.appImageTasks()) {
        if (!task.running || task.needsSetup) continue;
        const bool wasRunning = m_setupExistingApps.contains(task.key);
        const QVariantMap candidate{{"key", task.key}, {"name", task.name}, {"wasRunning", wasRunning},
            {"iconUrl", QString("image://tasks/%1/%2").arg(m_tasks.revision()).arg(task.row)}};
        (wasRunning ? existing : recent).append(candidate);
    }
    recent.append(existing);
    return recent;
}

QUrl AppImageController::launcherForPin(const QUrl &url, const QStringList &pins) const
{
    const bool image = m_store.isAppImage(url);
    auto launcher = image ? m_store.knownAppImageLauncher(QFileInfo(url.toLocalFile()).canonicalFilePath())
                          : AppImageStore::applicationLauncher(url);
    if (launcher.isEmpty() && image) launcher = m_store.appImageLauncher(url);
    return runtimeLauncher(launcher, pins);
}

QUrl AppImageController::runtimeLauncher(const QUrl &persistent, const QStringList &pins) const
{
    for (auto binding = m_confirmedRuntimeLaunchers.cbegin(); binding != m_confirmedRuntimeLaunchers.cend(); ++binding)
        if (binding.value().launcher == persistent.toString() && pins.contains(binding.key()))
            return QUrl(binding.key());
    return persistent;
}
bool AppImageController::confirm(const QString &candidateKey)
{
    if (m_sampleTasks || !m_tasks.isReady() || !pending()) return false;
    auto fail = [this](const QString &message) {
        m_setupError = message;
        emit setupChanged();
        return false;
    };
    if (m_reconcilingAppImages) return fail(tr("Dock is updating. Please try again."));
    // Saving the desktop entry can dispatch cache/model updates. Keep automatic
    // reconciliation from replacing this same placeholder mid-confirmation.
    QScopedValueRollback confirmation(m_reconcilingAppImages, true);
    AppImageTask selectedTask;
    for (const auto &task : m_tasks.appImageTasks()) {
        if (task.running && !task.needsSetup && task.key == candidateKey) {
            if (selectedTask.row >= 0) return fail(tr("Multiple matches. Please choose again."));
            selectedTask = task;
        }
    }
    if (selectedTask.row < 0) return fail(tr("App closed. Choose another application."));
    const auto original = m_setupLauncher;
    const auto imagePath = m_store.generatedAppImagePath(QUrl(original));
    auto pins = m_tasks.appImagePins();
    if (!pins.contains(original)) return fail(tr("The AppImage placeholder is no longer in the dock."));
    const auto runningUrl = selectedTask.launcher;
    const auto runningLauncher = AppImageStore::applicationLauncher(runningUrl);
    const auto selected = selectedTask.handle;
    auto resolved = runningLauncher;
    if (resolved.scheme() != "applications") {
        auto identity = selectedTask.identity;
        if (identity.endsWith(".desktop")) identity.chop(8);
        resolved = m_store.confirmedAppImageLauncher(imagePath, identity, selectedTask.name,
            selectedTask.icon);
    }
    if (resolved.isEmpty()) return fail(tr("Could not save the application launcher. Please try again."));
    // Pin the exact running launcher now, as Keep in Dock would. An indexed
    // persistent copy is saved for later; do not rely on an already-open
    // window immediately rediscovering changed desktop metadata.
    const auto requested = runningUrl.isEmpty() ? resolved : runningUrl;
    const auto originalKey = m_setupKey;
    int destination = -1;
    for (const auto &task : m_tasks.appImageTasks())
        if (task.key == originalKey) { destination = task.row; break; }
    if (destination < 0) return fail(tr("The AppImage placeholder is no longer in the dock."));
    if (selectedTask.row < destination) --destination;
    bool added = false;
    if (!m_tasks.addAppImagePin(requested, added))
        return fail(tr("Could not pin the selected application."));
    // Native Keep in Dock updates task/launcher association immediately. The
    // native model may canonicalize its URL; use its accepted value verbatim.
    const auto nativePins = m_tasks.appImagePins();
    QUrl live;
    for (const auto &pin : nativePins) {
        if (QUrl(pin) == requested) { live = QUrl(pin); break; }
        if (!pins.contains(pin)) live = QUrl(pin);
        else if (!runningLauncher.isEmpty() && AppImageStore::applicationLauncher(QUrl(pin)) == runningLauncher) live = QUrl(pin);
    }
    if (live.isEmpty()) return fail(tr("Could not pin the selected application."));
    auto stableKey = resolved.fileName();
    if (stableKey.endsWith(".desktop")) stableKey.chop(8);
    const auto currentKey = selected.isValid() ? m_tasks.appImageTask(selected).key : candidateKey;
    emit replacementPrepared(originalKey, currentKey, stableKey, destination);
    const auto previousBinding = m_confirmedRuntimeLaunchers.value(live.toString());
    if (live != resolved) m_confirmedRuntimeLaunchers.insert(live.toString(), {resolved.toString(), currentKey});
    QStringList replacement;
    for (const auto &pin : pins) {
        if (pin == original) replacement.append(live.toString());
        else if (QUrl(pin) != resolved && QUrl(pin) != live
                 && (runningLauncher.isEmpty() || AppImageStore::applicationLauncher(QUrl(pin)) != runningLauncher))
            replacement.append(pin);
    }
    if (!m_tasks.replaceAppImagePins(replacement)) {
        if (added) m_tasks.replaceAppImagePins(pins);
        emit replacementAborted(originalKey);
        if (previousBinding.launcher.isEmpty()) m_confirmedRuntimeLaunchers.remove(live.toString());
        else m_confirmedRuntimeLaunchers.insert(live.toString(), previousBinding);
        return fail(tr("Could not replace the dock placeholder."));
    }
    m_tasks.refreshAppImagePins();
    if (selected.isValid()) {
        const auto current = m_tasks.appImageTask(selected);
        if (current.row != destination) {
            const bool moved = m_tasks.moveAppImageTask(selected, destination);
            if (m_tasks.appImageTracing()) qInfo() << "appimage-trace placement" << "destination" << destination << "moved" << moved;
        }
    }
    replacement.removeAll("applications:spacedock-launch-test.desktop");
    if (!m_tasks.saveAppImagePins(persistentLaunchers(replacement)))
        return fail(tr("Could not save the dock configuration."));
    m_store.saveAssociation(QUrl(original), resolved, true);
    cancel();
    m_tasks.invalidateAppImageArtwork();
    return true;
}

QStringList AppImageController::persistentLaunchers(const QStringList &pins) const
{
    QStringList saved;
    for (const auto &pin : pins) {
        const auto stable = m_confirmedRuntimeLaunchers.contains(pin) ? m_confirmedRuntimeLaunchers.value(pin).launcher : pin;
        if (stable != "applications:spacedock-launch-test.desktop" && !saved.contains(stable)) saved.append(stable);
    }
    return saved;
}

void AppImageController::settleConfirmedLaunchers()
{
    if (!m_tasks.isReady() || m_confirmedRuntimeLaunchers.isEmpty()) return;
    auto pins = m_tasks.appImagePins();
    QStringList finished;
    for (auto binding = m_confirmedRuntimeLaunchers.cbegin(); binding != m_confirmedRuntimeLaunchers.cend(); ++binding) {
        bool running = false;
        for (const auto &task : m_tasks.appImageTasks()) {
            if (task.running && (task.key == binding.value().appKey
                || task.launcher.adjusted(QUrl::RemoveQuery) == QUrl(binding.key()).adjusted(QUrl::RemoveQuery))) {
                running = true;
                break;
            }
        }
        if (running) continue;
        const int position = pins.indexOf(binding.key());
        if (position >= 0) pins[position] = binding.value().launcher;
        finished.append(binding.key());
    }
    if (finished.isEmpty() || !m_tasks.replaceAppImagePins(pins)) return;
    m_tasks.refreshAppImagePins();
    for (const auto &runtime : finished) m_confirmedRuntimeLaunchers.remove(runtime);
    m_tasks.saveAppImagePins(persistentLaunchers(pins));
}

void AppImageController::forget(const QUrl &launcher)
{
    const auto binding = m_confirmedRuntimeLaunchers.constFind(launcher.toString());
    const auto stable = binding == m_confirmedRuntimeLaunchers.cend() ? launcher : QUrl(binding.value().launcher);
    if (!m_store.forgetAssociation(launcher, stable)) return;
    for (auto binding = m_confirmedRuntimeLaunchers.begin(); binding != m_confirmedRuntimeLaunchers.end();) {
        if (binding.key() == launcher.toString() || binding.value().launcher == stable.toString())
            binding = m_confirmedRuntimeLaunchers.erase(binding);
        else ++binding;
    }
    m_store.refreshDesktopCache();
}

void AppImageController::reconcile()
{
    if (m_sampleTasks || !m_tasks.isReady() || m_reconcilingAppImages) return;
    QScopedValueRollback guard(m_reconcilingAppImages, true);
    settleConfirmedLaunchers();
    const auto pins = m_tasks.appImagePins();
    QHash<QString, QString> replacements;
    for (const auto &pin : pins) {
        const auto imagePath = m_store.generatedAppImagePath(QUrl(pin));
        if (imagePath.isEmpty()) continue;
        if (m_store.requiresSelection(imagePath)) continue;
        auto resolved = m_store.knownAppImageLauncher(imagePath);
        if (resolved == QUrl(pin)) continue;
        if (resolved.isEmpty()) for (const auto &task : m_tasks.appImageTasks()) {
            if (!task.running) continue;
            const auto runningLauncher = AppImageStore::applicationLauncher(task.launcher);
            const auto launcherImage = m_store.launcherExecutable(runningLauncher);
            if (launcherImage != imagePath && processAppImage(task.pid) != imagePath) continue;
            if (runningLauncher.scheme() == "applications") {
                resolved = runningLauncher;
            } else {
                auto identity = task.identity;
                if (identity.endsWith(".desktop")) identity.chop(8);
                if (identity.isEmpty()) continue;
                if (m_store.appImageDesktopPath(imagePath, identity) == QUrl(pin).toLocalFile()) break;
                resolved = m_store.appImageLauncher(QUrl::fromLocalFile(imagePath), identity);
                if (resolved.isEmpty()) continue;
                m_store.updatePresentation(resolved, task.name, task.icon);
            }
            if (pins.contains(runningLauncher.toString()) && runningLauncher != resolved)
                replacements.insert(runningLauncher.toString(), resolved.toString());
            break;
        }
        if (!resolved.isEmpty() && resolved != QUrl(pin)) {
            replacements.insert(pin, resolved.toString());
            m_store.saveAssociation(QUrl::fromLocalFile(m_store.appImageDesktopPath(imagePath)), resolved);
        }
    }
    if (replacements.isEmpty()) return;
    QStringList normalized;
    for (const auto &pin : pins) {
        const auto resolved = replacements.value(pin, pin);
        if (!normalized.contains(resolved)) normalized.append(resolved);
    }
    const auto setupKey = m_setupKey;
    const auto replacementUrl = QUrl(replacements.value(m_setupLauncher));
    if (!replacementUrl.isEmpty()) {
        auto targetKey = replacementUrl.fileName();
        if (targetKey.endsWith(".desktop")) targetKey.chop(8);
        auto runningKey = targetKey;
        for (const auto &task : m_tasks.appImageTasks())
            if (task.running && AppImageStore::applicationLauncher(task.launcher) == replacementUrl)
                runningKey = task.key;
        int destination = -1;
        for (const auto &task : m_tasks.appImageTasks())
            if (task.key == setupKey) { destination = task.row; break; }
        emit replacementPrepared(setupKey, runningKey, targetKey, destination);
    }
    if (!m_tasks.replaceAppImagePins(normalized)) {
        if (!replacementUrl.isEmpty()) emit replacementAborted(setupKey);
        return;
    }
    m_tasks.refreshAppImagePins();
    normalized.removeAll("applications:spacedock-launch-test.desktop");
    m_tasks.saveAppImagePins(persistentLaunchers(normalized));
    if (m_tasks.appImageTracing()) qInfo() << "appimage-trace reconciled" << replacements.size() << "launcher identities";
}

QString AppImageController::processAppImage(quint32 pid)
{
    if (!pid) return {};
    QFile environment(QString("/proc/%1/environ").arg(pid));
    if (!environment.open(QIODevice::ReadOnly)) return {};
    // Only use the exact AppImage path, never a process name/title heuristic.
    for (const auto &value : environment.readAll().split('\0')) {
        if (value.startsWith("APPIMAGE=")) return QFileInfo(QString::fromLocal8Bit(value.mid(9))).canonicalFilePath();
    }
    return {};
}

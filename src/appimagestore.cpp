#include "appimagestore.h"
#include <KConfig>
#include <KConfigGroup>
#include <KShell>
#include <KApplicationTrader>
#include <KService>
#include <KSycoca>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

bool AppImageStore::isAppImage(const QUrl &url)
{
    if (!url.isLocalFile() || !url.host().isEmpty()) return false;
    const QFileInfo file(url.toLocalFile());
    return file.suffix().compare("AppImage", Qt::CaseInsensitive) == 0
        && file.isFile() && file.isReadable() && file.isExecutable();
}

QString AppImageStore::appImageDesktopPath(const QString &path, const QString &identity)
{
    const auto hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex();
    const auto base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + "/launchers/appimage-" + QString::fromLatin1(hash);
    if (!identity.isEmpty() && !identity.contains('/') && !identity.contains('\\') && identity != "." && identity != "..")
        return base + '/' + identity + ".desktop";
    return base + ".desktop";
}

bool AppImageStore::isGeneratedAppImageLauncher(const QUrl &url)
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/launchers";
    const QFileInfo file(url.toLocalFile());
    return url.isLocalFile() && url.host().isEmpty() && (file.absolutePath() == directory || file.absolutePath().startsWith(directory + "/appimage-"))
        && (file.fileName().startsWith("appimage-") || file.absolutePath() != directory) && file.suffix() == "desktop";
}

static QString desktopArgument(QString value)
{
    value.replace('\\', "\\\\").replace('"', "\\\"").replace('`', "\\`").replace('$', "\\$").replace('%', "%%");
    return '"' + value + '"';
}

QUrl AppImageStore::appImageLauncher(const QUrl &url, const QString &identity)
{
    if (!isAppImage(url)) return {};
    const QFileInfo file(url.toLocalFile());
    const QString path = file.canonicalFilePath();
    const auto desktopPath = appImageDesktopPath(path, identity);
    if (!QDir().mkpath(QFileInfo(desktopPath).absolutePath())) return {};
    KConfig desktop(desktopPath, KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    entry.writeEntry("Type", "Application");
    if (identity.isEmpty() || entry.readEntry("Name", QString()).isEmpty())
        entry.writeEntry("Name", file.completeBaseName());
    // Plasma can otherwise identify an AppImage by its temporary mounted
    // desktop file, creating a separate task from our persistent launcher.
    entry.writeEntry("Exec", "env " + desktopArgument("BAMF_DESKTOP_FILE_HINT=" + desktopPath)
        + ' ' + desktopArgument(path));
    entry.writeEntry("X-Spacedock-AppImage", path);
    if (!identity.isEmpty()) entry.writeEntry("X-Spacedock-AppId", identity);
    // Leave the fallback icon to the running window until we have cached it.
    if (QFileInfo::exists(desktopPath + ".png")) entry.writeEntry("Icon", desktopPath + ".png");
    else if (entry.readEntry("Icon", QString()) == "application-x-executable") entry.deleteEntry("Icon");
    entry.writeEntry("Terminal", false);
    if (!entry.sync()) return {};
    if (!QFile::setPermissions(desktopPath, QFile::permissions(desktopPath) | QFile::ExeOwner)) return {};
    return QUrl::fromLocalFile(desktopPath);
}

static QString appImageExecutable(const QString &command)
{
    KShell::Errors error;
    auto args = KShell::splitArgs(command, KShell::AbortOnMeta, &error);
    if (error != KShell::NoError || args.isEmpty()) return {};
    if (QFileInfo(args.first()).fileName() == "env") {
        args.removeFirst();
        while (!args.isEmpty() && args.first().contains('=')) args.removeFirst();
    }
    if (args.isEmpty()) return {};
    auto path = args.first();
    path.replace("%%", "%");
    return AppImageStore::isAppImage(QUrl::fromLocalFile(path)) ? QFileInfo(path).canonicalFilePath() : QString();
}

static QUrl registeredAppImageLauncher(const QString &imagePath)
{
    const auto services = KApplicationTrader::query([&imagePath](const KService::Ptr &service) {
        return !service->property<bool>(QStringLiteral("Hidden")) && appImageExecutable(service->exec()) == imagePath;
    });
    for (const auto &service : services) {
        if (!service->menuId().isEmpty()) return QUrl("applications:" + service->menuId());
    }
    return {};
}

QString AppImageStore::generatedAppImagePath(const QUrl &launcher)
{
    if (!isGeneratedAppImageLauncher(launcher)) return {};
    KConfig desktop(launcher.toLocalFile(), KConfig::SimpleConfig);
    const KConfigGroup entry(&desktop, "Desktop Entry");
    const auto stored = entry.readEntry("X-Spacedock-AppImage", QString());
    return stored.isEmpty() ? appImageExecutable(entry.readEntry("Exec", QString())) : QFileInfo(stored).canonicalFilePath();
}

QUrl AppImageStore::knownAppImageLauncher(const QString &imagePath)
{
    const auto original = appImageDesktopPath(imagePath);
    if (QFileInfo::exists(original)) {
        KConfig desktop(original, KConfig::SimpleConfig);
        if (KConfigGroup(&desktop, "Desktop Entry").readEntry("X-Spacedock-RequireSelection", false)) return {};
    }
    const auto registered = registeredAppImageLauncher(imagePath);
    if (!registered.isEmpty()) return registered;
    if (!QFileInfo::exists(original)) return {};
    KConfig desktop(original, KConfig::SimpleConfig);
    const QUrl learned(KConfigGroup(&desktop, "Desktop Entry").readEntry("X-Spacedock-ResolvedLauncher", QString()));
    if (learned.scheme() == "applications") {
        const auto path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + learned.path());
        if (!path.isEmpty()) {
            KConfig installed(path, KConfig::SimpleConfig);
            if (QFileInfo(KConfigGroup(&installed, "Desktop Entry").readEntry("X-Spacedock-AppImage", QString())).canonicalFilePath() == imagePath)
                return applicationLauncher(learned);
        }
    }
    if (generatedAppImagePath(learned) != imagePath) return {};
    return applicationLauncher(learned);
}

static bool saveAppImageIcon(const QString &desktopPath, const QIcon &icon)
{
    if (icon.isNull() || icon.name() == "application-x-executable"
        || icon.name() == "unknown" || icon.name() == "application-default-icon") return false;
    const auto pixmap = icon.pixmap(QSize(256, 256));
    if (pixmap.isNull()) return false;
    const auto iconPath = desktopPath + ".png";
    QSaveFile image(iconPath);
    if (!image.open(QIODevice::WriteOnly) || !pixmap.save(&image, "PNG") || !image.commit()) return false;
    KConfig desktop(desktopPath, KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    entry.writeEntry("Icon", iconPath);
    return entry.sync();
}

void AppImageStore::refreshAppImageLauncher(const QUrl &url)
{
    if (!isGeneratedAppImageLauncher(url)) return;
    KConfig desktop(url.toLocalFile(), KConfig::SimpleConfig);
    const KConfigGroup entry(&desktop, "Desktop Entry");
    auto path = entry.readEntry("X-Spacedock-AppImage", QString());
    if (path.isEmpty()) {
        // Upgrade the original single-argument launchers without running them.
        KShell::Errors error;
        const auto args = KShell::splitArgs(entry.readEntry("Exec", QString()), KShell::AbortOnMeta, &error);
        if (error != KShell::NoError || args.size() != 1) return;
        path = args.first();
        path.replace("%%", "%");
    }
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto identity = entry.readEntry("X-Spacedock-AppId", QString());
    if (canonical.isEmpty() || appImageDesktopPath(canonical, identity) != url.toLocalFile()) return;
    appImageLauncher(QUrl::fromLocalFile(canonical), identity);
}

QUrl AppImageStore::confirmedAppImageLauncher(const QString &imagePath, const QString &identity,
    const QString &name, const QIcon &icon)
{
    if (identity.isEmpty() || !isAppImage(QUrl::fromLocalFile(imagePath))) return {};
    const auto original = appImageDesktopPath(imagePath);
    const auto desktopId = QFileInfo(original).fileName().replace("appimage-", "spacedock-appimage-");
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
    if (!QDir().mkpath(directory)) return {};
    const auto path = directory + '/' + desktopId;
    KConfig desktop(path, KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    if (QFileInfo::exists(path) && entry.readEntry("X-Spacedock-AppImage", QString()) != imagePath) return {};
    saveAppImageIcon(original, icon);
    entry.writeEntry("Type", "Application");
    entry.writeEntry("Name", name.isEmpty() ? QFileInfo(imagePath).completeBaseName() : name);
    entry.writeEntry("Exec", "env " + desktopArgument("BAMF_DESKTOP_FILE_HINT=" + path) + ' ' + desktopArgument(imagePath));
    entry.writeEntry("StartupWMClass", identity);
    entry.writeEntry("X-Spacedock-AppImage", imagePath);
    entry.deleteEntry("Hidden");
    entry.writeEntry("NoDisplay", true);
    entry.writeEntry("Terminal", false);
    entry.writeEntry("Icon", QFileInfo::exists(original + ".png") ? original + ".png"
        : icon.name().isEmpty() ? QString("application-x-executable") : icon.name());
    if (!entry.sync()) return {};
    refreshDesktopCache();
    return QUrl("applications:" + desktopId);
}

QUrl AppImageStore::applicationLauncher(const QUrl &url)
{
    QString path;
    if (url.scheme() == "applications") {
        const auto id = url.path();
        if (id.contains('/') || !id.endsWith(".desktop")) return {};
        path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + id);
    } else if (url.isLocalFile() && url.host().isEmpty()) {
        path = url.toLocalFile();
    } else return {};
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable() || !file.fileName().endsWith(".desktop")) return {};
    KConfig desktop(file.absoluteFilePath(), KConfig::SimpleConfig);
    const KConfigGroup entry(&desktop, "Desktop Entry");
    if (entry.readEntry("Type", QString()) != "Application"
        || entry.readEntry("Hidden", false)
        || entry.readEntry("Name", QString()).isEmpty()
        || (entry.readEntry("Exec", QString()).isEmpty() && !entry.readEntry("DBusActivatable", false))) return {};
    // Match installed desktop IDs so menu and file-manager drags deduplicate.
    for (const auto &root : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        const auto relative = QDir(root + "/applications").relativeFilePath(file.absoluteFilePath());
        if (relative.startsWith("../") || QDir::isAbsolutePath(relative)) continue;
        auto id = relative;
        id.replace('/', '-');
        return QUrl("applications:" + id);
    }
    return QUrl::fromLocalFile(file.canonicalFilePath());
}

bool AppImageStore::forgetAssociation(const QUrl &launcher, const QUrl &stable)
{
    QSet<QString> images;
    auto desktopPath = [](const QUrl &url) {
        if (url.isLocalFile() && url.host().isEmpty()) return url.toLocalFile();
        if (url.scheme() == "applications")
            return QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + url.path());
        return QString();
    };
    const auto path = desktopPath(stable);
    if (!path.isEmpty()) {
        KConfig desktop(path, KConfig::SimpleConfig);
        const KConfigGroup entry(&desktop, "Desktop Entry");
        auto image = entry.readEntry("X-Spacedock-AppImage", QString());
        if (image.isEmpty()) image = appImageExecutable(entry.readEntry("Exec", QString()));
        if (!image.isEmpty()) images.insert(image);
    }
    // A manually chosen registered app need not launch the AppImage itself.
    // Find its reverse association without touching that application's file.
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/launchers");
    for (const auto &file : directory.entryInfoList({"appimage-*.desktop"}, QDir::Files)) {
        KConfig desktop(file.absoluteFilePath(), KConfig::SimpleConfig);
        const KConfigGroup entry(&desktop, "Desktop Entry");
        const QUrl linked(entry.readEntry("X-Spacedock-ResolvedLauncher", QString()));
        if (linked == stable || linked == launcher) {
            const auto image = entry.readEntry("X-Spacedock-AppImage", QString());
            if (!image.isEmpty()) images.insert(image);
        }
    }
    for (const auto &image : images) {
        const auto original = appImageDesktopPath(image);
        if (!QFileInfo::exists(original) && appImageLauncher(QUrl::fromLocalFile(image)).isEmpty()) continue;
        KConfig desktop(original, KConfig::SimpleConfig);
        KConfigGroup entry(&desktop, "Desktop Entry");
        entry.deleteEntry("X-Spacedock-ResolvedLauncher");
        entry.deleteEntry("X-Spacedock-AppId");
        entry.deleteEntry("Icon");
        entry.writeEntry("X-Spacedock-RequireSelection", true);
        entry.sync();
        QFile::remove(original + ".png");
        m_cachedIcons.remove(original);
        const auto ownedId = QFileInfo(original).fileName().replace("appimage-", "spacedock-appimage-");
        const auto ownedPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications/" + ownedId;
        if (QFileInfo::exists(ownedPath)) {
            KConfig owned(ownedPath, KConfig::SimpleConfig);
            KConfigGroup ownedEntry(&owned, "Desktop Entry");
            if (ownedEntry.readEntry("X-Spacedock-AppImage", QString()) == image) {
                ownedEntry.writeEntry("Hidden", true);
                ownedEntry.sync();
            }
        }
    }
    if (images.isEmpty()) return false;
    return true;
}

bool AppImageStore::needsSetup(const QUrl &launcher)
{
    if (!isGeneratedAppImageLauncher(launcher)) return false;
    KConfig desktop(launcher.toLocalFile(), KConfig::SimpleConfig);
    const KConfigGroup entry(&desktop, "Desktop Entry");
    return !generatedAppImagePath(launcher).isEmpty()
        && entry.readEntry("X-Spacedock-AppId", QString()).isEmpty()
        && entry.readEntry("X-Spacedock-ResolvedLauncher", QString()).isEmpty();
}

bool AppImageStore::requiresSelection(const QString &imagePath)
{
    KConfig desktop(appImageDesktopPath(imagePath), KConfig::SimpleConfig);
    return KConfigGroup(&desktop, "Desktop Entry").readEntry("X-Spacedock-RequireSelection", false);
}

QString AppImageStore::launcherExecutable(const QUrl &launcher)
{
    if (!launcher.isLocalFile()) return {};
    KConfig desktop(launcher.toLocalFile(), KConfig::SimpleConfig);
    return appImageExecutable(KConfigGroup(&desktop, "Desktop Entry").readEntry("Exec", QString()));
}

void AppImageStore::updatePresentation(const QUrl &launcher, const QString &name, const QIcon &icon)
{
    if (!isGeneratedAppImageLauncher(launcher)) return;
    KConfig desktop(launcher.toLocalFile(), KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    if (!name.isEmpty()) entry.writeEntry("Name", name);
    entry.sync();
    saveAppImageIcon(launcher.toLocalFile(), icon);
}

void AppImageStore::saveAssociation(const QUrl &original, const QUrl &resolved, bool confirmed)
{
    if (!isGeneratedAppImageLauncher(original)) return;
    if (!confirmed && !QFileInfo::exists(original.toLocalFile())) return;
    KConfig desktop(original.toLocalFile(), KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    entry.writeEntry("X-Spacedock-ResolvedLauncher", resolved.toString());
    if (confirmed) entry.deleteEntry("X-Spacedock-RequireSelection");
    entry.sync();
}

bool AppImageStore::cacheIcon(const QUrl &launcher, const QIcon &icon)
{
    if (!isGeneratedAppImageLauncher(launcher)) return false;
    if (requiresSelection(generatedAppImagePath(launcher))) return false;
    const auto path = launcher.toLocalFile();
    if (icon.isNull() || icon.name() == "application-x-executable"
        || icon.name() == "unknown" || icon.name() == "application-default-icon") return false;
    if (m_cachedIcons.value(path) == icon.cacheKey()) return false;
    if (icon.name() == path + ".png") return false;
    if (!saveAppImageIcon(path, icon)) return false;
    m_cachedIcons.insert(path, icon.cacheKey());
    return true;
}

QPixmap AppImageStore::cachedIcon(const QUrl &launcher, const QSize &size)
{
    if (!isGeneratedAppImageLauncher(launcher)) return {};
    const auto cached = launcher.toLocalFile() + ".png";
    return QFileInfo::exists(cached) ? QIcon(cached).pixmap(size, 1.0) : QPixmap();
}

void AppImageStore::refreshDesktopCache()
{
    KSycoca::self()->ensureCacheValid();
}

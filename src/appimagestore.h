#pragma once

#include <QHash>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QUrl>

// Owns AppImage desktop metadata, associations, selection markers and artwork.
// Does not execute AppImages or manipulate the Plasma task model. Paths are
// resolved on demand so the caller's XDG environment remains authoritative.
class AppImageStore {
public:
    static bool isAppImage(const QUrl &url);
    static QString appImageDesktopPath(const QString &path, const QString &identity = {});
    static bool isGeneratedAppImageLauncher(const QUrl &url);
    static QUrl appImageLauncher(const QUrl &url, const QString &identity = {});
    static QString generatedAppImagePath(const QUrl &launcher);
    static QUrl knownAppImageLauncher(const QString &imagePath);
    static void refreshAppImageLauncher(const QUrl &url);
    static QUrl confirmedAppImageLauncher(const QString &imagePath, const QString &identity,
        const QString &name, const QIcon &icon);
    // Shared desktop-entry validation used by both ordinary and AppImage pins.
    static QUrl applicationLauncher(const QUrl &url);
    static bool needsSetup(const QUrl &launcher);
    static bool requiresSelection(const QString &imagePath);
    static QString launcherExecutable(const QUrl &launcher);
    static void updatePresentation(const QUrl &launcher, const QString &name, const QIcon &icon);
    // Confirmation clears the selection marker; automatic learning preserves it.
    static void saveAssociation(const QUrl &original, const QUrl &resolved, bool confirmed = false);
    // Flush after the adapter has removed runtime bindings: cache updates may
    // dispatch native model notifications.
    static void refreshDesktopCache();
    bool forgetAssociation(const QUrl &launcher, const QUrl &stable);
    bool cacheIcon(const QUrl &launcher, const QIcon &icon);
    static QPixmap cachedIcon(const QUrl &launcher, const QSize &size);
private:
    QHash<QString, qint64> m_cachedIcons;
};

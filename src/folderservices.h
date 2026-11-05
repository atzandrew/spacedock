#pragma once

#include <KSharedConfig>
#include <QQuickImageProvider>
#include <QUrl>
#include <QVariantList>

namespace FolderServices {
QString folderPath(const QUrl &url);
QString folderPreference(KSharedConfig::Ptr config, const QString &path, const QString &kind);
void setFolderPreference(KSharedConfig::Ptr config, const QString &path, const QString &kind, const QString &value);
QString defaultFileManagerName();
QVariantList folderContents(const QString &path);
QUrl folderLauncher(const QUrl &url);
QImage cachedFolderThumbnail(const QString &path, const QSize &pixels);
}

// Thumbnail decoding runs on Qt Quick's image-loading thread.
class FolderThumbnails : public QQuickImageProvider {
public:
    FolderThumbnails() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

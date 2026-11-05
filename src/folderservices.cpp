#include "folderservices.h"
#include "appimagestore.h"
#include <KApplicationTrader>
#include <KService>
#include <KConfig>
#include <KConfigGroup>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStandardPaths>

QString FolderServices::folderPath(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile()
        : url.scheme() == "applications" ? QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + url.path()) : QString();
    if (path.isEmpty()) return {};
    KConfig desktop(path, KConfig::SimpleConfig);
    return KConfigGroup(&desktop, "Desktop Entry").readEntry("X-Spacedock-Folder", QString());
}

QString FolderServices::folderPreference(KSharedConfig::Ptr config, const QString &path, const QString &kind)
{
    if (path.isEmpty() || (kind != QLatin1String("view") && kind != QLatin1String("sort"))) return {};
    const auto key = QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString fallback = kind == QLatin1String("view") ? QStringLiteral("grid") : QStringLiteral("name");
    const QString value = KConfigGroup(config, "FolderPreferences").readEntry(key + QLatin1Char('_') + kind, fallback);
    const QStringList valid = kind == QLatin1String("view") ? QStringList{QStringLiteral("grid"), QStringLiteral("fan"), QStringLiteral("list")} : QStringList{QStringLiteral("name"), QStringLiteral("dateAdded"), QStringLiteral("dateModified"), QStringLiteral("dateCreated"), QStringLiteral("type")};
    return valid.contains(value) ? value : fallback;
}

void FolderServices::setFolderPreference(KSharedConfig::Ptr config, const QString &path, const QString &kind, const QString &value)
{
    if (path.isEmpty() || (kind != QLatin1String("view") && kind != QLatin1String("sort"))) return;
    const QStringList valid = kind == QLatin1String("view") ? QStringList{QStringLiteral("grid"), QStringLiteral("fan"), QStringLiteral("list")} : QStringList{QStringLiteral("name"), QStringLiteral("dateAdded"), QStringLiteral("dateModified"), QStringLiteral("dateCreated"), QStringLiteral("type")};
    if (!valid.contains(value)) return;
    const auto key = QString::fromLatin1(QCryptographicHash::hash(QDir::cleanPath(path).toUtf8(), QCryptographicHash::Sha256).toHex());
    KConfigGroup group(config, "FolderPreferences");
    group.writeEntry(key + QLatin1Char('_') + kind, value);
    group.sync();
}

QString FolderServices::defaultFileManagerName()
{
    const auto service = KApplicationTrader::preferredService(QStringLiteral("inode/directory"));
    return service && !service->name().isEmpty() ? service->name() : QCoreApplication::translate("DockBackend", "File Manager");
}

QVariantList FolderServices::folderContents(const QString &path)
{
    QVariantList result;
    const auto entries = QDir(path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
        QDir::Name | QDir::IgnoreCase | QDir::DirsFirst);
    for (const auto &entry : entries)
        result.append(QVariantMap{{"name", entry.fileName()}, {"url", QUrl::fromLocalFile(entry.absoluteFilePath())},
            {"dateAdded", entry.birthTime().toMSecsSinceEpoch()},
            {"dateModified", entry.lastModified().toMSecsSinceEpoch()},
            {"dateCreated", entry.birthTime().toMSecsSinceEpoch()},
            {"type", entry.isDir() ? QStringLiteral("folder") : entry.suffix().toLower()},
            {"icon", QString::fromLatin1(entry.absoluteFilePath().toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals))}});
    return result;
}

QUrl FolderServices::folderLauncher(const QUrl &url)
{
    if (!url.isLocalFile() || !url.host().isEmpty()) return {};
    const QFileInfo folder(url.toLocalFile());
    if (!folder.isDir()) return {};
    const QString path = folder.canonicalFilePath();
    const QString id = QStringLiteral("spacedock-folder-") + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".desktop");
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
    if (!QDir().mkpath(directory)) return {};
    KConfig desktop(directory + "/" + id, KConfig::SimpleConfig);
    KConfigGroup entry(&desktop, "Desktop Entry");
    entry.writeEntry("Type", "Application");
    entry.writeEntry("Name", folder.fileName().isEmpty() ? path : folder.fileName());
    entry.writeEntry("Icon", "folder");
    entry.writeEntry("Exec", "xdg-open %u");
    entry.writeEntry("NoDisplay", true);
    entry.writeEntry("X-Spacedock-Folder", path);
    if (!desktop.sync()) return {};
    AppImageStore::refreshDesktopCache();
    return QUrl("applications:" + id);
}

// Qt folds PNG text keys containing colons together. Read the small, standard
// tEXt fields directly so KDE's Thumb:: metadata remains distinguishable.
static QHash<QByteArray, QByteArray> thumbnailMetadata(const QString &path)
{
    QFile file(path);
    QHash<QByteArray, QByteArray> result;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16 * 1024 * 1024
        || file.read(8) != QByteArray::fromHex("89504e470d0a1a0a")) return {};
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::BigEndian);
    while (!stream.atEnd()) {
        quint32 length = 0;
        stream >> length;
        const auto type = file.read(4);
        if (stream.status() != QDataStream::Ok || type.size() != 4
            || qint64(length) + 4 > file.bytesAvailable()) return {};
        if (type == "tEXt" && length <= 4096) {
            const auto chunk = file.read(length);
            const auto separator = chunk.indexOf('\0');
            if (separator > 0) result.insert(chunk.left(separator), chunk.mid(separator + 1));
        } else if (!file.seek(file.pos() + length)) return {};
        if (!file.seek(file.pos() + 4)) return {};
        if (type == "IEND") break;
    }
    return result;
}

QImage FolderServices::cachedFolderThumbnail(const QString &path, const QSize &pixels)
{
    const QFileInfo file(path);
    if (!file.isFile()) return {};
    const QString uri = QUrl::fromLocalFile(file.absoluteFilePath()).toString(QUrl::FullyEncoded);
    const auto hash = QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex();
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/thumbnails/";
    const QStringList sizes = pixels.width() <= 128 && pixels.height() <= 128
        ? QStringList{"normal", "large", "x-large", "xx-large"}
        : QStringList{"large", "x-large", "xx-large", "normal"};
    for (const auto &size : sizes) {
        const QString cached = root + size + "/" + QString::fromLatin1(hash) + ".png";
        const auto metadata = thumbnailMetadata(cached);
        const auto storedUri = metadata.value("Thumb::URI");
        // KDE's cache commonly omits URI; the filename still identifies it.
        if ((!storedUri.isEmpty() && storedUri != uri.toUtf8())
            || metadata.value("Thumb::MTime") != QByteArray::number(file.lastModified().toSecsSinceEpoch())) continue;
        const auto storedSize = metadata.value("Thumb::Size");
        if (!storedSize.isEmpty() && storedSize != QByteArray::number(file.size())) continue;
        QImageReader reader(cached, "png");
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || dimensions.width() > 1024 || dimensions.height() > 1024) continue;
        const auto image = reader.read();
        if (image.isNull()) continue;
        return image.scaled(pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return {};
}

QImage FolderThumbnails::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const auto path = QString::fromUtf8(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding));
    auto image = FolderServices::cachedFolderThumbnail(path, requestedSize.isValid() ? requestedSize : QSize(64, 64));
    if (image.isNull()) {
        // A transparent single pixel indicates that the file icon should remain visible.
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size) *size = image.size();
    return image;
}


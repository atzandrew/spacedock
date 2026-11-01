#pragma once
#include <QQuickImageProvider>
#include <QCache>
#include <QMutex>

// Cached raster shadows keep blur work out of hover/magnification animation.
class ToolTipShadows : public QQuickImageProvider {
public:
    ToolTipShadows() : QQuickImageProvider(Image, ForceAsynchronousImageLoading), m_cache(4096) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override;
private:
    QCache<QString, QImage> m_cache;
    QMutex m_mutex;
};

#pragma once
#include <QIdentityProxyModel>
#include "appimagecontroller.h"
#include "imagepicker.h"
#include "trashcontroller.h"
#include <QQuickImageProvider>
#include <QQuickItem>
#include <KSharedConfig>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QSet>
#include <QCache>
class QQmlEngine;
class QQuickWindow;

// The task-manager API is private Plasma API. Keep all task calls in this adapter.
class DockBackend : public QIdentityProxyModel, private AppImageTaskAdapter {
    Q_OBJECT
    Q_PROPERTY(TrashController* trash READ trash CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(ImagePicker* imagePicker READ imagePicker CONSTANT)
    Q_PROPERTY(bool dragTracing READ dragTracing CONSTANT)
    Q_PROPERTY(bool dropTracing READ dropTracing CONSTANT)
    Q_PROPERTY(QString taskDragMimeType READ taskDragMimeType CONSTANT)
    Q_PROPERTY(bool appImageSetupPending READ appImageSetupPending NOTIFY appImageSetupChanged)
    Q_PROPERTY(bool appImageSetupVisible READ appImageSetupVisible NOTIFY appImageSetupChanged)
    Q_PROPERTY(QString appImageSetupKey READ appImageSetupKey NOTIFY appImageSetupChanged)
    Q_PROPERTY(QString appImageSetupName READ appImageSetupName NOTIFY appImageSetupChanged)
    Q_PROPERTY(QString appImageSetupError READ appImageSetupError NOTIFY appImageSetupChanged)
    Q_PROPERTY(QVariantList appImageSetupCandidates READ appImageSetupCandidates NOTIFY appImageSetupChanged)
public:
    enum Role { Name = Qt::UserRole + 10000, Running, Pinned, Launching, Attention, Active, AppKey, NeedsAppImageSetup };
    explicit DockBackend(QQmlEngine *engine, bool sampleTasks = false, QObject *parent = nullptr, KSharedConfig::Ptr config = {});
    void setSourceModel(QAbstractItemModel *model) override;
    bool isReady() const override { return sourceModel(); }
    QString error() const { return m_error; }
    int revision() const override { return m_revision; }
    TrashController *trash() { return &m_trash; }
    ImagePicker *imagePicker() { return &m_imagePicker; }
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool addLaunchTest();
    void setTaskTracing(bool enabled);
    void setDragTracing(bool enabled) { m_traceDrag = enabled; m_dragTraceClock.start(); }
    bool dragTracing() const { return m_traceDrag; }
    bool dropTracing() const { return property("traceDrops").toBool(); }
    Q_INVOKABLE void trackDragPointer(QQuickWindow *window);
    Q_INVOKABLE void traceDrag(QQuickWindow *window, const QString &phase, const QPointF &mapped,
        const QRectF &artwork, const QPointF &grabOffset, qreal pickupProgress);
    static QString taskDragMimeType() { return QStringLiteral("application/x-spacedock-task"); }
    // Hand a lifted icon to the compositor as a native drag image, so it
    // stays locked to the cursor beyond the dock surface. size and hotSpot
    // are logical pixels. Emits taskDragFinished when the drag ends.
    Q_INVOKABLE void startTaskDrag(QQuickWindow *window, int row, const QString &appKey,
        qreal size, const QPointF &hotSpot);
    // Tests: record taskDragStarted without running a native drag.
    void setSimulatedTaskDrags(bool enabled) { m_simulateTaskDrags = enabled; }
    Q_INVOKABLE void activate(int row, bool bringAllWindows = false);
    Q_INVOKABLE void publishIconGeometry(int row, QQuickItem *item);
    Q_INVOKABLE void togglePin(int row);
    Q_INVOKABLE bool reorder(int from, int to);
    Q_INVOKABLE bool canTrashUrls(const QList<QUrl> &urls) const;
    Q_INVOKABLE bool trashUrls(const QList<QUrl> &urls);
    Q_INVOKABLE void openTrash();
    Q_INVOKABLE bool focusIconPicker(const QString &title);
    Q_INVOKABLE QString folderPath(int row) const;
    Q_INVOKABLE QString folderPreference(const QString &path, const QString &kind) const;
    Q_INVOKABLE void setFolderPreference(const QString &path, const QString &kind, const QString &value);
    Q_INVOKABLE QString defaultFileManagerName() const;
    Q_INVOKABLE QVariantList folderContents(const QString &path) const;
    Q_INVOKABLE void startFolderItemDrag(QQuickWindow *window, const QUrl &url);
    Q_INVOKABLE void openFolderItem(const QUrl &url);
    Q_INVOKABLE void openFolderPath(const QString &path);
    Q_INVOKABLE bool canPinUrls(const QList<QUrl> &urls) const;
    Q_INVOKABLE bool pinUrls(const QList<QUrl> &urls, int beforeRow);
    Q_INVOKABLE bool pinTaskAt(int row, int beforeRow);
    Q_INVOKABLE QVariantList contextEntries(int row);
    Q_INVOKABLE bool triggerContextAction(int token);
    Q_INVOKABLE void taskAction(int row, const QString &action);
    Q_INVOKABLE QVariantList iconChoices() const;
    Q_INVOKABLE bool setCustomIcon(const QString &key, const QUrl &image);
    Q_INVOKABLE bool resetCustomIcon(const QString &key);
    Q_INVOKABLE QVariantMap applicationEditorInfo(int row) const;
    Q_INVOKABLE bool saveApplicationEdit(const QString &key, const QString &name, const QUrl &image, bool restore, const QString &themeIcon = {});
    Q_INVOKABLE QPointF cursorPosition(QQuickWindow *window) const;
    static QUrl applicationLauncher(const QUrl &url);
    Q_INVOKABLE void setInputRegion(QQuickWindow *window, const QVariantList &rectangles, const QVariantList &shelfVertices);
    QPixmap icon(int row, QSize size) const;
    QPixmap defaultIcon(int row, QSize size) const;
    QString iconCacheIdentity(int row, bool defaultOnly) const;
    void cacheAppImageIcons();
    void reconcileAppImageLaunchers() { m_appImages.reconcile(); }
    bool appImageSetupPending() const { return m_appImages.pending(); }
    bool appImageSetupVisible() const { return m_appImages.visible(); }
    QString appImageSetupKey() const { return m_appImages.key(); }
    QString appImageSetupName() const { return m_appImages.name(); }
    QString appImageSetupError() const { return m_appImages.error(); }
    QVariantList appImageSetupCandidates() const { return m_appImages.candidates(); }
    Q_INVOKABLE void showAppImageSetup(int row) { m_appImages.show(row); }
    Q_INVOKABLE bool confirmAppImageSetup(const QString &candidateKey) { return m_appImages.confirm(candidateKey); }
    Q_INVOKABLE void cancelAppImageSetup() { m_appImages.cancel(); }
signals:
    void appImageReplacementPrepared(const QString &originalKey, const QString &targetKey, const QString &stableKey, int destination);
    void appImageReplacementAborted(const QString &originalKey);
    void appImageSetupChanged();
    void revisionChanged();
    void taskStructureChanged();
    void taskStructureAboutToChange();
    void taskDragStarted(int row, qreal size, const QPointF &hotSpot);
    void taskDragFinished(bool droppedOnDock);
    void folderItemDragFinished();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    TrashController m_trash;
    ImagePicker m_imagePicker;
    void invalidateSource();
    QList<QMetaObject::Connection> m_sourceConnections;
    bool m_traceDrag = false;
    bool needsAppImageSetup(const QModelIndex &index) const;
    bool m_iconCachePending = false;
    bool m_simulateTaskDrags = false;
    QQuickWindow *m_dragTraceWindow = nullptr;
    QPointF m_nativeDragPoint;
    QElapsedTimer m_dragTraceClock;
    QVariant taskData(const QModelIndex &index, const QByteArray &name) const;
    QString customIconKey(const QModelIndex &index) const;
    static QString customIconPath(const QString &key);
    KSharedConfig::Ptr m_config;
    QString m_error;
    int m_revision = 0;
    quint64 m_artworkRevision = 0;
    mutable QCache<QString, QImage> m_decodedCustomIcons{32768}; // KiB, bounded to 32 MiB.
    bool m_sampleTasks = false;
    bool m_traceTasks = false;
    QString m_lastTaskSnapshot;
    QElapsedTimer m_traceClock;
    QQmlEngine *m_engine = nullptr;
    QAbstractItemModel *m_appActions = nullptr;
    QPersistentModelIndex m_contextTask;
    QHash<int, QVariantMap> m_contextActions;
    int m_nextContextToken = 0;
    void traceTasks();
    QList<AppImageTask> appImageTasks() const override;
    AppImageTask appImageTask(const QPersistentModelIndex &handle) const override;
    QStringList appImagePins() const override;
    bool addAppImagePin(const QUrl &url, bool &added) override;
    bool replaceAppImagePins(const QStringList &pins) override;
    void refreshAppImagePins() override;
    bool moveAppImageTask(const QPersistentModelIndex &handle, int destination) override;
    bool saveAppImagePins(const QStringList &persistentPins) override;
    void invalidateAppImageArtwork() override;
    bool appImageTracing() const override { return m_traceTasks; }
    AppImageStore m_appImageStore;
    AppImageController m_appImages;
};

class TaskIcons : public QQuickImageProvider {
public:
    explicit TaskIcons(DockBackend *backend) : QQuickImageProvider(Pixmap), m_backend(backend), m_cache(32768) {}
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    DockBackend *m_backend;
    QCache<QString, QPixmap> m_cache; // Costs are KiB, bounded to 32 MiB.
};

// Thumbnail decoding runs on Qt Quick's image-loading thread.
class FolderThumbnails : public QQuickImageProvider {
public:
    FolderThumbnails() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

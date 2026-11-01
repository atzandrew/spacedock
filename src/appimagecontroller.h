#pragma once

#include "appimagestore.h"
#include <QObject>
#include <QPersistentModelIndex>
#include <QSet>
#include <QTimer>
#include <QVariantList>

// A snapshot of frontend task identity. The persistent source index is an
// opaque handle: only the adapter maps it or performs native model operations.
struct AppImageTask {
    QPersistentModelIndex handle;
    int row = -1;
    QString key;
    QString name;
    QString identity;
    QUrl launcher;
    QIcon icon;
    quint32 pid = 0;
    bool running = false;
    bool needsSetup = false;
};

class AppImageTaskAdapter {
public:
    virtual ~AppImageTaskAdapter() = default;
    virtual bool isReady() const = 0;
    virtual QList<AppImageTask> appImageTasks() const = 0;
    virtual AppImageTask appImageTask(const QPersistentModelIndex &handle) const = 0;
    virtual QStringList appImagePins() const = 0;
    virtual bool addAppImagePin(const QUrl &url, bool &added) = 0;
    virtual bool replaceAppImagePins(const QStringList &pins) = 0;
    virtual void refreshAppImagePins() = 0;
    virtual bool moveAppImageTask(const QPersistentModelIndex &handle, int destination) = 0;
    virtual bool saveAppImagePins(const QStringList &persistentPins) = 0;
    virtual int revision() const = 0;
    virtual void invalidateAppImageArtwork() = 0;
    virtual bool appImageTracing() const = 0;
};

// Owns setup, discovery and live-to-persistent associations. All native task
// operations go through the adapter; all AppImage file operations use the store.
class AppImageController : public QObject {
    Q_OBJECT
public:
    AppImageController(AppImageTaskAdapter &tasks, AppImageStore &store, bool sampleTasks,
        QObject *parent = nullptr);
    bool pending() const { return !m_setupLauncher.isEmpty(); }
    bool visible() const { return m_setupVisible; }
    QString key() const { return m_setupKey; }
    QString name() const { return m_setupName; }
    QString error() const { return m_setupError; }
    QVariantList candidates() const;
    void show(int row);
    // Returns true when activation should proceed; repeated setup clicks reveal
    // the existing session without launching the app again.
    bool prepareActivation(int row);
    bool confirm(const QString &candidateKey);
    void cancel();
    void update();
    void reconcile();
    void forget(const QUrl &launcher);
    QStringList persistentLaunchers(const QStringList &pins) const;
    QUrl launcherForPin(const QUrl &url, const QStringList &pins) const;
    static QString processAppImage(quint32 pid);
signals:
    void setupChanged();
    void replacementPrepared(const QString &originalKey, const QString &targetKey,
        const QString &stableKey, int destination);
    void replacementAborted(const QString &originalKey);
private:
    void begin(const AppImageTask &task);
    void settleConfirmedLaunchers();
    QUrl runtimeLauncher(const QUrl &persistent, const QStringList &pins) const;
    AppImageTaskAdapter &m_tasks;
    AppImageStore &m_store;
    bool m_sampleTasks;
    QString m_setupLauncher;
    QString m_setupKey;
    QString m_setupName;
    QString m_setupError;
    bool m_setupVisible = false;
    QSet<QString> m_setupExistingApps;
    QTimer m_setupTimer;
    struct RuntimeLauncher { QString launcher; QString appKey; };
    QHash<QString, RuntimeLauncher> m_confirmedRuntimeLaunchers;
    bool m_reconcilingAppImages = false;
};

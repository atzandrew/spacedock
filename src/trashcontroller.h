#pragma once
#include <QObject>
#include <QTimer>
#include <QPointer>
class QDialog;
class QQuickWindow;
#include <QFutureWatcher>

struct TrashSnapshot {
    int count = 0;
    quint64 bytes = 0;
    QString error;
};

class TrashController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool confirmationVisible READ confirmationVisible NOTIFY confirmationVisibleChanged)
    Q_PROPERTY(bool full READ full NOTIFY statusChanged)
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
public:
    explicit TrashController(QObject *parent = nullptr);
    ~TrashController() override;
    bool full() const { return m_full; }
    bool available() const { return m_available; }
    bool confirmationVisible() const { return !m_dialog.isNull(); }
    Q_INVOKABLE void showConfirmation(int count, const QString &size, QQuickWindow *parent);
    Q_INVOKABLE void focusConfirmation();
    Q_INVOKABLE void requestConfirmation();
    Q_INVOKABLE void empty();
signals:
    void statusChanged();
    void confirmationVisibleChanged();
    void confirmationReady(int count, const QString &size);
    void operationFailed(const QString &message);
private:
    void refresh(bool details);
    QPointer<QDialog> m_dialog;
    QTimer m_timer;
    QFutureWatcher<TrashSnapshot> m_watcher;
    bool m_full = false;
    bool m_available = false;
    bool m_details = false;
    bool m_pendingDetails = false;
};

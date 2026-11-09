#pragma once
#include <QObject>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

class LoginSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit LoginSettings(bool installed, QObject *parent = nullptr) : QObject(parent) {
        m_target = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + "/autostart/spacedock.desktop";
        if (!installed) return;
        const auto source = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
            "applications/spacedock.desktop");
        QFile file(source);
        if (file.open(QIODevice::ReadOnly)) m_entry = file.readAll();
        m_available = owns(m_entry);
    }
    bool available() const { return m_available; }
    bool enabled() const {
        QFile file(m_target);
        if (!file.open(QIODevice::ReadOnly)) return false;
        const auto entry = file.readAll();
        return owns(entry) && !entry.split('\n').contains("Hidden=true");
    }
    QString error() const { return m_error; }
    Q_INVOKABLE void setEnabled(bool enabled) {
        m_error.clear();
        if (!m_available) { m_error = tr("Install SpaceDock to manage login startup."); emit changed(); return; }
        QFile existing(m_target);
        if (existing.exists()) {
            if (!existing.open(QIODevice::ReadOnly) || !owns(existing.readAll())) {
                m_error = tr("The existing login entry points to another installation. Update it in Plasma Autostart settings.");
                emit changed(); return;
            }
            existing.close();
        }
        if (enabled) {
            QSaveFile file(m_target);
            if (!QDir().mkpath(QFileInfo(m_target).absolutePath()) || !file.open(QIODevice::WriteOnly)
                || file.write(m_entry) != m_entry.size() || !file.commit())
                m_error = tr("Could not save the login startup entry.");
        } else if (existing.exists() && !existing.remove()) {
            m_error = tr("Could not remove the login startup entry.");
        }
        emit changed();
    }
signals:
    void changed();
private:
    bool owns(const QByteArray &entry) const {
        auto path = QCoreApplication::applicationFilePath();
        path.replace("\\", "\\\\").replace("\"", "\\\"").replace("`", "\\`").replace("$", "\\$").replace("%", "%%");
        return entry.split('\n').contains(("Exec=\"" + path + "\"").toUtf8());
    }
    QString m_target, m_error;
    QByteArray m_entry;
    bool m_available = false;
};

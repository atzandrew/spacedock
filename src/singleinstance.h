#pragma once
#include <QDir>
#include <QDebug>
#include <QLockFile>
#include <QStandardPaths>
#include <memory>

// Both installed and development docks share this lock. Preview/test windows do not.
class SingleInstance {
public:
    bool acquire() {
        const auto runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (runtime.isEmpty()) {
            qCritical() << "Cannot find a runtime directory for the SpaceDock instance lock.";
            m_failed = true;
            return false;
        }
        m_lock = std::make_unique<QLockFile>(runtime + "/spacedock.lock");
        m_lock->setStaleLockTime(0); // A long-running dock is never stale just because of its age.
        if (m_lock->tryLock()) return true;
        m_failed = m_lock->error() != QLockFile::LockFailedError;
        if (m_failed) qCritical() << "Cannot create SpaceDock instance lock in" << runtime;
        else qInfo() << "SpaceDock is already running.";
        return false;
    }
    bool failed() const { return m_failed; }
private:
    std::unique_ptr<QLockFile> m_lock;
    bool m_failed = false;
};

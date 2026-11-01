#include "trashcontroller.h"
#include <KMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QQuickWindow>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrentRun>
#include <unistd.h>

namespace {
TrashSnapshot snapshot(bool details)
{
    TrashSnapshot result;
    QSet<QString> directories;
    directories.insert(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files"));
    const QString uid = QString::number(getuid());
    for (const auto &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady()) continue;
        const QDir root(volume.rootPath());
        directories.insert(root.filePath(QStringLiteral(".Trash/") + uid + QStringLiteral("/files")));
        directories.insert(root.filePath(QStringLiteral(".Trash-") + uid + QStringLiteral("/files")));
    }
    QSet<QString> visited;
    for (const QString &path : directories) {
        const QFileInfo info(path);
        if (!info.exists()) continue;
        const QString canonical = info.canonicalFilePath();
        if (visited.contains(canonical)) continue;
        visited.insert(canonical);
        if (!info.isDir() || !info.isReadable()) {
            result.error = QObject::tr("Cannot read trash folder %1").arg(path);
            return result;
        }
        QDir dir(path);
        const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        result.count += entries.size();
        if (!details) continue;
        for (const auto &entry : entries) {
            if (entry.isFile() && !entry.isSymLink()) result.bytes += entry.size();
            if (!entry.isDir() || entry.isSymLink()) continue;
            QDirIterator it(entry.filePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo child = it.fileInfo();
                if (child.isDir() && !child.isReadable()) {
                    result.error = QObject::tr("Cannot read trash folder %1").arg(child.filePath());
                    return result;
                }
                if (child.isFile() && !child.isSymLink()) result.bytes += child.size();
            }
        }
    }
    return result;
}
}

TrashController::TrashController(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, [this] { refresh(false); });
    connect(&m_watcher, &QFutureWatcher<TrashSnapshot>::finished, this, [this] {
        const auto result = m_watcher.result();
        const bool details = m_details;
        if (result.error.isEmpty()) {
            m_available = true;
            m_full = result.count > 0;
            emit statusChanged();
            if (details) emit confirmationReady(result.count, QLocale().formattedDataSize(qint64(result.bytes)));
        } else {
            m_available = false;
            emit statusChanged();
            if (details) emit operationFailed(tr("Could not inspect the Trash: %1").arg(result.error));
        }
        if (m_pendingDetails) { m_pendingDetails = false; refresh(true); }
    });
    m_timer.start();
    refresh(false);
}
TrashController::~TrashController() { delete m_dialog.data(); }
void TrashController::refresh(bool details)
{
    if (m_watcher.isRunning()) { if (details) m_pendingDetails = true; return; }
    m_details = details;
    m_watcher.setFuture(QtConcurrent::run([details] { return snapshot(details); }));
}
void TrashController::showConfirmation(int count, const QString &size, QQuickWindow *parent)
{
    if (m_dialog) { focusConfirmation(); return; }
    auto *dialog = new QDialog;
    dialog->setObjectName(QStringLiteral("emptyTrashDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Empty Trash?"));
    dialog->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, dialog);
    buttons->button(QDialogButtonBox::No)->setDefault(true);
    buttons->button(QDialogButtonBox::No)->setFocus();
    connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton *button) {
        if (buttons->standardButton(button) == QDialogButtonBox::Yes) empty();
    });
    const QString message = tr("Permanently delete %n item(s) from the Trash?", "", count)
        + QLatin1String("\n\n") + tr("Total file size: %1. This cannot be undone.").arg(size);
    KMessageBox::createKMessageBox(dialog, buttons, QMessageBox::Warning, message, {}, {}, nullptr,
        KMessageBox::NoExec | KMessageBox::Dangerous);
    dialog->setWindowModality(Qt::NonModal);
    dialog->winId();
    if (parent && dialog->windowHandle()) dialog->windowHandle()->setTransientParent(parent);
    m_dialog = dialog;
    connect(dialog, &QObject::destroyed, this, [this] { m_dialog = nullptr; emit confirmationVisibleChanged(); });
    emit confirmationVisibleChanged();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    buttons->button(QDialogButtonBox::No)->setFocus();
}
void TrashController::focusConfirmation()
{
    if (!m_dialog) return;
    m_dialog->showNormal();
    if (m_dialog->windowHandle()) m_dialog->windowHandle()->alert(0);
    m_dialog->raise();
    m_dialog->activateWindow();
}
void TrashController::requestConfirmation() { refresh(true); }
void TrashController::empty()
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0)
            emit operationFailed(tr("Could not empty the Trash: %1").arg(QString::fromUtf8(process->readAllStandardError()).trimmed()));
        process->deleteLater();
        refresh(false);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit operationFailed(tr("Could not empty the Trash: %1").arg(process->errorString()));
            process->deleteLater();
        }
    });
    process->start(QStringLiteral("ktrash6"), {QStringLiteral("--empty")});
}

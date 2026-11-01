#include "docklifecycle.h"
#include <QCoreApplication>
#include <QEvent>
#include <QSocketNotifier>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

namespace {
volatile sig_atomic_t shutdownFd = -1;
void handleTermination(int)
{
    const int savedErrno = errno;
    const char byte = 1;
    if (shutdownFd >= 0) {
        const auto ignored = ::write(shutdownFd, &byte, 1);
        (void)ignored;
    }
    errno = savedErrno;
}
}

DockLifecycle::DockLifecycle(QObject *content, QObject *parent)
    : QObject(parent), m_content(content)
{
    qApp->installEventFilter(this);
    connect(content, SIGNAL(shutdownFinished()), this, SLOT(finishShutdown()));
    if (::pipe2(m_pipe, O_NONBLOCK | O_CLOEXEC) == 0) {
        shutdownFd = m_pipe[1];
        auto *notifier = new QSocketNotifier(m_pipe[0], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] {
            char bytes[64];
            while (::read(m_pipe[0], bytes, sizeof(bytes)) > 0) {}
            requestShutdown();
        });
        struct sigaction action {};
        action.sa_handler = handleTermination;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
    }
}

DockLifecycle::~DockLifecycle()
{
    shutdownFd = -1;
    if (m_pipe[0] >= 0) ::close(m_pipe[0]);
    if (m_pipe[1] >= 0) ::close(m_pipe[1]);
}

bool DockLifecycle::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == qApp && event->type() == QEvent::Quit) {
        requestShutdown();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

void DockLifecycle::requestShutdown()
{
    QMetaObject::invokeMethod(m_content, "beginShutdown");
}

void DockLifecycle::finishShutdown()
{
    QCoreApplication::exit(0);
}

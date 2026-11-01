#pragma once
#include <QByteArray>

// Qt chooses a platform/shell while creating the first window. These overrides
// belong to the dock only; launch jobs must see the original desktop environment.
class StartupEnvironment {
public:
    explicit StartupEnvironment(bool enabled) : m_enabled(enabled),
        m_platform(qgetenv("QT_QPA_PLATFORM")),
        m_shell(qgetenv("QT_WAYLAND_SHELL_INTEGRATION"))
    {
        if (enabled) {
            qputenv("QT_QPA_PLATFORM", "wayland");
            // LayerShell.Window attaches layer-shell to the dock/drag surfaces.
            // Keep xdg-shell as the default so menus get real popup roles.
            qputenv("QT_WAYLAND_SHELL_INTEGRATION", "xdg-shell");
        }
    }
    ~StartupEnvironment() { restore(); }
    void restore() {
        if (!m_enabled) return;
        restoreVariable("QT_QPA_PLATFORM", m_platform);
        restoreVariable("QT_WAYLAND_SHELL_INTEGRATION", m_shell);
        m_enabled = false;
    }
    StartupEnvironment(const StartupEnvironment &) = delete;
    StartupEnvironment &operator=(const StartupEnvironment &) = delete;
private:
    static void restoreVariable(const char *name, const QByteArray &value) {
        if (value.isNull()) qunsetenv(name);
        else qputenv(name, value);
    }
    bool m_enabled;
    QByteArray m_platform;
    QByteArray m_shell;
};

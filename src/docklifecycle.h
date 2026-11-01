#pragma once

#include <QObject>

// Routes ordinary quit requests and Unix termination signals through the QML
// exit animation while the event loop and Wayland surface are still alive.
class DockLifecycle : public QObject {
    Q_OBJECT
public:
    explicit DockLifecycle(QObject *content, QObject *parent = nullptr);
    ~DockLifecycle() override;
public slots:
    void requestShutdown();
    void finishShutdown();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QObject *m_content;
    int m_pipe[2] {-1, -1};
};

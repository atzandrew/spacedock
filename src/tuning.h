#pragma once
#include <QFileSystemWatcher>
#include <QObject>
#include <QVariantMap>
#include <QTimer>

class Tuning : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap layout READ layout NOTIFY changed)
    Q_PROPERTY(QVariantMap magnification READ magnification NOTIFY changed)
    Q_PROPERTY(QVariantMap tooltip READ tooltip NOTIFY changed)
    Q_PROPERTY(QVariantMap autoHide READ autoHide NOTIFY changed)
    Q_PROPERTY(QVariantMap feedback READ feedback NOTIFY changed)
    Q_PROPERTY(QVariantMap motion READ motion NOTIFY changed)
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY changed)
    Q_PROPERTY(double defaultSize READ defaultSize NOTIFY changed)
    Q_PROPERTY(double magnificationScale READ magnificationScale WRITE setMagnificationScale NOTIFY changed)
    Q_PROPERTY(double artworkSize READ artworkSize NOTIFY changed)
    Q_PROPERTY(bool bringAllWindows READ bringAllWindows WRITE setBringAllWindows NOTIFY changed)
public:
    explicit Tuning(QString path, QObject *parent = nullptr, bool persistSettings = false);
    ~Tuning() override;
    QVariantMap layout() const;
    QVariantMap magnification() const;
    QVariantMap tooltip() const { return m_tooltip; }
    QVariantMap autoHide() const;
    QVariantMap feedback() const { return m_feedback; }
    QVariantMap motion() const { return m_motion; }
    bool reload();
    double size() const;
    double defaultSize() const { return m_layout.value("iconSize").toDouble(); }
    double artworkSize() const { return m_artworkSize > 0 ? m_artworkSize : size(); }
    Q_INVOKABLE void setSize(double value);
    Q_INVOKABLE void prepareSizeAnimation(double target);
    Q_INVOKABLE void finishSizeAnimation();
    Q_INVOKABLE void beginSizeDrag();
    Q_INVOKABLE void setDragSize(double value);
    double magnificationScale() const;
    Q_INVOKABLE void setMagnificationScale(double value);
    Q_INVOKABLE void setMagnificationEnabled(bool enabled);
    Q_INVOKABLE void setAutoHideEnabled(bool enabled);
    bool bringAllWindows() const { return m_bringAllWindows; }
    Q_INVOKABLE void setBringAllWindows(bool enabled);
signals:
    void changed();
private:
    QString m_path;
    QVariantMap m_layout;
    QVariantMap m_magnification;
    QVariantMap m_tooltip;
    QVariantMap m_feedback;
    QVariantMap m_autoHide;
    QVariantMap m_motion;
    QFileSystemWatcher m_watcher;
    bool m_persistSettings = false;
    double m_size = 0;
    double m_magnificationScale = 0;
    double m_artworkSize = 0;
    double m_animationSurfaceHeight = 0;
    int m_magnificationEnabled = -1;
    int m_autoHideEnabled = -1;
    bool m_bringAllWindows = false;
    QTimer m_saveTimer;
    void saveSettings();
    void writeSettings();
    double surfaceHeightForSize(double size) const;
};

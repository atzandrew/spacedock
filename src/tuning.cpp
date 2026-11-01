#include "tuning.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <QTimer>
#include <QDebug>
#include <QSettings>
#include <QElapsedTimer>
#include <algorithm>

Tuning::Tuning(QString path, QObject *parent, bool persistSettings) : QObject(parent), m_path(std::move(path)), m_persistSettings(persistSettings)
{
    // Always start with valid resource defaults; bad edits keep the last good values.
    QFile defaults(":/tuning.json");
    if (!defaults.open(QIODevice::ReadOnly))
        qFatal("Embedded tuning defaults are missing");
    const auto embedded = QJsonDocument::fromJson(defaults.readAll()).object();
    m_layout = embedded["layout"].toObject().toVariantMap();
    m_magnification = embedded["magnification"].toObject().toVariantMap();
    m_tooltip = embedded["tooltip"].toObject().toVariantMap();
    m_feedback = embedded["feedback"].toObject().toVariantMap();
    m_autoHide = embedded["autoHide"].toObject().toVariantMap();
    m_motion = embedded["motion"].toObject().toVariantMap();
    reload();
    if (m_persistSettings) {
        QSettings settings;
        if (settings.contains("Dock/size")) m_size = std::clamp(settings.value("Dock/size").toDouble(), 18.0, 128.0);
        if (settings.contains("Dock/magnificationScale")) {
            const double scale = settings.value("Dock/magnificationScale").toDouble();
            if (std::isfinite(scale)) m_magnificationScale = std::clamp(scale, 1.0, 4.0);
        }
        if (settings.contains("Dock/magnification")) m_magnificationEnabled = settings.value("Dock/magnification").toBool();
        if (settings.contains("Dock/autoHide")) m_autoHideEnabled = settings.value("Dock/autoHide").toBool();
        m_bringAllWindows = settings.value("Dock/bringAllWindows", false).toBool();
        m_saveTimer.setSingleShot(true);
        m_saveTimer.setInterval(250);
        connect(&m_saveTimer, &QTimer::timeout, this, &Tuning::writeSettings);
    }
    if (!m_path.startsWith(":")) {
        m_watcher.addPath(QFileInfo(m_path).absolutePath());
        auto schedule = [this] { QTimer::singleShot(50, this, [this] { reload(); }); };
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, schedule);
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
    }
}

Tuning::~Tuning() { if (m_saveTimer.isActive()) writeSettings(); }

void Tuning::writeSettings()
{
    QSettings settings;
    if (m_size > 0) settings.setValue("Dock/size", m_size);
    if (m_magnificationScale > 0) settings.setValue("Dock/magnificationScale", m_magnificationScale);
    if (m_magnificationEnabled >= 0) settings.setValue("Dock/magnification", bool(m_magnificationEnabled));
    if (m_autoHideEnabled >= 0) settings.setValue("Dock/autoHide", bool(m_autoHideEnabled));
    settings.setValue("Dock/bringAllWindows", m_bringAllWindows);
}

double Tuning::size() const { return m_size > 0 ? m_size : m_layout.value("iconSize").toDouble(); }

QVariantMap Tuning::layout() const
{
    auto result = m_layout;
    if (m_size <= 0 && m_animationSurfaceHeight <= 0 && m_magnificationScale <= 0) return result;
    const double currentSize = size();
    const double ratio = currentSize / m_layout.value("iconSize").toDouble();
    for (const auto *key : {"spacing", "padding", "shelfHeight", "shelfDepth", "perspectiveInset", "shelfLip", "iconBaselineInset"})
        result[key] = m_layout.value(key).toDouble() * ratio;
    result["iconSize"] = currentSize;
    result["surfaceHeight"] = std::max(surfaceHeightForSize(currentSize), m_animationSurfaceHeight);
    return result;
}

double Tuning::surfaceHeightForSize(double iconSize) const
{
    const double ratio = iconSize / m_layout.value("iconSize").toDouble();
    const double tooltipSpace = m_tooltip.value("enabled").toBool()
        ? m_tooltip.value("height").toDouble() + m_tooltip.value("gap").toDouble() + m_tooltip.value("margin").toDouble() : 0;
    const double bounce = m_feedback.value("bounceEnabled").toBool() ? m_feedback.value("bounceHeight").toDouble() : 0;
    const auto scales = magnification().value("scaleSamples").toList();
    const double required = std::ceil(iconSize * scales.first().toDouble()
        + m_layout.value("iconBaselineInset").toDouble() * ratio + bounce + tooltipSpace);
    return std::max({m_layout.value("surfaceHeight").toDouble(), m_layout.value("shelfHeight").toDouble() * ratio, required});
}

double Tuning::magnificationScale() const {
    return m_magnificationScale > 0 ? m_magnificationScale : m_magnification.value("scaleSamples").toList().first().toDouble();
}
QVariantMap Tuning::magnification() const {
    auto result = m_magnification;
    if (m_magnificationEnabled >= 0) result["enabled"] = bool(m_magnificationEnabled);
    if (m_magnificationScale > 0) {
        auto scales = result.value("scaleSamples").toList();
        const double peak = scales.first().toDouble();
        for (auto &scale : scales)
            scale = peak > 1 ? 1 + (scale.toDouble() - 1) * (m_magnificationScale - 1) / (peak - 1) : m_magnificationScale;
        scales.last() = 1.0;
        result["scaleSamples"] = scales;
    }
    return result;
}
QVariantMap Tuning::autoHide() const { auto result = m_autoHide; if (m_autoHideEnabled >= 0) result["enabled"] = bool(m_autoHideEnabled); return result; }

void Tuning::saveSettings()
{
    if (!m_persistSettings) return;
    m_saveTimer.start();
}
void Tuning::setSize(double value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 18.0, 128.0);
    if (m_size == value) return;
    const bool trace = qEnvironmentVariableIsSet("SPACEDOCK_PROFILE_RESIZE");
    QElapsedTimer timer;
    if (trace) timer.start();
    m_size = value; emit changed();
    const auto changedMs = trace ? timer.elapsed() : 0;
    saveSettings();
    if (trace && changedMs >= 8) qInfo() << "resize-profile changed-signal ms" << changedMs << "size" << value;
    if (trace && timer.elapsed() >= 8) qInfo() << "resize-profile set-size ms" << timer.elapsed() << "size" << value;
}
void Tuning::prepareSizeAnimation(double target) {
    if (!std::isfinite(target)) return;
    m_artworkSize = std::max(size(), std::clamp(target, 18.0, 128.0));
    m_animationSurfaceHeight = std::max(layout().value("surfaceHeight").toDouble(), surfaceHeightForSize(m_artworkSize));
    emit changed();
}
void Tuning::finishSizeAnimation() {
    if (m_artworkSize <= 0 && m_animationSurfaceHeight <= 0) return;
    m_artworkSize = 0;
    m_animationSurfaceHeight = 0;
    emit changed();
}
void Tuning::beginSizeDrag() {
    m_artworkSize = std::min(128.0, std::ceil(size() / 24.0) * 24.0);
    m_animationSurfaceHeight = std::max(layout().value("surfaceHeight").toDouble(), surfaceHeightForSize(128));
    emit changed();
}
void Tuning::setDragSize(double value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 18.0, 128.0);
    if (value > m_artworkSize)
        m_artworkSize = std::min(128.0, std::ceil(value / 24.0) * 24.0);
    setSize(value);
}
void Tuning::setMagnificationScale(double value) {
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 1.0, 4.0);
    if (m_magnificationScale == value) return;
    m_magnificationScale = value; emit changed(); saveSettings();
}
void Tuning::setMagnificationEnabled(bool enabled) {
    if (m_magnificationEnabled == int(enabled)) return;
    m_magnificationEnabled = enabled; emit changed(); saveSettings();
}
void Tuning::setAutoHideEnabled(bool enabled) {
    if (m_autoHideEnabled == int(enabled)) return;
    m_autoHideEnabled = enabled; emit changed(); saveSettings();
}
void Tuning::setBringAllWindows(bool enabled) {
    if (m_bringAllWindows == enabled) return;
    m_bringAllWindows = enabled; emit changed(); saveSettings();
}

bool Tuning::reload()
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read tuning file:" << m_path;
        return false;
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    const auto root = doc.object();
    const auto layout = root["layout"].toObject();
    bool valid = error.error == QJsonParseError::NoError && root["schemaVersion"].toInt() == 1;
    const QMap<QString, QPair<int, int>> limits {
        {"iconSize", {16, 128}}, {"spacing", {0, 64}}, {"padding", {0, 64}},
        {"surfaceHeight", {64, 512}}, {"shelfHeight", {32, 256}},
        {"shelfDepth", {12, 96}}, {"perspectiveInset", {0, 64}},
        {"shelfLip", {1, 12}}, {"iconBaselineInset", {4, 64}}
    };
    for (auto it = limits.begin(); it != limits.end(); ++it) {
        const auto value = layout[it.key()];
        valid &= value.isDouble() && value.toDouble() == value.toInt()
            && value.toInt() >= it.value().first && value.toInt() <= it.value().second;
    }
    valid &= layout["shelfHeight"].toInt() >= layout["iconSize"].toInt() + 12;
    valid &= layout["surfaceHeight"].toInt() >= layout["shelfHeight"].toInt();
    valid &= layout["shelfHeight"].toInt() >= layout["iconSize"].toInt() + layout["iconBaselineInset"].toInt();
    valid &= layout["shelfDepth"].toInt() <= layout["shelfHeight"].toInt();
    valid &= layout["iconBaselineInset"].toInt() > layout["shelfLip"].toInt();
    valid &= layout["iconBaselineInset"].toInt() < layout["shelfDepth"].toInt();
    valid &= layout["reflectionOpacity"].isDouble()
        && layout["reflectionOpacity"].toDouble() >= 0 && layout["reflectionOpacity"].toDouble() <= 1;
    valid &= layout["showIndicators"].isBool();
    const auto magnification = root["magnification"].toObject();
    valid &= magnification["enabled"].isBool();
    for (const auto &key : {"enterDurationMs", "exitDurationMs"}) {
        const auto value = magnification[key];
        valid &= value.isDouble() && value.toDouble() >= 1 && value.toDouble() <= 2000;
    }
    const auto scales = magnification["scaleSamples"].toArray();
    valid &= scales.size() >= 2 && scales.size() <= 16;
    double previous = 3;
    for (const auto &value : scales) {
        valid &= value.isDouble() && std::isfinite(value.toDouble())
            && value.toDouble() >= 1 && value.toDouble() <= previous;
        previous = value.toDouble();
    }
    valid &= !scales.isEmpty() && scales.last().toDouble() == 1;
    const auto maxScale = scales.isEmpty() ? 1 : scales.first().toDouble();
    valid &= layout["surfaceHeight"].toInt() >= std::ceil(layout["iconSize"].toInt() * maxScale)
        + layout["iconBaselineInset"].toInt();
    for (const auto &key : {"enterProgressSamples", "exitProgressSamples"}) {
        const auto points = magnification[key].toArray();
        valid &= points.size() >= 2 && points.size() <= 64;
        double last = 0;
        for (const auto &value : points) {
            valid &= value.isDouble() && std::isfinite(value.toDouble())
                && value.toDouble() >= last && value.toDouble() <= 1;
            last = value.toDouble();
        }
        valid &= !points.isEmpty() && points.first().toDouble() == 0 && points.last().toDouble() == 1;
    }
    const auto tooltip = root["tooltip"].toObject();
    const auto feedback = root["feedback"].toObject();
    auto bounded = [&](const QJsonObject &object, const char *key, double minimum, double maximum) {
        const auto value = object[key];
        return value.isDouble() && std::isfinite(value.toDouble())
            && value.toDouble() >= minimum && value.toDouble() <= maximum;
    };
    // Older tuning documents inherit auto-hide defaults.
    const auto autoHide = root.contains("autoHide") ? root["autoHide"].toObject()
        : QJsonObject::fromVariantMap(m_autoHide);
    valid &= autoHide["enabled"].isBool();
    if (autoHide.contains("hiddenScale"))
        valid &= bounded(autoHide, "hiddenScale", 0.5, 1);
    valid &= bounded(autoHide, "hideDelayMs", 0, 5000)
        && bounded(autoHide, "revealDelayMs", 0, 2000)
        && bounded(autoHide, "slideDurationMs", 1, 2000)
        && bounded(autoHide, "edgeSize", 1, 8);
    // Animation timings not owned by another section. Older documents
    // inherit the defaults.
    const auto motion = root.contains("motion") ? root["motion"].toObject()
        : QJsonObject::fromVariantMap(m_motion);
    valid &= bounded(motion, "menuHighlightFadeMs", 0, 2000)
        && bounded(motion, "partingDurationMs", 1, 2000)
        && bounded(motion, "dragPartingDelayMs", 0, 5000);
    valid &= tooltip["enabled"].isBool() && feedback["bounceEnabled"].isBool();
    valid &= bounded(tooltip, "delayMs", 0, 2000) && bounded(tooltip, "height", 16, 96)
        && bounded(tooltip, "fontPixelSize", 8, 32) && bounded(tooltip, "padding", 0, 32)
        && bounded(tooltip, "margin", 0, 32) && bounded(tooltip, "gap", 0, 32)
        && bounded(tooltip, "shadowBlur", 0, 32)
        && bounded(tooltip, "shadowStrength", 1, 12)
        && bounded(tooltip, "shadowCoreBlur", 0, 32)
        && bounded(tooltip, "shadowSourceScale", 0.25, 1) && bounded(tooltip, "shadowOffset", 0, 8);
    valid &= bounded(tooltip, "fontWeight", 100, 900);
    valid &= tooltip["fontFamily"].isString() && !tooltip["fontFamily"].toString().trimmed().isEmpty();
    valid &= tooltip["height"].toDouble() >= tooltip["fontPixelSize"].toDouble()
        + 2 * tooltip["shadowBlur"].toDouble() + tooltip["shadowOffset"].toDouble();
    valid &= tooltip["shadowCoreBlur"].toDouble() <= tooltip["shadowBlur"].toDouble();
    valid &= tooltip["padding"].toDouble() >= tooltip["shadowBlur"].toDouble();
    valid &= tooltip["margin"].toDouble() >= tooltip["shadowBlur"].toDouble();
    valid &= bounded(feedback, "bounceHeight", 0, 64) && bounded(feedback, "bounceDurationMs", 100, 2000);
    const auto bounceSamples = feedback["bounceSamples"].toArray();
    valid &= bounceSamples.size() >= 3 && bounceSamples.size() <= 64;
    for (const auto &value : bounceSamples)
        valid &= value.isDouble() && std::isfinite(value.toDouble()) && value.toDouble() >= 0 && value.toDouble() <= 1;
    valid &= !bounceSamples.isEmpty() && bounceSamples.first().toDouble() == 0 && bounceSamples.last().toDouble() == 0;
    const double bounceSpace = feedback["bounceEnabled"].toBool() ? feedback["bounceHeight"].toDouble() : 0;
    const double tooltipSpace = tooltip["enabled"].toBool()
        ? tooltip["height"].toDouble() + tooltip["gap"].toDouble() + tooltip["margin"].toDouble() : 0;
    valid &= layout["surfaceHeight"].toInt() >= std::ceil(layout["iconSize"].toInt() * maxScale)
        + layout["iconBaselineInset"].toInt() + bounceSpace + tooltipSpace;
    if (!valid) {
        qWarning() << "Invalid tuning document; keeping last valid settings:" << m_path;
        return false;
    }
    if (!m_path.startsWith(":") && !m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path); // Editors commonly save by atomic rename.
    const auto nextAutoHide = autoHide.toVariantMap();
    const auto next = layout.toVariantMap();
    const auto nextMagnification = magnification.toVariantMap();
    const auto nextTooltip = tooltip.toVariantMap();
    const auto nextFeedback = feedback.toVariantMap();
    const auto nextMotion = motion.toVariantMap();
    if (nextAutoHide != m_autoHide || next != m_layout || nextMagnification != m_magnification || nextTooltip != m_tooltip || nextFeedback != m_feedback || nextMotion != m_motion) {
        m_autoHide = nextAutoHide;
        m_layout = next;
        m_magnification = nextMagnification;
        m_tooltip = nextTooltip;
        m_feedback = nextFeedback;
        m_motion = nextMotion;
        emit changed();
    }
    return true;
}

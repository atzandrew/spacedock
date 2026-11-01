#include "tooltipshadow.h"
#include <QFont>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
// Sliding sums make each separable box pass linear in image area, regardless
// of blur radius. Three passes approximate a Gaussian without repeated glyph draws.
std::vector<float> blur(std::vector<float> values, int width, int height, double radius)
{
    if (radius <= 0) return values;
    const int r = std::max(1, int(std::round(radius / 2)));
    std::vector<float> scratch(values.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < height; ++y) {
            float sum = 0;
            for (int x = 0; x <= std::min(r, width - 1); ++x) sum += values[y * width + x];
            for (int x = 0; x < width; ++x) {
                scratch[y * width + x] = sum / (2 * r + 1);
                if (x - r >= 0) sum -= values[y * width + x - r];
                if (x + r + 1 < width) sum += values[y * width + x + r + 1];
            }
        }
        for (int x = 0; x < width; ++x) {
            float sum = 0;
            for (int y = 0; y <= std::min(r, height - 1); ++y) sum += scratch[y * width + x];
            for (int y = 0; y < height; ++y) {
                values[y * width + x] = sum / (2 * r + 1);
                if (y - r >= 0) sum -= scratch[(y - r) * width + x];
                if (y + r + 1 < height) sum += scratch[(y + r + 1) * width + x];
            }
        }
    }
    return values;
}
}

QImage ToolTipShadows::requestImage(const QString &id, QSize *size, const QSize &)
{
    QMutexLocker lock(&m_mutex);
    if (const auto *cached = m_cache.object(id)) {
        if (size) *size = cached->size();
        return *cached;
    }
    const auto spec = QJsonDocument::fromJson(QByteArray::fromPercentEncoding(id.toUtf8())).object();
    const double dpr = std::clamp(spec["dpr"].toDouble(1), 1.0, 4.0);
    const int width = std::clamp(int(std::ceil(spec["width"].toDouble() * dpr)), 1, 8192);
    const int height = std::clamp(int(std::ceil(spec["height"].toDouble() * dpr)), 1, 1024);
    QImage mask(width, height, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.scale(dpr, dpr);
        QFont font(spec["family"].toString());
        font.setPixelSize(spec["fontSize"].toInt());
        font.setWeight(QFont::Weight(spec["weight"].toInt()));
        painter.setFont(font);
        painter.setPen(QColor(0, 0, 0, 230));
        const auto text = spec["text"].toString();
        const double x = (spec["width"].toDouble() - painter.fontMetrics().horizontalAdvance(text)) / 2;
        const double baseline = spec["baseline"].toDouble() + spec["offset"].toDouble();
        const double sourceScale = std::clamp(spec["sourceScale"].toDouble(1), 0.25, 1.0);
        // Shrink only the shadow's source glyphs around the text line center.
        // The visible caption and blur radii retain their independent sizes.
        const QPointF center(spec["width"].toDouble() / 2,
            baseline + (painter.fontMetrics().descent() - painter.fontMetrics().ascent()) / 2.0);
        painter.translate(center);
        painter.scale(sourceScale, sourceScale);
        painter.translate(-center);
        painter.drawText(QPointF(x, baseline), text);
    }
    std::vector<float> alpha(width * height);
    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x = 0; x < width; ++x) alpha[y * width + x] = qAlpha(row[x]) / 255.0f;
    }
    const auto broad = blur(alpha, width, height, spec["blur"].toDouble() * dpr);
    const auto core = blur(alpha, width, height, spec["coreBlur"].toDouble() * dpr);
    const double strength = spec["strength"].toDouble();
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<QRgb *>(mask.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int i = y * width + x;
            const double a = 1 - std::pow(1 - std::clamp(double(broad[i]), 0.0, 1.0), strength)
                * std::pow(1 - std::clamp(double(core[i]), 0.0, 1.0), 2);
            row[x] = qRgba(0, 0, 0, qRound(a * 255));
        }
    }
    if (size) *size = mask.size();
    m_cache.insert(id, new QImage(mask), std::max(1, int(mask.sizeInBytes() / 1024)));
    return mask;
}

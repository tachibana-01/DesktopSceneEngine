#pragma once

#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>

namespace DesktopSceneEngine::Player::PreviewGeometry
{
// Presentation intent only. CSS viewport coordinates are converted exactly
// once using the current WebView controller's authoritative scale.
struct Intent
{
    QRectF rect;
    QSizeF viewport;
    double observedScale = 1;
    bool visible = false;
    // Current page presentation facts for native hover eligibility. The
    // composed WebView already draws above the Preview.
    bool modalActive = false;
    QVector<QRectF> floatingRects;
};

struct Placement
{
    QRect physicalRect;
    QString error;
};

inline Placement resolve(const Intent& intent, const QSize& nativeViewport, double controllerScale)
{
    constexpr double Limit = 1000000;
    const auto finiteBounded = [](double value) { return std::isfinite(value) && std::abs(value) <= Limit; };
    if (!finiteBounded(intent.rect.x()) || !finiteBounded(intent.rect.y())
        || !finiteBounded(intent.rect.width()) || !finiteBounded(intent.rect.height())
        || intent.rect.width() < 0 || intent.rect.height() < 0
        || !finiteBounded(intent.viewport.width()) || !finiteBounded(intent.viewport.height())
        || intent.viewport.width() <= 0 || intent.viewport.height() <= 0
        || !std::isfinite(intent.observedScale) || intent.observedScale < .25 || intent.observedScale > 8)
        return {{}, QStringLiteral("invalid-preview-geometry")};
    if (intent.floatingRects.size() > 32) return {{}, QStringLiteral("invalid-preview-occlusion")};
    for (const auto& rect : intent.floatingRects)
        if (!finiteBounded(rect.x()) || !finiteBounded(rect.y())
            || !finiteBounded(rect.width()) || !finiteBounded(rect.height())
            || rect.width() < 0 || rect.height() < 0)
            return {{}, QStringLiteral("invalid-preview-occlusion")};
    if (!std::isfinite(controllerScale) || controllerScale < .25 || controllerScale > 8)
        return {{}, QStringLiteral("preview-controller-scale-unavailable")};
    if (std::abs(intent.observedScale - controllerScale) > .001)
        return {{}, QStringLiteral("preview-scale-mismatch")};
    // A hidden/zero native viewport cannot safely adopt an old page's rect.
    if (nativeViewport.width() <= 0 || nativeViewport.height() <= 0) return {};
    if (std::abs(std::round(intent.viewport.width() * controllerScale) - nativeViewport.width()) > 2
        || std::abs(std::round(intent.viewport.height() * controllerScale) - nativeViewport.height()) > 2)
        return {{}, QStringLiteral("preview-viewport-mismatch")};
    if (!intent.visible || intent.rect.width() == 0 || intent.rect.height() == 0) return {};

    // Round edges, then derive extents. Do not multiply by OS DPI again.
    const auto& rect = intent.rect;
    const auto left = std::clamp(static_cast<int>(std::round(rect.x() * controllerScale)), 0, nativeViewport.width());
    const auto top = std::clamp(static_cast<int>(std::round(rect.y() * controllerScale)), 0, nativeViewport.height());
    const auto right = std::clamp(static_cast<int>(std::round((rect.x() + rect.width()) * controllerScale)), 0, nativeViewport.width());
    const auto bottom = std::clamp(static_cast<int>(std::round((rect.y() + rect.height()) * controllerScale)), 0, nativeViewport.height());
    return {{left, top, std::max(0, right - left), std::max(0, bottom - top)}, {}};
}
}

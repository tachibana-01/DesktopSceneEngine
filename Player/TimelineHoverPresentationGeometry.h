#pragma once

#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace DesktopSceneEngine::Player::TimelineHoverPresentation
{
// Current-page presentation facts only, in WebView CSS client coordinates.
// The runtime binding/source remain owned and checked by the native owner.
struct Intent
{
    QString bindingToken;
    QUuid itemId;
    quint32 serial = 0;
    double time = 0;
    QRectF rect, lane;
    QSizeF imageSize, viewport;
    double observedScale = 1;
    QString timeLabel;
};

struct Placement
{
    QRect screenRect, imageRect, timeRect;
    int borderPixels = 1;
    int fontPixels = 10;
    bool suspended = true;
    QString error;
};

inline bool validTimeLabel(const QString& text)
{
    if (text.isEmpty() || text.size() > 32) return false;
    const auto parts = text.split(QLatin1Char(':'));
    if (parts.size() != 2 && parts.size() != 3) return false;
    for (qsizetype index = 0; index < parts.size(); ++index)
    {
        const auto& part = parts[index];
        const bool minuteOrSecond = index > 0 || parts.size() == 2;
        if (minuteOrSecond ? part.size() != 2 : (part.size() < 2 || part.size() > 20)) return false;
        for (const auto character : part)
            if (character < QLatin1Char('0') || character > QLatin1Char('9')) return false;
        if (minuteOrSecond && part.toInt() > 59) return false;
    }
    return true;
}

// Validate the CSS contract before any current controller/viewport is borrowed.
inline QString validate(const Intent& intent)
{
    constexpr double Limit = 1000000;
    constexpr double Epsilon = .000001;
    const auto bounded = [](double value) { return std::isfinite(value) && std::abs(value) <= Limit; };
    const auto rectangle = [&](const QRectF& value) {
        return bounded(value.x()) && bounded(value.y()) && bounded(value.width()) && bounded(value.height())
            && value.width() > 0 && value.height() > 0;
    };
    if (!rectangle(intent.rect) || !rectangle(intent.lane)
        || !bounded(intent.viewport.width()) || !bounded(intent.viewport.height())
        || intent.viewport.width() <= 0 || intent.viewport.height() <= 0
        || !std::isfinite(intent.observedScale) || intent.observedScale < .25 || intent.observedScale > 8
        || !std::isfinite(intent.imageSize.width()) || !std::isfinite(intent.imageSize.height())
        || intent.imageSize.width() <= 0 || intent.imageSize.width() > 200 + Epsilon
        || intent.imageSize.height() <= 0 || intent.imageSize.height() > 140 + Epsilon
        || !validTimeLabel(intent.timeLabel))
        return QStringLiteral("invalid-hover-presentation-geometry");

    // The canonical image fits a 200x140 envelope. Card sizing is independent
    // of the DOM strip's remaining height; an unavailable placement never shrinks.
    if ((std::abs(intent.imageSize.width() - 200) > Epsilon && std::abs(intent.imageSize.height() - 140) > Epsilon)
        || std::abs(intent.rect.width() - std::max(112.0, intent.imageSize.width() + 16)) > Epsilon
        || std::abs(intent.rect.height() - intent.imageSize.height() - 40) > Epsilon)
        return QStringLiteral("invalid-hover-canonical-size");

    if (intent.lane.left() < 0 || intent.lane.top() < 0
        || intent.lane.right() > intent.viewport.width() + Epsilon
        || intent.lane.bottom() > intent.viewport.height() + Epsilon
        || intent.rect.left() < intent.lane.left() + 8 - Epsilon
        || intent.rect.right() > intent.lane.right() - 8 + Epsilon
        || intent.rect.top() < 0 || intent.rect.bottom() > intent.viewport.height() + Epsilon
        || intent.rect.bottom() > intent.lane.bottom() + Epsilon)
        return QStringLiteral("hover-card-does-not-fit");
    return {};
}

// This is the sole CSS->physical conversion. screenOrigin is already physical,
// obtained from the current WebView Host client; never multiply it by OS DPI.
inline Placement resolve(const Intent& intent, const QSize& nativeViewport,
    double controllerScale, const QPoint& screenOrigin)
{
    if (const auto error = validate(intent); !error.isEmpty()) return {{}, {}, {}, 1, 10, true, error};
    if (!std::isfinite(controllerScale) || controllerScale < .25 || controllerScale > 8)
        return {{}, {}, {}, 1, 10, true, QStringLiteral("hover-controller-scale-unavailable")};
    if (std::abs(intent.observedScale - controllerScale) > .001)
        return {{}, {}, {}, 1, 10, true, QStringLiteral("hover-scale-mismatch")};
    if (nativeViewport.width() <= 0 || nativeViewport.height() <= 0) return {};
    if (std::abs(std::round(intent.viewport.width() * controllerScale) - nativeViewport.width()) > 2
        || std::abs(std::round(intent.viewport.height() * controllerScale) - nativeViewport.height()) > 2)
        return {{}, {}, {}, 1, 10, true, QStringLiteral("hover-viewport-mismatch")};

    const auto edge = [&](double value) { return static_cast<int>(std::lround(value * controllerScale)); };
    const int left = edge(intent.rect.left()), top = edge(intent.rect.top());
    const int right = edge(intent.rect.right()), bottom = edge(intent.rect.bottom());
    const qint64 screenLeft = qint64(screenOrigin.x()) + left, screenTop = qint64(screenOrigin.y()) + top;
    const qint64 screenRight = qint64(screenOrigin.x()) + right, screenBottom = qint64(screenOrigin.y()) + bottom;
    const auto fitsInt = [](qint64 value) {
        return value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max();
    };
    if (!fitsInt(screenLeft) || !fitsInt(screenTop) || !fitsInt(screenRight) || !fitsInt(screenBottom))
        return {{}, {}, {}, 1, 10, true, QStringLiteral("hover-screen-coordinate-overflow")};
    const double imageLeft = intent.rect.left() + (intent.rect.width() - intent.imageSize.width()) / 2;
    const double imageTop = intent.rect.top() + 8;
    const int imageX = edge(imageLeft) - left, imageY = edge(imageTop) - top;
    const int timeX = edge(intent.rect.left() + 8) - left;
    const int timeY = edge(imageTop + intent.imageSize.height()) - top;
    Placement result;
    result.screenRect = QRect{static_cast<int>(screenLeft), static_cast<int>(screenTop), right - left, bottom - top};
    result.imageRect = QRect{imageX, imageY,
        std::max(1, edge(imageLeft + intent.imageSize.width()) - left - imageX),
        std::max(1, edge(imageTop + intent.imageSize.height()) - top - imageY)};
    result.timeRect = QRect{timeX, timeY, edge(intent.rect.right() - 8) - left - timeX,
        edge(imageTop + intent.imageSize.height() + 24) - top - timeY};
    result.borderPixels = std::max(1, edge(1));
    result.fontPixels = std::max(1, edge(10));
    result.suspended = false;
    return result;
}
}

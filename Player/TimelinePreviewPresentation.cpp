#include "TimelinePreviewPresentation.h"

#include <DesktopSceneEngine/Presentation/PresentationGeometry.h>

#include <QColor>
#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace
{

constexpr QSize maximumTimelinePreviewCanvasSize{224, 140};
const QColor timelineCanvasBackground{26, 26, 26};

}

namespace DesktopSceneEngine::Player
{

TimelinePreviewImageRequestDecision decideTimelinePreviewImageRequest(
    const TimelinePreviewImageIdentity& requested,
    const std::optional<TimelinePreviewImageIdentity>& displayed,
    const std::optional<TimelinePreviewImageIdentity>& pending
) noexcept
{
    const bool displayedMatches = displayed.has_value() && *displayed == requested;
    const bool pendingMatches = pending.has_value() && *pending == requested;
    const bool displayedMatchesSource = displayed.has_value()
        && displayed->source == requested.source;

    if (displayedMatches)
    {
        return {false, pending.has_value() && !pendingMatches, true};
    }

    if (pendingMatches)
    {
        return {false, false, displayedMatchesSource};
    }

    return {true, pending.has_value(), displayedMatchesSource};
}

bool isTimelinePreviewFrameCurrent(
    const TimelinePreviewDisplayedFrame& displayed,
    const TimelinePreviewDisplayedFrame::Identity& requested,
    std::uint64_t presentationGeneration
) noexcept
{
    if (displayed.presentationGeneration != presentationGeneration
        || displayed.identity.source != requested.source
        || displayed.identity.generation != requested.generation)
    {
        return false;
    }

    if (requested.source.mediaType == WorkspaceMediaType::Image)
    {
        return true;
    }

    if (displayed.identity.representativeTimestampMilliseconds.has_value()
        && requested.representativeTimestampMilliseconds.has_value())
    {
        return displayed.identity.representativeTimestampMilliseconds
            == requested.representativeTimestampMilliseconds;
    }

    return displayed.identity.serial == requested.serial
        && displayed.identity.timeSeconds == requested.timeSeconds;
}

QSize timelinePreviewPopupCanvasSize(const Domain::AspectRatio& canvasAspect) noexcept
{
    if (!canvasAspect.isValid())
    {
        return {};
    }

    const double widthScale = static_cast<double>(maximumTimelinePreviewCanvasSize.width())
        / static_cast<double>(canvasAspect.numerator);
    const double heightScale = static_cast<double>(maximumTimelinePreviewCanvasSize.height())
        / static_cast<double>(canvasAspect.denominator);
    const double scale = (std::min)(widthScale, heightScale);
    const int width = (std::max)(
        1,
        static_cast<int>(std::lround(static_cast<double>(canvasAspect.numerator) * scale)));
    const int height = (std::max)(
        1,
        static_cast<int>(std::lround(static_cast<double>(canvasAspect.denominator) * scale)));
    return {width, height};
}

QImage composeTimelineCanvasPreview(
    const QImage& sourceFrame,
    const Domain::Scene& activeScene,
    const QSize& popupCanvasSize
)
{
    if (sourceFrame.isNull() || popupCanvasSize.width() <= 0
        || popupCanvasSize.height() <= 0)
    {
        return {};
    }

    const Presentation::PresentationGeometryInput input{
        sourceFrame.width(),
        sourceFrame.height(),
        popupCanvasSize.width(),
        popupCanvasSize.height(),
        activeScene.crop,
        activeScene.layout,
        activeScene.presentation};
    const std::optional<Presentation::ResolvedPresentationGeometry> geometry =
        Presentation::resolvePresentationGeometry(input);
    if (!geometry.has_value())
    {
        return {};
    }

    QImage canvas{popupCanvasSize, QImage::Format_RGBA8888};
    canvas.fill(timelineCanvasBackground);

    QPainter painter{&canvas};
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(QRectF{0.0, 0.0,
        static_cast<double>(popupCanvasSize.width()),
        static_cast<double>(popupCanvasSize.height())});
    const QRectF sourceRect{
        static_cast<double>(geometry->sourceUv.x) * sourceFrame.width(),
        static_cast<double>(geometry->sourceUv.y) * sourceFrame.height(),
        static_cast<double>(geometry->sourceUv.width) * sourceFrame.width(),
        static_cast<double>(geometry->sourceUv.height) * sourceFrame.height()};
    const QRectF destinationRect{
        static_cast<double>(geometry->destination.x) * popupCanvasSize.width(),
        static_cast<double>(geometry->destination.y) * popupCanvasSize.height(),
        static_cast<double>(geometry->destination.width) * popupCanvasSize.width(),
        static_cast<double>(geometry->destination.height) * popupCanvasSize.height()};
    painter.drawImage(destinationRect, sourceFrame, sourceRect);
    painter.end();
    return canvas;
}

}

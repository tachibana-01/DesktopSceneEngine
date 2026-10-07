#pragma once

#include "TimelinePreviewSource.h"

#include <DesktopSceneEngine/Domain/AspectRatio.h>
#include <DesktopSceneEngine/Domain/Scene.h>

#include <QImage>
#include <QSize>

#include <cstdint>
#include <optional>

namespace DesktopSceneEngine::Player
{

// Video preview content is identified by its source plus the persistent
// representative-frame timestamp, not by the raw hover position.
struct TimelinePreviewImageIdentity
{
    TimelinePreviewSource source{};
    std::int64_t representativeTimestampMilliseconds = 0;

    [[nodiscard]] bool operator==(const TimelinePreviewImageIdentity&) const noexcept = default;
};

struct TimelinePreviewImageRequestDecision
{
    bool shouldSubmit = false;
    bool shouldCancelPending = false;
    bool retainDisplayedImage = false;
};

// Decides only Player-side image-request state. Raw hover time and popup
// geometry deliberately remain outside this content-identity boundary.
[[nodiscard]] TimelinePreviewImageRequestDecision decideTimelinePreviewImageRequest(
    const TimelinePreviewImageIdentity& requested,
    const std::optional<TimelinePreviewImageIdentity>& displayed,
    const std::optional<TimelinePreviewImageIdentity>& pending
) noexcept;

// A valid raw frame is paired with the request that produced it and the GUI-side
// presentation generation used to compose it for the hover popup.
struct TimelinePreviewDisplayedFrame
{
    struct Identity
    {
        TimelinePreviewSource source{};
        float timeSeconds = 0.0F;
        std::uint64_t generation = 0U;
        std::uint64_t serial = 0U;
        std::optional<std::int64_t> representativeTimestampMilliseconds{};
    } identity{};
    std::uint64_t presentationGeneration = 0U;
};

// Video frames are valid only for the exact hover request. A static image has
// identical content at every synthetic timeline time, so it can remain visible
// while its timestamp changes.
[[nodiscard]] bool isTimelinePreviewFrameCurrent(
    const TimelinePreviewDisplayedFrame& displayed,
    const TimelinePreviewDisplayedFrame::Identity& requested,
    std::uint64_t presentationGeneration
) noexcept;

// Returns a bounded popup canvas that preserves the logical Desktop Canvas
// aspect rather than the incidental Preview widget geometry.
[[nodiscard]] QSize timelinePreviewPopupCanvasSize(
    const Domain::AspectRatio& canvasAspect
) noexcept;

// Produces an owning, reduced Desktop Canvas image from a source-derived
// representative frame. Scene presentation is resolved by the shared public
// Engine contract; source retrieval and cache identity remain outside this seam.
[[nodiscard]] QImage composeTimelineCanvasPreview(
    const QImage& sourceFrame,
    const Domain::Scene& activeScene,
    const QSize& popupCanvasSize
);

}
